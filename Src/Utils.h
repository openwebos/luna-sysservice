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


#ifndef UTILS_H
#define UTILS_H

#undef min
#undef max

#include <glib.h>
#include <sstream>
#include <iostream>
#include <list>
#include <string>
#include <vector>
#include <cjson/json.h>
#include <cjson/json_util.h>
#include <luna-service2/lunaservice.h>

#define SS_DEBUG_INFO	100
#define SS_DEBUG_WARN	50
#define SS_DEBUG_ERR	10

#ifdef SYSSERVICE_DEBUGLIMIT
#define dbgcprintf(level,...) \
	do { \
		if (((unsigned int)(level)) <= (SYSSERVICE_DEBUGLIMIT)) \
			Utils::_dbgprintf(__VA_ARGS__); \
	} while (0);
#define dbgprintf(...) \
	do { \
		if ((SS_DEBUG_INFO) <= (SYSSERVICE_DEBUGLIMIT)) \
			Utils::_dbgprintf(__VA_ARGS__); \
	} while (0);

#else
#define dbgcprintf(level,s,...)  (void)0
#define dbgprintf(s,...)	(void)0
#endif

namespace Utils {

template <class T>
std::string toSTLString(const T &arg) {
	std::ostringstream	out;
	out << arg;
	return(out.str());
}

char* readFile(const char* filePath);

std::string trimWhitespace(const std::string& s,const std::string& drop = "\r\n\t ");
void trimWhitespace_inplace(std::string& s_mod,const std::string& drop = "\r\n\t ");

bool getNthSubstring(unsigned int n,std::string& target, const std::string& str,const std::string& delims = " \t\n\r");
int splitFileAndPath(const std::string& srcPathAndFile,std::string& pathPart,std::string& filePart);
int splitFileAndExtension(const std::string& srcFileAndExt,std::string& filePart,std::string& extensionPart);
int splitStringOnKey(std::vector<std::string>& returnSplitSubstrings,const std::string& baseStr,const std::string& delims);
int splitStringOnKey(std::list<std::string>& returnSplitSubstrings,const std::string& baseStr,const std::string& delims);

bool doesExistOnFilesystem(const char * pathAndFile);
int fileCopy(const char * srcFileAndPath,const char * dstFileAndPath);

int filesizeOnFilesystem(const char * pathAndFile);

int urlDecodeFilename(const std::string& encodedName,std::string& decodedName);
int urlEncodeFilename(std::string& encodedName,const std::string& decodedName);

unsigned int getRNG_UInt();
std::string base64_encode(unsigned char const* , unsigned int len);
std::string base64_decode(std::string const& s);

bool extractFromJson(const std::string& jsonString,const std::string& key,std::string& r_value);
bool extractFromJson(struct json_object * root,const std::string& key,std::string& r_value);

//Wrapper on CJSON to avoid ptr values of -1 (and the use of is_error)
struct json_object * JsonGetObject(struct json_object * root,const std::string& key);

#define ERRMASK_POSTSUBUPDATE_PUBLIC	1
#define ERRMASK_POSTSUBUPDATE_PRIVATE	2
int postSubscriptionUpdate(const std::string& key,const std::string& postMessage,LSHandle * serviceHandlePublic,LSHandle * serviceHandlePrivate);
bool processSubscription(LSHandle * serviceHandle, LSMessage * message,const std::string& key);
uint32_t removeSubscriptions(const std::string& key,LSHandle * serviceHandlePublic,LSHandle * serviceHandlePrivate);

uint32_t countSubscribers(const std::string& key,LSHandle * serviceHandlePublic,LSHandle * serviceHandlePrivate);
bool 	hasSubscribers(const std::string& key,LSHandle * serviceHandlePublic,LSHandle * serviceHandlePrivate);

void _dbgprintf(const char * format,...);

int createTempFile(const std::string& baseDir,const std::string& tag,const std::string& extension,std::string& r_fileAndPath);

// Build an std::string using printf-style formatting
std::string string_printf(const char *format, ...) G_GNUC_PRINTF(1, 2);

// Append a printf-style string to an existing std::string
std::string & append_format(std::string & str, const char * format, ...) G_GNUC_PRINTF(2, 3);

void string_to_lower(std::string& str);

}

#endif /* UTILS_H */
