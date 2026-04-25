/* KT_SimonSay.ino — Simon Says module (4 buttons, 4 LEDs)
 * Based on KT_Template_Module.ino structure
 * Simon Says puzzle rules implemented
 */

#include <Wire.h>
#define I2C_ADDR_SIMONSAY 7

enum : uint8_t {
  K_BATTERY_COUNT = 0x01,   // (unused)
  K_SN_LAST_ODD   = 0x02,   // (unused)
  K_SN_VOWEL      = 0x03,   // <-- USED
  K_LIT_FRK       = 0x04,   // (unused)
  K_LIT_CAR       = 0x05,   // (unused)
  K_MISTAKE_COUNT = 0x06,   // <-- USED
  K_GAME_DURATION = 0x07,   // (unused)
  K_START_SIGNAL  = 0x08,   // <-- USED
  K_MISTAKE_ACK   = 0x09,   // <-- USED
  K_GAME_LOST     = 0x0A,   // <-- USED
  K_GAME_WON      = 0x0B    // <-- USED
};


// ===== Module-specific variables
// Color index mapping: 0=RED, 1=BLUE, 2=GREEN, 3=YELLOW
const uint8_t BtnPins[4] = {7, 9, 6, 8};   // RED, BLUE, GREEN, YELLOW
const uint8_t LedPins[4] = {A3, A0, A2, A1};

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
uint8_t  btnRawPressed[4]   = {0,0,0,0};
uint8_t  btnStablePressed[4]= {0,0,0,0};
uint8_t  btnPrevPressed[4]  = {0,0,0,0};
unsigned long btnT[4]       = {0,0,0,0};
const unsigned long debounceMs = 25;


const int ResetBt_pin  = 2;
const int RedLED_pin   = 3;
const int GreenLED_pin = 4;
const int BlueLED_pin  = 5;

unsigned long previousMillis = 0;
const long    blinkInterval  = 500;
bool          ledState2      = LOW;

int  M_State = 0;

volatile int  Serial_Number = 0;
volatile int  Indicateur_ID = 0;
volatile int  Battery_Type  = 0;
volatile int  Port_Type     = 0;

volatile bool Start_Signal = false;
volatile bool Mistake_Ack  = false;
volatile bool Game_Lost    = false;
volatile bool Game_Won     = false;

volatile uint8_t MistakeCount = 0;   // strikes from master
bool SN_Vowel = false;               // serial vowel from master

bool Device_Ready  = false;
bool Mistake_Made  = false;
bool Module_Solved = false;

volatile uint8_t cfgMask = 0;
const uint8_t BIT_VOWEL = 0x01;
const uint8_t BIT_ANYKV = 0x80;   // (optional, if you want to accept any KV)
bool Mod_Info_Ok = false;

// ===== Prototypes (Arduino IDE does not require, kept for clarity)
void M_State_0();
void M_State_1();
void M_State_2();
void M_State_3();
void M_State_4();
void M_State_5();
void Var_Mod_Info(int ID);
void Precheck();
void GameLoop();
void readSensors();
void SolvePuzzle();

// ===== setup()
void setup() {
  // Cross-module standard setup
  pinMode(ResetBt_pin, INPUT_PULLUP);
  pinMode(RedLED_pin, OUTPUT);
  pinMode(GreenLED_pin, OUTPUT);
  pinMode(BlueLED_pin, OUTPUT);

  // Module-specific setup
  for (uint8_t i=0;i<4;i++){
    pinMode(BtnPins[i], INPUT_PULLUP); // pull-ups enabled; LOW = pressed
    pinMode(LedPins[i], OUTPUT);
    digitalWrite(LedPins[i], LOW);
  }

Wire.begin(I2C_ADDR_SIMONSAY);
Wire.onReceive(onI2CReceive);
Wire.onRequest(onI2CRequest);

  Serial.begin(9600);

  // RNG seed for sequence
  randomSeed(analogRead(A5) ^ micros());
}

// ===== loop()
void loop() {
  //Serial.println(M_State);//Debug
  // I2C: process received command → flags
  

  // State machine
  switch (M_State) {
    case 0: M_State_0(); break;
    case 1: M_State_1(); break;
    case 2: M_State_2(); break;
    case 3: M_State_3(); break;
    case 4: M_State_4(); break;
    case 5: M_State_5(); break;
  }
}

// ===== I2C callbacks
void onI2CReceive(int howMany) {
  if (howMany < 2) return;
  uint8_t key = Wire.read();
  uint8_t len = Wire.read();
  uint8_t b[4] = {0};
  for (uint8_t i=0; i<len && Wire.available() && i<sizeof(b); i++) b[i] = Wire.read();

  switch (key) {
    case K_SN_VOWEL:
      if (len>=1) { SN_Vowel = (b[0] != 0); cfgMask |= BIT_VOWEL; }
      break;

    case K_MISTAKE_COUNT:
      if (len>=1) MistakeCount = b[0];
      break;

    case K_START_SIGNAL:
      if (len>=1 && b[0]==1) Start_Signal = true;
      break;

    case K_MISTAKE_ACK:
      Mistake_Ack = true;
      break;

    case K_GAME_LOST:
      Game_Lost = true;
      break;

    case K_GAME_WON:
      Game_Won = true;
      break;

    default: break;
  }

  // Decide if we have enough info to arm (require vowel)
  Mod_Info_Ok = (cfgMask & BIT_VOWEL);
}

void onI2CRequest() {
  Wire.write((uint8_t)M_State);   // master polls our state byte
}


// ===== State handling (STANDARD)

// Awaiting Setup
void M_State_0() {
  // Next step
  if (digitalRead(ResetBt_pin) == LOW) {
    delay(50);
    if (digitalRead(ResetBt_pin) == LOW) {
      M_State = 1;
    }
  }
  // LED: Blue steady
  digitalWrite(RedLED_pin, HIGH);
  digitalWrite(GreenLED_pin, HIGH);
  digitalWrite(BlueLED_pin, LOW);
}

// Precheck
void M_State_1() {
  Precheck();
  if (Mod_Info_Ok) M_State = 2;

  // LED: one-shot blink Green if ready else Red
  digitalWrite(RedLED_pin, Device_Ready);
  digitalWrite(GreenLED_pin, !Device_Ready);
  digitalWrite(BlueLED_pin, HIGH);
  delay(blinkInterval);
}

// Wait Start
void M_State_2() {
  if (Start_Signal) {
    Start_Signal = false;
    M_State = 3;
  }

  // LED: Blue blink
  unsigned long now = millis();
  if (now - previousMillis >= blinkInterval) {
    previousMillis = now;
    ledState2 = !ledState2;
    digitalWrite(RedLED_pin, HIGH);
    digitalWrite(GreenLED_pin, HIGH);
    digitalWrite(BlueLED_pin, ledState2);
  }
}

// Game running
void M_State_3() {
  GameLoop();

  if (Mistake_Made) { M_State = 4; Mistake_Made = false; }
  if (Module_Solved) M_State = 5;

  // LED: off
  digitalWrite(RedLED_pin, HIGH);
  digitalWrite(GreenLED_pin, HIGH);
  digitalWrite(BlueLED_pin, HIGH);
}

// Mistake
void M_State_4() {
  if (!Game_Lost && Mistake_Ack) {
    M_State = 3;
    Mistake_Ack = false;
  }
  // LED: Red blink once
  digitalWrite(RedLED_pin, LOW);
  digitalWrite(GreenLED_pin, HIGH);
  digitalWrite(BlueLED_pin, HIGH);
}

// Solved
void M_State_5() {
  // LED: Green steady
  digitalWrite(RedLED_pin, HIGH);
  digitalWrite(GreenLED_pin, LOW);
  digitalWrite(BlueLED_pin, HIGH);
}

// ===== Module info decoding
void Var_Mod_Info(int ID) {
  // Decode to: Serial_Number, Indicateur_ID, Battery_Type, Port_Type
  Mod_Info_Ok = true;
}

// ===== Standard hooks you defined
void Precheck() {
  readSensors();
  SolvePuzzle();
  if (seqLen >= 1 && targetStages >= 3 && targetStages <= 5) Device_Ready = true;
}

void GameLoop() {
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
          Module_Solved = true;
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
      Mistake_Made = true;
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

void SolvePuzzle() {
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
}
