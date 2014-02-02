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


/* ============================================================
 * Date  : 2010-03-31
 *
 * Partially based on localtime.c from tzcode distribution. Licensing
 * for it follows:
 * 
 * This file is in the public domain, so clarified as of
 * 1996-06-05 by Arthur David Olson.
 * ============================================================ */

#include <string>
#include <vector>

#include <stdio.h>
#include <stdint.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include "TzParser.h"

//#define TRACE 1

#undef DBG
#if defined(TRACE)
#define DBG(...) printf(__VA_ARGS__)
#else
#define DBG(...) (void) 0
#endif

#define TZ_MAGIC "TZif"
#define TZ_ABBR_CHAR_SET "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 :+-._"
#define TZ_ABBR_ERR_CHAR  '_'

#define TYPE_SIGNED(type) (((type) -1) < 0)
#define TYPE_INTEGRAL(type) (((type) 0.5) != 0.5)

struct tzhead {
	char    tzh_magic[4];       /* TZ_MAGIC */
	char    tzh_version[1];     /* '\0' or '2' as of 2005 */
	char    tzh_reserved[15];   /* reserved--must be zero */
	char    tzh_ttisgmtcnt[4];  /* coded number of trans. time flags */
	char    tzh_ttisstdcnt[4];  /* coded number of trans. time flags */
	char    tzh_leapcnt[4];     /* coded number of leap seconds */
	char    tzh_timecnt[4];     /* coded number of transition times */
	char    tzh_typecnt[4];     /* coded number of local time types */
	char    tzh_charcnt[4];     /* coded number of abbr. chars */
};

struct ttinfo {
	time_t gmtOffset;
	bool   isDst;
	int    abbrIndex;
};

struct ttentry {	
	time_t time;
	int    indexToLocalTime;
};


typedef std::vector<ttentry> ttentrylist;
typedef std::vector<ttinfo>  ttinfolist;

static long
detzcode(const char* codep)
{
	register long   result;
	register int    i;

	result = (codep[0] & 0x80) ? ~0L : 0;
	for (i = 0; i < 4; ++i)
		result = (result << 8) | (codep[i] & 0xff);
	return result;
}

static time_t
detzcode64(const char* codep)
{
	register time_t	result;
	register int	i;

	result = (codep[0] & 0x80) ?  (~(int64_t) 0) : 0;
	for (i = 0; i < 8; ++i)
		result = result * 256 + (codep[i] & 0xff);
	return result;
}

TzTransitionList parseTimeZone(const char* tzName)
{
	static const char* zoneInfoDir = "/usr/share/zoneinfo/";
	static const char* etcZoneInfoDir = "/usr/share/zoneinfo/Etc/";
	std::string filePath = zoneInfoDir;
	filePath += tzName;
	
	struct stat stBuf;
	FILE* fp = fopen(filePath.c_str(), "r");
	if (!fp && errno == ENOENT)
	{
		// if file not found - try alternative filePath
		printf("Failed to find file: %s\n", filePath.c_str());

		filePath = etcZoneInfoDir;
		filePath += tzName;

		fp = fopen(filePath.c_str(), "r");
		if (!fp && errno == ENOENT)
		{
			printf("Failed to find second try file: %s\n", filePath.c_str());
			return TzTransitionList();
		}
	}
	if (fp)
	{
		if (fstat(fileno(fp), &stBuf) != 0)
		{
			printf("Failed to stat opened file: %s\n", filePath.c_str());
			fclose(fp);
			return TzTransitionList();
		}

		if (stBuf.st_size <= (int) sizeof(tzhead)) {
			printf("file too short to be a tz file: %s\n", filePath.c_str());
			fclose(fp);
			return TzTransitionList();
		}
	}
	else
	{
		printf("Failed to open file: %s\n", filePath.c_str());
		return TzTransitionList();
	}	

	char* buf = (char*) malloc(stBuf.st_size);
	size_t size = fread(buf, 1, stBuf.st_size, fp);
	fclose(fp);

	if (size != (size_t) stBuf.st_size) {
		printf("Short read on file: %s\n", filePath.c_str());
		free(buf);
		return TzTransitionList();
	}

	ttentrylist       ttEntryList;
	ttinfolist        ttInfoList;
	std::vector<char> ttAbbrList;

	/*
	  The  time zone information files used by tzset(3) begin with the
	  magic characters "TZif" to identify then as time zone information
	  files, followed by sixteen bytes reserved for future use,
	  followed by six four-byte values of type long, written in  a  "standard"
	  byte order (the high-order byte of the value is written first).
	  These values are, in order:

       tzh_ttisgmtcnt
              The number of UTC/local indicators stored in the file.

       tzh_ttisstdcnt
              The number of standard/wall indicators stored in the file.

       tzh_leapcnt
              The number of leap seconds for which data is stored in the file.

       tzh_timecnt
              The number of "transition times" for which data is stored in the file.

       tzh_typecnt
              The number of "local time types" for which data is stored in the file (must not be zero).

       tzh_charcnt
              The number of characters of "time zone abbreviation strings" stored in the file.
	*/
	
	long leapCnt = 0;
	long timeCnt = 0;
	long typeCnt = 0;	
	long charCnt = 0;

	(void) leapCnt;
	(void) timeCnt;
	(void) typeCnt;
	(void) charCnt;
	
	int index = 0;
	for (int stored = 4; stored <= 8; stored *= 2) {

		DBG("-----------------------------------------------------\n");

		if (memcmp(buf, TZ_MAGIC, 4) != 0) {
			printf("Not a tz file. Header signature mismatch: %s\n", filePath.c_str());
			free(buf);
			return TzTransitionList();
		}
		
		struct tzhead* head = (struct tzhead*) (buf + index);

		leapCnt = detzcode(head->tzh_leapcnt);
		timeCnt = detzcode(head->tzh_timecnt);
		typeCnt = detzcode(head->tzh_typecnt);
		charCnt = detzcode(head->tzh_charcnt);

		ttInfoList.clear();
		ttInfoList.reserve(timeCnt);

		ttEntryList.clear();
		ttEntryList.reserve(typeCnt);

		ttAbbrList.clear();
		ttAbbrList.reserve(charCnt + 1);

		index += sizeof(struct tzhead);
	
		DBG("tzh_ttisgmtcnt: %ld\n", detzcode(head->tzh_ttisgmtcnt));
		DBG("tzh_ttisstdcnt: %ld\n", detzcode(head->tzh_ttisstdcnt));
		DBG("tzh_leapcnt: %ld\n", leapCnt);
		DBG("tzh_timecnt: %ld\n", timeCnt);
		DBG("tzh_typecnt: %ld\n", typeCnt);
		DBG("tzh_charcnt: %ld\n", charCnt);

		/* The  above  header  is followed by tzh_timecnt four-byte values of type
		   long, sorted in ascending order.  These values are  written  in  "standard"
		   byte  order.   Each is used as a transition time (as returned by
		   time(2)) at which the rules for computing local time change. */
		for (long i = 0; i < timeCnt; i++) {
			time_t time;
			time = (stored == 4) ? detzcode(buf + index) : detzcode64(buf + index);
			index += stored;
			
			ttentry e;
			e.time = time;
			ttEntryList.push_back(e);

			DBG("tzh_timecnt: Time: %ld, %s", time, asctime(gmtime(&time)));			
		}

		/* Next come tzh_timecnt one-byte values of type unsigned char;
		   each one tells which of the different types of "local time"
		   types described in the file is associated with the same-indexed
		   transition time. */
		for (long i = 0; i < timeCnt; i++) {
			unsigned char indexToLocalTime = (unsigned char) buf[index];
			index++;

			ttEntryList[i].indexToLocalTime = indexToLocalTime;
			
			DBG("tzh_timecnt: Index: %d\n", indexToLocalTime);			
		}

		/* These values serve as indices into an  array  of
		   ttinfo structures that appears next in the file; these structures are
		   defined as follows:

           struct ttinfo {
		   long         tt_gmtoff;
		   int          tt_isdst;
		   unsigned int tt_abbrind;
           };

		   Each  structure is written as a four-byte value for tt_gmtoff of type
		   long, in a standard byte order, followed by a one-byte value
		   for tt_isdst and a one-byte value for tt_abbrind.  In each structure,
		   tt_gmtoff gives the number of seconds to be  added  to  UTC,
		   tt_isdst  tells  whether  tm_isdst  should  be  set by localtime(3),
		   and tt_abbrind serves as an index into the array of time zone
		   abbreviation characters that follow the ttinfo structure(s) in the file. */
		for (long i = 0; i < typeCnt; i++) {

			ttinfo ttInfo;
			ttInfo.gmtOffset = detzcode(buf + index);
			index += 4;
			ttInfo.isDst = (unsigned char) buf[index];
			index++;
			ttInfo.abbrIndex = (unsigned char) buf[index];
			index++;

			ttInfoList.push_back(ttInfo);

			DBG("gmtOff: %ld, isDst: %d, abbrind: %d\n",
				ttInfo.gmtOffset, ttInfo.isDst, ttInfo.abbrIndex);
		}

		DBG("abbrev: ");
		for (long i = 0; i < charCnt; i++) {
			DBG("%c", buf[index]);
			ttAbbrList.push_back(buf[index]);
			index++;
		}
		ttAbbrList.push_back(0);
		DBG("\n");

		/* Then there are tzh_leapcnt pairs of four-byte values,
		   written in standard byte order; the first value of each
		   pair gives the  time (as  returned by time(2)) at which a leap
		   second occurs; the second gives the total number of leap seconds
		   to be applied after the given time.  The pairs of values are
		   sorted in ascending order by time. */
		for (long i = 0; i < leapCnt; i++) {

			long leapTime = (stored == 4) ? detzcode(buf + index)
							: detzcode64(buf + index);
			index += stored;

			long leapSeconds = detzcode(buf + index);
			index += 4;

			(void) leapTime;
			(void) leapSeconds;

			DBG("leapTime: %ld, leapSeconds: %ld\n",
				leapTime, leapSeconds);
		}

		/*
		  Then there are tzh_ttisstdcnt standard/wall indicators, each
		  stored as a one-byte value; they tell whether the  transition
		  times associated with local time types were specified as standard
		  time or wall clock time, and are used when a time zone file is
		  used in handling POSIX-style time zone environment variables.
		*/
		for (long i = 0; i < typeCnt; i++) {

			int standardOrWallClock = (unsigned char) buf[index];
			index++;

			(void) standardOrWallClock;
			DBG("standardOrWallClock: %d\n", standardOrWallClock);
		}
	

		/*
		  Finally, there are tzh_ttisgmtcnt UTC/local indicators,
		  each stored as a one-byte value; they tell whether  the
		  transition  times associated  with  local  time  types  were
		  specified as UTC or local time, and are used when a time zone
		  file is used in handling POSIX-style time zone environment variables.
		*/
		for (long i = 0; i < typeCnt; i++) {

			int utcOrLocalTime = (unsigned char) buf[index];
			index++;

			(void) utcOrLocalTime;
			DBG("utcOrLocalTime: %d\n", utcOrLocalTime);
		}

		/*
		  Localtime uses the first standard-time ttinfo structure in the file
		  (or simply the first ttinfo structure  in  the  absence  of  a
		  standard-time structure) if either tzh_timecnt is zero or the time
		  argument is less than the first transition time recorded in the
		  file.
		*/

		/*
		 * Out-of-sort ats should mean we're running on a
		 * signed time_t system but using a data file with
		 * unsigned values (or vice versa).
		 */
		for (int i = 0; i < timeCnt - 2; ++i) {
			if (ttEntryList[i].time > ttEntryList[i+1].time) {
				++i;
				if (TYPE_SIGNED(time_t)) {
					// Ignore the end (easy).
					timeCnt = i;
				} else {
					// Ignore the beginning (harder).
					int j;
					for (j = 0; (j + i) < timeCnt; ++j) {
						ttEntryList[j] = ttEntryList[j+i];
					}
					timeCnt = j;
				}
				break;
			}
		}

		/*
		 * If this is an old file, we're done.
		 */
		if (head->tzh_version[0] == '\0') {
			DBG("Version 0 file. breaking\n");
			break;
		}

		/*
		 * If this is a narrow integer time_t system, we're done.
		 */
		if ((stored >= (int) sizeof(time_t)) && (TYPE_INTEGRAL(time_t))) {
			DBG("narrow integer time_t system. breaking\n");
			break;
		}
	}
	
	DBG("Total Buffer size parsed: %d\n", index);
	
	free(buf);

	// Dummy entry for standardized timezones which never had
	// a transition time
	if (ttEntryList.empty() && !ttInfoList.empty()) {
		ttentry e;
		timeCnt = 1;
		e.time = -2147483648UL;
		e.indexToLocalTime = 0;
		ttEntryList.push_back(e);		
	}

	TzTransitionList result;
	for (int i = 0; i < timeCnt; i++) {
		const ttentry& entry = ttEntryList[i];
		const ttinfo& info   = ttInfoList[entry.indexToLocalTime];
		struct tm* gmTime    = gmtime(&entry.time);
		
		TzTransition trans;
		trans.time        = entry.time;
		trans.utcOffset   = info.gmtOffset;
		trans.isDst       = info.isDst;
		trans.year        = gmTime->tm_year + 1900;
		trans.abbrName[0] = 0;

		int j = info.abbrIndex;
		int k = 0;
		for (; (j < (int) ttAbbrList.size()) && (k < TZ_ABBR_MAX_LEN);
			 j++, k++) {
			trans.abbrName[k] = ttAbbrList[j];
			if (ttAbbrList[j] == 0)
				break;
		}
		trans.abbrName[TZ_ABBR_MAX_LEN-1] = 0;

		result.push_back(trans);
	}

	return result;
}

/*
int main(int argc, char** argv)
{
	if (argc < 2) {
		printf("Usage: tzparse <tzname>\n");
		return -1;
	}

	TzTransitionList result = parseTimeZone(argv[1]);
	for (TzTransitionList::const_iterator it = result.begin();
		 it != result.end(); ++it) {

		printf("--------------------------------------------------------------------------------\n");
		printf("year: %d, time: %ld, utcOffset: %ld, isDst: %s, name: '%s'\n",
			   it->year, it->time, it->utcOffset, it->isDst ? "true" : "false",
			   it->abbrName);
			   

	}

	return 0;
}
*/
