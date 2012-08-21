/**
 *  Copyright 2010 - 2012 Hewlett-Packard Development Company, L.P.
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


#ifndef BACKUP_MANAGER_H
#define BACKUP_MANAGER_H

#include <string>
#include <list>
#include <map>
#include "lunaservice.h"

struct LSHandle;
struct LSMessage;

class PrefsDb;

class BackupManager
{
public:

	static BackupManager*	instance();
	void setServiceHandle(LSPalmService* service);

	void 	turnOffBackup() { m_doBackupFiles = false; }
	void	turnOnBackup() { m_doBackupFiles = true; }
	bool	isBackupOn() { return m_doBackupFiles; }

private:

	BackupManager	();
	~BackupManager	();

	bool	init();

	static LSMethod	s_BackupServerMethods[];
	static BackupManager* s_instance;

	bool	m_doBackupFiles;

	LSPalmService* m_service;
	std::list<std::string>	m_backupFiles;	///< List of items managing the backup/restore of.

	PrefsDb *	m_p_backupDb;

	void copyKeysToBackupDb();
	void initFilesForBackup(bool filenamesOnly);

	static bool preBackupCallback( LSHandle* lshandle, LSMessage *message, void *user_data);
	static bool postRestoreCallback( LSHandle* lshandle, LSMessage *message, void *user_data);


	// (these both return bool so that they can be used more conventiently in ___Callback() e.g.
	// return (sendPreBackupResponse(...));
	/// sendPreBackupResponse - made to take a fileList param so as to allow m_backupFiles to be unmodded in case
	// 							a temporary alternate list is to be used; this gives max flexibility
	bool sendPreBackupResponse(LSHandle* lshandle, LSMessage *message,const std::list<std::string> fileList);
	bool sendPostRestoreResponse(LSHandle* lshandle, LSMessage *message);


	static std::string s_backupKeylistFilename;
};


#endif // BACKUP_MANAGER_H
