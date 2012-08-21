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
