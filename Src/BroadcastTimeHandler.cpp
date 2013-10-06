/****************************************************************
 * @@@LICENSE
 *
 * Copyright (c) 2013 LG Electronics, Inc.
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

#define JSON(content...) #content

namespace {
    pbnjson::JSchemaFragment schemaGeneric("{}");
    pbnjson::JSchemaFragment schemaEmptyObject(JSON({"additionalProperties": false}));

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
                "utc": {
                    "type": "integer",
                    "description": "UTC time in seconds since epoch",
                    "optional": true
                },
                "local": {
                    "type": "integer",
                    "description": "Local time in seconds since epoch"
                }
            },
            "additionalProperties": false
        }
    ));

    // schema for /time/getEffectiveBroadcastTime
    pbnjson::JSchemaFragment schemaGetEffectiveBroadcastTimeReply(JSON(
        {
            "type": "object",
            "description": "Effective local time for apps that relay on broadcast time",
            "properties": {
                "returnValue": {
                    "type": "boolean",
                    "enum": [true]
                },
                "local": {
                    "type": "integer",
                    "description": "Local time in seconds since epoch or user set time"
                }
            },
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
}

bool TimePrefsHandler::cbSetBroadcastTime(LSHandle* handle, LSMessage *message,
                                          void *userData)
{
    BroadcastTime &broadcastTime = static_cast<TimePrefsHandler*>(userData)->m_broadcastTime;

    LSMessageJsonParser parser(message, schemaSetBroadcastTime);
    if (!parser.parse("cbSetBroadcastTime", handle, EValidateAndErrorAlways)) return true;

    pbnjson::JValue request = parser.get();

    broadcastTime.set(toTimeT(request["utc"]), toTimeT(request["local"]));

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

    return reply(handle, message, answer, schemaGetBroadcastTimeReply);
}

bool TimePrefsHandler::cbGetEffectiveBroadcastTime(LSHandle* handle, LSMessage *message,
                                                   void *userData)
{
    TimePrefsHandler *timePrefsHandler = static_cast<TimePrefsHandler*>(userData);
    BroadcastTime &broadcastTime = timePrefsHandler->m_broadcastTime;

    LSMessageJsonParser parser(message, schemaEmptyObject);
    if (!parser.parse("cbGetEffectiveBroadcastTime", handle, EValidateAndErrorAlways)) return true;

    time_t local;
    if (timePrefsHandler->isManualTimeUsed())
    {
        // just use system local time (set by user)
        local = toLocal(time(0));
    }
    else
    {
        time_t utc;
        if (!broadcastTime.get(utc, local))
        {
            qWarning() << "No broadcast time available. Will use system local time (set by NTP/NITZ/RTC etc)";
            local = toLocal(time(0));
        }
    }

    if (local == (time_t)-1) // invalid time
    {
        return reply(handle, message, createJsonReply(false, -1, "Failed to get localtime"));
    }

    pbnjson::JValue answer = pbnjson::Object();
    answer.put("returnValue", true);
    answer.put("local", toJValue(local));

    return reply(handle, message, answer, schemaGetBroadcastTimeReply);
}
