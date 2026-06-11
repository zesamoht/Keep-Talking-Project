#include "KeepTalkingModule.h"

namespace KeepTalking {

Module* Module::_activeInstance = nullptr;

Module::Module(uint8_t i2cAddress,
               uint8_t resetPin,
               uint8_t redLedPin,
               uint8_t greenLedPin,
               uint8_t blueLedPin,
               bool resetActiveLow,
               bool statusLedActiveLow)
    : statusLed(redLedPin, greenLedPin, blueLedPin, statusLedActiveLow),
      _i2cAddress(i2cAddress),
      _resetPin(resetPin),
      _resetActiveLow(resetActiveLow),
      _state(STATE_AWAITING_SETUP),
      _deviceReady(false),
      _mistakeMade(false),
      _moduleSolved(false),
      _startSignal(false),
      _mistakeAck(false),
      _gameLost(false),
      _gameWon(false) {
}

void Module::begin() {
    _activeInstance = this;

    pinMode(_resetPin, _resetActiveLow ? INPUT_PULLUP : INPUT);
    statusLed.begin();

    Wire.begin(_i2cAddress);
    Wire.onReceive(Module::onReceiveThunk);
    Wire.onRequest(Module::onRequestThunk);

    moduleSetup();
}

void Module::update() {
    switch (_state) {
        case STATE_AWAITING_SETUP: stateAwaitingSetup(); break;
        case STATE_PRECHECK:       statePrecheck();       break;
        case STATE_WAIT_START:     stateWaitStart();      break;
        case STATE_RUNNING:        stateRunning();        break;
        case STATE_MISTAKE:        stateMistake();        break;
        case STATE_SOLVED:         stateSolved();         break;
        default:                   setState(STATE_AWAITING_SETUP); break;
    }
}

uint8_t Module::state() const {
    return _state;
}

bool Module::isReady() const {
    return _deviceReady;
}

bool Module::hasMistake() const {
    return _mistakeMade;
}

bool Module::isSolved() const {
    return _moduleSolved;
}

void Module::onProtocolValue(uint8_t key, const uint8_t* data, uint8_t len) {
    switch (key) {
        case K_START_SIGNAL:
            if (len == 0 || data[0] != 0) {
                _startSignal = true;
            }
            break;

        case K_MISTAKE_ACK:
            _mistakeAck = true;
            onMistakeAck();
            break;

        case K_GAME_LOST:
            _gameLost = true;
            onGameLost();
            break;

        case K_GAME_WON:
            _gameWon = true;
            onGameWon();
            break;

        default:
            break;
    }
}

bool Module::hasRequiredConfig() const {
    return true;
}

void Module::setReady(bool ready) {
    _deviceReady = ready;
}

void Module::setMistake(bool mistake) {
    _mistakeMade = mistake;
}

void Module::setSolved(bool solved) {
    _moduleSolved = solved;
}

void Module::setState(uint8_t state) {
    _state = state;
}

bool Module::startSignal() const {
    return _startSignal;
}

bool Module::mistakeAck() const {
    return _mistakeAck;
}

bool Module::gameLost() const {
    return _gameLost;
}

bool Module::gameWon() const {
    return _gameWon;
}

void Module::stateAwaitingSetup() {
    if (resetPressed()) {
        delay(50);
        if (resetPressed()) {
            setState(STATE_PRECHECK);
        }
    }

    statusLed.awaitingSetup();
}

void Module::statePrecheck() {
    _deviceReady = false;

    precheck();
    solvePuzzle();

    if (_deviceReady && hasRequiredConfig()) {
        setState(STATE_WAIT_START);
    }

    statusLed.ready(_deviceReady);
}

void Module::stateWaitStart() {
    if (_startSignal) {
        _startSignal = false;
        onGameStart();
        setState(STATE_RUNNING);
    }

    statusLed.waitingStart();
}

void Module::stateRunning() {
    gameLoop();

    if (_mistakeMade) {
        _mistakeMade = false;
        setState(STATE_MISTAKE);
    }

    if (_moduleSolved) {
        setState(STATE_SOLVED);
    }

    statusLed.running();
}

void Module::stateMistake() {
    if (!_gameLost && _mistakeAck) {
        _mistakeAck = false;
        setState(STATE_RUNNING);
    }

    statusLed.mistake();
}

void Module::stateSolved() {
    statusLed.solved();
}

bool Module::resetPressed() const {
    int level = digitalRead(_resetPin);
    return _resetActiveLow ? (level == LOW) : (level == HIGH);
}

void Module::receiveBytes(int howMany) {
    if (howMany <= 0) {
        return;
    }

    uint8_t key = Wire.read();
    uint8_t len = 0;
    uint8_t data[4] = {0, 0, 0, 0};

    if (Wire.available()) {
        len = Wire.read();
    }

    for (uint8_t i = 0; i < len && i < sizeof(data) && Wire.available(); ++i) {
        data[i] = Wire.read();
    }

    onProtocolValue(key, data, len);
}

void Module::requestByte() {
    Wire.write(_state);
}

void Module::onReceiveThunk(int howMany) {
    if (_activeInstance != nullptr) {
        _activeInstance->receiveBytes(howMany);
    }
}

void Module::onRequestThunk() {
    if (_activeInstance != nullptr) {
        _activeInstance->requestByte();
    }
}

} // namespace KeepTalking
