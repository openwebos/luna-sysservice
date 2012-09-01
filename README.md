Summary
========
Provides image manipulation, preference, timezone and ringtone services for webOS components.

LunaSysService
==============

This service supports the following methods, which are described in detail in the generated documentation:  

*  com.palm.image/convert
*  com.palm.image/exResize
*  com.palm.image/imageInfo

*  com.palm.systemservice/getPreferences
*  com.palm.systemservice/getPreferenceValues
*  com.palm.systemservice/setPreferences

*  com.palm.systemservice/backup/preBackup
*  com.palm.systemservice/backup/postRestore

*  com.palm.systemservice/ringtone/addRingtone
*  com.palm.systemservice/ringtone/deleteRingtone

*  com.palm.systemservice/time/convertDate
*  com.palm.systemservice/time/getNTPTime
*  com.palm.systemservice/time/getSystemTime
*  com.palm.systemservice/time/getSystemTimezoneFile
*  com.palm.systemservice/time/launchTimeChangeApps
*  com.palm.systemservice/time/setSystemNetworkTime
*  com.palm.systemservice/time/setSystemTime
*  com.palm.systemservice/time/setTimeChangeLaunch
*  com.palm.systemservice/time/setTimeWithNTP

*  com.palm.systemservice/timezone/getTimeZoneFromEasData
*  com.palm.systemservice/timezone/getTimeZoneRules

*  com.palm.systemservice/wallpaper/convert
*  com.palm.systemservice/wallpaper/deleteWallpaper
*  com.palm.systemservice/wallpaper/importWallpaper
*  com.palm.systemservice/wallpaper/info
*  com.palm.systemservice/wallpaper/refresh


How to Build on Linux
=====================

### Building the latest "stable" version

Clone the repository openwebos/build-desktop and follow the instructions in the README file.

### Building your local clone

First follow the directions to build the latest "stable" version.

To build your local clone of luna-sysservice instead of the "stable" version installed with the build-webos-desktop script:  
* Open the build-webos-desktop.sh script with a text editor
* Locate the function build_luna-sysservice
* Change the line "cd $BASE/luna-sysservice" to use the folder containing your clone, for example "cd ~/github/luna-sysservice"
* Close the text editor
* Remove the file ~/luna-desktop-binaries/luna-sysservice/luna-desktop-build.stamp
* Start the build

Cautions:
* When you re-clone openwebos/build-desktop, you'll have to overwrite your changes and reapply them
* Components often advance in parallel with each other, so be prepared to keep your cloned repositories updated
* Fetch and rebase frequently

### Generating documentation

The tools required to generate the documentation are:

* doxygen 1.6.3
* graphviz 2.20.2


# Copyright and License Information

All content, including all source code files and documentation files in this repository except otherwise noted are: 

 Copyright (c) 2010-2012 Hewlett-Packard Development Company, L.P.

All content, including all source code files and documentation files in this repository except otherwise noted are:
Licensed under the Apache License, Version 2.0 (the "License");
you may not use this content except in compliance with the License.
You may obtain a copy of the License at

http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
