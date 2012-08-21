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


#ifndef LOGGING_H
#define LOGGING_H

#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

bool LunaChannelEnabled(const char* channel);

#ifndef NO_LOGGING

#define luna_log(channel, ...)                                              \
do {                                                                                \
    if (LunaChannelEnabled(channel)) {                                              \
	   fprintf(stdout, "LOG<%s>:(%s:%d) ", channel, __PRETTY_FUNCTION__, __LINE__);  \
       fprintf(stdout, __VA_ARGS__);                                        \
       fprintf(stdout, "\n");                                                       \
    }                                                                               \
} while(0)

#define luna_warn(channel, ...)                                             \
do {                                                                                \
    if (LunaChannelEnabled(channel)) {                                              \
	   fprintf(stdout, "WARN<%s>:(%s:%d) ", channel, __PRETTY_FUNCTION__, __LINE__); \
       fprintf(stdout, __VA_ARGS__);                                        \
       fprintf(stdout, "\n");                                                       \
    }                                                                               \
} while(0)
	
#else // NO_LOGGING

#define luna_log(channel, ...) (void)0
#define luna_warn(channel, ...) (void)0

#endif // NO_LOGGING

#define luna_critical(channel, ...)                                         \
do {                                                                                \
   fprintf(stdout, "CRITICAL<%s>:(%s:%d) ", channel, __PRETTY_FUNCTION__, __LINE__); \
   fprintf(stdout, __VA_ARGS__);                                            \
   fprintf(stdout, "\n");                                                           \
} while(0)
	
#ifdef __cplusplus
}
#endif	

#endif /* LOGGING_H */
