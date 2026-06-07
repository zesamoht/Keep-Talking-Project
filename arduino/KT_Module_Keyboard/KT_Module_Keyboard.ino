#include <Wire.h>
#include <light_CD74HC4067.h>

#define I2C_ADDR_KEYBOARD 3

enum : uint8_t {
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

// -----------------------------------------------------------------------------
// Module hardware
// -----------------------------------------------------------------------------
CD74HC4067 mux(10, 11, 12, 13);

const uint8_t GreenLEDPin[4] = {9, 8, 7, 6};
const uint8_t KEY_LED_ON  = HIGH;
const uint8_t KEY_LED_OFF = LOW;

const uint8_t M0_signal_pin = A0;
const uint8_t M0_enable_pin = A2;
const uint8_t M1_signal_pin = A1;
const uint8_t M1_enable_pin = A3;

int M0Array[16];
int M1Array[16];

const int threshold = 512;
const uint8_t buttonMuxChannels[4] = {5, 6, 7, 8};

uint8_t KeyCodes[4];
uint8_t AnswerPosArray[4];
int8_t AnswerPos = -1;
uint8_t KeyOrder[4] = {0, 1, 2, 3};

int8_t PressedButton = -1;
uint8_t ButtonStep = 0;

// Button debounce / edge detection
int8_t rawButton = -1;
int8_t stableButton = -1;
unsigned long rawButtonChangedAt = 0;
const unsigned long buttonDebounceMs = 30;

const uint8_t puzzleTable[7][6] = {
  {B11011, B01111, B00000, B01010, B10111, B01010},
  {B01100, B11011, B00111, B10100, B00011, B01111},
  {B11101, B10110, B11001, B11110, B11110, B11010},
  {B01011, B11001, B00100, B00110, B10101, B01101},
  {B00110, B00010, B01110, B00100, B10100, B10111},
  {B01000, B01000, B11101, B10011, B10010, B10001},
  {B10110, B10011, B00010, B00011, B00001, B00101}
};

// -----------------------------------------------------------------------------
// Standard module hardware
// -----------------------------------------------------------------------------
const uint8_t ResetBt_pin = 2;
const uint8_t RedLED_pin = 5;
const uint8_t GreenLED_pin = 4;
const uint8_t BlueLED_pin = 3;

// Status LEDs are wired active-LOW: LOW = on, HIGH = off.
void setStatusLED(bool redOn, bool greenOn, bool blueOn) {
  digitalWrite(RedLED_pin, redOn ? LOW : HIGH);
  digitalWrite(GreenLED_pin, greenOn ? LOW : HIGH);
  digitalWrite(BlueLED_pin, blueOn ? LOW : HIGH);
}

unsigned long previousMillis = 0;
const unsigned long blinkInterval = 500;
bool ledState = false;
bool joinButtonWasPressed = false;

// -----------------------------------------------------------------------------
// Communication and game state
// -----------------------------------------------------------------------------
volatile bool Start_Signal = false;
volatile bool Mistake_Ack  = false;
volatile bool Game_Lost    = false;
volatile bool Game_Won     = false;

bool Device_Ready  = false;
bool Mistake_Made  = false;
bool Module_Solved = false;
bool Mod_Info_Ok   = true;
volatile uint8_t M_State = 0;

int8_t lastReportedState = -1;

void setup() {
  // Same join-button convention as the other modules: external pulldown,
  // therefore HIGH means pressed.
  pinMode(ResetBt_pin, INPUT);

  pinMode(RedLED_pin, OUTPUT);
  pinMode(GreenLED_pin, OUTPUT);
  pinMode(BlueLED_pin, OUTPUT);

  pinMode(M0_signal_pin, INPUT);
  pinMode(M0_enable_pin, OUTPUT);
  pinMode(M1_signal_pin, INPUT);
  pinMode(M1_enable_pin, OUTPUT);

  for (uint8_t i = 0; i < 4; i++) {
    pinMode(GreenLEDPin[i], OUTPUT);
    digitalWrite(GreenLEDPin[i], KEY_LED_OFF);
  }

  digitalWrite(M0_enable_pin, HIGH);
  digitalWrite(M1_enable_pin, HIGH);

  Wire.begin(I2C_ADDR_KEYBOARD);
  Wire.onReceive(onI2CReceive);
  Wire.onRequest(onI2CRequest);

  Serial.begin(115200);
  setStatusLED(false, false, true);
}

void loop() {
  if ((int8_t)M_State != lastReportedState) {
    lastReportedState = (int8_t)M_State;
    Serial.print(F("Keyboard state: "));
    Serial.println(lastReportedState);
  }

  switch (M_State) {
    case 0: M_State_0(); break;
    case 1: M_State_1(); break;
    case 2: M_State_2(); break;
    case 3: M_State_3(); break;
    case 4: M_State_4(); break;
    case 5: M_State_5(); break;
    default: M_State = 0; break;
  }
}

// -----------------------------------------------------------------------------
// I2C callbacks
// -----------------------------------------------------------------------------
void onI2CReceive(int howMany) {
  if (howMany < 2) {
    while (Wire.available()) Wire.read();
    return;
  }

  const uint8_t key = Wire.read();
  const uint8_t len = Wire.read();
  uint8_t data[4] = {0, 0, 0, 0};

  uint8_t copied = 0;
  while (Wire.available()) {
    const uint8_t value = Wire.read();
    if (copied < sizeof(data) && copied < len) {
      data[copied++] = value;
    }
  }

  switch (key) {
    case K_START_SIGNAL:
      if (len >= 1 && copied >= 1 && data[0] == 1) Start_Signal = true;
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

    default:
      break;
  }
}

void onI2CRequest() {
  Wire.write((uint8_t)M_State);
}

// -----------------------------------------------------------------------------
// State machine
// -----------------------------------------------------------------------------
void M_State_0() {
  const bool joinPressed = (digitalRead(ResetBt_pin) == HIGH);

  // Detect a new press rather than repeatedly triggering while held.
  if (joinPressed && !joinButtonWasPressed) {
    delay(50);
    if (digitalRead(ResetBt_pin) == HIGH && Mod_Info_Ok) {
      M_State = 1;
    }
  }

  joinButtonWasPressed = joinPressed;
  setStatusLED(false, false, true);
}

void M_State_1() {
  Precheck();

  if (Device_Ready) {
    M_State = 2;
  } else {
    M_State = 0;
  }

  setStatusLED(!Device_Ready, Device_Ready, false);
}

void M_State_2() {
  if (Start_Signal) {
    Start_Signal = false;
    Mistake_Ack = false;
    Game_Lost = false;
    Game_Won = false;
    Mistake_Made = false;
    Module_Solved = false;

    ButtonStep = 0;
    resetButtonScanner();

    for (uint8_t i = 0; i < 4; i++) {
      digitalWrite(GreenLEDPin[i], KEY_LED_OFF);
    }

    M_State = 3;
    return;
  }

  const unsigned long currentMillis = millis();
  if (currentMillis - previousMillis >= blinkInterval) {
    previousMillis = currentMillis;
    ledState = !ledState;
  }

  setStatusLED(false, false, ledState);
}

void M_State_3() {
  GameLoop();

  if (Game_Lost || Game_Won) {
    return;
  }

  if (Mistake_Made) {
    Mistake_Made = false;
    M_State = 4;
  } else if (Module_Solved) {
    M_State = 5;
  }

  setStatusLED(false, false, false);
}

void M_State_4() {
  if (!Game_Lost && Mistake_Ack) {
    Mistake_Ack = false;
    M_State = 3;
  }

  setStatusLED(true, false, false);
}

void M_State_5() {
  setStatusLED(false, true, false);
}

// -----------------------------------------------------------------------------
// Puzzle logic
// -----------------------------------------------------------------------------
void Precheck() {
  Device_Ready = false;

  // Restore the base order before solving a new key arrangement.
  for (uint8_t i = 0; i < 4; i++) {
    KeyOrder[i] = i;
  }

  ReadKey();
  SolvePuzzle();
  Device_Ready = (AnswerPos >= 0);

  Serial.print(F("Precheck: "));
  Serial.println(Device_Ready ? F("ready") : F("invalid key set"));
}

void GameLoop() {
  ReadBt();

  // PressedButton is an event set only once after a stable new press.
  if (PressedButton < 0) return;

  Serial.print(F("Button press: "));
  Serial.println(PressedButton + 1);

  if (KeyOrder[ButtonStep] == (uint8_t)PressedButton) {
    digitalWrite(GreenLEDPin[PressedButton], KEY_LED_ON);

    if (ButtonStep == 3) {
      Module_Solved = true;
    } else {
      ButtonStep++;
    }
  } else {
    Mistake_Made = true;
  }
}

void ReadKey() {
  // Read keys 1-3 from M0.
  digitalWrite(M0_enable_pin, LOW);
  digitalWrite(M1_enable_pin, HIGH);

  for (uint8_t i = 0; i < 16; i++) {
    mux.channel(i);
    analogRead(M0_signal_pin);
    delay(3);
    M0Array[i] = analogRead(M0_signal_pin);
  }

  // Read key 4 and button channels from M1.
  digitalWrite(M0_enable_pin, HIGH);
  digitalWrite(M1_enable_pin, LOW);

  for (uint8_t i = 0; i < 16; i++) {
    mux.channel(i);
    analogRead(M1_signal_pin);
    delay(3);
    M1Array[i] = analogRead(M1_signal_pin);
  }

  digitalWrite(M1_enable_pin, HIGH);

  for (uint8_t key = 0; key < 3; key++) {
    uint8_t keyValue = 0;

    for (uint8_t bit = 0; bit < 5; bit++) {
      const uint8_t index = key * 5 + bit;
      if (M0Array[index] > threshold) {
        keyValue |= (1 << (4 - bit));
      }
    }

    KeyCodes[key] = keyValue;
  }

  uint8_t keyValue = 0;
  for (uint8_t bit = 0; bit < 5; bit++) {
    if (M1Array[bit] > threshold) {
      keyValue |= (1 << (4 - bit));
    }
  }
  KeyCodes[3] = keyValue;

  for (uint8_t i = 0; i < 4; i++) {
    Serial.print(F("Key "));
    Serial.print(i + 1);
    Serial.print(F(": "));
    Serial.println(KeyCodes[i], BIN);
  }
}

void ReadBt() {
  int8_t detectedButton = -1;

  // Sample M1 during gameplay rather than reusing the values captured once
  // during Precheck().
  digitalWrite(M0_enable_pin, HIGH);
  digitalWrite(M1_enable_pin, LOW);

  for (uint8_t i = 0; i < 4; i++) {
    const uint8_t channel = buttonMuxChannels[i];
    mux.channel(channel);
    analogRead(M1_signal_pin);
    delayMicroseconds(250);
    M1Array[channel] = analogRead(M1_signal_pin);

    if (detectedButton < 0 && M1Array[channel] > threshold) {
      detectedButton = i;
    }
  }

  digitalWrite(M1_enable_pin, HIGH);
  PressedButton = -1;

  if (detectedButton != rawButton) {
    rawButton = detectedButton;
    rawButtonChangedAt = millis();
  }

  if ((millis() - rawButtonChangedAt) >= buttonDebounceMs &&
      stableButton != rawButton) {
    stableButton = rawButton;

    if (stableButton >= 0) {
      PressedButton = stableButton;
    }
  }
}

void resetButtonScanner() {
  rawButton = -1;
  stableButton = -1;
  PressedButton = -1;
  rawButtonChangedAt = millis();
}

void SolvePuzzle() {
  AnswerPos = -1;

  for (uint8_t col = 0; col < 6; col++) {
    bool allFound = true;

    for (uint8_t key = 0; key < 4; key++) {
      bool foundInColumn = false;

      for (uint8_t row = 0; row < 7; row++) {
        if (puzzleTable[row][col] == KeyCodes[key]) {
          foundInColumn = true;
          AnswerPosArray[key] = row;
          break;
        }
      }

      if (!foundInColumn) {
        allFound = false;
        break;
      }
    }

    if (allFound) {
      AnswerPos = col;
      break;
    }
  }

  if (AnswerPos < 0) return;

  for (uint8_t i = 0; i < 3; i++) {
    for (uint8_t j = i + 1; j < 4; j++) {
      if (AnswerPosArray[KeyOrder[i]] > AnswerPosArray[KeyOrder[j]]) {
        const uint8_t temp = KeyOrder[i];
        KeyOrder[i] = KeyOrder[j];
        KeyOrder[j] = temp;
      }
    }
  }

  Serial.print(F("Required order: "));
  for (uint8_t i = 0; i < 4; i++) {
    Serial.print(KeyOrder[i] + 1);
    if (i < 3) Serial.print(F(" -> "));
  }
  Serial.println();
}
