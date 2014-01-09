/****************************************************************
 * @@@LICENSE
 *
 *  Copyright (c) 2013 LG Electronics, Inc.
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
 *
 * LICENSE@@@
 ****************************************************************/

/**
 *  @file ClockHandler.h
 */

#ifndef __CLOCKHANDLER_H
#define __CLOCKHANDLER_H

#include <map>
#include <string>
#include "SignalSlot.h"

struct LSPalmService;
struct LSHandle;
struct LSMessage;

class ClockHandler : public Trackable
{
public:
	ClockHandler();

	/**
	 * Register/attach this handler to service
	 *
	 * @param LSPalmService
	 * @return false if some failure occured
	 */
	bool setServiceHandle(LSPalmService* service);

	/**
	 * Notify about system-time moving forward/backward.
	 *
	 * Results in adjusting all clocks
	 *
	 * @param offset from old value (i.e. positive means times moves forward)
	 */
	void adjust(time_t offset);

	/**
	 * Notify about about change of manual-time mode
	 * true - for manual time settings
	 *
	 * Note that as a side effect of switching to "false" will result in
	 * sending clockChanged for all available clocks.
	 */
	void manualOverride(bool enabled);

	/**
	 * Register clock with specific priority and optionally initial offset
	 *
	 * Note that time mark for last update always will be invalidTime (for
	 *      first setup). Even if offset is valid time.
	 * Note that its possible to perform second setup which will result in
	 *      changing priority and optionally offset for that clock.
	 */
	void setup(const std::string &clockTag, int priority, time_t offset = invalidOffset);

	/**
	 * Update specific clock with new offset (from system time)
	 */
	bool update(time_t offset, const std::string &clockTag = manual);

	/**
	 * Signal emmited when some clock was changed (i.e. offset from system time
	 * changed)
	 * First - time source tag
	 * Second - priority
	 * Third - offset from system time
	 * Fourth - last system time it was updated
	 */
	Signal<const std::string &, int, time_t, time_t> clockChanged;

	/**
	 * Pre-defined clock tag for manual adjusted time
	 */
	static const std::string manual;

	/**
	 * Pre-defined clock tag for micom time
	 */
	static const std::string micom;

	/**
	 * Pre-defined clock tag for system-wide time
	 */
	static const std::string system;

	/**
	 * Pre-defined time_t constant for invalid time (corresponds with mktime
	 * invalid time result)
	 */
	static const time_t invalidTime;

	/**
	 * Pre-defined time_t constant for invalid time offset
	 */
	static const time_t invalidOffset;

	static bool cbSetTime(LSHandle* lshandle, LSMessage *message,
	                      void *user_data);

	static bool cbGetTime(LSHandle* lshandle, LSMessage *message,
	                      void *user_data);

private:
	struct Clock {
		/**
		 * Priority. Higher overrides lower.
		 */
		int priority;

		/**
		 * Offset from system time (in UTC)
		 */
		time_t systemOffset;

		/**
		 * Time since some moment of time (valid at least for single boot
		 * session)
		 */
		time_t lastUpdate;
	};

	typedef std::map<std::string, ClockHandler::Clock> ClocksMap;
	ClocksMap m_clocks;
	bool m_manualOverride;
};

#endif
