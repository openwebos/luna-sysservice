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


#ifndef PREFSDB_H
#define PREFSDB_H

#include <string>
#include <map>
#include <list>

#include <sqlite3.h>

class BackupManager;

class PrefsDb
{
public:

	static PrefsDb* instance();
	static PrefsDb* createStandalone(const std::string& dbFilename,bool deleteExisting=true);

	bool setPref(const std::string& key, const std::string& value);

	std::string getPref(const std::string& key);
	bool getPref(const std::string& key,std::string& r_val);

	std::map<std::string, std::string> getPrefs(const std::list<std::string>& keys);	
	std::map<std::string,std::string> getAllPrefs();

	int merge(PrefsDb * p_sourceDb,bool overwriteSameKeys=true);
	int merge(const std::string& sourceDbFilename,bool overwriteSameKeys=true);

	int copyKeys(PrefsDb * p_sourceDb,const std::list<std::string>& keys,bool overwriteSame=true);

	std::string databaseFile() const
	{ return m_dbFilename; }

	void setDatabaseFileDeleteOnDestruction(bool deleteAtDestructor=true);

	//keeping all this in one place so that all of system service has one place to look it up in, rather than all over the other source files
	static const char* s_defaultPrefsFile;
	static const char* s_defaultPlatformPrefsFile;
	static const char* s_customizationOverridePrefsFile;
	static const char* s_custCareNumberFile;
	static const char* s_prefsDbPath;
	static const char* s_tempBackupDbFilenameOnly;
	static const char* s_prefsPath;
	static const char* s_logChannel;
	static const char* s_mediaPartitionPath;
	static const char* s_mediaPartitionWallpapersDir;
	static const char* s_mediaPartitionWallpaperThumbsDir;
	static const char* s_mediaPartitionRingtonesDir;
	static const char* s_mediaPartitionTempDir;
	static const char* s_ringtonesDir;
	static const char* s_sysserviceDir;
	static const char* s_systemTokenFileAndPath;
	static const char* s_volumeIconFile;
	static const char* s_volumeIconFileAndPathSrc;
	static const char* s_volumeIconFileAndPathDest;
	static const char* s_sysDefaultWallpaperKey;
	static const char* s_sysDefaultRingtoneKey;

	friend class BackupManager;			//because it operates on db directly
private:

	PrefsDb();
	PrefsDb(const std::string& standaloneDbFilename);
	~PrefsDb();

	void openPrefsDb();
	void closePrefsDb();

	bool checkTableConsistency();
	bool integrityCheckDb();
	void loadDefaultPrefs();
	void loadDefaultPlatformPrefs();
	void backupDefaultPrefs();

	void synchronizeDefaults();
	void synchronizePlatformDefaults();
	void synchronizeCustomerCareInfo();
	
	void updateWithCustomizationPrefOverrides();

	// MUST RUN sqlite3_finalize(x);  on return value 'x' from runSqlQuery(..) unless x == 0
	sqlite3_stmt* runSqlQuery(const std::string& queryStr);
	// (___Command is the same except does an sql exec)
	bool runSqlCommand(const std::string& cmdStr);

private:

	static PrefsDb* s_instance;
	sqlite3* m_prefsDb;
	bool m_standalone;
	std::string m_dbFilename;
	bool m_deleteOnDestroy;
};

#endif /* PREFSDB_H */
