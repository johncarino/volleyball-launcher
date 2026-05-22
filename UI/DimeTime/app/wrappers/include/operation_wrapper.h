#ifndef OPERATION_WRAPPER_H
#define OPERATION_WRAPPER_H

#include <QObject>

class OperationWrapper : public QObject
{
    Q_OBJECT
    Q_PROPERTY(float tiltAngle READ getTiltAngle NOTIFY tiltAngleChanged)
    Q_PROPERTY(float yawAngle READ getYawAngle NOTIFY yawAngleChanged)
    Q_PROPERTY(float speed READ getSpeed NOTIFY speedChanged)
    Q_PROPERTY(int rpm READ getRpm NOTIFY rpmChanged)

public:
    explicit OperationWrapper(QObject *parent = nullptr);

    float getTiltAngle() const;
    float getYawAngle() const;
    float getSpeed() const;
    int getRpm() const;

    Q_INVOKABLE void tiltSignal(float angle);
    Q_INVOKABLE void yawSignal(float angle);
    Q_INVOKABLE void speedSignal(float speed);
    Q_INVOKABLE void operationInit();
    Q_INVOKABLE void operationCleanup();
    Q_INVOKABLE void pause_machine();
    Q_INVOKABLE void resume_machine();

signals:
    void tiltAngleChanged();
    void yawAngleChanged();
    void speedChanged();
    void rpmChanged();
};

#endif // OPERATION_WRAPPER_H
