/**
 *  Copyright 2010 - 2012 Hewlett-Packard Development Company, L.P.
 * 
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 */


#include "TimePrefsHandler.h"

#include <glib.h>
#include <stdlib.h>
#include <sys/time.h>
#include <time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <memory.h>
#include <set>

#if defined(HAVE_LUNA_PREFS)
#include <lunaprefs.h>
#endif

#include "NetworkConnectionListener.h"
#include "PrefsDb.h"
#include "PrefsFactory.h"
#include "Logging.h"
#include "Utils.h"
#include "JSONUtils.h"

#include <cjson/json_util.h>
#include <json_utils.h>

static const char*	  s_tzFile	=	"/usr/palm/ext-timezones.json";
static const char*    s_tzFilePath = "/var/luna/preferences/localtime";
static const char*    s_zoneInfoFolder = "/usr/share/zoneinfo/";
static const int      s_sysTimeNotificationThreshold = 3000; // 5 mins
static const char*    s_logChannel = "TimePrefsHandler";

#define				  	ORIGIN_NITZ			"nitz"
#define					HOURFORMAT_12		"HH12"
#define					HOURFORMAT_24		"HH24"

#define					NITZVALIDITY_STATE_NITZVALID					"NITZVALID"
#define					NITZVALIDITY_STATE_NITZINVALIDUSERNOTSET		"NITZINVALID_USERNOTSET"
#define					NITZVALIDITY_STATE_NITZINVALIDUSERSET			"NITZINVALID_USERSET"

#define DEBUG_TIMEPREFS 1

#define ABSV(x) ((x) < 0 ? (x*-1) : (x))
#define DIFFTIME(x,y) ((x) > (y) ? ((x)-(y)) : ((y)-(x)))
#define LSREPORT(lse) g_critical( "in %s: %s => %s", __func__, \
        (lse).func, (lse).message )

#define TIMEOUT_INTERVAL_SEC	5

json_object * TimePrefsHandler::s_timeZonesJson = NULL;
TimePrefsHandler * TimePrefsHandler::s_inst = NULL;

extern GMainLoop * g_gmainLoop;

extern char *strptime (__const char *__restrict __s,
		       __const char *__restrict __fmt, struct tm *__tp);

static void
print_tzvars (void)
{
    g_debug ("tzname[0]='%s' tzname[1]='%s' daylight='%d' timezone='%ld'", tzname[0], tzname[1], daylight, timezone);
}


static void
set_tz(const char * tz) {
    static gchar * env = NULL;
    g_free(env);
    env = g_strdup_printf("TZ=%s", tz);
    putenv(env);
    tzset();
    
    g_debug("%s: tz set to %s", __func__, tz);

    print_tzvars();
}

static bool
tz_exists(const char* tz_name) {
#define ZONEINFO_PATH_PREFIX "/usr/share/zoneinfo/"
    g_return_val_if_fail(tz_name != NULL, false);
    g_return_val_if_fail(strlen(tz_name) > 1, false);
    g_return_val_if_fail(tz_name[0] != '/', false);
    g_return_val_if_fail(tz_name[0] != '.', false);
    g_return_val_if_fail(strstr(tz_name, "..") == NULL, false);

    char *path = g_build_filename(ZONEINFO_PATH_PREFIX, tz_name, NULL);
    bool ret = g_file_test(path, G_FILE_TEST_IS_REGULAR);
    g_free(path);
    return ret;
}

class TimeZoneInfo
{
public:
	TimeZoneInfo() {
		name = "";
		jsonStringValue = "";
	}

	TimeZoneInfo(const std::string& _name,const std::string& _json,int _offset,int dst)
	: name(_name) , jsonStringValue(_json) , dstSupported(dst) , offsetToUTC(_offset) , preferred(false)
	{
	}
	
	TimeZoneInfo(const TimeZoneInfo& c)
	{
		name = c.name;
		countryCode = c.countryCode;
		jsonStringValue = c.jsonStringValue;
		dstSupported = c.dstSupported;
		offsetToUTC = c.offsetToUTC;
		preferred = c.preferred;
	}
	
	TimeZoneInfo& operator=(const TimeZoneInfo& c)
	{
		if (this == &c)
			return *this;
		name = c.name;
		countryCode = c.countryCode;
		jsonStringValue = c.jsonStringValue;
		dstSupported = c.dstSupported;
		offsetToUTC = c.offsetToUTC;
		preferred = c.preferred;
		return *this;		
	}
	
	~TimeZoneInfo() {
	}

	bool operator==(const struct TimeZoneInfo& c) const {
		return (name == c.name);
	}
	std::string name;
	std::string countryCode;
	std::string jsonStringValue;
	int   	dstSupported;
    int   	offsetToUTC;
    bool 	preferred;					//if set to true, then pick this TZ is searching by offset vs any others
    int		howManyZonesForCountry;		//how many offsets (incl. this one) does this country (based on countryCode) span? e.g. USA = 9
    
};

const TimeZoneInfo TimePrefsHandler::s_failsafeDefaultZone("Etc/GMT-0",
		"{\"Country\":\"\",\"CountryCode\":\"\",\"ZoneID\":\"Etc/GMT-0\",\"City\":\"\",\"Description\":\"GMT\",\"offsetFromUTC\": 0,\"supportsDST\":0}",
		0,
		0);

///just a simple container
class PreferredZones
{
public:
	PreferredZones() : dstPref(NULL) , nonDstPref(NULL) , dstFallback(NULL) , nonDstFallback(NULL) {}
	PreferredZones(const PreferredZones& c) {
		offset = c.offset;
		dstPref = c.dstPref;
		nonDstPref = c.nonDstPref;
		dstFallback = c.dstFallback;
		nonDstFallback = c.nonDstFallback;
	}
	
	PreferredZones& operator=(const PreferredZones& c) {
		if (this == &c)
			return *this;
		offset = c.offset;
		dstPref = c.dstPref;
		nonDstPref = c.nonDstPref;
		dstFallback = c.dstFallback;
		nonDstFallback = c.nonDstFallback;
		return *this;
	}
	
	int 		   offset;
	TimeZoneInfo * dstPref;
	TimeZoneInfo * nonDstPref;
	TimeZoneInfo * dstFallback;
	TimeZoneInfo * nonDstFallback;
};

NitzParameters::NitzParameters()
: _offset(-1000) , _dst(0), _mcc(0) , _mnc(0) , _timevalid(false) , _tzvalid(false) , _dstvalid(false) , _localtimeStamp(0)
{
	memset(&_timeStruct,0,sizeof(_timeStruct));
}

NitzParameters::NitzParameters(struct tm& timeStruct,int offset,int dst,int mcc,int mnc,bool timevalid,bool tzvalid,bool dstvalid,uint32_t remotetimeStamp)
: _offset(offset) , _dst(dst), _mcc(mcc) , _mnc(mnc) , _timevalid(timevalid) , _tzvalid(tzvalid) , _dstvalid(dstvalid) , _localtimeStamp(remotetimeStamp)
{
	memcpy(&_timeStruct,&timeStruct,sizeof(timeStruct));
	_localtimeStamp = time(NULL);
}

void NitzParameters::stampTime()
{
	_localtimeStamp = time(NULL);
}

bool NitzParameters::valid(uint32_t threshold)
{
	/// not using timestamps anymore since the TIL sets the time directly
	
//	uint32_t lt = time(NULL);
//	uint32_t difft = (uint32_t)(DIFFTIME(_localtimeStamp,(uint32_t)time(NULL)));
//	g_debug("%s: object(%u) ? (%u) current time = diff: %u , threshold: %u",__FUNCTION__,_localtimeStamp,lt,difft,threshold);
//	if (difft > threshold)
//		return false;
	return true;
}

static json_object * valuesFor_useNetworkTime( TimePrefsHandler * pTimePrefsHandler);
static bool			validateFor_useNetworkTime( TimePrefsHandler * pTimePrefsHandler,const json_object* pValue);

static json_object * valuesFor_useNetworkTimeZone( TimePrefsHandler * pTimePrefsHandler);
static bool			validateFor_useNetworkTimeZone( TimePrefsHandler * pTimePrefsHandler,const json_object* pValue);

static json_object * valuesFor_timeZone( TimePrefsHandler * pTimePrefsHandler);
static bool			validateFor_timeZone( TimePrefsHandler * pTimePrefsHandler,const json_object* pValue);

static json_object * valuesFor_timeFormat( TimePrefsHandler * pTimePrefsHandler); 
static bool			validateFor_timeFormat( TimePrefsHandler * pTimePrefsHandler,const json_object* pValue);

static json_object * valuesFor_timeChangeLaunch( TimePrefsHandler * pTimePrefsHandler); 
static bool			validateFor_timeChangeLaunch( TimePrefsHandler * pTimePrefsHandler,const json_object* pValue);

static json_object * presetValues_boolean();

static void tzsetWorkaround(const char * newTZ) __attribute__((unused));

static LSMethod s_methods[]  = {
	{ "setSystemTime",     TimePrefsHandler::cbSetSystemTime },
	{ "setSystemNetworkTime", TimePrefsHandler::cbSetSystemNetworkTime},
	{ "getSystemTime",     TimePrefsHandler::cbGetSystemTime },
	{ "getSystemTimezoneFile", TimePrefsHandler::cbGetSystemTimezoneFile},
	{ "setTimeChangeLaunch",	TimePrefsHandler::cbSetTimeChangeLaunch},
	{ "launchTimeChangeApps", TimePrefsHandler::cbLaunchTimeChangeApps},
	{ "getNTPTime",			TimePrefsHandler::cbGetNTPTime},
	{ "setTimeWithNTP",		TimePrefsHandler::cbSetPeriodicWakeupPowerDResponse},
	{ "convertDate",		TimePrefsHandler::cbConvertDate},
    { 0, 0 },
};

typedef json_object * (*valuesForKeyFnPtr)(TimePrefsHandler * pTimePrefsHandler);
typedef bool (*validateForKeyFnPtr)(TimePrefsHandler * pTimePrefsHandler,const json_object* pValue);

typedef struct timePrefKey_s {
	const char * keyName;
	valuesForKeyFnPtr valuesFn;
	validateForKeyFnPtr validateFn;
} TimePrefKey;

static const TimePrefKey timePrefKeys[] = {
	{"useNetworkTime" , valuesFor_useNetworkTime , validateFor_useNetworkTime},
	{"useNetworkTimeZone" , valuesFor_useNetworkTimeZone , validateFor_useNetworkTimeZone},
	{"timeZone", valuesFor_timeZone , validateFor_timeZone},
	{"timeFormat", valuesFor_timeFormat , validateFor_timeFormat},
	{"timeChangeLaunch", valuesFor_timeChangeLaunch, validateFor_timeChangeLaunch},
	{"nitzValidity",NULL,NULL}
};

static inline bool isSpaceOrNull(char v) {
	return (v == 0 || isspace(v));
}

static time_t PrvCurrentUpTimeSeconds()
{
#if !defined(DESKTOP)
	// FIXME: CLOCK_UPTIME doesn't work
	struct timespec currTime;
	::clock_gettime(CLOCK_MONOTONIC, &currTime);   
	return currTime.tv_sec;
#else	
	struct timespec currTime;
	::clock_gettime(CLOCK_MONOTONIC, &currTime);   
	return currTime.tv_sec;
#endif
}


TimePrefsHandler::TimePrefsHandler(LSPalmService* service)
	: PrefsHandler(service)
	, m_cpCurrentTimeZone(0)
	, m_pDefaultTimeZone(0)
	, m_nitzSetting(TimePrefsHandler::NITZ_TimeEnable | TimePrefsHandler::NITZ_TZEnable)
	, m_lastNitzValidity(TimePrefsHandler::NITZ_Unknown)
	, m_immNitzTimeValid(false)
    , m_immNitzZoneValid(false)
	, m_p_lastNitzParameter(0)
	, m_lastNitzFlags(0)
	, m_gsource_periodic(NULL)
    , m_gsource_periodic_id(0)
    , m_timeoutCycleCount(0)
    , m_sendWakeupSetToPowerD(true)
	, m_lastNtpUpdate(0)
    , m_nitzTimeZoneAvailable(true)
{
	if (!s_inst)
		s_inst=this;
	
	init();    
}

TimePrefsHandler::~TimePrefsHandler()
{
    
}

std::list<std::string> TimePrefsHandler::keys() const
{
    return m_keyList; 
}

bool TimePrefsHandler::validate(const std::string& key, json_object* value)
{
	if (value == NULL)
		return false;
	
	for (size_t i=0;i<sizeof(timePrefKeys)/sizeof(TimePrefKey);i++) {
		if (key == timePrefKeys[i].keyName) {
			if (timePrefKeys[i].validateFn != NULL)
				return ((*(timePrefKeys[i].validateFn))(this,value));
			else
				return false;
		}
	}
	
    return false;
}

void TimePrefsHandler::valueChanged(const std::string& key, json_object* value)
{
	bool bval;
	std::string strval;
	
	if (key == "useNetworkTime") {
		if (value) {
			bval = json_object_get_boolean(value);
		}
		else {
			bval = true;
		}
		setNITZTimeEnable(bval);
		if (bval)
		{
			//kick off an update cycle right now (* see the function for restrictions; It won't do anything in some cases)
			startBootstrapCycle(3);
		}
	}
	else if (key == "useNetworkTimeZone") {
		if (value) {
			bval = json_object_get_boolean(value);
		}
		else {
			bval = true;
		}
		setNITZTZEnable(bval);
	}
	else if (key == "timeZone") {
		//TODO: change tz
		if (value) {
			strval = TimePrefsHandler::tzNameFromJsonValue(value);
			g_warning("TimePrefsHandler::valueChanged(): attempted change of timeZone to [%s]",strval.c_str());
			const TimeZoneInfo * new_mcTZ = timeZone_ZoneFromName(strval.c_str());
			if (new_mcTZ) {
				if (*new_mcTZ == *m_cpCurrentTimeZone) {
					g_warning("TimePrefsHandler::valueChanged(): new and old timezones are the same...skipping the rest of the change procedure");
					return;
				}
				m_cpCurrentTimeZone = new_mcTZ;
			}
			
			if (m_cpCurrentTimeZone) {
				g_warning("%s: successfully mapped to zone [%s]",__FUNCTION__,m_cpCurrentTimeZone->name.c_str());
				setTimeZone(m_cpCurrentTimeZone);
			}
			else {
				int currOffsetToUTC = offsetToUtcSecs()/60;
				//last chance to get a valid timezone given the offset

				m_cpCurrentTimeZone = timeZone_ZoneFromOffset(currOffsetToUTC);
				if (m_cpCurrentTimeZone == NULL)
				{
					g_warning("%s: Couldn't pick timezone from offset %d ... picking a generic zone based on offset",__FUNCTION__,currOffsetToUTC);
					//STILL NULL! pick a generic zone
					m_cpCurrentTimeZone = timeZone_GenericZoneFromOffset(currOffsetToUTC);
					if (m_cpCurrentTimeZone == NULL)
					{
						g_warning("%s: Couldn't pick GENERIC timezone from offset %d ... last resort: go to default zone",__FUNCTION__,currOffsetToUTC);
						//This should never happen unless the syszone list is corrupt. But if it is, pick the failsafe default
						m_cpCurrentTimeZone = &s_failsafeDefaultZone;
					}
				}
				setTimeZone(m_cpCurrentTimeZone);
			}
			
			transitionNITZValidState((this->getLastNITZValidity() == TimePrefsHandler::NITZ_Valid),true);
			
			postSystemTimeChange();	
			//launch any apps that wanted to be launched when the time/zone changed
			launchAppsOnTimeChange();
		}
		else {
			g_warning("TimePrefsHandler::valueChanged(): attempted change of timeZone but no value provided");
		}
	}
	else if (key == "timeFormat") {
		//TODO: change tformat
		if (value) {
			strval = json_object_get_string(value);
			g_warning("TimePrefsHandler::valueChanged(): attempted change of timeFormat to [%s]",strval.c_str());
		}
		else {
			g_warning("TimePrefsHandler::valueChanged(): attempted change of timeFormat but no value provided");
		}
	}
	
	g_warning("valueChanged: useNetworkTime is [%s] , useNetworkTimeZone is [%s]",
			(this->isNITZTimeEnabled() ? "true" : "false"),(this->isNITZTZEnabled() ? "true" : "false"));
}

json_object* TimePrefsHandler::valuesForKey(const std::string& key)
{
	
	json_object * ro = NULL;
	for (size_t i=0;i<sizeof(timePrefKeys)/sizeof(TimePrefKey);i++) {
		if ((key == timePrefKeys[i].keyName) && (timePrefKeys[i].valuesFn != NULL)) {
			ro = ((*(timePrefKeys[i].valuesFn))(this));
			break;
		}
		else if ((key == timePrefKeys[i].keyName) && (timePrefKeys[i].valuesFn == NULL))
			break;
	}
	
	if (ro) {
		return ro;
	}
	
	//else, a default return object is returned
	ro = json_object_new_object();
	return ro;	
}

json_object * TimePrefsHandler::timeZoneListAsJson() {
	if (TimePrefsHandler::s_timeZonesJson != NULL)
		return json_object_get(TimePrefsHandler::s_timeZonesJson);		//"copy" it!
	
	return (NULL);
}

bool TimePrefsHandler::isValidTimeZoneName(const std::string& tzName) {
	
	if (TimePrefsHandler::s_timeZonesJson == NULL)
		return false;

	json_object * label = json_object_object_get(TimePrefsHandler::s_timeZonesJson, "timeZone");
	if (!label || is_error(label)) {
		return false;
	}

	array_list * srcJsonArray = json_object_get_array(label);
	if ((!srcJsonArray) || (is_error(srcJsonArray))) {
		return false;
	}
	for (int i = 0; i < array_list_length(srcJsonArray); i++) {
		json_object* obj = (json_object*) array_list_get_idx(srcJsonArray, i);

		//parse out the TZ name
		label = json_object_object_get(obj,(char*)"ZoneID");
		if (!label) {
			continue;
		}
		const char * tzname = json_object_get_string(label);
		if (tzname == NULL) {
			continue;
		}
		if (tzName == tzname)
			return true;
	}

	label = json_object_object_get(TimePrefsHandler::s_timeZonesJson, "syszones");
	if (!label || is_error(label)) {
		return false;
	}

	srcJsonArray = json_object_get_array(label);
	if ((!srcJsonArray) || (is_error(srcJsonArray))) {
		return false;
	}
	for (int i = 0; i < array_list_length(srcJsonArray); i++) {
		json_object* obj = (json_object*) array_list_get_idx(srcJsonArray, i);

		//parse out the TZ name
		label = json_object_object_get(obj,(char*)"ZoneID");
		if (!label) {
			continue;
		}
		const char * tzname = json_object_get_string(label);
		if (tzname == NULL) {
			continue;
		}
		if (tzName == tzname)
			return true;
	}

	return false;
			
}

static json_object * presetValues_boolean() {
	
	json_object* arrayObj = json_object_new_array();
	json_object_array_add(arrayObj,json_object_new_boolean(true));
	json_object_array_add(arrayObj,json_object_new_boolean(false));

	return arrayObj;
}

static json_object * valuesFor_useNetworkTime( TimePrefsHandler * pTimePrefsHandler) {
	
	json_object* json = json_object_new_object();
	json_object_object_add(json,(char *)"useNetworkTime",presetValues_boolean());
	return json;
		
}

static bool validateFor_useNetworkTime( TimePrefsHandler * pTimePrefsHandler,const json_object* pValue) {
	
	if (json_object_get_type(const_cast<json_object*>(pValue)) != json_type_boolean)
		return false;
	return true;
}

static json_object * valuesFor_useNetworkTimeZone( TimePrefsHandler * pTimePrefsHandler) {
	
	json_object* json = json_object_new_object();
	json_object_object_add(json,(char *)"useNetworkTimeZone",presetValues_boolean());
	return json;
}

static bool validateFor_useNetworkTimeZone( TimePrefsHandler * pTimePrefsHandler,const json_object* pValue) {
	
	if (json_object_get_type(const_cast<json_object*>(pValue)) != json_type_boolean)
			return false;
		return true;
}

static json_object * valuesFor_timeZone( TimePrefsHandler * pTimePrefsHandler) {
	
	if (pTimePrefsHandler == NULL)
		return (json_object_new_object());
	
	json_object * json = pTimePrefsHandler->timeZoneListAsJson();
	if (json == NULL)
		return (json_object_new_object());
	
	return json;
}

static bool validateFor_timeZone( TimePrefsHandler * pTimePrefsHandler,const json_object* pValue) {
	
	if (pTimePrefsHandler == NULL)
		return false;
	
	json_object * pval = const_cast<json_object*>(pValue);
	
	if (json_object_get_type(pval) != json_type_object)
			return false;

	json_object* label = 0;
	std::string tzname;
	
	label = json_object_object_get(pval, "ZoneID");
	if (!label) {
		return false;
	}

	tzname = json_object_get_string(label);
	
	bool rv = pTimePrefsHandler->isValidTimeZoneName(tzname);
	return rv;			//broken out for debugging ease
}

/*
 * Scans the json timezone array (built in init()) for "default" on an entry and returns that json object as
 * a string
 */
std::string TimePrefsHandler::getDefaultTZFromJson(TimeZoneInfo * r_pZoneInfo)
{
	
	if (s_timeZonesJson == NULL)
	{
		if (r_pZoneInfo)
			*r_pZoneInfo = s_failsafeDefaultZone;
		return s_failsafeDefaultZone.jsonStringValue;
	}
	
	json_object * label = json_object_object_get(s_timeZonesJson, "timeZone");
	if (!label || is_error(label)) {
		g_warning("error on json object: it doesn't contain a timezones array");
		if (r_pZoneInfo)
			*r_pZoneInfo = s_failsafeDefaultZone;
		return s_failsafeDefaultZone.jsonStringValue;
	}

	array_list * srcJsonArray = json_object_get_array(label);
	if ((!srcJsonArray) || (is_error(srcJsonArray))) {
		g_warning("error on json object: it doesn't contain a timezones array");
		if (r_pZoneInfo)
			*r_pZoneInfo = s_failsafeDefaultZone;
		return s_failsafeDefaultZone.jsonStringValue;
	}	
	for (int i = 0; i < array_list_length(srcJsonArray); i++) {
		json_object* obj = (json_object*) array_list_get_idx(srcJsonArray, i);

		//look for "default" boolean
		label = json_object_object_get(obj,(char*)"default");
		if (!label)
			continue;
		
		//found it - I actually don't care if it's true or false...its mere existence is enough to 
		//consider this a default. 
		
		if (r_pZoneInfo)
		{
			if (TimePrefsHandler::jsonUtil_ZoneFromJson(obj,*r_pZoneInfo) == false)
			{
				*r_pZoneInfo = s_failsafeDefaultZone;
				return (s_failsafeDefaultZone.jsonStringValue);
			}
		}
		
		return (r_pZoneInfo->jsonStringValue);
		
	}
	
	if (r_pZoneInfo)
		*r_pZoneInfo = s_failsafeDefaultZone;
	return s_failsafeDefaultZone.jsonStringValue;
}

//static
/*
 * reads the nitzValidity key from the db and decides what the next state will be, writes that back to the db
 * returns the previous state
 * 
 */
std::string TimePrefsHandler::transitionNITZValidState(bool nitzValid,bool userSetTime)
{
	std::string currentState = PrefsDb::instance()->getPref("nitzValidity");
	std::string nextState;
	if ((currentState == NITZVALIDITY_STATE_NITZVALID) || (currentState.size() == 0)) {
		if (nitzValid == false)
			nextState = NITZVALIDITY_STATE_NITZINVALIDUSERNOTSET;
		else
			nextState = NITZVALIDITY_STATE_NITZVALID;
	}
	else if (currentState == NITZVALIDITY_STATE_NITZINVALIDUSERNOTSET) {
		if (userSetTime)
			nextState = NITZVALIDITY_STATE_NITZINVALIDUSERSET;
		else if (nitzValid)
			nextState = NITZVALIDITY_STATE_NITZVALID;
		else 
			nextState = NITZVALIDITY_STATE_NITZINVALIDUSERNOTSET;
	}
	else if (currentState == NITZVALIDITY_STATE_NITZINVALIDUSERSET) {
		if (nitzValid)
			nextState = NITZVALIDITY_STATE_NITZVALID;
		else
			nextState = NITZVALIDITY_STATE_NITZINVALIDUSERSET;
	}
	else {
		//confused? weird state...default to NITZVALID
		nextState = NITZVALIDITY_STATE_NITZVALID;
	}
	
	PrefsDb::instance()->setPref("nitzValidity",nextState);
	g_warning("transitionNITZValidState(): transitioning [%s] -> [%s]",currentState.c_str(),nextState.c_str());
	
	return currentState;
}

static json_object * valuesFor_timeFormat( TimePrefsHandler * pTimePrefsHandler) {
	
	json_object* json = json_object_new_object();
	json_object* arrayObj = json_object_new_array();
	json_object_array_add(arrayObj,json_object_new_string((char *)"HH12"));
	json_object_array_add(arrayObj,json_object_new_string((char *)"HH24"));
	json_object_object_add(json,(char *)"timeFormat",arrayObj);
	return json;
}

static bool validateFor_timeFormat( TimePrefsHandler * pTimePrefsHandler,const json_object* pValue) {
	if (json_object_get_type(const_cast<json_object*>(pValue)) != json_type_string)
			return false;
		
	std::string val = json_object_get_string(const_cast<json_object*>(pValue));
	return ((val == "HH12") || (val == "HH24"));
}

static json_object * valuesFor_timeChangeLaunch( TimePrefsHandler * pTimePrefsHandler)
{
	return json_object_new_object();
}

static bool	validateFor_timeChangeLaunch( TimePrefsHandler * pTimePrefsHandler,const json_object* pValue)
{
	return false;		//this is a special key which can only be set via a setTimeChangeLaunch message
}

void TimePrefsHandler::init()
{
	bool result;
    LSError lsError;
    LSErrorInit(&lsError);

    //(these will also set defaults in the db if there was nothing stored yet (e.g. first use))
    readCurrentNITZSettings();
    readCurrentTimeSettings();
    
   //init the keylist
    for (size_t i=0;i<sizeof(timePrefKeys)/sizeof(TimePrefKey);i++) {
    	m_keyList.push_back(std::string(timePrefKeys[i].keyName));
    }
    
    result = LSPalmServiceRegisterCategory( m_service, "/time", s_methods, NULL,
    		NULL, this, &lsError);
    if (!result) {
    	luna_critical(s_logChannel, "Failed in registering time handler method: %s", lsError.message);
    	LSErrorFree(&lsError);
    	return;
    }

    m_serviceHandlePublic = LSPalmServiceGetPublicConnection(m_service);
    m_serviceHandlePrivate = LSPalmServiceGetPrivateConnection(m_service);

	if (!s_timeZonesJson) {		//or else I'll leak
		s_timeZonesJson = json_object_from_file(const_cast<char*>(s_tzFile));
		if ((!s_timeZonesJson) || is_error(s_timeZonesJson))
			s_timeZonesJson = NULL;
		else {
			json_object* ja = json_object_object_get(s_timeZonesJson,(char *)"timeZone");
			if ((ja) && (!is_error(ja))) {
				int s = json_object_array_length(ja);
				g_message("%d timezones loaded from [%s]",s,s_tzFile);
			}
			json_object* jsa = json_object_object_get(s_timeZonesJson,(char *)"syszones");
			if ((jsa) && (!is_error(jsa))) {
				int s = json_object_array_length(jsa);
				g_message("%d sys timezones loaded from [%s]",s,s_tzFile);
			}
						
		}
	}
	
	//load the default
	m_pDefaultTimeZone = new TimeZoneInfo();
	(void)getDefaultTZFromJson(m_pDefaultTimeZone);
	
	std::string nitzValidityState = PrefsDb::instance()->getPref("nitzValidity");
	if (nitzValidityState == "") {
		PrefsDb::instance()->setPref("nitzValidity",NITZVALIDITY_STATE_NITZVALID);
		g_warning("nitzValidity default set to [%s]",NITZVALIDITY_STATE_NITZVALID);
	}
	
	std::string currentlySetTimeZoneJsonString= PrefsDb::instance()->getPref("timeZone");
	if (currentlySetTimeZoneJsonString == "") {
		currentlySetTimeZoneJsonString = m_pDefaultTimeZone->jsonStringValue;
		//set a default
		PrefsDb::instance()->setPref("timeZone",currentlySetTimeZoneJsonString);
		g_warning("timezone default set to [%s]",currentlySetTimeZoneJsonString.c_str());	
	}
	
	std::string currentlySetTimeZoneName = tzNameFromJsonString(currentlySetTimeZoneJsonString);
    
	scanTimeZoneJson();
    
    m_cpCurrentTimeZone = timeZone_ZoneFromName(currentlySetTimeZoneName);
    
    if (m_cpCurrentTimeZone) {
    	g_warning("%s: successfully mapped to zone [%s]",__FUNCTION__,m_cpCurrentTimeZone->name.c_str());
    	setTimeZone(m_cpCurrentTimeZone);
    }
    else {
    	int currOffsetToUTC = offsetToUtcSecs()/60;
    	//last chance to get a valid timezone given the offset
    	m_cpCurrentTimeZone = this->timeZone_ZoneFromOffset(currOffsetToUTC);
    	if (m_cpCurrentTimeZone == NULL)
    	{
    		g_warning("%s: Couldn't pick timezone from offset %d ... picking a generic zone based on offset",__FUNCTION__,currOffsetToUTC);
    		//STILL NULL! pick a generic zone
    		m_cpCurrentTimeZone = timeZone_GenericZoneFromOffset(currOffsetToUTC);
    		if (m_cpCurrentTimeZone == NULL)
    		{
    			g_warning("%s: Couldn't pick GENERIC timezone from offset %d ... last resort: go to default zone",__FUNCTION__,currOffsetToUTC);
    			//This should never happen unless the syszone list is corrupt. But if it is, pick the failsafe default
    			m_cpCurrentTimeZone = &s_failsafeDefaultZone;
    		}
    	}
    	
    	//there is no way to get here w/o m_cpCurrentTimeZone being non-NULL
    	setTimeZone(m_cpCurrentTimeZone);
    }
    
    if (LSCall(m_serviceHandlePrivate,"palm://com.palm.bus/signal/registerServerStatus",
    		"{\"serviceName\":\"com.palm.power\", \"subscribe\":true}",
    		cbServiceStateTracker,this,NULL, &lsError) == false)
    {
    	LSErrorFree(&lsError);
    }

    if (LSCall(m_serviceHandlePrivate, "palm://com.palm.bus/signal/registerServerStatus",
               "{\"serviceName\":\"com.palm.telephony\", \"subscribe\":true}",
               cbServiceStateTracker, this, NULL, &lsError) == false)
    {
        LSErrorFree(&lsError);
    }


	NetworkConnectionListener::instance()->signalConnectionStateChanged.
		connect(this, &TimePrefsHandler::slotNetworkConnectionStateChanged);

    //kick off an initial timeout for time setting, for cases where TIL/modem won't be there
    startBootstrapCycle();
}

/*
 * it will look for specific pref values. These were nicely set by a more general and flexible 
 * implementation with valuesFor_ functions. So if those change, so must this function.
 * TODO: extend the TimePrefKey struct to include setter/validator function ptrs 
 */
void TimePrefsHandler::readCurrentNITZSettings()
{
	std::string settingJsonStr = PrefsDb::instance()->getPref("useNetworkTime");
	bool val;
	g_warning("readCurrentNITZSettings: string1 is [%s]",settingJsonStr.c_str());
	json_object* json = json_tokener_parse(settingJsonStr.c_str());
	if (json && (!is_error(json))) {
		val = json_object_get_boolean(json);
		json_object_put(json);
	}
	else {
		//set a default
		PrefsDb::instance()->setPref("useNetworkTime","true");
		val = true;
	}
		
	setNITZTimeEnable(val);
	
	settingJsonStr = PrefsDb::instance()->getPref("useNetworkTimeZone");
	g_warning("readCurrentNITZSettings: string2 is [%s]",settingJsonStr.c_str());
	json = json_tokener_parse(settingJsonStr.c_str());
	if (json && (!is_error(json))) {
		val = json_object_get_boolean(json);
		
		json_object_put(json);
	}
	else {
		//set a default
		PrefsDb::instance()->setPref("useNetworkTimeZone","true");
		val = true;
	}

	setNITZTZEnable(val);
	
}

void TimePrefsHandler::readCurrentTimeSettings()
{
	std::string settingJsonStr = PrefsDb::instance()->getPref("timeFormat");
	g_warning("readCurrentTimeSettings: string1 is [%s]",settingJsonStr.c_str());
	if (settingJsonStr == "") {
		PrefsDb::instance()->setPref("timeFormat","\"HH12\"");		//must store as a json string, or else baaaad stuff
		//TODO: fix that ...it's not very robust
	}
	
}
std::string TimePrefsHandler::tzNameFromJsonValue(json_object * pValue) {
	
	if (pValue == NULL)
		return std::string();
	
	if (json_object_get_type(pValue) != json_type_object)
		return std::string();

	json_object* label = json_object_object_get(pValue, "ZoneID");
	if (!label) {
		return std::string();
	}

	std::string tzname = json_object_get_string(label);
	return tzname;
}

std::string TimePrefsHandler::tzNameFromJsonString(const std::string& TZJson) {
	
	json_object * root = json_tokener_parse(TZJson.c_str());
	if (!root || is_error(root))
		return std::string("");
	
	std::string zoneId;
	
	json_object* obj = json_object_object_get(root,(char *)"ZoneID");
	if ((obj) && (!is_error(obj))) {
		zoneId = json_object_get_string(obj);
	}
	json_object_put(root);
	return zoneId;
}

/**
 * Scans s_timeZonesJson for ZoneID == tzName. Returns that json object as a string, or "" if none found
 * 
 * 
 */

std::string TimePrefsHandler::getQualifiedTZIdFromName(const std::string& tzName) {
	
	if ((tzName.length() == 0) || (s_timeZonesJson == NULL))
		return std::string("");
	
	json_object * label = json_object_object_get(s_timeZonesJson, "timeZone");
	if (!label || is_error(label)) {
		g_warning("error on json object: it doesn't contain a timezones array");
		return std::string("");
	}

	array_list * srcJsonArray = json_object_get_array(label);
	if ((!srcJsonArray) || (is_error(srcJsonArray))) {
		g_warning("error on json object: it doesn't contain a timezones array");
		return std::string("");
	}	
	for (int i = 0; i < array_list_length(srcJsonArray); i++) {
		json_object* obj = (json_object*) array_list_get_idx(srcJsonArray, i);

		label = json_object_object_get(obj,(char*)"ZoneID");
		if ((!label) || (is_error(label)))
			continue;

		std::string zoneId = json_object_get_string(label);
		if (zoneId == tzName)
			return (std::string(json_object_to_json_string(obj)));

	}
	
	//try the sys zones
	
	label = json_object_object_get(s_timeZonesJson, "syszones");
	if (!label || is_error(label)) {
		g_warning("error on json object: it doesn't contain a syszones array");
		return std::string("");
	}

	srcJsonArray = json_object_get_array(label);
	if ((!srcJsonArray) || (is_error(srcJsonArray))) {
		g_warning("error on json object: it doesn't contain a syszones array");
		return std::string("");
	}	
	for (int i = 0; i < array_list_length(srcJsonArray); i++) {
		json_object* obj = (json_object*) array_list_get_idx(srcJsonArray, i);

		label = json_object_object_get(obj,(char*)"ZoneID");
		if ((!label) || (is_error(label)))
			continue;

		std::string zoneId = json_object_get_string(label);
		if (zoneId == tzName)
			return (std::string(json_object_to_json_string(obj)));
	}
		
	return std::string("");
}

std::string TimePrefsHandler::getQualifiedTZIdFromJson(const std::string& jsonTz) {
	
	if ((jsonTz.length() == 0) || (s_timeZonesJson == NULL))
		return std::string("");
	
	std::string tzName;
	json_object * label;
	
	json_object * jsontzRoot = json_tokener_parse(jsonTz.c_str());
	if (!jsontzRoot || is_error(jsontzRoot)) {
		return std::string("");
	}
	label = json_object_object_get(jsontzRoot,"ZoneID");
	if ((!label) || is_error(label)) {
		g_warning("error on json input: it doesn't contain a ZoneID key");
		json_object_put(jsontzRoot);
		return std::string("");
	}
	else {
		tzName = json_object_get_string(label);
	}
	json_object_put(jsontzRoot);
	
	label = json_object_object_get(s_timeZonesJson, "timeZone");
	if (!label || is_error(label)) {
		g_warning("error on json object: it doesn't contain a timezones array");
		return std::string("");
	}

	array_list * srcJsonArray = json_object_get_array(label);
	if ((!srcJsonArray) || (is_error(srcJsonArray))) {
		g_warning("error on json object: it doesn't contain a timezones array");
		return std::string("");
	}	
	for (int i = 0; i < array_list_length(srcJsonArray); i++) {
		json_object* obj = (json_object*) array_list_get_idx(srcJsonArray, i);

		label = json_object_object_get(obj,(char*)"ZoneID");
		if ((!label) || (is_error(label)))
			continue;

		std::string zoneId = json_object_get_string(label);
		if (zoneId == tzName)
			return (std::string(json_object_to_json_string(obj)));

	}
	
	//try the sys zones
	
	label = json_object_object_get(s_timeZonesJson, "syszones");
	if (!label || is_error(label)) {
		g_warning("error on json object: it doesn't contain a syszones array");
		return std::string("");
	}

	srcJsonArray = json_object_get_array(label);
	if ((!srcJsonArray) || (is_error(srcJsonArray))) {
		g_warning("error on json object: it doesn't contain a syszones array");
		return std::string("");
	}	
	for (int i = 0; i < array_list_length(srcJsonArray); i++) {
		json_object* obj = (json_object*) array_list_get_idx(srcJsonArray, i);

		label = json_object_object_get(obj,(char*)"ZoneID");
		if ((!label) || (is_error(label)))
			continue;

		std::string zoneId = json_object_get_string(label);
		if (zoneId == tzName)
			return (std::string(json_object_to_json_string(obj)));
	}
		
	return std::string("");
}

//a replacement for the scanTimeZoneFile so that I only need to deal with 1 file...see init() for where the json obj is created
void TimePrefsHandler::scanTimeZoneJson() {

	std::string name;
	std::string countryCode;
	int offset;
	bool pref;
	int supportsDst;
	int mcc;
	std::map<int,PreferredZones> tmpPrefZoneMap;
	std::map<int,PreferredZones>::iterator tmpPrefZoneMapIter;
	std::map<std::string,std::set<int> > tmpCountryZoneCounterMap;
	
	if (s_timeZonesJson == NULL) {
		g_warning("scanTimeZoneJson(): no json loaded");
		return;
	}
	
	json_object * label = json_object_object_get(TimePrefsHandler::s_timeZonesJson, "timeZone");
	if (!label || is_error(label)) {
		g_warning("scanTimeZoneJson(): invalid json; missing timeZone array");
		return;
	}
	else if (!(json_object_is_type(label,json_type_array))) {
		g_warning("scanTimeZoneJson(): invalid json; timeZone is not an array");
		return;
	}
	
	array_list * srcJsonArray = json_object_get_array(label);
	if ((!srcJsonArray) || (is_error(srcJsonArray))) {
		g_warning("scanTimeZoneJson(): invalid json; timeZone array invalid");
		return;
	}
	
	for (int i = 0; i < array_list_length(srcJsonArray); i++) {
		json_object* obj = (json_object*) array_list_get_idx(srcJsonArray, i);

		if ((!obj) || (is_error(obj)))
			continue;
		
		label = json_object_object_get(obj,(char*)"ZoneID");
		if ((!label) || (is_error(label))) {
			continue;
		}
		
		name = json_object_get_string(label);
		
		label = json_object_object_get(obj,(char*)"offsetFromUTC");
		if ((!label) || (is_error(label))) {
			continue;
		}

		offset = json_object_get_int(label);
		
		label = json_object_object_get(obj,(char*)"supportsDST");
		if ((!label) || (is_error(label))) {
			continue;
		}

		supportsDst = json_object_get_int(label);
		
		label = json_object_object_get(obj,(char*)"preferred");
		if ((!label) || (is_error(label))) {
			pref=false;
		}
		else
			pref = json_object_get_boolean(label);

		label = json_object_object_get(obj, (char*)"CountryCode");
		if (label && !is_error(label))
			countryCode = json_object_get_string(label);
		else
			countryCode = "";
		
		//update "counter map"
		tmpCountryZoneCounterMap[countryCode].insert(offset);
		
		TimeZoneInfo* tz = new TimeZoneInfo;
		tz->offsetToUTC = offset;
		tz->preferred = pref;
		tz->dstSupported = supportsDst;
		tz->name = name;
		tz->countryCode = countryCode;
		tz->jsonStringValue = json_object_to_json_string(obj);
		
		tmpPrefZoneMapIter = tmpPrefZoneMap.find(tz->offsetToUTC);
		if (tmpPrefZoneMapIter == tmpPrefZoneMap.end()) {
			PreferredZones pz;
			pz.offset = offset;
			if ((pref) && (supportsDst))
				pz.dstPref = tz;
			else if ((pref) && (!supportsDst))
				pz.nonDstPref = tz;
			else if (supportsDst)
				pz.dstFallback = tz;
			else
				pz.nonDstFallback = tz;
			
			tmpPrefZoneMap[offset] = pz;
		}
		else {
			if ((pref) && (supportsDst))
				(*tmpPrefZoneMapIter).second.dstPref = tz;
			else if ((pref) && (!supportsDst))
				(*tmpPrefZoneMapIter).second.nonDstPref = tz;
			else if ((supportsDst) && (((*tmpPrefZoneMapIter).second.dstFallback)==NULL))
				(*tmpPrefZoneMapIter).second.dstFallback = tz;
			else if ( (!supportsDst) && ((*tmpPrefZoneMapIter).second.nonDstFallback)==NULL)
				(*tmpPrefZoneMapIter).second.nonDstFallback = tz;
		}
		
		m_zoneList.push_back(tz);

		m_offsetZoneMultiMap.insert(TimeZonePair(offset, tz));
		
	}
	
	//go through the whole zone list and assign offset-per-country counter values
	for (TimeZoneInfoList::iterator it = m_zoneList.begin(); it != m_zoneList.end();++it)
	{
		(*it)->howManyZonesForCountry = tmpCountryZoneCounterMap[(*it)->countryCode].size();
	}
	
	//go through the temp map and assign values to the final dst and non-dst maps
	for (tmpPrefZoneMapIter = tmpPrefZoneMap.begin();tmpPrefZoneMapIter != tmpPrefZoneMap.end();tmpPrefZoneMapIter++) {
		int off_key = (*tmpPrefZoneMapIter).second.offset;
		
		//if there is only a dstPref, then use that for both dst and non-dst
		if ( ((*tmpPrefZoneMapIter).second.dstPref) && ((*tmpPrefZoneMapIter).second.nonDstPref == NULL))
		{
			m_preferredTimeZoneMapDST[off_key] = (*tmpPrefZoneMapIter).second.dstPref;
			m_preferredTimeZoneMapNoDST[off_key] = (*tmpPrefZoneMapIter).second.dstPref;
			continue;
		}
		
		if ((*tmpPrefZoneMapIter).second.dstPref)
			m_preferredTimeZoneMapDST[off_key] = (*tmpPrefZoneMapIter).second.dstPref;
		else if ((*tmpPrefZoneMapIter).second.dstFallback)
			m_preferredTimeZoneMapDST[off_key] = (*tmpPrefZoneMapIter).second.dstFallback;
		else if ((*tmpPrefZoneMapIter).second.nonDstPref)
			m_preferredTimeZoneMapDST[off_key] = (*tmpPrefZoneMapIter).second.nonDstPref;
		else
			m_preferredTimeZoneMapDST[off_key] = (*tmpPrefZoneMapIter).second.nonDstFallback;
		
		if ((*tmpPrefZoneMapIter).second.nonDstPref)
			m_preferredTimeZoneMapNoDST[off_key] = (*tmpPrefZoneMapIter).second.nonDstPref;
		else if ((*tmpPrefZoneMapIter).second.nonDstFallback)
			m_preferredTimeZoneMapNoDST[off_key] = (*tmpPrefZoneMapIter).second.nonDstFallback;		
		else if ((*tmpPrefZoneMapIter).second.dstPref)										
			m_preferredTimeZoneMapNoDST[off_key] = (*tmpPrefZoneMapIter).second.dstPref;
		else
			m_preferredTimeZoneMapNoDST[off_key] = (*tmpPrefZoneMapIter).second.dstFallback;
	}
	
	g_warning("scanTimeZoneJson(): found %d timezones",m_zoneList.size());
	
	for (TimeZoneMapIterator it = m_preferredTimeZoneMapDST.begin();it != m_preferredTimeZoneMapDST.end();it++) {
		g_warning("DST-MAP: preferred zone found: [%s] , offset = %d , dstSupport = %s",
				it->second->name.c_str(),it->second->offsetToUTC,(it->second->dstSupported != 0 ? "TRUE" : "FALSE"));
	}
	for (TimeZoneMapIterator it = m_preferredTimeZoneMapNoDST.begin();it != m_preferredTimeZoneMapNoDST.end();it++) {
		g_warning("NON-DST-MAP: preferred zone found: [%s] , offset = %d , dstSupport = %s",
						it->second->name.c_str(),it->second->offsetToUTC,(it->second->dstSupported != 0 ? "TRUE" : "FALSE"));	
	} 
	
	//now grab the "syszones"...these are the default, generic, timezones that get set in case NITZ supplies "dstinvalid"
	
	label = json_object_object_get(s_timeZonesJson, "syszones");
	if (!label || is_error(label)) {
		g_warning("scanTimeZoneJson(): invalid json; missing syszones array");
		return;
	}
	else if (!(json_object_is_type(label,json_type_array))) {
		g_warning("scanTimeZoneJson(): invalid json; syszones is not an array");
		return;
	}
	
	srcJsonArray = json_object_get_array(label);
	if ((!srcJsonArray) || (is_error(srcJsonArray))) {
		g_warning("scanTimeZoneJson(): invalid json; syszones array invalid");
		return;
	}
	
	for (int i = 0; i < array_list_length(srcJsonArray); i++) {
		json_object* obj = (json_object*) array_list_get_idx(srcJsonArray, i);

		if ((!obj) || (is_error(obj)))
			continue;
		
		label = json_object_object_get(obj,(char*)"ZoneID");
		if ((!label) || (is_error(label))) {
			continue;
		}
		
		name = json_object_get_string(label);

		label = json_object_object_get(obj,(char*)"offsetFromUTC");
		if ((!label) || (is_error(label))) {
			continue;
		}
		
		offset = json_object_get_int(label);
		
		TimeZoneInfo* tz = new TimeZoneInfo;
		tz->offsetToUTC = offset;
		tz->preferred = false;
		tz->dstSupported = 0;
		//setTZIName(tz,name.c_str());
		tz->name = name;
		tz->jsonStringValue = json_object_to_json_string(obj);
		
		m_syszoneList.push_back(tz);
	}
	
	//now grab the time zone info for known MCCs...
	// This is used to correct problems in many networks' NITZ data

	label = json_object_object_get(s_timeZonesJson, "mccInfo");
	if (!label || is_error(label)) {
		g_warning("scanTimeZoneJson(): invalid json; missing mccInfo array");
		return;
	}
	else if (!(json_object_is_type(label,json_type_array))) {
		g_warning("scanTimeZoneJson(): invalid json; mccInfo is not an array");
		return;
	}
	
	srcJsonArray = json_object_get_array(label);
	if ((!srcJsonArray) || (is_error(srcJsonArray))) {
		g_warning("scanTimeZoneJson(): invalid json; mccInfo array invalid");
		return;
	}

	for (int i = 0; i < array_list_length(srcJsonArray); i++) {
		json_object* obj = (json_object*) array_list_get_idx(srcJsonArray, i);

		if ((!obj) || (is_error(obj)))
			continue;

		label = json_object_object_get(obj,(char*)"ZoneID");
		if ((label) && (!is_error(label))) {
			name = json_object_get_string(label);
		}
		else
			name = "";

		label = json_object_object_get(obj, (char*)"CountryCode");
		if (label && !is_error(label))
			countryCode = json_object_get_string(label);
		else
			countryCode = "";
			
		label = json_object_object_get(obj,(char*)"offsetFromUTC");
		if ((!label) || (is_error(label))) {
			continue;
		}

		offset = json_object_get_int(label);
		
		label = json_object_object_get(obj,(char*)"supportsDST");
		if ((!label) || (is_error(label))) {
			continue;
		}

		supportsDst = json_object_get_int(label);
				
		label = json_object_object_get(obj,(char*)"mcc");
		if ((!label) || (is_error(label))) {
			continue;
		}
		
		mcc = json_object_get_int(label);
		
		TimeZoneInfo* tz = new TimeZoneInfo;
		tz->offsetToUTC = offset;
		tz->preferred = false;
		tz->dstSupported = supportsDst;
		tz->countryCode = countryCode;
		if (name.size()) {
			//setTZIName(tz,name.c_str());
			tz->name = name;
			tz->jsonStringValue = json_object_to_json_string(obj);
		}
		else {
			tz->name = "";
			tz->jsonStringValue = "";
		}
		m_mccZoneInfoMap[mcc] = tz;
		
	}

}

void TimePrefsHandler::setTimeZone(const TimeZoneInfo * pZoneInfo)
{
	if (pZoneInfo == NULL)
	{
		//failsafe default!
		pZoneInfo = &s_failsafeDefaultZone;
		g_warning("%s: passed in NULL for the zone. Failsafe activated! setting failsafe-default zone: [%s]",__FUNCTION__,pZoneInfo->name.c_str());
	}

	m_cpCurrentTimeZone = pZoneInfo;
	PrefsDb::instance()->setPref("timeZone",pZoneInfo->jsonStringValue);
	systemSetTimeZone(pZoneInfo);
}

void TimePrefsHandler::systemSetTimeZone(const TimeZoneInfo * pZoneInfo) {

	if (pZoneInfo == NULL)
		return;
	
	// Do we have a timezone file in place?
	struct stat stBuf;
	if (lstat(s_tzFilePath, &stBuf) == 0) {
		unlink(s_tzFilePath);
	}
	
	std::string tzFileActual = s_zoneInfoFolder;
	tzFileActual += pZoneInfo->name;
	
	symlink(tzFileActual.c_str(), s_tzFilePath);
	g_warning("systemSetTimeZone(): Setting Time Zone: %s, utc Offset: %d",
			pZoneInfo->name.c_str(), pZoneInfo->offsetToUTC);
	std::string tzsetting = pZoneInfo->name;
	tzsetWorkaround(tzsetting.c_str());
	g_warning("TZ env is now [%s]",getenv("TZ"));
}

//static
void TimePrefsHandler::systemSetTime(time_t utc)
{
	struct timeval timeVal;
	timeVal.tv_sec = utc;
	timeVal.tv_usec = 0;
	g_warning("%s: settimeofday: %u",__FUNCTION__,(unsigned int)timeVal.tv_sec);
	int rc=settimeofday(&timeVal, 0);
	g_warning("%s: settimeofday %s",__FUNCTION__,( rc == 0 ? "succeeded" : "failed"));
		
}

//static
void TimePrefsHandler::systemSetTime(struct timeval * pTimeVal)
{
	g_warning("%s: settimeofday: %u",__FUNCTION__,(unsigned int)pTimeVal->tv_sec);
	int rc=settimeofday(pTimeVal, 0);
	g_warning("%s: settimeofday %s",__FUNCTION__,( rc == 0 ? "succeeded" : "failed"));
		
}


//static
bool TimePrefsHandler::jsonUtil_ZoneFromJson(json_object * json,TimeZoneInfo& r_zoneInfo)
{
	
	if ((!json) || (is_error(json)))
		return false;

	json_object * label = json_object_object_get(json,(char*)"ZoneID");
	if ((!label) || (is_error(label))) {
		return false;
	}

	const char * name = json_object_get_string(label);

	label = json_object_object_get(json,(char*)"offsetFromUTC");
	if ((!label) || (is_error(label))) {
		return false;
	}

	int offset = json_object_get_int(label);

	label = json_object_object_get(json,(char*)"supportsDST");
	if ((!label) || (is_error(label))) {
		return false;
	}

	int supportsDst = json_object_get_int(label);

	bool pref;
	label = json_object_object_get(json,(char*)"preferred");
	if ((!label) || (is_error(label))) {
		pref=false;
	}
	else
		pref = json_object_get_boolean(label);

	std::string countryCode;
	label = json_object_object_get(json, (char*)"countryCode");
	if (label && !is_error(label))
		countryCode = json_object_get_string(label);
	
	r_zoneInfo.offsetToUTC = offset;
	r_zoneInfo.preferred = pref;
	r_zoneInfo.dstSupported = supportsDst;
	r_zoneInfo.name = name;
	r_zoneInfo.countryCode = countryCode;
	r_zoneInfo.jsonStringValue = json_object_to_json_string(json);
	
	return true;
}

void TimePrefsHandler::postSystemTimeChange()
{
	if (!m_cpCurrentTimeZone)
		return;

	std::string nitzValidity = PrefsDb::instance()->getPref("nitzValidity");
	
	LSError lsError;
	json_object* json = 0;
	json_object* localtime_json = 0;
	char tzoneabbr_cstr[16] = {0};
	LSErrorInit(&lsError);
	std::string tzAbbr;
	
	time_t utctime = time(NULL);
	struct tm * p_localtime_s = localtime(&utctime);

	json = json_object_new_object();
	json_object_object_add(json, (char*) "utc", json_object_new_int((int)time(NULL)));
	localtime_json = json_object_new_object();
	json_object_object_add(localtime_json,(char *)"year",json_object_new_int(p_localtime_s->tm_year + 1900));
	json_object_object_add(localtime_json,(char *)"month",json_object_new_int(p_localtime_s->tm_mon + 1));
	json_object_object_add(localtime_json,(char *)"day",json_object_new_int(p_localtime_s->tm_mday));
	json_object_object_add(localtime_json,(char *)"hour",json_object_new_int(p_localtime_s->tm_hour));
	json_object_object_add(localtime_json,(char *)"minute",json_object_new_int(p_localtime_s->tm_min));
	json_object_object_add(localtime_json,(char *)"second",json_object_new_int(p_localtime_s->tm_sec));
	json_object_object_add(json,(char *)"localtime",localtime_json);

	json_object_object_add(json, (char*) "offset", json_object_new_int(offsetToUtcSecs()/60));


	if (currentTimeZone()) {
		json_object_object_add(json, (char*) "timezone", json_object_new_string(currentTimeZone()->name.c_str()));
		//get current time zone abbreviation
		strftime (tzoneabbr_cstr,16,"%Z",localtime(&utctime));
		tzAbbr = std::string(tzoneabbr_cstr);
	}
	else {
		json_object_object_add(json, (char*) "timezone", json_object_new_string((char*) "UTC"));
		tzAbbr = std::string("UTC");		//default to something
	}
	json_object_object_add(json, (char*) "TZ", json_object_new_string(tzAbbr.c_str()));

	json_object_object_add(json, (char*) "timeZoneFile", json_object_new_string(const_cast<char*>(s_tzFilePath)));
	
	if (nitzValidity == NITZVALIDITY_STATE_NITZVALID)
		json_object_object_add(json,(char*) "NITZValid", json_object_new_boolean(true));
	else if (nitzValidity == NITZVALIDITY_STATE_NITZINVALIDUSERNOTSET)
		json_object_object_add(json,(char*) "NITZValid", json_object_new_boolean(false));

	//the new "sub"keys for nitz validity...
	//the new "sub"keys for nitz validity...
	if (isNITZTimeEnabled())
		json_object_object_add(json,(char*) "NITZValidTime", json_object_new_boolean(m_immNitzTimeValid));
	if (isNITZTZEnabled())
		json_object_object_add(json,(char*) "NITZValidZone", json_object_new_boolean(m_immNitzZoneValid));
	
	const char * reply = json_object_to_json_string(json);

	std::string subKeyStr = std::string("getSystemTime");
	std::string subValStr = std::string(reply);
	PrefsFactory::instance()->postPrefChangeValueIsCompleteString(subKeyStr,subValStr);
	
	json_object_put(json);    
}

void TimePrefsHandler::postNitzValidityStatus()
{
	if (!m_cpCurrentTimeZone)
		return;

	std::string nitzValidity = PrefsDb::instance()->getPref("nitzValidity");
	
	LSError lsError;
	json_object* json = 0;
	LSErrorInit(&lsError);
	
	json = json_object_new_object();
	if (nitzValidity == NITZVALIDITY_STATE_NITZVALID)
		json_object_object_add(json,(char*) "NITZValid", json_object_new_boolean(true));
	else if (nitzValidity == NITZVALIDITY_STATE_NITZINVALIDUSERNOTSET)
		json_object_object_add(json,(char*) "NITZValid", json_object_new_boolean(false));

	//the new "sub"keys for nitz validity...
	if (isNITZTimeEnabled())
		json_object_object_add(json,(char*) "NITZValidTime", json_object_new_boolean(m_immNitzTimeValid));
	if (isNITZTZEnabled())
		json_object_object_add(json,(char*) "NITZValidZone", json_object_new_boolean(m_immNitzZoneValid));
		
	const char * reply = json_object_to_json_string(json);

	std::string subKeyStr = std::string("getSystemTime");
	std::string subValStr = std::string(reply);
	PrefsFactory::instance()->postPrefChangeValueIsCompleteString(subKeyStr,subValStr);
	json_object_put(json);    
}


void TimePrefsHandler::launchAppsOnTimeChange()
{
	
	bool rc;
	LSError lsError;
	LSErrorInit(&lsError);
	
	//grab the pref and parse out the json
	std::string rawCurrentPref = PrefsDb::instance()->getPref("timeChangeLaunch");
	struct json_object * storedJson = json_tokener_parse(rawCurrentPref.c_str());
	if ((storedJson == NULL) || (is_error(storedJson))) {
		//nothing to do
		return;
	}

	//get the launchList array object out of it
	struct json_object * storedJson_listArray = Utils::JsonGetObject(storedJson,"launchList");
	if (storedJson_listArray == NULL) {
		//nothing to do
		return;
	}
	
	for (int listIdx=0;listIdx<json_object_array_length(storedJson_listArray);++listIdx) {
		struct json_object * storedJson_listObject = json_object_array_get_idx(storedJson_listArray,listIdx);
		if (storedJson_listObject == NULL)
			continue;
		struct json_object * label = Utils::JsonGetObject(storedJson_listObject,"appId");
		if (label == NULL) {
			continue;		//something really bad happened; something was stored in the list w/o an appId!
		}
		std::string appId = json_object_get_string(label);
		label = Utils::JsonGetObject(storedJson_listObject,"parameters");
		std::string launchStr;
		if (label)
			launchStr = std::string("{ \"id\":\"")+appId+std::string("\", \"params\":")+json_object_to_json_string(label)+std::string(" }");
		else
			launchStr = launchStr = std::string("{ \"id\":\"")+appId+std::string("\", \"params\":\"\" }");
		
		LSErrorInit(&lsError);
		rc = LSCall(getPrivateHandle(),
				"luna://com.palm.applicationManager/launch",
				launchStr.c_str(),
				NULL,NULL,NULL, &lsError);

		if (!rc) {
			LSErrorFree(&lsError);
			LSErrorInit(&lsError);
		}
	}

}

time_t TimePrefsHandler::offsetToUtcSecs() const
{
	// We retrieve current offset to UTC separately because Daylight Savings may be in
    // effect and the offset will be different than the standard one
    time_t currTime;
	struct tm lt;

	// UTC time
    currTime = time(NULL);

	// Local time
	localtime_r(&currTime, &lt);

	// Back to UTC
	time_t ltSecs = timegm(&lt);

	return (ltSecs - currTime);    
}

bool TimePrefsHandler::setNITZTimeEnable(bool time_en) {	//returns old value

	bool rv = (m_nitzSetting & TimePrefsHandler::NITZ_TimeEnable);

#if defined(HAVE_LUNA_PREFS)	
	LPAppHandle lpHandle = 0;
	if (LPAppGetHandle("com.palm.systemservice", &lpHandle) == LP_ERR_NONE)
	{
		g_warning("%s: Writing networkTimeEnabled = %d",__FUNCTION__,(int)time_en);
		LPAppSetValueInt(lpHandle, "networkTimeEnabled", (int)time_en);
		LPAppFreeHandle(lpHandle, true);
	}
#endif	

	if (time_en) {
		m_nitzSetting |= TimePrefsHandler::NITZ_TimeEnable;
		//schedule a periodic NTP event
		setPeriodicTimeSetWakeup();
	}
	else {
		//clear the flag
		m_nitzSetting &= (~TimePrefsHandler::NITZ_TimeEnable);
	}
	
	return rv;
}

bool TimePrefsHandler::setNITZTZEnable(bool tz_en) {	//returns old value

	bool rv = (m_nitzSetting & TimePrefsHandler::NITZ_TZEnable);
	
	if (tz_en) {
		m_nitzSetting |= TimePrefsHandler::NITZ_TZEnable;
	}
	else {
		//clear the flag
		m_nitzSetting &= (~TimePrefsHandler::NITZ_TZEnable);
	}
	
	return rv;
}

const TimeZoneInfo* TimePrefsHandler::timeZone_ZoneFromOffset(int offset,int dstValue, int mcc) const
{
	if (mcc != 0) {

		const TimeZoneInfo* tzMcc = timeZone_ZoneFromMCC(mcc, 0);
		if (tzMcc && !tzMcc->countryCode.empty()) {

			g_message("MCC code: %d, Offset: %d, DstValue: %d, TZ Entry: %s", mcc, offset, dstValue,
					  tzMcc->jsonStringValue.c_str());
			
			std::string countryCode = tzMcc->countryCode;

			// All timezones wih matching offset
			std::pair<TimeZoneMultiMapConstIterator, TimeZoneMultiMapConstIterator> iterPair
				= m_offsetZoneMultiMap.equal_range(offset);

			// narrow down list to those matching the MCC code
			TimeZoneInfoList mccMatchingTzList;			
			for (TimeZoneMultiMapConstIterator iter = iterPair.first; iter != iterPair.second; ++iter) {
				if (iter->second->countryCode == countryCode) {
					mccMatchingTzList.push_back(iter->second);
				}
			}

			if (!mccMatchingTzList.empty()) {

//				if (dstValue == 1) {
					
					// First iteration: preferred and DST enabled
					for (TimeZoneInfoListConstIterator iter = mccMatchingTzList.begin();
						 iter != mccMatchingTzList.end(); ++iter) {
						
						TimeZoneInfo* z = (*iter);
//						if (z->preferred && z->dstSupported == 1) {
						if (z->preferred && z->dstSupported == dstValue) {
							g_message("Found match in first iteration: %s", z->jsonStringValue.c_str());
							return z;
						}
					}

					// Second iteration: DST enabled
					for (TimeZoneInfoListConstIterator iter = mccMatchingTzList.begin();
						 iter != mccMatchingTzList.end(); ++iter) {
						
						TimeZoneInfo* z = (*iter);
						if (z->dstSupported == 1) {
							g_message("Found match in second iteration: %s", z->jsonStringValue.c_str());
							return z;
						}
					}
//				}

				// Third iteration: just preferred
				for (TimeZoneInfoListConstIterator iter = mccMatchingTzList.begin();
					 iter != mccMatchingTzList.end(); ++iter) {

					TimeZoneInfo* z = (*iter);
					if (z->preferred) {
						g_message("Found match in third iteration: %s", z->jsonStringValue.c_str());
						return z;
					}
				}

				//  Fourth iteration: just matching DST
				for (TimeZoneInfoListConstIterator iter = mccMatchingTzList.begin();
					 iter != mccMatchingTzList.end(); ++iter) {

					TimeZoneInfo* z = (*iter);
					if (z->dstSupported == dstValue) {
						g_message("Found match in fourth iteration: %s", z->jsonStringValue.c_str());
						return z;
					}
				}

				// Finally: just the first in the list
				TimeZoneInfo* z = mccMatchingTzList.front();
				if (z) {
					g_message("Found match in fifth iteration: %s", z->jsonStringValue.c_str());
					return z;
				}
			}
		}
	}
	
	TimeZoneInfo * z = NULL;
	TimeZoneMapConstIterator it;
	if (dstValue == 0) {
		it = m_preferredTimeZoneMapNoDST.find(offset);
		if (it != m_preferredTimeZoneMapNoDST.end())
			z = it->second;
	}
	else {
		it = m_preferredTimeZoneMapDST.find(offset);
		if (it != m_preferredTimeZoneMapDST.end())
			z = it->second;
	}
	
	return z;
}

const TimeZoneInfo* TimePrefsHandler::timeZone_GenericZoneFromOffset(int offset) const
{

	//scan the sys zones list
	for (TimeZoneInfoListConstIterator 	it = m_syszoneList.begin();
									it != m_syszoneList.end();
									it++) 
	{
		TimeZoneInfo * zc = *it;
		if (zc->offsetToUTC == offset)
			return zc;
	}
	return NULL;
}

const TimeZoneInfo* TimePrefsHandler::timeZone_ZoneFromMCC(int mcc,int mnc) const
{
	TimeZoneMapConstIterator it = m_mccZoneInfoMap.find(mcc);
	if (it == m_mccZoneInfoMap.end())
		return NULL;
	return it->second;
}

const TimeZoneInfo* TimePrefsHandler::timeZone_ZoneFromName(const std::string& name) const
{
	if (name.empty())
		return 0;
    
	for (TimeZoneInfoList::const_iterator it = m_zoneList.begin();
		 it != m_zoneList.end(); ++it) {
		TimeZoneInfo* z = (TimeZoneInfo*) (*it);

		if (z->name == name)
			return z;
	}
	
	for (TimeZoneInfoList::const_iterator it = m_syszoneList.begin();
	it != m_syszoneList.end(); ++it) {
		TimeZoneInfo* z = (TimeZoneInfo*) (*it);

		if (z->name == name)
			return z;
	}

	return 0;    
}

const TimeZoneInfo* TimePrefsHandler::timeZone_GetDefaultZoneFailsafe()
{
	//No matter what, return *a* zone...never null.
	//order:  try the m_pDefaultTimeZone , then default zone from json, then finally the failsafe default hardcoded
	
	const TimeZoneInfo * tz = NULL;
	
	if (m_pDefaultTimeZone)
	{
		tz = timeZone_ZoneFromName(m_pDefaultTimeZone->name);
		return tz;
	}
	else
	{
		tz= timeZone_ZoneFromName(tzNameFromJsonString(getDefaultTZFromJson()));
	}
	
	if (tz == NULL)
		tz = &s_failsafeDefaultZone;
	
	return tz;
}

bool TimePrefsHandler::isCountryAcrossMultipleTimeZones(const TimeZoneInfo& tzinfo) const
{
	//placeholder fn in case this logic needs to get more complex
	return ((tzinfo.howManyZonesForCountry) > 1);
}

/**
	setSystemTime: Set System time. 
*/
//static 
bool TimePrefsHandler::cbSetSystemTime(LSHandle* lshandle, LSMessage *message,
							void *user_data)
{
    // {"utc": string}
    VALIDATE_SCHEMA_AND_RETURN(lshandle,
                               message,
                               SCHEMA_1(REQUIRED(utc, string)));

    LSError lserror;
    LSErrorInit(&lserror);

	const char* str = LSMessageGetPayload(message);
    if( !str )
		return false;

	struct json_object* root = json_tokener_parse(str);
	struct json_object* label = 0;

	std::string utcTimeInSecsStr;
	std::string errorText;
	std::string errorCode;
	
	long utcTimeInSecs=0;
	bool utcTimeValid=true;
	
	struct timeval timeVal;	
	int rc=0;
	
	TimePrefsHandler* th = (TimePrefsHandler*) user_data;
	
	if (!root || (is_error(root))) {
		root = 0;
		errorText = "malformed json";
		goto Done_cbSetSystemTime;
	}

	label = json_object_object_get(root, "utc");

	if ((!label) || is_error(label))
	{	
		utcTimeValid=false;
		
	}
	else
	{
		utcTimeValid = true;
		utcTimeInSecsStr = json_object_get_string(label);
	}
	
	errno = 0;
	if (utcTimeValid) {
		utcTimeInSecs   = strtoul(utcTimeInSecsStr.c_str(), 0, 10);
		if (errno != 0 && utcTimeInSecs == 0) 
		{
			errorText = "conversion of utc value failed";
			goto Done_cbSetSystemTime;
		}
		//a new time was specified
		timeVal.tv_sec = utcTimeInSecs;
		timeVal.tv_usec = 0;
		g_warning("%s: settimeofday: %u",__FUNCTION__,(unsigned int)timeVal.tv_sec);
		rc=settimeofday(&timeVal, 0);
		g_warning("%s: settimeofday %s",__FUNCTION__,( rc == 0 ? "succeeded" : "failed"));
				
	}
	
	TimePrefsHandler::transitionNITZValidState((th->getLastNITZValidity() & TimePrefsHandler::NITZ_Valid),true);
	th->postSystemTimeChange();
	th->launchAppsOnTimeChange();
									   
Done_cbSetSystemTime:
		
	if (root)
		json_object_put(root);

	root = json_object_new_object();
	
	if (errorText.empty())
	{
		//success case
		json_object_object_add(root,"returnValue",json_object_new_boolean(true));
	}
	else
	{
		//failure case
		json_object_object_add(root,"returnValue",json_object_new_boolean(false));
		json_object_object_add(root,"errorText",json_object_new_string(errorText.c_str()));
		json_object_object_add(root,"errorCode",json_object_new_string("FAIL"));
	}
	
	if (!LSMessageReply( lshandle, message,json_object_to_json_string(root) , &lserror )) {
		LSErrorFree (&lserror);
	}
	
	json_object_put(root);
	return true;
}

//static
bool TimePrefsHandler::cbPowerDActivityStatus(LSHandle* lshandle, LSMessage *message,
							void *user_data) 
{
    EMPTY_SCHEMA_RETURN(lshandle, message);

	//just log what's happening
	const char* str = LSMessageGetPayload(message);
	if( !str )
		str = "[NO PAYLOAD IN LSMessage!]";
	g_warning("cbPowerDActivityStatus(): reported status: %s",str);
	return true;
}

//static 
bool TimePrefsHandler::cbSetSystemNetworkTime(LSHandle * lshandle, LSMessage *message, void * user_data)
{
    /*
        {
            "sec": string,
            "min": string,
            "hour": string,
            "mday": string,
            "mon": string,
            "year": string,
            "offset": string,
            "mcc": string,
            "mnc": string,
            "tzvalid": boolean,
            "timevalid": boolean,
            "dstvalid": boolean,
            "dst": integer,
            "timestamp": string,
            "tilIgnore": boolean
        }
    */
    const char* pSchema = SCHEMA_15(REQUIRED(sec, string), REQUIRED(min, string), REQUIRED(hour, string), REQUIRED(mday, string), REQUIRED(mon, string), REQUIRED(year, string), REQUIRED(offset, string), REQUIRED(mcc, string), REQUIRED(mnc, string), REQUIRED(tzvalid, boolean), REQUIRED(timevalid, boolean), REQUIRED(dstvalid, boolean), REQUIRED(dst, integer), REQUIRED(timestamp, string), REQUIRED(tilIgnore, boolean));
    VALIDATE_SCHEMA_AND_RETURN(lshandle, message, pSchema);

	LSError lserror;
	std::string errorText;

	json_object* label = 0;
	int rc=0;
	int utcOffset=-1;
	struct tm timeStruct;
	bool dstValid = false;
	bool tzValid = false;
	bool timeValid = false;
	int dst = 0;
	int mcc = 0;
	int mnc = 0;
	time_t remotetimeStamp = 0;
	NitzParameters nitzParam;
	int nitzFlags = 0;
	std::string nitzFnMsg;
	
	TimePrefsHandler* th = (TimePrefsHandler*)user_data;

	const char* str = LSMessageGetPayload(message);
	if( !str )
		return false;

	json_object* root = json_tokener_parse(str);
	if (!root || is_error(root)) {
		root = 0;
		errorText = std::string("unable to parse json");
		goto Done_cbSetSystemNetworkTime;
	}

	memset(&timeStruct,0,sizeof(struct tm));
	g_warning("NITZ message received from Telephony Service: %s",str);

	label = json_object_object_get(root, "sec");
	if (!label || (is_error(label)))
		++rc;
	else
		timeStruct.tm_sec = strtol(json_object_get_string(label),0,10);
	label = json_object_object_get(root, "min");
	if (!label || (is_error(label)))
		++rc;
	else
		timeStruct.tm_min = strtol(json_object_get_string(label),0,10);
	label = json_object_object_get(root, "hour");
	if (!label || (is_error(label)))
		++rc;
	else
		timeStruct.tm_hour = strtol(json_object_get_string(label),0,10);
	label = json_object_object_get(root, "mday");
	if (!label || (is_error(label)))
		++rc;
	else
		timeStruct.tm_mday = strtol(json_object_get_string(label),0,10);
	label = json_object_object_get(root, "mon");
	if (!label || (is_error(label)))
		++rc;
	else
		timeStruct.tm_mon = strtol(json_object_get_string(label),0,10);
	label = json_object_object_get(root, "year");
	if (!label || (is_error(label)))
		++rc;
	else
		timeStruct.tm_year = strtol(json_object_get_string(label),0,10);

	label = json_object_object_get(root, "offset");
	if ((label) && (!is_error(label)))
		utcOffset = strtol(json_object_get_string(label),0,10);
	else
		utcOffset = -1000;					// this is an invalid value so it can be detected later on
	
	label = json_object_object_get(root, "mcc");
	if ((label) && (!is_error(label)))
		mcc = strtol(json_object_get_string(label),0,10);
	else
		mcc = 0;
	
	label = json_object_object_get(root, "mnc");
	if ((label) && (!is_error(label)))
		mnc = strtol(json_object_get_string(label),0,10);
	else
		mnc = 0;
	
	label = json_object_object_get(root, "tzvalid");
	if ((label) && (!is_error(label)))
		tzValid = json_object_get_boolean(label);
	else 
		tzValid = false;
	
	dbg_time_tzvalidOverride(tzValid);
	
	label = json_object_object_get(root, "timevalid");
	if ((label) && (!is_error(label)))
		timeValid = json_object_get_boolean(label);
	else
		timeValid = false;
	
	dbg_time_timevalidOverride(timeValid);
	
	label = json_object_object_get(root, "dstvalid");
	if ((label) && (!is_error(label)))
		dstValid = json_object_get_boolean(label);
	else
		dstValid = false;
	
	dbg_time_dstvalidOverride(dstValid);
	
	label = json_object_object_get(root, "dst");
	if ((label) && (!is_error(label)))
		dst = json_object_get_int(label);
	else
		dst = 0;
	
	//additional param checks
	if (utcOffset == -1000)
		tzValid = false;

	//check to see if there is a valid timestamp
	label = json_object_object_get(root, "timestamp");
	if ((label) && (!is_error(label)))
		remotetimeStamp = strtoul(json_object_get_string(label),0,10);
	else
		remotetimeStamp = 0;			//...I suppose this can be valid in some cases...like for "threshold" seconds when the time() clock rolls over [not a big deal]

	label = json_object_object_get(root, "tilIgnore");
	if ((label) && (!is_error(label)))
	{
		if (json_object_get_boolean(label))
			nitzFlags |= NITZHANDLER_FLAGBIT_IGNORE_TIL_SET;
	}
		
	nitzParam = NitzParameters(timeStruct,utcOffset,dst,mcc,mnc,timeValid,tzValid,dstValid,remotetimeStamp);	//wasteful copy but this fn isn't called much

	//run the nitz chain
	if (th->nitzHandlerEntry(nitzParam,nitzFlags,nitzFnMsg) != NITZHANDLER_RETURN_SUCCESS)
	{
		errorText = "nitz message failed entry: "+nitzFnMsg;
		goto Done_cbSetSystemNetworkTime;
	}
	if (th->nitzHandlerTimeValue(nitzParam,nitzFlags,nitzFnMsg) != NITZHANDLER_RETURN_SUCCESS)
	{
		errorText = "nitz message failed in time-value handler: "+nitzFnMsg;
		goto Done_cbSetSystemNetworkTime;
	}
	if (th->nitzHandlerOffsetValue(nitzParam,nitzFlags,nitzFnMsg) != NITZHANDLER_RETURN_SUCCESS)
	{
		errorText = "nitz message failed in timeoffset-value handler: "+nitzFnMsg;
		goto Done_cbSetSystemNetworkTime;
	}
	if (th->nitzHandlerDstValue(nitzParam,nitzFlags,nitzFnMsg) != NITZHANDLER_RETURN_SUCCESS)
	{
		errorText = "nitz message failed in timedst-value handler: "+nitzFnMsg;
		goto Done_cbSetSystemNetworkTime;
	}
	if (th->nitzHandlerExit(nitzParam,nitzFlags,nitzFnMsg) != NITZHANDLER_RETURN_SUCCESS)
	{
		errorText = "nitz message failed exit: "+nitzFnMsg;
		goto Done_cbSetSystemNetworkTime;
	}
	
	//if successfully completed, then reset the last nitz parameter member and flags
	if (th->m_p_lastNitzParameter == NULL)
		th->m_p_lastNitzParameter = new NitzParameters(nitzParam);
	else
		*(th->m_p_lastNitzParameter) = nitzParam;
	
	th->m_lastNitzFlags = nitzFlags;

Done_cbSetSystemNetworkTime:

	//start the timeout cycle for completing NITZ processing later
	th->startTimeoutCycle();
	
	if (root)
		json_object_put(root);

	root = json_object_new_object();
	
	if (errorText.empty())
	{
		//success
		json_object_object_add(root,"returnValue",json_object_new_boolean(true));
		
	}
	else
	{
		json_object_object_add(root,"returnValue",json_object_new_boolean(false));
		json_object_object_add(root,"errorText",json_object_new_string(errorText.c_str()));
	}
	
	LSErrorInit(&lserror);
	if (!LSMessageReply( lshandle, message,json_object_to_json_string(root), &lserror )) {
		LSErrorFree (&lserror);
	}
	json_object_put(root);
	
	return true;

}

int  TimePrefsHandler::nitzHandlerEntry(NitzParameters& nitz,int& flags,std::string& r_statusMsg)
{
	//check the validity of the received nitz message
	if (nitz.valid() == false)
	{
		r_statusMsg = "timestamps are too far apart";
		return NITZHANDLER_RETURN_ERROR;
	}
	//set up the flags
	if (PrefsDb::instance()->getPref("timeZonesUseGenericExclusively") == "true")
		flags |= NITZHANDLER_FLAGBIT_GZONEFORCE;
	if (PrefsDb::instance()->getPref("AllowGenericTimezones") == "true")
		flags |= NITZHANDLER_FLAGBIT_GZONEALLOW;
	if (PrefsDb::instance()->getPref("AllowMCCAssistedTimezones") == "true")
		flags |= NITZHANDLER_FLAGBIT_MCCALLOW;
	if (PrefsDb::instance()->getPref("AllowNTPTime") == "true")
		flags |= NITZHANDLER_FLAGBIT_NTPALLOW;
	
	return NITZHANDLER_RETURN_SUCCESS;
}

int  TimePrefsHandler::nitzHandlerTimeValue(NitzParameters& nitz,int& flags,std::string& r_statusMsg)
{
	
	if (isNITZTimeEnabled() == false)
		return NITZHANDLER_RETURN_SUCCESS;			//automatic time adjustments are not allowed

	if (flags & NITZHANDLER_FLAGBIT_IGNORE_TIL_SET)
	{
		int utc = timegm(&(nitz._timeStruct));
		systemSetTime(utc);
		nitz._timevalid = true;
	}
	
	if (nitz._timevalid)
	{
		signalReceivedNITZUpdate(true,false);
		return NITZHANDLER_RETURN_SUCCESS;			//the time was already set by the TIL...nothing to do, so exit
	}
	
	//check to see if NTP time is allowed.
	if ((flags & NITZHANDLER_FLAGBIT_NTPALLOW) == 0)
		return NITZHANDLER_RETURN_SUCCESS;			//no NTP allowed...nothing left to do

	//get NTP time if possible
	time_t ntpUtc;
	if (getUTCTimeFromNTP(ntpUtc) == 0)
	{
		//ok, got it from NTP...
		nitz._timevalid = true;
		memcpy(&(nitz._timeStruct),gmtime(&ntpUtc),sizeof(struct tm));		//for posterity; try to keep the working copy in sync so that down-chain stuff can use it if it wants
		systemSetTime(ntpUtc);
		signalReceivedNITZUpdate(true,false);

		m_lastNtpUpdate = PrvCurrentUpTimeSeconds();
	}
	else {
		m_lastNtpUpdate = 0;
	}
		
	return NITZHANDLER_RETURN_SUCCESS;
	
}

int	 TimePrefsHandler::nitzHandlerOffsetValue(NitzParameters& nitz,int& flags,std::string& r_statusMsg)
{
	
	if (isNITZTZEnabled() == false)
		return NITZHANDLER_RETURN_SUCCESS;

	nitzHandlerSpecialCaseOffsetValue(nitz,flags,r_statusMsg);

	if (nitz._tzvalid == false)
		return NITZHANDLER_RETURN_SUCCESS;			///this is not a message with a tz offset value...return (not an error)
	
	//try and set the timezone
	const TimeZoneInfo * selectedZone = NULL;
	
	
	//check to see if I've been told to use generic timezones exclusively
	if ( flags & NITZHANDLER_FLAGBIT_GZONEFORCE )
	{
		//pick a generic zone
		selectedZone = timeZone_GenericZoneFromOffset(nitz._offset);
		setTimeZone(selectedZone);					///setTimeZone() has a failsafe against NULLs being passed in so this is safe 
		signalReceivedNITZUpdate(false,true);
		return NITZHANDLER_RETURN_SUCCESS;
	}
	
	int effectiveDstValue = nitz._dst;
	//try and pick a zone based on offset and dst passed in. In the case of dstvalid = false, assume dst=1. This will be corrected when an updated message comes in (IF it comes in...else, 1 it is)
	if (nitz._dstvalid)
	{
		flags |= NITZHANDLER_FLAGBIT_SKIP_DST_SELECT;
	}
	else
		effectiveDstValue = 0;
	
	selectedZone = timeZone_ZoneFromOffset(nitz._offset,effectiveDstValue,nitz._mcc);
	if (selectedZone == NULL)
	{
		//couldn't get one with this combination...if generic zones are allowed, pick one of those
		if ( flags & NITZHANDLER_FLAGBIT_GZONEALLOW )
		{
			//pick a generic zone
			selectedZone = timeZone_GenericZoneFromOffset(nitz._offset);
		}
	}

	setTimeZone(selectedZone);					///setTimeZone() has a failsafe against NULLs being passed in so this is safe 
	signalReceivedNITZUpdate(false,true);
	return NITZHANDLER_RETURN_SUCCESS;			
}

int  TimePrefsHandler::nitzHandlerDstValue(NitzParameters& nitz,int& flags,std::string& r_statusMsg)
{
	
	// enforcing rules for dst according to some test cases that need to be passed with explicit assumptions on dstvalid <-> dst=x implications. Therefore
	// 					 	will handle everything in the nitzHandlerOffsetValue() fn
	
//	if (isNITZTZEnabled() == false)
//		return NITZHANDLER_RETURN_SUCCESS;
//	
//	if (nitz._dstvalid == false)
//		return NITZHANDLER_RETURN_SUCCESS;		//this is not a message with a dst value...return (not an error)
//	
//	if (flags & NITZHANDLER_FLAGBIT_SKIP_DST_SELECT)
//		return NITZHANDLER_RETURN_SUCCESS;		//something up the chain already figured dst into the mix...skip all this to avoid reselecting a TZ
//	
//	//take the currently set timezone's offset, and this NitzParameter's dst value, and run the timezone selection sequence
//	
//	
//	const TimeZoneInfo * selectedZone = NULL;
//	//check to see if I've been told to use generic timezones exclusively
//	if( flags & NITZHANDLER_FLAGBIT_GZONEFORCE )
//	{
//		//pick a generic zone
//		selectedZone = timeZone_GenericZoneFromOffset(m_cpCurrentTimeZone->offsetToUTC);
//		setTimeZone(selectedZone);					///setTimeZone() has a failsafe against NULLs being passed in so this is safe 
//		return NITZHANDLER_RETURN_SUCCESS;
//	}
//	
//	selectedZone = timeZone_ZoneFromOffset(m_cpCurrentTimeZone->offsetToUTC,nitz._dst);
//	if (selectedZone == NULL)
//	{
//		//couldn't get one with this combination...if generic zones are allowed, pick one of those
//		if ( flags & NITZHANDLER_FLAGBIT_GZONEALLOW )
//		{
//			//pick a generic zone
//			selectedZone = timeZone_GenericZoneFromOffset(m_cpCurrentTimeZone->offsetToUTC);
//		}
//	}
//	setTimeZone(selectedZone);					///setTimeZone() has a failsafe against NULLs being passed in so this is safe 
	return NITZHANDLER_RETURN_SUCCESS;		
}

int  TimePrefsHandler::nitzHandlerExit(NitzParameters& nitz,int& flags,std::string& r_statusMsg)
{
	//nothing special to do here at this time...just a hook to allow future post-process
	return NITZHANDLER_RETURN_SUCCESS;
}
	
void  TimePrefsHandler::nitzHandlerSpecialCaseOffsetValue(NitzParameters& nitz,int& flags,std::string& r_statusMsg)
{
	//Special Case #1:  If the MCC is France (208), and the offset value is 120, then flip that to offset 60, tzvalid=true, dst=1, dstvalid=true
	if ((nitz._mcc == 208) && (nitz._offset == 120))
	{
		nitz._tzvalid = true;
		nitz._offset = 60;
		nitz._dst = 1;
		nitz._dstvalid = true;
		g_warning("%s: Special Case 1 applied! MCC 208 offset 120 -> offset 60, dst=1",__FUNCTION__);
		return;
	}

	//Special Case #2:  If the MCC is Spain (214), and the offset value is 120, then flip that to offset 60, tzvalid=true, dst=1, dstvalid=true
	if ((nitz._mcc == 214) && (nitz._offset == 120))
	{
		nitz._tzvalid = true;
		nitz._offset = 60;
		nitz._dst = 1;
		nitz._dstvalid = true;
		g_warning("%s: Special Case 2 applied! MCC 214 offset 120 -> offset 60, dst=1",__FUNCTION__);
		return;
	}
}

int TimePrefsHandler::timeoutFunc()
{
	if (m_timeoutCycleCount > 0)
	{
		//the timeout has been extended..decrement count and return signaling that cycle should repeat
		--m_timeoutCycleCount;
		g_warning("%s: Resetting the timeout cycle, count is now %d",__FUNCTION__,m_timeoutCycleCount);
		return TIMEOUTFN_RESETCYCLE;
	}
	
	//else, timeout needs to do work
	//run the nitz chain
	int nitzFlags = 0;
	std::string errorText,nitzFnMsg;
	NitzParameters nitzParam;		//this will be the "working copy" that the handlers will modify
	
	g_warning("%s: Running the NITZ chain...",__FUNCTION__);
	if (timeoutNitzHandlerEntry(nitzParam,nitzFlags,nitzFnMsg) != NITZHANDLER_RETURN_SUCCESS)
	{
		errorText = "timeout-nitz message failed entry: "+nitzFnMsg;
		goto Done_timeoutFunc;
	}
	if (timeoutNitzHandlerTimeValue(nitzParam,nitzFlags,nitzFnMsg) != NITZHANDLER_RETURN_SUCCESS)
	{
		errorText = "timeout-nitz message failed in time-value handler: "+nitzFnMsg;
		goto Done_timeoutFunc;
	}
	if (timeoutNitzHandlerOffsetValue(nitzParam,nitzFlags,nitzFnMsg) != NITZHANDLER_RETURN_SUCCESS)
	{
		errorText = "timeout-nitz message failed in timeoffset-value handler: "+nitzFnMsg;
		goto Done_timeoutFunc;
	}
	if (timeoutNitzHandlerDstValue(nitzParam,nitzFlags,nitzFnMsg) != NITZHANDLER_RETURN_SUCCESS)
	{
		errorText = "timeout-nitz message failed in timedst-value handler: "+nitzFnMsg;
		goto Done_timeoutFunc;
	}
	if (timeoutNitzHandlerExit(nitzParam,nitzFlags,nitzFnMsg) != NITZHANDLER_RETURN_SUCCESS)
	{
		errorText = "timeout-nitz message failed exit: "+nitzFnMsg;
		goto Done_timeoutFunc;
	}

	//if successfully completed, then reset the last nitz parameter member and flags
	if (m_p_lastNitzParameter == NULL)
		m_p_lastNitzParameter = new NitzParameters(nitzParam);
	else
		*m_p_lastNitzParameter = nitzParam;

	m_lastNitzFlags = nitzFlags;

Done_timeoutFunc:

	g_warning("%s: NITZ chain completed: %s",__FUNCTION__,(errorText.size() ? errorText.c_str() : "Ok"));
	
	//if neither automatic time or automatic zone were turned on, then skip advertising the system time or nitz valid status
	/*
	 * The whole chain run could have just been avoided at the cb__ function level if "manual" mode was on....
	 * The chains were still run despite "manual" mode being on, because it may be good to have a structure in place that can
	 * track and possibly remember nitz messages even though they are not getting applied. That type of thing isn't being used here
	 * (yet) but if a switch to that is needed, it's already mostly in place. Having this run always isn't that costly anyways so
	 * there is no real drawback.
	 * 
	 */
	if ((isNITZTimeEnabled() == false) && (isNITZTZEnabled() == false))
	{
		g_warning("%s: Manual mode was on...not changing any NITZ variables/state",__FUNCTION__);
		//finish, indicating I'd like the periodic source to go away 
		return TIMEOUTFN_ENDCYCLE;
	}
	
	//figure out if everything was set ok
	if ((nitzParam._timevalid == false) && (nitzParam._tzvalid == false) && (nitzParam._dstvalid == false))
	{
		m_immNitzTimeValid = false;
		m_immNitzZoneValid = false;
		g_warning("%s: Special-NITZ FAIL scenario detected - UI prompt to follow",__FUNCTION__);
		//no...set the overall validity flags (for tracking UI) to invalid, and post the inability to set the time
		(void)TimePrefsHandler::transitionNITZValidState(false,false);
		markLastNITZInvalid();
		postNitzValidityStatus();
	}
	else
	{
		bool totallyGoodNitz = (nitzParam._timevalid) && (nitzParam._tzvalid) && (nitzParam._dstvalid);
		time_t dbg_time_outp = time(NULL);
		g_warning("%s: NITZ FINAL: At least something was ok (timevalid = %s,tzvalid = %s,dstvalid = %s), time is now %s",
				__FUNCTION__,(nitzParam._timevalid ? "true" : "false"),
				(nitzParam._tzvalid ? "true" : "false"),
				(nitzParam._dstvalid ? "true" : "false"),
				(ctime(&dbg_time_outp)));

		//yes...at least something was ok 
		//set the right overall validity flags (for tracking UI), post the timechange, and launch apps
		(void)TimePrefsHandler::transitionNITZValidState(totallyGoodNitz,false);
		if (totallyGoodNitz)
			markLastNITZValid();
		else
			markLastNITZInvalid();
		// also set the new sub-values for validity (redundant, but not disturbing the old nitz state machine for now
		//should be phased out slowly though from here on in)
		m_immNitzTimeValid = nitzParam._timevalid;
		m_immNitzZoneValid = (nitzParam._tzvalid && nitzParam._dstvalid);
		postSystemTimeChange();
		launchAppsOnTimeChange();
	}
	
	//then finish, indicating I'd like the periodic source to go away 
	return TIMEOUTFN_ENDCYCLE;
}

void TimePrefsHandler::startBootstrapCycle(int delaySeconds)
{

#if defined(MACHINE_topaz) || defined(DESKTOP) || defined(MACHINE_opal)
	g_warning("%s: On a Machine without Cellular...kicking off time-set timeout cycle in %d seconds (to allow machine to settle down)", __FUNCTION__,delaySeconds);
	if (m_p_lastNitzParameter)
	{
		m_p_lastNitzParameter->_timevalid = false;	//this will force NTP
	}
	startTimeoutCycle(delaySeconds);
#endif

}

void TimePrefsHandler::startTimeoutCycle()
{
	startTimeoutCycle(TIMEOUT_INTERVAL_SEC);
}

void TimePrefsHandler::startTimeoutCycle(unsigned int timeoutInSeconds)
{
	//if one is already running, extend it up to one cycle
	if (m_gsource_periodic)
	{
		m_timeoutCycleCount = (m_timeoutCycleCount > 0 ? 1 : 0);
		g_warning("%s: timeout cycle count extended , now %d",__FUNCTION__,m_timeoutCycleCount);
		return;
	}
	
	//else, create a timeout source and attach it
	//create a new periodic source
	
	if (timeoutInSeconds == 0)
	{
		timeoutInSeconds = strtoul((PrefsDb::instance()->getPref(".sysservice-time-nitzHandlerTimeout")).c_str(),NULL,10L);
		if ((timeoutInSeconds == 0) || (timeoutInSeconds > 300))
			timeoutInSeconds = TIMEOUT_INTERVAL_SEC;
	}
	
	m_gsource_periodic = g_timeout_source_new_seconds(timeoutInSeconds);
	if (m_gsource_periodic == NULL) 
	{
		g_warning("Failed to create periodic source");
		return;
	}
	//add the callback functions to it
	g_source_set_callback(m_gsource_periodic,TimePrefsHandler::source_periodic,m_gsource_periodic,TimePrefsHandler::source_periodic_destroy);
			//attach the new periodic source
	GMainContext *context = g_main_loop_get_context(g_gmainLoop);
	m_gsource_periodic_id = g_source_attach(m_gsource_periodic,context);
	if (m_gsource_periodic_id == 0) 
	{
		g_warning("%s: Failed to attach periodic source",__FUNCTION__);
		//destroy the periodic source
		g_source_destroy(m_gsource_periodic);
		m_gsource_periodic = NULL;
	}
	else {
		g_warning("%s: Timeout cycle of %d seconds started",__FUNCTION__,timeoutInSeconds);
		g_source_unref(m_gsource_periodic);		//it's owned now by the context
	}

}

void TimePrefsHandler::timeout_destroy(gpointer userData)
{
	if (userData != m_gsource_periodic)
		return;			//makes it harder for someone to call this function directly
	
	m_gsource_periodic_id = 0;
	m_gsource_periodic = NULL;
}

int  TimePrefsHandler::timeoutNitzHandlerEntry(NitzParameters& nitz,int& flags,std::string& r_statusMgs)
{
	//if a previous message was valid, use the flags from the previous message.
	if (m_p_lastNitzParameter)
	{
		flags = m_lastNitzFlags;
		nitz = *m_p_lastNitzParameter;
	}
	else
	{
		//rescan from prefs
		if (PrefsDb::instance()->getPref("timeZonesUseGenericExclusively") == "true")
			flags |= NITZHANDLER_FLAGBIT_GZONEFORCE;
		if (PrefsDb::instance()->getPref("AllowGenericTimezones") == "true")
			flags |= NITZHANDLER_FLAGBIT_GZONEALLOW;
		if (PrefsDb::instance()->getPref("AllowMCCAssistedTimezones") == "true")
			flags |= NITZHANDLER_FLAGBIT_MCCALLOW;
		if (PrefsDb::instance()->getPref("AllowNTPTime") == "true")
			flags |= NITZHANDLER_FLAGBIT_NTPALLOW;
	}
	
	return NITZHANDLER_RETURN_SUCCESS;
}

int  TimePrefsHandler::timeoutNitzHandlerTimeValue(NitzParameters& nitz,int& flags,std::string& r_statusMsg)
{
	/*
	 * Even though NTP was attempted in the original handler when the NITZ message came in, if that was unsuccessful (which means _timevalid is still == false),
	 * try again here. Maybe a retry after the short delay it took to get here will work
	 */
	
	//check if network time is allowed, if not, then exit
	if (isNITZTimeEnabled() == false)
		return NITZHANDLER_RETURN_SUCCESS;
		
	//check the timevalid field
	if (nitz._timevalid)
	{
		return NITZHANDLER_RETURN_SUCCESS;			//time was successfully applied in the original nitz cycle...nothing to do
	}
	
	//else, check to see if NTP time is allowed.
	if ((flags & NITZHANDLER_FLAGBIT_NTPALLOW) == 0)
	{
		return NITZHANDLER_RETURN_SUCCESS;			//no NTP allowed...nothing left to do
	}
	
	//get NTP time if possible
	time_t ntpUtc;
	if (getUTCTimeFromNTP(ntpUtc) == 0)
	{
		//ok, got it from NTP...
		nitz._timevalid = true;
		memcpy(&(nitz._timeStruct),gmtime(&ntpUtc),sizeof(struct tm));		//for posterity; try to keep the working copy in sync so that down-chain stuff can use it if it wants
		systemSetTime(ntpUtc);
		signalReceivedNITZUpdate(true,false);
		m_lastNtpUpdate = PrvCurrentUpTimeSeconds();
		return NITZHANDLER_RETURN_SUCCESS;
	}
	else {
		m_lastNtpUpdate = 0;
	}
	
	return NITZHANDLER_RETURN_SUCCESS;
	
}

int	 TimePrefsHandler::timeoutNitzHandlerOffsetValue(NitzParameters& nitz,int& flags,std::string& r_statusMsg)
{
	
	//if automatic timezone is not allowed, then just exit
	if (isNITZTZEnabled() == false)
		return NITZHANDLER_RETURN_SUCCESS;
		
	//check the tzvalid field
	if (nitz._tzvalid)
	{
		return NITZHANDLER_RETURN_SUCCESS;			//time zone was successfully applied in the original nitz cycle...nothing to do
	}
	
	const TimeZoneInfo * tz = NULL;
	if ( flags & NITZHANDLER_FLAGBIT_MCCALLOW )
	{
		//try and pick a zone by MCC
		tz = timeZone_ZoneFromMCC(nitz._mcc,nitz._mnc);
		if (tz)
		{
			//found one!  ...set it...but first, see if the "name" is set. If not, reselect based on the offset and dst (not all MCC table zones have names)
			nitz._offset = tz->offsetToUTC;
			nitz._dst = tz->dstSupported;
			if (tz->name.empty())
			{
				tz = timeZone_ZoneFromOffset(nitz._offset,nitz._dst);
				//check to see that this zone's country doesn't span multiple zones...if it does, then it can't be used,
				// so exit early
				//if the name WAS set though, assume that the intent was to override this logic and set it
				if (isCountryAcrossMultipleTimeZones(*tz))
					return NITZHANDLER_RETURN_SUCCESS;
			}
			nitz._tzvalid = true;
			nitz._dstvalid = true;
			setTimeZone(tz);
			signalReceivedNITZUpdate(false,true);
			return NITZHANDLER_RETURN_SUCCESS;
		}
	}
	
	return NITZHANDLER_RETURN_SUCCESS;
}

int  TimePrefsHandler::timeoutNitzHandlerDstValue(NitzParameters& nitz,int& flags,std::string& r_statusMsg)
{
	//nothing to do here...dst can't really be helped if it didn't come in initially

	if (flags & NITZHANDLER_FLAGBIT_SKIP_DST_SELECT)
		return NITZHANDLER_RETURN_SUCCESS;		//something up the chain already figured dst into the mix...skip all this to avoid reselecting a TZ
	
	///However, some networks  seem to be sending dstvalid = false even when it shouldn't be. This hidden setting defaults to ignoring that
	//				but if it's set "true", then dstvalid = false will result in it being considered a NITZ TZ set failure
	if (PrefsDb::instance()->getPref(".sysservice-time-strictDstErrors") != "true")
		nitz._dstvalid = true;
	
	return NITZHANDLER_RETURN_SUCCESS;
}

int  TimePrefsHandler::timeoutNitzHandlerExit(NitzParameters& nitz,int& flags,std::string& r_statusMsg)
{
	return NITZHANDLER_RETURN_SUCCESS;
}
	
void TimePrefsHandler::setPeriodicTimeSetWakeup()
{
	
	g_warning("%s called",__FUNCTION__);
	
	if (getPrivateHandle() == NULL)
	{
		//not yet on the bus
		m_sendWakeupSetToPowerD = true;
		return;
	}
	
	//if (isNITZTimeEnabled() && isNTPAllowed())				TODO:  should really be this and not the line below, but since "AllowNTPTime" setting/key currently doesn't
																	// have a "changed" handler, there's no way to detect that it has been turned (back)on, so if it ever
																	// gets set off, there will be no more NTP events scheduled even if it gets turned back on
	if (isNITZTimeEnabled())
	{
		LSError lserror;
		LSErrorInit(&lserror);
	
		std::string interval = PrefsDb::instance()->getPref(".sysservice-time-autoNtpInterval");
		uint32_t timev = strtoul(interval.c_str(),NULL,10);
		if ((timev < 300) || (timev > 86400))
			timev = 86399;							//24 hour default (23h.59m.59s actually)

		std::string timeStr;

		uint32_t hr = timev / 3600;
		uint32_t mr = timev % 3600;

		if (hr >= 10)
			timeStr = Utils::toSTLString<uint32_t>(hr) + std::string(":");
		else
			timeStr = std::string("0")+Utils::toSTLString<uint32_t>(hr) + std::string(":");

		if (mr >= 600)
			timeStr.append(Utils::toSTLString<uint32_t>(mr/60)+std::string(":"));
		else
			timeStr.append(std::string("0")+Utils::toSTLString<uint32_t>(mr/60)+std::string(":"));

		uint32_t sr = mr % 60;
		if (sr >= 10)
			timeStr.append(Utils::toSTLString<uint32_t>(sr));
		else
			timeStr.append(std::string("0")+Utils::toSTLString<uint32_t>(sr));
		
		//std::string payload = "{\"key\":\"sysservice_ntp_periodic\",\"in\":\"01:00:00\",\"wakeup\":false,\"uri\":\"palm://com.palm.systemservice/time/setTimeWithNTP\",\"params\":\"{'source':'periodic'}\"}";
		
		std::string payload = std::string("{\"key\":\"sysservice_ntp_periodic\",\"in\":\"")
								+timeStr
								+std::string("\",\"wakeup\":false,\"uri\":\"palm://com.palm.systemservice/time/setTimeWithNTP\",\"params\":\"{'source':'periodic'}\"}");
		
		g_warning("%s: scheduling event for %s in the future or when the device next wakes, whichever is later",__FUNCTION__,timeStr.c_str());
		bool lsCallResult = LSCall(getPrivateHandle(),
				"palm://com.palm.power/timeout/set",
				payload.c_str(),
				cbSetPeriodicWakeupPowerDResponse,this,NULL, &lserror);

		if (!lsCallResult) {
			g_warning("%s: call to powerD failed",__FUNCTION__);
			LSErrorFree(&lserror);
			m_sendWakeupSetToPowerD = true;
		}
		else
		{
			m_sendWakeupSetToPowerD = false;		//unless the response tells me otherwise, assume it succeeded so supress reschedule on powerD reconnect
		}
	}
	else
	{
		m_sendWakeupSetToPowerD = false;
	}
}


bool TimePrefsHandler::isNTPAllowed()
{
	return (PrefsDb::instance()->getPref("AllowNTPTime") == "true");
}

void TimePrefsHandler::signalReceivedNITZUpdate(bool time,bool zone)
{
	
	LSError lsError;
	
	if (time)
	{
		LSErrorInit(&lsError);
		if (!(LSCall(getPrivateHandle(),
				"luna://com.palm.systemservice/setPreferences",
				"{\"receiveNetworkTimeUpdate\":true}",
				NULL,this,NULL, &lsError)))
		{
			LSErrorFree(&lsError);
		}
	}
	if (zone)
	{
		LSErrorInit(&lsError);
		if (!(LSCall(getPrivateHandle(),
				"luna://com.palm.systemservice/setPreferences",
				"{\"receiveNetworkTimezoneUpdate\":true}",
				NULL,this,NULL, &lsError)))
		{
			LSErrorFree(&lsError);
		}
	}
}

//static
void TimePrefsHandler::dbg_time_timevalidOverride(bool& timevalid)
{
	if (PrefsDb::instance()->getPref(".sysservice-dbg-time-debugEnable") != "true")
		return;
	
	g_warning("%s: !!!!!!!!!!!!!!! USING DEBUG OVERRIDES !!!!!!!!!!!!!!",__FUNCTION__);
	std::string v = PrefsDb::instance()->getPref(".sysservice-dbg-time-timevalid");
	if (strcasecmp(v.c_str(),"true") == 0)
		timevalid = true;
	else if (strcasecmp(v.c_str(),"false") == 0)
		timevalid = false;
	
	g_warning("%s: timevalid <--- %s",__FUNCTION__,(timevalid ? "true" : "false"));
}

//static
void TimePrefsHandler::dbg_time_tzvalidOverride(bool& tzvalid)
{
	if (PrefsDb::instance()->getPref(".sysservice-dbg-time-debugEnable") != "true")
		return;

	g_warning("%s: !!!!!!!!!!!!!!! USING DEBUG OVERRIDES !!!!!!!!!!!!!!",__FUNCTION__);
	std::string v = PrefsDb::instance()->getPref(".sysservice-dbg-time-tzvalid");
	if (strcasecmp(v.c_str(),"true") == 0)
		tzvalid = true;
	else if (strcasecmp(v.c_str(),"false") == 0)
		tzvalid = false;
	
	g_warning("%s: tzvalid <--- %s",__FUNCTION__,(tzvalid ? "true" : "false"));
}

//static
void TimePrefsHandler::dbg_time_dstvalidOverride(bool& dstvalid)
{
	if (PrefsDb::instance()->getPref(".sysservice-dbg-time-debugEnable") != "true")
		return;

	g_warning("%s: !!!!!!!!!!!!!!! USING DEBUG OVERRIDES !!!!!!!!!!!!!!",__FUNCTION__);
	std::string v = PrefsDb::instance()->getPref(".sysservice-dbg-time-dstvalid");
	if (strcasecmp(v.c_str(),"true") == 0)
		dstvalid = true;
	else if (strcasecmp(v.c_str(),"false") == 0)
		dstvalid = false;
	
	g_warning("%s: dstvalid <--- %s",__FUNCTION__,(dstvalid ? "true" : "false"));
}
	
//static
bool TimePrefsHandler::cbSetPeriodicWakeupPowerDResponse(LSHandle* lsHandle, LSMessage *message,
							void *user_data)
{
    // {"returnValue": boolean}
    VALIDATE_SCHEMA_AND_RETURN(lsHandle,
                               message,
                               SCHEMA_1(REQUIRED(returnValue, boolean)));

	const char* str = LSMessageGetPayload(message);
	if( !str ) {
		return false;
	}

	g_message("%s: received message %s",__FUNCTION__,str);
	
	TimePrefsHandler* th = (TimePrefsHandler*) user_data;
	if (th == NULL)
	{
		g_critical("%s: user_data passed as NULL!",__FUNCTION__);
		return true;
	}
	json_object * root = json_tokener_parse(str);
	json_object * label = 0;
	
	if ((!root) || is_error(root)) 
	{
		root=0;
		goto Done_cbSetPeriodicWakeupPowerDResponse;
	}
	
	
	label = Utils::JsonGetObject(root,"returnValue");
	if (label)
	{
		//this is a response to a call...
		th->m_sendWakeupSetToPowerD = !(json_object_get_boolean(label));
		//if it was true, then the call succeeded so supress sending it again if the service disconnects+reconnects (by setting the m_sendWakeupSetToPowerD to false)
	}
	else
	{
		//it's an actual event...
		if (th->isNITZTimeEnabled() && th->isNTPAllowed())
		{
			//get NTP time if possible
			time_t ntpUtc;
			if (getUTCTimeFromNTP(ntpUtc) == 0)
			{
				//ok, got it from NTP...
				systemSetTime(ntpUtc);
				th->m_lastNtpUpdate = PrvCurrentUpTimeSeconds();
			}
			else {
				th->m_lastNtpUpdate = 0;
			}
		}
		
		//schedule another
		th->setPeriodicTimeSetWakeup();
	}
	
Done_cbSetPeriodicWakeupPowerDResponse:

	if (root)
		json_object_put(root);
	
	return true;
}

bool TimePrefsHandler::cbServiceStateTracker(LSHandle* lsHandle, LSMessage *message,
								void *user_data)
{
    // {"serviceName": string, "connected": boolean}
    VALIDATE_SCHEMA_AND_RETURN(lsHandle,
                               message,
                               SCHEMA_2(REQUIRED(serviceName, string), REQUIRED(connected, boolean)));

	LSError lsError;
	LSErrorInit(&lsError);

	json_object * root = 0;
	const char* str = LSMessageGetPayload(message);
	if( !str )
		return false;

	TimePrefsHandler * th = (TimePrefsHandler *)user_data;
	
	if (th == NULL) 
	{
		g_critical("%s: user_data passed as NULL!",__FUNCTION__);
		return true;
	}
	root = json_tokener_parse(str);
	if ((root == NULL) || (is_error(root))) {
		root = NULL;
		return true;
	}

	json_object * label = Utils::JsonGetObject(root,"serviceName");
	if (!label)
	{
		json_object_put(root);
		return true;
	}
	
	std::string serviceName = json_object_get_string(label);
	bool isConnected = false;
	label = Utils::JsonGetObject(root,"connected");
	if (label != NULL)
	{
		isConnected = json_object_get_boolean(label);
	}
	
	if (serviceName == "com.palm.power")
	{
		if ((isConnected) && (th->m_sendWakeupSetToPowerD))
		{
			//powerD is connected, and the flag is set to schedule a periodic wakeup for NTP
			th->setPeriodicTimeSetWakeup();
		}
	}
    else if (serviceName == "com.palm.telephony") {
		if (isConnected)
            LSCallOneReply(th->getPrivateHandle(), "palm://com.palm.telephony/platformQuery", "{}",
                           cbTelephonyPlatformQuery, th, NULL, &lsError);
    }

	json_object_put(root);

	return true;
	
}

//static 
bool TimePrefsHandler::cbGetSystemTime(LSHandle* lsHandle, LSMessage *message,
							void *user_data)
{
    SUBSCRIBE_SCHEMA_RETURN(lsHandle, message);

    bool        retVal;
	LSError     lsError;
	const char* reply = 0;
	char tzoneabbr_cstr[16] = {0};
	std::string tzAbbr;
	json_object* json = 0;
	json_object* localtime_json = 0;
	struct tm * p_localtime_s;
	time_t utctime;
	std::string nitzValidity;
	
	TimePrefsHandler* th = (TimePrefsHandler*) user_data;

	LSErrorInit(&lsError);

	bool subscribed = false;

	if (LSMessageIsSubscription(message)) 
	{
		retVal = LSSubscriptionAdd(lsHandle,"getSystemTime", message, &lsError);
		if (!retVal) 
		{
			LSErrorFree (&lsError);
			subscribed=false;
			reply = "{\"returnValue\":false,\"subscribed\":false}";
			goto Done;
		}
		else
			subscribed=true;
	}

	utctime = time(NULL);
	p_localtime_s = localtime(&utctime);
	
	json = json_object_new_object();
	json_object_object_add(json, (char*) "utc", json_object_new_int((int)time(NULL)));
	localtime_json = json_object_new_object();
	json_object_object_add(localtime_json,(char *)"year",json_object_new_int(p_localtime_s->tm_year + 1900));
	json_object_object_add(localtime_json,(char *)"month",json_object_new_int(p_localtime_s->tm_mon + 1));
	json_object_object_add(localtime_json,(char *)"day",json_object_new_int(p_localtime_s->tm_mday));
	json_object_object_add(localtime_json,(char *)"hour",json_object_new_int(p_localtime_s->tm_hour));
	json_object_object_add(localtime_json,(char *)"minute",json_object_new_int(p_localtime_s->tm_min));
	json_object_object_add(localtime_json,(char *)"second",json_object_new_int(p_localtime_s->tm_sec));
	json_object_object_add(json,(char *)"localtime",localtime_json);
	
	json_object_object_add(json, (char*) "offset", json_object_new_int(th->offsetToUtcSecs()/60));
	
	if (th->currentTimeZone()) {
		json_object_object_add(json, (char*) "timezone", json_object_new_string(th->currentTimeZone()->name.c_str()));
		//get current time zone abbreviation
		strftime (tzoneabbr_cstr,16,"%Z",localtime(&utctime));
		tzAbbr = std::string(tzoneabbr_cstr);
	}
	else {
		json_object_object_add(json, (char*) "timezone", json_object_new_string((char*) "UTC"));
		tzAbbr = std::string("UTC");		//default to something
	}
	json_object_object_add(json, (char*) "TZ", json_object_new_string(tzAbbr.c_str()));
	
	json_object_object_add(json, (char*) "timeZoneFile", json_object_new_string(const_cast<char*>(s_tzFilePath)));
	
	nitzValidity = PrefsDb::instance()->getPref("nitzValidity");
	
	if (nitzValidity == NITZVALIDITY_STATE_NITZVALID)
		json_object_object_add(json,(char*) "NITZValid", json_object_new_boolean(true));
	else if (nitzValidity == NITZVALIDITY_STATE_NITZINVALIDUSERNOTSET)
		json_object_object_add(json,(char*) "NITZValid", json_object_new_boolean(false));
	
	reply = json_object_to_json_string(json);
	
	//**DEBUG validate for correct UTF-8 output
	 if (!g_utf8_validate (reply, -1, NULL))
	 {
		 g_warning("cbGetSystemTime(): bus reply fails UTF-8 validity check! [%s]",reply);
	 }
		
Done:

	retVal = LSMessageReply(lsHandle, message, reply, &lsError);
	if (!retVal)
		LSErrorFree (&lsError);

	if (json)
		json_object_put(json);

	return true;
}

//static 
bool TimePrefsHandler::cbGetSystemTimezoneFile(LSHandle* lsHandle, LSMessage *message,
							void *user_data)
{
    EMPTY_SCHEMA_RETURN(lsHandle,message);
    bool        retVal;
	LSError     lsError;
	const char* reply = 0;

	json_object* json = 0;
	LSErrorInit(&lsError);

	json = json_object_new_object();
	json_object_object_add(json, (char*) "timeZoneFile", json_object_new_string(const_cast<char*>(s_tzFilePath)));
	json_object_object_add(json, (char*) "subscribed",json_object_new_boolean(false));	//no subscriptions on this; make that explicit!
	reply = json_object_to_json_string(json);
		
	retVal = LSMessageReply(lsHandle, message, reply, &lsError);
	if (!retVal)
		LSErrorFree (&lsError);

	if (json)
		json_object_put(json);

	return true;
}

//static 
bool TimePrefsHandler::cbSetTimeChangeLaunch(LSHandle* lsHandle, LSMessage *message,
							void *user_data)
{
    bool        retVal;
	LSError     lsError;

	struct json_object* jsonInput = 0;
	struct json_object* jsonOutput = 0;
	struct json_object* storedJson = 0;
	struct json_object* storedJson_listArray = 0;
	struct json_object* tmpJson_listArray=0;
	struct json_object* storedJson_listObject = 0;
	int listIdx;
	
	struct json_object* label = 0;
	LSErrorInit(&lsError);
	std::string errorText;
	
	std::string appId;
	bool active;
	struct json_object * params;
	
	std::string rawCurrentPref;
	
	//format:  { "appId":<string; REQ>, "active":<boolean; OPT - default true> , "parameters":<string encoded json object>; OPT - default ""> }
    VALIDATE_SCHEMA_AND_RETURN(lsHandle,
                               message,
                               SCHEMA_3(REQUIRED(appId, string), OPTIONAL(active, boolean), OPTIONAL(parameters, string)));
	
	const char* str = LSMessageGetPayload(message);
	if( !str ) {
		return false;
	}
	
	jsonInput = json_tokener_parse(str);

	if ((!jsonInput) || is_error(jsonInput)) {
		jsonInput=0;
		errorText = "couldn't parse json parameters";
		goto Done;
	}
	
	label = Utils::JsonGetObject(jsonInput,"appId");
	if (label == NULL) {
		errorText = "missing required parameter appId";
		goto Done;
	}
	appId = json_object_get_string(label);
	
	label = Utils::JsonGetObject(jsonInput,"active");
	if (label) {
		active = json_object_get_boolean(label);
	}
	else
		active = true;
	
	params = Utils::JsonGetObject(jsonInput,"parameters");
	if (params)
		params = json_object_get(params);
	else
		params = json_object_new_object();
	
	/*
	 * 
	 * Format of the stored app launch list
	 * 
	 * {"launchList":[ { "appId":"com.palm.app.x","active":true,"parameters":""},{ "appId":"com.palm.app.y" , "active":true , "parameters":""},... ]}
	 * 
	 */
	
	//get the currently stored list of launch apps
	rawCurrentPref = PrefsDb::instance()->getPref("timeChangeLaunch");
	storedJson = json_tokener_parse(rawCurrentPref.c_str());
	if ((storedJson == NULL) || (is_error(storedJson))) {
		storedJson = json_object_new_object();
	}
	
	//get the launchList array object out of it
	storedJson_listArray = Utils::JsonGetObject(storedJson,"launchList");
	if ((storedJson_listArray == NULL) && (active)) {
		//nothing in the list yet, and I need to add...
		storedJson_listArray = json_object_new_array();
		json_object_object_add(storedJson,(char *) "launchList",storedJson_listArray);
	}
	else if ((storedJson_listArray == NULL) && (active == false)) {
		//nothing in the list yet, and I was told to remove; nothing to do, so just go to Done
		errorText = "cannot deactivate an appId that isn't in the list";
		goto Done;
	}
	else if ((storedJson_listArray) && (active == false)) {
		//list exists, and I have to remove
		goto Remove;
	}
	//else, Add
	goto Add;		//making it explicit
	
Add:
	//go through the array and try and find the appid (if it's in there already)
	for (listIdx=0;listIdx<json_object_array_length(storedJson_listArray);++listIdx,storedJson_listObject=NULL) {
		storedJson_listObject = json_object_array_get_idx(storedJson_listArray,listIdx);
		if (storedJson_listObject == NULL)
			continue;
		label = Utils::JsonGetObject(storedJson_listObject,"appId");
		if (label == NULL) {
			continue;		//something really bad happened; something was stored in the list w/o an appId!
		}
		std::string foundAppId = json_object_get_string(label);
		if (appId == foundAppId)
			break;
	}
	
	if (storedJson_listObject) {
		//found it...
		//json_object_object_add(storedJson_listObject,(char *)"parameters",json_object_new_string(params.c_str()));
		json_object_object_add(storedJson_listObject,(char *)"parameters",params);
	}
	else {
		//create a new object, populate, and add it to the array
		storedJson_listObject = json_object_new_object();
		json_object_object_add(storedJson_listObject,(char *)"appId",json_object_new_string(appId.c_str()));
		//json_object_object_add(storedJson_listObject,(char *)"parameters",json_object_new_string(params.c_str()));
		json_object_object_add(storedJson_listObject,(char *)"parameters",params);
		json_object_array_add(storedJson_listArray,storedJson_listObject);
	}
	goto Store;
	
Remove:

	tmpJson_listArray = json_object_new_array();
	for (listIdx=0;listIdx<json_object_array_length(storedJson_listArray);++listIdx,storedJson_listObject=NULL) {
		storedJson_listObject = json_object_array_get_idx(storedJson_listArray,listIdx);
		if (storedJson_listObject == NULL)
			continue;
		label = Utils::JsonGetObject(storedJson_listObject,"appId");
		if (label == NULL) {
			continue;		//something really bad happened; something was stored in the list w/o an appId!
		}
		std::string foundAppId = json_object_get_string(label);
		if (appId == foundAppId)
			continue;
		//else add to the new array
		json_object_array_add(tmpJson_listArray,json_object_get(storedJson_listObject));
	}
	//replace the old array with the new one in the storedJson object
	json_object_object_add(storedJson,(char *) "launchList",tmpJson_listArray);
	//TODO: verify that this object add that replaced the json array that was in the object already, actually deallocated the old json array
	
	//...proceed right below to Store
	goto Store;
	
Store:
	//store the pref back, in string form
	rawCurrentPref = json_object_to_json_string(storedJson);
	PrefsDb::instance()->setPref("timeChangeLaunch",rawCurrentPref.c_str());
	json_object_put(storedJson);
	
Done:
	
	if (jsonInput)
		json_object_put(jsonInput);
	
	jsonOutput = json_object_new_object();
	json_object_object_add(jsonOutput, (char*) "subscribed",json_object_new_boolean(false));	//no subscriptions on this; make that explicit!
	if (errorText.size()) {
		json_object_object_add(jsonOutput,(char *)"errorCode",json_object_new_string(errorText.c_str()));
		json_object_object_add(jsonOutput,(char *)"returnValue",json_object_new_boolean(false));
	}
	else 
		json_object_object_add(jsonOutput,(char *)"returnValue",json_object_new_boolean(true));
	
	const char * reply = json_object_to_json_string(jsonOutput);
		
	retVal = LSMessageReply(lsHandle, message, reply, &lsError);
	if (!retVal)
		LSErrorFree (&lsError);

	json_object_put(jsonOutput);

	return true;
}


bool TimePrefsHandler::cbGetNTPTime(LSHandle* lsHandle, LSMessage *message,
							void *user_data) 
{
	LSError lsError;
	LSErrorInit(&lsError);

	time_t utc=0;
	TimePrefsHandler::getUTCTimeFromNTP(utc);

	struct json_object * jsonOutput = json_object_new_object();
	json_object_object_add(jsonOutput, (char*) "subscribed",json_object_new_boolean(false));	//no subscriptions on this; make that explicit!
	json_object_object_add(jsonOutput,(char *)"returnValue",json_object_new_boolean(true));
	json_object_object_add(jsonOutput,(char *)"utc",json_object_new_int(utc));
	const char * reply = json_object_to_json_string(jsonOutput);

	if (!LSMessageReply(lsHandle, message, reply, &lsError))
		LSErrorFree (&lsError);

	json_object_put(jsonOutput);

	return true;
		
}

bool TimePrefsHandler::cbConvertDate(LSHandle* pHandle, LSMessage* pMessage, void* pUserData)
{
	const char* date = NULL;
	const char* source_tz = NULL;
	const char* dest_tz = NULL;
	char *status = NULL;
	char *error_text = NULL;
	bool ret = false;
	struct tm local_tm;
	char * bad_char = NULL;
	time_t local_time;
	const char * str = LSMessageGetPayload(pMessage);
	if (str == NULL)
		return false;

     // {"date": string, "source_tz": string, "dest_tz": string}
    VALIDATE_SCHEMA_AND_RETURN(pHandle,
                               pMessage,
                               SCHEMA_3(REQUIRED(date, string), REQUIRED(source_tz, string), REQUIRED(dest_tz, string)));

	json_t* json_o = json_parse_document(str);

	if (!json_o) {
		error_text = g_strdup("bad payload (no json)");
		goto respond;
	}

	ret = json_get_string(json_o, "date", &date);
	if (!ret) {
		error_text = g_strdup("no date in payload");
		goto respond;
	}

	ret = json_get_string(json_o, "source_tz", &source_tz);
	if (!ret) {
		error_text = g_strdup("no source_tz in payload");
		goto respond;
	}

	ret = json_get_string(json_o, "dest_tz", &dest_tz);
	if (!ret) {
		error_text = g_strdup("no dest_tz in payload");
		goto respond;
	}

	g_debug("%s: converting %s from %s to %s", __func__, date, source_tz, dest_tz);
	
	bad_char = (char *) strptime(date, "%Y-%m-%d %H:%M:%S", &local_tm);
	if (NULL == bad_char) {
		g_warning("bad date: %s", date);
		error_text = g_strdup_printf("unrecognized date format: '%s'", date);
		goto respond;
	} else if (*bad_char != '\0') {
		g_warning("unrecognized characters in date: '%s'", date);
		error_text = g_strdup_printf("unrecognized characters in date: '%s'", date);
		goto respond;
	}

	if (!tz_exists(source_tz)) {
		g_warning("source_tz timezone not found: '%s'", source_tz);
		error_text = g_strdup_printf("timezone not found: '%s'", source_tz);
		goto respond;
	}

	if (!tz_exists(dest_tz)) {
		g_warning("dest_tz timezone not found: '%s'", dest_tz);
		error_text = g_strdup_printf("timezone not found: '%s'", dest_tz);
		goto respond;
	}

	set_tz(source_tz);

	local_time = mktime(&local_tm);
	g_debug("0 date='%s' ctime='%s' local_time=%ld timezone=%ld", date, ctime(&local_time), local_time, timezone);

	if (!tz_exists(dest_tz)) {
		g_warning("timezone not found: '%s'", dest_tz);
		error_text = g_strdup_printf("timezone not found: '%s'", dest_tz);
	}

	set_tz(dest_tz);
	g_debug("1 date='%s' ctime='%s' local_time=%ld timezone=%ld", date, ctime(&local_time), local_time, timezone);

	g_assert(!error_text);
	status = g_strdup_printf("{\"returnValue\":true,\"date\":\"%s\"}", ctime(&local_time));

respond:
	if (!status) {
		g_assert(error_text);
		status = g_strdup_printf("{\"returnValue\":false,\"errorText\":\"%s\"}", error_text);
		g_free(error_text);
	}

	LSError lserror;
	LSErrorInit(&lserror);
	ret = LSMessageReply(pHandle, pMessage, status, &lserror);
	if (!ret)
	{
		LSREPORT(lserror);
	}
	g_free(status);

	if (json_o)
		json_free_value(&json_o);
	
	LSErrorFree (&lserror);
	return ret;
}
//static
gboolean TimePrefsHandler::source_periodic(gpointer userData)
{
	if (TimePrefsHandler::s_inst == NULL) 
	{
		g_warning("%s: instance handle is NULL!",__FUNCTION__);
		return FALSE;
	}
	int rc = TimePrefsHandler::s_inst->timeoutFunc();
	
	if (rc == TIMEOUTFN_RESETCYCLE)
	{
		g_warning("%s: Repeating timeout cycle",__FUNCTION__);
		return TRUE;
	}
	else if (rc == TIMEOUTFN_ENDCYCLE)
	{
		g_warning("%s: Ending timeout cycle",__FUNCTION__);
		return FALSE;
	}
	
	g_warning("%s fall through! (rc %d)",__FUNCTION__,rc);
	return TRUE;		///fall through! should never happen. if it does, repeat the cycle
}

//static
void TimePrefsHandler::source_periodic_destroy(gpointer userData)
{
	//tear down the timeout source
	if (TimePrefsHandler::s_inst == NULL) 
	{
		g_warning("%s: instance handle is NULL!",__FUNCTION__);
		return;
	}
	TimePrefsHandler::s_inst->timeout_destroy(userData);
}

//static
bool TimePrefsHandler::cbLaunchTimeChangeApps(LSHandle* lsHandle, LSMessage *message,
							void *user_data) 
{
    EMPTY_SCHEMA_RETURN(lsHandle, message);

	LSError lsError;
	LSErrorInit(&lsError);
	
	TimePrefsHandler * th = (TimePrefsHandler *)user_data;
	if (th == NULL)
		return false;
	
	th->launchAppsOnTimeChange();
	
	struct json_object * jsonOutput = json_object_new_object();
	json_object_object_add(jsonOutput, (char*) "subscribed",json_object_new_boolean(false));	//no subscriptions on this; make that explicit!
	json_object_object_add(jsonOutput,(char *)"returnValue",json_object_new_boolean(true));

	const char * reply = json_object_to_json_string(jsonOutput);

	if (!LSMessageReply(lsHandle, message, reply, &lsError))
		LSErrorFree (&lsError);

	json_object_put(jsonOutput);

	return true;
}

//necessary because tzset() apparently doesn't re-set if the path to a timezone file is the same as the old path that was set)
static void tzsetWorkaround(const char * newTZ) {
	
	setenv("TZ","",1);
	tzset();
	setenv("TZ",newTZ, 1);
	tzset();
	//exec the tzset busybox executable
#if defined (__arm__)
    gchar* cmdline = g_find_program_in_path("tzset");
    if (cmdline) {
        system(cmdline);
        free(cmdline); cmdline = (gchar *) 0;
    } else
        g_warning("Couldn't find tzset in PATH");
#endif
}

/*
 * 
 * 					NTP
 * 
 */


/*
 * Returns -1 for failure
 * on success, adjustedTime contains the correct current utc time
 */
//static 
int TimePrefsHandler::getUTCTimeFromNTP(time_t& adjustedTime) 
{
	if ( (PrefsDb::instance()->getPref(".sysservice-dbg-time-debugEnable") == "true")
		&& (PrefsDb::instance()->getPref(".sysservice-dbg-time-ntpBlock") == "true") )
	{
		g_warning("%s: !!!!!!!!!!!!!!! USING DEBUG OVERRIDES !!!!!!!!!!!!!!",__FUNCTION__);
		return -1;
	}
	gchar * g_stdoutBuffer = NULL;
	gchar * g_stderrBuffer = NULL;
	int rc=0;
	char * pFoundStr;
	double offsetValue;
	long loffsetValue;
	
	//try and retrieve the currently set NTP server to query
	std::string ntpServer = PrefsDb::instance()->getPref("NTPServer");
	if (ntpServer.empty()) {
		ntpServer = DEFAULT_NTP_SERVER;
	}
	
	gchar *argv[4];
	argv[0] = (gchar *)"ntpdate";
	argv[1] = (gchar *)"-bqu";
	argv[2] = (gchar *)ntpServer.c_str();
	argv[3] = 0;
	
	g_warning("%s: [NITZ , NTP] running ntpdate on %s",__FUNCTION__,ntpServer.c_str());
	GError * gerr = NULL;
	gint exit_status=0;
	GSpawnFlags flags = (GSpawnFlags)(G_SPAWN_SEARCH_PATH);
	
	/*
	 * gboolean            g_spawn_sync                 (const gchar *working_directory,
			                                                         gchar **argv,
			                                                         gchar **envp,
			                                                         GSpawnFlags flags,
			                                                         GSpawnChildSetupFunc child_setup,
			                                                         gpointer user_data,
			                                                         gchar **standard_output,
			                                                         gchar **standard_error,
			                                                         gint *exit_status,
			                                                         GError **error);
	 */

	gboolean resultStatus = g_spawn_sync(NULL,
			argv,
			NULL,
			flags,
			NULL,
			NULL,
			&g_stdoutBuffer,
			&g_stderrBuffer,
			&exit_status,
			&gerr);

	if ((!resultStatus) || (!g_stdoutBuffer)) {
		rc = -1;
		goto Done_getUTCTimeFromNTP;
	}

	//success, maybe...parse the output
	
	pFoundStr = strstr(g_stdoutBuffer,"step time server");
	if (pFoundStr == NULL) {
		//the query failed in some way
		rc = -1;
		g_warning("%s: Failed in general output parse: raw output was: %s\n",__FUNCTION__,g_stdoutBuffer);
		goto Done_getUTCTimeFromNTP;
	}

	if (sscanf(pFoundStr,"step time server %*s offset %lf sec",&offsetValue) != 1) {
		//parse error...couldn't find the time offset in the string
		rc = -1;
		g_warning("%s: Failed in specific (offset) output parse: raw output was: %s\n",__FUNCTION__,g_stdoutBuffer);
		goto Done_getUTCTimeFromNTP;
	}
	
	/*
	 * Note the following works only if the system clock is set to UTC, or in other words only 1 system time is maintained.
	 *  (this way, changing the system's local time is reflected in subsequent calls to time().
	 * 	On the desktop (ubuntu) environment, changing the local time will not affect what time() returns )  
	 * 
	 */
	loffsetValue = (long)(offsetValue >= 0.0 ? (offsetValue+0.5) : (offsetValue-0.5));
	//grab the current time
	adjustedTime = time(NULL);
	//..and adjust
	adjustedTime += loffsetValue;
	
Done_getUTCTimeFromNTP:

	if (g_stdoutBuffer)
		g_free(g_stdoutBuffer);
	if (g_stderrBuffer)
		g_free(g_stderrBuffer);
	if (gerr) {
		g_warning("TimePrefsHandler::getUTCTimeFromNTP(): error - %s",gerr->message);
		g_error_free(gerr);
	}
	
	return rc;
}

std::list<std::string> TimePrefsHandler::getTimeZonesForOffset(int offset)
{
	std::list<std::string> timeZones;

	// All timezones wih matching offset
	std::pair<TimeZoneMultiMapConstIterator, TimeZoneMultiMapConstIterator> iterPair
		= m_offsetZoneMultiMap.equal_range(offset);

	for (TimeZoneMultiMapConstIterator iter = iterPair.first; iter != iterPair.second; ++iter)
		timeZones.push_back(iter->second->name);

	return timeZones;    
}

void TimePrefsHandler::slotNetworkConnectionStateChanged(bool connected)
{
	g_warning("%s: connected: %d", __PRETTY_FUNCTION__, connected);	
	if (!connected)
		return;

	if (!isNITZTimeEnabled())
		return;

	std::string interval = PrefsDb::instance()->getPref(".sysservice-time-autoNtpInterval");
	uint32_t timev = strtoul(interval.c_str(),NULL,10);
	if ((timev < 300) || (timev > 86400))
		timev = 86399; //24 hour default (23h.59m.59s actually)

	time_t currTime = PrvCurrentUpTimeSeconds();
	g_warning("%s: currTime: %d, lastNtpUpdate: %d, interval: %d",
			  __PRETTY_FUNCTION__, (int)currTime, (int)m_lastNtpUpdate, timev);
	if ((m_lastNtpUpdate > 0) && (time_t)(m_lastNtpUpdate + timev) > currTime)
		return;

	g_warning("%s: startBootstrapCycle", __PRETTY_FUNCTION__);	
	startBootstrapCycle(0);
}

bool TimePrefsHandler::cbTelephonyPlatformQuery(LSHandle* lsHandle, LSMessage *message,
                                                void *userData)
{
    // {"extended": string, "capabilities": string, "hfenable": boolean}
    VALIDATE_SCHEMA_AND_RETURN(lsHandle,
                               message,
                               SCHEMA_3(REQUIRED(extended, string), REQUIRED(capabilities, string), REQUIRED(hfenable, boolean)));

    LSError lsError;
    LSErrorInit(&lsError);

    const char* payload = LSMessageGetPayload(message);
    if (!payload)
        return false;

    struct json_object* root = json_tokener_parse(payload);
    struct json_object* label = 0;

    if (!root || is_error(root))
        return false;

    label = json_object_object_get(root, "extended");
    if (!label || is_error(label)) {
        json_object_put(root);
        return false;
    }

    label = json_object_object_get(label, "capabilities");
    if (!label || is_error(label)) {
        json_object_put(root);
        return false;
    }

    label = json_object_object_get(label, "hfenable");
    if (!label || is_error(label) || !json_object_is_type(label, json_type_boolean)) {
        json_object_put(root);
        return false;
    }
    else {
        bool timeZoneAvailable = json_object_get_boolean(label);

        TimePrefsHandler* th = (TimePrefsHandler*) userData;
        th->m_nitzTimeZoneAvailable = timeZoneAvailable;

        g_message("%s: NITZ Time Zone Available: %d", __PRETTY_FUNCTION__, timeZoneAvailable);
    }

    json_object_put(root);
    return false;
}
