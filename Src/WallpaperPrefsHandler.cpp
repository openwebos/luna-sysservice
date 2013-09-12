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


#include "Logging.h"
#include "PrefsDb.h"
#include "Utils.h"
#include <errno.h>
#include <dirent.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <linux/fb.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include "UrlRep.h"
#include "ImageServices.h"
#include "SystemRestore.h"
#include "Settings.h"

#include <cjson/json.h>
#include <glib.h>
#include "JSONUtils.h"

#include <QtCore/QDebug>
#include <QtCore/QString>
#include <QtCore/QFile>
#include <QtGui/QImage>
#include <QtGui/QImageReader>
#include <QtGui/QImageWriter>
#include <QtGui/QPainter>
#include <QtCore/QtGlobal>

#include "ImageHelpers.h"

#include "WallpaperPrefsHandler.h"

static const char* s_logChannel = "WallpaperPrefsHandler";

std::string WallpaperPrefsHandler::s_wallpaperDir;
std::string WallpaperPrefsHandler::s_wallpaperThumbsDir;


#define		THUMBS_WIDTH			64
#define		THUMBS_HEIGHT			64

static int SCREEN_WIDTH = 0;
static int SCREEN_HEIGHT = 0;

static bool cbImportWallpaper(LSHandle* lsHandle, LSMessage *message,
							void *user_data);

static bool cbRefreshWallpaperIndex(LSHandle* lsHandle, LSMessage *message,
							void *user_data);

static bool cbGetWallpaperSpec(LSHandle* lsHandle, LSMessage *message,
							void *user_data);

static bool cbDeleteWallpaper(LSHandle* lsHandle, LSMessage *message,
							  void *user_data);

static bool cbConvertImage(LSHandle* lsHandle, LSMessage *message,
							  void *user_data);

/*
static bool _dbg_cbReadFile(LSHandle* lsHandle, LSMessage *message,
							  void *user_data);
*/

/*! \page com_palm_systemservice_wallpaper Service API com.palm.systemservice/wallpaper/
 *
 * Public methods:
 * - \ref com_palm_systemservice_wallpaper_import_wallpaper
 * - \ref com_palm_systemservice_wallpaper_refresh
 * - \ref com_palm_systemservice_wallpaper_info
 * - \ref com_palm_systemservice_wallpaper_delete_wallpaper
 * - \ref com_palm_systemservice_wallpaper_convert
 */
static LSMethod s_methods[]  = {
	{ "importWallpaper",     cbImportWallpaper},
	{ "refresh"		 	,	 cbRefreshWallpaperIndex},
	{ "info"			,	 cbGetWallpaperSpec},	
	{ "deleteWallpaper" ,	 cbDeleteWallpaper},
	{ "convert"			,	 cbConvertImage},
    { 0, 0 },
};

WallpaperPrefsHandler::WallpaperPrefsHandler(LSPalmService* service)
	: PrefsHandler(service)
{
	init();
}

WallpaperPrefsHandler::~WallpaperPrefsHandler()
{   
}

std::list<std::string> WallpaperPrefsHandler::keys() const
{
	std::list<std::string> k;
	k.push_back("wallpaper");
	k.push_back("screenSize.width");
	k.push_back("screenSize.height");

	return k;
}

bool WallpaperPrefsHandler::validate(const std::string& key, json_object* value, const std::string& originId)
{

	//this just validates the "screenSize" key, and delegates the rest to the more general validate()

	if (value == NULL)
		return false;
	if (key.find("screenSize") != 0)
		return validate(key,value);

	if (!originId.empty())
	{
        qWarning("%s: [SECURITY] : refusing screenSize (%s) setting from %s",__FUNCTION__,key.c_str(),originId.c_str());
		return false;
	}
	return true;
}

bool WallpaperPrefsHandler::validate(const std::string& key, json_object* value)
{
	
	//the value coming has to contain the name as a value for "wallpaperName", as provided
	//by valuesForKey in "wallpaperName"
	
	if (value == NULL)
		return false;
	if (key != "wallpaper")
		return false;
	if (json_object_get_type(value) != json_type_object)
		return false;
	
	json_object* label = 0;
	std::string wallpaperName;
	
	label = json_object_object_get(value, "wallpaperName");
	if (!label) {
		return false;
	}

	wallpaperName = json_object_get_string(label);
	
	//refresh the wallpapers from the directory
	//WARNING: small chance for a race condition here. The file could be deleted after the scan
	scanForWallpapers();
	
	//try to match the given wallpaper to one of the ones found in the scan
	for (std::list<std::string>::iterator it = m_wallpapers.begin();it != m_wallpapers.end();it++) {
		if (wallpaperName == (*it))
			return true;
	}
	
    return false;
}

void WallpaperPrefsHandler::valueChanged(const std::string& key, json_object* value)
{
	if (value == NULL)
		return;
	
	json_object* label = 0;

	label = json_object_object_get(value, "wallpaperName");
	if (!label) {
		return;
	}

	m_currentWallpaperName = json_object_get_string(label);
	
}

json_object* WallpaperPrefsHandler::valuesForKey(const std::string& key)
{
	//scan the wallpapers dir
	std::list<std::string> wallpaperFilenames = scanForWallpapers();
	
	json_object* json = json_object_new_object();
	json_object* arrayObj = json_object_new_array();
	for (std::list<std::string>::iterator it = wallpaperFilenames.begin();it != wallpaperFilenames.end();it++) {
		json_object * element = json_object_new_object();
		char * filename_cstr = const_cast<char*>((*it).c_str());
		json_object_object_add(element,(char *)"wallpaperName",json_object_new_string(filename_cstr));
		std::string wpFile = s_wallpaperDir + std::string("/")+(*it);
		std::string wpThumbFile = s_wallpaperThumbsDir + std::string("/")+(*it);
		filename_cstr = const_cast<char*>(wpFile.c_str());
		json_object_object_add(element,(char *)"wallpaperFile",json_object_new_string(filename_cstr));
		filename_cstr = const_cast<char*>(wpThumbFile.c_str());
		json_object_object_add(element,(char *)"wallpaperThumbFile",json_object_new_string(filename_cstr));
		json_object_array_add(arrayObj,element);
	}
	
	json_object_object_add(json,(char *)"wallpaper",arrayObj);
	return json;	
}

void WallpaperPrefsHandler::init()
{
    //luna_log(s_logChannel,"WallpaperPrefsHandler::init()");
    __qMessage("start");
	bool result;
	LSError lsError;
	LSErrorInit(&lsError);

	getScreenDimensions();
    __qMessage("Screen Width set to %d , Screen Height set to %d",SCREEN_WIDTH,SCREEN_HEIGHT);
	s_wallpaperDir = std::string(PrefsDb::s_mediaPartitionPath) + std::string(PrefsDb::s_mediaPartitionWallpapersDir);
	s_wallpaperThumbsDir = std::string(PrefsDb::s_mediaPartitionPath) + std::string(PrefsDb::s_mediaPartitionWallpaperThumbsDir);
	
	//make sure the wallpaper directories exist
	int exit_status = g_mkdir_with_parents(s_wallpaperDir.c_str(),0766);
	if (exit_status < 0) {
        qWarning("can't seem to create the wallpaper dir (currently [%s])",s_wallpaperDir.c_str());
	}
	exit_status = g_mkdir_with_parents(s_wallpaperThumbsDir.c_str(),0766);
	if (exit_status < 0) {
        qWarning("can't seem to create the wallpaper thumbs dir (currently [%s])",s_wallpaperThumbsDir.c_str());
	}
	
	result = LSPalmServiceRegisterCategory( m_service, "/wallpaper", s_methods, NULL,
			NULL, this, &lsError);
	if (!result) {
        //luna_critical(s_logChannel, "Failed in registering wallpaper handler method: %s", lsError.message);
        qCritical("Failed in registering wallpaper handler method: %s", lsError.message);
		LSErrorFree(&lsError);
		return;
	}

	m_serviceHandlePublic = LSPalmServiceGetPublicConnection(m_service);
	m_serviceHandlePrivate = LSPalmServiceGetPrivateConnection(m_service);

	    
	result = LSCategorySetData(m_serviceHandlePublic, "/wallpaper", this, &lsError);
	if (!result) {
        //luna_critical(s_logChannel, "Failed in LSCategorySetData: %s", lsError.message);
        qCritical("Failed in LSCategorySetData: %s", lsError.message);
		LSErrorFree(&lsError);
		return;
	}
	
	result = LSCategorySetData(m_serviceHandlePrivate, "/wallpaper", this, &lsError);
	if (!result) {
        //luna_critical(s_logChannel, "Failed in LSCategorySetData: %s", lsError.message);
        qCritical("Failed in LSCategorySetData: %s", lsError.message);
		LSErrorFree(&lsError);
		return;
	}


	int n=0;
	this->buildIndexFromExisting(&n);
	if (n)
		this->scanForWallpapers();
	
}

bool WallpaperPrefsHandler::isPrefConsistent()
{
	//check to see if the wallpaper setting points to something actually on the disk
	return SystemRestore::instance()->isWallpaperSettingConsistent();
	
}

void WallpaperPrefsHandler::restoreToDefault()
{
	SystemRestore::instance()->restoreDefaultWallpaperSetting();
}

static bool isNonErrorProcExit(int ecode,int normalCode=0) {

	if (!WIFEXITED(ecode))
		return false;
	if (WEXITSTATUS(ecode) != normalCode)
		return false;

	return true;
}

static std::string runImage2Binary(json_object * p_jsonRequestObject)
{

	//spawn directly with the proper cmd line arguments: -e <json request string>
	gchar * argv[4];
	GError * gerr = NULL;
	int exitStatus = 0;
	gchar * stdoutBuffer = 0;

	GSpawnFlags flags = (GSpawnFlags)(G_SPAWN_SEARCH_PATH | G_SPAWN_STDOUT_TO_DEV_NULL | G_SPAWN_STDERR_TO_DEV_NULL);
	std::string requestString = json_object_to_json_string(p_jsonRequestObject);
	std::string bin = (Settings::settings()->m_comPalmImage2BinaryFile);

	argv[0] = (gchar*)(bin.c_str());
	argv[1] = (gchar*)"-e";
	argv[2] = (gchar*)(requestString.c_str());
	argv[3] = NULL;

	//g_warning("%s: executing: %s %s %s",__FUNCTION__,argv[0],argv[1],argv[2]);
    __qMessage(" executing: %s %s %s",argv[0],argv[1],argv[2]);

	gboolean resultStatus = g_spawn_sync(NULL,
			argv,
			NULL,
			flags,
			NULL,
			NULL,
			&stdoutBuffer,
			NULL,
			&exitStatus,
			&gerr);

	if (gerr) {
		//g_warning("%s: error: %s",__FUNCTION__,gerr->message);
        qCritical("error: %s", gerr->message);
		g_error_free(gerr);
		if (stdoutBuffer)
		{
			g_free(stdoutBuffer);
		}
		gerr=NULL;
		return "";
	}

	if (resultStatus) {
		if (isNonErrorProcExit((int)exitStatus) == false) {
			if (stdoutBuffer)
			{
				g_free(stdoutBuffer);
			}
			//g_warning("%s: error: result status is %d , error code = %d",__FUNCTION__,resultStatus,exitStatus);
            qCritical("error: result status is %d , error code = %d", resultStatus, exitStatus);
			return "";
		}
	}
	else {
		//failed to exec command
		//g_warning("%s: error: spawn failed",__FUNCTION__);
        qCritical("error: spawn failed");
		if (stdoutBuffer)
		{
			g_free(stdoutBuffer);
		}
		return "";
	}

	std::string reply = std::string(stdoutBuffer);
	return reply;
}


bool WallpaperPrefsHandler::importWallpaperViaImage2(const std::string& imageFilepath,double focusX,double focusY,double scaleFactor,json_object ** r_p_responseObject)
{
	if (!r_p_responseObject)
	{
		return false;
	}

	//split into parts
	gchar* fileName = g_path_get_basename(imageFilepath.c_str());
	gchar* folderPath = g_path_get_dirname(imageFilepath.c_str());

	if (!fileName || !folderPath) {
		g_free(fileName);
		g_free(folderPath);
		return false;
	}

	std::string file = fileName;
	std::string path = folderPath;
	struct stat stBuf;

	g_free(fileName);
	g_free(folderPath);

	//TODO: going to ignore the thumbnail versions. They were never used anyways
	std::string destPathAndFile = s_wallpaperDir + std::string("/")+file;

	if (lstat(destPathAndFile.c_str(), &stBuf) == 0) {
		//yes...destroy it
		unlink(destPathAndFile.c_str());
	}
	std::list<std::string>::iterator iter=m_wallpapers.begin();
	while (iter != m_wallpapers.end()) {
		if (*iter == file)
			iter = m_wallpapers.erase(iter);
		else
			iter++;
	}

	json_object * requestObject = json_object_new_object();
	json_object_object_add(requestObject,"cmd",json_object_new_string("wallpaperConvert"));
	json_object * paramObject = json_object_new_object();
	json_object_object_add(paramObject,"src",json_object_new_string(imageFilepath.c_str()));
	json_object_object_add(paramObject,"dest",json_object_new_string(destPathAndFile.c_str()));
	json_object_object_add(paramObject,"focusX",json_object_new_double(focusX));
	json_object_object_add(paramObject,"focusY",json_object_new_double(focusY));
	json_object_object_add(paramObject,"scale",json_object_new_double(scaleFactor));
	json_object_object_add(requestObject,"params",paramObject);

	//invoke the exec binary
	std::string result = runImage2Binary(requestObject);

	//g_message("%s: result: %s",__FUNCTION__,(result.empty() ? "(NO OUTPUT)" : result.c_str()));
    __qMessage("result: %s", (result.empty() ? "(NO OUTPUT)" : result.c_str()));

	return false;
}

bool WallpaperPrefsHandler::importWallpaper(std::string& ret_wallpaperName,const std::string& sourcePathAndFile,
		bool toScreenSize,
		double centerX,double centerY,double scale,std::string& errorText)
{
	//split into parts

	gchar* fileName = g_path_get_basename(sourcePathAndFile.c_str());
	gchar* folderPath = g_path_get_dirname(sourcePathAndFile.c_str());

	if (!fileName || !folderPath) {
		g_free(fileName);
		g_free(folderPath);
		return false;
	}

	std::string file = fileName;
	std::string path = folderPath;

	g_free(fileName);
	g_free(folderPath);

	return importWallpaper_lowMem(ret_wallpaperName, path, file, toScreenSize, centerX, centerY, scale, errorText);
}

bool WallpaperPrefsHandler::importWallpaper(std::string& ret_wallpaperName, const std::string& sourcePath, const std::string& sourceFile,
        bool toScreenSize,
        double centerX, double centerY,
        double scale, std::string& errorText) {
    std::string pathAndFile = sourcePath + std::string("/")+sourceFile;

    QImageReader reader(QString::fromStdString(pathAndFile));
    if(!reader.canRead()) {
        errorText = reader.errorString().toStdString();
        return false;
    }

    //does it exist in the wallpaper dir?
    //(trim off the extension)
    std::string destPathAndFile = s_wallpaperDir + std::string("/")+sourceFile;
    std::string destThumbPathAndFile = s_wallpaperThumbsDir +  std::string("/")+sourceFile;

    struct stat stBuf;
    if (lstat(destPathAndFile.c_str(), &stBuf) == 0) {
        //yes...destroy it
        unlink(destPathAndFile.c_str());
    }
    if (lstat(destThumbPathAndFile.c_str(), &stBuf) == 0) {
        //the thumbnail too?...destroy it
        unlink(destThumbPathAndFile.c_str());
    }

    std::list<std::string>::iterator iter=m_wallpapers.begin();
    while (iter != m_wallpapers.end()) {
        if (*iter == sourceFile)
            iter = m_wallpapers.erase(iter);
        else
            iter++;
    }

    //fix scale factor just in case it's negative
    if (scale < 0.0)
        scale *= -1.0;

    //TODO: WARN: strict comparison of float to 0 might fail
    if (qFuzzyCompare(scale,0.0))
        scale = 1.0;


    //if the center is at (0.5,0.5) the scale is 1.0, and the image is originally at screen size, then toScreenSize should be set
    //(that function should be able to optimize things, such as copying the image directly without (re)compression
    if (qFuzzyCompare(scale, 1.0)
        && qFuzzyCompare(centerX,0.5) && qFuzzyCompare(centerY,0.5)
        && reader.size().width() == SCREEN_WIDTH && reader.size().height() == SCREEN_HEIGHT)
        toScreenSize = true;

    qDebug("importWallpaper(): parameters: scale = %lf , centerX = %lf , centerY = %lf , toScreenSize? = %s\n",
            scale,centerX,centerY,(toScreenSize ? "True" : "False"));
    //create a resized version of the image to screen res in the wallpapers dir

    if (toScreenSize) {
        if (resizeImage(pathAndFile, destPathAndFile, SCREEN_WIDTH, SCREEN_HEIGHT, reader.format().data()) != 0)
            return false;
    }
    else {
        double prescale;
        QImage image;
        if(!readImageWithPrescale(reader, image, prescale)) {
            errorText=reader.errorString().toStdString();
        }
        scale /= prescale;

        if(scale != 1.0)
            image = image.scaled(image.width() * scale, image.height() * scale);

        // now refocus as requested
        qDebug("importWallpaper(): calling clipImageBufferToScreenSizeWithFocus...\n");
        image = clipImageToScreenSizeWithFocus(image,
                image.width() * centerX, image.height() * centerY);

        // and write out the file
        image.save(QString::fromStdString(destPathAndFile), 0, 100);
        qDebug("importWallpaper(): wrote final image to file\n");
    }

    //create a thumbnail version in the wallpaper thumbs dir
    if (resizeImage(destPathAndFile, destThumbPathAndFile, THUMBS_WIDTH, THUMBS_HEIGHT, reader.format().data()) != 0) {
        //delete the resized screen wallpaper
        unlink(destPathAndFile.c_str());
        errorText = std::string("couldn't create thumbnail");
        return false;
    }

    m_wallpapers.push_back(sourceFile);
    ret_wallpaperName = sourceFile;
    //all good...
    qDebug("importWallpaper(): complete\n");

    return true;
}

//TODO: contains code to get around not having a BMP writer (just a reader).
bool WallpaperPrefsHandler::importWallpaper_lowMem(std::string& ret_wallpaperName,const std::string& sourcePath,const std::string& sourceFile,
        bool toScreenSize,
        double centerX, double centerY, double scale,
        std::string& errorText) {

    std::string pathAndFile = sourcePath + std::string("/")+sourceFile;
    QImageReader reader(QString::fromStdString(pathAndFile));
    if(!reader.canRead()) {
        qWarning()<<reader.supportedImageFormats();
        errorText = reader.errorString().toStdString();
        return false;
    }

    //does it exist in the wallpaper dir?
    //(trim off the extension)
    std::string destPathAndFile = s_wallpaperDir + std::string("/")+sourceFile;
    std::string destThumbPathAndFile = s_wallpaperThumbsDir +  std::string("/")+sourceFile;

    struct stat stBuf;
    if (lstat(destPathAndFile.c_str(), &stBuf) == 0) {
        //yes...destroy it
        unlink(destPathAndFile.c_str());
    }
    if (lstat(destThumbPathAndFile.c_str(), &stBuf) == 0) {
        //the thumbnail too?...destroy it
        unlink(destThumbPathAndFile.c_str());
    }

    std::list<std::string>::iterator iter=m_wallpapers.begin();
    while (iter != m_wallpapers.end()) {
        if (*iter == sourceFile)
            iter = m_wallpapers.erase(iter);
        else
            iter++;
    }

    //fix scale factor just in case it's negative
    if (scale < 0.0)
        scale *= -1.0;

    //fix scale factor just in case it's negative
    if (scale < 0.0)
        scale *= -1.0;

    //TODO: WARN: strict comparison of float to 0 might fail
    if (qFuzzyCompare(scale, 0.0))
        scale = 1.0;
    int srcWidth = reader.size().width();
    int srcHeight = reader.size().height();

    if ((qFuzzyCompare(scale,1.0)) && (qFuzzyCompare(centerX,0.5)) && (qFuzzyCompare(centerY,0.5)) 
            && (srcWidth == SCREEN_WIDTH) && (srcHeight == SCREEN_HEIGHT))
        toScreenSize = true;

	qDebug("importWallpaper(): parameters: scale = %lf , centerX = %lf , centerY = %lf , toScreenSize? = %s\n",
			scale,centerX,centerY,(toScreenSize ? "True" : "False"));

	int maxDim = (SCREEN_WIDTH > SCREEN_HEIGHT) ? SCREEN_WIDTH : SCREEN_HEIGHT;
	bool result;
	if((srcWidth > maxDim) || (srcHeight > maxDim)) {
		// Image needs to be resized (scaled down)

		double xScale, yScale;
		xScale = (double)srcWidth / (double)SCREEN_WIDTH;
		yScale = (double)srcHeight / (double)SCREEN_HEIGHT;
        qDebug()<<"x/y scale:"<<xScale<<yScale;
		int desiredWidth, desiredHeight;

		double aspectRatio = srcWidth * 1.0 / srcHeight;
		aspectRatio = MAX(aspectRatio, 1.0 / aspectRatio);
		const double maxAllowedAspectRatio = 2.0;

		if (aspectRatio > maxAllowedAspectRatio) {
            qDebug()<<"aspect ratio"<<aspectRatio<<"> max of"<<maxAllowedAspectRatio;

			// Aspect ratio exceeded. just do a straight aspect ratio constrained scale

			if (xScale >= yScale) {
				desiredWidth = (int)((double)srcWidth / xScale);
				desiredHeight = (int)((double)srcHeight / xScale);
			} else {
				desiredWidth = (int)((double)srcWidth / yScale);
				desiredHeight = (int)((double)srcHeight / yScale);
			}
		}
		else {

			// Within aspect ratio. Go for maximum coverage by scaling to fit either width or height

			if (srcWidth > srcHeight) {
				desiredHeight = MIN(srcHeight, maxDim);
				yScale = srcHeight * 1.0 / desiredHeight;
				desiredWidth = srcWidth / yScale;
			}
			else {
				desiredWidth = MIN(srcWidth, maxDim);
				xScale = srcWidth * 1.0 / desiredWidth;
				desiredHeight = srcHeight / xScale;
			}
		}

		result = ImageServices::instance()->ezResize(pathAndFile, destPathAndFile, reader.format().data(), desiredWidth, desiredHeight, errorText);
	} else {
		// Don't resize the image, SysMgr can handle this.
		result = (0 < Utils::fileCopy(pathAndFile.c_str(), destPathAndFile.c_str()));
	}

	//create a thumbnail version in the wallpaper thumbs dir
	if (resizeImage(destPathAndFile, destThumbPathAndFile, THUMBS_WIDTH, THUMBS_HEIGHT, reader.format().data()) != 0) {
		//delete the resized screen wallpaper
		unlink(destPathAndFile.c_str());
		errorText = std::string("couldn't create thumbnail");
		return false;
	}
	
	m_wallpapers.push_back(sourceFile);
	ret_wallpaperName = sourceFile;
	//all good...
	qDebug("importWallpaper(): complete\n");
	return result;
}

bool WallpaperPrefsHandler::convertImage(const std::string& pathToSourceFile,
                                         const std::string& pathToDestFile, const char* format,
                                         bool justConvert,
                                         double centerX, double centerY, double scale,
                                         std::string& r_errorText)
{
	int destHeight,destWidth;

    QImageReader reader(QString::fromStdString(pathToSourceFile));
    if(!reader.canRead()) {
		r_errorText = reader.errorString().toStdString();
		return false;
	}

	//fix scale factor just in case it's negative
	if (scale < 0.0)
		scale *= -1.0;

	//fix scale factor just in case it's negative
	if (scale < 0.0)
		scale *= -1.0;

	//TODO: WARN: strict comparison of float to 0 might fail
	if (qFuzzyCompare(scale,0.0))
		scale = 1.0;


    qDebug("convertImage(): parameters: scale = %lf , centerX = %lf , centerY = %lf\n",
            scale,centerX,centerY);

    // used to scale the file before it is actually read to memory
    double prescale = 1.0;
    QImage image;
    if (!readImageWithPrescale(reader, image, prescale)) {
        r_errorText = reader.errorString().toStdString();
        return false;
    }
    //scale the image as requested...factor in whatever the prescale did
    scale /= prescale;

    if (scale != 1.0) {
        qDebug("convertImage(): scaling image\n");
        image = image.scaled(scale * image.width(), scale * image.height());
    }

    if (!justConvert) {
        //now refocus as requested
        qDebug("convertImage(): calling clipImageBufferToScreenSizeWithFocus...\n");
        image = clipImageToScreenSizeWithFocus(image, image.width() * centerX, image.height() * centerY);

        destWidth = SCREEN_WIDTH;
        destHeight = SCREEN_HEIGHT;
        qDebug("convertImage(): clipImageBufferToScreenSizeWithFocus Ok\n");
    }

    //and write out the file
    image.save(QString::fromStdString(pathToDestFile), format, 100);
    qDebug("convertImage(): wrote final image to file\n");
    return true;
}

bool WallpaperPrefsHandler::deleteWallpaper(std::string wallpaperName) {
	
	//does it exist in the wallpaper dir?
	std::string destPathAndFile = s_wallpaperDir + std::string("/")+wallpaperName;
	std::string destThumbPathAndFile = s_wallpaperThumbsDir +  std::string("/")+wallpaperName;

	bool found;		//a "loose" indicator of success. It will be true if *any* action was taken
	struct stat stBuf;
	//if the wallpaper is the current one set, then return false
	if (wallpaperName == this->m_currentWallpaperName)
		return false;
	
	if (lstat(destPathAndFile.c_str(), &stBuf) == 0) {
		//yes...destroy it
		unlink(destPathAndFile.c_str());
		found = true;
	}
	if (lstat(destThumbPathAndFile.c_str(), &stBuf) == 0) {
		//the thumbnail too?...destroy it
		unlink(destThumbPathAndFile.c_str());
		found=true;
	}

	std::list<std::string>::iterator iter=m_wallpapers.begin();
	while (iter != m_wallpapers.end()) {
		if (*iter == wallpaperName) {
			found = true;
			iter = m_wallpapers.erase(iter);
		}
		else
			iter++;
	}

	return found;
}

/*
 * 	Makes the image size == to screen size. 
 * In case the image is LARGER than screen size, it just cuts off the bigger pieces
 * In case the image is SMALLER than screen size, it mounts the picture in a black RGBA(0,0,0,255) border
 *  	
 * 	'center' determines whether cut (LARGER) or mount (SMALLER) happens relative to the center of the image
 * 		true = if cut, the cut is taken evenly along all sides (center-cut)
 * 				if mount, the image is mounted in the center
 * 		false = if cut, anything >= SCREEN_WIDTH on the right edge is cut and anything >= SCREEN_HEIGHT on the bottom edge is cut
 * 				if mount, the image is mounted with (0,0) aligned, and anything in dest image (x,y) such that (x,y) >= (srcImgW,srcImgH)
 * 				 is blackfilled
 * 
 */
QImage WallpaperPrefsHandler::clipImageToScreenSize(QImage& image, bool center)
{
    if (image.width() == SCREEN_WIDTH && image.height() == SCREEN_HEIGHT)
        return image;

    QImage result(SCREEN_WIDTH, SCREEN_HEIGHT, image.format());

    result.fill(Qt::black);
    int halfScreenW = SCREEN_WIDTH>>1;
    int halfScreenH = SCREEN_HEIGHT>>1;

    QPainter p(&result);
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    if(center) {
        p.translate(-image.width()/2, -image.height());
        p.translate(halfScreenW, halfScreenH);
    }
    p.drawImage(QPoint(0,0), image);
    p.end();
    return result;
}

/*
 * 	Makes the image size == to screen size, but the image center is now focus_x,focus_y
 *  
 * 	focus_x,focus_y determines the center point of the image. In other words, the final image has the src img's (focus_x,focus_y)
 * 	at its center, and extends +/- SCREEN_WIDTH/2 horizontally and +/- SCREEN_HEIGHT/2 vertically
 * 
 * 	any region of the source image that is not within UL(focus_x-SCREEN_WIDTH/2,focus_y-SCREEN_HEIGHT/2)
 * 													  LR(focus_x+SCREEN_WIDTH/2,focus_y+SCREEN_HEIGHT/2)
 * 	...is matted black
 */

QImage WallpaperPrefsHandler::clipImageToScreenSizeWithFocus(QImage& image, int focus_x,int focus_y)
{
    if (focus_x < 0)
        focus_x = 0;
    if(focus_x > image.width())
        focus_x = image.width();
    if(focus_y < 0)
        focus_y = 0;
    if(focus_y >= image.height())
        focus_y = image.height();

    qDebug("clipImageBufferToScreenSizeWithFocus(): srcImg is ( %d , %d )\n",image.width(),image.height());
    qDebug("clipImageBufferToScreenSizeWithFocus(): focus is ( %d , %d )\n",focus_x,focus_y);

    QImage result(SCREEN_WIDTH, SCREEN_HEIGHT, image.format());

    result.fill(Qt::black);
    int halfScreenW = SCREEN_WIDTH>>1;
    int halfScreenH = SCREEN_HEIGHT>>1;

    QPainter p(&result);
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    p.translate(-focus_x, -focus_y);
    p.translate(halfScreenW, halfScreenH);
    p.drawImage(QPoint(0,0), image);
    p.end();
    return result;
}

int WallpaperPrefsHandler::resizeImage(const std::string& sourceFile,
                                       const std::string& destFile,
                                       int destImgW, int destImgH,
                                       const char* format)
{
    if ((destImgW <= 0) || (destImgH <= 0))
        return -1;

    QImage image;
    if(!image.load(QString::fromStdString(sourceFile)))
        return -1;

    if ((image.width() == destImgW) && (image.height() == destImgH)) {
        // already desired size - just copy
        int fcopyRc = Utils::fileCopy(sourceFile.c_str(),destFile.c_str());
        if (fcopyRc <= 0) {
            qDebug()<<"error copying to"<<QString::fromStdString(destFile);
            return EIO;
        }
    }
    QImage result(destImgW, destImgH, image.format());

    QPainter p(&result);
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    p.drawImage(QRect(0,0,destImgW, destImgH), image);
    p.end();

    int quality=-1;
    // png does not save with quality 100 for some reason...
    if(QString(format) == "jpeg")
         quality = 100;

    QImageWriter w(QString::fromStdString(destFile), format);
    w.setQuality(100);
    qDebug()<<"saving with quality"<<w.quality()<<format;
    printf("writing...\n");
    w.write(result);
    printf("done writing...\n");
    qDebug()<<"writer:"<<w.errorString();

/*    if(!result.save(QString::fromStdString(destFile), format, quality)) {
        qDebug()<<"error saving"<<QString::fromStdString(destFile) <<"as format"<<format;
        return -1;
    }*/

    return 0;
}

static int
one (const struct dirent*)
{
	return 1;
}

/*
 * just builds the m_wallpapers index from existing files in the wallpaper dir
 * Don't do any rescaling for thumbnails
 * 
 * In order to be considered, the wallpaper must have both a main pic and a thumbnail
 * 
 * pass in a pointer to an int and it will place the number of "invalid" (i.e. missing thumbnail)
 * wallpapers in there (call scanForWallpapers(true) afterwards to fix)
 * 
 */
const std::list<std::string>& WallpaperPrefsHandler::buildIndexFromExisting(int * nInvalid)
{
	if (s_wallpaperDir.length() == 0)
		return m_wallpapers;

	std::string path = s_wallpaperDir;
	if (path[path.size() - 1] != '/')
		path += '/';

	std::string thumbpath = s_wallpaperThumbsDir;
	if (thumbpath[thumbpath.size() - 1] != '/')
		thumbpath += '/';

	struct dirent **entries=0;

	int n=0;

	m_wallpapers.clear();

	std::map<std::string,char> thumbExistenceMap;
	int count = scandir(thumbpath.c_str(),&entries,one,alphasort);
	int i;
	for (i = 0;i < count; ++i) {

		if (entries[i]->d_name[0] == '.') {
			continue;
		}

		if (entries[i]->d_type == DT_DIR) {
			continue;
		}

		if (entries[i]->d_type == DT_REG) {
			std::string p = path + entries[i]->d_name;

			//add to the map
			thumbExistenceMap[std::string(entries[i]->d_name)] = ' ';
		}

	}

	for (i = 0; i < count; ++i) {
		free(entries[i]);
	}

	if (entries) {
		free(entries);
		entries=0;
	}

	count = scandir(path.c_str(), &entries, one, alphasort);
	if (count < 0)
		return m_wallpapers;

	for (i = 0; i < count; ++i) {

		if (entries[i]->d_name[0] == '.') {
			continue;
		}

		/*
		 * NOT SUPPORTING RECURSIVE DIRS FOR WALLPAPERS CURRENTLY
		 * 
			if (entries[i]->d_type == DT_DIR) {
				std::string p = path;
				p += entries[i]->d_name;
				p += "/";
				scanTimeZoneFolder(p, len, zoneInfoList);
			}
		 */

		if (entries[i]->d_type == DT_REG) {

			//does it already have a thumbnail?
			std::map<std::string,char>::iterator find_it = thumbExistenceMap.find(std::string(entries[i]->d_name));
			if (find_it == thumbExistenceMap.end()) {
				//doesn't have a thumbnail...increment invalid count
				++n;
				continue;
			}

            QImageReader reader(QString::fromStdString(path + entries[i]->d_name));
            // UNSUPPORTED FILE TYPE
            // not incrementing invalid count since there isn't a lot I can do about this wallpaper 
            // even if I call scanForWallpapers()
            if(!reader.canRead())
                continue;

            m_wallpapers.push_back(std::string(entries[i]->d_name));
		}
	}

	for (i = 0; i < count; ++i) {
		free(entries[i]);
	}

	if (entries)
		free(entries);	

	if (nInvalid)
		*nInvalid = n;
	
	return m_wallpapers;
		
}

const std::list<std::string>& WallpaperPrefsHandler::scanForWallpapers(bool rebuild)
{
	if (s_wallpaperDir.length() == 0)
		return m_wallpapers;

	std::string path = s_wallpaperDir;
	if (path[path.size() - 1] != '/')
		path += '/';

	std::string thumbpath = s_wallpaperThumbsDir;
	if (thumbpath[thumbpath.size() - 1] != '/')
		thumbpath += '/';

	struct dirent **entries;

	int rc;
	std::map<std::string,char> thumbExistenceMap;
	int count = scandir(thumbpath.c_str(),&entries,one,alphasort);
	int i;
	for (i = 0;i < count; ++i) {

		if (entries[i]->d_name[0] == '.') {
			continue;
		}

		if (entries[i]->d_type == DT_DIR) {
			continue;
		}

		if (entries[i]->d_type == DT_REG) {
			std::string p = path + entries[i]->d_name;
			
			//add to the map
			thumbExistenceMap[std::string(entries[i]->d_name)] = ' ';
		}

	}

	for (i = 0; i < count; ++i) {
		free(entries[i]);
	}

	free(entries);	

	count = scandir(path.c_str(), &entries, one, alphasort);
	if (count < 0)
		return m_wallpapers;

	for (i = 0; i < count; ++i) {

		if (entries[i]->d_name[0] == '.') {
			continue;
		}

		/*
		 * NOT SUPPORTING RECURSIVE DIRS FOR WALLPAPERS CURRENTLY
		 * 
		if (entries[i]->d_type == DT_DIR) {
			std::string p = path;
			p += entries[i]->d_name;
			p += "/";
			scanTimeZoneFolder(p, len, zoneInfoList);
		}
		*/

		if (entries[i]->d_type == DT_REG) {

			//does it already have a thumbnail?
			std::map<std::string,char>::iterator find_it = thumbExistenceMap.find(std::string(entries[i]->d_name));
			if ((find_it != thumbExistenceMap.end()) && (!rebuild)) {
				//yup, already got this one...skip
				continue;
			}

            std::string p = path + entries[i]->d_name;
            QImageReader reader(QString::fromStdString(p));
            if(!reader.canRead())
                continue;

            if (reader.format() == "png") {
                rc = WallpaperPrefsHandler::resizeImage(p, thumbpath+(entries[i]->d_name), THUMBS_WIDTH, THUMBS_HEIGHT, reader.format());
                if (rc == 0) {
                    //success...
                    m_wallpapers.push_back(std::string(entries[i]->d_name));
                }
            }
            else if (reader.format() == "jpg") {
                // why do we not create thumbs for jpgs?
                qWarning() << "Can\'t create thumbnails for JPGs" << entries[i]->d_name;
            }
		}
	}

	for (i = 0; i < count; ++i) {
		free(entries[i]);
	}

	free(entries);	

	return m_wallpapers;
}

bool WallpaperPrefsHandler::makeLocalUrlsFromWallpaperName(std::string& wallpaperUrl,std::string& wallpaperThumbUrl,const std::string& wallpaperName) {
	
	if (wallpaperName.length() > 0) {
		wallpaperUrl = std::string("file://")+s_wallpaperDir+std::string("/")+wallpaperName;
		wallpaperThumbUrl = std::string("file://")+s_wallpaperThumbsDir+std::string("/")+wallpaperName;
		return true;
	}
	return false;
}

bool WallpaperPrefsHandler::makeLocalPathnamesFromWallpaperName(std::string& wallpaperUrl,std::string& wallpaperThumbUrl,const std::string& wallpaperName) {
	
	if (wallpaperName.length() > 0) {
		wallpaperUrl = s_wallpaperDir+std::string("/")+wallpaperName;
		wallpaperThumbUrl = s_wallpaperThumbsDir+std::string("/")+wallpaperName;
		return true;
	}
	return false;
}

bool WallpaperPrefsHandler::getWallpaperSpecFromName(const std::string& wallpaperName,std::string& wallpaperFile,std::string& wallpaperThumbFile) {
	
	//try to match the given wallpaper to one of the ones in the list
	std::list<std::string>::iterator it;
	for (it = m_wallpapers.begin();it != m_wallpapers.end();it++) {
		if (wallpaperName == (*it))
			break;
	}

	if (it == m_wallpapers.end())
		return false;

	return makeLocalPathnamesFromWallpaperName(wallpaperFile,wallpaperThumbFile,wallpaperName);
}

bool WallpaperPrefsHandler::getWallpaperSpecFromFilename(std::string& wallpaperName,std::string& wallpaperFile,std::string& wallpaperThumbFile) {
	
	//separate the name from the filename
	UrlRep url = UrlRep::fromUrl(wallpaperFile.c_str());
	if (url.valid ==  false)
		return false;

	wallpaperName = url.resource;
	//try to match the given wallpaper to one of the ones in the list
	std::list<std::string>::iterator it;
	for (it = m_wallpapers.begin();it != m_wallpapers.end();it++) {
		if (wallpaperName == (*it))
			break;
	}

	if (it == m_wallpapers.end())
		return false;

	return makeLocalPathnamesFromWallpaperName(wallpaperFile,wallpaperThumbFile,wallpaperName);
}

/*!
\page com_palm_systemservice_wallpaper
\n
\section com_palm_systemservice_wallpaper_import_wallpaper importWallpaper

\e Public.

com.palm.systemservice/wallpaper/importWallpaper

Converts an image to a wallpaper for the device. The image is either re-centered and cropped, or scaled:

\li If no focus or scale parameters are passed, importWallpaper scales the image to fill the screen.
\li If focus parameters are passed but scale is not specified, the image is re-centered at the point specified by the focus parameters and cropped. Black is added anywhere the image does not reach the edge of the screen.
\li If scale is passed but focus is not specified, then the image is scaled and then cropped.
\li If focus and scale parameters are passed, the image is first scaled and then re-centered and cropped.

The focusX and focusY parameters are the coordinates of the new center of the image. The scale parameter determines the new size of the image.

Once the image has been converted, the wallpaper is stored in the internal list of wallpapers on the device, and is available until deleted using deleteWallpaper.

\subsection com_palm_systemservice_wallpaper_import_wallpaper_syntax Syntax:
\code
{
    "target": string,
    "focusX": double,
    "focusY": double,
    "scale": double
}
\endcode

\param target Path to the image file. Required.
\param focusX The horizontal coordinate of the new center of the image, from 0.0 (left edge) to 1.0 (right edge). A value of 0.5 preserves the current horizontal center of the image.
\param focusY The vertical coordinate of the new center of the image, from 0.0 (top edge) to 1.0 (bottom edge). A value of 0.5 preserves the current vertical center of the image.
\param scale Scale factor for the image, must be greater than zero.

\subsection com_palm_systemservice_wallpaper_import_wallpaper_returns Returns:
\code
{
    "returnValue": boolean,
    "wallpaper": {
        "wallpaperName": string,
        "wallpaperFile": string,
        "wallpaperThumbFile": string
    },
    "errorText": string
}
\endcode

\param returnValue Indicates if the call was succesful or not.
\param wallpaper A wallpaper object that can be passed to setPreferences to set the wallpaper key. See fields below.
\param wallpaperName Name of wallpaper file.
\param wallpaperFile Path to wallpaper file.
\param wallpaperThumbFile Path to wallpaper thumb file.
\param errorText Description of the error if call was not succesful.

\subsection com_palm_systemservice_wallpaper_import_wallpaper_examples Examples:
\code
luna-send -n 1 -f luna://com.palm.systemservice/wallpaper/importWallpaper '{ "src": "/media/internal/.wallpapers/flowers.png" }'
\endcode

Example response for a succesful call:
\code
{
    "returnValue": true,
    "wallpaper": {
        "wallpaperName": "flowers.png",
        "wallpaperFile": "\/media\/internal\/.wallpapers\/flowers.png",
        "wallpaperThumbFile": "\/media\/internal\/.wallpapers\/thumbs\/flowers.png"
    }
}
\endcode

Example response for a failed call:
\code
{
    "returnValue": false,
    "errorText": ""
}
\endcode
*/
static bool cbImportWallpaper(LSHandle* lsHandle, LSMessage *message,
							void *user_data)
{
    // {"target": string, "focusX": double, "focusY": double, "scale": double}
    VALIDATE_SCHEMA_AND_RETURN(lsHandle,
                               message,
                               SCHEMA_4(REQUIRED(target, string), OPTIONAL(focusX, double), OPTIONAL(focusY, double), OPTIONAL(scale, double)));

    bool        retVal = false;
	LSError     lsError;
	std::string result;
	const char* reply = 0;
	json_object * jsonReplyObject = 0;

	bool success = false;
	std::string input;
	json_object* json = 0;
	std::string errorText;
	UrlRep urlRep;
	double scaleFactor;
	double fx,fy;
	bool toScreenSize;
	int c;
	std::string wallpaperName;		//the name of the wallpaper will be returned here if successful

	WallpaperPrefsHandler* wh = (WallpaperPrefsHandler*) user_data;

	LSErrorInit(&lsError);

	const char* str = LSMessageGetPayload(message);
    qDebug()<<"payload:<"<<str<<">";
	if( !str )
		return false;

	struct json_object* root = json_tokener_parse(str);
	struct json_object* label = 0;
	
	if (is_error(root)) {
		root = 0;
		success = false;
		errorText = std::string("couldn't parse json :(");
        qDebug()<<"could not parse JSON in" <<str;
		goto Done;
	}

	if (wh == NULL) {
		errorText = std::string("lunabus handler error; luna didn't pass a valid instance var to handler");
		goto Done;
	}

	label = json_object_object_get(root,"target");
	if (label) {
		input = json_object_get_string(label);
	}
	else {
		errorText = std::string("no input file specified");
		goto Done;
	}

	if (input.empty()) {
		errorText = std::string("empty input file path specified");
		goto Done;
	}

	if (input[0] == '/') {
		// Just a regular file.
	}
	else {
		// potentially a url

		urlRep = UrlRep::fromUrl(input.c_str());

		if (urlRep.valid == false) {
			errorText = std::string("invalid specification for input file (please use url format)");
			goto Done;
		}
	
		// UNSUPPORTED: non-file:// schemes (not supporting directly fetching remote wallpaper just yet)
		if ((urlRep.scheme != "") && (urlRep.scheme != "file")) {
			errorText = std::string("input file specification doesn't support non-local files (use file:///path/file or /path/file format");
			goto Done;
		}

		input = urlRep.path;
	}

	scaleFactor = 1.0;
	fx = 0.5;fy=0.5;
	c=0;
	//attempt to get additional parameters
	label = json_object_object_get(root,"focusX");
	if ((label) && (!is_error(label))) {
		fx = json_object_get_double(label);
		++c;
	}
	label = json_object_object_get(root,"focusY");
	if ((label) && (!is_error(label))) {
		fy = json_object_get_double(label);
		++c;
	}
	label = json_object_object_get(root,"scale");
	if ((label) && (!is_error(label))) {
		scaleFactor = json_object_get_double(label);
		++c;
	}

	if (c)						//if any of the specifiers were present, then don't use the default scaling
		toScreenSize=false;
	else
		toScreenSize=true;		//default if params were missing

	//delegate to a class member function from here on in
	
	//is com.palm.image2 available?
	if (Settings::settings()->m_image2svcAvailable && Settings::settings()->m_useComPalmImage2)
	{
        qDebug()<<"using Image2 for import.";
		//yes. Use it for wallpaper import
		success = wh->importWallpaperViaImage2(input,fx,fy,scaleFactor,&jsonReplyObject);
	}
	else
	{
        qDebug()<<"import method.";
		//use the older method here
		success = wh->importWallpaper(wallpaperName,input,toScreenSize,fx,fy,scaleFactor,errorText);
	}

Done:

	if (root)
		json_object_put(root);
	
	if (jsonReplyObject)
	{
		json_object_put(jsonReplyObject);
	}

	json = json_object_new_object();
	json_object_object_add(json, (char*) "returnValue", json_object_new_boolean(success));
	if (!success) {
		json_object_object_add(json,(char*) "errorText",json_object_new_string(const_cast<char*>(errorText.c_str())));
	}
	else {
		std::string wallpaperFile;
		std::string wallpaperThumbFile;
		json_object * inner_json = json_object_new_object();
		WallpaperPrefsHandler::makeLocalPathnamesFromWallpaperName(wallpaperFile,wallpaperThumbFile,wallpaperName);
		json_object_object_add(inner_json,(char*) "wallpaperName",json_object_new_string(const_cast<char*>(wallpaperName.c_str())));
		json_object_object_add(inner_json,(char*) "wallpaperFile",json_object_new_string(const_cast<char*>(wallpaperFile.c_str())));
		json_object_object_add(inner_json,(char*) "wallpaperThumbFile",json_object_new_string(const_cast<char*>(wallpaperThumbFile.c_str())));
		json_object_object_add(json,(char*) "wallpaper",inner_json);
	}

	reply = json_object_to_json_string(json);
		
	if (reply)
		retVal = LSMessageReply(lsHandle, message, reply, &lsError);
	if (!retVal)
		LSErrorFree (&lsError);

	if (json)
		json_object_put(json);

	return true;
}

/*!
\page com_palm_systemservice_wallpaper
\n
\section com_palm_systemservice_wallpaper_convert convert

\e Public.

com.palm.systemservice/wallpaper/convert

Converts an image. The type, scaling and centering of the image may be changed. If the resulting image is would be smaller than the original, black is added to the edges so that the resulting image is the same size as the original. If the resulting image would be bigger than the original, the image is cropped.

\subsection com_palm_systemservice_wallpaper_convert_syntax Syntax:
\code
{
    "source": string,
    "destType": string,
    "dest": string,
    "focusX": double,
    "focusY": double,
    "scale": double
}
\endcode

\param source Path to the source file. Required.
\param source destType Type for the destination file. Can be "jpg", "png" or "bmp". Required.
\param source dest Path for the destination file.
\param focusX The horizontal coordinate of the new center of the image, from 0.0 (left edge) to 1.0 (right edge). A value of 0.5 preserves the current horizontal center of the image.
\param focusY The vertical coordinate of the new center of the image, from 0.0 (top edge) to 1.0 (bottom edge). A value of 0.5 preserves the current vertical center of the image.
\param scale Scale factor for the image, must be greater than zero.

\subsection com_palm_systemservice_wallpaper_convert_returns Returns:
\code
{
    "returnValue": boolean,
    "conversionResult": {
        "source": string,
        "dest": string,
        "destType": string
    },
    "errorText": string
}
\endcode

\param returnValue Indicates if the call was succesful.
\param conversionResult Object containing information of the converted image. See fields below.
\param source Path to the original file.
\param dest Path to the output file.
\param destType Type of the output file.
\param errorText Description of the error if call was not succesful.

\subsection com_palm_systemservice_wallpaper_convert_examples Examples:
\code
luna-send -n 1 -f luna://com.palm.systemservice/wallpaper/convert '{ "source": "/usr/lib/luna/system/luna-systemui/images/flowers.png", "destType": "jpg", "dest": "/usr/lib/luna/system/luna-systemui/images/scaled_flowers.jpg", "focusX": 0.75, "focusY": 0.75, "scale" : 2 }'
\endcode

Example response for a succesful call:
\code
{
    "returnValue": true,
    "conversionResult": {
        "source": "\/\/usr\/lib\/luna\/system\/luna-systemui\/images\/flowers.png",
        "dest": "\/\/usr\/lib\/luna\/system\/luna-systemui\/images\/scaled_flowers.jpg",
        "destType": "jpg"
    }
}
\endcode

Example response for a failed call:
\code
{
    "returnValue": false,
    "errorText": "no output type ( jpg , png , bmp ) specified"
}
\endcode
*/
static bool cbConvertImage(LSHandle* lsHandle, LSMessage *message,
							  void *user_data)
{
    // {"source": string, "destType": string, "dest": string, "focusX": double, "focusY": double, "scale": double}
    VALIDATE_SCHEMA_AND_RETURN(lsHandle,
                               message,
                               SCHEMA_6(REQUIRED(source, string), REQUIRED(destType, string), OPTIONAL(dest, string), OPTIONAL(focusX, double), OPTIONAL(focusY, double), OPTIONAL(scale, double)));

	bool        retVal=false;
	LSError     lsError;
	std::string result;
	const char* reply = 0;

	bool success = false;
	std::string input;
	json_object* json = 0;
	std::string errorText;
	UrlRep srcUrlRep,destUrlRep;
	double scaleFactor;
	double fx,fy;
	bool justConvert=true;
	WallpaperPrefsHandler* wh = (WallpaperPrefsHandler*) user_data;

	std::string sourceFile,destFile;
	std::string sourceFileEncoded,destFileEncoded;
	std::string tempDestFileExtn;
	std::string destTypeStr;

	LSErrorInit(&lsError);

	const char* str = LSMessageGetPayload(message);
	if( !str )
		return false;

	struct json_object* root = json_tokener_parse(str);
	struct json_object* label = 0;

	if (is_error(root)) {
		root = 0;
		success = false;
		errorText = std::string("couldn't parse json");
        qDebug()<<"could not parse JSON in" <<str;
		goto Done;
	}

	if (wh == NULL) {
		errorText = std::string("lunabus handler error; luna didn't pass a valid instance var to handler");
		goto Done;
	}

	if (Utils::extractFromJson(root,"source",sourceFile) == false) {
		errorText = std::string("no input file specified");
		goto Done;
	}

	if (Utils::extractFromJson(root,"destType",destTypeStr) == false) {
		errorText = std::string("no output type ( jpg , png , bmp ) specified");
		goto Done;
	}

	if (destTypeStr == "jpg") {
		tempDestFileExtn = ".jpg";
	}
	else if (destTypeStr == "png") {
		tempDestFileExtn = ".png";
	}
	else if (destTypeStr == "bmp") {
		tempDestFileExtn = ".bmp";
	}
	else {
		tempDestFileExtn = ".raw";
	}

	if (Utils::extractFromJson(root,"dest",destFile) == false) {
		if (Utils::createTempFile(std::string(PrefsDb::s_mediaPartitionPath)+std::string(PrefsDb::s_mediaPartitionTempDir)
									,std::string("image")
									,tempDestFileExtn
									,destFile) == 0) {
			errorText = std::string("no destination file specified and couldn't create temp file");
			goto Done;
		}
	}
	

	//parse URLs

	//urlencode the sourceFile and destFile, because they may contain things UrlRep can't deal with..

	//(1) if the source file is already encoded (or partially encoded  ), we'll have problems, so decode it first. If it wasn't encoded, this won't change it
	sourceFileEncoded = sourceFile;
	Utils::urlDecodeFilename(sourceFileEncoded,sourceFile);
	//(2) same with dest file
	destFileEncoded = destFile;
	Utils::urlDecodeFilename(destFileEncoded,destFile);

	//now, (re)encode them both fully for safety
	Utils::urlEncodeFilename(sourceFileEncoded,sourceFile);
	Utils::urlEncodeFilename(destFileEncoded,destFile);

	srcUrlRep = UrlRep::fromUrl(sourceFileEncoded.c_str());

	if (srcUrlRep.valid == false) {
		errorText = std::string("invalid specification for input file (please use url format)");
		goto Done;
	}
	// UNSUPPORTED: non-file:// schemes (not supporting directly fetching remote wallpaper just yet)
	if ((srcUrlRep.scheme != "") && (srcUrlRep.scheme != "file")) {
		errorText = std::string("input file specification doesn't support non-local files (use file:///path/file or /path/file format");
		goto Done;
	}

	destUrlRep = UrlRep::fromUrl(destFileEncoded.c_str());
	
	if (destUrlRep.valid == false) {
		errorText = std::string("invalid specification for output file (please use url format)");
		goto Done;
	}
	// UNSUPPORTED: non-file:// schemes (not supporting directly fetching remote wallpaper just yet)
	if ((destUrlRep.scheme != "") && (destUrlRep.scheme != "file")) {
		errorText = std::string("output file specification doesn't support non-local files (use file:///path/file or /path/file format");
		goto Done;
	}

	scaleFactor = 1.0;
	fx = 0.5;fy=0.5;

	//attempt to get additional parameters
	label = json_object_object_get(root,"focusX");
	if ((label) && (!is_error(label))) {
		fx = json_object_get_double(label);
		justConvert=false;
	}
	label = json_object_object_get(root,"focusY");
	if ((label) && (!is_error(label))) {
		fy = json_object_get_double(label);
		justConvert=false;
	}
	label = json_object_object_get(root,"scale");
	if ((label) && (!is_error(label))) {
		scaleFactor = json_object_get_double(label);
		justConvert=false;
	}

	success = wh->convertImage(
			srcUrlRep.path,
			destUrlRep.path,
            destTypeStr.c_str(),
			justConvert,
			fx,
			fy,
			scaleFactor,
			errorText);

	Done:

	if (root)
		json_object_put(root);

	json = json_object_new_object();
	json_object_object_add(json, (char*) "returnValue", json_object_new_boolean(success));
	if (!success) {
		json_object_object_add(json,(char*) "errorText",json_object_new_string(const_cast<char*>(errorText.c_str())));
        qWarning(errorText.c_str());
	}
	else {
		json_object * inner_json = json_object_new_object();
		json_object_object_add(inner_json,(char*) "source",json_object_new_string(const_cast<char*>(srcUrlRep.path.c_str())));
		json_object_object_add(inner_json,(char*) "dest",json_object_new_string(const_cast<char*>(destUrlRep.path.c_str())));
		json_object_object_add(inner_json,(char*) "destType",json_object_new_string(const_cast<char*>(destTypeStr.c_str())));
		json_object_object_add(json,(char*) "conversionResult",inner_json);
	}

	reply = json_object_to_json_string(json);

	if (reply)
		retVal = LSMessageReply(lsHandle, message, reply, &lsError);
	if (!retVal)
		LSErrorFree (&lsError);

	if (json)
		json_object_put(json);

	return true;
}

/*!
\page com_palm_systemservice_wallpaper
\n
\section com_palm_systemservice_wallpaper_refresh refresh

\e Public.

com.palm.systemservice/wallpaper/refresh

Refreshes the internal list of available wallpapers. Under normal circumstances, there is no need to call refresh directly.

\subsection com_palm_systemservice_wallpaper_refresh_syntax Syntax:
\code
{
}
\endcode

\subsection com_palm_systemservice_wallpaper_refresh_returns Returns:
\code
{
    "returnValue": boolean,
    "errorText": string
}
\endcode

\param returnValue Indicates if the call was succesful.
\param errorText Description of the error if call was not succesful.

\subsection com_palm_systemservice_wallpaper_refresh_examples Examples:
\code
luna-send -n 1 -f luna://com.palm.systemservice/wallpaper/refresh '{}'
\endcode

Example response for a succesful call:
\code
{
    "returnValue": true
}
\endcode
*/
static bool cbRefreshWallpaperIndex(LSHandle* lsHandle, LSMessage *message,
							void *user_data) 
{
    EMPTY_SCHEMA_RETURN(lsHandle,message);

	bool        retVal;
	LSError     lsError;
	std::string result;
	const char* reply = 0;
	json_object * json;

	std::string errorText;

	WallpaperPrefsHandler* wh = (WallpaperPrefsHandler*) user_data;
	if (wh) {
		wh->scanForWallpapers(true);
		retVal=true;
	}
	else {
		retVal=false;
		errorText = std::string("lunabus handler error; luna didn't pass a valid instance var to handler");
	}

	json = json_object_new_object();
	json_object_object_add(json, (char*) "returnValue", json_object_new_boolean(retVal));
	if (!retVal) {
		json_object_object_add(json,(char*) "errorText",json_object_new_string(const_cast<char*>(errorText.c_str())));
        qWarning(errorText.c_str());
	}

	reply = json_object_to_json_string(json);

	if (reply)
		retVal = LSMessageReply(lsHandle, message, reply, &lsError);
	if (!retVal)
		LSErrorFree (&lsError);

	if (json)
		json_object_put(json);

	return true;
}

/*!
\page com_palm_systemservice_wallpaper
\n
\section com_palm_systemservice_wallpaper_info info

\e Public.

com.palm.systemservice/wallpaper/info

Retrieves a wallpaper object using either the wallpaperName or wallpaperFile parameter.

\subsection com_palm_systemservice_wallpaper_info_syntax Syntax:
\code
{
    "wallpaperName": string,
    "wallpaperFile": string
}
\endcode

\param wallpaperName Wallpaper name. Either this, or "wallpaperFile" is required.
\param wallpaperFile Wallpaper's full path and file name. Either this, or "wallpaperName" is required.

\subsection com_palm_systemservice_wallpaper_info_returns Returns:
\code
{
   "returnValue" : boolean,
   "wallpaper"   : {
      "wallpaperName"      : string,
      "wallpaperFile"      : string,
      "wallpaperThumbFile" : string
   }
   "errorText" : string
}
\endcode

\param returnValue Indicates if the call was succesful or not.
\param wallpaper A wallpaper object that can be passed to setPreferences to set the wallpaper key. See fields below.
\param wallpaperName Name of wallpaper file.
\param wallpaperFile Path to wallpaper file.
\param wallpaperThumbFile Path to wallpaper thumb file.
\param errorText Description of the error if call was not succesful.

\subsection com_palm_systemservice_wallpaper_info_examples Examples:
\code
luna-send -n 1 -f luna://com.palm.systemservice/wallpaper/info '{ "wallpaperName": "flowers.png" }'
\endcode
\code
luna-send -n 1 -f luna://com.palm.systemservice/wallpaper/info '{ "wallpaperFile": "/media/internal/.wallpapers/flowers.png" }'
\endcode

Example response for a succesful call:
\code
{
    "returnValue": true,
    "wallpaper": {
        "wallpaperName": "flowers.png",
        "wallpaperFile": "\/media\/internal\/.wallpapers\/flowers.png",
        "wallpaperThumbFile": "\/media\/internal\/.wallpapers\/thumbs\/flowers.png"
    }
}
\endcode

Example response for a failed call:
\code
{
    "returnValue": false,
    "errorText": "invalid wallpaper name specified (perhaps it doesn't exist in the wallpaper dir; was it imported?"
}
\endcode
*/
static bool cbGetWallpaperSpec(LSHandle* lsHandle, LSMessage *message,
							void *user_data) 
{
	bool        retVal;
	LSError     lsError;
	std::string result;
	const char* reply = 0;
	json_object * json;
	json_object * inner_json;
	
	std::string errorText;
	std::string wallpaperName;
	std::string wallpaperFile;
	std::string wallpaperThumbFile;

	LSErrorInit(&lsError);

    // {"wallpaperName": string, "wallpaperFile": string}
    VALIDATE_SCHEMA_AND_RETURN(lsHandle,
                               message,
                               SCHEMA_2(REQUIRED(wallpaperName, string), REQUIRED(wallpaperFile, string)));

	const char* str = LSMessageGetPayload(message);
	if( !str )
		return false;

	struct json_object* root = json_tokener_parse(str);
	struct json_object* label = 0;

	WallpaperPrefsHandler* wh = (WallpaperPrefsHandler*) user_data;
	
	if (is_error(root)) {
		root = 0;
		retVal = false;
		errorText = std::string("couldn't parse json");
        qDebug()<<"could not parse JSON in" <<str;
		goto Done;
	}

	if (!wh) {
		retVal=false;
		errorText = std::string("lunabus handler error; luna didn't pass a valid instance var to handler");
		goto Done;
	}

	label = json_object_object_get(root,"wallpaperName");
	if ((label) && (!is_error(label))) {
		wallpaperName = json_object_get_string(label);
		retVal = wh->getWallpaperSpecFromName(wallpaperName,wallpaperFile,wallpaperThumbFile);
		if (!retVal)
			errorText = std::string("invalid wallpaper name specified (perhaps it doesn't exist in the wallpaper dir; was it imported?");
		goto Done;
	}

	label = json_object_object_get(root,"wallpaperFile");
	if ((label) && (!is_error(label))) {
		wallpaperFile = json_object_get_string(label);
		retVal = wh->getWallpaperSpecFromFilename(wallpaperName,wallpaperFile,wallpaperThumbFile);
		if (!retVal)
			errorText = std::string("invalid wallpaper file specified (perhaps it doesn't exist in the wallpaper dir; was it imported?");
		goto Done;
	}

	//neither name nor file specified
	retVal = false;
	errorText = std::string("must specify either wallpaperName or wallpaperFile");
	
Done:

	json = json_object_new_object();
	json_object_object_add(json, (char*) "returnValue", json_object_new_boolean(retVal));
	if (!retVal) {
		json_object_object_add(json,(char*) "errorText",json_object_new_string(const_cast<char*>(errorText.c_str())));
        qWarning(errorText.c_str());
	}
	else {
		inner_json = json_object_new_object();
		json_object_object_add(inner_json,(char *)"wallpaperName",json_object_new_string((char*) wallpaperName.c_str()));
		json_object_object_add(inner_json,(char *)"wallpaperFile",json_object_new_string((char*) wallpaperFile.c_str()));
		json_object_object_add(inner_json,(char *)"wallpaperThumbFile",json_object_new_string((char*) wallpaperThumbFile.c_str()));
		json_object_object_add(json,(char *)"wallpaper",inner_json);
	}
	
	reply = json_object_to_json_string(json);

	if (reply)
		retVal = LSMessageReply(lsHandle, message, reply, &lsError);
	if (!retVal)
		LSErrorFree (&lsError);
	
	if (json)
		json_object_put(json);

	return true;
}

/*!
\page com_palm_systemservice_wallpaper
\n
\section com_palm_systemservice_wallpaper_delete_wallpaper deleteWallpaper

\e Public.

com.palm.systemservice/wallpaper/deleteWallpaper

Deletes the specified wallpaper from the list of available wallpapers on the device.

\subsection com_palm_systemservice_wallpaper_delete_wallpaper_syntax Syntax:
\code
{
    "wallpaperName": string
}
\endcode

\param wallpaperName The wallpaperName attribute of the wallpaper object to delete.

\subsection com_palm_systemservice_wallpaper_delete_wallpaper_returns Returns:
\code
{
    "returnValue" : boolean,
    "wallpaper"  : {
        "wallpaperName" : string
    },
    "errorText": string
}
\endcode

\param returnValue Indicates if the call was succesful or not.
\param wallpaper A wallpaper object. See field below.
\param wallpaperName The wallpaperName that was passed to the method.
\param errorText Description of the error if call was not succesful.

\subsection com_palm_systemservice_wallpaper_delete_wallpaper_examples Examples:
\code
luna-send -n 1 -f luna://com.palm.systemservice/wallpaper/deleteWallpaper '{ "wallpaperName": "record-large.png" }'
\endcode

Example response for a succesful call:
\code
{
    "returnValue": true,
    "wallpaper": {
        "wallpaperName": "record-large.png"
    }
}
\endcode

\note The call will be succesful even if there is no wallpaper to match the wallpaperName that was passed as parameter.

Example response for a failed call:
\code
{
    "returnValue": false,
    "errorText": "must specify wallpaperName"
}
\endcode
*/
static bool cbDeleteWallpaper(LSHandle* lsHandle, LSMessage *message,
		void *user_data)
{
	bool        retVal;
	LSError     lsError;
	std::string result;
	const char* reply = 0;
	json_object * json;
	json_object * inner_json;

	std::string errorText;
	std::string wallpaperName;
	
	LSErrorInit(&lsError);

    // {"wallpaperName": string}
    VALIDATE_SCHEMA_AND_RETURN(lsHandle,
                               message,
                               SCHEMA_1(REQUIRED(wallpaperName, string)));

	const char* str = LSMessageGetPayload(message);
	if( !str )
		return false;

	struct json_object* root = json_tokener_parse(str);
	struct json_object* label = 0;

	WallpaperPrefsHandler* wh = (WallpaperPrefsHandler*) user_data;

	if (is_error(root)) {
		root = 0;
		retVal = false;
		errorText = std::string("couldn't parse json");
        qDebug()<<"could not parse JSON in" <<str;
		goto Done;
	}

	if (!wh) {
		retVal=false;
		errorText = std::string("lunabus handler error; luna didn't pass a valid instance var to handler");
		goto Done;
	}

	label = json_object_object_get(root,"wallpaperName");
	if ((label) && (!is_error(label))) {
		wallpaperName = json_object_get_string(label);
		retVal = wh->deleteWallpaper(wallpaperName);
		if (!retVal)
			errorText = std::string("invalid wallpaper name specified (perhaps it doesn't exist in the wallpaper dir");
		goto Done;
	}

	retVal = false;
	errorText = std::string("must specify wallpaperName");

	Done:

	json = json_object_new_object();
	json_object_object_add(json, (char*) "returnValue", json_object_new_boolean(retVal));
	if (!retVal) {
		json_object_object_add(json,(char*) "errorText",json_object_new_string(const_cast<char*>(errorText.c_str())));
        qWarning(errorText.c_str());
	}
	else {
		inner_json = json_object_new_object();
		json_object_object_add(inner_json,(char *)"wallpaperName",json_object_new_string((char*) wallpaperName.c_str()));
		json_object_object_add(json,(char *)"wallpaper",inner_json);
	}

	reply = json_object_to_json_string(json);

	if (reply)
		retVal = LSMessageReply(lsHandle, message, reply, &lsError);
	if (!retVal)
		LSErrorFree (&lsError);

	if (json)
		json_object_put(json);

	return true;
		
}

void WallpaperPrefsHandler::getScreenDimensions()
{
	// Get the display info
	
	int fd = ::open("/dev/fb0", O_RDONLY);
	if (fd >= 0) {
		struct fb_var_screeninfo varinfo;
		::memset(&varinfo, 0, sizeof(varinfo));

		if (::ioctl(fd, FBIOGET_VSCREENINFO, &varinfo) != -1)
		{
			SCREEN_WIDTH = varinfo.xres;
			SCREEN_HEIGHT = varinfo.yres;
			if (((unsigned int)(SCREEN_WIDTH)) > 65536)
			{
                //g_message("%s: fb0 opened, but FBIOGET_VSCREENINFO ioctl gave a bad xres value (%d)",__FUNCTION__, varinfo.xres);
                qWarning() << "fb0 opened, but FBIOGET_VSCREENINFO ioctl gave a bad xres value (%d)" << varinfo.xres;
				SCREEN_WIDTH = 320;
			}
			if (((unsigned int)(SCREEN_HEIGHT)) > 65536)
			{
                //g_message("%s: Failed to open framebuffer device fb0",__FUNCTION__);
                qWarning() << "Failed to open framebuffer device fb0";
				SCREEN_HEIGHT = 480;
			}
		}
		else
		{
            //g_message("%s: fb0 opened, but couldn't execute FBIOGET_VSCREENINFO ioctl",__FUNCTION__);
            qWarning() << "fb0 opened, but couldn't execute FBIOGET_VSCREENINFO ioctl";
			SCREEN_WIDTH = 320;
			SCREEN_HEIGHT = 480;
		}
		::close(fd);
	}
	else
	{
        //g_message("%s: Failed to open framebuffer device fb0",__FUNCTION__);
        qWarning() << "Failed to open framebuffer device fb0";
		SCREEN_WIDTH = 320;
		SCREEN_HEIGHT = 480;
	}

	//override it with the special preference settings, if they exist
	std::string wpref = PrefsDb::instance()->getPref("screenSize.width");
	std::string hpref = PrefsDb::instance()->getPref("screenSize.height");

	if (!(wpref.empty()))
	{
		SCREEN_WIDTH = (int)strtoul(wpref.c_str(), 0, 10);
		if (SCREEN_WIDTH > 65536)
			SCREEN_WIDTH = 320;
	}
	if (!(hpref.empty()))
	{
		SCREEN_HEIGHT = (int)strtoul(hpref.c_str(), 0, 10);
		if (SCREEN_HEIGHT > 65536)
			SCREEN_HEIGHT = 480;
	}

}
