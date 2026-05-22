#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>

#include "app/wrappers/include/calibration_wrapper.h"
#include "app/wrappers/include/operation_wrapper.h"


int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);

    CalibrationWrapper calibrationWrapper;
    OperationWrapper operationWrapper;

    QQmlApplicationEngine engine;
    QObject::connect(
        &engine,
        &QQmlApplicationEngine::objectCreationFailed,
        &app,
        []() { QCoreApplication::exit(-1); },
        Qt::QueuedConnection);

    engine.rootContext()->setContextProperty("calibrationWrapper", &calibrationWrapper);
    engine.rootContext()->setContextProperty("operationWrapper", &operationWrapper);
    
    engine.loadFromModule("DimeTime", "Main");

    return QGuiApplication::exec();
}
