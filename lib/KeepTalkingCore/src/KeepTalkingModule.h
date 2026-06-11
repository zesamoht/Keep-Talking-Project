#ifndef KEEP_TALKING_MODULE_H
#define KEEP_TALKING_MODULE_H

#include <Arduino.h>
#include <Wire.h>
#include "KeepTalkingProtocol.h"
#include "KeepTalkingStatusLed.h"

namespace KeepTalking {

class Module {
public:
    Module(uint8_t i2cAddress,
           uint8_t resetPin,
           uint8_t redLedPin,
           uint8_t greenLedPin,
           uint8_t blueLedPin,
           bool resetActiveLow = true,
           bool statusLedActiveLow = true);

    void begin();
    void update();

    uint8_t state() const;
    bool isReady() const;
    bool hasMistake() const;
    bool isSolved() const;

protected:
    virtual void moduleSetup() {}
    virtual void readSensors() {}
    virtual void precheck() {}
    virtual void solvePuzzle() {}
    virtual void gameLoop() {}

    virtual void onProtocolValue(uint8_t key, const uint8_t* data, uint8_t len);
    virtual bool hasRequiredConfig() const;
    virtual void onGameStart() {}
    virtual void onMistakeAck() {}
    virtual void onGameLost() {}
    virtual void onGameWon() {}

    void setReady(bool ready);
    void setMistake(bool mistake);
    void setSolved(bool solved);
    void setState(uint8_t state);

    bool startSignal() const;
    bool mistakeAck() const;
    bool gameLost() const;
    bool gameWon() const;

    StatusLed statusLed;

private:
    static Module* _activeInstance;

    uint8_t _i2cAddress;
    uint8_t _resetPin;
    bool _resetActiveLow;

    uint8_t _state;
    bool _deviceReady;
    bool _mistakeMade;
    bool _moduleSolved;

    volatile bool _startSignal;
    volatile bool _mistakeAck;
    volatile bool _gameLost;
    volatile bool _gameWon;

    void stateAwaitingSetup();
    void statePrecheck();
    void stateWaitStart();
    void stateRunning();
    void stateMistake();
    void stateSolved();

    bool resetPressed() const;

    void receiveBytes(int howMany);
    void requestByte();

    static void onReceiveThunk(int howMany);
    static void onRequestThunk();
};

} // namespace KeepTalking

#endif // KEEP_TALKING_MODULE_H
