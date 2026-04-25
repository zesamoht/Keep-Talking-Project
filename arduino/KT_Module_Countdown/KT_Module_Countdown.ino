#include <Wire.h>
#include <TM1637Display.h>

#define I2C_ADDR_COUNTDOWN 2

enum : uint8_t {
  K_BATTERY_COUNT=0x01, K_SN_LAST_ODD=0x02, K_SN_VOWEL=0x03,
  K_LIT_FRK=0x04, K_LIT_CAR=0x05, K_MISTAKE_COUNT=0x06,
  K_GAME_DURATION=0x07, K_START_SIGNAL=0x08, K_MISTAKE_ACK=0x09,
  K_GAME_LOST=0x0A, K_GAME_WON=0x0B
};

#define DISP_CLK 7
#define DISP_DIO 6
TM1637Display display(DISP_CLK, DISP_DIO);

#define BUZZER_PIN 9
#define LED1_PIN 12
#define LED2_PIN 11
#define LED3_PIN 10

const int ResetBt_pin = 2;
const int RedLED_pin = 3;
const int GreenLED_pin = 4;
const int BlueLED_pin = 5;

unsigned long previousMillis = 0;
const long blinkInterval = 500;
bool ledState = LOW;

// ---- state/flags ----
volatile uint8_t  M_State = 0;     // 0 precheck, 1 ready, 2 armed, 3 running, 4 lose
bool Mod_Info_Ok = false;
volatile bool Start_Signal = false;
volatile bool Game_Lost = false;
volatile bool Game_Won  = false;
volatile bool Strike_Ack = false;

volatile uint8_t cfgMask = 0;
const uint8_t BIT_GAMEDUR = 0x01;
const uint8_t REQUIRED_MASK = BIT_GAMEDUR;

uint16_t gameDurationSec = 180;
uint32_t tStartMs = 0;
uint16_t lastShownSec = 65535;

volatile uint8_t MistakeCount = 0;
uint8_t prevMistakeCount = 0;
volatile bool strikeEdge = false;   // NEW: a mistake just arrived


// ---- I2C ----
void onI2CReceive(int howMany) {
  if (howMany < 2) return;
  uint8_t key = Wire.read();
  uint8_t len = Wire.read();
  uint8_t b[4] = {0};
  for (uint8_t i=0; i<len && i<sizeof(b) && Wire.available(); i++) b[i]=Wire.read();

  switch (key) {
    case K_GAME_DURATION:
      if (len>=2) {
        gameDurationSec = (uint16_t)b[0] | ((uint16_t)b[1] << 8);
        cfgMask |= BIT_GAMEDUR;
        Mod_Info_Ok = ((cfgMask & REQUIRED_MASK) == REQUIRED_MASK);
      }
      break;
    case K_START_SIGNAL:
      if (len>=1 && b[0]==1) Start_Signal = true;
      break;
    case K_MISTAKE_COUNT:
        if (len>=1) {
        uint8_t newCnt = b[0];
        if (newCnt > MistakeCount) strikeEdge = true; // rising edge
        MistakeCount = newCnt;
      }
      break;
    case K_MISTAKE_ACK: Strike_Ack = true; break;
    case K_GAME_LOST:   Game_Lost = true;  break;
    case K_GAME_WON:    Game_Won  = true;  break;
    
    default: break;
  }
}

void onI2CRequest() {
  Wire.write(M_State);
}

static inline uint16_t timeLeftSec() {
  if (M_State != 3) return gameDurationSec;
  uint32_t elapsed = (millis() - tStartMs) / 1000UL;
  if (elapsed >= gameDurationSec) return 0;
  return (uint16_t)(gameDurationSec - elapsed);
}

void handleStrikeEdge() {
  if (!strikeEdge) return;
  strikeEdge = false;

  // short beep
  digitalWrite(BUZZER_PIN, HIGH); delay(200); digitalWrite(BUZZER_PIN, LOW);

  // update strike LEDs
  digitalWrite(LED1_PIN, MistakeCount >= 1);
  digitalWrite(LED2_PIN, MistakeCount >= 2);
  digitalWrite(LED3_PIN, MistakeCount >= 3);

  if (MistakeCount > 3) M_State = 4;  // lose
  prevMistakeCount = MistakeCount;
}


void setup() {
  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(LED1_PIN, OUTPUT);
  pinMode(LED2_PIN, OUTPUT);
  pinMode(LED3_PIN, OUTPUT);

  pinMode(RedLED_pin, OUTPUT);
  pinMode(GreenLED_pin, OUTPUT);
  pinMode(BlueLED_pin, OUTPUT);
  pinMode(ResetBt_pin, INPUT_PULLUP);

  Wire.begin(I2C_ADDR_COUNTDOWN);
  Wire.onReceive(onI2CReceive);
  Wire.onRequest(onI2CRequest);

  display.setBrightness(0x0f);
  display.showNumberDecEx(0, 0b01000000, true);
  digitalWrite(LED1_PIN, LOW);
  digitalWrite(LED2_PIN, LOW);
  digitalWrite(LED3_PIN, LOW);

  M_State = 0;  // precheck
}

void Precheck() {
  uint16_t tl = gameDurationSec;
  uint8_t mm = tl / 60, ss = tl % 60;
  display.showNumberDecEx(mm*100 + ss, 0b01000000, true);
  digitalWrite(LED1_PIN, LOW);
  digitalWrite(LED2_PIN, LOW);
  digitalWrite(LED3_PIN, LOW);
  M_State = 1; // ready (master will see 1)
}

void loop() {
  unsigned long currentMillis = millis();

  switch (M_State) {
    case 0: // Precheck (enter here after power/reset, or after pressing ResetBt)
      // allow opting-in via Reset button: 0->1 when pressed
      if (digitalRead(ResetBt_pin) == LOW) { delay(30); if (digitalRead(ResetBt_pin)==LOW) Precheck(); }
      // LED: blue steady
      digitalWrite(RedLED_pin, HIGH);
      digitalWrite(GreenLED_pin, HIGH);
      digitalWrite(BlueLED_pin, LOW);
      break;

    case 1: // Ready: wait for gameDuration from master
      if (Mod_Info_Ok) M_State = 2;  // armed/wait start
      // one blink
      digitalWrite(RedLED_pin, HIGH);
      digitalWrite(GreenLED_pin, LOW);
      digitalWrite(BlueLED_pin, HIGH);
      delay(blinkInterval);
      break;  // <-- important!

    case 2: // Armed: wait for START
      if (Start_Signal) {
        Start_Signal = false;
        tStartMs = millis();
        lastShownSec = 65535;
        prevMistakeCount = MistakeCount;
        M_State = 3; // running
        // start beep
        digitalWrite(BUZZER_PIN, HIGH); delay(1000); digitalWrite(BUZZER_PIN, LOW);
      }
      // LED: blue blink
      if (currentMillis - previousMillis >= blinkInterval) {
        previousMillis = currentMillis;
        ledState = !ledState;
        digitalWrite(RedLED_pin, HIGH);
        digitalWrite(GreenLED_pin, HIGH);
        digitalWrite(BlueLED_pin, ledState);
      }
      break;

    case 3: { // Running
      // update display once per second
      uint16_t tl = timeLeftSec();
      if (tl != lastShownSec) {
        lastShownSec = tl;
        uint8_t mm = tl/60, ss = tl%60;
        display.showNumberDecEx(mm*100 + ss, 0b01000000, true);
        if (tl == 0) { M_State = 4; digitalWrite(BUZZER_PIN, HIGH); delay(1000); digitalWrite(BUZZER_PIN, LOW); }
      }
      handleStrikeEdge(); 
      // update strike LEDs on change
      if (MistakeCount != prevMistakeCount) {
        prevMistakeCount = MistakeCount;
        digitalWrite(BUZZER_PIN, HIGH); delay(200); digitalWrite(BUZZER_PIN, LOW);
        digitalWrite(LED1_PIN, MistakeCount >= 1);
        digitalWrite(LED2_PIN, MistakeCount >= 2);
        digitalWrite(LED3_PIN, MistakeCount >= 3);
        if (MistakeCount > 3) M_State = 4;
      }
      // LEDs off in run
      digitalWrite(RedLED_pin, HIGH);
      digitalWrite(GreenLED_pin, HIGH);
      digitalWrite(BlueLED_pin, HIGH);
    } break;

    case 4: // Lose
      // red steady + flash error LEDs
      digitalWrite(RedLED_pin, LOW);
      digitalWrite(GreenLED_pin, HIGH);
      digitalWrite(BlueLED_pin, HIGH);
      digitalWrite(LED1_PIN, HIGH); digitalWrite(LED2_PIN, HIGH); digitalWrite(LED3_PIN, HIGH);
      delay(200);
      digitalWrite(LED1_PIN, LOW);  digitalWrite(LED2_PIN, LOW);  digitalWrite(LED3_PIN, LOW);
      delay(200);
      // stay here until power/reset
      break;
  }
}
