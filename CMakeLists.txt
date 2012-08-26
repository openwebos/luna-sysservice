# @@@LICENSE
#
# Copyright (c) 2012 Hewlett-Packard Development Company, L.P.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
# http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#
# LICENSE@@@

cmake_minimum_required(VERSION 2.8.7)

project(luna-sysservice CXX)

include(webOS/webOS)
webos_modules_init(1 0 0 QUALIFIER RC2)
webos_component(2 0 0)

include(FindPkgConfig)

# -- check for glib 2.0
pkg_check_modules(GLIB2 REQUIRED glib-2.0)
webos_add_compiler_flags(ALL ${GLIB2_CFLAGS})

# -- check for gthread 2.0
pkg_check_modules(GTHREAD2 REQUIRED gthread-2.0)
webos_add_compiler_flags(ALL ${GTHREAD2_CFLAGS})

# -- check for libxml 2.0
pkg_check_modules(GXML2 REQUIRED libxml-2.0)
webos_add_compiler_flags(ALL ${GXML2_CFLAGS})

# -- check for sqlite 3.0
pkg_check_modules(SQLITE3 REQUIRED sqlite3)
webos_add_compiler_flags(ALL ${SQLITE3_CFLAGS})

# -- check for cjson
pkg_check_modules(CJSON REQUIRED cjson)
include_directories(CJSON_INCLUDE_DIRS)
webos_add_compiler_flags(ALL ${CJSON_CFLAGS})

# -- check for pbnjson
pkg_check_modules(PBNJSON REQUIRED pbnjson)
include_directories(PBNJSON_INCLUDE_DIRS)
webos_add_compiler_flags(ALL ${PBNJSON_CFLAGS})

# -- check for LS2 (internally depends on pmloglib)
pkg_check_modules(LS2 REQUIRED luna-service2)
include_directories(LS2_INCLUDE_DIRS)
webos_add_compiler_flags(ALL ${LS2_CFLAGS})

# -- check for QtCore 
# (e.g. /usr/local/Trolltech/QtLighthouse-4.8.0/lib/pkgconfig/QtCore.pc)
pkg_check_modules(QTCORE REQUIRED QtCore)
include_directories(QTCORE_INCLUDE_DIRS)
webos_add_compiler_flags(ALL ${QTCORE_CFLAGS})

# -- check for QtGui
# (e.g. /usr/local/Trolltech/QtLighthouse-4.8.0/lib/pkgconfig/QtGui.pc)
pkg_check_modules(QTGUI REQUIRED QtGui)
include_directories(QTGUI_INCLUDE_DIRS)
webos_add_compiler_flags(ALL ${QTGUI_CFLAGS})

# -- check for uriparser
pkg_check_modules(URIPARSER REQUIRED uriparser)
include_directories(URIPARSER_INCLUDE_DIRS)
webos_add_compiler_flags(ALL ${URIPARSER_CFLAGS})

webos_build_pkgconfig()
webos_config_build_doxygen(doc Doxyfile)

# -- no way to disable warn_unused_result right now.
webos_add_compiler_flags(ALL -g -O3 -Wall -Wno-unused-but-set-variable -Wno-unused-variable -fno-exceptions -fno-rtti)
webos_add_linker_options(ALL --no-undefined)

# -- add local include paths
include_directories(include/public)

#file(GLOB SOURCE_FILES Src/*.cpp)
file(GLOB SOURCE_FILES Src/Logging.cpp
	Src/LocalePrefsHandler.cpp 
	Src/Main.cpp 
	Src/PrefsDb.cpp 
	Src/PrefsFactory.cpp 
	Src/TimePrefsHandler.cpp 
	Src/UrlRep.cpp 
	Src/WallpaperPrefsHandler.cpp 
	Src/BuildInfoHandler.cpp 
	Src/Utils.cpp 
	Src/SystemRestore.cpp 
	Src/RingtonePrefsHandler.cpp 
	Src/ImageServices.cpp 
	Src/Mainloop.cpp 
	Src/TimeZoneService.cpp 
	Src/TzParser.cpp 
	Src/BackupManager.cpp 
	Src/Settings.cpp 
	Src/NetworkConnectionListener.cpp 
        Src/JSONUtils.cpp 
        Src/ImageHelpers.cpp
        )
add_executable(luna-sysservice ${SOURCE_FILES})
target_link_libraries(luna-sysservice 
                      ${GLIB2_LDFLAGS} 
                      ${GTHREAD2_LDFLAGS}
                      ${GXML2_LDFLAGS}
                      ${SQLITE3_LDFLAGS}
                      ${CJSON_LDFLAGS}
                      ${MJSON_LDFLAGS}
                      -L/usr/local/lib;-lpbnjson_c
                      -L/usr/local/lib;-lpbnjson_cpp
                      ${LS2_LDFLAGS}
                      ${QTCORE_LDFLAGS}
                      ${QTGUI_LDFLAGS}
                      ${URIPARSER_LDFLAGS}
                      )


webos_build_system_bus_files()
webos_build_daemon()

install(FILES files/conf/sysservice-backupkeys.json DESTINATION ${WEBOS_INSTALL_WEBOS_SYSCONFDIR})
install(FILES files/conf/sysservice.conf DESTINATION ${WEBOS_INSTALL_WEBOS_SYSCONFDIR})
install(FILES files/sysbus/com.palm.systemservice.backupRegistration.json DESTINATION ${WEBOS_INSTALL_WEBOS_SYSCONFDIR}/backup)

