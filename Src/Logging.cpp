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


#include <glib.h>
#include <stdlib.h>
#include <unistd.h>

#include "Logging.h"

#if defined(USE_PMLOG)

PmLogContext sysServiceLogContext()
{
    static PmLogContext logContext = PmLogGetContextInline("LunaSysService");
    return logContext;
}

static const size_t MSGID_LENGTH = 30;

static char * fmtUniqueLogId(char *dest, const char *pFile, guint32 lineNbr)
{
    const char *pStart = strrchr (pFile, '/');
    gchar *str = g_ascii_strup((pStart ? pStart + 1 : pFile), MSGID_LENGTH - 6);
    char *ptr = strchr(str, '.');
    if (ptr) *ptr = '\0'; // trim off file extensions
    snprintf (dest, MSGID_LENGTH, "%s#%d", str, lineNbr);
    g_free (str);
    return dest;
}

void sysServiceLogInfo(const char * fileName, guint32 lineNbr,const char* funcName, const char *logMsg)
{
    PmLogContext pmContext = sysServiceLogContext();
    // Length will be an arbitrary short string with length up to 31
    char msgId[MSGID_LENGTH+1];

    PmLogInfo(pmContext, fmtUniqueLogId(msgId, fileName, lineNbr),
            1, PMLOGKS("FUNC", funcName),
            "%s", logMsg);
}
#endif // USE_PMLOG


// Qt message handlers
// (combination of Qt API x USE_PMLOG flag produces 4 variants)

#if defined(USE_PMLOG)

#if QT_VERSION >= QT_VERSION_CHECK(5, 0, 0)
void outputQtMessages(QtMsgType type,
                    const QMessageLogContext &context,
                    const QString &msg)
{
    PmLogContext pmContext = sysServiceLogContext();
    // Length will be an arbitrary short string with length up to 31
    char msgId[MSGID_LENGTH+1];

    QString file = QString(context.file).section('/', -1);

    switch (type) {
    case QtDebugMsg:
#ifndef NO_LOGGING
            PmLogDebug(pmContext, "%s", msg.toStdString().c_str());
#endif
        break;
    case QtWarningMsg:
        PmLogWarning(pmContext, fmtUniqueLogId(msgId, file.toStdString().c_str(), context.line),
                1, PMLOGKS("FUNC", context.function),
                "%s", msg.toStdString().c_str());
        break;
    case QtCriticalMsg:
        PmLogError(pmContext,  fmtUniqueLogId(msgId, file.toStdString().c_str(), context.line),
                1, PMLOGKS("FUNC", context.function),
                "%s", msg.toStdString().c_str());
        break;
    case QtFatalMsg:
        PmLogCritical(pmContext,  fmtUniqueLogId(msgId, file.toStdString().c_str(), context.line),
                1, PMLOGKS("FUNC", context.function),
                "%s", msg.toStdString().c_str());
        abort();
    }
}
#else // QT_VERSION < 5.0.0
void outputQtMessages(QtMsgType type, const char *str) {
    PmLogContext pmContext = sysServiceLogContext();

    // before Qt 5.0 we had no context information (no filename and line number)
    // so lets use one predefined MSGID
    static char *msgId = "QTLOG";

    switch (type) {
    case QtDebugMsg:
#ifndef NO_LOGGING
        PmLogDebug(pmContext, "%s", str);
#endif
        break;
    case QtWarningMsg:
        PmLogWarning(pmContext, msgId, 0, "%s", str);
        break;
    case QtCriticalMsg:
        PmLogError(pmContext, msgId, 0, "%s", str);
        break;
    case QtFatalMsg:
        PmLogCritical(pmContext, msgId, 0, "%s", str);
        abort();
    }
}
#endif // QT_VERSION

#else  //USE_PMLOG

#if (QT_VERSION >= QT_VERSION_CHECK(5, 0, 0))
void outputQtMessages(QtMsgType type,
            const QMessageLogContext &context,
                    const QString &msg)
{
    QByteArray utf_text(msg.toUtf8());
    const char *raw(utf_text.constData());
    switch (type) {
        default:
    case QtDebugMsg:
#ifndef NO_LOGGING
        g_debug("%s: %s", context.function, raw);
#endif
        break;
    case QtWarningMsg:
        g_warning ("%s: %s", context.function, raw);
        break;
    case QtCriticalMsg:
        g_error("%s: %s", context.function, raw);
        break;
    case QtFatalMsg:
        g_critical("%s: %s", context.function, raw);
        abort();
    }
}
#else
void outputQtMessages(QtMsgType type, const char *str) {
    switch(type)
    {
    case QtDebugMsg:
#ifndef NO_LOGGING
        g_debug("QDebug: %s", str);
#endif
        break;
    case QtWarningMsg:
        g_warning("QWarning: %s", str);
        break;
    case QtCriticalMsg:
        g_critical("QCritical: %s", str);
        break;
    case QtFatalMsg:
        g_error("QFatal: %s", str);
        break;
    default:
        g_message("QMessage: %s", str);
        break;
    }
}
#endif

#endif  // #ifdef USE_PMLOG
