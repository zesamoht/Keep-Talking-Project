#include "KeepTalkingStatusLed.h"

namespace KeepTalking {

StatusLed::StatusLed(uint8_t redPin, uint8_t greenPin, uint8_t bluePin, bool activeLow)
    : _redPin(redPin),
      _greenPin(greenPin),
      _bluePin(bluePin),
      _activeLow(activeLow),
      _blinkState(false),
      _previousMillis(0),
      _blinkInterval(500) {
}

void StatusLed::begin() {
    pinMode(_redPin, OUTPUT);
    pinMode(_greenPin, OUTPUT);
    pinMode(_bluePin, OUTPUT);
    off();
}

void StatusLed::setBlinkInterval(unsigned long intervalMs) {
    _blinkInterval = intervalMs;
}

void StatusLed::awaitingSetup() {
    setColor(false, false, true);
}

void StatusLed::ready(bool deviceReady) {
    setColor(!deviceReady, deviceReady, false);
}

void StatusLed::waitingStart() {
    blueBlink();
}

void StatusLed::running() {
    off();
}

void StatusLed::mistake() {
    setColor(true, false, false);
}

void StatusLed::solved() {
    setColor(false, true, false);
}

void StatusLed::setColor(bool redOn, bool greenOn, bool blueOn) {
    digitalWrite(_redPin, levelFor(redOn));
    digitalWrite(_greenPin, levelFor(greenOn));
    digitalWrite(_bluePin, levelFor(blueOn));
}

void StatusLed::off() {
    setColor(false, false, false);
}

uint8_t StatusLed::levelFor(bool on) const {
    if (_activeLow) {
        return on ? LOW : HIGH;
    }

    return on ? HIGH : LOW;
}

void StatusLed::blueBlink() {
    unsigned long now = millis();
    if (now - _previousMillis >= _blinkInterval) {
        _previousMillis = now;
        _blinkState = !_blinkState;
    }

    setColor(false, false, _blinkState);
}

} // namespace KeepTalking
