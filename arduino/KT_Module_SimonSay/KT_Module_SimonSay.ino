/* KT_SimonSay.ino — Simon Says module (4 buttons, 4 LEDs)
 * Based on KeepTalkingCore shared module structure
 * Simon Says puzzle rules implemented here
 */

#include <KeepTalkingModule.h>

#define I2C_ADDR_SIMONSAY 7

// ===== Module-specific variables
// Color index mapping: 0=RED, 1=BLUE, 2=GREEN, 3=YELLOW
const uint8_t BtnPins[4] = {7, 9, 6, 8};   // RED, BLUE, GREEN, YELLOW
const uint8_t LedPins[4] = {A3, A0, A2, A1};

const int ResetBt_pin  = 2;
const int RedLED_pin   = 3;
const int GreenLED_pin = 4;
const int BlueLED_pin  = 5;

class SimonSayModule : public KeepTalking::Module {
public:
  SimonSayModule()
    : KeepTalking::Module(I2C_ADDR_SIMONSAY,
                          ResetBt_pin,
                          RedLED_pin,
                          GreenLED_pin,
                          BlueLED_pin,
                          true,   // Reset button uses INPUT_PULLUP, LOW = pressed
                          true) { // Status RGB LED is active LOW
  }

protected:
  void moduleSetup() override {
    // Module-specific setup
    for (uint8_t i=0;i<4;i++){
      pinMode(BtnPins[i], INPUT_PULLUP); // pull-ups enabled; LOW = pressed
      pinMode(LedPins[i], OUTPUT);
      digitalWrite(LedPins[i], LOW);
    }

    Serial.begin(9600);

    // RNG seed for sequence
    randomSeed(analogRead(A5) ^ micros());
  }

  void onProtocolValue(uint8_t key, const uint8_t* data, uint8_t len) override {
    switch (key) {
      case KeepTalking::K_SN_VOWEL:
        if (len>=1) { SN_Vowel = (data[0] != 0); cfgMask |= BIT_VOWEL; }
        break;

      case KeepTalking::K_MISTAKE_COUNT:
        if (len>=1) MistakeCount = data[0];
        break;

      default:
        KeepTalking::Module::onProtocolValue(key, data, len);
        break;
    }
  }

  bool hasRequiredConfig() const override {
    return (cfgMask & BIT_VOWEL);
  }

  void precheck() override {
    readSensors();
  }

  void solvePuzzle() override {
    // Initialize sequence once
    if (seqLen == 0) {
      targetStages = 3 + (uint8_t)random(0,3); // 3–5 stages
      seq[0] = (uint8_t)random(0,4);
      seqLen = 1;
      expectIdx = 0;
      playIdx = 0;
      playing = true;
      ledOn = false;
      inGap = false;
      inputHold = false;
      tNext = millis();
    }

    setReady(seqLen >= 1 && targetStages >= 3 && targetStages <= 5);
  }

  void gameLoop() override {
    unsigned long now = millis();
    readSensors();

    // --- Sequence playback engine (coexists with input) ---
    if (!inputHold && (playing || inGap)) {
      if (now >= tNext) {
        if (inGap) {
          // restart sequence if no input yet
          inGap = false;
          if (expectIdx == 0) { playing = true; playIdx = 0; ledOn = false; }
        } else if (ledOn) {
          // turn off current LED
          for (uint8_t c=0;c<4;c++) digitalWrite(LedPins[c], LOW);
          ledOn = false;
          tNext = now + tOff;

          if (playIdx + 1 >= seqLen) {
            playing = false;
            inGap = (expectIdx == 0);
            tNext = now + tGap;
          } else {
            playIdx++;
          }
        } else {
          // turn on LED for current flash
          uint8_t c = seq[playIdx];
          digitalWrite(LedPins[c], HIGH);
          ledOn = true;
          tNext = now + tOn;
        }
      }
    }

    // --- Input handling (allowed during playback) ---
    int8_t pressedColor = -1;
    for (uint8_t c=0;c<4;c++) {
      // detect PRESS edge: released(0) -> pressed(1)
      if (btnPrevPressed[c] == 0 && btnStablePressed[c] == 1) {
        pressedColor = c;
      }
    }

    if (pressedColor >= 0) {
      uint8_t strikesIdx = (MistakeCount >= 2) ? 2 : MistakeCount;

      static const uint8_t mapVowel[3][4]   = { {1,0,3,2}, {3,2,1,0}, {2,0,3,1} };
      static const uint8_t mapNoVowel[3][4] = { {1,3,2,0}, {0,1,3,2}, {3,2,1,0} };

      uint8_t flashColor = seq[expectIdx];
      uint8_t required   = SN_Vowel ? mapVowel[strikesIdx][flashColor]
                                           : mapNoVowel[strikesIdx][flashColor];

      // User press feedback: pause playback during feedback
      for (uint8_t c=0;c<4;c++) digitalWrite(LedPins[c], LOW);
      digitalWrite(LedPins[pressedColor], HIGH);
      inputHold = true;
      tNext = now + tOn;

      if ((uint8_t)pressedColor == required) {
        expectIdx++;
        if (expectIdx >= seqLen) {
          if (seqLen >= targetStages) {
            setSolved(true);
            playing = false; inGap = false;
          } else {
            // extend sequence and replay from start
            seq[seqLen++] = (uint8_t)random(0,4);
            expectIdx = 0;
            playIdx = 0; playing = true; ledOn = false; inGap = false;
          }
        }
      } else {
        // strike condition: replay same sequence from start
        setMistake(true);
        expectIdx = 0;
        playIdx = 0; playing = true; ledOn = false; inGap = false;
      }
    }

    // Release inputHold or auto turn off LEDs after feedback
    if (inputHold && now >= tNext) {
      for (uint8_t c=0;c<4;c++) digitalWrite(LedPins[c], LOW);
      inputHold = false;
      // resume playback immediately (if it was active)
      if (playing) { ledOn = false; tNext = now; }
    } else if (!inputHold && ledOn && now >= tNext) {
      for (uint8_t c=0;c<4;c++) digitalWrite(LedPins[c], LOW);
      ledOn = false;
    }

    // save current debounced states for edge detection next tick
    for (uint8_t c=0;c<4;c++) btnPrevPressed[c] = btnStablePressed[c];
  }

private:
  uint8_t seq[16];          // flash sequence buffer
  uint8_t seqLen = 0;
  uint8_t targetStages = 3; // 3–5 stages
  uint8_t expectIdx = 0;

  // Playback timing constants (similar to Simon Says)
  const unsigned long tOn  = 500;   // LED ON duration
  const unsigned long tOff = 250;   // LED OFF between flashes
  const unsigned long tGap = 800;   // delay before repeating sequence

  // Playback state
  bool     playing = false;
  bool     ledOn   = false;
  bool     inGap   = false;
  bool     inputHold = false;        // pause playback during user feedback
  uint8_t  playIdx = 0;
  unsigned long tNext = 0;

  // Debounce state (pressed = 1, released = 0, with INPUT_PULLUP)
  uint8_t  btnRawPressed[4]    = {0,0,0,0};
  uint8_t  btnStablePressed[4] = {0,0,0,0};
  uint8_t  btnPrevPressed[4]   = {0,0,0,0};
  unsigned long btnT[4]        = {0,0,0,0};
  const unsigned long debounceMs = 25;

  volatile uint8_t MistakeCount = 0;   // strikes from master
  bool SN_Vowel = false;               // serial vowel from master

  volatile uint8_t cfgMask = 0;
  const uint8_t BIT_VOWEL = 0x01;

  void readSensors() {
    // read buttons with debounce (INPUT_PULLUP: LOW means pressed)
    unsigned long now = millis();
    for (uint8_t c=0;c<4;c++) {
      uint8_t rawPressed = (digitalRead(BtnPins[c]) == LOW) ? 1 : 0; // map to 1 when pressed
      if (rawPressed != btnRawPressed[c]) {
        btnRawPressed[c] = rawPressed;
        btnT[c] = now;
      } else {
        if ((now - btnT[c]) >= debounceMs) {
          btnStablePressed[c] = rawPressed;
        }
      }
    }
  }
};

SimonSayModule module;

void setup() {
  module.begin();
}

void loop() {
  module.update();
}
