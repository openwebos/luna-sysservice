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


#ifndef ERASE_HANDLER_H
#define ERASE_HANDLER_H

#include <string>
#include <list>
#include <map>
#include <luna-service2/lunaservice.h>
#include <nyx/nyx_client.h>

struct LSHandle;
struct LSMessage;

class EraseHandler
{
public:

    typedef enum EraseType
    {
         kEraseVar
        ,kEraseAll
        ,kEraseMedia
        ,kEraseMDeveloper
        ,kSecureWipe
    } EraseType_t;


    static EraseHandler*    instance();
    void    setServiceHandle(LSPalmService* service);
    bool    Erase(LSHandle* pHandle, LSMessage* pMessage, EraseType_t type);


private:
    EraseHandler    ();
    ~EraseHandler   ();

    bool    init();


    static LSMethod    s_EraseServerMethods[];
    static EraseHandler* s_instance;

    static nyx_device_handle_t nyxSystem;

    LSPalmService* m_service;
    LSHandle* m_serviceHandlePrivate;
};


#endif // ERASE_HANDLER_H
