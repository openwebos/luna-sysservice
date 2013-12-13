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
 *  @file NTPClock.h
 */

#ifndef __NTPCLOCK_H
#define __NTPCLOCK_H

#include <string>
#include <vector>

#include "LSUtils.h"

#include "SignalSlot.h"

class TimePrefsHandler;

/**
 * Groupped information required for handling NTP clocks in TimePrefsHandler
 */
struct NTPClock
{
	TimePrefsHandler &timePrefsHandler;

	NTPClock(TimePrefsHandler &th) :
		timePrefsHandler(th),
		sntpPid(-1)
	{}

	~NTPClock()
	{
		if (sntpPid != -1) g_spawn_close_pid(sntpPid);
	}

	/**
	 * PID of "sntp" process we are waiting for or -1 if we are in idle
	 */
	GPid sntpPid;

	/**
	 * stdout contents of "sntp" process
	 */
	std::string sntpOutput;

	/**
	 * Request for NTP time update.
	 * @param message originator of this request if present
	 * @return false if message present and failed to handle it
	 */
	bool requestNTP(LSMessage *message = NULL);

	/**
	 * Send NTP time offset from system time to all requests and to "ntp" clock
	 */
	void postNTP(time_t offset);

	/**
	 * Send Error in response to all NTP requests
	 */
	void postError();

	/**
	 * Pending responses for /time/getNTPTime
	 */
	typedef std::vector<LS::MessageRef> RequestMessages;
	RequestMessages requestMessages;

	/**
	 * Callback for "sntp" child process change state
	 */
	static void cbChild(GPid pid, gint status, NTPClock *ntpClock);

	/**
	 * Callback for "sntp" stdout data flow
	 */
	static gboolean cbStdout(GIOChannel *channel, GIOCondition cond, NTPClock *ntpClock);
};

#endif
