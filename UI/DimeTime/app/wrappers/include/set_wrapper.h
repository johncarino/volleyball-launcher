#ifndef SET_WRAPPER_H
#define SET_WRAPPER_H

#include <QObject>

class SetWrapper : public QObject
{
    Q_OBJECT
    Q_PROPERTY(int currentSetIndex READ getCurrentSetIndex WRITE setCurrentSetIndex NOTIFY currentSetIndexChanged)
    Q_PROPERTY(int currentMachinePosition READ getCurrentMachinePosition WRITE setCurrentMachinePosition NOTIFY currentMachinePositionChanged)
    Q_PROPERTY(int setLocation READ getSetLocation NOTIFY setLocationChanged)
    Q_PROPERTY(int setTempo READ getSetTempo NOTIFY setTempoChanged)
    Q_PROPERTY(set_specs_t setSpecs READ getSetSpecs NOTIFY setSpecsChanged)

public:
    explicit SetWrapper(QObject *parent = nullptr);

    int getSetLocation(int setIndex) const;
    int getSetTempo(int setIndex) const;
    int getCurrentSetIndex() const;
    int getCurrentMachinePosition() const;
    
    Q_INVOKABLE void saveSet(int setIndex);
    Q_INVOKABLE void chooseTargetLocation(int location);
    Q_INVOKABLE void chooseTempo(int tempo);
    Q_INVOKABLE void setCurrentSetIndex(int index);
    Q_INVOKABLE void setCurrentMachinePosition(int position);


signals:
    void setLocationChanged();
    void setTempoChanged();
    void setSpecsChanged();

private:
    int s_currentSetIndex = 0;
    int s_currentMachinePosition = 0;
};

#endif // SET_WRAPPER_H