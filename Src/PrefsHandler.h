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


#ifndef PREFSHANDLER_H
#define PREFSHANDLER_H

#include <string>
#include <list>
#include <map>

#include <cjson/json.h>
#include <luna-service2/lunaservice.h>

class PrefsHandler
{
public:

	PrefsHandler(LSPalmService* service) : m_service(service) , m_serviceHandlePublic(0) , m_serviceHandlePrivate(0) {}
	virtual ~PrefsHandler() {}

	virtual std::list<std::string> keys() const = 0;
	virtual bool validate(const std::string& key, json_object* value) = 0;
	virtual bool validate(const std::string& key, json_object* value, const std::string& originId)
	{ return validate(key,value); }
	virtual void valueChanged(const std::string& key, json_object* value) = 0;
	virtual void valueChanged(const std::string& key,const std::string& strval)
	{
		if (strval.empty())
			return;
		//WORKAROUND WRAPPER FOR USING valueChanged() internally.  //TODO: do this the proper way.
		// the way it is now makes a useless conversion at least once

		json_object * jo = json_tokener_parse(strval.c_str());
		if (!jo || is_error(jo)) {
			return;
		}
		valueChanged(key,jo);
		json_object_put(jo);
	}
	virtual json_object* valuesForKey(const std::string& key) = 0;
	// FIXME: We very likely need a windowed version the above function
	virtual bool isPrefConsistent() { return true; }
	virtual void restoreToDefault() {}
	virtual bool shouldRefreshKeys(std::map<std::string,std::string>& keyvalues) { return false;}
	
	LSHandle * getPrivateHandle() { return m_serviceHandlePrivate;}
	LSHandle * getPublicHandle() { return m_serviceHandlePublic;}
	
protected:

	LSPalmService*	m_service;
	LSHandle* m_serviceHandlePublic;
	LSHandle* m_serviceHandlePrivate;
};

#endif /* PREFSHANDLER_H */
