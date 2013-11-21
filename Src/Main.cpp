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


#include <stdio.h>
#include <glib.h>
#include <strings.h>
#include <time.h>
#include <syslog.h>

#include <QCoreApplication>

#if (QT_VERSION >= QT_VERSION_CHECK(5, 0, 0))
 #include <QGuiApplication>
#endif

#include <luna-service2/lunaservice.h>

#include "PrefsDb.h"
#include "PrefsFactory.h"
#include "Logging.h"

#include "SystemRestore.h"
#include "Mainloop.h"
#include "ImageServices.h"
#include "TimeZoneService.h"
#include "OsInfoService.h"
#include "DeviceInfoService.h"

#include "BackupManager.h"
#include "EraseHandler.h"

#include "Utils.h"
#include "Settings.h"
#include "JSONUtils.h"


static void logFilter(const gchar *log_domain, GLogLevelFlags log_level, const gchar *message, gpointer unused_data);

GMainLoop * g_gmainLoop = NULL;

static gchar* s_logLevelStr = NULL;
static gboolean s_useSysLog = false;

int gLoggerLevel = G_LOG_LEVEL_WARNING;

static void turnNovacomOn(LSHandle * lshandle);

static bool parseCommandlineOptions(int argc, char** argv)
{
    GError* error = NULL;
    GOptionContext* context = NULL;
	bool succeed = true;

	static GOptionEntry entries[] = {
		{ "logger", 'l', 0, G_OPTION_ARG_STRING,  &s_logLevelStr, "log level", "level"},
		{ "syslog", 's', 0, G_OPTION_ARG_NONE, &s_useSysLog, "Use syslog", NULL },
		{ NULL }
	};

	context = g_option_context_new(NULL);
	g_option_context_add_main_entries (context, entries, NULL);
	if (! g_option_context_parse (context, &argc, &argv, &error) )
	{
		g_printerr("Error: %s\n", error->message);
		g_error_free(error);
		succeed = false;
	}

    if (s_logLevelStr)
    {
        if (0 == strcasecmp(s_logLevelStr, "error"))
        	gLoggerLevel = G_LOG_LEVEL_ERROR;
        else if (0 == strcasecmp(s_logLevelStr, "critical"))
        	gLoggerLevel = G_LOG_LEVEL_CRITICAL;
        else if (0 == strcasecmp(s_logLevelStr, "warning"))
        	gLoggerLevel = G_LOG_LEVEL_WARNING;
        else if (0 == strcasecmp(s_logLevelStr, "message"))
        	gLoggerLevel = G_LOG_LEVEL_MESSAGE;
        else if (0 == strcasecmp(s_logLevelStr, "info"))
        	gLoggerLevel = G_LOG_LEVEL_INFO;
        else if (0 == strcasecmp(s_logLevelStr, "debug"))
        	gLoggerLevel = G_LOG_LEVEL_DEBUG;
    }

	g_option_context_free(context);

	return succeed;
}

static void setLoglevel(const char * loglstr)
{
	if (loglstr)
	{
		if (0 == strcasecmp(loglstr, "error"))
			gLoggerLevel = G_LOG_LEVEL_ERROR;
		else if (0 == strcasecmp(loglstr, "critical"))
			gLoggerLevel = G_LOG_LEVEL_CRITICAL;
		else if (0 == strcasecmp(loglstr, "warning"))
			gLoggerLevel = G_LOG_LEVEL_WARNING;
		else if (0 == strcasecmp(loglstr, "message"))
			gLoggerLevel = G_LOG_LEVEL_MESSAGE;
		else if (0 == strcasecmp(loglstr, "info"))
			gLoggerLevel = G_LOG_LEVEL_INFO;
		else if (0 == strcasecmp(loglstr, "debug"))
			gLoggerLevel = G_LOG_LEVEL_DEBUG;
	}
}

static bool cbComPalmImage2Status(LSHandle* lsHandle, LSMessage *message,
									void *user_data)
{
	LSError lsError;
	LSErrorInit(&lsError);

    // {"serviceName": string, "connected": boolean}
    VALIDATE_SCHEMA_AND_RETURN(lsHandle,
                               message,
                               SCHEMA_2(REQUIRED(serviceName, string), REQUIRED(connected, boolean)));

	json_object * root = 0;
	const char* str = LSMessageGetPayload(message);
	if( !str )
		return false;

	root = json_tokener_parse(str);
	if ((root == NULL) || (is_error(root))) {
		root = NULL;
		return true;
	}

	json_object * label = Utils::JsonGetObject(root,"connected");
	if (label != NULL)
	{
		if (json_object_get_boolean(label) == true)
		{
			//image2 is available
			Settings::settings()->m_image2svcAvailable = true;
		}
		else
		{
			Settings::settings()->m_image2svcAvailable = false;
		}
	}
	else
	{
		Settings::settings()->m_image2svcAvailable = false;
	}

	json_object_put(root);
	return true;
}

int main(int argc, char ** argv)
{
    setenv("QT_PLUGIN_PATH","/usr/plugins",1);
    setenv("QT_QPA_PLATFORM", "minimal",1);

#if (QT_VERSION >= QT_VERSION_CHECK(5, 0, 0))
    qInstallMessageHandler(outputQtMessages);
    QGuiApplication app(argc, argv);
#else
    qInstallMsgHandler(outputQtMessages);
    QCoreApplication app(argc, argv);
#endif

	if (!parseCommandlineOptions(argc, argv))
	{
		// error already reported
		return 1;
	}
	setLoglevel(Settings::settings()->m_logLevel.c_str());

	g_log_set_default_handler(logFilter, NULL);
	PMLOG_TRACE("%s:Started",__FUNCTION__);

	SystemRestore::createSpecialDirectories();
	
	// Initialize the Preferences database
	(void) PrefsDb::instance();
	///and system restore (refresh settings while I'm at it...)
	SystemRestore::instance()->refreshDefaultSettings();
	
	//run startup restore before anything else starts
	SystemRestore::startupConsistencyCheck();
	
	Mainloop * mainLoopObj = new Mainloop();
	g_gmainLoop = mainLoopObj->getMainLoopPtr();
	
	//GMainLoop* mainLoop =  g_main_loop_new(NULL, FALSE);	
	
	LSPalmService* serviceHandle = NULL;
	LSError lsError;
	bool result;

	LSErrorInit(&lsError);

	// Register the service
	result = LSRegisterPalmService("com.palm.systemservice", &serviceHandle, &lsError);
	if (!result) {
        qCritical() << "Failed to register service: com.palm.sysservice";
		return -1;
	}

//	LSHandle * serviceHandlePublic = LSPalmServiceGetPublicConnection(serviceHandle);
	LSHandle * serviceHandlePrivate = LSPalmServiceGetPrivateConnection(serviceHandle);
		
	result = LSGmainAttachPalmService(serviceHandle, g_gmainLoop, &lsError);
	if (!result) {
        qCritical() << "Failed to attach service handle to main loop";
		return -1;
	}

	//turn novacom on if requested
	if (Settings::settings()->m_turnNovacomOnAtStartup)
	{
		turnNovacomOn(serviceHandlePrivate);
	}

	if ((result = LSCall(serviceHandlePrivate,"palm://com.palm.bus/signal/registerServerStatus",
			"{\"serviceName\":\"com.palm.image2\", \"subscribe\":true}",
			cbComPalmImage2Status,NULL,NULL, &lsError)) == false)
	{
		//non-fatal
		LSErrorFree(&lsError);
		LSErrorInit(&lsError);
	}

	// register for storage daemon signals
	result = LSCall(serviceHandlePrivate,
			"palm://com.palm.lunabus/signal/addmatch",
			"{\"category\":\"/storaged\", \"method\":\"MSMAvail\"}",
			SystemRestore::msmAvailCallback, NULL, SystemRestore::instance()->getLSStorageToken(), &lsError);
	if (!result)
		return -1;

	result = LSCall(serviceHandlePrivate,
			"palm://com.palm.lunabus/signal/addmatch",
			"{\"category\":\"/storaged\", \"method\":\"MSMProgress\"}",
			SystemRestore::msmProgressCallback, NULL, SystemRestore::instance()->getLSStorageToken(), &lsError);
	if (!result)
		return -1;

	result = LSCall(serviceHandlePrivate,
			"palm://com.palm.lunabus/signal/addmatch",
			"{\"category\":\"/storaged\", \"method\":\"MSMEntry\"}",
			SystemRestore::msmEntryCallback, NULL, SystemRestore::instance()->getLSStorageToken(), &lsError);
	if (!result)
		return -1;

	result = LSCall(serviceHandlePrivate,
			"palm://com.palm.lunabus/signal/addmatch",
			"{\"category\":\"/storaged\", \"method\":\"MSMFscking\"}",
			SystemRestore::msmFsckingCallback, NULL, SystemRestore::instance()->getLSStorageToken(), &lsError);
	if (!result)
		return -1;

	result = LSCall(serviceHandlePrivate,
			"palm://com.palm.lunabus/signal/addmatch",
			"{\"category\":\"/storaged\", \"method\":\"PartitionAvail\"}",
			SystemRestore::msmPartitionAvailCallback, NULL, SystemRestore::instance()->getLSStorageToken(), &lsError);
	if (!result)
		return -1;

	// Initialize the Prefs Factory
	PrefsFactory::instance()->setServiceHandle(serviceHandle);
	BackupManager::instance()->setServiceHandle(serviceHandle);

	// Initialize erase handler
	if (!EraseHandler::instance()->init())
	{
		PmLogError(sysServiceLogContext(), "ERASE_FAILURE", 0, "Failed to init EraseHandler (functionality disabled)");
	}
	EraseHandler::instance()->setServiceHandle(serviceHandle);

	//init the image service
	ImageServices *imgSvc = ImageServices::instance(mainLoopObj);
	if (!imgSvc) {
        qCritical() << "Image service failed init!";
	}

	//init the timezone service;
	TimeZoneService *tzSvc = TimeZoneService::instance();
	tzSvc->setServiceHandle(serviceHandle);

        //init the osinfo service;
	OsInfoService *osiSvc = OsInfoService::instance();
	osiSvc->setServiceHandle(serviceHandle);

	//init the deviceinfo service;
	DeviceInfoService *diSvc = DeviceInfoService::instance();
	diSvc->setServiceHandle(serviceHandle);
	
	// Run the main loop
	g_main_loop_run(g_gmainLoop);
	
	return 0;
}

static void turnNovacomOn(LSHandle * lshandle)
{
	LSError lserror;
	LSErrorInit(&lserror);
	if (!(LSCall(lshandle,"palm://com.palm.connectionmanager/setnovacommode",
			"{\"isEnabled\":true, \"bypassFirstUse\":false}",
			NULL,NULL,NULL, &lserror)))
	{
        qCritical() << "failed to force novacom to On state";
		LSErrorFree(&lserror);
	}
}

static void logFilter(const gchar *log_domain, GLogLevelFlags log_level, const gchar *message, gpointer unused_data)
{
    int priority;

    if ((log_level & G_LOG_LEVEL_MASK) > gLoggerLevel) return;

    switch (log_level & G_LOG_LEVEL_MASK) {
    case G_LOG_LEVEL_ERROR:
    	priority = LOG_CRIT;
    	break;
    case G_LOG_LEVEL_CRITICAL:
    	priority = LOG_ERR;
    	break;
    case G_LOG_LEVEL_WARNING:
    	priority = LOG_WARNING;
    	break;
    case G_LOG_LEVEL_MESSAGE:
    	priority = LOG_NOTICE;
    	break;
    case G_LOG_LEVEL_DEBUG:
    	priority = LOG_DEBUG;
    	break;
    case G_LOG_LEVEL_INFO:
    default:
    	priority = LOG_INFO;
    	break;
    }
    syslog(priority, "%s", message);
}


