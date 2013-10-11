/**
 *  Copyright (c) 2013 LG Electronics, Inc.
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

#ifndef __BroadcastTime_h__
#define __BroadcastTime_h__

#include <time.h>

/**
 * @class Maintains information about broadcast-time time tied with system time
 */
class BroadcastTime
{
    enum { None, UtcAndLocal } m_type;

    time_t m_utcOffset;
    time_t m_localOffset;

public:
    BroadcastTime();

    /**
     * Tie up new broadcast time to system clocks
     */
    void set(time_t utc, time_t local);

    /**
     * Retrieve estimated broadcast time
     */
    bool get(time_t &utc, time_t &local) const;

    /**
     * Notify about system-time moving forward/backward
     *
     * @param offset from old value (i.e. positive means times moves forward)
     */
    bool adjust(time_t offset);

    bool avail() const
    { return m_type != None; }
};

#endif
