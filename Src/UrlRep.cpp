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


#include <uriparser/Uri.h> 
#include "UrlRep.h"

#define URI_TEXT_RANGE_TO_STRING(textRange)			   					  \
	textRange.first ? std::string(textRange.first,						  \
								  textRange.afterLast - textRange.first)  \
	: std::string()

UrlRep UrlRep::fromUrl(const char* uri)
{
	UrlRep urlRep;

	if (!uri)
		return urlRep;
	
	UriParserStateA state;
	UriUriA uriA;
	UriQueryListA* queryList;
	int queryCount;

	state.uri = &uriA;
	uriParseUriA(&state, uri);

	urlRep.query.clear();
	
	urlRep.scheme = URI_TEXT_RANGE_TO_STRING(uriA.scheme);
	urlRep.userInfo = URI_TEXT_RANGE_TO_STRING(uriA.userInfo);
	urlRep.host = URI_TEXT_RANGE_TO_STRING(uriA.hostText);
	urlRep.port = URI_TEXT_RANGE_TO_STRING(uriA.portText);
	urlRep.fragment = URI_TEXT_RANGE_TO_STRING(uriA.fragment);

	UriPathSegmentA* tmpPath = uriA.pathHead;
	while (tmpPath) {
		if (tmpPath == uriA.pathTail) {
			urlRep.pathOnly = urlRep.path;
		}
		urlRep.path += "/";
		urlRep.path += URI_TEXT_RANGE_TO_STRING(tmpPath->text);
		tmpPath = tmpPath->next;
	}

	if (uriA.pathTail) {
		urlRep.resource = URI_TEXT_RANGE_TO_STRING((uriA.pathTail)->text);
	}
	
	if (uriA.query.first) {
		uriDissectQueryMallocA(&queryList, &queryCount,
							   uriA.query.first,
							   uriA.query.afterLast);

		UriQueryListA* tmpQueryList = queryList;
		while (tmpQueryList) {
			if (tmpQueryList->key) {
				urlRep.query[tmpQueryList->key] = tmpQueryList->value ?
												  tmpQueryList->value : std::string();
			}
			tmpQueryList = tmpQueryList->next;
		}

		uriFreeQueryListA(queryList);
	}
	
	uriFreeUriMembersA(&uriA);	       

	urlRep.resource = unescape(urlRep.resource);
	urlRep.path = unescape(urlRep.path);
	urlRep.pathOnly = unescape(urlRep.pathOnly);
	
	//TODO: maybe a more stringent check on validity???
	urlRep.valid = true;
	return urlRep;
}

std::string unescape(const std::string& encoded) {
	
	char * buffer = new char[strlen(encoded.c_str())+1];
	strcpy(buffer,encoded.c_str());
	uriUnescapeInPlaceA(buffer);
	std::string r(buffer);
	delete[] buffer;
	return r;
}

std::string escape(const std::string& source) {
	
	if (source.length() == 0)
		return std::string("");
	char * buffer = new char[6*(source.length())+1];
	char * term = uriEscapeA(source.c_str(),buffer,false,false);
	if (term == NULL)
		return std::string("");
	std::string r(buffer);
	delete[] buffer;
	return r;
}

  

