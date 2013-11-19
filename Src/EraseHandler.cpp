/**
 *  Copyright (c) 2002-2013 LG Electronics, Inc.
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


#include <sys/prctl.h>
#include <string>

//for basename()...
#include <string.h>
#include <map>
#include <cjson/json.h>

#include "EraseHandler.h"
#include "Logging.h"
#include "Utils.h"
#include "JSONUtils.h"
#include <nyx/client/nyx_system.h>

static bool    cbEraseAll(LSHandle* pHandle, LSMessage* pMessage, void* pUserData);
static bool    cbEraseDeveloper(LSHandle* pHandle, LSMessage* pMessage, void* pUserData);
static bool    cbEraseMedia(LSHandle* pHandle, LSMessage* pMessage, void* pUserData);
static bool    cbEraseVar(LSHandle* pHandle, LSMessage* pMessage, void* pUserData);
static bool    cbSecureWipe(LSHandle* pHandle, LSMessage* pMessage, void* pUserData);

#define ERASE(h,m,u) (EraseHandler::instance()->Erase(h,m,u))

EraseHandler* EraseHandler::s_instance = NULL;
nyx_device_handle_t EraseHandler::nyxSystem = NULL;

/*!
 * \page com_palm_systemservice_erase Service API com.palm.systemservice/erase/
 *
 * Private methods:
 *
 * - \ref com_palm_systemservice_EraseAll
 * - \ref com_palm_systemservice_EraseMedia
 * - \ref com_palm_systemservice_EraseVar
 * - \ref com_palm_systemservice_EraseDeveloper
 * - \ref com_palm_systemservice_Wipe
 */

/**
 * These are the methods that the erase service provides.
 */
static LSMethod s_methods[]  = {
     { "EraseAll",       cbEraseAll }
    ,{ "EraseMedia",     cbEraseMedia }
    ,{ "EraseVar",       cbEraseVar }
    ,{ "EraseDeveloper", cbEraseDeveloper }
    ,{ "Wipe",           cbSecureWipe }
    ,{ 0, 0 }
};


EraseHandler::EraseHandler()
: m_service(0)
{
}

/**
 * Initialize the erase handler.
 */
bool EraseHandler::init()
{
    if (!nyxSystem)
    {
        int ret = nyx_device_open(NYX_DEVICE_SYSTEM, "Main", &nyxSystem);

        if (ret != NYX_ERROR_NONE)
        {
            PmLogError(sysServiceLogContext(), "NYX_DEVICE_OPEN_FAIL", 1,
                    PMLOGKFV("NYX_ERROR", "%d", ret), "Unable to open the nyx device System");
            nyxSystem = 0;
            return false
        }
    }
    return true;
}

/**
 * Handles return of singleton instance
 */
EraseHandler* EraseHandler::instance()
{
    if (NULL == s_instance) {
        s_instance = new EraseHandler();
    }

    return s_instance;
}

/**
 * Only listen on the private bus
 */
void EraseHandler::setServiceHandle(LSPalmService* service)
{
    if (!service) {
        qCritical() << "NULL parameter";
        return;
    }
    m_service = service;
    m_serviceHandlePrivate = LSPalmServiceGetPrivateConnection(m_service);

    bool result;
    LSError lsError;
    LSErrorInit(&lsError);

    result = LSRegisterCategory(m_serviceHandlePrivate, "/erase", s_methods,
               NULL, NULL, &lsError);
    if (!result) {
        LSREPORT(lsError);
        LSErrorFree(&lsError);
    }
    return;
}

EraseHandler::~EraseHandler()
{
}

/**
 * @brief Erase
 *
 * Set the run level, which executes the reset scripts.  These
 * scripts bring the system down cleanly, then reboot into the
 * mountall script that erases /var or both /var and the user
 * partition.
 *
 * @param pHandle
 * @param pMessage
 * @param type
 */
bool EraseHandler::Erase(LSHandle* pHandle, LSMessage* pMessage, EraseType_t type)
{
    if(!m_service) {
        qCritical() << "EraseHandler called before it has been initialized";
        return false;
    }
    if(!nyxSystem) {
        PmLogWarning(sysServiceLogContext(), "ERASE_BROKEN", 1, PMLOGKFV("ERASE_TYPE", "%d", type), "Call for erase when no working provider available");
        return false;
    }
    LSError lserror;
    char *error_text=NULL;
    char* return_msg = NULL;
    nyx_system_erase_type_t nyx_type;

    // write flag file used by mountall.sh
    switch (type)
    {
        case kEraseVar:
            qDebug("System erase partition type: \'var\'");
            nyx_type = NYX_SYSTEM_ERASE_VAR;
            break;

        case kEraseAll:
            qDebug("System erase partition type:  \'all\'");
            nyx_type = NYX_SYSTEM_ERASE_ALL;
            break;

        case kEraseMedia:
            qDebug("System erase partition type:  \'media\'");
            nyx_type = NYX_SYSTEM_ERASE_MEDIA;
            break;

        case kEraseMDeveloper:
            qDebug("System erase partition type:  \'developer\'");
            nyx_type =  NYX_SYSTEM_ERASE_DEVELOPER;
            break;

        case kSecureWipe:
            qDebug("System erase partition type:  \'secure wipe\'");
            nyx_type = NYX_SYSTEM_WIPE;
            break;

        default:
            error_text = g_strdup_printf("Invalid type %d", type);
            break;
    }

    if (!error_text) {
        nyx_error_t ret = NYX_ERROR_NONE;
        ret = nyx_system_erase_partition(nyxSystem, nyx_type/*, error_text*/);
        if (ret != NYX_ERROR_NONE) {
            qCritical("Failed to execute nyx_system_erase_partition, ret : %d",ret);
            error_text = g_strdup_printf("Failed to execute NYX erase API");
        }
    }

    if (error_text) {
        qWarning() << error_text;
        return_msg = g_strdup_printf("{\"returnValue\":false, \"errorText\":\"%s\"}", error_text);
        g_free(error_text);
    } else {
        return_msg = g_strdup_printf("{\"returnValue\":true}");
    }

    LSErrorInit(&lserror);
    if (!LSMessageReply(pHandle, pMessage, return_msg, &lserror))
        LSREPORT( lserror );
    g_free(return_msg);
    LSErrorFree(&lserror);
    return true;
}

/**
 * @brief handle_erase_var
 *
 * @param pHandle
 * @param pMessage
 * @param pUserData
 *
 * @return
 */
bool cbEraseVar(LSHandle* pHandle, LSMessage* pMessage, void* pUserData)
{
    PMLOG_TRACE("%s:starting",__FUNCTION__);
    if (LSMessageIsHubErrorMessage(pMessage)) {  // returns false if message is NULL
        qWarning("The message received is an error message from the hub");
        return true;
    }
    else return ERASE(pHandle, pMessage, EraseHandler::kEraseVar);
}

/**
 * @brief handle_erase_all
 *
 * @param pHandle
 * @param pMessage
 * @param pUserData
 *
 * @return
 */
bool cbEraseAll(LSHandle* pHandle, LSMessage* pMessage, void* pUserData)
{
    PMLOG_TRACE("%s:starting",__FUNCTION__);
    if (LSMessageIsHubErrorMessage(pMessage)) {  // returns false if message is NULL
        qWarning("The message received is an error message from the hub");
        return true;
    }
    else return ERASE(pHandle, pMessage, EraseHandler::kEraseAll);
}

/**
 * @brief handle_erase_media
 *
 * @param pHandle
 * @param pMessage
 * @param pUserData
 *
 * @return
 */
bool cbEraseMedia(LSHandle* pHandle, LSMessage* pMessage, void* pUserData)
{
    PMLOG_TRACE("%s:starting",__FUNCTION__);
    if (LSMessageIsHubErrorMessage(pMessage)) {  // returns false if message is NULL
        qWarning("The message received is an error message from the hub");
        return true;
    }
    else return ERASE(pHandle, pMessage, EraseHandler::kEraseMedia);
}

/**
 * @brief handle_erase_developer
 *
 * @param pHandle
 * @param pMessage
 * @param pUserData
 *
 * @return
 */
bool cbEraseDeveloper(LSHandle* pHandle, LSMessage* pMessage, void* pUserData)
{
    PMLOG_TRACE("%s:starting",__FUNCTION__);
    if (LSMessageIsHubErrorMessage(pMessage)) {  // returns false if message is NULL
        qWarning("The message received is an error message from the hub");
        return true;
    }
    else return ERASE(pHandle, pMessage, EraseHandler::kEraseMDeveloper);
}

/**
 * @brief handle_secure_wipe
 *
 * @param pHandle
 * @param pMessage
 * @param pUserData
 *
 * @return
 */
bool cbSecureWipe(LSHandle* pHandle, LSMessage* pMessage, void* pUserData)
{
    PMLOG_TRACE("%s:starting",__FUNCTION__);
    if (LSMessageIsHubErrorMessage(pMessage)) {  // returns false if message is NULL
        qWarning("The message received is an error message from the hub");
        return true;
    }
    else return ERASE(pHandle, pMessage, EraseHandler::kSecureWipe);
}

