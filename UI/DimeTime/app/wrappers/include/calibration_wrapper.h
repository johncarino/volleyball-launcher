#ifndef CALIBRATION_WRAPPER_H
#define CALIBRATION_WRAPPER_H

#include <QObject>

class CalibrationWrapper : public QObject
{
    Q_OBJECT
    Q_PROPERTY(float net_height READ net_height NOTIFY netHeightChanged)
    Q_PROPERTY(float court_length READ court_length NOTIFY courtLengthChanged)
    Q_PROPERTY(float court_width READ court_width NOTIFY courtWidthChanged)

public:
    explicit CalibrationWrapper(QObject *parent = nullptr);

    float net_height() const;
    float court_length() const;
    float court_width() const;

    Q_INVOKABLE void defaultCalibration();
    Q_INVOKABLE void setNetHeight(float height);
    Q_INVOKABLE void setCourtLength(float length);
    Q_INVOKABLE void setCourtWidth(float width);

signals:
    void netHeightChanged();
    void courtLengthChanged();
    void courtWidthChanged();
};

#endif // CALIBRATION_WRAPPER_H