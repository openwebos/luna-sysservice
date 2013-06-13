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


/*
 * Base64 encoding copyright follows:
 * 
 * Copyright (C) 2004-2008 René Nyffenegger

   This source code is provided 'as-is', without any express or implied
   warranty. In no event will the author be held liable for any damages
   arising from the use of this software.

   Permission is granted to anyone to use this software for any purpose,
   including commercial applications, and to alter it and redistribute it
   freely, subject to the following restrictions:

   1. The origin of this source code must not be misrepresented; you must not
      claim that you wrote the original source code. If you use this source code
      in a product, an acknowledgment in the product documentation would be
      appreciated but is not required.

   2. Altered source versions must be plainly marked as such, and must not be
      misrepresented as being the original source code.

   3. This notice may not be removed or altered from any source distribution.

   René Nyffenegger rene.nyffenegger@adp-gmbh.ch
 * 
 */


#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/statfs.h>
#include <stdarg.h>
#include <unistd.h>
#include "UrlRep.h"
#include "Utils.h"
#include <cstdlib>
#include <cstring>
#include <algorithm>

namespace Utils
{

void _dbgprintf(const char * format, ...)
{
	va_list args;
	va_start(args, format);
	std::string appendFormat = std::string("LunaSysService:: ")+std::string(format)+std::string("\n");
	vprintf(appendFormat.c_str(), args);
	fflush(stdout);fflush(stderr);
	va_end(args);
}

char* readFile(const char* filePath)
{
	if (!filePath)
		return 0;
	
	FILE* f = fopen(filePath,"r");
	
	if (!f)
		return 0;

	fseek(f, 0L, SEEK_END);
	long sz = ftell(f);
	fseek( f, 0L, SEEK_SET );
	if (!sz) {
		fclose(f);
		return 0;
	}

	char* ptr = new char[sz+1];
	if( !ptr )
	{
		fclose(f);
		return 0;
	}
	ptr[sz] = 0;
	
	fread(ptr, sz, 1, f);
	fclose(f);

	return ptr;
}

std::string trimWhitespace(const std::string& s,const std::string& drop)
{
 std::string::size_type first = s.find_first_not_of( drop );
 std::string::size_type last  = s.find_last_not_of( drop );
 
 if( first == std::string::npos || last == std::string::npos ) return std::string( "" );
   return s.substr( first, last - first + 1 );
}

bool getNthSubstring(unsigned int n,std::string& dest,const std::string& str,const std::string& delims) {
	if (n == 0)
		n=1;
	
	std::string::size_type start = 0;
	std::string::size_type mark = 0;
	unsigned int i=1;
	while (1) {
		//find the start of a non-delim
		start = str.find_first_not_of(delims,mark);
		if (start == std::string::npos)
			break;
		//find the end of the current substring (where the next instance of delim lives, or end of the string)
		mark = str.find_first_of(delims,start);
		if ((mark == std::string::npos) || (i == n))
			break;	//last substring, or Nth one found
		++i;
	}
	
	if (i != n)
		return false;
	
	//extract
	dest = str.substr(start,mark-start);
	return true;
	
}

int splitFileAndPath(const std::string& srcPathAndFile,std::string& pathPart,std::string& filePart) {
		
	std::vector<std::string> parts;
	//printf("splitFileAndPath - input [%s]\n",srcPathAndFile.c_str());
	int s = splitStringOnKey(parts,srcPathAndFile,std::string("/"));
	if ((s == 1) && (srcPathAndFile.at(srcPathAndFile.length()-1) == '/')) {
		//only path part
		pathPart = srcPathAndFile;
		filePart = "";
	}
	else if (s == 1) {
		//only file part
		if (srcPathAndFile.at(0) == '/') {
			pathPart = "/";
		}
		else {
			pathPart = "";
		}
		filePart = parts.at(0);
	}
	else if (s >= 2) {
		for (int i=0;i<s-1;i++) {
			if ((parts.at(i)).size() == 0)
				continue;
			pathPart += std::string("/")+parts.at(i);
			//printf("splitFileAndPath - path is now [%s]\n",pathPart.c_str());
		}
		pathPart += std::string("/");
		filePart = parts.at(s-1);
	}
	
	return s;
}

int splitFileAndExtension(const std::string& srcFileAndExt,std::string& filePart,std::string& extensionPart) {
	
	std::vector<std::string> parts;
		int s = splitStringOnKey(parts,srcFileAndExt,std::string("."));
		if (s == 1) {
			//only file part; no extension
			filePart = parts.at(0);
		}
		else if (s >= 2) {
			filePart += parts.at(0);
			for (int i=1;i<s-1;i++)
				filePart += std::string(".")+parts.at(i);
			extensionPart = parts.at(s-1);
		}
		return s;
}

int splitStringOnKey(std::vector<std::string>& returnSplitSubstrings,const std::string& baseStr,const std::string& delims) {

	std::string base = trimWhitespace(baseStr);
	std::string::size_type start = 0;
	std::string::size_type mark = 0;
	std::string extracted;
	
	int i=0;
	while (start < baseStr.size()) {
		//find the start of a non-delims
		start = baseStr.find_first_not_of(delims,mark);
		if (start == std::string::npos)
			break;
		//find the end of the current substring (where the next instance of delim lives, or end of the string)
		mark = baseStr.find_first_of(delims,start);
		if (mark == std::string::npos)
			mark = baseStr.size();
		
		extracted = baseStr.substr(start,mark-start);
		if (extracted.size() > 0) {
			//valid string...add it
			returnSplitSubstrings.push_back(extracted);
			++i;
		}
		start=mark;
	}

	return i;
}

void trimWhitespace_inplace(std::string& s_mod,const std::string& drop)
{
 std::string::size_type first = s_mod.find_first_not_of( drop );
 std::string::size_type last  = s_mod.find_last_not_of( drop );
 
 if( first == std::string::npos || last == std::string::npos ) 
	 s_mod= std::string( "" );
 else
	 s_mod = s_mod.substr( first, last - first + 1 );
}

int splitStringOnKey(std::list<std::string>& returnSplitSubstrings,const std::string& baseStr,const std::string& delims) {

	std::string base = trimWhitespace(baseStr);
	std::string::size_type start = 0;
	std::string::size_type mark = 0;
	std::string extracted;
	
	int i=0;
	while (start < baseStr.size()) {
		//find the start of a non-delims
		start = baseStr.find_first_not_of(delims,mark);
		if (start == std::string::npos)
			break;
		//find the end of the current substring (where the next instance of delim lives, or end of the string)
		mark = baseStr.find_first_of(delims,start);
		if (mark == std::string::npos)
			mark = baseStr.size();
		
		extracted = baseStr.substr(start,mark-start);
		if (extracted.size() > 0) {
			//valid string...add it
			returnSplitSubstrings.push_back(extracted);
			++i;
		}
		start=mark;
	}

	return i;
}

bool doesExistOnFilesystem(const char * pathAndFile) {
	
	if (pathAndFile == NULL)
		return false;
	
	struct stat buf;
	if (-1 == ::stat(pathAndFile, &buf ) )
		return false;
	return true;
			
	
}

int fileCopy(const char * srcFileAndPath,const char * dstFileAndPath)
{
	if ((srcFileAndPath == NULL) || (dstFileAndPath == NULL))
		return -1;
	
	FILE * infp = fopen(srcFileAndPath,"rb");
	FILE * outfp = fopen(dstFileAndPath,"wb");
	if ((infp == NULL) || (outfp == NULL)) {
		if (infp)
			fclose(infp);
		if (outfp)
			fclose(outfp);
		return -1;
	}
	
	char buffer[2048];
	bool success=true;
	while (!feof(infp)) {
		size_t r = fread(buffer,1,2048,infp);
		if ((r == 0) && (ferror(infp))) {
			success=false;
			break;
		}
		size_t w = fwrite(buffer,1,r,outfp);
		if (w < r) {
			success=false;
			break;
		}
	}
	
	fflush(infp);
	fflush(outfp);	//apparently our filesystem doesn't like to commit even on close
	fclose(infp);
	fclose(outfp);
	return 1;
}

int filesizeOnFilesystem(const char * pathAndFile)
{
	if (pathAndFile == NULL)
		return 0;

	struct stat buf;
	if (-1 == ::stat(pathAndFile, &buf ) )
		return 0;
	return buf.st_size;
}

unsigned int getRNG_UInt()
{
	FILE * fp = fopen("/dev/urandom","rb");
	if (fp == NULL) {
		//can't open RNG. Use a much less random method
		srand(time(NULL));
		return rand();
	}
	
	unsigned int r=0;
	int nr=0;
	do {
		nr = fread(&r,1, sizeof(r), fp);
	} while (nr != sizeof(r));
	
	fclose(fp);
	return r;
}

//// wrappers for now, in case more processing/error checking is needed on the inp/outp values (probably should)
// return > 0 if coding was successful
int urlDecodeFilename(const std::string& encodedName,std::string& decodedName)
{
	decodedName = unescape(encodedName);
	return 1;
}

int urlEncodeFilename(std::string& encodedName,const std::string& decodedName)
{
	encodedName = escape(decodedName);
	return 1;
}

static const std::string base64_chars = 
             "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
             "abcdefghijklmnopqrstuvwxyz"
             "0123456789+/";


static inline bool is_base64(unsigned char c) {
  return (isalnum(c) || (c == '+') || (c == '/'));
}

std::string base64_encode(unsigned char const* bytes_to_encode, unsigned int in_len) {
  std::string ret;
  int i = 0;
  int j = 0;
  unsigned char char_array_3[3];
  unsigned char char_array_4[4];

  while (in_len--) {
    char_array_3[i++] = *(bytes_to_encode++);
    if (i == 3) {
      char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
      char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
      char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);
      char_array_4[3] = char_array_3[2] & 0x3f;

      for(i = 0; (i <4) ; i++)
        ret += base64_chars[char_array_4[i]];
      i = 0;
    }
  }

  if (i)
  {
    for(j = i; j < 3; j++)
      char_array_3[j] = '\0';

    char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
    char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
    char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);
    char_array_4[3] = char_array_3[2] & 0x3f;

    for (j = 0; (j < i + 1); j++)
      ret += base64_chars[char_array_4[j]];

    while((i++ < 3))
      ret += '=';

  }

  return ret;

}

std::string base64_decode(std::string const& encoded_string) {
  int in_len = encoded_string.size();
  int i = 0;
  int j = 0;
  int in_ = 0;
  unsigned char char_array_4[4], char_array_3[3];
  std::string ret;

  while (in_len-- && ( encoded_string[in_] != '=') && is_base64(encoded_string[in_])) {
    char_array_4[i++] = encoded_string[in_]; in_++;
    if (i ==4) {
      for (i = 0; i <4; i++)
        char_array_4[i] = base64_chars.find(char_array_4[i]);

      char_array_3[0] = (char_array_4[0] << 2) + ((char_array_4[1] & 0x30) >> 4);
      char_array_3[1] = ((char_array_4[1] & 0xf) << 4) + ((char_array_4[2] & 0x3c) >> 2);
      char_array_3[2] = ((char_array_4[2] & 0x3) << 6) + char_array_4[3];

      for (i = 0; (i < 3); i++)
        ret += char_array_3[i];
      i = 0;
    }
  }

  if (i) {
    for (j = i; j <4; j++)
      char_array_4[j] = 0;

    for (j = 0; j <4; j++)
      char_array_4[j] = base64_chars.find(char_array_4[j]);

    char_array_3[0] = (char_array_4[0] << 2) + ((char_array_4[1] & 0x30) >> 4);
    char_array_3[1] = ((char_array_4[1] & 0xf) << 4) + ((char_array_4[2] & 0x3c) >> 2);
    char_array_3[2] = ((char_array_4[2] & 0x3) << 6) + char_array_4[3];

    for (j = 0; (j < i - 1); j++) ret += char_array_3[j];
  }

  return ret;
}

bool extractFromJson(const std::string& jsonString,const std::string& key,std::string& r_value)
{
	if ((jsonString.length() == 0) || (key.length() == 0))
		return false;
	
	struct json_object* root = json_tokener_parse(jsonString.c_str());
	struct json_object* label = 0;
	
	if ((!root) || (is_error(root)))
		return false;
	
	label = json_object_object_get(root,key.c_str());
	if ((!label) || (is_error(label))) {
		json_object_put(root);
		return false;
	}

	r_value = json_object_get_string(label);
	json_object_put(root);
	return true;
}

bool extractFromJson(struct json_object * root,const std::string& key,std::string& r_value)
{
	if ((!root) || (is_error(root)) || (key.length() == 0))
		return false;
	
	struct json_object* label = 0;
	
	label = json_object_object_get(root,key.c_str());
	if ((!label) || (is_error(label))) {
		return false;
	}

	r_value = json_object_get_string(label);
	return true;
}

struct json_object * JsonGetObject(struct json_object * root,const std::string& key)
{
	if ((!root) || (is_error(root)) || (key.length() == 0))
		return NULL;

	struct json_object* label = 0;

	label = json_object_object_get(root,key.c_str());
	if ((!label) || (is_error(label))) {
		return NULL;
	}

	return label;
}

int postSubscriptionUpdate(const std::string& key,const std::string& postMessage,LSHandle * serviceHandlePublic,LSHandle * serviceHandlePrivate)
{
	if ((serviceHandlePublic == NULL) && (serviceHandlePrivate == NULL))
		return true;			//nothing to do 
	
	LSSubscriptionIter *iter=NULL;
	LSError lserror;
	LSErrorInit(&lserror);

	//acquire the subscription and reply.

	int rc=0;
	
	bool retVal = LSSubscriptionAcquire(serviceHandlePublic, key.c_str(), &iter, &lserror);
	if (retVal) {
		while (LSSubscriptionHasNext(iter)) {
			LSMessage *message = LSSubscriptionNext(iter);
			if (!LSMessageReply(serviceHandlePublic,message,postMessage.c_str(),&lserror)) {
				LSErrorPrint(&lserror,stderr);
				LSErrorFree(&lserror);
				//mark the return code bitfield to indicate at least one public bus failure
				rc |= ERRMASK_POSTSUBUPDATE_PUBLIC;
			}
		}

		LSSubscriptionRelease(iter);
	}
	else {
		LSErrorFree(&lserror);
	}

	LSErrorInit(&lserror);
	iter=NULL;
	retVal = LSSubscriptionAcquire(serviceHandlePrivate, key.c_str(), &iter, &lserror);
	if (retVal) {
		while (LSSubscriptionHasNext(iter)) {

			LSMessage *message = LSSubscriptionNext(iter);
			if (!LSMessageReply(serviceHandlePrivate,message,postMessage.c_str(),&lserror)) {
				LSErrorPrint(&lserror,stderr);
				LSErrorFree(&lserror);
				//mark the return code bitfield to indicate at least one private bus failure
				rc |= ERRMASK_POSTSUBUPDATE_PRIVATE;
			}
		}

		LSSubscriptionRelease(iter);
	}
	else {
		LSErrorFree(&lserror);
	}

	return rc;
}

bool processSubscription(LSHandle * serviceHandle, LSMessage * message,const std::string& key)
{
	
	if ((serviceHandle == NULL) || (message == NULL))
		return false;
	
	LSError lsError;
	LSErrorInit(&lsError);
	
	if (LSMessageIsSubscription(message)) {		
		if (!LSSubscriptionAdd(serviceHandle, key.c_str(),
				message, &lsError)) {
			LSErrorFree(&lsError);
			return false;
		}
		else 
			return true;
	}
	return false;
}

uint32_t removeSubscriptions(const std::string& key,LSHandle * serviceHandlePublic,LSHandle * serviceHandlePrivate)
{
	if (key.size() == 0)
		return false;
	
	if ((serviceHandlePublic == NULL) && (serviceHandlePrivate == NULL))
		return false;
	
	LSSubscriptionIter *iter=NULL;
	LSError lserror;
	LSErrorInit(&lserror);

	//acquire the subscription

	uint32_t rc=0;

	bool retVal = LSSubscriptionAcquire(serviceHandlePublic, key.c_str(), &iter, &lserror);
	if (retVal) {
		while (LSSubscriptionHasNext(iter)) {
			LSSubscriptionNext(iter);	//..or else remove won't work
			LSSubscriptionRemove(iter);
				++rc;
		}
		LSSubscriptionRelease(iter);
	}
	else {
		LSErrorFree(&lserror);
	}

	LSErrorInit(&lserror);
	iter=NULL;
	retVal = LSSubscriptionAcquire(serviceHandlePrivate, key.c_str(), &iter, &lserror);
	if (retVal) {
		while (LSSubscriptionHasNext(iter)) {
			LSSubscriptionNext(iter);	//..or else remove won't work
			LSSubscriptionRemove(iter);
			++rc;
		}
		LSSubscriptionRelease(iter);
	}
	else {
		LSErrorFree(&lserror);
	}

	return rc;
			
}

uint32_t countSubscribers(const std::string& key,LSHandle * serviceHandlePublic,LSHandle * serviceHandlePrivate)
{
	if ((serviceHandlePublic == NULL) && (serviceHandlePrivate == NULL))
		return 0;			//nothing to do 

	LSSubscriptionIter *iter=NULL;
	LSError lserror;
	LSErrorInit(&lserror);

	//acquire the subscription

	uint32_t rc=0;

	bool retVal = LSSubscriptionAcquire(serviceHandlePublic, key.c_str(), &iter, &lserror);
	if (retVal) {
		while (LSSubscriptionHasNext(iter)) {
			LSMessage *message = LSSubscriptionNext(iter);
			if (message != NULL)
				++rc;
		}
		LSSubscriptionRelease(iter);
	}
	else {
		LSErrorFree(&lserror);
	}

	LSErrorInit(&lserror);
	iter=NULL;
	retVal = LSSubscriptionAcquire(serviceHandlePrivate, key.c_str(), &iter, &lserror);
	if (retVal) {
		while (LSSubscriptionHasNext(iter)) {
			LSMessage *message = LSSubscriptionNext(iter);
			if (message != NULL)
				++rc;
		}
		LSSubscriptionRelease(iter);
	}
	else {
		LSErrorFree(&lserror);
	}

	return rc;
}

bool hasSubscribers(const std::string& key,LSHandle * serviceHandlePublic,LSHandle * serviceHandlePrivate)
{
	if ((serviceHandlePublic == NULL) && (serviceHandlePrivate == NULL))
			return false;			//nothing to do 

		LSSubscriptionIter *iter=NULL;
		LSError lserror;
		LSErrorInit(&lserror);

		bool retVal = LSSubscriptionAcquire(serviceHandlePublic, key.c_str(), &iter, &lserror);
		if (retVal) {
			while (LSSubscriptionHasNext(iter)) {
				LSMessage *message = LSSubscriptionNext(iter);
				if (message != NULL) {
					LSSubscriptionRelease(iter);
					return true;
				}
			}
			LSSubscriptionRelease(iter);
		}
		else {
			LSErrorFree(&lserror);
		}

		LSErrorInit(&lserror);
		iter=NULL;
		retVal = LSSubscriptionAcquire(serviceHandlePrivate, key.c_str(), &iter, &lserror);
		if (retVal) {
			while (LSSubscriptionHasNext(iter)) {
				LSMessage *message = LSSubscriptionNext(iter);
				if (message != NULL) {
					LSSubscriptionRelease(iter);
					return true;
				}
			}
			LSSubscriptionRelease(iter);
		}
		else {
			LSErrorFree(&lserror);
		}

		return false;
}

/*
 * returns 0 on error, >0 otherwise
 * 
 * Dangerous on its own! Doesn't check for baseDir validity so could technically overwrite things in sensitive places
 * (though unlikely since the temp filenames wont be anything like other, vital files on the filesystem(
 */
int createTempFile(const std::string& baseDir,const std::string& tag,const std::string& extension,std::string& r_fileAndPath)
{

	std::string templateStr = baseDir + std::string("/file_")+std::string(tag)+std::string("_XXXXXX");
	char * templateFileName = new char[templateStr.length()+2];
	strcpy(templateFileName,templateStr.c_str());
	int fd = mkstemp(templateFileName);
	templateStr = std::string(templateFileName);
	delete[] templateFileName;

	if (fd == -1) {
		return 0;
	}
	//write nothing and close, to assure file is on disk; this will prevent mkstemp from assigning the same name by chance (if the filename isn't used to create a file in the meantime)
	write (fd,templateStr.data(),0);	//.data() just used to avoid null param. arbitrary
	close(fd);
	r_fileAndPath = templateStr+extension;
	if (rename(templateStr.c_str(),r_fileAndPath.c_str()) == -1) {
		//error...unlink the base file
		unlink(templateStr.c_str());
		return 0;
	}
	return 1;
}

void string_to_lower(std::string& str)
{
	std::transform(str.begin(), str.end(), str.begin(), tolower);	
}

std::string string_printf(const char *format, ...)
{
    if (format == 0)
        return "";
    va_list args;
    va_start(args, format);
    char stackBuffer[1024];
    int result = vsnprintf(stackBuffer, G_N_ELEMENTS(stackBuffer), format, args);
    if (result > -1 && result < (int) G_N_ELEMENTS(stackBuffer))
    {   // stack buffer was sufficiently large. Common case with no temporary dynamic buffer.
        va_end(args);
        return std::string(stackBuffer, result);
    }

    int length = result > -1 ? result + 1 : G_N_ELEMENTS(stackBuffer) * 3;
    char * buffer = 0;
    do
    {
        if (buffer)
        {
            delete[] buffer;
            length *= 3;
        }
        buffer = new char[length];
        result = vsnprintf(buffer, length, format, args);
    } while (result == -1 && result < length);
    va_end(args);
    std::string str(buffer, result);
    delete[] buffer;
    return str;
}

std::string & append_format(std::string & str, const char * format, ...)
{
    if (format == 0)
        return str;
    va_list args;
    va_start(args, format);
    char stackBuffer[1024];
    int result = vsnprintf(stackBuffer, G_N_ELEMENTS(stackBuffer), format, args);
    if (result > -1 && result < (int) G_N_ELEMENTS(stackBuffer))
    {   // stack buffer was sufficiently large. Common case with no temporary dynamic buffer.
        va_end(args);
        str.append(stackBuffer, result);
        return str;
    }

    int length = result > -1 ? result + 1 : G_N_ELEMENTS(stackBuffer) * 3;
    char * buffer = 0;
    do
    {
        if (buffer)
        {
            delete[] buffer;
            length *= 3;
        }
        buffer = new char[length];
        result = vsnprintf(buffer, length, format, args);
    } while (result == -1 && result < length);
    va_end(args);
    str.append(buffer, result);
    delete[] buffer;
    return str;
}

} //end Utils namespace

