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


#ifndef PREFSFACTORY_H
#define PREFSFACTORY_H

#include <string>
#include <map>

#include <luna-service2/lunaservice.h>

class PrefsHandler;

class PrefsFactory
{
public:

	static PrefsFactory* instance();
	void setServiceHandle(LSPalmService* service);
	LSPalmService* serviceHandle() const;

	PrefsHandler* getPrefsHandler(const std::string& key) const;
	
	void postPrefChange(const std::string& key,const std::string& value);
	void postPrefChangeValueIsCompleteString(const std::string& key,const std::string& json_string);
	void runConsistencyChecksOnAllHandlers();
	
	void refreshAllKeys();		//useful for when the database is completely restored to another version
								//at some point after sysservice startup (see BackupManager)
private:

	PrefsFactory();
	~PrefsFactory();

	void init();
	void registerPrefHandler(PrefsHandler* handler);
	
private:

	typedef std::map<std::string, PrefsHandler*> PrefsHandlerMap;
	
	LSPalmService* m_service;
	LSHandle* m_serviceHandlePublic;
	LSHandle* m_serviceHandlePrivate;
		
	PrefsHandlerMap m_handlersMaps;

};


#endif /* PREFSFACTORY_H */
