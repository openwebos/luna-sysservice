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

#include <ctime>

#include "BroadcastTime.h"

BroadcastTime::BroadcastTime() :
    m_type(None),
    m_stamp(0)
{}

bool BroadcastTime::set(time_t utc, time_t local, time_t stamp)
{
    // ensure that stamp only increases
    if (stamp < m_stamp) return false;

    time_t currentTime = time(0);
    m_type = UtcAndLocal;
    m_utcOffset = utc - currentTime;
    m_localOffset = local - currentTime;
    m_stamp = stamp;
    return true;
}

bool BroadcastTime::get(time_t &utc, time_t &local) const
{
    if (m_type == None) return false;
    time_t currentTime = time(0);
    utc = m_utcOffset + currentTime;
    local = m_localOffset + currentTime;
	return true;
}

bool BroadcastTime::adjust(time_t offset)
{
    if (m_type == None) return false;
    m_utcOffset -= offset;
    m_localOffset -= offset;
	return true;
}
