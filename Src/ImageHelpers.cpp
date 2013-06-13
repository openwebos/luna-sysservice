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

#include "ImageHelpers.h"

#define HALF_DECIMATION_THRESHOLD_H    1500
#define QUARTER_DECIMATION_THRESHOLD_H 3000
#define EIGHTH_DECIMATION_THRESHOLD_H  4500


bool readImageWithPrescale(QImageReader& reader, QImage& image, double& prescaleFactor)
{
    // used to scale the file before it is actually read to memory
    prescaleFactor = 1.0;

    int height = reader.size().height();
    if(height > HALF_DECIMATION_THRESHOLD_H)
        prescaleFactor = 0.5;
    else if(height > QUARTER_DECIMATION_THRESHOLD_H)
        prescaleFactor = 0.25;
    else if(height > EIGHTH_DECIMATION_THRESHOLD_H)
        prescaleFactor = 0.125;

    if(prescaleFactor != 1.0)
        reader.setScaledSize(QSize(reader.size().width() * prescaleFactor, reader.size().height() * prescaleFactor));

    return reader.read(&image);
}
