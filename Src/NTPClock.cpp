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
 *  @file NTPClock.cpp
 */

#include "Logging.h"

#include "PrefsDb.h"
#include "TimePrefsHandler.h"
#include "NTPClock.h"


void NTPClock::postNTP(time_t offset)
{
	PmLogDebug(sysServiceLogContext(), "post NTP offset %ld", offset);

	// send replies if any request waits for some
	if (!requestMessages.empty())
	{
		struct json_object * jsonOutput = json_object_new_object();
		json_object_object_add(jsonOutput, (char*) "subscribed",json_object_new_boolean(false));	//no subscriptions on this; make that explicit!
		json_object_object_add(jsonOutput,(char *)"returnValue",json_object_new_boolean(true));
		json_object_object_add(jsonOutput,(char *)"utc",json_object_new_int(time(0) + offset));
		const char * reply = json_object_to_json_string(jsonOutput);

		PmLogDebug(sysServiceLogContext(), "NTP reply: %s", reply);

		// for each request
		for (RequestMessages::iterator it = requestMessages.begin();
			 it != requestMessages.end(); ++it)
		{
			PmLogDebug(sysServiceLogContext(), "post response on %p", it->get());
			LS::Error lsError;

			if (!LSMessageRespond(it->get(), reply, &lsError))
			{
				PmLogError(sysServiceLogContext(), "NTP_RESPOND_FAIL", 1,
					PMLOGKS("REASON", lsError.message),
					"Failed to send response for NTP query call"
				);
			}
		}
		json_object_put(jsonOutput);
		requestMessages.clear();
	}

	// post as a new value for "ntp"
	timePrefsHandler.deprecatedClockChange.fire(offset, "ntp");
}

void NTPClock::postError()
{
	PmLogDebug(sysServiceLogContext(), "post NTP error");

	// nothing to do if no requests
	if (requestMessages.empty()) return;

	const char *reply = "{\"subscribed\":false,\"returnValue\":false,\"errorText\":\"Failed to get NTP time response\"}";

	// for each request
	for (RequestMessages::iterator it = requestMessages.begin();
		 it != requestMessages.end(); ++it)
	{
		PmLogDebug(sysServiceLogContext(), "post error response on %p", it->get());
		LS::Error lsError;

		if (!LSMessageRespond(it->get(), reply, &lsError))
		{
			PmLogError(sysServiceLogContext(), "NTP_ERROR_RESPOND_FAIL", 1,
				PMLOGKS("REASON", lsError.message),
				"Failed to send response for NTP query call"
			);
		}
	}
}

bool NTPClock::requestNTP(LSMessage *message /* = NULL */)
{
	if (message)
	{
		// postpone for further NTP time post
		requestMessages.push_back(message);
	}

	if (sntpPid != -1)
	{
		// already requested update
		return true;
	}

	//try and retrieve the currently set NTP server to query
	std::string ntpServer = PrefsDb::instance()->getPref("NTPServer");
	if (ntpServer.empty()) {
		ntpServer = DEFAULT_NTP_SERVER;
	}

	std::string ntpServerTimeout;
	if (!PrefsDb::instance()->getPref("NTPServerTimeout", ntpServerTimeout))
	{
		ntpServerTimeout = "2"; // seconds
	}

	gchar *argv[] = {
		(gchar *)"sntp",
		(gchar *)"-t",
		(gchar *)ntpServerTimeout.c_str(),
		(gchar *)"-d",
		(gchar *)ntpServer.c_str(),
		0
	};

	PmLogDebug(sysServiceLogContext(),
		"%s: running sntp on %s (timeout %s)",
		__FUNCTION__,
		ntpServer.c_str(),
		ntpServerTimeout.c_str()
	);

	gchar **envp = g_get_environ();

	// override all locale related variables (LC_*)
	envp = g_environ_setenv(envp, "LC_ALL", "C", true);

	int fdOut;
	GError *error;
	gboolean ret = g_spawn_async_with_pipes(
		/* workdir */ 0, argv, envp,
		GSpawnFlags (G_SPAWN_SEARCH_PATH | G_SPAWN_DO_NOT_REAP_CHILD),
		/* child_setup */ 0,
		this, &sntpPid,
		/* stdin */ 0, /* stdout */ &fdOut, /* stderr */ 0,
		&error
	);

	g_strfreev(envp);

	if (!ret)
	{
		PmLogError(sysServiceLogContext(), "SNTP_SPAWN_FAIL", 0,
			"Failed to spawn sntp"
		);
		postError();
		return false;
	}

	g_child_watch_add( sntpPid, (GChildWatchFunc)cbChild, this);

	GIOChannel *chOut = g_io_channel_unix_new(fdOut);
	g_io_add_watch( chOut, GIOCondition (G_IO_IN | G_IO_HUP), (GIOFunc)cbStdout, this);

	return true;
}

// callbacks
void NTPClock::cbChild(GPid pid, gint status, NTPClock *ntpClock)
{
	// move to new state
	ntpClock->sntpPid = -1;
	g_spawn_close_pid(pid);


	std::string &sntpOutput = ntpClock->sntpOutput;

	if (status != 0)
	{
		PmLogDumpDataDebug(sysServiceLogContext(),
			sntpOutput.data(), sntpOutput.size(),
			kPmLogDumpFormatDefault
		);
		ntpClock->postError();
		return;
	}

	//success, maybe...parse the output

	//sntp -d us.pool.ntp.org returns below offset.
	//
	//15 Aug 21:41:33 sntp[5529]: Started sntp
	//Starting to read KoD file /var/db/ntp-kod...
	//sntp sendpkt: Sending packet to 173.49.198.27... Packet sent.
	//sntp recvpkt: packet received from 173.49.198.27 is not authentic. Authentication not enforced.
	//sntp handle_pkt: Received 48 bytes from 173.49.198.27
	//sntp offset_calculation:	t21: 0.099049		 t34: -0.110320
	//        delta: 0.209369	 offset: -0.005636
	//get time offset from sntp's return output : "offset: -0.005636"

	const char *offsetStr = "offset: ";
	size_t offsetIndex = sntpOutput.find(offsetStr);
	if (offsetIndex == std::string::npos) {
		//the query failed in some way
		ntpClock->postError();
		return;
	}

	const char *startptr = sntpOutput.c_str() + offsetIndex + strlen(offsetStr);
	char *endptr = 0;

	PmLogDebug(sysServiceLogContext(), "offset: \"%.*s\"...", 20, startptr);

	time_t offsetValue = strtol(startptr, &endptr, /* base = */ 10);
	if ( endptr == startptr ||
	     (*endptr != '\0' && strchr(" \t#.", *endptr) == NULL) )
	{
		// either empty string interpreted as a number
		// or string ends with unexpedted char
		// consider that as error
		ntpClock->postError();
	}
	else
	{
		ntpClock->postNTP(offsetValue);
	}

	sntpOutput.clear();
}

gboolean NTPClock::cbStdout(GIOChannel *channel, GIOCondition cond, NTPClock *ntpClock)
{
	if (cond == G_IO_HUP)
	{
		g_io_channel_unref(channel);
		return false;
	}

	while (true)
	{
		char buf[4096];
		gsize bytesRead;
		GIOStatus status = g_io_channel_read_chars(channel, buf, sizeof(buf), &bytesRead, 0);
		if (status == G_IO_STATUS_AGAIN) continue;
		else if (status == G_IO_STATUS_EOF) break;
		else if (status == G_IO_STATUS_ERROR)
		{
			PmLogDebug(sysServiceLogContext(), "Error during read");
			return false;
		}

		ntpClock->sntpOutput.append(buf, bytesRead);
	}
	return true;
}
