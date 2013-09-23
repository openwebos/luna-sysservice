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


#include <sys/prctl.h>
#include <string>

//for basename()...
#include <string.h>
#include <map>
#include "BackupManager.h"
#include <cjson/json.h>
#include "PrefsDb.h"
#include "PrefsFactory.h"

#include "Logging.h"
#include "Utils.h"
#include "Settings.h"
#include "JSONUtils.h"

/* BackupManager implementation is based on the API documented at https://wiki.palm.com/display/ServicesEngineering/Backup+and+Restore+2.0+API
 * Backs up the systemprefs database
 */
BackupManager* BackupManager::s_instance = NULL;

std::string BackupManager::s_backupKeylistFilename = "/etc/palm/sysservice-backupkeys.json";

/*!
 * \page com_palm_systemservice_backup Service API com.palm.systemservice/backup/
 *
 * Public methods:
 *
 * - \ref com_palm_systemservice_pre_backup
 * - \ref com_palm_systemservice_post_restore
 */

/**
 * These are the methods that the backup service can call when it's doing a 
 * backup or restore.
 */
LSMethod BackupManager::s_BackupServerMethods[]  = {
	{ "preBackup"  , BackupManager::preBackupCallback },
	{ "postRestore", BackupManager::postRestoreCallback },
    { 0, 0 }
};


BackupManager::BackupManager()
: m_doBackupFiles(true)
, m_service(0)
, m_p_backupDb(0)
{
}

/**
 * Initialize the backup manager.
 */
bool BackupManager::init()
{
    return true;
}

void BackupManager::setServiceHandle(LSPalmService* service)
{
	m_service = service;

	bool result;
	LSError lsError;
	LSErrorInit(&lsError);

	result = LSPalmServiceRegisterCategory( m_service, "/backup", s_BackupServerMethods, NULL,
			NULL, this, &lsError);
	if (!result) {
        //g_critical("%s: Failed to register backup methods",__FUNCTION__);
        qCritical() << "Failed to register backup methods";
		LSErrorFree(&lsError);
		return;
	}

}

BackupManager::~BackupManager()
{
	if (m_p_backupDb)
	{
		delete m_p_backupDb;
	}
}

void BackupManager::copyKeysToBackupDb()
{
	if (!m_p_backupDb)
		return;
	//open the backup keys list to figure out what to copy
	json_object * backupKeysJson = json_object_from_file((char *)(BackupManager::s_backupKeylistFilename.c_str()));
	if (!backupKeysJson || is_error(backupKeysJson))
		return;
	//iterate over all the keys
	std::list<std::string> keylist;
	array_list* fileArray = json_object_get_array(backupKeysJson);
	if (!fileArray || is_error(fileArray))
	{
        //g_warning ("%s: file does not contain an array of string keys",__FUNCTION__);
        qWarning () << "file does not contain an array of string keys";
		return;
	}


	int fileArrayLength = array_list_length (fileArray);
	int index = 0;

    //g_message("%s: fileArrayLength = %d",__FUNCTION__, fileArrayLength);
    __qMessage("fileArrayLength = %d", fileArrayLength);

	for (index = 0; index < fileArrayLength; ++index)
	{
		json_object* obj = (json_object*) array_list_get_idx (fileArray, index);
		if ((!obj) || is_error(obj))
		{
            //g_warning("%s: array object [%d] isn't valid (skipping)",__FUNCTION__,index);
            qWarning() << "array object [" << index << "] isn't valid (skipping)";
			continue;
		}

		const char * ckey = json_object_get_string(obj);
		std::string key = ( ckey ? ckey : "");
        //g_message("%s: array[%d] file: %s",__FUNCTION__,index,key.c_str());
        __qMessage("array[%d] file: %s",index,key.c_str());

		if (key.empty())
		{
            //g_warning("%s: array object [%d] is a key that is empty (skipping)",__FUNCTION__,index);
            qWarning() << "array object [" << index << "] is a key that is empty (skipping)";
			continue;
		}
		keylist.push_back(key);
	}
	m_p_backupDb->copyKeys(PrefsDb::instance(),keylist);
	json_object_put(backupKeysJson);

}

void BackupManager::initFilesForBackup(bool useFilenameWithoutPath)
{
	if (m_p_backupDb)
	{
		if (g_file_test(m_p_backupDb->databaseFile().c_str(), G_FILE_TEST_EXISTS))
		{
			if (useFilenameWithoutPath)
			{
				m_backupFiles.push_back(m_p_backupDb->m_dbFilename.c_str());
			}
			else
			{
				const char * cstr = basename(m_p_backupDb->databaseFile().c_str());
				std::string filename = ( cstr ? std::string(cstr) : std::string(cstr));
				if (filename.find("/") != std::string::npos)
					filename = std::string("");			///all for safety
				m_backupFiles.push_back(filename);
			}

			if (Settings::settings()->m_saveLastBackedUpTempDb)
			{
				Utils::fileCopy(m_p_backupDb->databaseFile().c_str(),
						(std::string(PrefsDb::s_mediaPartitionPath)+std::string(PrefsDb::s_sysserviceDir)+std::string("/lastBackedUpTempDb.db")).c_str());
			}
		}
	}
}

BackupManager* BackupManager::instance()
{
	if (NULL == s_instance) {
		s_instance = new BackupManager();
	}

	return s_instance;
}

/*! \page com_palm_systemservice_backup
\n
\section com_palm_systemservice_pre_backup preBackup

\e Public.

com.palm.systemservice/backup/preBackup

Make a backup of LunaSysService preferences.

\subsection com_palm_systemservice_pre_backup_syntax Syntax:
\code
{
    "incrementalKey": object,
    "maxTempBytes":   int,
    "tempDir":        string
}
\endcode

\param incrementalKey This is used primarily for mojodb, backup service will handle other incremental backups.
\param maxTempBytes The allowed size of upload, currently 10MB (more than enough for our backups).
\param tempDir Directory to store temporarily generated files.

\subsection com_palm_systemservice_pre_backup_returns Returns:
\code
{
    "description": string,
    "version": string,
    "files": string array
}
\endcode

\param description Describes the backup.
\param version Version of the backup.
\param files List of files included in the backup.

\subsection com_palm_systemservice_pre_backup_examples Examples:
\code
luna-send -n 1 -f luna://com.palm.systemservice/backup/preBackup '{}'
\endcode

Example response for a succesful call:
\code
{
    "description": "Backup of LunaSysService, containing the systemprefs sqlite3 database",
    "version": "1.0",
    "files": [
        "\/var\/luna\/preferences\/systemprefs_backup.db"
    ]
}
\endcode
*/
/**
 * Called by the backup service for all four of our callback functions: preBackup, 
 * postBackup, preRestore, postRestore.
 */
bool BackupManager::preBackupCallback( LSHandle* lshandle, LSMessage *message, void *user_data)
{
    //g_message ("%s: starting",__FUNCTION__);
    __qMessage ("starting");
    if (LSMessageIsHubErrorMessage(message)) {  // returns false if message is NULL
        qWarning("The message received is an error message from the hub");
        return true;
    }
    BackupManager* pThis = static_cast<BackupManager*>(user_data);
    if (pThis == NULL)
    {
        //g_warning("%s: LScallback didn't preserve user_data ptr! (returning false)",__FUNCTION__);
        qWarning() << "LScallback didn't preserve user_data ptr! (returning false)";
    	return false;
    }

	// payload is expected to have the following fields -
	// incrementalKey - this is used primarily for mojodb, backup service will handle other incremental backups
	// maxTempBytes - this is the allowed size of upload, currently 10MB (more than enough for our backups)
	// tempDir - directory to store temporarily generated files

    // {"tempDir": string}
    VALIDATE_SCHEMA_AND_RETURN(lshandle,
                               message,
                               SCHEMA_1(REQUIRED(tempDir, string)));

    //grab the temp dir
    const char* str = LSMessageGetPayload(message);
    if (!str)
    {
        //g_warning("%s: LScallback didn't have any text in the payload! (returning false)",__FUNCTION__);
        qWarning() << "LScallback didn't have any text in the payload! (returning false)";
    	return false;
    }
    //g_message("%s: received %s", __FUNCTION__, str);
    __qMessage("received %s", str);
    json_object* root = json_tokener_parse(str);
    if (!root || is_error(root))
    {
        //g_warning("%s: text payload didn't contain valid json [message was: [%s] ]",__FUNCTION__,str);
        qWarning() << "text payload didn't contain valid json message, was: [" << str << "]";
    	return false;
    }
    json_object* tempDirLabel = json_object_object_get (root, "tempDir");
    std::string tempDir;
    bool myTmp = false;
    if ((!tempDirLabel) || is_error(tempDirLabel))
    {
        //g_warning ("%s: No tempDir specified in preBackup message",__FUNCTION__);
        qWarning () << "No tempDir specified in preBackup message";
    	tempDir = PrefsDb::s_prefsPath;
    	myTmp = true;
    }
    else
    {
    	const char * ctemp = json_object_get_string(tempDirLabel);
    	tempDir = (ctemp ? ctemp : "");
    }

    if (pThis->m_p_backupDb)
    {
    	delete pThis->m_p_backupDb;		//stale
    	pThis->m_p_backupDb = 0;
    }

    //try and create it
    std::string dbfile = ( (tempDir.at(tempDir.length()-1) == '/') ? (tempDir+PrefsDb::s_tempBackupDbFilenameOnly) : (tempDir+std::string("/")+PrefsDb::s_tempBackupDbFilenameOnly));
    pThis->m_p_backupDb = PrefsDb::createStandalone(dbfile);
    if (!pThis->m_p_backupDb)
    {
    	//failed to create temp db
        //g_warning("%s: unable to create a temporary backup db at [%s]...aborting!",__FUNCTION__,dbfile.c_str());
        qWarning() << "unable to create a temporary backup db at [" << dbfile.c_str() << "]...aborting!";
    	return pThis->sendPreBackupResponse(lshandle,message,std::list<std::string>());
    }

    // Attempt to copy relevant keys into the temporary backup database
    pThis->copyKeysToBackupDb();
	// adding the files for backup at the time of request.
	pThis->initFilesForBackup(myTmp);

	if (!(pThis->m_doBackupFiles))
	{
        //g_warning("%s: opted not to do a backup at this time due to doBackup internal var",__FUNCTION__);
        qWarning() << "opted not to do a backup at this time due to doBackup internal var";
		return (pThis->sendPreBackupResponse(lshandle,message,std::list<std::string>()));
	}

	return (pThis->sendPreBackupResponse(lshandle,message,pThis->m_backupFiles));
}

/*! \page com_palm_systemservice_backup
\n
\section com_palm_systemservice_post_restore postRestore

\e Public.

com.palm.systemservice/backup/postRestore

Restore a LunaSysService backup.

\subsection com_palm_systemservice_post_restore_syntax Syntax:
\code
{
    "tempDir": string,
    "files":   string array
}
\endcode

\param tempDir Directory to store temporarily generated files. Required.
\param files List of files to restore. Required.

\subsection com_palm_systemservice_post_restore_returns Returns:
\code
{
    "returnValue": boolean
}
\endcode

\param returnValue Indicates if the call was succesful.

\subsection com_palm_systemservice_post_restore_examples Examples:
\code
luna-send -n 1 -f luna://com.palm.systemservice/backup/postRestore '{"tmpDir": "/tmp/", "files": ["/var/luna/preferences/systemprefs_backup.db"] }'
\endcode

Example response for a succesful call:
\code
{
    "returnValue": true
}
\endcode
*/
bool BackupManager::postRestoreCallback( LSHandle* lshandle, LSMessage *message, void *user_data)
{
        LSError lserror;
        LSErrorInit(&lserror);
        json_object* response = json_object_new_object();
    // {"tempDir": string, "files": array}
    VALIDATE_SCHEMA_AND_RETURN(lshandle,
                               message,
                               SCHEMA_2(REQUIRED(tempDir, string), REQUIRED(files, array)));

	BackupManager* pThis = static_cast<BackupManager*>(user_data);
	if (pThis == NULL)
	{
        //g_warning("%s: LScallback didn't preserve user_data ptr! (returning false)",__FUNCTION__);
        qWarning() << "LScallback didn't preserve user_data ptr! (returning false)";
		return false;
	}
    const char* str = LSMessageGetPayload(message);
    if (!str)
    {
        //g_warning("%s: LScallback didn't have any text in the payload! (returning false)",__FUNCTION__);
        qWarning() << "LScallback didn't have any text in the payload! (returning false)";
        json_object_object_add (response, "returnValue", json_object_new_boolean(false));
        json_object_object_add (response, "errorText", json_object_new_string("Required Arguments Missing."));
        if (!LSMessageReply (lshandle, message, json_object_to_json_string(response), &lserror )) {
                //g_warning("Can't send reply to postRestoreCallback error: %s", lserror.message);
                qWarning() << "Can\'t send reply to postRestoreCallback error:" << lserror.message;
                LSErrorFree (&lserror);
        }

        json_object_put (response);
        return true;
    }

    json_object* root = json_tokener_parse(str);
    if (!root || is_error(root))
    {
        //g_warning("%s: text payload didn't contain valid json [message was: [%s] ]",__FUNCTION__,str);
        qWarning() << "text payload didn't contain valid json [message was: [" << str << "] ]";
        json_object_object_add (response, "returnValue", json_object_new_boolean(false));
        json_object_object_add (response, "errorText", json_object_new_string("Required Arguments Missing"));

        //g_message ("Sending response to postRestoreCallback: %s", json_object_to_json_string (response));
        __qMessage ("Sending response to postRestoreCallback: %s", json_object_to_json_string (response));
        if (!LSMessageReply (lshandle, message, json_object_to_json_string(response), &lserror )) {
                //g_warning("Can't send reply to postRestoreCallback error: %s", lserror.message);
                qWarning() << "Can't send reply to postRestoreCallback error:" <<lserror.message;
                LSErrorFree (&lserror);
        }

        json_object_put (response);
        return true;
    }

    json_object* tempDirLabel = json_object_object_get (root, "tempDir");
    std::string tempDir;
    if ((!tempDirLabel) || is_error(tempDirLabel))
    {
        //g_warning ("%s: No tempDir specified in postRestore message",__FUNCTION__);
        qWarning () << "No tempDir specified in postRestore message";
        tempDir = "";		//try and ignore it...hopefully all the files will have abs. paths
        json_object_object_add (response, "returnValue", json_object_new_boolean(false));
        json_object_object_add (response, "errorText", json_object_new_string("invalid arguments"));

        //g_message ("Sending response to postRestoreCallback: %s", json_object_to_json_string (response));
        __qMessage ("Sending response to postRestoreCallback: %s", json_object_to_json_string (response));
        if (!LSMessageReply (lshandle, message, json_object_to_json_string(response), &lserror )) {
                //g_warning("Can't send reply to postRestoreCallback error: %s", lserror.message);
                qWarning() << "Can't send reply to postRestoreCallback error:" << lserror.message;
                LSErrorFree (&lserror);
        }


        json_object_put (response);
        return true;
    }
    else
    {
    	const char * ctemp = json_object_get_string(tempDirLabel);
    	tempDir = (ctemp ? ctemp : "");
    }

    json_object* files = json_object_object_get (root, "files");
    if (!files || is_error(files))
    {
        //g_warning ("%s: No files specified in postRestore message",__FUNCTION__);
        qWarning () << "No files specified in postRestore message";
        json_object_object_add (response, "returnValue", json_object_new_boolean(false));
        json_object_object_add (response, "errorText", json_object_new_string("Required Arguments Missing"));

        //g_message ("Sending response to postRestoreCallback: %s", json_object_to_json_string (response));
        __qMessage ("Sending response to postRestoreCallback: %s", json_object_to_json_string (response));
        if (!LSMessageReply (lshandle, message, json_object_to_json_string(response), &lserror )) {
                //g_warning("Can't send reply to postRestoreCallback error: %s", lserror.message);
                qWarning() << "Can't send reply to postRestoreCallback error:" << lserror.message;
                LSErrorFree (&lserror);
        }

        json_object_put (response);
        return true;
    }

    array_list* fileArray = json_object_get_array(files);
    if (!fileArray || is_error(fileArray))
    {
        //g_warning ("%s: json value for key 'files' is not an array",__FUNCTION__);
        qWarning () << "json value for key 'files' is not an array";
        json_object_object_add (response, "returnValue", json_object_new_boolean(false));
        json_object_object_add (response, "errorText", json_object_new_string("Required Arguments Missing"));

        //g_message ("Sending response to postRestoreCallback: %s", json_object_to_json_string (response));
        __qMessage ("Sending response to postRestoreCallback: %s", json_object_to_json_string (response));
        if (!LSMessageReply (lshandle, message, json_object_to_json_string(response), &lserror )) {
                //g_warning("Can't send reply to postRestoreCallback error: %s", lserror.message);
                qWarning() <<"Can't send reply to postRestoreCallback error:" << lserror.message;
                LSErrorFree (&lserror);
        }

        json_object_put (response);
        return true;
    }


    int fileArrayLength = array_list_length (fileArray);
    int index = 0;

    //g_message("%s: fileArrayLength = %d",__FUNCTION__, fileArrayLength);
    __qMessage("fileArrayLength = %d", fileArrayLength);

    for (index = 0; index < fileArrayLength; ++index)
    {
    	json_object* obj = (json_object*) array_list_get_idx (fileArray, index);
    	if ((!obj) || is_error(obj))
    	{
            //g_warning("%s: array object [%d] isn't valid (skipping)",__FUNCTION__,index);
            qWarning() << "array object [%d] isn't valid (skipping)";
     		continue;
    	}

    	const char * cpath = json_object_get_string(obj);
    	std::string path = ( cpath ? cpath : "");
        //g_message("%s: array[%d] file: %s",__FUNCTION__,index,path.c_str());
        __qMessage("array[%d] file: %s", index,path.c_str());

    	if (path.empty())
    	{
            //g_warning("%s: array object [%d] is a file path that is empty (skipping)",__FUNCTION__,index);
            qWarning() << "array object [" << index << "] is a file path that is empty (skipping)";
    		continue;
    	}
    	if (path.find("/") != 0)
    	{
    		//not an absolute path apparently...try taking on tempdir
    		path = tempDir + std::string("/") + path;
            //g_warning("%s: array object [%d] is a file path that seems to be relative...trying to absolute-ize it by adding tempDir, like so: [%s]",
                //__FUNCTION__,index, path.c_str());
            qWarning() << "array object [" << index <<
                    "] is a file path that seems to be relative...trying to absolute-ize it by adding tempDir, like so: [" << path.c_str() << "]";
    	}

    	///PROCESS SPECIFIC FILES HERE....

    	if (path.find("systemprefs_backup.db") != std::string::npos)
    	{
    		//found the backup db...

    		if (Settings::settings()->m_saveLastBackedUpTempDb)
    		{
    			Utils::fileCopy(path.c_str(),
    					(std::string(PrefsDb::s_mediaPartitionPath)+std::string(PrefsDb::s_sysserviceDir)+std::string("/lastRestoredTempDb.db")).c_str());
    		}

    		//run a merge
    		int rc = PrefsDb::instance()->merge(path);
    		if (rc == 0)
    		{
                //g_warning("%s: merge() from [%s] didn't merge anything...could be an error or just an empty backup db",
                    //__FUNCTION__,path.c_str());
                qWarning() << "merge() from [" << path.c_str() << "] didn't merge anything...could be an error or just an empty backup db";
    		}
    	}
    }

    // if for whatever reason the main db got closed, reopen it (the function will act ok if already open)
    PrefsDb::instance()->openPrefsDb();
    //now refresh all the keys
    PrefsFactory::instance()->refreshAllKeys();

    return pThis->sendPostRestoreResponse(lshandle,message);
}

bool BackupManager::sendPreBackupResponse(LSHandle* lshandle, LSMessage *message,const std::list<std::string> fileList)
{
    EMPTY_SCHEMA_RETURN(lshandle, message);

	// the response has to contain
	// description - what is being backed up
	// files - array of files to be backed up
	// version - version of the service
	json_object* response = json_object_new_object();
	if (!response || is_error(response)) {
        //g_warning("%s: Unable to allocate json object",__FUNCTION__);
        qWarning() << "Unable to allocate json object";
		return false;
	}

	std::string versionDb = PrefsDb::instance()->getPref("databaseVersion");
	if (versionDb.empty())
		versionDb = "0.0";			//signifies a problem

	json_object_object_add (response, "description", json_object_new_string ("Backup of LunaSysService, containing the systemprefs sqlite3 database"));
	json_object_object_add (response, "version", json_object_new_string (versionDb.c_str()));

	struct json_object* files = json_object_new_array();

	if (m_doBackupFiles)
	{
		std::list<std::string>::const_iterator i;
		for (i = fileList.begin(); i != fileList.end(); ++i) {
				json_object_array_add (files, json_object_new_string(i->c_str()));
                //g_message("%s: added file %s to the backup list",__FUNCTION__,i->c_str());
                __qMessage("added file %s to the backup list", i->c_str());
		}
	}
	else
	{
        //g_warning("%s: opted not to do a backup at this time due to doBackup internal var",__FUNCTION__);
        qWarning() << "opted not to do a backup at this time due to doBackup internal var";
	}

	json_object_object_add (response, "files", files);

	LSError lserror;
	LSErrorInit(&lserror);

    //g_message ("%s: Sending response to preBackupCallback: %s", __FUNCTION__,json_object_to_json_string (response));
    __qMessage ("Sending response to preBackupCallback: %s", json_object_to_json_string (response));
	if (!LSMessageReply (lshandle, message, json_object_to_json_string(response), &lserror )) {
        //g_warning("%s: Can't send reply to preBackupCallback error: %s",__FUNCTION__, lserror.message);
        qWarning() << "Can't send reply to preBackupCallback error:" << lserror.message;
		LSErrorFree (&lserror);
	}

	json_object_put (response);
	return true;
}

bool BackupManager::sendPostRestoreResponse(LSHandle* lshandle, LSMessage *message)
{
    EMPTY_SCHEMA_RETURN(lshandle, message);

	LSError lserror;
	LSErrorInit(&lserror);
	json_object* response = json_object_new_object();

	json_object_object_add (response, "returnValue", json_object_new_boolean(true));

    //g_message ("Sending response to postRestoreCallback: %s", json_object_to_json_string (response));
    __qMessage ("Sending response to postRestoreCallback: %s", json_object_to_json_string (response));
	if (!LSMessageReply (lshandle, message, json_object_to_json_string(response), &lserror )) {
        //g_warning("Can't send reply to postRestoreCallback error: %s", lserror.message);
        qWarning() << "Can't send reply to postRestoreCallback error:" << lserror.message;
		LSErrorFree (&lserror);
	}

	json_object_put (response);
	return true;

}
