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


#include "Logging.h"
#include "PrefsDb.h"
#include "Utils.h"

#include "LocalePrefsHandler.h"

static const char* s_logChannel = "LocalePrefsHandler";
static const char* s_defaultLocaleFile = WEBOS_INSTALL_WEBOS_SYSCONFDIR "/locale.txt";
static const char* s_custLocaleFile = WEBOS_INSTALL_SYSMGR_DATADIR "/customization/locale.txt";
static const char* s_defaultRegionFile = WEBOS_INSTALL_WEBOS_SYSCONFDIR "/region.json";
static const char* s_custRegionFile = WEBOS_INSTALL_SYSMGR_DATADIR "/customization/region.json";

LocalePrefsHandler::LocalePrefsHandler(LSPalmService* service)
	: PrefsHandler(service)
{
	init();
}

LocalePrefsHandler::~LocalePrefsHandler()
{
}

std::list<std::string> LocalePrefsHandler::keys() const
{
	std::list<std::string> k;
	k.push_back("locale");
	k.push_back("region");
	return k;
}

bool LocalePrefsHandler::validateLocale(json_object* value)
{
	if (json_object_get_type(value) != json_type_object)
		return false;

	json_object* label = 0;
	std::string lcode;
	std::string ccode;
	const char* languageCode = 0;
	const char* countryCode = 0;

	label = json_object_object_get(value, "languageCode");
	if (!label) {
        //luna_warn(s_logChannel, "Failed to find param languageCode");
        qWarning() << "Failed to find param languageCode";
	}
	else {
		lcode = json_object_get_string(label);
		languageCode = lcode.c_str();
	}

	label = json_object_object_get(value, "countryCode");
	if (!label) {
        //luna_warn(s_logChannel, "Failed to find param countryCode");
        qWarning() << "Failed to find param countryCode";
	}
	else {
		ccode = json_object_get_string(label);
		countryCode = ccode.c_str();
	}

	if ((languageCode) && (countryCode)) {
		bool found = false;

		for (LocaleEntryList::const_iterator it = m_localeEntryList.begin();
		it != m_localeEntryList.end(); ++it) {

			const LocaleEntry& locEntry = (*it);
			if (locEntry.language.second == languageCode) {

				for (NameCodePairList::const_iterator iter = locEntry.countries.begin();
				iter != locEntry.countries.end(); ++iter) {

					const NameCodePair& countryEntry = (*iter);
					if (countryEntry.second == countryCode) {
						found = true;
						break;
					}
				}
				if (found)
					break;
			}
		}
		if (!found)
			return false;
	}

	return true;

}

bool LocalePrefsHandler::validateRegion(json_object* value)
{
	if (json_object_get_type(value) != json_type_object)
		return false;

	json_object* label = 0;
	std::string rcode;
	const char* regionCode = 0;

	label = json_object_object_get(value, "countryCode");
	if (!label) {
        //luna_warn(s_logChannel, "Failed to find param regionCode");
        qWarning() << "Failed to find param regionCode";
	}
	else {
		rcode = json_object_get_string(label);
		regionCode = rcode.c_str();
	}

	if (regionCode) {
		bool found = false;

		for (RegionEntryList::const_iterator it = m_regionEntryList.begin();
		it != m_regionEntryList.end(); ++it) {

			const RegionEntry& rEntry = (*it);
			if (rEntry.region[2] == regionCode) {
				found=true;
				break;
			}
		}
		if (!found)
			return false;
	}

	return true;

}

bool LocalePrefsHandler::validate(const std::string& key, json_object* value)
{

	if (key == "locale")
		return validateLocale(value);
	else if (key == "region")
		return validateRegion(value);

	return false;
}

void LocalePrefsHandler::valueChangedLocale(json_object* value)
{
	// nothing to do
}

void LocalePrefsHandler::valueChangedRegion(json_object* value)
{
	// nothing to do
}

void LocalePrefsHandler::valueChanged(const std::string& key, json_object* value)
{
	// We will assume that the value has been validated
	if (key == "locale")
		valueChangedLocale(value);
	else if (key == "region")
		valueChangedRegion(value);
}

json_object* LocalePrefsHandler::valuesForLocale()
{
	json_object* json = json_object_new_object();
	json_object* langArrayObj = json_object_new_array();

	for (LocaleEntryList::const_iterator it = m_localeEntryList.begin();
	it != m_localeEntryList.end(); ++it) {

		const LocaleEntry& locEntry = (*it);
		json_object* langObj = json_object_new_object();
		json_object_object_add(langObj, (char*) "languageName",
				json_object_new_string((char*) locEntry.language.first.c_str()));
		json_object_object_add(langObj, (char*) "languageCode",
				json_object_new_string((char*) locEntry.language.second.c_str()));

		json_object* countryArrayObj = json_object_new_array();
		for (NameCodePairList::const_iterator iter = locEntry.countries.begin();
		iter != locEntry.countries.end(); ++iter) {
			json_object* countryObj = json_object_new_object();
			json_object_object_add(countryObj, (char*) "countryName",
					json_object_new_string((char*) (*iter).first.c_str()));
			json_object_object_add(countryObj, (char*) "countryCode",
					json_object_new_string((char*) (*iter).second.c_str()));
			json_object_array_add(countryArrayObj, countryObj);
		}

		json_object_object_add(langObj, (char*) "countries", countryArrayObj);
		json_object_array_add(langArrayObj, langObj);
	}

	json_object_object_add(json, (char*) "locale", langArrayObj);

	return json;

}

json_object* LocalePrefsHandler::valuesForRegion()
{
	json_object* json = json_object_new_object();
	json_object* regArrayObj = json_object_new_array();

	for (RegionEntryList::const_iterator it = m_regionEntryList.begin();
	it != m_regionEntryList.end(); ++it) {

		const RegionEntry& regEntry = (*it);
		json_object* regObj = json_object_new_object();
		json_object_object_add(regObj,(char*) "shortCountryName",
				json_object_new_string((char*) regEntry.region[0].c_str()));
		json_object_object_add(regObj, (char*) "countryName",
				json_object_new_string((char*) regEntry.region[1].c_str()));
		json_object_object_add(regObj, (char*) "countryCode",
				json_object_new_string((char*) regEntry.region[2].c_str()));

		json_object_array_add(regArrayObj, regObj);
	}

	json_object_object_add(json, (char*) "region", regArrayObj);
	
	return json;
}
	
json_object* LocalePrefsHandler::valuesForKey(const std::string& key)
{
	if (key == "locale")
		return valuesForLocale();
	else if (key == "region")
		return valuesForRegion();
	else
		return json_object_new_object();
}

void LocalePrefsHandler::init()
{
	readCurrentLocaleSetting();
	readCurrentRegionSetting();
	readLocaleFile();
	readRegionFile();
}

void LocalePrefsHandler::readCurrentRegionSetting()
{
	std::string region = PrefsDb::instance()->getPref("region");
	bool success = false;

	if (!region.empty()) {

		json_object* label = 0;
		json_object* json = json_tokener_parse(region.c_str());
		if (!json || is_error(json))
			goto Done;

		label = json_object_object_get(json, "countryCode");
		if (!label || is_error(label))
			goto Done;
		m_regionCode = json_object_get_string(label);

		success = true;

	Done:

		if (json && !is_error(json))
			json_object_put(json);
	}

	if (!success) {
		m_regionCode = "us";
	}
}

void LocalePrefsHandler::readCurrentLocaleSetting()
{
	std::string locale = PrefsDb::instance()->getPref("locale");
	bool success = false;
	
	if (!locale.empty()) {

		json_object* label = 0;
		json_object* json = json_tokener_parse(locale.c_str());
		if (!json || is_error(json))
			goto Done;

		label = json_object_object_get(json, "languageCode");
		if (!label || is_error(label))
			goto Done;
		m_languageCode = json_object_get_string(label);

		label = json_object_object_get(json, "countryCode");
		if (!label || is_error(label))
			goto Done;
		m_countryCode = json_object_get_string(label);

		success = true;

	Done:

		if (json && !is_error(json))
			json_object_put(json);
	}

	if (!success) {
		m_languageCode = "en";
		m_countryCode = "us";
	}
}

void LocalePrefsHandler::readLocaleFile()
{
	// Read the locale file
	char* jsonStr = Utils::readFile(s_custLocaleFile);
	if (!jsonStr)
		jsonStr = Utils::readFile(s_defaultLocaleFile);
	if (!jsonStr) {
        //luna_critical(s_logChannel, "Failed to load locale files: [%s] nor [%s]", s_custLocaleFile,s_defaultLocaleFile);
        qCritical() << "Failed to load locale files: [" << s_custLocaleFile << "] nor [" << s_defaultLocaleFile << "]";
		return;
	}

	json_object* root = 0;
	json_object* label = 0;
	array_list* localeArray = 0;

	root = json_tokener_parse(jsonStr);
	if (!root || is_error(root)) {
        //luna_critical(s_logChannel, "Failed to parse locale file contents into json");
        qCritical() << "Failed to parse locale file contents into json";
		goto Done;
	}

	label = json_object_object_get(root, "locale");
	if (!label || is_error(label)) {
        //luna_critical(s_logChannel, "Failed to get locale entry from locale file");
        qCritical() << "Failed to get locale entry from locale file";
		goto Done;
	}

	localeArray = json_object_get_array(label);
	if (!localeArray) {
        //luna_critical(s_logChannel, "Failed to get locale array from locale file");
        qCritical() << "Failed to get locale array from locale file";
		goto Done;
	}

	for (int i = 0; i < array_list_length(localeArray); i++) {
		json_object* obj = (json_object*) array_list_get_idx(localeArray, i);

		LocaleEntry localeEntry;
		array_list* countryArray = 0;
		
		label = json_object_object_get(obj, "languageName");
		if (!label || is_error(label))
			continue;

		localeEntry.language.first = json_object_get_string(label);

		label = json_object_object_get(obj, "languageCode");
		if (!label || is_error(label))
			continue;

		localeEntry.language.second = json_object_get_string(label);

		label = json_object_object_get(obj, "countries");
		if (!label || is_error(label))
			continue;

		countryArray = json_object_get_array(label);
		for (int j = 0; j < array_list_length(countryArray); j++) {
			json_object* countryObj = (json_object*) array_list_get_idx(countryArray, j);
			NameCodePair country;

			label = json_object_object_get(countryObj, "countryName");
			if (!label || is_error(label))
				continue;

			country.first = json_object_get_string(label);

			label = json_object_object_get(countryObj, "countryCode");
			if (!label || is_error(label))
				continue;

			country.second = json_object_get_string(label);

			localeEntry.countries.push_back(country);
		}

		m_localeEntryList.push_back(localeEntry);		
	}

Done:

	if (root && !is_error(root))
		json_object_put(root);

	delete [] jsonStr;
}

void LocalePrefsHandler::readRegionFile() 
{
	// Read the locale file
	char* jsonStr = Utils::readFile(s_custRegionFile);
	if (!jsonStr)
		jsonStr = Utils::readFile(s_defaultRegionFile);
	if (!jsonStr) {
        //luna_critical(s_logChannel, "Failed to load region files: [%s] nor [%s]", s_custRegionFile,s_defaultRegionFile);
        qCritical() << "Failed to load region files: [" << s_custRegionFile << "] nor [" << s_defaultRegionFile << "]";
		return;
	}

	json_object* root = 0;
	json_object* label = 0;
	array_list* regionArray = 0;

	root = json_tokener_parse(jsonStr);
	if (!root || is_error(root)) {
        //luna_critical(s_logChannel, "Failed to parse region file contents into json");
        qCritical() << "Failed to parse region file contents into json";
		goto Done;
	}

	label = json_object_object_get(root, "region");
	if (!label || is_error(label)) {
        //luna_critical(s_logChannel, "Failed to get region entry from region file");
        qCritical() << "Failed to get region entry from region file";
		goto Done;
	}

	regionArray = json_object_get_array(label);
	if (!regionArray) {
        //luna_critical(s_logChannel, "Failed to get region array from region file");
        qCritical() << "Failed to get region array from region file";
		goto Done;
	}

	for (int i = 0; i < array_list_length(regionArray); i++) {
		json_object* obj = (json_object*) array_list_get_idx(regionArray, i);

		RegionEntry regionEntry;

		label = json_object_object_get(obj, "countryName");
		if (!label || is_error(label))
			continue;

		regionEntry.region[1] = std::string(json_object_get_string(label));
		
		label = json_object_object_get(obj, "shortCountryName");
		if (!label || is_error(label))
			regionEntry.region[0] = regionEntry.region[1];
		else
			regionEntry.region[0] = std::string(json_object_get_string(label));
		
		label = json_object_object_get(obj, "countryCode");
		if (!label || is_error(label))
			continue;

		regionEntry.region[2] = std::string(json_object_get_string(label));

		m_regionEntryList.push_back(regionEntry);		
	}

	Done:

	if (root && !is_error(root))
		json_object_put(root);

	delete [] jsonStr;
}

std::string LocalePrefsHandler::currentLocale() const
{
	return m_languageCode + "_"  + m_countryCode;    
}

std::string LocalePrefsHandler::currentRegion() const
{
	return m_regionCode;    
}

