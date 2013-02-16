/**
 *  Copyright 2010 - 2013 Hewlett-Packard Development Company, L.P.
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


#ifndef TIMEZONESERVICE_H
#define TIMEZONESERVICE_H

#include <luna-service2/lunaservice.h>
#include <stdint.h>
#include <cjson/json.h>

#include <list>


class TimeZoneService
{
public:

	static TimeZoneService* instance();
	void setServiceHandle(LSPalmService* service);
	LSPalmService* serviceHandle() const;

	static bool cbGetTimeZoneRules(LSHandle* lshandle, LSMessage *message,
								   void *user_data);
	static bool cbGetTimeZoneFromEasData(LSHandle* lshandle, LSMessage *message,
										 void *user_data);

private:

	typedef std::list<int> IntList;

	struct TimeZoneEntry {
		std::string tz;
		IntList years;
	};

	struct TimeZoneResult {
		std::string tz;
		int year;
		bool hasDstChange;
		int64_t utcOffset;
		int64_t dstOffset;
		int64_t dstStart;
		int64_t dstEnd;
	};

	struct EasSystemTime {
		bool valid;
		int year;
		int month;
		int dayOfWeek;
		int day;
		int hour;
		int minute;
		int second;
	};	

	typedef std::list<TimeZoneEntry> TimeZoneEntryList;
	typedef std::list<TimeZoneResult> TimeZoneResultList;

private:

	TimeZoneService();
	~TimeZoneService();

	std::string getTimeZoneRules(const TimeZoneEntryList& entries);
	TimeZoneResultList getTimeZoneRuleOne(const TimeZoneEntry& entry);
	static void readEasDate(json_object* obj, EasSystemTime& time);
	static void updateEasDateDayOfMonth(EasSystemTime& time, int year);

private:

	LSPalmService* m_service;
	LSHandle* m_serviceHandlePublic;
	LSHandle* m_serviceHandlePrivate;
};	


#endif /* TIMEZONESERVICE_H */
