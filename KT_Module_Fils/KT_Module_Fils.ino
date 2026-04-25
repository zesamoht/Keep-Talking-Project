#include <Wire.h>

// I2C address for Maze module (must match master)
#define I2C_ADDR_MAZE 8

// Key/Value protocol (same as master)
enum : uint8_t {
  K_BATTERY_COUNT = 0x01,   // (not used by Maze)
  K_LIT_FRK       = 0x04,   // (not used by Maze)
  K_LIT_CAR       = 0x05,   // (not used by Maze)
  K_GAME_DURATION = 0x07,   // (master may send; Maze ignores)
  K_START_SIGNAL  = 0x08,
  K_MISTAKE_ACK   = 0x09,
  K_GAME_LOST     = 0x0A,
  K_GAME_WON      = 0x0B,
  K_SN_LAST_ODD   = 0x02
};

// ---- State machine vars (keep yours) ----
extern int  M_State;         // 0..5
extern bool Mod_Info_Ok;
extern volatile bool Start_Signal, Mistake_Ack, Game_Lost, Game_Won;

// Handshake: mark when we have received at least one KV from master
volatile uint8_t cfgMask = 0;
const uint8_t BIT_ANY_KV   = 0x80;          // “saw any KV from master”
const uint8_t REQUIRED_MASK = BIT_ANY_KV;   // For Maze, this is all we need


// --- Module constants ---
const float RRef = 220.0;                   // unchanged hardware
const float Acuracy = 0.5;                  // ±50%
const int   WireArray[6] = {0,100,330,1000,2000,5000}; // 0,Y,R,B,K,W

// Fil1..Fil6 (top→bottom)
const uint8_t AnalogPins[6]    = {A7, A6, A3, A2, A1, A0};
const uint8_t WireSensePins[6] = {11, 12, 9, 8, 7, 6};

// --- Measurements ---
float RArray[6] = {0,0,0,0,0,0};
int   RArrayCorr[6] = {0,0,0,0,0,0};
int   AnswerPos = -1;
bool  WireCut[6]     = {false,false,false,false,false,false};
bool  WireHandled[6] = {false,false,false,false,false,false};

// --- IO pins ---
const int ResetBt_pin  = 2;
const int RedLED_pin   = 3;
const int GreenLED_pin = 4;
const int BlueLED_pin  = 5;

// --- Blink ---
unsigned long previousMillis = 0;
const unsigned long blinkInterval = 500;
bool ledState = LOW;

// --- State and flags ---
int  M_State = 0;
volatile bool Start_Signal = false;
volatile bool Mistake_Ack  = false;
volatile bool Game_Lost    = false;
volatile bool Game_Won     = false;

bool Mod_Info_Ok = false;
bool Device_Ready = false;
bool Mistake_Made = false;
bool Module_Solved = false;

uint8_t Battery_Count = 0;
bool SN_Last_Digit_Odd = false;

// --- Template functions ---
void readSensors();
void SolvePuzzle();
void Precheck();
void GameLoop();

void setup() {
  pinMode(ResetBt_pin, INPUT_PULLUP);
  pinMode(RedLED_pin, OUTPUT);
  pinMode(GreenLED_pin, OUTPUT);
  pinMode(BlueLED_pin, OUTPUT);
  for (byte i=0;i<6;i++) pinMode(WireSensePins[i], INPUT_PULLUP);

  Wire.begin(I2C_ADDR_MAZE);
  Wire.onReceive(onI2CReceive);
  Wire.onRequest(onI2CRequest);

  Serial.begin(9600);
}

void loop() {
//  Serial.println(M_State);

  switch (M_State) {
    case 0: M_State_0(); break;
    case 1: M_State_1(); break;
    case 2: M_State_2(); break;
    case 3: M_State_3(); break;
    case 4: M_State_4(); break;
    case 5: M_State_5(); break;
  }
}

// --- I2C handlers ---
void onI2CReceive(int howMany) {
  if (howMany < 2) return;

  uint8_t key = Wire.read();
  uint8_t len = Wire.read();

  uint8_t buf[8];
  for (uint8_t i=0; i<len && Wire.available(); i++) buf[i] = Wire.read();

  switch (key) {
    case K_START_SIGNAL:
      if (len>=1 && buf[0]==1) {
        Start_Signal = true;
        // Serial.println(F("[I2C] START_SIGNAL=1")); // keep if you want
      }
      break;

    case K_MISTAKE_ACK:
      Mistake_Ack = true;
      // Serial.println(F("[I2C] MISTAKE_ACK"));
      break;

    case K_GAME_LOST:
      Game_Lost = true;
      // Serial.println(F("[I2C] GAME_LOST"));
      break;

    case K_GAME_WON:
      Game_Won = true;
      // Serial.println(F("[I2C] GAME_WON"));
      break;

      case K_SN_LAST_ODD:
      if (len >= 1) SN_Last_Digit_Odd = (buf[0] != 0);
      break;

    default:
      // Keys not used by Maze: ignore (but they still count for handshake)
      break;
  }

  // Handshake: any K/V received counts as “info OK”
  cfgMask |= BIT_ANY_KV;
  Mod_Info_Ok = ((cfgMask & REQUIRED_MASK) == REQUIRED_MASK);
}

void onI2CRequest() {
  // Always return current M_State as a single byte
  Wire.write((uint8_t)M_State);
}

// --- States ---
void M_State_0() {
  if (digitalRead(ResetBt_pin) == LOW) {
    delay(50);
    if (digitalRead(ResetBt_pin) == LOW) M_State = 1;
  }
  digitalWrite(RedLED_pin, HIGH);
  digitalWrite(GreenLED_pin, HIGH);
  digitalWrite(BlueLED_pin, LOW);
}

void M_State_1() { // Checking readiness
  static bool didPrecheck = false;

  if (!didPrecheck) {
    Precheck();          // your existing random maze/build
    didPrecheck = true;
  }

  // Advance to “armed-waiting” only after we got any K/V from master
  if (Mod_Info_Ok) {
    M_State = 2;
  }

  // LED: one blink (your original behavior)
  digitalWrite(RedLED_pin,  HIGH);             // show “checked”
  digitalWrite(GreenLED_pin, LOW);             // invert if you prefer
  digitalWrite(BlueLED_pin,  HIGH);
  delay(500);
}

void M_State_2() {
  if (Start_Signal) {
    Start_Signal = false;
    Mistake_Made = false;
    Module_Solved = false;
    readSensors();                                  // get RArrayCorr[] and WireCut[]
    // DEBUG add once in M_State_2 after readSensors()
    Serial.print("Init WireCut: ");
    for (byte f=0; f<6; f++) { Serial.print(WireCut[f]); Serial.print(' '); }
    Serial.print(" | AnswerPos="); Serial.println(AnswerPos);

    for (byte f=0; f<6; f++) WireHandled[f] = WireCut[f]; // remember initial cuts (incl. empty slots)
    Mistake_Made=false; Module_Solved=false; Game_Won=false; Game_Lost=false;
    M_State = 3;
  }
  unsigned long now = millis();
  if (now - previousMillis >= blinkInterval) {
    previousMillis = now;
    ledState = !ledState;
    digitalWrite(RedLED_pin, HIGH);
    digitalWrite(GreenLED_pin, HIGH);
    digitalWrite(BlueLED_pin, !ledState);
  }

  // 5) Debug
  for (byte d=0; d<6; d++){ Serial.print(RArray[d]); Serial.print(","); } Serial.println();
  for (byte d=0; d<6; d++){ Serial.print(RArrayCorr[d]); Serial.print(","); } Serial.println();

}

void M_State_3() {
  if ((WireCut[0]) == 1) { Serial.print(WireCut[0]);}//Debug
  GameLoop();
  if (Mistake_Made) { M_State = 4; Mistake_Made = false; }
  if (Module_Solved) M_State = 5;

  digitalWrite(RedLED_pin, HIGH);
  digitalWrite(GreenLED_pin, HIGH);
  digitalWrite(BlueLED_pin, HIGH);
}

void M_State_4() {

  if (!Game_Lost && Mistake_Ack) { M_State = 3; Mistake_Ack = false; }
  digitalWrite(RedLED_pin, LOW);
  digitalWrite(GreenLED_pin, HIGH);
  digitalWrite(BlueLED_pin, HIGH);
}

void M_State_5() {
  digitalWrite(RedLED_pin, HIGH);
  digitalWrite(GreenLED_pin, LOW);
  digitalWrite(BlueLED_pin, HIGH);
}

// --- readSensors(): inputs and basic transforms ---
void readSensors() {
  // 1) ADC reads (median-of-9) → raw R
  float measR[6];
  bool  valid[6];
  byte  nValid = 0;

  for (byte ch = 0; ch < 6; ch++) {
    int v[9];
    (void)analogRead(AnalogPins[ch]);
    delayMicroseconds(50);
    for (byte i = 0; i < 9; i++) v[i] = analogRead(AnalogPins[ch]);
    for (byte i = 0; i < 5; i++) {
      byte m = i;
      for (byte j = i + 1; j < 9; j++) if (v[j] < v[m]) m = j;
      int t = v[i]; v[i] = v[m]; v[m] = t;
    }
    int adc = v[4];

    if (adc < 3) { RArray[ch] = 1e9; RArrayCorr[ch] = 0; valid[ch] = false; continue; }

    float R = RRef * (1023.0 / adc - 1.0);
    RArray[ch] = R;
    valid[ch] = (R > 10.0 && R < 20000.0);
    if (valid[ch]) { measR[ch] = R; nValid++; } else { RArrayCorr[ch] = 0; }
  }

  // 2) Robust global scale via median ratio to nearest nominal
  const float nom[5] = {100.0, 330.0, 1000.0, 2000.0, 5000.0};
  float ratios[6]; byte m = 0;
  for (byte ch = 0; ch < 6; ch++) if (valid[ch]) {
    float Ri = RArray[ch];
    float best = nom[0], dmin = fabs(Ri - nom[0]);
    for (byte k = 1; k < 5; k++) {
      float d = fabs(Ri - nom[k]);
      if (d < dmin) { dmin = d; best = nom[k]; }
    }
    ratios[m++] = Ri / best;
  }
  float sBest = 1.0;
  if (m > 0) {
    for (byte i = 0; i < m; i++) {
      byte p = i;
      for (byte j = i + 1; j < m; j++) if (ratios[j] < ratios[p]) p = j;
      float t = ratios[i]; ratios[i] = ratios[p]; ratios[p] = t;
    }
    sBest = ratios[m / 2];
  }
  if (sBest < 0.4) sBest = 0.4;
  if (sBest > 2.5) sBest = 2.5;

  // 3) Log-scale classification using geometric boundaries
  for (byte ch = 0; ch < 6; ch++) {
    if (!valid[ch]) continue;
    float Ri = RArray[ch];
    if (Ri < 10.0 || Ri > 20000.0) { RArrayCorr[ch] = 0; continue; }

    float T0 = sBest * nom[0];
    float T1 = sBest * nom[1];
    float T2 = sBest * nom[2];
    float T3 = sBest * nom[3];
    float T4 = sBest * nom[4];

    float boundary01 = sqrt(T0 * T1);
    float boundary12 = sqrt(T1 * T2);
    float boundary23 = sqrt(T2 * T3);
    float boundary34 = sqrt(T3 * T4);

    RArrayCorr[ch] =
      (Ri < boundary01) ? 100  :
      (Ri < boundary12) ? 330  :
      (Ri < boundary23) ? 1000 :
      (Ri < boundary34) ? 2000 : 5000;
  }

  // 4) Digital cut sense (INPUT_PULLUP: HIGH = cut)
  for (byte f = 0; f < 6; f++) WireCut[f] = (digitalRead(WireSensePins[f]) == HIGH);
}



// --- SolvePuzzle(): compute solution from classified data ---
void SolvePuzzle() {
  int WirePos[6]; int NbFils = 0;
  for (byte f=0; f<6; f++) if (RArrayCorr[f] != 0) WirePos[NbFils++] = f;

  if (NbFils < 3) { AnswerPos = -1; return; }

  int CountY=0, CountR=0, CountB=0, CountK=0, CountW=0;
  int LastY=-1, LastR=-1, LastB=-1, LastK=-1, LastW=-1;
  for (int i=0;i<NbFils;i++){
    int v = RArrayCorr[WirePos[i]];
    if (v==100)  {CountY++; LastY=i;}
    if (v==330)  {CountR++; LastR=i;}
    if (v==1000) {CountB++; LastB=i;}
    if (v==2000) {CountK++; LastK=i;}
    if (v==5000) {CountW++; LastW=i;}
  }

  if (NbFils == 3) {
    if (CountR == 0)                       { AnswerPos = WirePos[1]; }
    else if (RArrayCorr[WirePos[2]]==5000) { AnswerPos = WirePos[2]; }
    else if (CountB > 1)                   { AnswerPos = WirePos[LastB]; }
    else                                   { AnswerPos = WirePos[2]; }
  }
  else if (NbFils == 4) {
    if (CountR > 1 && SN_Last_Digit_Odd)      { AnswerPos = WirePos[LastR]; }
    else if (RArrayCorr[WirePos[3]]==100 && CountR==0)
                                           { AnswerPos = WirePos[0]; }
    else if (CountB == 1)                  { AnswerPos = WirePos[0]; }
    else if (CountY > 1)                   { AnswerPos = WirePos[3]; }
    else                                   { AnswerPos = WirePos[1]; }
  }
  else if (NbFils == 5) {
    if (RArrayCorr[WirePos[4]]==2000 && SN_Last_Digit_Odd)
                                           { AnswerPos = WirePos[3]; }
    else if (CountR == 1 && CountY > 1)    { AnswerPos = WirePos[0]; }
    else if (CountK == 0)                  { AnswerPos = WirePos[1]; }
    else                                   { AnswerPos = WirePos[0]; }
  }
  else if (NbFils == 6) {
    if (CountY == 0 && SN_Last_Digit_Odd)     { AnswerPos = WirePos[2]; }
    else if (CountY == 1 && CountW > 1)    { AnswerPos = WirePos[3]; }
    else if (CountR == 0)                  { AnswerPos = WirePos[5]; }
    else                                   { AnswerPos = WirePos[3]; }
  }
}

// --- Precheck(): readiness gate ---
void Precheck() {
  readSensors();
  SolvePuzzle();
  Device_Ready = (AnswerPos >= 0);
}

// --- GameLoop(): game management ---
void GameLoop() {
  readSensors(); // updates WireCut[]

  for (byte f = 0; f < 6; f++) {
    // rising edge: intact -> cut
    if (WireCut[f] && !WireHandled[f]) {
      WireHandled[f] = true;                 // latch once

      //DEBUG
        Serial.print("Wire cut at position: ");
        Serial.print(f);
        Serial.print(" | Expected: ");
        Serial.println(AnswerPos);


      if (f == AnswerPos) {
        Serial.println("Correct wire");
        Module_Solved = true;
        Game_Won = true;
      } else {
        Serial.println("Wrong wire");
        Mistake_Made = true;                 // M_State -> 4, wait for Mistake_Ack
      }
    }
  }
}

