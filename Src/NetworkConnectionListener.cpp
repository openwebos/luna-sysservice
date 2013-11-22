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


#include <cjson/json.h>

#include "PrefsFactory.h"

#include "NetworkConnectionListener.h"
#include "Logging.h"
#include "JSONUtils.h"

NetworkConnectionListener* NetworkConnectionListener::instance()
{
	static NetworkConnectionListener* s_instance = 0;
	if (!s_instance)
		s_instance = new NetworkConnectionListener;
												  
	return s_instance;
}

NetworkConnectionListener::NetworkConnectionListener()
	: m_isInternetConnectionAvailable(false)
{
	registerForConnectionManager();
}

NetworkConnectionListener::~NetworkConnectionListener()
{
}

void NetworkConnectionListener::registerForConnectionManager()
{
	LSPalmService* palmService = PrefsFactory::instance()->serviceHandle();
	LSHandle* service = LSPalmServiceGetPrivateConnection(palmService);

	LSError error;
	LSErrorInit(&error);

	bool ret = LSCall(service, "palm://com.palm.lunabus/signal/registerServerStatus",
					  "{\"serviceName\":\"com.palm.connectionmanager\"}",
					  connectionManagerConnectCallback, this, NULL,
					  &error);
	if (!ret) {
        qCritical() << "Failed in calling palm://com.palm.lunabus/signal/registerServerStatus:" << error.message;
		LSErrorFree(&error);
		return;
	}
}

bool NetworkConnectionListener::connectionManagerConnectCallback(LSHandle *sh, LSMessage *message, void *ctx)
{
    if (LSMessageIsHubErrorMessage(message)) {  // returns false if message is NULL
        qWarning("The message received is an error message from the hub");
        return true;
    }
	return NetworkConnectionListener::instance()->connectionManagerConnectCallback(sh, message);
}

bool NetworkConnectionListener::connectionManagerGetStatusCallback(LSHandle* sh, LSMessage* message, void* ctxt)
{
    if (LSMessageIsHubErrorMessage(message)) {  // returns false if message is NULL
        qWarning("The message received is an error message from the hub");
        return true;
    }
	return NetworkConnectionListener::instance()->connectionManagerGetStatusCallback(sh, message);
}

bool NetworkConnectionListener::connectionManagerConnectCallback(LSHandle *sh, LSMessage *message)
{
    // {"serviceName": string, "connected": boolean}
    VALIDATE_SCHEMA_AND_RETURN(sh,
                               message,
                               SCHEMA_2(REQUIRED(serviceName, string), REQUIRED(connected, boolean)));

	if (!message)
		return true;

	const char* payload = LSMessageGetPayload(message); 
	json_object* label = 0;
	json_object* json = 0;
	bool connected = false;
	
	label = 0;
	json = json_tokener_parse(payload);
	if (!json || is_error(json))
		return true;

	label = json_object_object_get(json, "connected");
	if (!label || is_error(label)) {
		json_object_put(json);
		return true;
	}

	connected = json_object_get_boolean(label);

	if (!connected) {
		json_object_put(json);
		return true;
	}

	bool ret = false;
	LSError error;
	LSErrorInit(&error);

	LSPalmService* palmService = PrefsFactory::instance()->serviceHandle();
	LSHandle* service = LSPalmServiceGetPrivateConnection(palmService);
	
	ret = LSCall(service, "palm://com.palm.connectionmanager/getstatus",
				 "{\"subscribe\":true}",
				 connectionManagerGetStatusCallback, NULL, NULL, &error);
	if (!ret) {
        qCritical() << "Failed in calling palm://com.palm.connectionmanager/getstatus:" << error.message;
		LSErrorFree(&error);
	}

	json_object_put(json);
	return true;
}

bool NetworkConnectionListener::connectionManagerGetStatusCallback(LSHandle *sh, LSMessage *message)
{
    // {"isInternetConnectionAvailable": boolean}
	VALIDATE_SCHEMA_AND_RETURN( sh, message, RELAXED_SCHEMA(
		PROPS_3(
			REQUIRED(returnValue, boolean),
			OPTIONAL(subscribed, boolean),
			OPTIONAL(isInternetConnectionAvailable, boolean)
		)

		REQUIRED_1( returnValue )
	));

	if (!message)
		return true;

	const char* payload = LSMessageGetPayload(message); 
	json_object* label = 0;
	json_object* json = 0;
	bool isInternetConnectionAvailable;

	json = json_tokener_parse(payload);
	if (!json || is_error(json)) {
		return true;
	}

	label = json_object_object_get(json, "isInternetConnectionAvailable");
	if (label && !is_error(label)) {
		isInternetConnectionAvailable = json_object_get_boolean(label);
	}
    else {
        isInternetConnectionAvailable = false;
    }

	json_object_put(json);

	if (m_isInternetConnectionAvailable != isInternetConnectionAvailable) {
		m_isInternetConnectionAvailable = isInternetConnectionAvailable;
		signalConnectionStateChanged.fire(isInternetConnectionAvailable);
	}

	return true;	
}
