#include "app/wrappers/include/operation_wrapper.h"
#include "app/src/include/operation.h"

OperationWrapper::OperationWrapper(QObject *parent)
    : QObject(parent)
{
}

float OperationWrapper::getTiltAngle() const
{
    return get_tilt_angle();
}

float OperationWrapper::getYawAngle() const
{
    return get_yaw_angle();
}

float OperationWrapper::getSpeed() const
{
    return get_speed();
}

int OperationWrapper::getRpm() const
{
    return get_rpm();
}

void OperationWrapper::tiltSignal(float angle)
{
    tilt_signal(angle);
    emit tiltAngleChanged();
}

void OperationWrapper::yawSignal(float angle)
{
    yaw_signal(angle);
    emit yawAngleChanged();
}

void OperationWrapper::speedSignal(float speed)
{
    speed_signal(speed);
    emit speedChanged();
    emit rpmChanged();
}

void OperationWrapper::operationInit()
{
    operation_init();
}

void OperationWrapper::operationCleanup()
{
    operation_cleanup();
}

void OperationWrapper::pause_machine()
{
    pause_machine();
}

void OperationWrapper::resume_machine()
{
    resume_machine();
}