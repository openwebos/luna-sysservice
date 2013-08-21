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


///INCLUDED FOR testNetwork...REMOVE!
#include <netdb.h>
#include <netinet/in.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/statfs.h> 

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>

#include <glib.h>
#include "PrefsFactory.h"

#include "LocalePrefsHandler.h"
#include "Logging.h"
#include "PrefsDb.h"
#include "PrefsHandler.h"
#include "TimePrefsHandler.h"
#include "WallpaperPrefsHandler.h"
#include "BuildInfoHandler.h"
#include "RingtonePrefsHandler.h"

#include "UrlRep.h"
#include "JSONUtils.h"

static const char* s_logChannel = "PrefsFactory";

static PrefsFactory* s_instance = 0;

static bool cbSetPreferences(LSHandle* lsHandle, LSMessage* message,
							 void* user_data);
static bool cbGetPreferences(LSHandle* lsHandle, LSMessage* message,
							 void* user_data);
static bool cbGetPreferenceValues(LSHandle* lsHandle, LSMessage* message,
								  void* user_data);

/*!
 * \page com_palm_systemservice Service API com.palm.systemservice/
 *
 * Public methods:
 * - \ref com_palm_systemservice_set_preferences
 * - \ref com_palm_systemservice_get_preferences
 * - \ref com_palm_systemservice_get_preference_values
 */

static LSMethod s_methods[] = {
	{ "setPreferences", cbSetPreferences },
	{ "getPreferences", cbGetPreferences },
	{ "getPreferenceValues", cbGetPreferenceValues },
	{ 0, 0 }
};

PrefsFactory* PrefsFactory::instance()
{
    if (!s_instance)
		new PrefsFactory;

	return s_instance;
}

PrefsFactory::PrefsFactory()
	: m_service(0)
{
	s_instance = this;
	(void) PrefsDb::instance();
}

PrefsFactory::~PrefsFactory()
{
	s_instance = 0;
}

void PrefsFactory::setServiceHandle(LSPalmService* service)
{
    m_service = service;

	bool result;
	LSError lsError;
	LSErrorInit(&lsError);
	
	result = LSPalmServiceRegisterCategory( m_service, "/", s_methods, NULL,
			NULL, this, &lsError);
	if (!result) {
			luna_critical(s_logChannel, "Failed to register methods: %s", lsError.message);
			LSErrorFree(&lsError);
			return;
		}

	m_serviceHandlePublic = LSPalmServiceGetPublicConnection(m_service);
	m_serviceHandlePrivate = LSPalmServiceGetPrivateConnection(m_service);
		
	// Now we can create all the prefs handlers
	registerPrefHandler(new LocalePrefsHandler(service));
	registerPrefHandler(new TimePrefsHandler(service));
	registerPrefHandler(new WallpaperPrefsHandler(service));
	registerPrefHandler(new BuildInfoHandler(service));
	registerPrefHandler(new RingtonePrefsHandler(service));
}

LSPalmService* PrefsFactory::serviceHandle() const
{
	return m_service;    
}

PrefsHandler* PrefsFactory::getPrefsHandler(const std::string& key) const
{
	PrefsHandlerMap::const_iterator it = m_handlersMaps.find(key);
	if (it == m_handlersMaps.end())
		return 0;
	
    return (*it).second;
}

void PrefsFactory::registerPrefHandler(PrefsHandler* handler)
{
	if (!handler)
		return;
	
	std::list<std::string> keys = handler->keys();
	for (std::list<std::string>::const_iterator it = keys.begin(); it != keys.end(); ++it)
		m_handlersMaps[*it] = handler;
}

void PrefsFactory::postPrefChange(const std::string& keyStr,const std::string& valueStr)
{
	LSSubscriptionIter *iter=NULL;
	LSError lserror;
	LSHandle * lsHandle;
	
	LSErrorInit(&lserror);
	
	std::string reply = std::string("{ \"")+keyStr+std::string("\":")+valueStr+std::string("}");
	// Find out which handle this subscription needs to go to
	bool retVal = LSSubscriptionAcquire(m_serviceHandlePublic, keyStr.c_str(), &iter, &lserror);
	if (retVal) {
		lsHandle = m_serviceHandlePublic;
		while (LSSubscriptionHasNext(iter)) {

			LSMessage *message = LSSubscriptionNext(iter);
			if (!LSMessageReply(lsHandle,message,reply.c_str(),&lserror)) {
				LSErrorPrint(&lserror,stderr);
				LSErrorFree(&lserror);
			}
		}

		LSSubscriptionRelease(iter);
	}
	else {
		LSErrorFree(&lserror);
	}
	
	LSErrorInit(&lserror);
	iter=NULL;
	retVal = LSSubscriptionAcquire(m_serviceHandlePrivate, keyStr.c_str(), &iter, &lserror);
	if (retVal) {
		lsHandle = m_serviceHandlePrivate;
		while (LSSubscriptionHasNext(iter)) {

			LSMessage *message = LSSubscriptionNext(iter);
			if (!LSMessageReply(lsHandle,message,reply.c_str(),&lserror)) {
				LSErrorPrint(&lserror,stderr);
				LSErrorFree(&lserror);
			}
		}

		LSSubscriptionRelease(iter);
	}
	else {
		LSErrorFree(&lserror);
	}

	
}

void PrefsFactory::postPrefChangeValueIsCompleteString(const std::string& keyStr,const std::string& json_string)
{
	LSSubscriptionIter *iter=NULL;
	LSError lserror;
	LSHandle * lsHandle;
	
	LSErrorInit(&lserror);
	//std::string reply = std::string("{ \"")+keyStr+std::string("\":")+valueStr+std::string("}");
	const std::string reply = json_string;
	//**DEBUG validate for correct UTF-8 output
	if (!g_utf8_validate (reply.c_str(), -1, NULL))
	{
		g_warning("%s: bus reply fails UTF-8 validity check! [%s]",__FUNCTION__,reply.c_str());
	}
	// Find out which handle this subscription needs to go to
	bool retVal = LSSubscriptionAcquire(m_serviceHandlePublic, keyStr.c_str(), &iter, &lserror);
	if (retVal) {
		lsHandle = m_serviceHandlePublic;
		while (LSSubscriptionHasNext(iter)) {

			LSMessage *message = LSSubscriptionNext(iter);
			if (!LSMessageReply(lsHandle,message,reply.c_str(),&lserror)) {
				LSErrorPrint(&lserror,stderr);
				LSErrorFree(&lserror);
			}
		}

		LSSubscriptionRelease(iter);
			
	}
	else  {
		LSErrorFree(&lserror);
	}
	
	LSErrorInit(&lserror);
	iter=NULL;
	retVal = LSSubscriptionAcquire(m_serviceHandlePrivate, keyStr.c_str(), &iter, &lserror);
	if (retVal) {
		lsHandle = m_serviceHandlePrivate;
		while (LSSubscriptionHasNext(iter)) {

			LSMessage *message = LSSubscriptionNext(iter);
			if (!LSMessageReply(lsHandle,message,reply.c_str(),&lserror)) {
				LSErrorPrint(&lserror,stderr);
				LSErrorFree(&lserror);
			}
		}

		LSSubscriptionRelease(iter);

	}
	else  {
		LSErrorFree(&lserror);
	}

}

void PrefsFactory::refreshAllKeys()
{

	//get all the keys from the db
	std::map<std::string,std::string> allPrefs = PrefsDb::instance()->getAllPrefs();

	for (std::map<std::string,std::string>::const_iterator it = allPrefs.begin();
			it != allPrefs.end(); ++it)
	{
		//iterate over all the keys in the database
		std::string key = it->first;
		std::string val = it->second;
		json_object* json = json_object_new_object();
		PrefsHandler* handler = PrefsFactory::instance()->getPrefsHandler(key);
		// Inform the handler about the change
		if (handler)
		{
			handler->valueChanged(key, val);
		}

		//post change about it
		postPrefChange(key,val);
		json_object_put(json);
	}

}

void PrefsFactory::runConsistencyChecksOnAllHandlers() 
{
	//go through all the handlers
	
	for (PrefsHandlerMap::iterator it = m_handlersMaps.begin();it != m_handlersMaps.end();it++) {
		std::string key = it->first;
		PrefsHandler * handler = it->second;
		if (handler) {
			//run the verifier on this key to make sure the pref is correct
			if (handler->isPrefConsistent() == false) {
				g_warning("PrefsFactory::runConsistencyChecksOnAllHandlers() reports inconsistency with key [%s]. Restoring default...",key.c_str());
				handler->restoreToDefault();		//something is wrong with this...try and restore it
				std::string restoreVal = PrefsDb::instance()->getPref(key);
				g_warning("PrefsFactory::runConsistencyChecksOnAllHandlers() key [%s] restored to value [%s]",key.c_str(),restoreVal.c_str());
				PrefsFactory::instance()->postPrefChange(key,restoreVal);
			}
		}
	}
}

/*!
\page com_palm_systemservice
\n
\section com_palm_systemservice_set_preferences setPreferences

\e Public.

com.palm.systemservice/setPreferences

Sets preference keys to specified values.

\subsection com_palm_systemservice_set_preferences_syntax Syntax:
\code
{
    "params" : object
}
\endcode

\param params An object containing one or more key-value pairs or other objects.

\subsection com_palm_systemservice_set_preferences_returns Returns:
\code
{
    "returnValue": boolean,
    "errorText": string
}
\endcode

\param returnValue Indicates if the call was succesful.
\param errorText Description of the error if call was not succesful.

\subsection com_palm_systemservice_set_preferences_examples Examples:
\code
luna-send -n 1 -f luna://com.palm.systemservice/setPreferences '{ "params": {"food":"pizza"} }'
\endcode

Example response for a succesful call:
\code
{
    "returnValue": true
}
\endcode

Example response for a failed call:
\code
{
    "returnValue": false,
    "errorText": "couldn't parse json"
}
\endcode
*/
static bool cbSetPreferences(LSHandle* lsHandle, LSMessage* message,
							 void* user_data)
{
	json_object* root = 0;
	bool result;
	bool success;
	LSError lsError;
	LSErrorInit(&lsError);
	std::string errorText;
	int savecount=0;
	int errcount=0;
	std::string callerId;
	
	const char* payload = LSMessageGetPayload(message);
	if (!payload) {
		success=false;
		errorText = std::string("missing payload");
		goto Done;
	}

	root = json_tokener_parse(payload);
	if (!root || is_error(root)) {
		root=0;
		success = false;
		errorText = std::string("couldn't parse json");
		goto Done;
	}
	
	callerId = (LSMessageGetApplicationID(message) != 0 ? LSMessageGetApplicationID(message) : "" );

	json_object_object_foreach(root, key, val) {
		// Is there a preferences handler for this?

		bool savedPref = false;
		
		PrefsHandler* handler = PrefsFactory::instance()->getPrefsHandler(key);
		
		if (handler) {
			g_warning("setPreference(): setPref found handler for %s",key);
			if (handler->validate(key, val, callerId)) {
				g_warning("setPreference(): setPref handler validated value for key [%s]",key);
				savedPref = PrefsDb::instance()->setPref(key, json_object_to_json_string(val));
			}
			else {
				g_warning("setPreference(): setPref handler DID NOT validate value for key [%s]",key);
			}
		}
		else {
			g_warning("setPreference(): setPref did NOT find handler for %s",key);
			
			//filter out 
			savedPref = PrefsDb::instance()->setPref(key, json_object_to_json_string(val));
		}
		g_warning("setPreference(): setPref saved? %s",(savedPref ? "true" : "false"));
		
		if (savedPref) {
			++savecount;
			
			// successfully set the preference. post a notification about it

			json_object* json = 0;
			
			json = json_object_new_object();
			json_object_object_add(json, (char*) key, json_object_get(val));
				
			std::string subKeyStr = std::string(key);
			std::string subValStr = std::string(json_object_to_json_string(json));
			
			PrefsFactory::instance()->postPrefChangeValueIsCompleteString(subKeyStr,subValStr);
			
			// Inform the handler about the change
			if (handler)
				handler->valueChanged(key, val);
			
			json_object_put(json);
			success=true;
		}
		else {
			++errcount;
		}
		
	}
	
	if (errcount) {
		success=false;
		errorText=std::string("Some settings could not be saved");
	}
	
Done:
	json_object * result_object = json_object_new_object();
	json_object_object_add(result_object,(char *)"returnValue",json_object_new_boolean(success));
	if (!success)
		json_object_object_add(result_object,(char *)"errorText",json_object_new_string((char*) errorText.c_str()));
	
	const char * r = json_object_to_json_string(result_object);
	result = LSMessageReply(lsHandle, message, r, &lsError);
	if (!result)
		LSErrorFree (&lsError);
	
	json_object_put(result_object);
	if (root)
		json_object_put(root);

	return true;
}

/*!
\page com_palm_systemservice
\n
\section com_palm_systemservice_get_preferences getPreferences

\e Public.

com.palm.systemservice/getPreferences

Retrieves the values for keys specified in a passed array. If subscribe is set to true, then getPreferences sends an update if the key values change.

\subsection com_palm_systemservice_get_preferences_syntax Syntax:
\code
{
    "subscribe" : boolean,
    "keys"      : string array
}
\endcode

\param subscribe If true, getPreferences sends an update whenever the value of one of the keys changes.
\param keys An array of key names. Required.

\subsection com_palm_systemservice_get_preferences_returns Returns:
\code
{
   "[no name]"   : object,
   "returnValue" : boolean
}
\endcode

\param "[no name]" Key-value pairs containing the values for the requested preferences. If the requested preferences key or keys do not exist, the object is empty.
\param returnValue Indicates if the call was succesful.

\subsection com_palm_systemservice_get_preferences_examples Examples:
\code
luna-send -n 1 -f luna://com.palm.systemservice/getPreferences '{"subscribe": false, "keys":["wallpaper", "ringtone"]}'
\endcode

Example response for a succesful call:
\code
{
    "ringtone": {
        "fullPath": "\/usr\/lib\/luna\/customization\/copy_binaries\/media\/internal\/ringtones\/Pre.mp3",
        "name": "Prē"
    },
    "wallpaper": {
        "wallpaperName": "flowers.png",
        "wallpaperFile": "\/usr\/lib\/luna\/system\/luna-systemui\/images\/flowers.png",
        "wallpaperThumbFile": ""
    },
    "returnValue": true
}
\endcode

Example response for a failed call:
\code
{
    "returnValue": false,
    "subscribed": false,
    "errorCode": "no keys specified"
}
\endcode
*/
static bool cbGetPreferences(LSHandle* lsHandle, LSMessage* message,
							 void* user_data)
{
    // {"subscribe": boolean, "keys": array}
    VALIDATE_SCHEMA_AND_RETURN(lsHandle,
                               message,
                               SCHEMA_2(REQUIRED(subscribe, boolean), REQUIRED(keys, array)));

    bool retVal;
	LSError lsError;
	const char* r = 0;
	std::string reply;
	json_object* root = 0;
	json_object* label = 0;
	json_object* replyRoot = 0;
	array_list* keyArray = 0;
	std::list<std::string> keyList;
	std::map<std::string, std::string> resultMap;
	bool subscription = false;	
	bool success = false;
	std::string errorCode;
	PrefsHandler* handler=NULL;
	std::string key;
	std::string restoreVal;
	
	const char* payload = LSMessageGetPayload(message);
	if (!payload)
		return false;
	
	LSErrorInit(&lsError);
	
	root = json_tokener_parse(payload);
	if (!root || is_error(root))
		goto Done;
	
	label = json_object_object_get(root, "subscribe");
	if (label && !is_error(label))
		subscription = json_object_get_boolean(label);

	label = json_object_object_get(root, "keys");
	if (!label || is_error(label)) {
		errorCode = "no keys specified";
		goto Done;
	}

	keyArray = json_object_get_array(label);
	if (!keyArray) {
		errorCode = "no key array specified";
		goto Done;
	}

	if (array_list_length(keyArray) <= 0) {
		errorCode = "invalid key array";
		goto Done;
	}

	for (int i = 0; i < array_list_length(keyArray); i++) {
		json_object* obj = (json_object*) array_list_get_idx(keyArray, i);
		if (!obj || is_error(obj))
			continue;
//		printf("getPrefs: [%s]",json_object_get_string(obj));
		if (!json_object_is_type(obj,json_type_string))
			continue;
		key = json_object_get_string(obj);
		handler = PrefsFactory::instance()->getPrefsHandler(key);
		if (handler) {
			//run the verifier on this key to make sure the pref is correct
			if (handler->isPrefConsistent() == false) {
				handler->restoreToDefault();		//something is wrong with this...try and restore it
				restoreVal = PrefsDb::instance()->getPref(key);
				PrefsFactory::instance()->postPrefChange(key,restoreVal);
			}
		}
		keyList.push_back(json_object_get_string(obj));
	}

	resultMap = PrefsDb::instance()->getPrefs(keyList);

	if (LSMessageIsSubscription(message)) {		
		
		for (std::list<std::string>::const_iterator it = keyList.begin();
			 it != keyList.end(); ++it) {
			(void) LSSubscriptionAdd(lsHandle, (*it).c_str(),
									 message, &lsError);
		}
		subscription = true;
	}
	else
		subscription = false;

	replyRoot = json_object_new_object();
	
	for (std::map<std::string, std::string>::const_iterator it = resultMap.begin();
		 it != resultMap.end(); ++it) {
		json_object* value = json_tokener_parse((*it).second.c_str());
		if (value && (!is_error(value))) {
			g_warning("getPreferences(): resultMap: [%s] -> [---, length %d]",(*it).first.c_str(),(*it).second.size());
			json_object_object_add(replyRoot,
					(char*) (*it).first.c_str(), value);
		}
		else {
			errorCode = std::string("invalid value encoded in preference (\"did you escape your strings?\")");
			success=false;
			goto Done;
		}
	}
        json_object_object_add(replyRoot,"subscribed",json_object_new_boolean(subscription));
	json_object_object_add(replyRoot,"returnValue",json_object_new_boolean(true));
	success = true;
		
Done:

	if (!is_error(replyRoot) && (success))
		reply = json_object_to_json_string(replyRoot);
	else
		reply = "{\"returnValue\":false,\"subscribed\":false , \"errorCode\":\""+errorCode+"\"}";

	r = reply.c_str();
	
	retVal = LSMessageReply(lsHandle, message, r, &lsError);
	if (!retVal)
		LSErrorFree (&lsError);

	if (replyRoot && !is_error(replyRoot))
		json_object_put(replyRoot);
	
	if (root && !is_error(root))
		json_object_put(root);

	return true;
}

/*!
\page com_palm_systemservice
\n
\section com_palm_systemservice_get_preference_values getPreferenceValues

\e Public.

com.palm.systemservice/getPreferenceValues

Retrieve the list of valid values for the specified key. If the key is of a type that takes one of a discrete set of valid values, getPreferenceValues returns that set. Otherwise, getPreferenceValues returns nothing for the key.

\subsection com_palm_systemservice_get_preference_values_syntax Syntax:
\code
{
    "key": string
}
\endcode

\param key Key name.

\subsection com_palm_systemservice_get_preference_value_returns Returns:
\code
{
    "[no name]"   : object,
    "returnValue" : boolean
}
\endcode

\param "[no name]" The key and the valid values.
\param returnValue Indicates if the call was succesful.

\subsection com_palm_systemservice_get_preference_value_examples Examples:
\code
luna-send -n 1 -f luna://com.palm.systemservice/getPreferenceValues '{"key": "wallpaper" }'
\endcode

Example responses for succesful calls:
\code
{
    "wallpaper": [
        {
            "wallpaperName": "flowers.png",
            "wallpaperFile": "\/media\/internal\/.wallpapers\/flowers.png",
            "wallpaperThumbFile": "\/media\/internal\/.wallpapers\/thumbs\/flowers.png"
        }
    ],
    "returnValue": true
}
\endcode
\code
{
    "timeFormat": [
        "HH12",
        "HH24"
    ],
    "returnValue": true
}
\endcode

Example response for a failed call:
\code
{
    "returnValue": false
}
\endcode
*/
static bool cbGetPreferenceValues(LSHandle* lsHandle, LSMessage* message,
								  void* user_data)
{
    // {"key": string}
    VALIDATE_SCHEMA_AND_RETURN(lsHandle,
                               message,
                               SCHEMA_1(REQUIRED(key, string)));

    bool retVal;
	LSError lsError;
	const char* reply = 0;
	json_object* root = 0;
	json_object* label = 0;
	json_object* replyRoot = 0;
	PrefsHandler* handler = 0;
	std::string key;
	bool success = false;
	
	const char* payload = LSMessageGetPayload(message);
	if (!payload)
		return false;

	LSErrorInit(&lsError);
	
	root = json_tokener_parse(payload);
	if (!root || is_error(root))
		goto Done;
	
	label = json_object_object_get(root, "key");
	if (!label || is_error(label))
		goto Done;
	key = json_object_get_string(label);

	handler = PrefsFactory::instance()->getPrefsHandler(key);
	if (!handler)
		goto Done;

	replyRoot = handler->valuesForKey(key);
	if (!replyRoot || is_error(replyRoot))
		goto Done;

	json_object_object_add(replyRoot,"returnValue",json_object_new_boolean(true));
	reply = json_object_to_json_string(replyRoot);
	success = true;
		
Done:

	if (!success)
		reply = "{\"returnValue\":false}";

	retVal = LSMessageReply(lsHandle, message, reply, &lsError);
	if (!retVal)
		LSErrorFree (&lsError);

	if (replyRoot && !is_error(replyRoot))
		json_object_put(replyRoot);
	
	if (root && !is_error(root))
		json_object_put(root);

	return true;
}
