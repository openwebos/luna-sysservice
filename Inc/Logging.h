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


#ifndef LOGGING_H
#define LOGGING_H

#include <stdio.h>
#include <glib.h>
#include <QtDebug>

#ifdef __cplusplus
extern "C" {
#endif

#if defined(USE_PMLOG)

#include "PmLogLib.h"

#define SYSSERVICELOG_MESSAGE_MAX 500
inline void sysServiceFmtMsg(char *logMsg, char *fmt, ...)
{
  va_list args;
  va_start (args, fmt);
  vsnprintf(logMsg, SYSSERVICELOG_MESSAGE_MAX, fmt, args);
  va_end (args);
}
extern void sysServiceLogInfo(const char * fileName, guint32 lineNbr,const char* funcName, const char *logMsg);

//  __qMessage() is a simpler version of a logging function which should exist in QDEBUG, but doesn't

#define __qMessage(...)  do { \
      char logMsg[SYSSERVICELOG_MESSAGE_MAX+1]; \
      sysServiceFmtMsg(logMsg, __VA_ARGS__); \
      sysServiceLogInfo(__FILE__, __LINE__, __func__, logMsg); \
} while(0)

#else // !defined(USE_PMLOG)
// Work-arounds to build without PmLogLib (sometimes used to simplify debugging).

// Probably we should drop possibility to build without USE_PMLOG or we shouldn't use
// PMLOG_TRACES without appropriate guard.

#define __qMessage(...)  do { g_message(__VA_ARGS__); } while (0)
#define PMLOG_TRACE(...) __qMessage(__VA_ARGS__)

#endif // USE_PMLOG

// Qt handler for logging
#if (QT_VERSION >= QT_VERSION_CHECK(5, 0, 0))
void outputQtMessages(QtMsgType type,
                    const QMessageLogContext &context,
                    const QString &msg);
#else
void outputQtMessages(QtMsgType type, const char *str);
#endif

#ifdef __cplusplus
}
#endif	

#endif /* LOGGING_H */
