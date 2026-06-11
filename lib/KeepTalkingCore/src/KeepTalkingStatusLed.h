#ifndef KEEP_TALKING_STATUS_LED_H
#define KEEP_TALKING_STATUS_LED_H

#include <Arduino.h>

namespace KeepTalking {

class StatusLed {
public:
    StatusLed(uint8_t redPin, uint8_t greenPin, uint8_t bluePin, bool activeLow = true);

    void begin();
    void setBlinkInterval(unsigned long intervalMs);

    void awaitingSetup();
    void ready(bool deviceReady);
    void waitingStart();
    void running();
    void mistake();
    void solved();

    void setColor(bool redOn, bool greenOn, bool blueOn);
    void off();

private:
    uint8_t _redPin;
    uint8_t _greenPin;
    uint8_t _bluePin;
    bool _activeLow;
    bool _blinkState;
    unsigned long _previousMillis;
    unsigned long _blinkInterval;

    uint8_t levelFor(bool on) const;
    void blueBlink();
};

} // namespace KeepTalking

#endif // KEEP_TALKING_STATUS_LED_H
