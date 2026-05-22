#include "app/wrappers/include/set_wrapper.h"
#include "app/src/include/set.h"

SetWrapper::SetWrapper(QObject *parent)
    : QObject(parent)
{
}

int SetWrapper::getSetLocation() const
{
    return get_set_location(s_currentSetIndex);
}

int SetWrapper::getSetTempo() consts
{
    return get_set_tempo(s_currentSetIndex);
}

int SetWrapper::getCurrentSetIndex() const
{
    return s_currentSetIndex;
}

int SetWrapper::getCurrentMachinePosition() const
{
    return s_currentMachinePosition;
}

void SetWrapper::saveSet(int setIndex)
{
    save_set(setIndex, 0);
    emit setLocationChanged();
    emit setTempoChanged();
}

void SetWrapper::chooseTargetLocation(int location)
{
    choose_target_location(location);
    emit setLocationChanged();
}

void SetWrapper::chooseTempo(int tempo)
{
    choose_tempo(tempo);
    emit setTempoChanged();
}

void SetWrapper::setCurrentSetIndex(int index)
{
    if (index >= 0 && index < NUM_SETS) {
        s_currentSetIndex = index;
    }
}

void SetWrapper::setCurrentMachinePosition(int position)
{
    if (position >= 0 && position < NUM_MACHINE_POSITIONS) {
        s_currentMachinePosition = position;
    }
    for (int i = 0; i < NUM_SETS; i++) {
        s_currentSetIndex = i%NUM_SETS;
        emit setLocationChanged();
        emit setTempoChanged();
    }
}

