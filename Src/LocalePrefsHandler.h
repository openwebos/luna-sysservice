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


#ifndef LOCALEPREFSHANDLER_H
#define LOCALEPREFSHANDLER_H

#include "PrefsHandler.h"

class LocalePrefsHandler : public PrefsHandler
{
public:

	LocalePrefsHandler(LSPalmService* service);
	virtual ~LocalePrefsHandler();

	virtual std::list<std::string> keys() const;
	virtual bool validate(const std::string& key, json_object* value);
	virtual void valueChanged(const std::string& key, json_object* value);
	virtual json_object* valuesForKey(const std::string& key);
	
	std::string currentLocale() const;
	std::string currentRegion() const;
	
private:

	void init();
	void readCurrentLocaleSetting();
	void readCurrentRegionSetting();
	void readLocaleFile();
	void readRegionFile();
	
	bool validateLocale(json_object* value);
	bool validateRegion(json_object* value);
	void valueChangedLocale(json_object* value);
	void valueChangedRegion(json_object* value);
	json_object* valuesForLocale();
	json_object* valuesForRegion();
		
private:

	typedef std::pair<std::string, std::string> NameCodePair;
	typedef std::string						NameCodeTriplet[3];
	typedef std::list<NameCodePair> NameCodePairList;

	struct LocaleEntry {
		NameCodePair language;
		NameCodePairList countries;
	};

	struct RegionEntry {
		NameCodeTriplet region;
	};
	
	typedef std::list<LocaleEntry> LocaleEntryList;
	typedef std::list<RegionEntry> RegionEntryList;
	
	LocaleEntryList m_localeEntryList;
	RegionEntryList m_regionEntryList;
	std::string m_languageCode;
	std::string m_countryCode;
	std::string m_regionCode;
};

#endif /* LOCALEPREFSHANDLER_H */
