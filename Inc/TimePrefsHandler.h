/**
 *  Copyright (c) 2010-2013 LG Electronics, Inc.
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


#ifndef TIMEPREFSHANDLER_H
#define TIMEPREFSHANDLER_H

#include <map>
#include <list>
#include <vector>

#include <glib.h>

#include "PrefsHandler.h"
#include "SignalSlot.h"
#include "BroadcastTime.h"
#include "NTPClock.h"

#define		DEFAULT_NTP_SERVER	"us.pool.ntp.org"

class TimeZoneInfo;
class PreferredZones;

//a container only
class NitzParameters
{
public:
	
	struct tm	_timeStruct;
	int _offset;
	int _dst;
	int _mcc;
	int _mnc;
	bool _timevalid;
	bool _tzvalid;
	bool _dstvalid;
	uint32_t	_localtimeStamp;
	
	NitzParameters();
	NitzParameters(struct tm& timeStruct,int offset,int dst,int mcc,int mnc,bool timevalid,bool tzvalid,bool dstvalid,uint32_t remotetimeStamp);
	
	void stampTime();
	bool valid(uint32_t timeThreshold=60);
};

class TimePrefsHandler : public PrefsHandler
					   , public Trackable
{
public:

	//DO NOT CHANGE THE VALUES!!!!
	enum {
		NITZ_TimeEnable = 1,
		NITZ_TZEnable = 2
	};
	
	enum {
		NITZ_Unknown,
		NITZ_Valid,
		NITZ_Invalid
	};

	typedef std::vector<std::string> TimeSources;

	TimePrefsHandler(LSPalmService* service);
	
	virtual std::list<std::string> keys() const;
	virtual bool validate(const std::string& key, json_object* value);
	virtual void valueChanged(const std::string& key, json_object* value);
	virtual json_object* valuesForKey(const std::string& key);

	static TimePrefsHandler *instance() { return s_inst; }
	json_object * timeZoneListAsJson();
	bool isValidTimeZoneName(const std::string& tzName);
	
	void postSystemTimeChange();
    void postBroadcastEffectiveTimeChange();
	void postNitzValidityStatus();
	void launchAppsOnTimeChange();
	
	const TimeZoneInfo* currentTimeZone() const { return m_cpCurrentTimeZone; }
	time_t offsetToUtcSecs() const;	
	
	void setHourFormat(const std::string& formatStr);
	
    bool isManualTimeUsed() const { return !(m_nitzSetting & NITZ_TimeEnable); }
    bool isSystemTimeBroadcastEffective() const { return isManualTimeUsed() || !m_broadcastTime.avail(); }
	bool isNITZTimeEnabled() { return (m_nitzSetting & NITZ_TimeEnable); }
	bool isNITZTZEnabled() { return (m_nitzSetting & NITZ_TZEnable) && m_nitzTimeZoneAvailable; }
	bool isNITZDisabled() { return ((!(m_nitzSetting & NITZ_TimeEnable)) && (!(m_nitzSetting & NITZ_TZEnable))); }
	const std::string &getSystemTimeSource() const { return m_systemTimeSourceTag; }
	bool setNITZTimeEnable(bool time_en);	//returns old value
	bool setNITZTZEnable(bool tz_en);	//returns old value
	
	int  getLastNITZValidity() { return m_lastNitzValidity;}
	void markLastNITZInvalid() { m_lastNitzValidity = NITZ_Invalid;}
	void markLastNITZValid() { m_lastNitzValidity = NITZ_Valid;}
	void clearLastNITZValidity() { m_lastNitzValidity = NITZ_Unknown;}

	const TimeSources &timeSources() const { return m_timeSources; }

	std::list<std::string> getTimeZonesForOffset(int offset);

	/**
	 * Signal emmited when system-wide time changed with time delta (positive
	 * when time moves forward, i.e. new time is greater than old one)
	 */
	void clockChanged(
		const std::string &clockTag, int priority,
		time_t systemOffset, time_t lastUpdate
	);

	/**
	 * Signal emmited when system-wide time changed with time delta (positive
	 * when time moves forward, i.e. new time is greater than old one)
	 */
	Signal<time_t> systemTimeChanged;

	/**
	 * Signal emmited when user prefers manually set system-wide time.
	 * Value true passed in case of switching to manual mode
	 */
	Signal<bool> isManualTimeChanged;

	/**
	 * Signal emmited when deprecated API used to update time-source
	 */
	Signal<time_t, const std::string &> deprecatedClockChange;

	static std::string getQualifiedTZIdFromName(const std::string& tzName);
	static std::string getQualifiedTZIdFromJson(const std::string& jsonTz);
	static std::string tzNameFromJsonValue(json_object * pValue);
	static std::string tzNameFromJsonString(const std::string& TZJson);
	
	static std::string getDefaultTZFromJson(TimeZoneInfo * r_pZoneInfo=NULL);
	
	static std::string transitionNITZValidState(bool nitzValid,bool userSetTime);
	
	static bool cbPowerDActivityStatus(LSHandle* lshandle, LSMessage *message,
								void *user_data);
	
	static bool cbSetSystemTime(LSHandle* lshandle, LSMessage *message,
								void *user_data);

	static bool cbSetSystemNetworkTime(LSHandle* lshandle, LSMessage *message,
				void *user_data);

	static bool cbGetSystemTime(LSHandle* lsHandle, LSMessage *message,
								void *user_data);

	static bool cbGetSystemTimezoneFile(LSHandle* lsHandle, LSMessage *message,
								void *user_data);

	static bool cbSetTimeChangeLaunch(LSHandle* lsHandle, LSMessage *message,
								void *user_data);

	static bool cbLaunchTimeChangeApps(LSHandle* lsHandle, LSMessage *message,
								void *user_data);
	
	static bool cbGetNTPTime(LSHandle* lsHandle, LSMessage *message,
								void *user_data);
	
	static bool cbSetTimeWithNTP(LSHandle* lsHandle, LSMessage *message,
	                             void *user_data);

	static bool cbSetPeriodicWakeupPowerDResponse(LSHandle* lsHandle, LSMessage *message,
								void *user_data);
	
	static bool cbConvertDate(LSHandle* lsHandle, LSMessage *message,
								void *user_data);
	
	static bool cbServiceStateTracker(LSHandle* lsHandle, LSMessage *message,
								void *user_data);

    static bool cbSetBroadcastTime(LSHandle* lsHandle, LSMessage *message,
                                   void *userData);

    static bool cbGetBroadcastTime(LSHandle* lsHandle, LSMessage *message,
                                   void *userData);

    static bool cbGetEffectiveBroadcastTime(LSHandle* lsHandle, LSMessage *message,
                                            void *userData);
	
	 // timeout for NITZ completion
	 static gboolean source_periodic(gpointer userData);
	 static void 	source_periodic_destroy(gpointer userData);
	    
private:

	virtual ~TimePrefsHandler();
	
	void init();
	void scanTimeZoneJson();
	
	const TimeZoneInfo* timeZone_ZoneFromOffset(int offset,int dstValue=1,int mcc=0) const;
	const TimeZoneInfo* timeZone_GenericZoneFromOffset(int offset) const;
	
	const TimeZoneInfo* timeZone_ZoneFromMCC(int mcc,int mnc) const;
	const TimeZoneInfo* timeZone_ZoneFromName(const std::string& name) const;
	
	const TimeZoneInfo* timeZone_GetDefaultZoneFailsafe();
	
	bool 				isCountryAcrossMultipleTimeZones(const TimeZoneInfo& tzinfo) const;
	
	void readCurrentNITZSettings();
	void readCurrentTimeSettings();
	
	void setTimeZone(const TimeZoneInfo * pZoneInfo);       //this one sets it in the prefs db and then calls systemSetTimeZone
	void systemSetTimeZone(const std::string &tzFileActual,
	                       const TimeZoneInfo &zoneInfo);   //this one does the OS work to set the timezone
	bool systemSetTime(time_t deltaTime, const std::string &source);

    /**
     * Ask system time to be set from one of available time sources
     * I.e. do a request to NTP server, etc
     */
    void updateSystemTime();

	/**
	 * Attach system-time information to json object.
	 * Useful for building getSystemTime response
	 */
	void attachSystemTime(json_object *json);

	static bool jsonUtil_ZoneFromJson(json_object * json,TimeZoneInfo& r_zoneInfo);
	
	/// PIECEWISE NITZ HANDLING - (new , 11/2009)
#define NITZHANDLER_FLAGBIT_NTPALLOW		(1)
#define NITZHANDLER_FLAGBIT_MCCALLOW		(1 << 1)
#define NITZHANDLER_FLAGBIT_GZONEALLOW		(1 << 2)
#define NITZHANDLER_FLAGBIT_GZONEFORCE		(1 << 3)
#define NITZHANDLER_FLAGBIT_SKIP_DST_SELECT	(1 << 4)
#define NITZHANDLER_FLAGBIT_IGNORE_TIL_SET	(1 << 5)

#define NITZHANDLER_RETURN_ERROR			-1
#define NITZHANDLER_RETURN_SUCCESS			1
	int  nitzHandlerEntry(NitzParameters& nitz,int& flags,std::string& r_statusMsg);
	int  nitzHandlerTimeValue(NitzParameters& nitz,int& flags,std::string& r_statusMsg);
	int	 nitzHandlerOffsetValue(NitzParameters& nitz,int& flags,std::string& r_statusMsg);
	int  nitzHandlerDstValue(NitzParameters& nitz,int& flags,std::string& r_statusMsg);
	int  nitzHandlerExit(NitzParameters& nitz,int& flags,std::string& r_statusMsg);
	
	void  nitzHandlerSpecialCaseOffsetValue(NitzParameters& nitz,int& flags,std::string& r_statusMsg);

#define TIMEOUTFN_RESETCYCLE		1
#define TIMEOUTFN_ENDCYCLE			2
	int timeoutFunc();
	void startBootstrapCycle(int delaySeconds=20);			//this one is for machines that will never have a NITZ message kick off NTP
	void startTimeoutCycle();
	void startTimeoutCycle(unsigned int timeoutInSeconds);
	void timeout_destroy(gpointer userData);
	
	int  timeoutNitzHandlerEntry(NitzParameters& nitz,int& flags,std::string& r_statusMgs);
	int  timeoutNitzHandlerTimeValue(NitzParameters& nitz,int& flags,std::string& r_statusMsg);
	int	 timeoutNitzHandlerOffsetValue(NitzParameters& nitz,int& flags,std::string& r_statusMsg);
	int  timeoutNitzHandlerDstValue(NitzParameters& nitz,int& flags,std::string& r_statusMsg);
	int  timeoutNitzHandlerExit(NitzParameters& nitz,int& flags,std::string& r_statusMsg);
	
	void setPeriodicTimeSetWakeup();
	bool isNTPAllowed();

    /**
     * Amount of seconds that increases during whole up-time
     */
    static time_t currentStamp();

	void signalReceivedNITZUpdate(bool time,bool zone);
	void slotNetworkConnectionStateChanged(bool connected);

	static void dbg_time_timevalidOverride(bool&);
	static void dbg_time_tzvalidOverride(bool&);
	static void dbg_time_dstvalidOverride(bool&);

    static bool cbTelephonyPlatformQuery(LSHandle* lsHandle, LSMessage *message,
                                         void *userData);

private:

	static TimePrefsHandler * s_inst;			///not a true instance handle. Just points to the first one created
	
	typedef std::list<TimeZoneInfo*> TimeZoneInfoList;
	typedef std::list<TimeZoneInfo*>::iterator TimeZoneInfoListIterator;
	typedef std::list<TimeZoneInfo*>::const_iterator TimeZoneInfoListConstIterator;
	
	typedef std::map<int,TimeZoneInfo*> TimeZoneMap;
	typedef std::map<int,TimeZoneInfo*>::iterator TimeZoneMapIterator;
	typedef std::map<int,TimeZoneInfo*>::const_iterator TimeZoneMapConstIterator;

	typedef std::pair<int,TimeZoneInfo*> TimeZonePair;
	typedef std::multimap<int,TimeZoneInfo*> TimeZoneMultiMap;
	typedef std::multimap<int,TimeZoneInfo*>::iterator TimeZoneMultiMapIterator;
	typedef std::multimap<int,TimeZoneInfo*>::const_iterator TimeZoneMultiMapConstIterator;
	
	std::list<std::string> m_keyList;
	
	TimeZoneInfoList m_zoneList;
	TimeZoneInfoList m_syszoneList;
	
	TimeZoneMap m_mccZoneInfoMap;
	TimeZoneMap m_preferredTimeZoneMapDST;
	TimeZoneMap m_preferredTimeZoneMapNoDST;
	TimeZoneMultiMap m_offsetZoneMultiMap;
	
	static const TimeZoneInfo s_failsafeDefaultZone;
	const TimeZoneInfo * 	m_cpCurrentTimeZone;
	TimeZoneInfo *	m_pDefaultTimeZone;
    int m_nitzSetting;					//see the enum...this is a bitfield
    int m_lastNitzValidity;
    bool m_immNitzTimeValid;
    bool m_immNitzZoneValid;
    
    NitzParameters	*	m_p_lastNitzParameter;
    int					m_lastNitzFlags;
    
    static json_object * s_timeZonesJson;
    
    GSource *	m_gsource_periodic;
    guint		m_gsource_periodic_id;
    int			m_timeoutCycleCount;
    
    bool		m_sendWakeupSetToPowerD;

	time_t m_lastNtpUpdate;

    bool        m_nitzTimeZoneAvailable;

    BroadcastTime m_broadcastTime;

	TimeSources m_timeSources;
	int         m_currentTimeSourcePriority;
	time_t      m_nextSyncTime;
	std::string m_systemTimeSourceTag;

	NTPClock    m_ntpClock;
};

#endif /* TIMEPREFSHANDLER_H */

