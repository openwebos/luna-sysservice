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


#include <glib.h>
#include <stdlib.h>
#include <memory.h>
#include "cjson/json.h"
#include "Logging.h"
#include "Utils.h"
#include "errno.h"
#include "JSONUtils.h"

#include "ImageServices.h"
#include <QtGui/QImageReader>
#include <QtGui/QImage>
#include <QtGui/QPainter>
#include <QtCore/QtGlobal>

#include "ImageHelpers.h"

#if 0
#define IMS_TRACE(...) \
do { \
    fprintf(stdout, "ImageService: " ); \
    fprintf(stdout, __VA_ARGS__); \
} while (0)

#else
#define IMS_TRACE(...) (void)0
#endif

/*! \page com_palm_image_service Service API com.palm.image/
 *
 *  Public methods:
 *   - \ref image_service_convert
 *   - \ref image_service_ez_resize
 *   - \ref image_service_image_info
 */

ImageServices * ImageServices::s_instance = NULL;

static LSMethod s_methods_public[] = {
	{ "convert" , ImageServices::lsConvertImage },
	{ "imageInfo" , ImageServices::lsImageInfo },
	{ "ezResize" , ImageServices::lsEzResize },
	{ 0, 0 }
};

static LSMethod s_methods_private[] = {
	{ 0, 0 }
};

//static 
ImageServices * ImageServices::instance(MainLoopProvider *p)
{
	if (s_instance == NULL) {
		s_instance = new ImageServices();
		s_instance->m_valid = s_instance->init(p);
	}
	
	return s_instance;
}

/*! \page com_palm_image_service
\n
\section image_service_convert convert

\e Public.

com.palm.image/convert

Converts an image.

\subsection com_palm_image_convert_syntax Syntax:
\code
{
    "src": string,
    "dest": string,
    "destType": string,
    "focusX": double,
    "focusY": double,
    "scale": double,
    "cropW": double,
    "cropH": double
}
\endcode

\param src Absolute path to source file. Required.
\param dest Absolute path for output file. Required.
\param destType Type of the output file. Required.
\param focusX The horizontal coordinate of the new center of the image, from 0.0 (left edge) to 1.0 (right edge). A value of 0.5 preserves the current horizontal center of the image.
\param focusY The vertical coordinate of the new center of the image, from 0.0 (top edge) to 1.0 (bottom edge). A value of 0.5 preserves the current vertical center of the image.
\param scale Scale factor for the image, must be greater than zero.
\param cropW Crop the image to this width.
\param cropH Crop the image to this width height.

\subsection com_palm_image_convert_return Returns:
\code
{
    "subscribed": boolean,
    "returnValue": boolean,
    "errorCode": string
}
\endcode

\param subscribed Always false.
\param returnValue Indicates if the call was succesful.
\param errorCode Description of the error if call was not succesful.

\subsection com_palm_image_convert_examples Examples:
\code
luna-send -n 1 -f luna://com.palm.image/convert '{"src": "/usr/lib/luna/system/luna-systemui/images/opensearch-small-icon.png", "dest": "/tmp/convertedimage.png", "destType": "jpg"  }'
\endcode

Example response for a succesful call:
\code
{
    "subscribed": false,
    "returnValue": true
}
\endcode
Example response for a failed call:
\code
{
    "subscribed": false,
    "returnValue": false,
    "errorCode": "'destType' parameter missing"
}
\endcode
*/
//static 
bool ImageServices::lsConvertImage(LSHandle* lsHandle, LSMessage* message,void* user_data)
{
	LSError lserror;
	LSErrorInit(&lserror);
	std::string errorText;
	json_object * root = NULL;
	json_object * label = NULL;
	const char* str;
	int rc;
	bool specOn = false;
	std::string srcfile;
	std::string destfile;
	std::string desttype;
	double focusX = -1;
	double focusY = -1;
	double scale = -1;
	uint32_t cropW = 0;
	uint32_t cropH = 0;

    // {"src": string, "dest": string, "destType": string, "focusX": double, "focusY": double, "scale": double, "cropW": double, "cropH": double}
    VALIDATE_SCHEMA_AND_RETURN(lsHandle,
                               message,
                               SCHEMA_8(REQUIRED(src, string), REQUIRED(dest, string), REQUIRED(destType, string), REQUIRED(focusX, double), REQUIRED(focusY, double), REQUIRED(scale, double), REQUIRED(cropW, double), REQUIRED(cropH, double)));

	
	ImageServices * pImgSvc = instance();
	if (pImgSvc == NULL) {
		errorText = "Image Service has not started";
		goto Done_lsConvertImage;
	}

	if (pImgSvc->isValid() == false) {
		errorText = "Image Service has not started (failed init)";
		goto Done_lsConvertImage;
	}
	
	str = LSMessageGetPayload( message );
	if (!str) {
		errorText = "No payload provided";
		goto Done_lsConvertImage;
	}

	root = json_tokener_parse( str );
	if (!root || is_error(root)) {
		errorText = "Malformed JSON detected in payload";
		root = 0;
		goto Done_lsConvertImage;
	}
	
	if (Utils::extractFromJson(root,"src",srcfile) == false) {
		errorText = "'src' parameter missing";
		goto Done_lsConvertImage;
	}
	if (Utils::extractFromJson(root,"dest",destfile) == false) {
		errorText = "'dest' parameter missing";
		goto Done_lsConvertImage;
	}
	if (Utils::extractFromJson(root,"destType",desttype) == false) {
		errorText = "'destType' parameter missing";
		goto Done_lsConvertImage;
	}

	if ((label = Utils::JsonGetObject(root,"focusX")) != NULL) {
		focusX = json_object_get_double(label);
		if ((focusX < 0) || (focusX > 1)) {
			errorText = "'focusX' parameter out of range (must be [0.0,1.0] )";
			goto Done_lsConvertImage;
		}
		specOn = true;
	}
	if ((label = Utils::JsonGetObject(root,"focusY")) != NULL) {
		focusY = json_object_get_double(label);
		if ((focusY < 0) || (focusY > 1)) {
			errorText = "'focusY' parameter out of range (must be [0.0,1.0] )";
			goto Done_lsConvertImage;
		}
		specOn = true;
	}
	if ((label = Utils::JsonGetObject(root,"scale")) != NULL) {
		scale = json_object_get_double(label);
		if (scale <= 0) {
			errorText = "'scale' parameter out of range ( must be > 0.0 )";
			goto Done_lsConvertImage;
		}
		specOn = true;
	}
	if ((label = Utils::JsonGetObject(root,"cropW")) != NULL) {
		cropW = json_object_get_double(label);
		if (cropW < 0) {
			errorText = "'cropW' parameter out of range (must be > 0 )";
			goto Done_lsConvertImage;
		}
		specOn = true;
	}
	if ((label = Utils::JsonGetObject(root,"cropH")) != NULL) {
		cropH = json_object_get_double(label);
		if (cropH < 0) {
			errorText = "'cropH' parameter out of range (must be > 0 )";
			goto Done_lsConvertImage;
		}
		specOn = true;
	}
	
	/*
	 * 
	 * bool convertImage(const std::string& pathToSourceFile,
			const std::string& pathToDestFile,int destType,
			double focusX,double focusY,double scale,
			uint32_t widthFinal,uint32_t heightFinal,
			std::string& r_errorText);
	
	bool convertImage(const std::string& pathToSourceFile,
			const std::string& pathToDestFile,int destType,
			std::string& r_errorText);
			
	 * 
	 */
    if (specOn) {
        rc = ImageServices::instance()->convertImage(srcfile, destfile, desttype.c_str(),
                                                     focusX, focusY,
                                                     scale,
                                                     cropW, cropH,
                                                     errorText);
    }
    else {
        // the "just transcode" version of convert is called
        rc = ImageServices::instance()->convertImage(srcfile, destfile, desttype.c_str(), errorText);
    }
	
Done_lsConvertImage:

	if (root)
		json_object_put(root);

	json_object * reply = json_object_new_object();
	json_object_object_add(reply, "subscribed", json_object_new_boolean(false));
	if (errorText.size() > 0) {
		json_object_object_add(reply, "returnValue", json_object_new_boolean(false));
		json_object_object_add(reply, "errorCode", json_object_new_string(errorText.c_str()));
        qWarning() << errorText.c_str();
	}
	else {
		json_object_object_add(reply, "returnValue", json_object_new_boolean(true));
	}

	if (!LSMessageReply(lsHandle, message, json_object_to_json_string(reply), &lserror))
		LSErrorFree (&lserror);

	json_object_put(reply);

	return true;
}

/*! \page com_palm_image_service
\n
\section image_service_ez_resize ezResize

\e Public.

com.palm.image/ezResize

Resize an image.

\subsection image_service_ez_resize_syntax Syntax:
\code
{
    "src": string,
    "dest": string,
    "destType": string,
    "destSizeW": integer,
    "destSizeH": integer
}
\endcode

\param src Absolute path to source file. Required.
\param dest Absolute path for output file. Required.
\param destType Type of the output file. Required.
\param destSizeW Width of the resized image. Required.
\param destSizeH Height of the resized image. Required.

\subsection image_service_ez_resize_returns Returns:
\code
{
    "subscribed": boolean,
    "returnValue": boolean,
    "errorCode": string
}
\endcode

\param subscribed Always false.
\param returnValue Indicates if the call was succesful.
\param errorCode Description of the error if call was not succesful.

\subsection image_service_ez_resize_examples Examples:
\code
luna-send -n 1 -f luna://com.palm.image/ezResize '{"src": "/usr/lib/luna/system/luna-systemui/images/opensearch-small-icon.png", "dest": "/tmp/convertedimage", "destType": "jpg", "destSizeW": 6, "destSizeH": 6 }'
\endcode

Example response for a succesful call:
\code
{
    "subscribed": false,
    "returnValue": true
}
\endcode

Example response for a failed call:
\code
{
    "subscribed": false,
    "returnValue": false,
    "errorCode": "'destSizeH' missing"
}
\endcode
*/
//static
bool ImageServices::lsEzResize(LSHandle* lsHandle, LSMessage* message,void* user_data)
{
	LSError lserror;
	LSErrorInit(&lserror);
	std::string errorText;
	json_object * root = NULL;
	json_object * label = NULL;
	const char* str;
	std::string srcfile;
	std::string destfile;
	std::string desttype;
	uint32_t destSizeW = 0;
	uint32_t destSizeH = 0;

    // {"src": string, "dest": string, "destType": string, "destSizeW": integer, "destSizeH": integer}
    VALIDATE_SCHEMA_AND_RETURN(lsHandle,
                               message,
                               SCHEMA_5(REQUIRED(src, string), REQUIRED(dest, string), REQUIRED(destType, string), REQUIRED(destSizeW, integer), REQUIRED(destSizeH, integer)));

	ImageServices * pImgSvc = instance();
	if (pImgSvc == NULL) {
		errorText = "Image Service has not started";
		goto Done_ezResize;
	}

	if (pImgSvc->isValid() == false) {
		errorText = "Image Service has not started (failed init)";
		goto Done_ezResize;
	}

	str = LSMessageGetPayload( message );
	if (!str) {
		errorText = "No payload provided";
		goto Done_ezResize;
	}

	root = json_tokener_parse( str );
	if (!root || is_error(root)) {
		errorText = "Malformed JSON detected in payload";
		root = 0;
		goto Done_ezResize;
	}

	if (Utils::extractFromJson(root,"src",srcfile) == false) {
		errorText = "'src' parameter missing";
		goto Done_ezResize;
	}
	if (Utils::extractFromJson(root,"dest",destfile) == false) {
		errorText = "'dest' parameter missing";
		goto Done_ezResize;
	}
	if (Utils::extractFromJson(root,"destType",desttype) == false) {
		errorText = "'destType' parameter missing";
		goto Done_ezResize;
	}

	if ((label = Utils::JsonGetObject(root,"destSizeW")) != NULL)
	{
		destSizeW = json_object_get_int(label);
	}
	else
	{
		errorText = "'destSizeW' missing";
		goto Done_ezResize;
	}
	if ((label = Utils::JsonGetObject(root,"destSizeH")) != NULL)
	{
		destSizeH = json_object_get_int(label);
	}
	else
	{
		errorText = "'destSizeH' missing";
		goto Done_ezResize;
	}

    (void)ImageServices::instance()->ezResize(srcfile, destfile, desttype.c_str(), destSizeW, destSizeH, errorText);

Done_ezResize:

	if (root)
		json_object_put(root);

	json_object * reply = json_object_new_object();
	json_object_object_add(reply, "subscribed", json_object_new_boolean(false));
	if (errorText.size() > 0) {
		json_object_object_add(reply, "returnValue", json_object_new_boolean(false));
		json_object_object_add(reply, "errorCode", json_object_new_string(errorText.c_str()));
        qWarning() << errorText.c_str();
	}
	else {
		json_object_object_add(reply, "returnValue", json_object_new_boolean(true));
	}

	if (!LSMessageReply(lsHandle, message, json_object_to_json_string(reply), &lserror))
		LSErrorFree (&lserror);

	json_object_put(reply);

	return true;
}

/*! \page com_palm_image_service
\n
\section image_service_image_info imageInfo

\e Public.

com.palm.image/imageInfo

Get information for an image.

\subsection image_service_image_info_syntax Syntax:
\code
{
    "src": string
}
\endcode

\param src Absolute path to source file. Required.

\subsection image_service_image_info_returns Returns:
\code
{
    "subscribed": boolean,
    "returnValue": boolean,
    "errorCode": string,
    "width": int,
    "height": int,
    "bpp": int,
    "type": "string
}
\endcode

\param subscribed Always false.
\param returnValue Indicates if the call was succesful or not.
\param errorCode Description of the error if call was not succesful.
\param with Width of the image.
\param height Height of the image.
\param bpp Color depth, bits per pixel.
\param type Type of the image file.

\subsection image_service_image_info_examples Examples:

\code
luna-send -n 1 -f  luna://com.palm.image/imageInfo '{"src":"/usr/lib/luna/system/luna-systemui/images/opensearch-small-icon.png"}'
\endcode
Example response for a successful call:
\code
{
    "subscribed": false,
    "returnValue": true,
    "width": 24,
    "height": 24,
    "bpp": 8,
    "type": "png"
}
\endcode

Example response in case of a failure:
\code
{
    "subscribed": false,
    "returnValue": false,
    "errorCode": "source file does not exist"
}
\endcode
*/
//static
bool ImageServices::lsImageInfo(LSHandle* lsHandle, LSMessage* message,void* user_data)
{
	LSError lserror;
	LSErrorInit(&lserror);
	std::string errorText;
	json_object * root = NULL;
	const char* str;
	std::string srcfile;
	int srcWidth;
	int srcHeight;
	int srcBpp;
    const char* srcType;
    // {"src": string}
    VALIDATE_SCHEMA_AND_RETURN(lsHandle,
                               message,
                               SCHEMA_1(REQUIRED(src, string)));

	ImageServices * pImgSvc = instance();
	if (pImgSvc == NULL) {
		errorText = "Image Service has not started";
		goto Done_lsImageInfo;
	}

	if (pImgSvc->isValid() == false) {
		errorText = "Image Service has not started (failed init)";
		goto Done_lsImageInfo;
	}

	str = LSMessageGetPayload( message );
	if (!str) {
		errorText = "No payload provided";
		goto Done_lsImageInfo;
	}

	root = json_tokener_parse( str );
	if (!root || is_error(root)) {
		errorText = "Malformed JSON detected in payload";
		root = 0;
		goto Done_lsImageInfo;
	}

	if (Utils::extractFromJson(root,"src",srcfile) == false) {
		errorText = "'src' parameter missing";
		goto Done_lsImageInfo;
	}

    {
        QImageReader reader(QString::fromStdString(srcfile));
        if(!reader.canRead()) {
            errorText = reader.errorString().toStdString();
            return false;
            goto Done_lsImageInfo;
        }
        srcWidth = reader.size().width();
        srcHeight = reader.size().height();
        // QImageReader probably won't return all of these, but just to make sure we cover all cases
        switch(reader.imageFormat()) {
            case QImage::Format_ARGB32_Premultiplied:
            case QImage::Format_ARGB32:
            case QImage::Format_RGB32:
            srcBpp = 32; break;
            case QImage::Format_RGB888:
            case QImage::Format_RGB666:
            case QImage::Format_ARGB8565_Premultiplied:
            case QImage::Format_ARGB6666_Premultiplied:
            case QImage::Format_ARGB8555_Premultiplied:
            srcBpp = 24; break;
            case QImage::Format_RGB444:
            case QImage::Format_ARGB4444_Premultiplied:
            case QImage::Format_RGB16:
            case QImage::Format_RGB555:
            srcBpp = 16; break;
            case QImage::Format_Indexed8:
            srcBpp = 8; break;
            case QImage::Format_Mono:
            case QImage::Format_MonoLSB:
            srcBpp = 1; break;
            default:
            srcBpp = 0;
        }
       srcType = reader.format(); // png/jpg etc
    }

Done_lsImageInfo:

	if (root)
		json_object_put(root);

	json_object * reply = json_object_new_object();
	json_object_object_add(reply, "subscribed", json_object_new_boolean(false));
	if (errorText.size() > 0) {
		json_object_object_add(reply, "returnValue", json_object_new_boolean(false));
		json_object_object_add(reply, "errorCode", json_object_new_string(errorText.c_str()));
        qWarning() << errorText.c_str();
	}
	else {
		json_object_object_add(reply, "returnValue", json_object_new_boolean(true));
		json_object_object_add(reply, "width",json_object_new_int(srcWidth));
		json_object_object_add(reply, "height",json_object_new_int(srcHeight));
		json_object_object_add(reply, "bpp",json_object_new_int(srcBpp));
		json_object_object_add(reply, "type", json_object_new_string(srcType));
	}

	if (!LSMessageReply(lsHandle, message, json_object_to_json_string(reply), &lserror))
		LSErrorFree (&lserror);

	json_object_put(reply);

	return true;
}

//////////////////////////////////////////////////////////////// PRIVATE ///////////////////////////////////////////////

ImageServices::ImageServices()
{
	m_valid = false;
}

//IF THIS FUNCTION EVER RETURNS FALSE, IT'S PRETTY MUCH IMPOSSIBLE TO CONTINUE BECAUSE IT'S UNCERTAIN WHETHER THE MAIN LOOP (GMAINLOOP) STATE IS "CLEAN"; ONCE
//LSGmainAttachPalmService SUCCEEDS, THERE IS NO WAY TO CLEANLY DETACH THE SERVICE (IF LSPalmServiceRegisterCategory, OR ANYTHING AFTERWARDS, FAILS)
//THEREFORE, A FAILED init() SHOULD BE GROUNDS FOR PROCESS TERMINATION

bool ImageServices::init(MainLoopProvider * p)
{
	if (p == NULL)
		return false;
	
	//grab the main loop ptr from the provider
	m_p_mainloop = p->getMainLoopPtr();
	if (m_p_mainloop == NULL)
		return false;
	
	//register the service
	LSError lsError;
	bool result;

	LSErrorInit(&lsError);

	// Register the service
	result = LSRegisterPalmService("com.palm.image", &m_service, &lsError);
	if (!result) {
        qCritical() << "Failed to register service: com.palm.image";
		return false;
	}

	m_serviceHandlePublic = LSPalmServiceGetPublicConnection(m_service);
	m_serviceHandlePrivate = LSPalmServiceGetPrivateConnection(m_service);
	result = LSGmainAttachPalmService(m_service, m_p_mainloop, &lsError);
	if (!result) {
        qCritical() << "Failed to attach service handle to main loop";
		LSErrorFree(&lsError);
		LSErrorInit(&lsError);
		result = LSUnregisterPalmService(m_service,&lsError);
		if (!result)
			LSErrorFree(&lsError);
		return false;
	}
	
	//register methods
	result = LSPalmServiceRegisterCategory( m_service, "/", s_methods_public, s_methods_private,
			NULL, this, &lsError);
	if (!result) {
        qCritical() << "Failed in registering handler methods on /:" << lsError.message;
		LSErrorFree(&lsError);
		result = LSUnregisterPalmService(m_service,&lsError);
		if (!result)
			LSErrorFree(&lsError);
		return false;
	}

	return true;

}

////////////////////////////////////////////// PRIVATE - IMAGE FUNCTIONS ///////////////////////////////////////////////

bool ImageServices::ezResize(const std::string& pathToSourceFile,
                             const std::string& pathToDestFile, const char* destType,
                             uint32_t widthFinal, uint32_t heightFinal,
                             std::string& r_errorText)
{
    qDebug("From: [%s], To: [%s], target: {Type: [%s], w:%d, h:%d}",
            pathToSourceFile.c_str(), pathToDestFile.c_str(), destType, widthFinal, heightFinal);

    QImageReader reader(QString::fromStdString(pathToSourceFile));
    if(!reader.canRead()) {
        r_errorText = reader.errorString().toStdString();
        return false;
    }

    QImage image;
    if (!reader.read(&image)) {
        r_errorText = reader.errorString().toStdString();
        return false;
    }
    // cropped rescale, see http://qt-project.org/doc/qt-4.8/qt.html#AspectRatioMode-enum
    
    QImage result(widthFinal, heightFinal, image.format());

    if(result.isNull()) {
        r_errorText = "ezResize: unable to allocate memory for QImage";
        return false;
    }

    QPainter p(&result);
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    p.drawImage(QRect(0,0,widthFinal, heightFinal), image);
    p.end();
//    image = image.scaled(widthFinal, heightFinal, Qt::KeepAspectRatioByExpanding);
    PMLOG_TRACE("About to save image");
    if(!result.save(QString::fromStdString(pathToDestFile), destType, 100)) {
        r_errorText = "ezResize: failed to save destination file";
        return false;
    }

    return true;
}

bool ImageServices::convertImage(const std::string& pathToSourceFile,
                                 const std::string& pathToDestFile, const char* destType,
                                 double focusX, double focusY, double scale,
                                 uint32_t widthFinal,uint32_t heightFinal,
                                 std::string& r_errorText)
{
    qDebug("From: [%s], To: [%s], focus:{x:%f,y:%f}, target: {Type: [%s], w:%d, h:%d}, scale: %f",
            pathToSourceFile.c_str(), pathToDestFile.c_str(), focusX, focusY, destType, widthFinal, heightFinal, scale);

    QImageReader reader(QString::fromStdString(pathToSourceFile));
    if(!reader.canRead()) {
        r_errorText = reader.errorString().toStdString();
        return false;
    }

    if (focusX < 0)
        focusX = 0.5;
    if (focusY < 0)
        focusY = 0.5;

    //fix scale factor just in case it's negative
    if (scale < 0.0)
        scale *= -1.0;

    //TODO: WARN: strict comparison of float to 0 might fail
    if (qFuzzyCompare(scale, 0.0))
        scale = 1.0;
    qDebug("After adjustments: scale: %f, focus:{x:%f,y:%f}", scale, focusX, focusY);

    QImage image;
    double prescale;
    if(!readImageWithPrescale(reader, image, prescale)) {
        r_errorText = reader.errorString().toStdString();
        return false;
    }

    //scale the image as requested...factor in whatever the prescaler did
    scale /= prescale;
    qDebug("scale after prescale adjustment: %f, prescale: %f", scale, prescale);

    QImage dest(widthFinal, heightFinal, image.format());
    QPainter p (&dest);
    p.translate(heightFinal/2, widthFinal/2);
    p.translate(-focusX * image.width(), -focusY * image.height());
    p.scale(scale, scale);
    p.drawImage(QPoint(0,0), image);
    p.end();

    dest.save(QString::fromStdString(pathToDestFile), destType, 100);
    return true;

}

bool ImageServices::convertImage(const std::string& pathToSourceFile,
                                 const std::string& pathToDestFile, const char* destType,
                                       std::string& r_errorText)
{
    qDebug("From: [%s], To: [%s], target: {Type: [%s]}",
            pathToSourceFile.c_str(), pathToDestFile.c_str(), destType);

    QImageReader reader(QString::fromStdString(pathToSourceFile));
    if(!reader.canRead()) {
        r_errorText = reader.errorString().toStdString();
        return false;
    }

    QImage image;
    if (!reader.read(&image)) {
        r_errorText = reader.errorString().toStdString();
        return false;
    }

    image.save(QString::fromStdString(pathToDestFile), destType, 100);
    return true;
}

