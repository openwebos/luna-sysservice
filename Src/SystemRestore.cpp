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


#include <assert.h>
#include <cjson/json.h>
#include <glib.h>
#include <stdio.h>
#include <stdlib.h>

#include "Logging.h"
#include "PrefsDb.h"
#include "PrefsFactory.h"

//place the debug define HERE
 
#include "Utils.h"
#include "SystemRestore.h"
#include "JSONUtils.h"

#include <QtCore/QDebug>
#include <QtCore/QtGlobal> //qWarning
#include <QtCore/QString>
#include <QtGui/QImageReader>

SystemRestore * SystemRestore::s_instance = 0;

SystemRestore * SystemRestore::instance()
{
	if (s_instance == NULL) {
		s_instance = new SystemRestore();
	}
	
	return s_instance;
}

SystemRestore::SystemRestore() : m_msmState(Phone)
{
	s_instance = this;
	std::string overrideStr;
	json_object* root = 0;
	json_object* label = 0;
	std::string keyStr;
	std::string valueStr;
		
	//load the defaults file
	char* jsonStr = Utils::readFile(PrefsDb::s_defaultPrefsFile);
	if (!jsonStr) {
        qWarning() << "Failed to load prefs file:" << PrefsDb::s_defaultPrefsFile;
		goto Platform;
	}

	root = json_tokener_parse(jsonStr);
	if (!root || is_error(root)) {
		root = 0;
        qWarning() << "Failed to parse file contents into json";
		goto Platform;
	}

	label = json_object_object_get(root, "preferences");
	if (!label || is_error(label)) {
        qWarning() << "Failed to get preferences entry from file";
		goto Platform;
	}
	{
	json_object_object_foreach(label, key, val) {

		keyStr = std::string(key);
		if (keyStr == std::string("ringtone"))
			defaultRingtoneString = std::string(json_object_to_json_string(val));
		else if (keyStr == std::string("wallpaper"))
			defaultWallpaperString = std::string(json_object_to_json_string(val));
		
	}
	}
	//load the platform defaults file if it exists
Platform:

	if (jsonStr) {
		delete[] jsonStr;
		jsonStr = 0;
	}
	if (root) {
		json_object_put(root);
		root = 0;
	}
	
	jsonStr = Utils::readFile(PrefsDb::s_defaultPlatformPrefsFile);
	if (!jsonStr) {
        qWarning() << "Failed to load platform prefs file:" << PrefsDb::s_defaultPlatformPrefsFile;
		goto Exit;
	}
		
	root = json_tokener_parse(jsonStr);
	if (!root || is_error(root)) {
		root = 0;
        qWarning() << "Failed to parse file [" << PrefsDb::s_defaultPlatformPrefsFile << "] contents into json";
		goto Exit;
	}

	label = json_object_object_get(root, "preferences");
	if (!label || is_error(label)) {
        qWarning() << "Failed to get preferences entry from file";
		goto Exit;
	}
	
	{
	json_object_object_foreach(label, key, val) {

		keyStr = std::string(key);
		if (keyStr == std::string("ringtone"))
			defaultRingtoneString = std::string(json_object_to_json_string(val));
		else if (keyStr == std::string("wallpaper"))
			defaultWallpaperString = std::string(json_object_to_json_string(val));

	}
	}
	
//	//override if necessary...
//	overrideStr = PrefsDb::instance()->getPref(PrefsDb::s_sysDefaultRingtoneKey);
//	if (overrideStr.length())
//		defaultRingtoneString = overrideStr;
//	overrideStr = PrefsDb::instance()->getPref(PrefsDb::s_sysDefaultWallpaperKey);
//	if (overrideStr.length())
//		defaultWallpaperString = overrideStr;
	
	if (defaultRingtoneString.size())
		PrefsDb::instance()->setPref(PrefsDb::s_sysDefaultRingtoneKey,defaultRingtoneString);
	else {
		overrideStr = PrefsDb::instance()->getPref(PrefsDb::s_sysDefaultRingtoneKey);
		if (overrideStr.size())
			defaultRingtoneString = overrideStr;
	}
		
	if (defaultWallpaperString.size())
		PrefsDb::instance()->setPref(PrefsDb::s_sysDefaultWallpaperKey,defaultWallpaperString);
	else {
		overrideStr = PrefsDb::instance()->getPref(PrefsDb::s_sysDefaultWallpaperKey);
		if (overrideStr.size())
			defaultWallpaperString = overrideStr;
	}
	
	Exit:
	
	if (jsonStr)
		delete[] jsonStr;
	if (root)
		json_object_put(root);
	
}

/* COMMENT: No gio-2.0 on device! blah...look for slow, alternate function below this one...
//static
int SystemRestore::fileCopy(const char * srcFileAndPath,const char * dstFileAndPath)
{
	if ((srcFileAndPath == NULL) || (dstFileAndPath == NULL))
		return -1;
	GFile * src = g_file_new_for_path(srcFileAndPath);
	GFile * dst = g_file_new_for_path(dstFileAndPath);

	GError * err=NULL;
	gboolean rc = g_file_copy(
			src,
			dst,
			G_FILE_COPY_OVERWRITE,
			NULL,
			NULL,
			NULL,
			&err
	);
	
	g_object_unref(src);
	g_object_unref(dst);
	
	if (rc == false) {
        qWarning("file copy error %d: [%s]",err->code,err->message);
		g_error_free(err);
		return -1;
	}
	
	//make sure there is no error struct created, and if so free it
	if (err)
		g_error_free(err);
	
	return 1;
}
*/

int SystemRestore::restoreDefaultRingtoneToMediaPartition() 
{
	//check the file specified by defaultRingtoneFileAndPath
	if (!Utils::doesExistOnFilesystem(defaultRingtoneFileAndPath.c_str())) {
		return -1;
	}
	
	//copy it to the media partition
	
	std::string pathPart ="";
	std::string filePart ="";
	Utils::splitFileAndPath(defaultRingtoneFileAndPath,pathPart,filePart);
	if (filePart.length() == 0)
	{
        qWarning() << "filepart.length == 0," << filePart.c_str();
		return -1;
	}
	std::string targetFileAndPath = std::string(PrefsDb::s_mediaPartitionPath)+std::string(PrefsDb::s_mediaPartitionRingtonesDir)+std::string("/")+filePart;
	int rc = Utils::fileCopy(defaultRingtoneFileAndPath.c_str(),targetFileAndPath.c_str());
	if (rc == -1)
	{
        qWarning() << "filecopy" << defaultRingtoneFileAndPath.c_str() << "->" << targetFileAndPath.c_str() << "failed";
		return -1;
	}
	return 1;
}
int SystemRestore::restoreDefaultWallpaperToMediaPartition()
{
	//check the file specified by defaultWallpaperFileAndPath
	if (!Utils::doesExistOnFilesystem(defaultWallpaperFileAndPath.c_str()) ) {
        qWarning() << "file" << defaultWallpaperFileAndPath.c_str() << "doesn\'t exist";
		return -1;
	}

	//copy it to the media partition
	
	std::string pathPart ="";
	std::string filePart ="";
	Utils::splitFileAndPath(defaultWallpaperFileAndPath,pathPart,filePart);
	if (filePart.length() == 0)
	{
        qWarning() << "filepart.length == 0," << filePart.c_str();
		return -1;
	}
		
	std::string targetFileAndPath = std::string(PrefsDb::s_mediaPartitionPath)+std::string(PrefsDb::s_mediaPartitionWallpapersDir)+std::string("/")+filePart;
	int rc = Utils::fileCopy(defaultWallpaperFileAndPath.c_str(),targetFileAndPath.c_str());
	if (rc == -1)
	{
        qWarning() << "filecopy" << defaultWallpaperFileAndPath.c_str() << "->" << targetFileAndPath.c_str() << "failed";
		return -1;
	}

	return 1;
}

int SystemRestore::restoreDefaultRingtoneSetting()
{
	//parse json in the defaultRingtoneString
	
	int rc=0;
	json_object* root = 0;
	json_object* label = 0;
	std::string keyStr;
	std::string valueStr;
		
	root = json_tokener_parse(defaultRingtoneString.c_str());
	if (!root || is_error(root)) {
        qWarning() << "Failed to parse string into json";
		root = 0;
		goto Exit;
	}
	
	label = json_object_object_get(root, "fullPath");
	if (!label || is_error(label)) {
        qWarning() << "Failed to parse ringtone details";
		goto Exit;
	}

	defaultRingtoneFileAndPath = json_object_get_string(label);
	
	//restore the default ringtone files to the media partition
	rc = restoreDefaultRingtoneToMediaPartition();
	if (rc == -1) {
		rc = 0;
		goto Exit;		//error of some kind
	}
	//set the key into the database...remember, at this point the handlers are *NOT* up yet, so have to do it manually
	PrefsDb::instance()->setPref(std::string("ringtone"),defaultRingtoneString);
	
	rc = 1;
	
	Exit:
	
	if (root)
		json_object_put(root);
	
	return rc;
}

int SystemRestore::restoreDefaultWallpaperSetting()
{
	//parse json in the defaultWallpaperString

	int rc=0;
	json_object* root = 0;
	json_object* label = 0;
	std::string keyStr;
	std::string valueStr;

	root = json_tokener_parse(defaultWallpaperString.c_str());
	if (!root || is_error(root)) {
        qWarning() << "Failed to parse string into json";
		root = 0;
		goto Exit;
	}

	label = json_object_object_get(root, "wallpaperFile");
	if (!label || is_error(label)) {
        qWarning() << "Failed to parse wallpaper details";
		goto Exit;
	}

	//TODO: use this to cache for later so I don't have to reparse json each time
	defaultWallpaperFileAndPath = json_object_get_string(label);

	//restore the default wallpaper file to the media partition
	rc = restoreDefaultWallpaperToMediaPartition();
	if (rc == -1) {
        qWarning() << "SystemRestore::restoreDefaultWallpaperSetting(): [ERROR] could not copy default wallpaper [" <<
                defaultWallpaperFileAndPath.c_str() << "] to media partition";
		rc=0;
		goto Exit;		//error of some kind
	}
	//set the key into the database...remember, at this point the handlers are *NOT* up yet, so have to do it manually
	PrefsDb::instance()->setPref(std::string("wallpaper"),defaultWallpaperString);
	
	rc=1;
	
	Exit:
	
	if (root)
		json_object_put(root);
	
	return rc;
}

bool SystemRestore::isRingtoneSettingConsistent() 
{
	std::string ringToneRawPref = PrefsDb::instance()->getPref("ringtone");
	std::string ringToneFileAndPath;
	
	bool rc=false;
	json_object* root = 0;
	json_object* label = 0;
	std::string keyStr;
	std::string valueStr;
		
	if (ringToneRawPref.length() == 0)
		goto Exit;
	
	//parse the setting
	root = json_tokener_parse(ringToneRawPref.c_str());
	if (!root || is_error(root)) {
        qWarning() << "Failed to parse string into json";
		root = 0;
		goto Exit;
	}

	label = json_object_object_get(root, "fullPath");
	if (!label || is_error(label)) {
        qWarning() << "Failed to parse ringtone details";
		goto Exit;
	}

	ringToneFileAndPath = json_object_get_string(label);
	
    __qMessage("checking [%s]...",ringToneFileAndPath.c_str());
	//check to see if file exists
	if (Utils::doesExistOnFilesystem(ringToneFileAndPath.c_str())) {
		if (Utils::filesizeOnFilesystem(ringToneFileAndPath.c_str()) > 0)			//TODO: a better check for corruption; see wallpaper consist. checking
			rc = true;
		else
            qWarning() << "file size is 0; corrupt file";
	}
	else {
        qWarning() << "Sound file is not on filesystem";
	}
	Exit:

	if (root)
		json_object_put(root);

	return rc;

}


bool SystemRestore::isWallpaperSettingConsistent()
{
	std::string wallpaperRawPref = PrefsDb::instance()->getPref("wallpaper");
	std::string wallpaperFileAndPath;
	
	bool rc=false;
	json_object* root = 0;
	json_object* label = 0;
	
	if (wallpaperRawPref.length() == 0)
		goto Exit;

	//parse the setting
	root = json_tokener_parse(wallpaperRawPref.c_str());
	if (!root || is_error(root)) {
        qWarning() << "Failed to parse string into json";
		root = 0;
		goto Exit;
	}

	label = json_object_object_get(root, "wallpaperFile");
	if (!label || is_error(label)) {
        qWarning() << "Failed to parse wallpaper details";
		goto Exit;
	}

	wallpaperFileAndPath = json_object_get_string(label);

    __qMessage("checking [%s]...",wallpaperFileAndPath.c_str());
	//check to see if file exists

    {
        QImageReader reader(QString::fromStdString(wallpaperFileAndPath));
        if (reader.canRead())
            rc = true;
        else
            qWarning() <<reader.errorString()<<reader.fileName();
    }
	Exit:

	if (root)
		json_object_put(root);

	return rc;
		
}

void SystemRestore::refreshDefaultSettings()
{
	json_object* root = 0;
	json_object* label = 0;
	std::string wallpaperRawDefaultPref;
	std::string ringtoneRawDefaultPref;

	wallpaperRawDefaultPref = PrefsDb::instance()->getPref(PrefsDb::s_sysDefaultWallpaperKey);
	
	if (wallpaperRawDefaultPref.length() == 0)
		goto Exit;

	//parse the setting
	root = json_tokener_parse(wallpaperRawDefaultPref.c_str());
	if (!root || is_error(root)) {
        qWarning() << "Failed to parse string into json";
		root = 0;
		goto Stage2;
	}

	label = json_object_object_get(root, "wallpaperFile");
	if (!label || is_error(label)) {
        qWarning() << "Failed to parse wallpaper details";
		goto Stage2;
	}

	defaultWallpaperString = wallpaperRawDefaultPref;
	defaultWallpaperFileAndPath = json_object_get_string(label);

	Stage2:

	if (root) {
		json_object_put(root);
		root = 0;
	}

	ringtoneRawDefaultPref = PrefsDb::instance()->getPref(PrefsDb::s_sysDefaultRingtoneKey);

	if (ringtoneRawDefaultPref.length() == 0)
		goto Exit;

	//parse the setting
	root = json_tokener_parse(ringtoneRawDefaultPref.c_str());
	if (!root || is_error(root)) {
        qWarning() << "Failed to parse string into json";
		root = 0;
		goto Exit;
	}

	label = json_object_object_get(root, "fullPath");
	if (!label || is_error(label)) {
        qWarning() << "Failed to parse ringtone details";
		goto Exit;
	}

	defaultRingtoneString = ringtoneRawDefaultPref;
	defaultRingtoneFileAndPath = json_object_get_string(label);

	Exit:

	if ((root) && !is_error(root)) {
		json_object_put(root);
	}
}

int SystemRestore::createSpecialDirectories()
{

	//make sure the prefs folder exists
	(void)  g_mkdir_with_parents(PrefsDb::s_prefsPath, 0755);

	//make sure the ringtones folder exists.
	std::string path = std::string(PrefsDb::s_mediaPartitionPath)+std::string(PrefsDb::s_mediaPartitionRingtonesDir);
	(void) g_mkdir_with_parents(path.c_str(), 0755);

	//make sure the wallpapers folder exists
	path = std::string(PrefsDb::s_mediaPartitionPath)+std::string(PrefsDb::s_mediaPartitionWallpapersDir);
	(void) g_mkdir_with_parents(path.c_str(), 0755);

	//make sure the wallpapers thumbnail folder exists
	path = std::string(PrefsDb::s_mediaPartitionPath)+std::string(PrefsDb::s_mediaPartitionWallpaperThumbsDir);
	(void) g_mkdir_with_parents(path.c_str(), 0755);

//#if defined (__arm__)
//	std::string cmdline = std::string("mattrib -i /dev/mapper/store-media +h ::")+std::string(PrefsDb::s_mediaPartitionWallpapersDir);
//	system(cmdline.c_str());
//#endif

	//make sure the systemservice special folder exists
	path = std::string(PrefsDb::s_mediaPartitionPath)+std::string(PrefsDb::s_sysserviceDir);
	(void) g_mkdir_with_parents(path.c_str(), 0755);

	//make sure the systemservice temp folder exists
	path = std::string(PrefsDb::s_mediaPartitionPath)+std::string(PrefsDb::s_mediaPartitionTempDir);
	(void) g_mkdir_with_parents(path.c_str(), 0755);
	
//#if defined (__arm__)
//	cmdline = std::string("mattrib -i /dev/mapper/store-media +h ::")+std::string(PrefsDb::s_sysserviceDir);
//	system(cmdline.c_str());
//#endif

		
	return 1;
}

int SystemRestore::startupConsistencyCheck() 
{

    __qMessage("started");
	// -- run startup tests to determine the state of the device

	if (Utils::doesExistOnFilesystem(PrefsDb::s_systemTokenFileAndPath) == false) {
		//the media partition has been reformatted or damaged
        qWarning() << "running - system token missing; media was erased/damaged";
		//run restore

		int rc=0;		//avoid having tons of if's ...so don't mess with the return values of the restore__ functions
		rc += SystemRestore::instance()->restoreDefaultRingtoneSetting();
		rc += SystemRestore::instance()->restoreDefaultWallpaperSetting();

		//create token if all these succeeded
		if (rc == 2) {
			FILE * fp = fopen(PrefsDb::s_systemTokenFileAndPath,"w");
			if (fp != NULL) {
				fprintf(fp,"%lu",time(NULL));		//doesn't matter what I put in here, but timestamp seems sane
				fflush(fp);
				fclose(fp);
			}
		}
		else {
            qWarning() << "running - system token missing and WAS NOT written because one of the restore functions failed!";
		}
	}
	else {
		
        __qMessage("running - checking wallpaper and ringtone consistency");
		//check consistency of wallpaper setting
		if (!SystemRestore::instance()->isWallpaperSettingConsistent()) {
			//run restore on wallpaper
			SystemRestore::instance()->restoreDefaultWallpaperSetting();
		}
		//check consistency of ringtone setting
		if (!SystemRestore::instance()->isRingtoneSettingConsistent()) {
			SystemRestore::instance()->restoreDefaultRingtoneSetting();
		}
	}

	//check the media icon file
	if (Utils::filesizeOnFilesystem(PrefsDb::s_volumeIconFileAndPathDest) == 0) {
        __qMessage("running - restoring volume icon file");
		//restore it
		Utils::fileCopy(PrefsDb::s_volumeIconFileAndPathSrc,PrefsDb::s_volumeIconFileAndPathDest);
	}

//	//attrib it all for good measure
//#if defined (__arm__)
//	int exitCode;
//	std::string cmdline = std::string("mattrib -i /dev/mapper/store-media +h ::")+std::string(PrefsDb::s_mediaPartitionWallpapersDir);
//	exitCode = system(cmdline.c_str());
//	g_warning("SystemRestore::startupConsistencyCheck() running - [%s] returned %d",cmdline.c_str(),exitCode);
//	cmdline = std::string("mattrib -i /dev/mapper/store-media +h ::")+std::string(PrefsDb::s_sysserviceDir);
//	exitCode = system(cmdline.c_str());
//	g_warning("SystemRestore::startupConsistencyCheck() running - [%s] returned %d",cmdline.c_str(),exitCode);
//	cmdline = std::string("mattrib -i /dev/mapper/store-media +h ::")+std::string(PrefsDb::s_volumeIconFile);
//	exitCode = system(cmdline.c_str());
//	g_warning("SystemRestore::startupConsistencyCheck() running - [%s] returned %d",cmdline.c_str(),exitCode);
//#endif

    __qMessage("finished");
	return 1;
}

//static
int SystemRestore::runtimeConsistencyCheck() 
{
    __qMessage("started");
	
	PrefsFactory::instance()->runConsistencyChecksOnAllHandlers();
	
	//check the media icon file
	if (Utils::filesizeOnFilesystem(PrefsDb::s_volumeIconFileAndPathDest) == 0) {
        __qMessage("running - restoring volume icon file");
		//restore it
		Utils::fileCopy(PrefsDb::s_volumeIconFileAndPathSrc,PrefsDb::s_volumeIconFileAndPathDest);
	}

	//attrib it all for good measure
//#if defined (__arm__)
//	int exitCode;
//	std::string cmdline = std::string("mattrib -i /dev/mapper/store-media +h ::")+std::string(PrefsDb::s_mediaPartitionWallpapersDir);
//	exitCode = system(cmdline.c_str());
//	g_warning("SystemRestore::runtimeConsistencyCheck() running - [%s] returned %d",cmdline.c_str(),exitCode);
//	cmdline = std::string("mattrib -i /dev/mapper/store-media +h ::")+std::string(PrefsDb::s_sysserviceDir);
//	exitCode = system(cmdline.c_str());
//	g_warning("SystemRestore::runtimeConsistencyCheck() running - [%s] returned %d",cmdline.c_str(),exitCode);
//	cmdline = std::string("mattrib -i /dev/mapper/store-media +h ::")+std::string(PrefsDb::s_volumeIconFile);
//	exitCode = system(cmdline.c_str());
//	g_warning("SystemRestore::runtimeConsistencyCheck() running - [%s] returned %d",cmdline.c_str(),exitCode);
//#endif

    __qMessage("finished");
	return 1;
}

bool SystemRestore::msmAvailCallback(LSHandle* handle, LSMessage* message, void* ctxt)
{
    if (LSMessageIsHubErrorMessage(message)) {  // returns false if message is NULL
        qWarning("The message received is an error message from the hub");
        return true;
    }
	return SystemRestore::instance()->msmAvail(message);
}

bool SystemRestore::msmProgressCallback(LSHandle* handle, LSMessage* message, void* ctxt)
{
    if (LSMessageIsHubErrorMessage(message)) {  // returns false if message is NULL
        qWarning("The message received is an error message from the hub");
        return true;
    }
	return SystemRestore::instance()->msmProgress(message);
}

bool SystemRestore::msmEntryCallback(LSHandle* handle, LSMessage* message, void* ctxt)
{
    if (LSMessageIsHubErrorMessage(message)) {  // returns false if message is NULL
        qWarning("The message received is an error message from the hub");
        return true;
    }
    return SystemRestore::instance()->msmEntry(message);
}

bool SystemRestore::msmFsckingCallback(LSHandle* handle, LSMessage* message, void* ctxt)
{
    if (LSMessageIsHubErrorMessage(message)) {  // returns false if message is NULL
        qWarning("The message received is an error message from the hub");
        return true;
    }
    return SystemRestore::instance()->msmFscking(message);
}

bool SystemRestore::msmPartitionAvailCallback(LSHandle* handle, LSMessage* message, void* ctxt)
{
    if (LSMessageIsHubErrorMessage(message)) {  // returns false if message is NULL
        qWarning("The message received is an error message from the hub");
        return true;
    }
    return SystemRestore::instance()->msmPartitionAvailable(message);
}


bool SystemRestore::msmAvail(LSMessage* message)
{
    // {"mode-avail": boolean}
    VALIDATE_SCHEMA_AND_RETURN(0,
                               message,
                               SCHEMA_1(REQUIRED(mode-avail, boolean)));

	const char* str = LSMessageGetPayload( message );
	if( !str )
		return false;
	
	struct json_object* payload = json_tokener_parse(str);
	if (is_error(payload))
		return false;

	struct json_object* modeAvail = json_object_object_get(payload, "mode-avail");
	if (!modeAvail) {
		json_object_put(payload);
		return false;
	}

	bool available = json_object_get_boolean(modeAvail);
    __qMessage("msmAvail(): MSM available: %s",( available == TRUE) ? "true" : "false");

	//attrib it all for good measure  ... necessary because attrib-ing at boot doesn't always work, storaged sometimes lies about partition available
	//			...so try it again right before the user can go into storage mode and see the hidden files anyways
//#if defined (__arm__)
//	if (available) {
//		int exitCode;
//		std::string cmdline = std::string("mattrib -i /dev/mapper/store-media +h ::")+std::string(PrefsDb::s_mediaPartitionWallpapersDir);
//		exitCode = system(cmdline.c_str());
//		g_warning("SystemRestore::msmAvail() running - [%s] returned %d",cmdline.c_str(),exitCode);
//		cmdline = std::string("mattrib -i /dev/mapper/store-media +h ::")+std::string(PrefsDb::s_sysserviceDir);
//		exitCode = system(cmdline.c_str());
//		g_warning("SystemRestore::msmAvail() running - [%s] returned %d",cmdline.c_str(),exitCode);
//		cmdline = std::string("mattrib -i /dev/mapper/store-media +h ::")+std::string(PrefsDb::s_volumeIconFile);
//		exitCode = system(cmdline.c_str());
//		g_warning("SystemRestore::msmAvail() running - [%s] returned %d",cmdline.c_str(),exitCode);
//	}
//#endif
	
	json_object_put(payload);

	return true;
}

bool SystemRestore::msmProgress(LSMessage* message)
{
    // {"stage": string}
    VALIDATE_SCHEMA_AND_RETURN(0,
                               message,
                               SCHEMA_1(REQUIRED(stage, string)));

	const char* str = LSMessageGetPayload( message );
	if( !str )
		return false;
	
	struct json_object* payload = json_tokener_parse(str);
	if (is_error(payload))
		return false;

	struct json_object* stage = json_object_object_get(payload, "stage");
	if (!stage ) {
		json_object_put(payload);
		return false;
	}

    __qMessage("msmProgress(): MSM stage: [%s]", json_object_get_string(stage));
	
	json_object_put( payload );
	
	return true;
}

bool SystemRestore::msmEntry(LSMessage* message)
{
    // {"new-mode": string}
    VALIDATE_SCHEMA_AND_RETURN(0,
                               message,
                               SCHEMA_1(REQUIRED(new-mode, string)));

	const char* str = LSMessageGetPayload( message );
	if( !str )
		return false;
	
	struct json_object* payload = json_tokener_parse(str);
	if (is_error(payload))
		return false;

	std::string modeStr("UNKNOWN");
	struct json_object* mode = json_object_object_get(payload, "new-mode");
	if (mode && (!is_error(mode))) {
		modeStr = std::string(json_object_get_string(mode));
		if (modeStr == "brick") {
			m_msmState = Brick;
		}
		else { 		//TODO: proper handling for all cases; for now, assume phone whenever not brick
			m_msmState = Phone;
		}
	}		

    __qMessage("msmEntry(): MSM mode: [%s]",modeStr.c_str());
	
	json_object_put(payload );

	return true;    
}

bool SystemRestore::msmFscking(LSMessage* message)
{
    __qMessage("msmFscking()");
	return true;
}

bool SystemRestore::msmPartitionAvailable(LSMessage* message) 
{
    // {"mount_point": string, "available": boolean}
    VALIDATE_SCHEMA_AND_RETURN(0,
                               message,
                               SCHEMA_2(REQUIRED(mount_point, string), REQUIRED(available, boolean)));

	std::string mountPoint;
	bool available=false;
    __qMessage("msmPartitionAvailable(): signaled");
	
	const char* str = LSMessageGetPayload( message );
	if( !str )
		return false;

	struct json_object* payload = json_tokener_parse(str);
	if (is_error(payload))
		return false;

	struct json_object* label = json_object_object_get(payload, "mount_point");
	if (!label ) {
		mountPoint = std::string("UNKNOWN");
	}		
	else
		mountPoint = std::string(json_object_get_string(label));
	
	label = json_object_object_get(payload, "available");
	if (label && (!is_error(label)))
		available = json_object_get_boolean(label);
	
    __qMessage("msmPartitionAvailable(): mount point: [%s] , available: %s",mountPoint.c_str(),(available ? "true" : "false"));
			
	if (available && (mountPoint == "/media/internal")) {
		SystemRestore::createSpecialDirectories();
		SystemRestore::runtimeConsistencyCheck();
	}
	
	json_object_put(payload );

	return true;    

}

