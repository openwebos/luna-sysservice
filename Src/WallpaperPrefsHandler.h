/**
 *  Copyright 2010 - 2012 Hewlett-Packard Development Company, L.P.
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


#ifndef WALLPAPERPREFSHANDLER_H
#define WALLPAPERPREFSHANDLER_H

#include "PrefsHandler.h"

#include <cjson/json.h>
#include <QImage>

class WallpaperPrefsHandler : public PrefsHandler {
	
public:
	
	WallpaperPrefsHandler(LSPalmService* service);
	virtual ~WallpaperPrefsHandler();

	virtual std::list<std::string> keys() const;
	virtual bool validate(const std::string& key, json_object* value);
	virtual void valueChanged(const std::string& key, json_object* value);
	virtual bool validate(const std::string& key, json_object* value, const std::string& originId);
	virtual json_object* valuesForKey(const std::string& key);
	virtual void init();
	
	virtual bool isPrefConsistent();
	virtual void restoreToDefault();
	
	bool importWallpaperViaImage2(const std::string& imageSourceUrl,double centerX,double centerY,double scaleFactor,json_object ** r_p_responseObject);

	bool importWallpaper(std::string& ret_wallpaperName,const std::string& sourcePathAndFile,
							bool toScreenSize,
							double centerX,double centerY,double scale,std::string& errorText);
	bool importWallpaper(std::string& ret_wallpaperName,const std::string& sourcePath,const std::string& sourceFile,
							bool toScreenSize,
							double centerX,double centerY,double scale,std::string& errorText);
		
	bool importWallpaper_lowMem(std::string& ret_wallpaperName,
						const std::string& sourcePath,
						const std::string& sourceFile,
						bool toScreenSize,
						double centerX,
						double centerY,
						double scale,
						std::string& errorText);
		
	bool deleteWallpaper(std::string wallpaperName);
	
    bool convertImage(const std::string& pathToSourceFile,
                      const std::string& pathToDestFile, const char* format,
                      bool justConvert,
                      double centerX, double centerY, double scale,
                      std::string& r_errorText);
	
	const std::list<std::string>& scanForWallpapers(bool rebuild=false); 
	const std::list<std::string>& buildIndexFromExisting(int * nInvalid=NULL);
	
	static bool makeLocalUrlsFromWallpaperName(std::string& wallpaperUrl,std::string& wallpaperThumbUrl,const std::string& wallpaperName);
	static bool makeLocalPathnamesFromWallpaperName(std::string& wallpaperUrl,std::string& wallpaperThumbUrl,const std::string& wallpaperName);
	
	bool getWallpaperSpecFromName(const std::string& wallpaperName,std::string& wallpaperFile,std::string& wallpaperThumbFile);
	bool getWallpaperSpecFromFilename(std::string& wallpaperName,std::string& wallpaperFile,std::string& wallpaperThumbFile);
	
private:
    QImage clipImageToScreenSizeWithFocus(QImage& image, int focus_x,int focus_y);
    QImage clipImageToScreenSize(QImage& image, bool center);
    int resizeImage(const std::string& sourceFile, const std::string& destFile, int destImgW, int destImgH, const char* format);
	void getScreenDimensions();
	
	std::list<std::string> m_wallpapers;
	std::string m_currentWallpaperName;
	static std::string s_wallpaperDir;
	static std::string s_wallpaperThumbsDir;
	
};

#endif
