#include <Wire.h>
#define SlaveAddress 1 // I2C slave address for this module

// ==========================
// LCD
// ==========================
#include <LiquidCrystal_I2C.h>
// Declare lcd with correct address and size
LiquidCrystal_I2C lcd(0x27, 8, 2); // 8x2 LCD at 0x27

// ==========================
// Module specific variables
// ==========================

// Button buses
const uint8_t PIN_UP     = 12; // shared UP bus, INPUT_PULLUP
const uint8_t PIN_DOWN   = 11; // shared DOWN bus, INPUT_PULLUP
const uint8_t PIN_SUBMIT = 13; // submit, INPUT_PULLUP to GND

// Column select pins (active = LOW)
const uint8_t N_COLS = 5;
const uint8_t COL_PINS[N_COLS] = {6, 7, 8, 9, 10};

// Column content
const uint8_t COL_SIZE = 6;            // exactly 6 letters per slot
char cols[N_COLS][COL_SIZE];           // 5×6 letters
uint8_t idx[N_COLS] = {0,0,0,0,0};     // current index per column [0..5]
char target[5];                         // chosen 5-letter word (fixed length, no '\0')

// Allowed words (exact list from the brief)
const char* WORDS[] = {
  "ABOUT","AFTER","AGAIN","BELOW","COULD",
  "EVERY","FIRST","FOUND","GREAT","HOUSE",
  "LARGE","LEARN","NEVER","OTHER","PLACE",
  "PLANT","POINT","RIGHT","SMALL","SOUND",
  "SPELL","STILL","STUDY","THEIR","THERE",
  "THESE","THING","THINK","THREE","WATER",
  "WHERE","WHICH","WORLD","WOULD","WRITE"
};
const uint8_t N_WORDS = sizeof(WORDS)/sizeof(WORDS[0]);

// Debounce
const uint16_t DEBOUNCE_MS = 25;
struct DebLine { uint8_t pin; bool last; uint32_t t; };
DebLine lineUp   { PIN_UP,     HIGH, 0 };
DebLine lineDown { PIN_DOWN,   HIGH, 0 };


// LCD write cache to minimize I2C traffic
char lastRow0[8] = {' ',' ',' ',' ',' ',' ',' ',' '};


// Actions captured during scan: 0 = none, +1 = UP, -1 = DOWN
int8_t actions[N_COLS] = {0,0,0,0,0};

// ==========================
// Cross-module standard variables
// ==========================

// I2C Communication Variables
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
  pinMode(PIN_UP, INPUT_PULLUP);
  pinMode(PIN_DOWN, INPUT_PULLUP);
  pinMode(PIN_SUBMIT, INPUT_PULLUP);
  for (uint8_t i=0;i<N_COLS;i++) { pinMode(COL_PINS[i], OUTPUT); digitalWrite(COL_PINS[i], HIGH); }

  lcd.init();
  lcd.backlight();
  lcd.clear();

  // Initialize RNG
  randomSeed(analogRead(A0) ^ micros());
}

void loop() { 
  Serial.println(M_State); // Debug

  // Process received I2C data
  if (dataReceived) {
    switch (M_State) {
      case 0: Var_Mod_Info(receivedValue); break; // Var. Module. Info.
      case 1: break;
      case 2: Start_Signal = true; break;
      case 3: Game_Lost = true; break;
      case 4: Mistake_Ack = true; break;
      case 5: break;
    }
    // Reset 
    receivedValue = 0;
    dataReceived = false;
  }

  // Game State
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

// This function is triggered when master sends data
void receiveEvent(int numBytes) {
  while (Wire.available()) {
    receivedValue = Wire.read(); // Read data from master
    dataReceived = true; // Set flag
  }
}
// This function is triggered when master requests data
void requestEvent() {
  Wire.write(M_State);
}

// ==========================
// State Handling (STANDARD)
// ==========================

// Awaiting Setup
void M_State_0() { 
  //Current step
    //NA
  //Next step
  if (digitalRead(ResetBt_pin) == HIGH) {
    delay(50); // Simple debounce
    if (digitalRead(ResetBt_pin) == HIGH && Mod_Info_Ok) { // Confirm button press
      M_State = 1;
    }
  }
  //LED Control : Blue steady
  digitalWrite(RedLED_pin, HIGH);
  digitalWrite(GreenLED_pin, HIGH);
  digitalWrite(BlueLED_pin, LOW);
}

// Checking readyness
void M_State_1() { 
  //Current step
    Precheck();
  //Next step
    if (Device_Ready) {M_State = 2;}
    else {M_State = 0;}
  //LED Control : Blink once Green/Red
  digitalWrite(RedLED_pin, Device_Ready);
  digitalWrite(GreenLED_pin, !Device_Ready);
  digitalWrite(BlueLED_pin, HIGH);
  delay(blinkInterval); // Stay lit once
}

// Awaiting START
void M_State_2() { 
  //Current step
    //Start_Signal received form Master
  //Next step
    if (Start_Signal) { 
      M_State = 3;
    }
  //LED Control : Blue blinking
  unsigned long currentMillis = millis();
  if (currentMillis - previousMillis >= blinkInterval) {
    previousMillis = currentMillis;
    ledState = !ledState;
    digitalWrite(RedLED_pin, HIGH);
    digitalWrite(GreenLED_pin, HIGH);
    digitalWrite(BlueLED_pin, !ledState);
  }
}

// Game In Progress
void M_State_3() {
  //Current step
    GameLoop();
  //Next step
  if (Mistake_Made) {
    M_State = 4;
    Mistake_Made = false;
  }
  if (Module_Solved) {
    M_State = 5;
  }
  //LED Control : off
  digitalWrite(RedLED_pin, HIGH);
  digitalWrite(GreenLED_pin, HIGH);
  digitalWrite(BlueLED_pin, HIGH);
}

// Mistake cycle
void M_State_4() { 
  //Current step
   //Mistake_Ack received form Master
  //Next step
  if (!Game_Lost && Mistake_Ack) {
    M_State = 3;
    Mistake_Ack = false;
  }
  //LED Control : Red steady (one cycle)
  digitalWrite(RedLED_pin, LOW);
  digitalWrite(GreenLED_pin, HIGH);
  digitalWrite(BlueLED_pin, HIGH);
}

// Module Solved
void M_State_5() { 
  //Current step
  //...
  //Next step
  //NA
  //LED Control : Green steady
  digitalWrite(RedLED_pin, HIGH);
  digitalWrite(GreenLED_pin, LOW);
  digitalWrite(BlueLED_pin, HIGH);
}

// ======================================
// MODULE IMPLEMENTATION AREA (CHANGE ME)
// ======================================

// Module info decoding
void Var_Mod_Info(int ID) { // Module info decoding
  Serial.println(ID);
  //TCf Module Info table ...
  //Output :
  //  Serial_Number
  //  Indicateur_ID
  //  Battery_Type
  //  Port_Type
  Mod_Info_Ok = true;
}

// Function to calculate the game scenario and solutions of the game
void Precheck() { // Checking readyness
  //Current step
  //  Run once
  //  Verify wiring state
  //  Define module solution
  //  Device_Ready = true; // Set success condition .. e.g. :

  SolvePuzzle();

  // Initial LCD render: line 0 shows 5 letters, line 1 stays empty
  // Write five letters on line 0 only
  for (uint8_t i=0;i<5;i++) {
    char ch = cols[i][idx[i]];
    lcd.setCursor(i,0);
    lcd.print(ch);
    lastRow0[i] = ch;
  }
  // Clear remaining columns on line 0
  for (uint8_t i=5;i<8;i++) {
    lcd.setCursor(i,0);
    lcd.print(' ');
    lastRow0[i] = ' ';
  }
  // Line 1 remains empty
  lcd.setCursor(0,1);
  for (uint8_t i=0;i<8;i++) lcd.print(' ');

  // Set readiness
  Device_Ready = true;    // define the success condition
}

// Function that runs when the player plays the game
void GameLoop() { // Game In Progress
  //Current step
  //  Steps of the game

  readSensors(); // Inputs read are handled within GameLoop by the help of the function readSensors()

  // Apply stored actions and update visuals
  for (uint8_t c=0; c<N_COLS; c++) {
    if (actions[c] == +1) {
      idx[c] = (idx[c] + 1) % COL_SIZE;
      char ch = cols[c][idx[c]];
      if (lastRow0[c] != ch) { lcd.setCursor(c,0); lcd.print(ch); lastRow0[c] = ch; }
    }
    else if (actions[c] == -1) {
      idx[c] = (idx[c] + COL_SIZE - 1) % COL_SIZE;
      char ch = cols[c][idx[c]];
      if (lastRow0[c] != ch) { lcd.setCursor(c,0); lcd.print(ch); lastRow0[c] = ch; }
    }
  }

  // Simple submit read (no debounce)
  if (digitalRead(PIN_SUBMIT) == LOW) {
    char guess[5];
    for (uint8_t i=0;i<5;i++) guess[i] = cols[i][idx[i]];

    bool ok = true;
    for (uint8_t i=0;i<5;i++) { if (guess[i] != target[i]) { ok = false; break; } }

    if (ok) {
      Module_Solved = true; // freeze: no more writes by policy
    } else {
      Mistake_Made = true;  // display unchanged
    }
  }
}

// Function to call for a reading of the inputs
void readSensors(){
  // Reads inputs for one full scan; fills actions[] using debounced UP/DOWN

  // clear previous actions
  for (uint8_t i=0;i<N_COLS;i++) actions[i] = 0;

  // Scan each column, detect UP/DOWN presses. Action codes: 0 = none, +1 = UP, -1 = DOWN
  for (uint8_t c=0; c<N_COLS; c++) {
    // select column c
    for (uint8_t k=0;k<N_COLS;k++) digitalWrite(COL_PINS[k], HIGH);
    digitalWrite(COL_PINS[c], LOW); // active
    delayMicroseconds(200); // settle time

    bool rawUp = (digitalRead(PIN_UP)   == LOW);
    bool rawDn = (digitalRead(PIN_DOWN) == LOW);

    // debounced detection for UP/DOWN
    // inline justPressed
    auto justPressed = [](DebLine& l, bool rawLow)->bool{
      bool v = rawLow ? LOW : HIGH;
      uint32_t now = millis();
      if (v != l.last) {
        if (now - l.t > DEBOUNCE_MS) {
          l.last = v; l.t = now;
          if (v == LOW) return true;
        }
      }
      return false;
    };

    if (justPressed(lineUp, rawUp))   actions[c] = +1;
    if (justPressed(lineDown, rawDn)) actions[c] = -1;
  }

  // de-select all columns (kept as explicit loop for readability)
  for (uint8_t k=0;k<N_COLS;k++) digitalWrite(COL_PINS[k], HIGH);
}

// Function to calculate the solution parameters based on the existing setup
void SolvePuzzle() {
  // Build target word and letter columns with uniqueness constraint (fixed-length copies only)

  // Pick a target word and copy 5 chars
  uint8_t wi = random(N_WORDS);
  for (uint8_t i=0;i<5;i++) target[i] = WORDS[wi][i];

  // Try to generate unique columns
  for (uint16_t tries=0; tries<3000; tries++) {
    if (generateUniqueColumnsForTarget(target, cols)) {
      for (uint8_t i=0;i<5;i++) idx[i] = random(COL_SIZE);
      return;
    }
    // choose another target
    wi = random(N_WORDS);
    for (uint8_t i=0;i<5;i++) target[i] = WORDS[wi][i];
  }

  // Fallback: force uniqueness with rare letters while including target letters
  for (uint8_t i=0;i<5;i++) {
    const char fillers[6] = {'q','z','x','j','v','k'};
    for (uint8_t k=0;k<6;k++) cols[i][k] = fillers[k];
    cols[i][0] = target[i];
    idx[i] = 0;
  }
}

// ==========================
// Word/column generation helpers
// ==========================
bool wordConstructibleByColumns(const char* w, char ccols[5][6]) {
  for (uint8_t i=0;i<5;i++) {
    bool ok=false;
    for (uint8_t k=0;k<6;k++) if (ccols[i][k]==w[i]) { ok=true; break; }
    if (!ok) return false;
  }
  return true;
}

bool anyNonTargetWordConstructible(const char target[5], char outCols[5][6]) {
  for (uint8_t i=0;i<N_WORDS;i++) {
    const char* w = WORDS[i];
    bool same = true;
    for (uint8_t j=0;j<5;j++) { if (w[j] != target[j]) { same = false; break; } }
    if (same) continue;
    if (wordConstructibleByColumns(w, outCols)) return true;
  }
  return false;
}

bool generateUniqueColumnsForTarget(const char target[5], char outCols[5][6]) {
  static const char alphabet[] = "abcdefghijklmnopqrstuvwxyz";
  for (uint16_t attempt=0; attempt<2000; attempt++) {
    // Build columns that include the target letter and 5 other distinct letters
    for (uint8_t i=0;i<5;i++) {
      outCols[i][0] = target[i];
      uint8_t filled = 1;
      while (filled < 6) {
        char c = alphabet[random(26)];
        bool dup=false;
        for (uint8_t k=0;k<filled;k++) if (outCols[i][k]==c) { dup=true; break; }
        if (!dup) outCols[i][filled++] = c;
      }
      // Shuffle column
      for (int k=5;k>0;k--) {
        uint8_t j = random(k+1);
        char t = outCols[i][k]; outCols[i][k] = outCols[i][j]; outCols[i][j] = t;
      }
    }
    // Enforce uniqueness: only target must be constructible
    if (!anyNonTargetWordConstructible(target, outCols)) return true;
  }
  return false;
}

// ==========================
// Column select helper
// ==========================
void setActiveCol(uint8_t i) {
  for (uint8_t k=0;k<N_COLS;k++) digitalWrite(COL_PINS[k], HIGH);
  digitalWrite(COL_PINS[i], LOW); // active column
}
