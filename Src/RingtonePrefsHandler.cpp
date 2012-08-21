/**
 *  Copyright 2012 Hewlett-Packard Development Company, L.P.
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
#include "RingtonePrefsHandler.h"
#include "SystemRestore.h"
#include "Utils.h"
#include "UrlRep.h"
#include "Logging.h"
#include "JSONUtils.h"

static const char* s_logChannel = "RingtonePrefsHandler";

static bool cbAddRingtone(LSHandle* lsHandle, LSMessage *message,
							void *user_data);

static bool cbDeleteRingtone(LSHandle* lsHandle, LSMessage *message,
							void *user_data);

static LSMethod s_methods[]  = {
	{ "addRingtone",     cbAddRingtone},
	{ "deleteRingtone",	 cbDeleteRingtone},
    { 0, 0 },
};

RingtonePrefsHandler::RingtonePrefsHandler(LSPalmService* service) : PrefsHandler(service)
{
	init();
}

RingtonePrefsHandler::~RingtonePrefsHandler()
{
	
}

void RingtonePrefsHandler::init() {
	luna_log(s_logChannel,"RingtonePrefsHandler::init()");
	bool result;
	LSError lsError;
	LSErrorInit(&lsError);
	
	result = LSPalmServiceRegisterCategory( m_service, "/ringtone", s_methods, NULL,
			NULL, this, &lsError);
	if (!result) {
		luna_critical(s_logChannel, "Failed in registering ringtone handler method: %s", lsError.message);
		LSErrorFree(&lsError);
		return;
	}

	m_serviceHandlePublic = LSPalmServiceGetPublicConnection(m_service);
	m_serviceHandlePrivate = LSPalmServiceGetPrivateConnection(m_service);

	    
	result = LSCategorySetData(m_serviceHandlePublic, "/ringtone", this, &lsError);
	if (!result) {
		luna_critical(s_logChannel, "Failed in LSCategorySetData: %s", lsError.message);
		LSErrorFree(&lsError);
		return;
	}
	
	result = LSCategorySetData(m_serviceHandlePrivate, "/ringtone", this, &lsError);
	if (!result) {
		luna_critical(s_logChannel, "Failed in LSCategorySetData: %s", lsError.message);
		LSErrorFree(&lsError);
		return;
	}
	
}

std::list<std::string> RingtonePrefsHandler::keys() const 
{
	std::list<std::string> k;
	k.push_back("ringtone");
	return k;
}

bool RingtonePrefsHandler::validate(const std::string& key, json_object* value)
{
	return true;		//TODO: should possibly see if the pref points to a valid file
}

void RingtonePrefsHandler::valueChanged(const std::string& key, json_object* value)
{
	//TODO: should possibly see if the pref points to a valid file
}

json_object* RingtonePrefsHandler::valuesForKey(const std::string& key)
{
	//TODO: could scan ringtone folders and return possible files. However, since selection is handled by file picker which
	// 		may be scanning in unspecified locations, this wouldn't exactly be accurate
	json_object* json = json_object_new_object();
	json_object* arrayObj = json_object_new_array();
	json_object_object_add(json,(char *)"ringtone",arrayObj);
	return json;	
}

bool RingtonePrefsHandler::isPrefConsistent()
{
	return SystemRestore::instance()->isRingtoneSettingConsistent();
}

void RingtonePrefsHandler::restoreToDefault() 
{
	SystemRestore::instance()->restoreDefaultRingtoneSetting();
}

static bool cbAddRingtone(LSHandle* lsHandle, LSMessage *message,void *user_data)
{
    // {"filePath": string}
    VALIDATE_SCHEMA_AND_RETURN(lsHandle,
                               message,
                               SCHEMA_1(REQUIRED(filePath, string)));

    bool success = true;
    
    int rc = 0;

    std::string result;	
	std::string srcFileName;
	std::string errorText;
	std::string targetFileAndPath;
	std::string pathPart ="";
	std::string filePart ="";
	
	UrlRep urlRep;
	
	json_object* response = 0;
	struct json_object* label = 0;
	struct json_object* root = 0;
	
	LSError     lsError;
	LSErrorInit(&lsError);
	
	const char* payload = LSMessageGetPayload(message);
	if( !payload ) {
		success = false;
		errorText = std::string("couldn't get the message");
		goto Done;
	}
		
	root = json_tokener_parse(payload);
	
	if (!root || is_error(root)) {
		root = 0;
		success = false;
		errorText = std::string("couldn't parse json");
		goto Done;
	}
	
	label = json_object_object_get(root, "filePath");
	if(!label || is_error(label)) {
		success = false;
		errorText = std::string("source file missing");
		goto Done;
	}
	
	srcFileName = json_object_get_string(label);
	
	//parse the string as a URL

	urlRep = UrlRep::fromUrl(srcFileName.c_str());
	
	if (urlRep.valid == false) {
		errorText = std::string("invalid specification for source file (please use url format)");
		success = false;
		goto Done;
	}
	
	// UNSUPPORTED: non-file:// schemes 
	if ((urlRep.scheme != "") && (urlRep.scheme != "file")) {
		errorText = std::string("input file specification doesn't support non-local files (use file:///path/file or /path/file format");
		success = false;
		goto Done;
	}
	
	//check the file exist on the file system.
	if (!Utils::doesExistOnFilesystem(srcFileName.c_str())) {
		errorText = std::string("source file doesn't exist");
		success = false;
		goto Done;
	}
	
	//copy it to the media partition
	
	Utils::splitFileAndPath(srcFileName,pathPart,filePart);
	
	if (filePart.length() == 0) {
		errorText = std::string("source file name missing.");
		success = false;
		goto Done;
	}
	
	targetFileAndPath = std::string(PrefsDb::s_mediaPartitionPath)+std::string(PrefsDb::s_mediaPartitionRingtonesDir)+std::string("/")+filePart;
	rc = Utils::fileCopy(srcFileName.c_str(),targetFileAndPath.c_str());
	
	if (rc == -1) {
		errorText = std::string("Unable to add ringtone.");
		success = false;
		goto Done;
	}
	
	Done: 
	
		if (root)
			json_object_put(root);
	
		response = json_object_new_object();
		json_object_object_add(response, (char*) "returnValue", json_object_new_boolean(success));
		
		if (!success) {
			json_object_object_add(response,(char*) "errorText",json_object_new_string(const_cast<char*>(errorText.c_str())));
		}
		
		if (!LSMessageReply(lsHandle, message, json_object_to_json_string (response), &lsError )) 	{
		    LSErrorPrint (&lsError, stderr);
		    LSErrorFree(&lsError);
		}

		if (response)
			json_object_put(response);

	return true;
}

static bool cbDeleteRingtone(LSHandle* lsHandle, LSMessage *message, void *user_data)
{
    // {"filePath": string}
    VALIDATE_SCHEMA_AND_RETURN(lsHandle,
                               message,
                               SCHEMA_1(REQUIRED(filePath, string)));

    bool success = true;
    int rc = 0;

    std::string result;	
	std::string srcFileName;
	std::string errorText;
	std::string pathPart ="";
	std::string filePart ="";
	std::string ringtonePartition = std::string(PrefsDb::s_mediaPartitionPath)+std::string(PrefsDb::s_mediaPartitionRingtonesDir)+std::string("/");
	
	json_object* response = 0;
	struct json_object* root = 0;
	struct json_object* label = 0;
	
	LSError     lsError;
	LSErrorInit(&lsError);
	
	const char* payload = LSMessageGetPayload(message);
	if( !payload ) {
		success = false;
		errorText = std::string("couldn't get the message");
		goto Done;
	}
		
	root = json_tokener_parse(payload);

	if (!root || is_error(root)) {
		root = 0;
		success = false;
		errorText = std::string("couldn't parse json");
		goto Done;
	}
	
	label = json_object_object_get(root, "filePath");
	if(!label || is_error(label)) {
		success = false;
		errorText = std::string("filePath is missing");
		goto Done;
	}
	
	srcFileName = json_object_get_string(label);
	
	//check the file exist on the file system.
	if (!Utils::doesExistOnFilesystem(srcFileName.c_str())) {
		errorText = std::string("file doesn't exist");
		success = false;
		goto Done;
	}
	
	//make sure we are deleting files only from ringtone partion.
	Utils::splitFileAndPath(srcFileName,pathPart,filePart);
	
	if (filePart.length() == 0) {
		errorText = std::string("source file name missing.");
		success = false;
		goto Done;
	}
	
	if(pathPart.compare(ringtonePartition) != 0) {
		errorText = std::string("Unable to delete.");
		success = false;
		goto Done;
	}
	
	//UI is currently making sure that the current ringtone is not getting deleted. May be, we can check again here to make sure the current ringtone is not removed.
	
	rc = unlink(srcFileName.c_str());
	if(rc == -1) {
		errorText = std::string("Unable to delete ringtone.");
		success = false;
		goto Done;
	}
	
	Done: 
	
		if (root)
			json_object_put(root);
	
		response = json_object_new_object();
		json_object_object_add(response, (char*) "returnValue", json_object_new_boolean(success));
		
		if (!success) {
			json_object_object_add(response,(char*) "errorText",json_object_new_string(const_cast<char*>(errorText.c_str())));
		}
		
		if (!LSMessageReply(lsHandle, message, json_object_to_json_string (response), &lsError )) 	{
		    LSErrorPrint (&lsError, stderr);
		    LSErrorFree(&lsError);
		}

		if (response)
			json_object_put(response);

	return true;
	
}
