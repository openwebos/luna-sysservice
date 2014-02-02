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



#include "BuildInfoHandler.h"

#include <glib.h>
#include <stdlib.h>
#include <sys/time.h>
#include <time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>

#include "PrefsDb.h"
#include "Logging.h"
#include "Utils.h"

#include <cjson/json_util.h>

#define		BUILDINFO_FILE			WEBOS_INSTALL_WEBOS_SYSCONFDIR "/palm-customization-info"

std::list<std::string> BuildInfoHandler::s_keys;

//public:

BuildInfoHandler::BuildInfoHandler(LSPalmService* service) : PrefsHandler(service) {
	init();
}

BuildInfoHandler::~BuildInfoHandler() {
	
}

std::list<std::string> BuildInfoHandler::keys() const {
	
	return s_keys;
}

bool BuildInfoHandler::validate(const std::string& key, json_object* value) {
	
	return false;		//don't allow any changes
}

void BuildInfoHandler::valueChanged(const std::string& key, json_object* value) {
	
}

json_object* BuildInfoHandler::valuesForKey(const std::string& key) {
	
	return NULL;
}

//private:

void BuildInfoHandler::init() 
{
	
	//load the build values from the build file
	
	std::map<std::string,std::string> KVpairs;
	int n = readBuildInfoFile(KVpairs);
	if (n == 0)
		return;
	
	for(std::map<std::string,std::string>::iterator it = KVpairs.begin();
		it != KVpairs.end();
		++it)
	{
		//only one I care about right now is "build"
		if (it->first == std::string("build"))
			PrefsDb::instance()->setPref(std::string("customizationBuild"),it->second);
	}
}

/*
 * returns the number of key-value pairs added to the map from the file
 * 
 * Note that it does not clear the map that was passed in (KVs are appended)
 */
int BuildInfoHandler::readBuildInfoFile(std::map<std::string,std::string>& KVpairs) {
	
	FILE * fp = fopen(BUILDINFO_FILE,"r");
	if (fp == NULL)
		return 0;
	
	char lineb[2048];
	int lc=0;
	int n=0;
	std::string key;
	std::string value;
	
	while (!feof(fp)) 
	{
		++lc;	//count lines...helps debug efforts
		char *lp = fgets(lineb,2048,fp);
		if (lp == NULL)
			continue;
		std::string line(lp);
		Utils::trimWhitespace_inplace(line);
		std::list<std::string> splits;
		if (Utils::splitStringOnKey(splits,line,std::string("=")) < 2)
			continue;
		std::list<std::string>::iterator it = splits.begin();
		key = *it;
		++it;
		value = *it;
		KVpairs[key] = value;
		++n;
	}
	fclose(fp);
	return n;
}

