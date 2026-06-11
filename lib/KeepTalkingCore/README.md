# KeepTalkingCore

Shared Arduino/PlatformIO library for the Keep Talking project.

This library is intended to reduce duplicated code between modules while keeping each puzzle module responsible for its own hardware and rules.

## Current migration stage

This branch adds the reusable library without rewriting the existing `.ino` modules yet.

The previous conservative PlatformIO proposal remains preserved in:

```text
feature/platformio-standard
```

This branch builds on top of it:

```text
refactor/keep-talking-core-library
```

## What belongs in this library

- Shared I2C protocol constants
- Shared module state constants
- Shared RGB status LED behavior
- A base module state machine for standard slave modules

## What should stay inside each module

- Puzzle rules
- Sensor reading
- Display rendering
- Servo or NeoPixel behavior
- Module-specific pin mapping
- Special behavior such as Countdown timer or Edgework master logic

## Intended module shape

A standard slave module can eventually become:

```cpp
#include <KeepTalkingModule.h>

class MyModule : public KeepTalking::Module {
public:
    MyModule()
        : KeepTalking::Module(I2C_ADDRESS, RESET_PIN, RED_LED_PIN, GREEN_LED_PIN, BLUE_LED_PIN) {}

protected:
    void moduleSetup() override {
        // Module-specific pin setup
    }

    void readSensors() override {
        // Module-specific sensor reading
    }

    void precheck() override {
        // Prepare puzzle and call setReady(true) when ready
    }

    void solvePuzzle() override {
        // Compute expected solution
    }

    void gameLoop() override {
        // Run the puzzle and call setMistake(true) or setSolved(true)
    }
};

MyModule module;

void setup() {
    module.begin();
}

void loop() {
    module.update();
}
```

## Suggested refactor order

1. Use `KeepTalkingProtocol.h` to replace duplicated protocol enums.
2. Use `KeepTalkingStatusLed` to replace duplicated RGB status LED code.
3. Convert one simple module to `KeepTalking::Module`.
4. Repeat module by module only after hardware behavior is confirmed.

Recommended first candidates:

- `KT_Module_SimonSay`
- `KT_Module_Button`

Recommended later candidates:

- `KT_Module_Keyboard`
- `KT_Module_Maze`
- `KT_Module_Password`
- `KT_Module_Fils`

Special cases:

- `KT_Module_Countdown` has timer/buzzer/loss behavior and may need a specialized class.
- `KT_Module_Edgework` is the master module and should probably use a separate future `KeepTalkingMaster` helper instead of the slave `KeepTalking::Module` base class.
