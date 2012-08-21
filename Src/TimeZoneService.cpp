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


#include <glib.h>
#include <cjson/json.h>
#include <time.h>
#include <stdio.h>
#include <string.h>

#include <string>
#include <list>

#include "TimeZoneService.h"

#include "PrefsFactory.h"
#include "TimePrefsHandler.h"
#include "TzParser.h"
#include "JSONUtils.h"

static LSMethod s_methods[]  = {
	{ "getTimeZoneRules",  TimeZoneService::cbGetTimeZoneRules },
	{ "getTimeZoneFromEasData", TimeZoneService::cbGetTimeZoneFromEasData },
    { 0, 0 },
};

TimeZoneService* TimeZoneService::instance()
{
	static TimeZoneService* s_instance = 0;
	if (G_UNLIKELY(s_instance == 0))
		s_instance = new TimeZoneService;

	return s_instance;
}

TimeZoneService::TimeZoneService()
	: m_service(0)
{    
}

TimeZoneService::~TimeZoneService()
{
    // NO-OP
}

void TimeZoneService::setServiceHandle(LSPalmService* service)
{
	m_service = service;
	
    LSError lsError;
    LSErrorInit(&lsError);

    bool result = LSPalmServiceRegisterCategory(m_service, "/timezone",
												s_methods, NULL,
												NULL, this, &lsError);
    if (!result) {
		g_critical("Failed in registering timezone handler method: %s", lsError.message);
    	LSErrorFree(&lsError);
    	return;
    }

    m_serviceHandlePublic = LSPalmServiceGetPublicConnection(m_service);
    m_serviceHandlePrivate = LSPalmServiceGetPrivateConnection(m_service);	
}

LSPalmService* TimeZoneService::serviceHandle() const
{
	return m_service;    
}

bool TimeZoneService::cbGetTimeZoneRules(LSHandle* lsHandle, LSMessage *message,
										 void *user_data)
{
	std::string reply;
	bool ret;
	LSError lsError;
	json_object* root = 0;
	TimeZoneEntryList entries;

	LSErrorInit(&lsError);
	
	const char* payload = LSMessageGetPayload(message);
	if (!payload) {
		reply = "{\"returnValue\": false, "
				" \"errorText\": \"No payload specifed for message\"}";
		goto Done;
	}

	root = json_tokener_parse(payload);
	if (!root || is_error(root)) {
		reply = "{\"returnValue\": false, "
				" \"errorText\": \"Cannot parse json payload\"}";
		goto Done;
	}

	if (!json_object_is_type(root, json_type_array)) {
		reply = "{\"returnValue\": false, "
				" \"errorText\": \"json root needs to be an array\"}";
		goto Done;
	}

	for (int i = 0; i < json_object_array_length(root); i++) {
		json_object* obj = json_object_array_get_idx(root, i);
		json_object* l = 0;

		TimeZoneEntry tzEntry;
		
		// Mandatory (tz)
		l = json_object_object_get(obj, "tz");
		if (!l) {
			reply = "{\"returnValue\": false,"
					" \"errorText\": \"Missing tz entry\"}";
			goto Done;
		}

		if (!json_object_is_type(l, json_type_string)) {
			reply = "{\"returnValue\": false,"
					" \"errorText\": \"tz entry is not string\"}";
			goto Done;
		}

		tzEntry.tz = json_object_get_string(l);

		// Optional (years)
		l = json_object_object_get(obj, "years");
		if (l) {

			if (!json_object_is_type(l, json_type_array)) {
				reply = "{\"returnValue\": false,"
						" \"errorText\": \"years entry is not array\"}";
				goto Done;
			}

			for (int j = 0; j < json_object_array_length(l); j++) {
				json_object* o = json_object_array_get_idx(l, j);

				if (!json_object_is_type(o, json_type_int)) {
					reply = "{\"returnValue\": false,"
							" \"errorText\": \"entry in years array is not integer\"}";
					goto Done;
				}

				tzEntry.years.push_back(json_object_get_int(o));
			}
		}

		if (tzEntry.years.empty()) {
			time_t utcTime = time(NULL);
			struct tm* localTime = localtime(&utcTime);
			tzEntry.years.push_back(localTime->tm_year + 1900);
		}

		entries.push_back(tzEntry);
	}	
	
	reply = TimeZoneService::instance()->getTimeZoneRules(entries);
	
Done:

	ret = LSMessageReply(lsHandle, message, reply.c_str(), &lsError);
	if (!ret)
		LSErrorFree(&lsError);

	if (root && !is_error(root))
		json_object_put(root);

	return true;
}

std::string TimeZoneService::getTimeZoneRules(const TimeZoneService::TimeZoneEntryList& entries)
{
	TimeZoneResultList totalResult;
	for (TimeZoneEntryList::const_iterator it = entries.begin();
		 it != entries.end(); ++it) {
		TimeZoneResultList r = getTimeZoneRuleOne(*it);
		totalResult.splice(totalResult.end(), r);
	}

	if (totalResult.empty()) {
		return std::string("{\"returnValue\": false, \"errorText\":\"Failed to retrieve results for specified timezones\"}");
	}

	json_object* obj = json_object_new_object();
	json_object_object_add(obj, "returnValue", json_object_new_boolean(true));

	json_object* array = json_object_new_array();
	for (TimeZoneResultList::const_iterator it = totalResult.begin();
		 it != totalResult.end(); ++it) {
		const TimeZoneResult& r = (*it);
		json_object* o = json_object_new_object();
		json_object_object_add(o, "tz", json_object_new_string(r.tz.c_str()));
		json_object_object_add(o, "year", json_object_new_int(r.year));
		json_object_object_add(o, "hasDstChange", json_object_new_boolean(r.hasDstChange));
		json_object_object_add(o, "utcOffset", json_object_new_int(r.utcOffset));
		json_object_object_add(o, "dstOffset", json_object_new_int(r.dstOffset));
		json_object_object_add(o, "dstStart", json_object_new_int(r.dstStart));
		json_object_object_add(o, "dstEnd", json_object_new_int(r.dstEnd));
		json_object_array_add(array, o);
		// printf("Name: %s, Year: %d, hasDstChange: %d, utcOffset: %lld, "
		// 	   "dstOffset: %lld, dstStart: %lld, dstEnd: %lld\n",
		// 	   r.tz.c_str(), r.year, r.hasDstChange,
		// 	   r.utcOffset, r.dstOffset, r.dstStart, r.dstEnd);
	}
	json_object_object_add(obj, "results", array);

	std::string res = json_object_to_json_string(obj);
	json_object_put(obj);
	
    return res;
}

TimeZoneService::TimeZoneResultList TimeZoneService::getTimeZoneRuleOne(const TimeZoneEntry& entry)
{
	TimeZoneResultList results;

	TzTransitionList transitionList = parseTimeZone(entry.tz.c_str());

	for (IntList::const_iterator it = entry.years.begin();
		 it != entry.years.end(); ++it) {

		int year = (*it);

		TimeZoneResult res;
		res.tz = entry.tz;
		res.year = year;
		res.hasDstChange = false;
		res.utcOffset = -1;
		res.dstOffset = -1;
		res.dstStart  = -1;
		res.dstEnd    = -1;

		// First do a scan to check if there are entries for this year
		bool hasEntriesForYear = false;
		for (TzTransitionList::const_iterator iter = transitionList.begin();
			 iter != transitionList.end(); ++iter) {

			const TzTransition& trans = (*iter);
			if (trans.year == year) {
				hasEntriesForYear = true;
				break;
			}
		}

		if (hasEntriesForYear) {
		
			for (TzTransitionList::const_iterator iter = transitionList.begin();
				 iter != transitionList.end(); ++iter) {

				const TzTransition& trans = (*iter);
				if (trans.year != year)
					continue;

				if (trans.isDst) {
					res.hasDstChange = true;
					res.dstOffset    = trans.utcOffset;
					res.dstStart     = trans.time;
				}
				else {
					res.utcOffset    = trans.utcOffset;
					res.dstEnd       = trans.time;
				}
			}
		}
		else {

			// Pick the latest year which is < the specified year
			for (TzTransitionList::const_reverse_iterator iter = transitionList.rbegin();
				 iter != transitionList.rend(); ++iter) {

				const TzTransition& trans = (*iter);
				if (trans.year > year)
					continue;

				res.hasDstChange = false;
				res.dstOffset    = -1;
				res.dstStart     = -1;
				res.dstEnd       = -1;
				res.utcOffset    = trans.utcOffset;

				break;
			}
		}

		if (res.utcOffset == -1)
			continue;
					
		if (res.dstStart == -1)
			res.dstEnd = -1;

		results.push_back(res);
	}	
	
    return results;
}

// http://msdn.microsoft.com/en-us/library/ms725481.aspx
bool TimeZoneService::cbGetTimeZoneFromEasData(LSHandle* lsHandle, LSMessage *message,
											   void *user_data)
{
	std::string reply;
	bool ret;
	LSError lsError;
	json_object* root = 0;
	json_object* label = 0;

	int easBias;
	EasSystemTime easStandardDate;
	int easStandardBias;
	EasSystemTime easDaylightDate;
	int easDaylightBias;

	LSErrorInit(&lsError);

	easBias = 0;
	easStandardDate.valid = false;
	easStandardBias = 0;
	easDaylightDate.valid = false;
	easDaylightBias = 0;

    // {"bias": integer, standardDate:{"year": integer, "month": integer, "dayOfWeek": integer, "day": integer, "hour": integer, "minute": integer, "second": integer}, "standardBias": integer, "daylightDate":{"year": integer, "month": integer, "dayOfWeek": integer, "day": integer, "hour": integer, "minute": integer, "second": integer}, "daylightBias": integer}
    VALIDATE_SCHEMA_AND_RETURN(lsHandle,
                               message,
                               SCHEMA_5(REQUIRED(bias, integer), NAKED_OBJECT_OPTIONAL_7(standardDate, year, integer, month, integer, dayOfWeek, integer, day, integer, hour, integer, minute, integer, second, integer), OPTIONAL(standardBias, integer), NAKED_OBJECT_OPTIONAL_7(daylightDate, year, integer, month, integer, dayOfWeek, integer, day, integer, hour, integer, minute, integer, second, integer),OPTIONAL(daylightBias, integer)));
	
	const char* payload = LSMessageGetPayload(message);
	if (!payload) {
		reply = "{\"returnValue\": false, "
				" \"errorText\": \"No payload specifed for message\"}";
		goto Done;
	}

	root = json_tokener_parse(payload);
	if (!root || is_error(root)) {
		reply = "{\"returnValue\": false, "
				" \"errorText\": \"Cannot parse json payload\"}";
		goto Done;
	}

	// bias. mandatory (everything else is optional)
	label = json_object_object_get(root, "bias");
	if (!label || !json_object_is_type(label, json_type_int)) {
		reply = "{\"returnValue\": false, "
				" \"errorText\": \"bias value missing\"}";
		goto Done;
	}
	easBias = json_object_get_int(label);

	// standard date
	label = json_object_object_get(root, "standardDate");
	if (label && json_object_is_type(label, json_type_object))
		readEasDate(label, easStandardDate);

	// standard bias
	label = json_object_object_get(root, "standardBias");
	if (label && json_object_is_type(label, json_type_int))
		easStandardBias = json_object_get_int(label);

	// daylight date
	label = json_object_object_get(root, "daylightDate");
	if (label && json_object_is_type(label, json_type_object))
		readEasDate(label, easDaylightDate);

	// daylight bias
	label = json_object_object_get(root, "daylightBias");
	if (label && json_object_is_type(label, json_type_int))
		easDaylightBias = json_object_get_int(label);

	// Both standard and daylight bias need to specified together,
	// otherwise both are invalid
	if (!easDaylightDate.valid)
		easStandardDate.valid = false;

	if (!easStandardDate.valid)
		easDaylightDate.valid = false;

	{
		// Get all timezones matching the current offset
		PrefsHandler* handler = PrefsFactory::instance()->getPrefsHandler("timeZone");
		TimePrefsHandler* tzHandler = static_cast<TimePrefsHandler*>(handler);
		TimeZoneService* tzService = TimeZoneService::instance();

		std::list<std::string> timeZones = tzHandler->getTimeZonesForOffset(-easBias);

		if (timeZones.empty()) {

			reply = "{\"returnValue\": false, "
					" \"errorText\": \"Failed to find any timezones with specified bias value\"}";
			goto Done;
		}

		if (!easStandardDate.valid) {
			// No additional data available for refinement. Just use the
			// first timezone entry in the list
			json_object* obj = json_object_new_object();
			json_object_object_add(obj, "returnValue", json_object_new_boolean(true));
			json_object_object_add(obj, "timeZone", json_object_new_string(timeZones.begin()->c_str()));
			reply = json_object_to_json_string(obj);
			json_object_put(obj);

			goto Done;
		}
		else {

			time_t utcTime = time(NULL);
			struct tm* localTime = localtime(&utcTime);
			int currentYear = localTime->tm_year + 1900;

			updateEasDateDayOfMonth(easStandardDate, currentYear);
			updateEasDateDayOfMonth(easDaylightDate, currentYear);
				
			
			for (std::list<std::string>::const_iterator it = timeZones.begin();
				 it != timeZones.end(); ++it) {
				TimeZoneEntry tzEntry;
				tzEntry.tz = (*it);
				tzEntry.years.push_back(currentYear);

				TimeZoneResultList tzResultList = tzService->getTimeZoneRuleOne(tzEntry);
				if (tzResultList.empty())
					continue;

				const TimeZoneResult& tzResult = tzResultList.front();
	
				printf("For timezone: %s\n", tzEntry.tz.c_str());
				printf("year: %d, utcOffset: %lld, dstOffset: %lld, dstStart: %lld, dstEnd: %lld\n",
					   tzResult.year, tzResult.utcOffset, tzResult.dstOffset,
					   tzResult.dstStart, tzResult.dstEnd);

				// Set this timezone as the current timezone, so that we can calculate when the
				// DST transitions occurs in seconds for the specified eas data
				setenv("TZ", tzEntry.tz.c_str(), 1);

				struct tm tzBrokenTime;
				tzBrokenTime.tm_sec = easStandardDate.second;
				tzBrokenTime.tm_min = easStandardDate.minute;
				tzBrokenTime.tm_hour = easStandardDate.hour;
				tzBrokenTime.tm_mday = easStandardDate.day;
				tzBrokenTime.tm_mon = easStandardDate.month - 1;
				tzBrokenTime.tm_year = currentYear - 1900;
				tzBrokenTime.tm_wday = 0;
				tzBrokenTime.tm_yday = 0;
				tzBrokenTime.tm_isdst = 1;

				time_t easStandardDateSeconds = ::mktime(&tzBrokenTime);

				tzBrokenTime.tm_sec = easDaylightDate.second;
				tzBrokenTime.tm_min = easDaylightDate.minute;
				tzBrokenTime.tm_hour = easDaylightDate.hour;
				tzBrokenTime.tm_mday = easDaylightDate.day;
				tzBrokenTime.tm_mon = easDaylightDate.month - 1;
				tzBrokenTime.tm_year = currentYear - 1900;
				tzBrokenTime.tm_wday = 0;
				tzBrokenTime.tm_yday = 0;
				tzBrokenTime.tm_isdst = 0;

				time_t easDaylightDateSeconds = ::mktime(&tzBrokenTime);

				printf("eas dstStart: %ld, dstEnd: %ld\n", easDaylightDateSeconds, easStandardDateSeconds);
				
				if (easStandardDateSeconds == tzResult.dstEnd &&
					easDaylightDateSeconds == tzResult.dstStart) {
					// We have a winner
					json_object* obj = json_object_new_object();
					json_object_object_add(obj, "returnValue", json_object_new_boolean(true));
					json_object_object_add(obj, "timeZone", json_object_new_string(tzEntry.tz.c_str()));
					reply = json_object_to_json_string(obj);
					json_object_put(obj);
					goto Done;
				}
			}

			reply = "{\"returnValue\": false, "
					" \"errorText\": \"Failed to find any timezones with specified parametes\"}";
		}
	}

Done:

	ret = LSMessageReply(lsHandle, message, reply.c_str(), &lsError);
	if (!ret)
		LSErrorFree(&lsError);

	if (root && !is_error(root))
		json_object_put(root);

	return true;	
}

void TimeZoneService::readEasDate(json_object* obj, TimeZoneService::EasSystemTime& time)
{
	json_object* l = 0;
	time.valid = false;

	l = json_object_object_get(obj, "year");
	if (!l || !json_object_is_type(l, json_type_int))
		return;
	time.year = json_object_get_int(l);

	l = json_object_object_get(obj, "month");
	if (!l || !json_object_is_type(l, json_type_int))
		return;
	time.month = json_object_get_int(l);
	
	l = json_object_object_get(obj, "dayOfWeek");
	if (!l || !json_object_is_type(l, json_type_int))
		return;
	time.dayOfWeek = json_object_get_int(l);

	l = json_object_object_get(obj, "day");
	if (!l || !json_object_is_type(l, json_type_int))
		return;
	time.day = json_object_get_int(l);
	
	l = json_object_object_get(obj, "hour");
	if (!l || !json_object_is_type(l, json_type_int))
		return;
	time.hour = json_object_get_int(l);

	l = json_object_object_get(obj, "minute");
	if (!l || !json_object_is_type(l, json_type_int))
		return;
	time.minute = json_object_get_int(l);

	l = json_object_object_get(obj, "second");
	if (!l || !json_object_is_type(l, json_type_int))
		return;
	time.second = json_object_get_int(l);

	// Sanitize the input:
	if (time.month < 1 || time.month > 12)
		return;

	if (time.dayOfWeek < 0 || time.dayOfWeek > 6)
		return;

	if (time.day < 1 || time.day > 5)
		return;

	if (time.hour < 0 || time.hour > 59)
		return;

	if (time.minute < 0 || time.minute > 59)
		return;

	if (time.second < 0 || time.second > 59)
		return;			
	
	time.valid = true;
}

// This function figures out the correct day of month based o
void TimeZoneService::updateEasDateDayOfMonth(TimeZoneService::EasSystemTime& time, int year)
{	
	// Beginning of this month at 1:00AM
	struct tm tzBrokenTimeFirst;
	tzBrokenTimeFirst.tm_sec = 0;
	tzBrokenTimeFirst.tm_min = 0;
	tzBrokenTimeFirst.tm_hour = 1;
	tzBrokenTimeFirst.tm_mday = 1;
	tzBrokenTimeFirst.tm_mon = time.month - 1;
	tzBrokenTimeFirst.tm_year = year - 1900;
	tzBrokenTimeFirst.tm_wday = 0;
	tzBrokenTimeFirst.tm_yday = 0;
	tzBrokenTimeFirst.tm_isdst = 0;

	// Beginning of next month at 1:00AM
	struct tm tzBrokenTimeLast;
	tzBrokenTimeLast.tm_sec = 0;
	tzBrokenTimeLast.tm_min = 0;
	tzBrokenTimeLast.tm_hour = 1;
	tzBrokenTimeLast.tm_mday = 1;
	tzBrokenTimeLast.tm_mon = time.month;
	tzBrokenTimeLast.tm_year = year - 1900;
	tzBrokenTimeLast.tm_wday = 0;
	tzBrokenTimeLast.tm_yday = 0;
	tzBrokenTimeLast.tm_isdst = 0;

	// Overflowed into next year
	if (tzBrokenTimeLast.tm_mon == 12) {
		tzBrokenTimeLast.tm_mon = 0;
		tzBrokenTimeLast.tm_year += 1;
	}

	time_t timeFirstOfMonth = ::timegm(&tzBrokenTimeFirst);
    time_t timeLastOfMonth = ::timegm(&tzBrokenTimeLast);
	// Subtract out 2 hours from 1:00 on first of next month.
	// This will give us 11:00 PM on last of this month
	timeLastOfMonth -= 2 * 60 * 60;

	// Now breakdown the time again
	gmtime_r(&timeFirstOfMonth, &tzBrokenTimeFirst);
	gmtime_r(&timeLastOfMonth, &tzBrokenTimeLast);

	// printf("Beg: year: %d, month: %d, day: %d, hour: %d, wday: %d\n",
	// 	   tzBrokenTimeFirst.tm_year + 1900, tzBrokenTimeFirst.tm_mon,
	// 	   tzBrokenTimeFirst.tm_mday, tzBrokenTimeFirst.tm_hour,
	// 	   tzBrokenTimeFirst.tm_wday);

	// printf("End: year: %d, month: %d, day: %d, hour: %d, wday: %d\n",
	// 	   tzBrokenTimeLast.tm_year + 1900, tzBrokenTimeLast.tm_mon,
	// 	   tzBrokenTimeLast.tm_mday, tzBrokenTimeLast.tm_hour,
	// 	   tzBrokenTimeLast.tm_wday);

	char days[35]; // 7 day x max 5 weeks
	::memset(days, -1, sizeof(days));
	for (int i = tzBrokenTimeFirst.tm_mday; i <= tzBrokenTimeLast.tm_mday; i++) {
		days[i + tzBrokenTimeFirst.tm_wday - 1] = i;
	}

	int week = CLAMP(time.day, 1, 5) - 1;
	int dayOfWeek = CLAMP(time.dayOfWeek, 0, 6);

	int weekStart = 0;
	for (weekStart = 0; weekStart < 5; weekStart++) {
		if (days[weekStart * 7 + dayOfWeek] != -1)
			break;
	}

	week = weekStart + week;
	week = CLAMP(week, 0, 4);

	time.day = days[week * 7 + dayOfWeek];
}
