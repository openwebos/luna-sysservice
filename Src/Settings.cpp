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
#include "Settings.h"

#include <stdio.h>
#include <string.h>
#include <glib.h>
#include <math.h>

#include "Utils.h"

static const char* kSettingsFile = "/etc/palm/sysservice.conf";
static const char* kSettingsFilePlatform = "/etc/palm/sysservice-platform.conf";

Settings* Settings::s_settings = 0;

Settings::Settings()
    : schemaValidationOption(0)
{
	(void)initValues();
	(void)load(kSettingsFile);
	(void)load(kSettingsFilePlatform);
	(void)createNeededFolders();
	(void)postLoad();
}

bool Settings::initValues()
{
	m_turnNovacomOnAtStartup = false;
	m_saveLastBackedUpTempDb = false;
	m_saveLastRestoredTempDb = false;
	m_logLevel = std::string("");
	m_useComPalmImage2 = false;
	m_image2svcAvailable = false;
	m_comPalmImage2BinaryFile = ("/usr/bin/acuteimaging");
	return true;
}

Settings::~Settings()
{
}

#define KEY_STRING(cat,name,var) \
{\
	gchar* _vs;\
	GError* _error = 0;\
	_vs=g_key_file_get_string(keyfile,cat,name,&_error);\
	if( !_error && _vs ) { var=(const char*)_vs; g_free(_vs); }\
	else g_error_free(_error); \
}

#define KEY_BOOLEAN(cat,name,var) \
{\
	gboolean _vb;\
	GError* _error = 0;\
	_vb=g_key_file_get_boolean(keyfile,cat,name,&_error);\
	if( !_error ) { var=_vb; }\
	else g_error_free(_error); \
}

#define KEY_INTEGER(cat,name,var) \
{\
	int _v;\
	GError* _error = 0;\
	_v=g_key_file_get_integer(keyfile,cat,name,&_error);\
	if( !_error ) { var=_v; }\
	else g_error_free(_error); \
}

#define KEY_DOUBLE(cat,name,var) \
{\
	double _v;\
	GError* _error = 0;\
	_v=g_key_file_get_double(keyfile,cat,name,&_error);\
	if( !_error ) { var=_v; }\
	else g_error_free(_error); \
}


bool Settings::load(const char* settingsFile)
{
	GKeyFile* keyfile;
	GKeyFileFlags flags;
	GError* error = 0;

	keyfile = g_key_file_new();
	if(!keyfile)
		return false;
	flags = GKeyFileFlags( G_KEY_FILE_KEEP_COMMENTS | G_KEY_FILE_KEEP_TRANSLATIONS);

	if( !g_key_file_load_from_file( keyfile, settingsFile, flags, &error ) )
	{
		g_key_file_free( keyfile );
		if (error) g_error_free(error);
		return false;
	}

	KEY_BOOLEAN("Debug","turnOnNovacomAtStart",m_turnNovacomOnAtStartup);
	KEY_BOOLEAN("Debug","saveLastBackedUpTempDb",m_saveLastBackedUpTempDb);
	KEY_BOOLEAN("Debug","saveLastRestoredTempDb",m_saveLastRestoredTempDb);
	KEY_STRING("Debug","logLevel",m_logLevel);

	KEY_BOOLEAN("ImageService","useComPalmImage2",m_useComPalmImage2);
	KEY_STRING("ImageService","comPalmImage2Binary",m_comPalmImage2BinaryFile);

    KEY_INTEGER("General", "schemaValidationOption", schemaValidationOption);

	g_key_file_free( keyfile );
	return true;
}

bool Settings::postLoad()
{
	return true;
}

bool Settings::createNeededFolders()
{
	return true;
}
