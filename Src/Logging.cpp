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


#include <glib.h>
#include <stdlib.h>
#include <unistd.h>

#include "Logging.h"

static GStaticMutex s_mutex       = G_STATIC_MUTEX_INIT;
static bool         s_initialized = false;
static GHashTable*  s_channelHash = 0;

bool LunaChannelEnabled(const char* channel)
{
	if (!channel)
		return false;

	bool ret = false;
	
	g_static_mutex_lock(&s_mutex);
		
	if (!s_initialized) {

		s_initialized = true;
		int index = 0;

		s_channelHash = ::g_hash_table_new(g_str_hash, g_str_equal);
		
		const char* env = ::getenv("LUNA_LOGGING");
		if (!env)
			goto Done;
		
		gchar** splitStr = ::g_strsplit(env, ",", 0);
		if (!splitStr)
			goto Done;

		while (splitStr[index]) {
			char* key = ::g_strdup(splitStr[index]);
			key = g_strstrip(key);
			g_hash_table_insert(s_channelHash, key, (gpointer)0x1);
			index++;
		}
	}

	if (g_hash_table_lookup(s_channelHash, channel))
		ret = true;

 Done:	

	g_static_mutex_unlock(&s_mutex);

	return ret;
}
