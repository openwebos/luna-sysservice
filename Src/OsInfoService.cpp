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

#include <cjson/json.h>

#include "OsInfoService.h"
#include "Logging.h"

OsInfoService::command_map_t OsInfoService::s_commandMap;

LSMethod s_os_methods[]  = {
	{ "query",  OsInfoService::cbGetOsInformation },
	{ 0, 0 },
};

/*! \page com_palm_os_info_service Service API com.palm.systemservice/osInfo/
 *
 *  Public methods:
 *   - \ref os_info_query
 */
OsInfoService* OsInfoService::instance()
{
	static OsInfoService* s_instance = 0;
	if (G_UNLIKELY(s_instance == 0))
	{
		OsInfoService::initCommandMap();
		s_instance = new OsInfoService;
	}

	return s_instance;
}

OsInfoService::OsInfoService(): m_service(0)
{
}

OsInfoService::~OsInfoService()
{
    // NO-OP
}

void OsInfoService::initCommandMap()
{
	s_commandMap["core_os_kernel_config"] = NYX_OS_INFO_CORE_OS_KERNEL_CONFIG; // Return Core OS kernel config
	s_commandMap["core_os_kernel_version"] = NYX_OS_INFO_CORE_OS_KERNEL_VERSION; // Return Core OS kernel version info
	s_commandMap["core_os_name"] = NYX_OS_INFO_CORE_OS_NAME; // Return Core OS name
	s_commandMap["core_os_release"] = NYX_OS_INFO_CORE_OS_RELEASE; // Return Core OS release info
	s_commandMap["core_os_release_codename"] = NYX_OS_INFO_CORE_OS_RELEASE_CODENAME; // Return Core OS release codename
	s_commandMap["webos_api_version"] = NYX_OS_INFO_WEBOS_API_VERSION; // Return webOS API version
	s_commandMap["webos_build_id"] = NYX_OS_INFO_WEBOS_BUILD_ID; // Return webOS build ID
	s_commandMap["webos_imagename"] = NYX_OS_INFO_WEBOS_IMAGENAME; // Return webOS imagename
	s_commandMap["webos_name"] = NYX_OS_INFO_WEBOS_NAME; // Return webOS name
	s_commandMap["webos_prerelease"] = NYX_OS_INFO_WEBOS_PRERELEASE; // Return webOS prerelease info
	s_commandMap["webos_release"] = NYX_OS_INFO_WEBOS_RELEASE; // Return webOS release info
	s_commandMap["webos_release_codename"] = NYX_OS_INFO_WEBOS_RELEASE_CODENAME; // Return webOS release codename
	s_commandMap["webos_manufacturing_version"] = NYX_OS_INFO_MANUFACTURING_VERSION; // Return webOS manufacting version

}

void OsInfoService::setServiceHandle(LSPalmService* service)
{
	m_service = service;

	LSError lsError;
	LSErrorInit(&lsError);

	bool result = LSPalmServiceRegisterCategory(m_service, "/osInfo",
												s_os_methods, NULL,
												NULL, this, &lsError);
	if (!result) {
		qCritical() << "Failed in registering osinfo handler method:" << lsError.message;
		LSErrorFree(&lsError);
		return;
	}
}

LSPalmService* OsInfoService::serviceHandle() const
{
	return m_service;
}

/*!
\page com_palm_os_info_service
\n
\section os_info_query query

\e Public.

com.palm.systemservice/osInfo/query

\subsection os_info_query_syntax Syntax:
\code
{
     "parameters": [string array]
}
\endcode

\param parameters List of requested parameters. If not specified, all available parameters wiil be returned. 

\subsection os_info_query_return Returns:
\code
{
    "returnValue": boolean,
    "errorCode": string
    "core_os_kernel_config": string
    "core_os_kernel_version": string
    "core_os_name": string
    "core_os_release": string
    "core_os_release_codename": string
    "webos_api_version": string
    "webos_build_id": string
    "webos_imagename": string
    "webos_name": string
    "webos_prerelease": string
    "webos_release": string
    "webos_release_codename": string
    "webos_manufacturing_version": string
}
\endcode

\param returnValue Indicates if the call was succesful.
\param errorCode Description of the error if call was not succesful.
\param core_os_kernel_config Core OS kernel config
\param core_os_kernel_version Core OS kernel version info
\param core_os_name Core OS name
\param core_os_release Core OS release info
\param core_os_release_codename Core OS release codename
\param webos_api_version webOS API version
\param webos_build_id webOS build ID
\param webos_imagename webOS imagename
\param webos_name webOS name
\param webos_prerelease webOS prerelease info
\param webos_release webOS release info
\param webos_release_codename webOS release codename
\param webos_manufacturing_version webOS manufacting version

\subsection os_info_query_examples Examples:
\code
luna-send-pub -n 1 -f luna://com.palm.systemservice/osInfo/query '{"parameters":["core_os_name", "webos_release"]}'
\endcode

Example response for a succesful call:
\code
{
    "core_os_name": "Linux",
    "webos_release": "0.10",
    "returnValue": true
}
\endcode

Example response for a failed call:
\code
{
    "errorCode": "Cannot parse json payload"
    "returnValue": false,
}
\endcode
*/
bool OsInfoService::cbGetOsInformation(LSHandle* lsHandle, LSMessage *message, void *user_data)
{
	std::string reply;
	std::string parameter;
	const char *nyx_result = NULL;
	bool is_parameters_verified = false; // Becomes `true` if we've formed parameter ourself.
	LSError lsError;

	json_object *payload = NULL;
	json_object *payloadParameterList = NULL;
	json_object *jsonResult = json_object_new_object();

	LSErrorInit(&lsError);

	nyx_error_t error = NYX_ERROR_GENERIC;
	nyx_device_handle_t device = NULL;

	const char *payload_data = LSMessageGetPayload(message);
	if (!payload_data) {
		reply = "{\"returnValue\": false, "
			" \"errorText\": \"No payload specifed for message\"}";
		goto Done;
	}

	payload = json_tokener_parse(payload_data);
	if (!payload || is_error(payload) || !json_object_is_type(payload, json_type_object)) {
		reply = "{\"returnValue\": false, "
		        " \"errorText\": \"Cannot parse/validate json payload\"}";
		goto Done;
	}

	if (json_object_object_get_ex(payload, "parameters", &payloadParameterList))
	{
		if (!payloadParameterList || !json_object_is_type(payloadParameterList, json_type_array)) {
			reply = "{\"returnValue\": false, "
				" \"errorText\": \"`parameters` needs to be an array\"}";
			goto Done;
		}
	}
	else
	{
		// No parameters. Fill array with all available parameters from the s_commandMap.
		is_parameters_verified = true;
		payloadParameterList = json_object_new_array();
		for (command_map_t::iterator it = s_commandMap.begin(); it != s_commandMap.end(); ++it)
		{
			json_object_array_add(payloadParameterList, json_object_new_string(it->first.c_str()));
		}
	}

	error = nyx_init();
	if (NYX_ERROR_NONE != error)
	{
		qCritical() << "Failed to inititalize nyx library: " << error;
		reply = "{\"returnValue\": false, "
			" \"errorText\": \"Can not initialize nyx\"}";
		goto Done;
	}

	error = nyx_device_open(NYX_DEVICE_OS_INFO, "Main", &device);
	if ((NYX_ERROR_NONE != error) || (NULL == device))
	{
		qCritical() << "Failed to get `Main` nyx device: " << error << "";
		reply = "{\"returnValue\": false, "
			" \"errorText\": \"Internal error. Can't open nyx device\"}";
		goto Done;
	}

	for (int i = 0; i < json_object_array_length(payloadParameterList); i++)
	{
		parameter = json_object_get_string(json_object_array_get_idx(payloadParameterList, i));
		command_map_t::iterator query = s_commandMap.find(parameter);

		if (!is_parameters_verified && query == s_commandMap.end())
		{
			reply = "{\"returnValue\": false, "
				" \"errorText\": \"Invalid parameter: " + parameter + "\"}";
			goto Done;
		}

		error = nyx_os_info_query(device, query->second, &nyx_result);
		if (NYX_ERROR_NONE != error)
		{
			qCritical() << "Failed to query nyx. Parameter: " << parameter.c_str() << ". Error: " << error;
			reply = "{\"returnValue\": false, "
				" \"errorText\": \"Internal error. Can't get os parameter: " + parameter + "\"}";
			goto Done;
		}

		json_object_object_add(jsonResult, parameter.c_str(), json_object_new_string(nyx_result));
	}

	json_object_object_add(jsonResult, "returnValue", json_object_new_boolean(true));
	reply = json_object_to_json_string(jsonResult);

Done:
	bool ret = LSMessageReply(lsHandle, message, reply.c_str(), &lsError);
	if (!ret)
		LSErrorFree(&lsError);

	if (NULL != device)
		nyx_device_close(device);
	nyx_deinit();

	if (payload && !is_error(payload))
		json_object_put(payload);
	if (payloadParameterList && !is_error(payloadParameterList))
		json_object_put(payloadParameterList);
	if (jsonResult && !is_error(jsonResult))
		json_object_put(jsonResult);

	return true;
}
