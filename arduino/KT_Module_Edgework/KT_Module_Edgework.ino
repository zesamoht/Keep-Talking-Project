// ================= MASTER MODULE =================
// Hardware: FS90R (D6) + external pot (A0), ARM button (D7)
// FRK LED (D5), CAR LED (D4), AA battery presence on A1/A2/A3
// I2C master polling with light bus load + fast mistake detection
//
// Serial drum positions (deg): 0, 90, 180, 230
// Labels (index): 0=E9T2K4, 1=B7Z3X5, 2=M8R4N6, 3=K4E7Q5

#include <Wire.h>
#include <Servo.h>

// ---------------- Pins ----------------
const int SERVO_PIN = 6;
const int POT_PIN   = A0;
const int TIME_BTN = 2;
const int ARM_BTN   = 7;
const int LED_FRK   = 5;
const int LED_CAR   = 4;
const int BAT1_PIN  = A1;
const int BAT2_PIN  = A2;
const int BAT3_PIN  = A3;

// -------------- I2C addrs --------------
const uint8_t ADDR_BUTTON     = 1;
const uint8_t ADDR_COUNTDOWN  = 2;
const uint8_t ADDR_KEYBOARD   = 3;
const uint8_t ADDR_KEYPADPCB  = 4;
const uint8_t ADDR_MAZE       = 5;
const uint8_t ADDR_MEMORY     = 6;
const uint8_t ADDR_SIMONSAY   = 7;
const uint8_t ADDR_WIRE       = 8;
const uint8_t ADDR_PASSWORD   = 9; // example (not used by Button)

// ----------- I2C key/value -------------
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

// ---- Game start lock ----
bool gameStarted = false;
unsigned long gameStartMs = 0;


// -------------- Servo model ------------
const float POS_DEG[4] = { 0.0f, 90.0f, 180.0f, 230.0f };
const char* SERIAL_STR[4] = { "E9T2K4","B7Z3X5","M8R4N6","K4E7Q5" };

// Pot calibration (A0 → 0..240 deg)
const int POT_AT_0   = 0;    // tune to your linkage
const int POT_AT_240 = 864;  // tune to your linkage

// FS90R micros
const int US_STOP = 1500;
const int US_MIN  = 1300;
const int US_MAX  = 1700;

// Control params
float Kp = 8.0;
const int   MIN_DELTA_US = 50;
const float DEG_DEADBAND = 10.0f;
const int   SAMPLE_DT_MS = 50;
const unsigned long TIMEOUT_MS = 4000;
const float ALPHA = 0.3f;

Servo m;
bool servoAttached = false;
float potFilt = 0;

// Button debounce
int lastBtn = HIGH;
unsigned long lastBounce = 0;
const unsigned long DEBOUNCE_MS = 30;
int lastTimeBtn = HIGH;
unsigned long lastTimeBounce = 0;
const unsigned long TIME_DEBOUNCE_MS = 30;


// ----------------- Game cfg -----------------
uint16_t gameDurationSec = 300; // 3/4/5 min at start
bool litFRK = false;
bool litCAR = false;
uint8_t mistakes = 0;

// Serial drum
int serialIndex = 2; // start 180°

// ----------------- Polling ------------------
struct Mod {
  uint8_t addr;
  bool    selected;        // in this game (m_state==1 at start)
  bool    active;          // currently in m_state==3
  uint8_t lastState;
  unsigned long nextPollAt;
  unsigned long priorityUntil;
};
Mod mods[] = {
  {ADDR_BUTTON,    false,false,0,0,0},
  {ADDR_COUNTDOWN, false,false,0,0,0},
  {ADDR_KEYBOARD,  false,false,0,0,0},
  {ADDR_KEYPADPCB, false,false,0,0,0},
  {ADDR_MAZE,      false,false,0,0,0},
  {ADDR_MEMORY,    false,false,0,0,0},
  {ADDR_SIMONSAY,  false,false,0,0,0},
  {ADDR_WIRE,      false,false,0,0,0},
  {ADDR_PASSWORD,  false,false,0,0,0},
};
const int NMOD = sizeof(mods)/sizeof(mods[0]);

const unsigned long TICK_MS         = 20;   // scheduler tick
unsigned long       lastTick        = 0;
unsigned            rrIndex         = 0;
const unsigned long ACTIVE_POLL_MS  = 160;  // target cadence
const unsigned long IDLE_POLL_MS    = 1200; // diagnostics
const unsigned long PRIORITY_MS     = 500;  // tight follow-up on mistake

// ================= Utils =================
float clampf(float v, float lo, float hi){ return v<lo?lo:(v>hi?hi:v); }
int clampi(int v, int lo, int hi){ return v<lo?lo:(v>hi?hi:v); }
float potToDeg(int adc){
  float span = (float)(POT_AT_240 - POT_AT_0);
  if (span <= 1.0f) return 0.0f;
  return clampf(240.0f * (adc - POT_AT_0) / span, 0.0f, 240.0f);
}
int controlUS(float errDeg){
  if (fabs(errDeg) <= DEG_DEADBAND) return US_STOP;
  float mag = MIN_DELTA_US + Kp * fabs(errDeg);
  float u = US_STOP + (errDeg > 0 ? +mag : -mag);
  return clampi((int)(u + 0.5f), US_MIN, US_MAX);
}
void attachServoIfNeeded() {
  if (!servoAttached) {
    m.attach(SERVO_PIN);
    m.writeMicroseconds(US_STOP);
    delay(100);
    servoAttached = true;
  }
}
void detachServo() {
  if (servoAttached) {
    m.writeMicroseconds(US_STOP);
    delay(100);
    m.detach();
    servoAttached = false;
  }
}
void goToDeg(float tgt){
  attachServoIfNeeded();
  tgt = clampf(tgt, 0.0f, 240.0f);
  unsigned long t0 = millis();
  int stableCount = 0;
  while (stableCount < 8) {
    int adc = analogRead(POT_PIN);
    if (potFilt == 0) potFilt = adc;
    else potFilt = ALPHA * adc + (1 - ALPHA) * potFilt;
    float pos = potToDeg((int)(potFilt + 0.5f));
    float err = tgt - pos;
    int us = controlUS(err);
    m.writeMicroseconds(us);
    if (fabs(err) <= DEG_DEADBAND) stableCount++; else stableCount = 0;
    if (millis() - t0 > TIMEOUT_MS) break;
    delay(SAMPLE_DT_MS);
  }
  detachServo();
  delay(150);
}

// Batteries: present if ADC > threshold
bool adcPresent(int pin){ return analogRead(pin) > 200; }
uint8_t batteryCount(){
  uint8_t n=0; if (adcPresent(BAT1_PIN)) n++; if (adcPresent(BAT2_PIN)) n++; if (adcPresent(BAT3_PIN)) n++;
  return n;
}

inline uint8_t snLastDigitOdd() {
  // SERIAL_STR: { "E9T2K4","B7Z3X5","M8R4N6","K4E7Q5" }
  return (serialIndex == 1 || serialIndex == 3) ? 1 : 0;
}

// I2C send helpers
void sendKV(uint8_t addr, uint8_t key, const uint8_t* buf, uint8_t len){
  Wire.beginTransmission(addr);
  Wire.write(key); Wire.write(len);
  for(uint8_t i=0;i<len;i++) Wire.write(buf[i]);
  Wire.endTransmission();
}
void sendU8(uint8_t addr, uint8_t key, uint8_t v){ sendKV(addr,key,&v,1); }
void sendU16(uint8_t addr, uint8_t key, uint16_t v){
  uint8_t b[2]={(uint8_t)(v&0xFF),(uint8_t)(v>>8)}; sendKV(addr,key,b,2);
}

// Push variables needed by each module (based on your “used=1” table)
// helpers you already have:
// void sendU8(uint8_t addr, uint8_t key, uint8_t v);
// void sendU16(uint8_t addr, uint8_t key, uint16_t v);
// uint8_t batteryCount();  uint8_t snLastDigitOdd();  uint8_t snHasVowel();
// globals: bool litFRK, litCAR; uint8_t mistakeCount; uint16_t gameDurationSec;

void pushVarsToModule(Mod& m){
  switch (m.addr) {

    case ADDR_BUTTON:           // BatteryCount, LitFRK, LitCAR, GameDuration
      sendU8 (m.addr, K_BATTERY_COUNT, batteryCount());
      sendU8 (m.addr, K_LIT_FRK,       litFRK ? 1 : 0);
      sendU8 (m.addr, K_LIT_CAR,       litCAR ? 1 : 0);
      sendU16(m.addr, K_GAME_DURATION, gameDurationSec);
      break;

    case ADDR_COUNTDOWN:        // MistakeCount, GameDuration
      sendU8 (m.addr, K_MISTAKE_COUNT, mistakes);
      sendU16(m.addr, K_GAME_DURATION, gameDurationSec);
      break;

    case ADDR_SIMONSAY:         // SNVowelPresent, MistakeCount
      sendU8 (m.addr, K_SN_VOWEL,      snHasVowel());
      sendU8 (m.addr, K_MISTAKE_COUNT, mistakes);
      break;

    case ADDR_WIRE:             // SNLastDigitOdd
      sendU8 (m.addr, K_SN_LAST_ODD,   snLastDigitOdd());
      break;

    // Keyboard, Keypad_PCB, Maze, Memory, Password: no config keys
    case ADDR_KEYBOARD:
    case ADDR_KEYPADPCB:
    case ADDR_MAZE:
    case ADDR_MEMORY:
    case ADDR_PASSWORD:
    default:
      break;
  }
}


// ================= Flow =================
void applySerialIndex() {
  if (gameStarted) return;

  // Move servo
  goToDeg(POS_DEG[serialIndex]);

  // Randomize FRK/CAR independently
  litFRK = random(2);  // 0 or 1
  litCAR = random(2);  // 0 or 1

  digitalWrite(LED_FRK, litFRK ? LOW : HIGH);
  digitalWrite(LED_CAR, litCAR ? LOW : HIGH);
}

void selectGameTime() {
  static int minuteSetting = 2; // start at 2 minutes
  minuteSetting++;
  if (minuteSetting > 5) minuteSetting = 2;
  gameDurationSec = (uint16_t)(minuteSetting * 60);

  /*Serial.print(F("Game time set to "));
  Serial.print(minuteSetting);
  Serial.println(F(" minutes."));*/
}


// --- I2C read with retry + explicit types ---
int8_t readMState(uint8_t addr){
  for (uint8_t attempt=0; attempt<2; attempt++){
    Wire.requestFrom(addr, (uint8_t)1, (uint8_t)true); // 1 byte, stop=true
    if (Wire.available()){
      return (int8_t)Wire.read();
    }
    delay(2);
  }
  return -1; // no response
}


// --- One-time scan to see who ACKs (call once in setup, after Wire.begin) ---
void i2cScan(){
  //Serial.println(F("I2C scan:"));
  for (uint8_t a=1; a<127; a++){
    Wire.beginTransmission(a);
    uint8_t e = Wire.endTransmission();
    if (e==0){ //Serial.print(F("  ACK @ 0x")); Serial.println(a, HEX); 
    }
  }
  //Serial.println(F("Scan done."));
}

void precheckDiscoverModules(){
  //Serial.println(F("Precheck: probing modules..."));
  unsigned long t0 = millis();
  bool anySeen = false;
  while (millis()-t0 < 1000) {
    for (int i=0; i<NMOD; i++){
      uint8_t addr = mods[i].addr;
      int8_t st = readMState(addr);          // retrying read (your new one)
      if (st >= 0) anySeen = true;
      mods[i].lastState = (st<0)?255:st;
      mods[i].selected  = (st==1 || st==2);
      mods[i].active    = false;
      unsigned long now = millis();
      mods[i].nextPollAt    = now + (mods[i].selected ? 10 : IDLE_POLL_MS);
      mods[i].priorityUntil = 0;
    }
    if (anySeen) break;   // at least one slave answered
    delay(50);
  }
  for (int i=0;i<NMOD;i++){
    /*Serial.print(F("  addr 0x")); Serial.print(mods[i].addr, HEX);
    Serial.print(F(" -> m_state=")); Serial.print((int)mods[i].lastState);
    Serial.print(F("  selected=")); Serial.println(mods[i].selected ? F("YES") : F("no"));*/
  }
}

void broadcastStart(){
  for(int i=0;i<NMOD;i++){
    if (!mods[i].selected) continue;
    sendU8(mods[i].addr, K_START_SIGNAL, 1);
  }
}

void pushVarsToSelected(){
  for(int i=0;i<NMOD;i++){
    if (!mods[i].selected) continue;
    pushVarsToModule(mods[i]);
  }
}

void syncMistakeCount(){
  for (int i=0;i<NMOD;i++){
    if (!mods[i].selected) continue;
    if (mods[i].addr==ADDR_COUNTDOWN || mods[i].addr==ADDR_SIMONSAY)
      sendU8(mods[i].addr, K_MISTAKE_COUNT, mistakes);
  }
}

// Poll scheduler (lightweight)
void pollScheduler(){
  unsigned long now = millis();

  // Priority pass (modules in mistake-recovery)
  for(int i=0;i<NMOD;i++){
    if (mods[i].priorityUntil && now < mods[i].priorityUntil && now >= mods[i].nextPollAt){
      int8_t st = readMState(mods[i].addr);
      if (st>=0) mods[i].lastState = st;
      mods[i].nextPollAt = now + 50;
      return;
    }
  }

  // One or two modules per tick depending on active count
  int activeCount=0; for(int i=0;i<NMOD;i++) if (mods[i].active) activeCount++;
  int pollsThisTick = (activeCount>8)?2:1;

  for(int k=0;k<pollsThisTick;k++){
    for (int t=0;t<NMOD;t++){
      rrIndex = (rrIndex+1)%NMOD;
      int i = rrIndex;
      unsigned long period = mods[i].active ? ACTIVE_POLL_MS : IDLE_POLL_MS;
      if (now >= mods[i].nextPollAt){
        int8_t st = readMState(mods[i].addr);
        if (st>=0){
          uint8_t prev = mods[i].lastState;
          mods[i].lastState = st;

          // *** NEW: auto-select when a module advertises ready (1) or waiting-for-start (2)
          if ((st == 1 || st == 2) && !mods[i].selected) {
            mods[i].selected = true;
            pushVarsToModule(mods[i]);           // send K/V right away
            //Serial.print(F("Auto-selected addr 0x"));
            //Serial.print(mods[i].addr, HEX);
            //Serial.print(F(" (m_state=")); Serial.print(st); Serial.println(')');
          }
          // Edge handling
          if (prev!=st){
            if (st==3) mods[i].active=true;
            if (st==5) mods[i].active=false; // solved
            if (st==4){ // mistake → ack + priority
              mistakes++;
              sendU8(mods[i].addr, K_MISTAKE_ACK, 1);
              mods[i].priorityUntil = now + PRIORITY_MS;
              syncMistakeCount();  // <-- add this line
            }
          }
        }
        mods[i].nextPollAt = now + period;
        break; // one polled, move on
      }
    }
  }
}

uint16_t timeLeftSec() {
  if (!gameStarted) return gameDurationSec;
  unsigned long elapsed = (millis() - gameStartMs) / 1000UL;
  if (elapsed >= gameDurationSec) return 0;
  return (uint16_t)(gameDurationSec - elapsed);
}

float currentServoDeg() {
  int adc = analogRead(POT_PIN);
  return potToDeg(adc);
}

// --- helpers you call in pushVarsToModule ---
uint8_t snHasVowel(){
  const char* s = SERIAL_STR[serialIndex];
  while (*s){
    char c = *s++;
    if (c=='A'||c=='E'||c=='I'||c=='O'||c=='U') return 1;
  }
  return 0;
}

void printMMSS(uint16_t sec) {
  uint16_t m = sec / 60, s = sec % 60;
  //Serial.print(m); Serial.print(':'); if (s < 10) Serial.print('0'); Serial.print(s);
}

/*void printStatusSnapshot() {
  Serial.print(F("SERVO_DEG=")); Serial.println(currentServoDeg(), 1);
  Serial.print(F("TIMER="));     printMMSS(timeLeftSec()); Serial.println();
  Serial.print(F("FRK="));       Serial.println(litFRK ? F("ON") : F("OFF"));
  Serial.print(F("CAR="));       Serial.println(litCAR ? F("ON") : F("OFF"));
  Serial.print(F("BAT1="));      Serial.println(adcPresent(BAT1_PIN) ? F("PRESENT") : F("ABSENT"));
  Serial.print(F("BAT2="));      Serial.println(adcPresent(BAT2_PIN) ? F("PRESENT") : F("ABSENT"));
  Serial.print(F("BAT3="));      Serial.println(adcPresent(BAT3_PIN) ? F("PRESENT") : F("ABSENT"));
  Serial.println(F("----"));
}
*/

// ================= Arduino =================
void setup(){
  Serial.begin(9600);
  pinMode(ARM_BTN, INPUT_PULLUP);
  pinMode(TIME_BTN, INPUT_PULLUP);
  pinMode(LED_FRK, OUTPUT);
  pinMode(LED_CAR, OUTPUT);
  digitalWrite(LED_FRK, LOW);
  digitalWrite(LED_CAR, LOW);

  Wire.begin();
  //Wire.setClock(50000);   // 50 kHz = tolerant
  delay(2000);             // let slaves boot
  i2cScan();
  precheckDiscoverModules();

  // Servo init
  attachServoIfNeeded();
  m.writeMicroseconds(US_STOP);
  delay(300);
  potFilt = analogRead(POT_PIN);

  // Random seed
  randomSeed(analogRead(A7)); // any floating analog

  // Choose initial serial and move
  serialIndex = random(4);
  applySerialIndex();

  // Precheck: choose game time, discover modules
  selectGameTime();
  precheckDiscoverModules();

  // Push initial vars to all selected modules
  pushVarsToSelected();

  //Serial.println("Master ready. Hold ARM to start when at least one module is in state 1 or 2.");
}

void loop(){
  unsigned long now = millis();

  int tbtn = digitalRead(TIME_BTN);
  if (!gameStarted && lastTimeBtn == HIGH && tbtn == LOW && millis() - lastTimeBounce > TIME_DEBOUNCE_MS) {
    lastTimeBounce = millis();
    selectGameTime();  // cycle time
    pushVarsToSelected(); // update all modules
  }
  lastTimeBtn = tbtn;


  // Ticker
  if (now - lastTick >= TICK_MS){
    lastTick = now;
    pollScheduler();
  }

  // ARM button: short press cycles serial; long press (>1s) starts the game
  static unsigned long pressT=0;
  int btn = digitalRead(ARM_BTN);
  if (lastBtn==HIGH && btn==LOW && now-lastBounce>DEBOUNCE_MS){ lastBounce=now; pressT=now; }
  if (lastBtn==LOW && btn==HIGH && now-lastBounce>DEBOUNCE_MS){
    lastBounce=now;
    unsigned long held = now - pressT;

  if (held > 1000){
    if (!gameStarted){
      bool anySelected=false; for(int i=0;i<NMOD;i++) if (mods[i].selected) { anySelected=true; break; }
      if (anySelected){
        broadcastStart();
        gameStarted = true;                // <<< LOCK: no more changes allowed
        gameStartMs = millis();            // start local shadow timer
        //Serial.println("Game START sent.");
      } else {
        //Serial.println("No modules ready (state 1 or 2).");
        precheckDiscoverModules();
      }
    } else {
      //Serial.println("Game already started; ignoring start press.");
    }
  }
    else {
      if (!gameStarted){
        // Cycle serial (changes indicators) BEFORE start only
        serialIndex = (serialIndex + 1) & 3;
        applySerialIndex();          // moves servo + sets FRK/CAR LEDs
        pushVarsToSelected();        // push updated indicators/batteries
        //Serial.print("Serial -> "); Serial.println(SERIAL_STR[serialIndex]);

        // Print snapshot ONLY before game is started
        //printStatusSnapshot();
      } 
      else {
      // After start: do not change variables, just ignore short press (or print status if you want)
      // printStatusSnapshot(); // <— enable if you want read-only status after start
      }
    }
  }
  lastBtn = btn;

  // End on Countdown module state==4 (time up)
  // We check its lastState; if 4 → send GAME_LOST to all selected
  for(int i=0;i<NMOD;i++){
    if (mods[i].addr==ADDR_COUNTDOWN && mods[i].lastState==4){
      for(int j=0;j<NMOD;j++){
        if (mods[j].selected) sendU8(mods[j].addr, K_GAME_LOST, 1);
      }
      //Serial.println("Time up → GAME_LOST broadcast.");
      while(1){} // stop loop; or implement a reset flow
    }
  }
}
