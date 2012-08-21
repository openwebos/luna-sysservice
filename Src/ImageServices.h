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


#ifndef IMAGESERVICES_H
#define IMAGESERVICES_H

#include <lunaservice.h>
#include "MainLoopProvider.h"
class ImageServices
{

public:
	static ImageServices * 	instance(MainLoopProvider * p = NULL);
	bool 					isValid() { return m_valid;}
	
	static bool lsConvertImage(LSHandle* lsHandle, LSMessage* message,void* user_data);
	static bool lsImageInfo(LSHandle* lsHandle, LSMessage* message,void* user_data);
	static bool lsEzResize(LSHandle* lsHandle, LSMessage* message,void* user_data);
    bool ezResize(const std::string& pathToSourceFile,
                  const std::string& pathToDestFile, const char* destType,
                  uint32_t widthFinal,uint32_t heightFinal,
                  std::string& r_errorText);
private:
    bool convertImage(const std::string& pathToSourceFile,
                      const std::string& pathToDestFile, const char* destType,
                      std::string& r_errorText);
    bool convertImage(const std::string& pathToSourceFile,
                      const std::string& pathToDestFile, const char* destType,
                      double focusX, double focusY, double scale,
                      uint32_t widthFinal, uint32_t heightFinal,
                      std::string& r_errorText);
	
	ImageServices();
	ImageServices(const ImageServices& c) {}
	~ImageServices();
	ImageServices& operator=(const ImageServices& c) 
		{ return *this;}
	bool init(MainLoopProvider * p);
	static ImageServices * s_instance;
	
	bool	m_valid;
	GMainLoop * m_p_mainloop;
	LSPalmService* m_service;
	LSHandle* m_serviceHandlePublic;
	LSHandle* m_serviceHandlePrivate;
};

#endif
