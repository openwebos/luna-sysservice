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

#include "DeviceInfoService.h"
#include "Logging.h"

DeviceInfoService::command_map_t DeviceInfoService::s_commandMap;

LSMethod s_device_methods[]  = {
	{ "query",  DeviceInfoService::cbGetDeviceInformation },
	{ 0, 0 },
};

/*! \page com_palm_device_info_service Service API com.palm.systemservice/deviceInfo/
 *
 *  Public methods:
 *   - \ref device_info_query
 */
DeviceInfoService* DeviceInfoService::instance()
{
	static DeviceInfoService* s_instance = 0;
	if (G_UNLIKELY(s_instance == 0))
	{
		DeviceInfoService::initCommandMap();
		s_instance = new DeviceInfoService;
	}

	return s_instance;
}

DeviceInfoService::DeviceInfoService(): m_service(0)
{
}

DeviceInfoService::~DeviceInfoService()
{
    // NO-OP
}

void DeviceInfoService::initCommandMap()
{
	s_commandMap["board_type"] = NYX_DEVICE_INFO_BOARD_TYPE; // Return board type
	s_commandMap["bt_addr"] = NYX_DEVICE_INFO_BT_ADDR; // Return Bluetooth address
	s_commandMap["device_name"] = NYX_DEVICE_INFO_DEVICE_NAME; // Return device name
	s_commandMap["hardware_id"] = NYX_DEVICE_INFO_HARDWARE_ID; // Return hardware ID
	s_commandMap["hardware_revision"] = NYX_DEVICE_INFO_HARDWARE_REVISION; // Return hardware revision
	s_commandMap["installer"] = NYX_DEVICE_INFO_INSTALLER; // Return installer
	s_commandMap["keyboard_type"] = NYX_DEVICE_INFO_KEYBOARD_TYPE; // Return keyboard type
	s_commandMap["modem_present"] = NYX_DEVICE_INFO_MODEM_PRESENT; // Return modem availability
	s_commandMap["nduid"] = NYX_DEVICE_INFO_NDUID; // Return NDUID
	s_commandMap["product_id"] = NYX_DEVICE_INFO_PRODUCT_ID; // Return product ID
	s_commandMap["radio_type"] = NYX_DEVICE_INFO_RADIO_TYPE; // Return radio type
	s_commandMap["ram_size"] = NYX_DEVICE_INFO_RAM_SIZE; // Return RAM size
	s_commandMap["serial_number"] = NYX_DEVICE_INFO_SERIAL_NUMBER; // Return serial number
	s_commandMap["storage_free"] = NYX_DEVICE_INFO_STORAGE_FREE; // Return free storage size
	s_commandMap["storage_size"] = NYX_DEVICE_INFO_STORAGE_SIZE; // Return storage size
	s_commandMap["wifi_addr"] = NYX_DEVICE_INFO_WIFI_ADDR; // Return WiFi MAC address
	s_commandMap["last_reset_type"] = NYX_DEVICE_INFO_LAST_RESET_TYPE; // Reason code for last reboot (may come from /proc/cmdline)
	s_commandMap["battery_challange"] = NYX_DEVICE_INFO_BATT_CH; // Battery challenge
	s_commandMap["battery_response"] = NYX_DEVICE_INFO_BATT_RSP; // Battery response
}

void DeviceInfoService::setServiceHandle(LSPalmService* service)
{
	m_service = service;

	LSError lsError;
	LSErrorInit(&lsError);

	bool result = LSPalmServiceRegisterCategory(m_service, "/deviceInfo",
												NULL, s_device_methods,
												NULL, this, &lsError);
	if (!result) {
		qCritical() << "Failed in registering deviceinfo handler method:" << lsError.message;
		LSErrorFree(&lsError);
		return;
	}
}

LSPalmService* DeviceInfoService::serviceHandle() const
{
	return m_service;
}

/*!
\page com_palm_device_info_service
\n
\section device_info_query query

\e Private. Available only at the private bus.

com.palm.systemservice/deviceInfo/query

\subsection device_info_query_syntax Syntax:
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
    "board_type": string
    "bt_addr": string
    "device_name": string
    "hardware_id": string
    "hardware_revision": string
    "installer": string
    "keyboard_type": string
    "modem_present": string
    "nduid": string
    "product_id": string
    "radio_type": string
    "ram_size": string
    "serial_number": string
    "storage_free": string
    "storage_size": string
    "wifi_addr": string
    "last_reset_type": string
    "battery_challange": string
    "battery_response": string
}
\endcode

\param returnValue Indicates if the call was succesful.
\param errorCode Description of the error if call was not succesful.
\param board_type Board type
\param bt_addr Bluetooth address
\param device_name Device name
\param hardware_id Hardware ID
\param hardware_revision Hardware revision
\param installer Installer
\param keyboard_type Keyboard type
\param modem_present Modem availability
\param nduid NDUID
\param product_id Product ID
\param radio_type Radio type
\param ram_size RAM size
\param serial_number Serial number
\param storage_free Free storage size
\param storage_size Storage size
\param wifi_addr WiFi MAC address
\param last_reset_type Reason code for last reboot (may come from /proc/cmdline)
\param battery_challange Battery challenge
\param battery_response Battery response

All listed parameters can have `not supported` value, if not supported by the device.

\subsection device_info_qeury_examples Examples:
\code
luna-send -n 1 -f luna://com.palm.systemservice/deviceInfo/query '{"parameters":["device_name", "storage_size"]}'
\endcode

Example response for a succesful call:
\code
{
    "device_name": "qemux86",
    "storage_size": "32 GB",
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
bool DeviceInfoService::cbGetDeviceInformation(LSHandle* lsHandle, LSMessage *message, void *user_data)
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
	if (!payload || is_error(payload)) {
		reply = "{\"returnValue\": false, "
			" \"errorText\": \"Cannot parse json payload\"}";
		goto Done;
	}

	if (json_object_object_get_ex(payload, "parameters", &payloadParameterList))
	{
		if (!json_object_is_type(payloadParameterList, json_type_array)) {
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

	error = nyx_device_open(NYX_DEVICE_DEVICE_INFO, "Main", &device);
	if ((NYX_ERROR_NONE != error) || (NULL == device))
	{
		qCritical() << "Failed to open `Main` nyx device: " << error;
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

	    // Some device don't have all available parameters. We will just ignore them.
		error = nyx_device_info_query(device, query->second, &nyx_result);
		if (NYX_ERROR_NONE == error)
		{
			json_object_object_add(jsonResult, parameter.c_str(), json_object_new_string(nyx_result));
		}
		else
		{
			json_object_object_add(jsonResult, parameter.c_str(), json_object_new_string("not supported"));
		}
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
