#include <Wire.h>
#include <Adafruit_NeoPixel.h>
#include <Servo.h>
#define SlaveAddress 1 // Adresse à adapter par module ...

// Hardware (NeoPixel + Servo + Input)
#define LED_PIN_NEOPIX 7
#define BTN_PIN        8
#define SERVO_PIN      6
#define BTN_LEDS       20
#define STRIP_LEDS     5
#define TOTAL_LEDS     (BTN_LEDS + STRIP_LEDS)

// ==========================
// Module Specific variables
// ==========================

Adafruit_NeoPixel pixels(TOTAL_LEDS, LED_PIN_NEOPIX, NEO_GRB + NEO_KHZ800);
Servo drum;

// Arrays
// BUTTON_COLOR: 0=Blue, 1=Red, 2=White, 3=Yellow, 4=Black(off)
int BUTTON_COLOR[5]   = {0,1,2,3,4};
// STRIP_COLOR:  0=Blue, 1=Red, 2=White, 3=Yellow
int STRIP_COLOR[4]    = {0,1,2,3};
// SERVO_POSITION: 0=Abort, 1=Detonate, 2=Hold, 3=Press
int SERVO_POSITION[4] = {0,90,180,270};

// Hardcoded comm variables (debug for now)
uint8_t  g_batteryCount     = 2;
bool     g_lit_CAR          = false;
bool     g_lit_FRK          = false;
uint16_t g_countdownStartSec= 5*60; // 05:00

// Local countdown
bool     countdownStarted = false;
uint32_t gameStartMillis  = 0;

// Button state
bool buttonPressed   = false;
uint32_t pressStartMs= 0;
const uint16_t TAP_MAX_HOLD_MS = 1000; // Max press time for a TAP

// Current randomized face
int buttonColor = 0; // from BUTTON_COLOR[]
int stripColor  = 0; // from STRIP_COLOR[]
int labelIndex  = 0; // 0=Abort,1=Detonate,2=Hold,3=Press
bool MustHold   = true; // Rule result

// ==========================
// Cross-module standard variables
// ==========================
volatile bool dataReceived = false;  // Flag to indicate new data arrival
volatile int receivedValue = 0;

// Initializing Std Pin
const int ResetBt_pin = 2;
const int RedLED_pin = 5;
const int GreenLED_pin = 4;
const int BlueLED_pin = 3;

// LED Variables
unsigned long previousMillis = 0;
const long blinkInterval = 500;
bool ledState = LOW;

// Communication Variables
int M_State = 0;
  //M_State_0
  volatile int Serial_Number = 0;
  volatile int Indicateur_ID = 0;
  volatile int Battery_Type = 0;
  volatile int Port_Type = 0;
  bool Mod_Info_Ok = true; // Debug on true / Normal on false
  //M_State_2
  volatile bool Start_Signal = false;
  //M_State_3
  volatile bool Game_Lost = false;
  //M_State_4
  volatile bool Mistake_Ack = false;

// Function interaction Variables
bool Device_Ready = false;
bool Mistake_Made = false;
bool Module_Solved = false;


// ==========================
// Setup & Loop
// ==========================
void setup() {
  // --------------------------
  // Cross-module standard setup
  // --------------------------
  pinMode(ResetBt_pin, INPUT);
  pinMode(RedLED_pin, OUTPUT);
  pinMode(GreenLED_pin, OUTPUT);
  pinMode(BlueLED_pin, OUTPUT);

  Wire.begin(SlaveAddress); // join i2c bus with address var
  Wire.onReceive(receiveEvent);
  Wire.onRequest(requestEvent);

  Serial.begin(9600);

  // --------------------------
  // Module specific setup
  // --------------------------
  pinMode(BTN_PIN, INPUT_PULLUP);
  pixels.begin();
  pixels.clear();
  pixels.setBrightness(64);
  pixels.show();
  drum.attach(SERVO_PIN);
  randomSeed(analogRead(A0));
}

void loop() { 
  Serial.println(M_State); // Debug

  // Process received I2C data
  if (dataReceived) {
    switch (M_State) {
    case 0: Var_Mod_Info(receivedValue); break; // Var. Module. Info.
    case 1: ; break;
    case 2: Start_Signal = true; break;
    case 3: Game_Lost = true; break;
    case 4: Mistake_Ack = true; break;
    case 5: ; break;
    }
    receivedValue = 0;
    dataReceived = false;
  }

  // Game State


  // State machine dispatch
  switch (M_State) {
    case 0: M_State_0(); break;
    case 1: M_State_1(); break;
    case 2: M_State_2(); break;
    case 3: M_State_3(); break;
    case 4: M_State_4(); break;
    case 5: M_State_5(); break;
  }
}

// ==========================
// I2C Communication Handlers
// ==========================
void receiveEvent(int numBytes) {
  while (Wire.available()) {
    receivedValue = Wire.read();
    dataReceived  = true;
  }
}
void requestEvent() { Wire.write(M_State); }

// ==========================
// State Handling (STANDARD)
// ==========================

// State 0: Awaiting setup
void M_State_0() {
  if (digitalRead(ResetBt_pin) == HIGH) {
    delay(50);
    if (digitalRead(ResetBt_pin) == HIGH && Mod_Info_Ok) { M_State = 1; }
  }
  digitalWrite(RedLED_pin, LOW);
  digitalWrite(GreenLED_pin, LOW);
  digitalWrite(BlueLED_pin, HIGH);
}

// State 1: Precheck / readiness
void M_State_1() {
  Precheck();
  SolvePuzzle();
  if (Device_Ready) M_State = 2;
  else              M_State = 0;
  digitalWrite(RedLED_pin, !Device_Ready);
  digitalWrite(GreenLED_pin, Device_Ready);
  digitalWrite(BlueLED_pin, LOW);
  delay(blinkInterval);
}

// State 2: Awaiting START signal
void M_State_2() {
  if (Start_Signal) { M_State = 3; }
  unsigned long currentMillis = millis();
  if (currentMillis - previousMillis >= blinkInterval) {
    previousMillis = currentMillis;
    ledState = !ledState;
    digitalWrite(RedLED_pin, LOW);
    digitalWrite(GreenLED_pin, LOW);
    digitalWrite(BlueLED_pin, ledState);
  }
}

// State 3: Game in progress
void M_State_3() {
  GameLoop();
  if (Mistake_Made) { M_State = 4; Mistake_Made = false; }
  if (Module_Solved){ M_State = 5; }
  digitalWrite(RedLED_pin, LOW);
  digitalWrite(GreenLED_pin, LOW);
  digitalWrite(BlueLED_pin, LOW);
}

// State 4: Mistake cycle
void M_State_4() {
  if (!Game_Lost && Mistake_Ack) { M_State = 3; Mistake_Ack = false; }
  digitalWrite(RedLED_pin, HIGH);
  digitalWrite(GreenLED_pin, LOW);
  digitalWrite(BlueLED_pin, LOW);
}

// State 5: Module solved
void M_State_5() {
  digitalWrite(RedLED_pin, LOW);
  digitalWrite(GreenLED_pin, HIGH);
  digitalWrite(BlueLED_pin, LOW);
}

// ======================================
// MODULE IMPLEMENTATION AREA
// ======================================

void readSensors() {
  buttonPressed = (digitalRead(BTN_PIN) == LOW); // pressed when LOW
}

// ---------- Precheck ----------
void Precheck() {
  // Randomize puzzle configuration
  buttonColor = BUTTON_COLOR[random(5)];
  stripColor  = STRIP_COLOR[random(4)];
  labelIndex  = random(4);

  // Move servo and set button color
  drum.write(SERVO_POSITION[labelIndex]);
  RenderFace(buttonColor);
  
  Device_Ready = true;
}

void SolvePuzzle() {
  // Apply rules to compute MustHold
  if (buttonColor == 0 && labelIndex == 0) MustHold = true;          // Blue + Abort
  else if (g_batteryCount > 1 && labelIndex == 1) MustHold = false;  // Detonate + >1 battery
  else if (buttonColor == 2 && g_lit_CAR) MustHold = true;           // White + CAR lit
  else if (g_batteryCount > 2 && g_lit_FRK) MustHold = false;        // FRK lit + >2 batteries
  else if (buttonColor == 3) MustHold = true;                        // Yellow
  else if (buttonColor == 1 && labelIndex == 2) MustHold = false;    // Red + Hold
  else MustHold = true;                                              // Default → HOLD

}

// ---------- Game loop ----------
void GameLoop() {
  readSensors();

  // Start countdown only once when Start_Signal arrives
  if (Start_Signal && !countdownStarted) {
    gameStartMillis = millis();
    countdownStarted = true;
  }

  static int play = 0;       // 0 idle, 1 TAP flow, 2 HOLD flow
  static bool lastPressed = false;

  bool pressedEdge  = (!lastPressed && buttonPressed);
  bool releasedEdge = ( lastPressed && !buttonPressed);

  switch (play) {
    case 0: { // Idle
      if (!countdownStarted) break; // wait until timer is running
      if (pressedEdge) {
        pressStartMs = millis();
        // Light up strip while held
        uint32_t sc = 0;
        switch (stripColor) {
          case 0: sc = pixels.Color(0, 0, 255); break;      // Blue
          case 1: sc = pixels.Color(255, 0, 0); break;      // Red
          case 2: sc = pixels.Color(255, 255, 255); break;  // White
          case 3: sc = pixels.Color(255, 180, 0); break;    // Yellow
        }
        for (int i = BTN_LEDS; i < TOTAL_LEDS; ++i) pixels.setPixelColor(i, sc);
        pixels.show();

        play = MustHold ? 2 : 1;
      }
    } break;

    case 1: { // TAP
      if (releasedEdge) {
        uint32_t heldMs = millis() - pressStartMs;
        if (heldMs <= TAP_MAX_HOLD_MS) Module_Solved = true;
        else                           Mistake_Made  = true;
        RenderFace(buttonColor);
        play = 0;
      }
    } break;

    case 2: { // HOLD
      if (releasedEdge) {
        // Required digit depends on strip color
        uint8_t need = 1;
        switch (stripColor) {
          case 0: need = 4; break; // Blue strip
          case 2: need = 1; break; // White strip
          case 3: need = 5; break; // Yellow strip
          case 1: default: need = 1; break; // Red strip
        }

        // Check if current timer shows required digit
        uint16_t sec = secondsRemaining();
        uint8_t m = sec / 60, s = sec % 60;
        uint8_t mT = m / 10, mU = m % 10, sT = s / 10, sU = s % 10;
        bool hasDigit = (need == mT) || (need == mU) || (need == sT) || (need == sU);

        bool success = MustHold && hasDigit;
        if (success) Module_Solved = true;
        else         Mistake_Made  = true;

        RenderFace(buttonColor);
        play = 0;
      }
    } break;
  }

  lastPressed = buttonPressed;
}

// ---------- Module info ----------
void Var_Mod_Info(int ID) { (void)ID; Mod_Info_Ok = true; }

// ---------- Countdown helpers ----------
uint16_t secondsRemaining() {
  if (!countdownStarted) return g_countdownStartSec;
  uint32_t elapsed = (millis() - gameStartMillis) / 1000UL;
  if (elapsed >= g_countdownStartSec) return 0;
  return (uint16_t)(g_countdownStartSec - elapsed);
}

// ---------- Rendering ----------
void RenderFace(int colorCode) {
  // Map button color → RGB
  uint32_t col;
  switch (colorCode) {
    case 0: col = pixels.Color(0, 0, 255); break;        // Blue
    case 1: col = pixels.Color(255, 0, 0); break;        // Red
    case 2: col = pixels.Color(255, 255, 255); break;    // White
    case 3: col = pixels.Color(255, 180, 0); break;      // Yellow
    case 4: default: col = pixels.Color(0, 0, 0); break; // Black/off
  }
  // Fill button area
  for (int i = 0; i < BTN_LEDS; ++i) pixels.setPixelColor(i, col);
  // Clear strip
  for (int i = BTN_LEDS; i < TOTAL_LEDS; ++i) pixels.setPixelColor(i, 0);
  pixels.show();
}
