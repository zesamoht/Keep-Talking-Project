#ifndef KEEP_TALKING_PROTOCOL_H
#define KEEP_TALKING_PROTOCOL_H

#include <Arduino.h>

namespace KeepTalking {

// Shared I2C key/value protocol used by the Keep Talking modules.
enum ProtocolKey : uint8_t {
    K_BATTERY_COUNT = 0x01,
    K_SN_LAST_ODD   = 0x02,
    K_SN_VOWEL      = 0x03,
    K_LIT_FRK       = 0x04,
    K_LIT_CAR       = 0x05,
    K_MISTAKE_COUNT = 0x06,
    K_GAME_DURATION = 0x07,
    K_START_SIGNAL  = 0x08,
    K_MISTAKE_ACK   = 0x09,
    K_GAME_LOST     = 0x0A,
    K_GAME_WON      = 0x0B
};

// Standard module states used by most slave modules.
enum ModuleState : uint8_t {
    STATE_AWAITING_SETUP = 0,
    STATE_PRECHECK       = 1,
    STATE_WAIT_START     = 2,
    STATE_RUNNING        = 3,
    STATE_MISTAKE        = 4,
    STATE_SOLVED         = 5
};

} // namespace KeepTalking

#endif // KEEP_TALKING_PROTOCOL_H
