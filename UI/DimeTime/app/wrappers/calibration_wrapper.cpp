#include "app/wrappers/include/calibration_wrapper.h"
#include "app/src/include/calibration.h"

CalibrationWrapper::CalibrationWrapper(QObject *parent)
    : QObject(parent)
{
}

float CalibrationWrapper::net_height() const
{
    return get_net_height();
}

float CalibrationWrapper::court_length() const
{
    return get_court_length();
}

float CalibrationWrapper::court_width() const
{
    return get_court_width();
}

void CalibrationWrapper::defaultCalibration()
{
    default_calibration();
    emit netHeightChanged();
    emit courtLengthChanged();
    emit courtWidthChanged();
}

void CalibrationWrapper::setNetHeight(float height)
{
    set_net_height(height);
    emit netHeightChanged();
}

void CalibrationWrapper::setCourtLength(float length)
{
    set_court_length(length);
    emit courtLengthChanged();
}

void CalibrationWrapper::setCourtWidth(float width)
{
    set_court_width(width);
    emit courtWidthChanged();
}