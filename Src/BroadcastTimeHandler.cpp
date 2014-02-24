/****************************************************************
 * @@@LICENSE
 *
 *  Copyright (c) 2013-2014 LG Electronics, Inc.
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
 *  @file BroadcastTimeHandler.cpp
 */

#include <stdint.h>
#include <pbnjson.hpp>

#include "JSONUtils.h"
#include "TimePrefsHandler.h"

// double macro extension to pre-process content
#define STRINGIFY(content...) #content
#define JSON(content...) STRINGIFY(content)

#define SCHEMA_LOCALTIME { \
                    "type": "object", \
                    "description": "Local time in components", \
                    "properties": { \
                        "year": { "type": "integer", "minimum": 1900 }, \
                        "month": { "type": "integer", "minimum": 1, "maximum": 12 }, \
                        "day": { "type": "integer", "minimum": 1, "maximum": 31 }, \
                        "hour": { "type": "integer", "minimum": 0, "maximum": 23 }, \
                        "minute": { "type": "integer", "minimum": 0, "maximum": 59 }, \
                        "second": { "type": "integer", "minimum": 0, "maximum": 59 } \
                    }, \
                    "required": [ "year", "month", "day", "hour", "minute", "second" ], \
                    "additionalProperties": false \
                }

namespace {
    const char *effectiveBroadcastKey = "effectiveBroadcastKey";
    pbnjson::JSchemaFragment schemaGeneric("{}");
    pbnjson::JSchemaFragment schemaEmptyObject(JSON({"additionalProperties": false}));
    pbnjson::JSchemaFragment schemaSubscribeRequest(JSON({
        "properties": {
            "subscribe": {
                "type": "boolean",
                "description": "Request additional replies that are sent in case when next reply can't be predicted",
                "default": false
            }
        },
        "additionalProperties": false
    }));

    // schema for /time/setBroadcastTime
    pbnjson::JSchemaFragment schemaSetBroadcastTime(JSON(
        {
            "type": "object",
            "description": "Method to notify system service about time info received in broadcast signal",
            "properties": {
                "utc": {
                    "type": "integer",
                    "description": "UTC time in seconds since epoch"
                },
                "local": {
                    "type": "integer",
                    "description": "Local time in seconds since epoch"
                }
            },
            "required": [ "utc", "local" ],
            "additionalProperties": false
        }
    ));

    // schema for /time/getBroadcastTime
    pbnjson::JSchemaFragment schemaGetBroadcastTimeReply(JSON(
        {
            "type": "object",
            "description": "Time info received from broadcast signal",
            "properties": {
                "returnValue": {
                    "type": "boolean",
                    "enum": [true]
                },
                "subscribed": {
                    "type": "boolean"
                },
                "utc": {
                    "type": "integer",
                    "description": "UTC time in seconds since epoch"
                },
                "adjustedUtc": {
                    "type": "integer",
                    "description": "UTC time in seconds since epoch adjusted with Time-Zone from local time"
                },
                "local": {
                    "type": "integer",
                    "description": "Local time in seconds since epoch"
                },
                "localtime": SCHEMA_LOCALTIME,
                "systemTimeSource": {
                    "type": "string",
                    "description": "Tag for clock system-time were synchronized with"
                }
            },
            "required": [ "returnValue", "local" ],
            "additionalProperties": false
        }
    ));

    bool reply(LSHandle* handle, LSMessage *message, const pbnjson::JValue &response, const pbnjson::JSchema &schema = schemaGeneric)
    {
        std::string serialized;

        pbnjson::JGenerator serializer(NULL);
        if (!serializer.toString(response, schema, serialized)) {
            qCritical() << "JGenerator failed";
            return false;
        }

        LSError lsError;
        LSErrorInit(&lsError);
        if (!LSMessageReply(handle, message, serialized.c_str(), &lsError))
        {
            qCritical() << "LSMessageReply failed, Error:" << lsError.message;
            LSErrorFree (&lsError);
            return false;
        }

        return true;
    }

    time_t toLocal(time_t utc)
    {
        // this is unusual for Unix to store local time in time_t
        // so we need to use some functions in a wrong way to get local time

        tm localTm;
        if (!localtime_r(&utc, &localTm)) return (time_t)-1;

        // re-convert to time_t pretending that we converting from UTC
        // (while converting from local)
        return timegm(&localTm);
    }

    time_t toUtc(time_t local)
    {
        // this is another hack to find which UTC time corresponds to one
        // stored in time_t that represents local time

        tm localTm;
        if (!gmtime_r(&local, &localTm)) return (time_t)-1;

        localTm.tm_isdst = -1; // mktime should lookup TZ rules

        return timelocal(&localTm);
    }

    time_t toTimeT(const pbnjson::JValue &value)
    {
        // this check will be compiled-out due to static condition
        if (sizeof(time_t) <= sizeof(int32_t))
        {
            return value.asNumber<int32_t>();
        }
        else
        {
            return value.asNumber<int64_t>();
        }
    }

    pbnjson::JValue toJValue(time_t value)
    {
        // this check will be compiled-out due to static condition
        if (sizeof(time_t) <= sizeof(int32_t))
        {
            return static_cast<int32_t>(value);
        }
        else
        {
            return static_cast<int64_t>(value);
        }
    }
    pbnjson::JValue toJValue(struct tm &tmValue)
    {
        pbnjson::JValue jValue = pbnjson::Object();
        jValue.put("year", tmValue.tm_year + 1900);
        jValue.put("month", tmValue.tm_mon + 1);
        jValue.put("day", tmValue.tm_mday);
        jValue.put("hour", tmValue.tm_hour);
        jValue.put("minute", tmValue.tm_min);
        jValue.put("second", tmValue.tm_sec);
        return jValue;
    }

    void addLocalTime(pbnjson::JValue &root, time_t local)
    {
        // gmtime/localtime both takes UTC and returns datetime broken into
        // components either without TZ or with TZ adjustment
        // Since we already have adjusted value we use gmtime
        tm tmLocal;
        if (!gmtime_r(&local, &tmLocal))
        {
            qWarning() << "gmtime() call failed - should never happen";
        }
        else
        {
            root.put("localtime", toJValue(tmLocal));
        }
    }

    /**
     * Builds JValue which represents answer to getEffectiveBroadcastTime
     *
     * @return false if error met and as result answer contains error info
     */
    bool answerEffectiveBroadcastTime(pbnjson::JValue &answer, const TimePrefsHandler &timePrefsHandler,
                                                               const BroadcastTime &broadcastTime)
    {
        time_t adjustedUtc, local;
        bool systemTimeUsed = false;
        if (timePrefsHandler.isSystemTimeBroadcastEffective())
        {
            // just use system local time (set by user)
            adjustedUtc = time(0);
            local = toLocal(adjustedUtc);
            systemTimeUsed = true;
        }
        else
        {
            if (!broadcastTime.get(adjustedUtc, local))
            {
                qWarning() << "Internal logic error (failed to get broadcast time while it is reported avaialble)";
                adjustedUtc = time(0);
                local = toLocal(adjustedUtc);
                systemTimeUsed = true;
            }
            else
            {
                // Broadcast sends correct utc and local time (with correct time-zone).
                // User may set time-zone in an incorrect value.
                // So instead of using UTC from broadcast we convert broadcast
                // local time to UTC according to user time-zone.
                // That allows clients to construct time object in a natural way
                // (from UTC).
                adjustedUtc = toUtc(local);
            }
        }

        if (local == (time_t)-1) // invalid time
        {
            answer.put("errorCode", int32_t(-1));
            answer.put("errorText", "Failed to get localtime");
            return false;
        }

        answer.put("adjustedUtc", toJValue(adjustedUtc));
        answer.put("local", toJValue(local));
        addLocalTime(answer, local);

        if (systemTimeUsed)
        {
            // add additional information associated with system time
            answer.put("systemTimeSource", TimePrefsHandler::instance()->getSystemTimeSource());
        }

        return true;
    }
}

bool TimePrefsHandler::cbSetBroadcastTime(LSHandle* handle, LSMessage *message,
                                          void *userData)
{
    TimePrefsHandler *timePrefsHandler = static_cast<TimePrefsHandler*>(userData);
    BroadcastTime &broadcastTime = timePrefsHandler->m_broadcastTime;

    LSMessageJsonParser parser(message, schemaSetBroadcastTime);
    if (!parser.parse("cbSetBroadcastTime", handle, EValidateAndErrorAlways)) return true;

    pbnjson::JValue request = parser.get();

    time_t utc = toTimeT(request["utc"]);
    time_t local = toTimeT(request["local"]);

    time_t utcCurrent = time(0);
    time_t utcOffset = utc - utcCurrent;

    // assume that broadcast local time is correct and allow user to set wrong time-zone
    time_t adjustedUtcOffset = toUtc(local) - time(0);

    PmLogInfo(sysServiceLogContext(), "SET_BROADCAST_TIME", 3,
        PMLOGKS("SENDER", LSMessageGetSenderServiceName(message)),
        PMLOGKFV("UTC_OFFSET", "%ld", utcOffset),
        PMLOGKFV("LOCAL_SHIFT", "%ld", local - utc),
        "/time/setBroadcastTime received with %s",
        parser.getPayload()
    );

    if (!broadcastTime.set( utc, local, timePrefsHandler->currentStamp()))
    {
        return reply(handle, message, createJsonReply(false, -2, "Failed to update broadcast time offsets"));
    }
    if (!timePrefsHandler->isManualTimeUsed()) timePrefsHandler->postBroadcastEffectiveTimeChange();

    // TODO: add handling of local clocks in ClockHandler
    timePrefsHandler->deprecatedClockChange.fire(adjustedUtcOffset, "broadcast-adjusted", utcCurrent);
    timePrefsHandler->deprecatedClockChange.fire(utcOffset, "broadcast", utcCurrent);

    return reply(handle, message, createJsonReply(true));
}

bool TimePrefsHandler::cbGetBroadcastTime(LSHandle* handle, LSMessage *message,
                                          void *userData)
{
    BroadcastTime &broadcastTime = static_cast<TimePrefsHandler*>(userData)->m_broadcastTime;

    LSMessageJsonParser parser(message, schemaEmptyObject);
    if (!parser.parse("cbGetBroadcastTime", handle, EValidateAndErrorAlways)) return true;

    time_t utc, local;
    if (!broadcastTime.get(utc, local))
    {
        return reply(handle, message, createJsonReply(false, -2, "No information available"));
    }

    pbnjson::JValue answer = pbnjson::Object();
    answer.put("returnValue", true);
    answer.put("utc", toJValue(utc));
    answer.put("local", toJValue(local));
    addLocalTime(answer, local);

    return reply(handle, message, answer, schemaGetBroadcastTimeReply);
}

bool TimePrefsHandler::cbGetEffectiveBroadcastTime(LSHandle* handle, LSMessage *message,
                                                   void *userData)
{
    TimePrefsHandler *timePrefsHandler = static_cast<TimePrefsHandler*>(userData);
    BroadcastTime &broadcastTime = timePrefsHandler->m_broadcastTime;

    LSMessageJsonParser parser(message, schemaSubscribeRequest);
    if (!parser.parse("cbGetEffectiveBroadcastTime", handle, EValidateAndErrorAlways)) return true;

    pbnjson::JValue request = parser.get();

    pbnjson::JValue answer = pbnjson::Object();
    if (!answerEffectiveBroadcastTime(answer, *timePrefsHandler, broadcastTime))
    {
        // error?
        answer.put("returnValue", false);
        return reply(handle, message, answer);
    }
    answer.put("returnValue", true);

    // handle subscription
    if (request["subscribe"].asBool())
    {
        LSError lsError;
        LSErrorInit(&lsError);
        bool subscribed = LSSubscriptionAdd(handle, effectiveBroadcastKey, message, &lsError);
        if (!subscribed)
        {
            qCritical() << " failed, Error:" << lsError.message;
        }
        LSErrorFree(&lsError);
        answer.put("subscribed", subscribed);
    }

    return reply(handle, message, answer, schemaGetBroadcastTimeReply);
}

void TimePrefsHandler::postBroadcastEffectiveTimeChange()
{
    pbnjson::JValue answer = pbnjson::Object();

    // ignore error (will be reported as one of the reply
    if (!answerEffectiveBroadcastTime(answer, *this, m_broadcastTime))
    {
        qWarning() << "Failed to prepare post answer for getEffectiveBroadcastTime subscription (ignoring)";
        return;
    }

    std::string serialized;

    pbnjson::JGenerator serializer(NULL);
    if (!serializer.toString(answer, schemaGeneric, serialized)) {
        qCritical() << "JGenerator failed";
        return;
    }

    LSError lsError;
    LSErrorInit(&lsError);
    if(!LSSubscriptionRespond(m_service, effectiveBroadcastKey, serialized.c_str(), &lsError))
    {
        qCritical() << "LSSubscriptionRespond failed, Error:" << lsError.message;
    }
    LSErrorFree(&lsError);
}
