#include <Wire.h>
#include <light_CD74HC4067.h>

#define I2C_ADDR_KEYBOARD 3   // must match master

enum : uint8_t {
  K_BATTERY_COUNT = 0x01,   // (unused)
  K_SN_LAST_ODD   = 0x02,   // (unused)
  K_SN_VOWEL      = 0x03,   // (unused)
  K_LIT_FRK       = 0x04,   // (unused)
  K_LIT_CAR       = 0x05,   // (unused)
  K_MISTAKE_COUNT = 0x06,   // (unused)
  K_GAME_DURATION = 0x07,   // (unused)
  K_START_SIGNAL  = 0x08,   // USED
  K_MISTAKE_ACK   = 0x09,   // USED
  K_GAME_LOST     = 0x0A,   // USED
  K_GAME_WON      = 0x0B    // USED
};


//Module specific variables
  CD74HC4067 mux(10, 11, 12, 13);  // create a new CD74HC4067 object with its four select lines
  int GreenLEDPin[4] = {9, 8, 7, 6}; //Digital pins of the Green LED (1 per key)
  const int M0_signal_pin = A0; // Pin Connected to Sig pin of CD74HC4067
  const int M0_enable_pin = A2; // Pin Connected to En pin of CD74HC4067
  const int M1_signal_pin = A1; // Pin Connected to Sig pin of CD74HC4067
  const int M1_enable_pin = A3; // Pin Connected to En pin of CD74HC4067
  int M0Array[16];//Raw readings of M0
  int M1Array[16];//Raw readings of M1
  byte AnswerPosArray[4];
  int AnswerPos = -1;
  const int threshold = 512;         // Threshold for HIGH/LOW
  byte KeyCodes[4];                  // Keys 1 to 4
  int KeyOrder[4] = {0, 1, 2, 3}; //When reordering the keys use this array as a base
  int PressedButton = -1;            // Button pressed (0-3), or -1 if none
  int ButtonStep = 0;            // current order of button pressed (0-3)
  const byte puzzleTable[7][6] = {
    {B11011, B01111, B00000, B01010, B10111, B01010},
    {B01100, B11011, B00111, B10100, B00011, B01111},
    {B11101, B10110, B11001, B11110, B11110, B11010},
    {B01011, B11001, B00100, B00110, B10101, B01101},
    {B00110, B00010, B01110, B00100, B10100, B10111},
    {B01000, B01000, B11101, B10011, B10010, B10001},
    {B10110, B10011, B00010, B00011, B00001, B00101}
  };


//Initializing Std Pin
  const int ResetBt_pin = 2;
  const int RedLED_pin = 5;
  const int GreenLED_pin = 4;
  const int BlueLED_pin = 3;

// LED Variables
  unsigned long previousMillis = 0;
  const long blinkInterval = 500;
  bool ledState = LOW;

// Communication Variables
volatile bool Start_Signal = false;
volatile bool Mistake_Ack  = false;
volatile bool Game_Lost    = false;
volatile bool Game_Won     = false;

bool Device_Ready  = false;   // you already set this in Precheck()
bool Mistake_Made  = false;   // set this in your puzzle logic
bool Module_Solved = false;   // set this in your puzzle logic
bool Mod_Info_Ok   = true;    // no config required for Keyboard
int  M_State       = 0;       // 0..5

void setup() {
  //Pin Setup
  pinMode(ResetBt_pin, INPUT_PULLUP);
  pinMode(RedLED_pin, OUTPUT);
  pinMode(GreenLED_pin, OUTPUT);
  pinMode(BlueLED_pin, OUTPUT);
    //Module specific
  pinMode(M0_signal_pin, INPUT);
  pinMode(M0_enable_pin, OUTPUT);
  pinMode(M1_signal_pin, INPUT);
  pinMode(M1_enable_pin, OUTPUT); 
  for (int i = 0; i < 4; i++) {pinMode(GreenLEDPin[i],OUTPUT);}

  //I2C & Serial setup
  Wire.begin(I2C_ADDR_KEYBOARD);
  Wire.onReceive(onI2CReceive);
  Wire.onRequest(onI2CRequest);

  //Serial.begin(9600);
  digitalWrite(M0_enable_pin, HIGH);
  digitalWrite(M1_enable_pin, HIGH);

}

void loop() { 
  //Serial.println(M_State);//Debug

//Game State
  switch (M_State) {
    case 0: M_State_0(); break;
    case 1: M_State_1(); break;
    case 2: M_State_2(); break;
    case 3: M_State_3(); break;
    case 4: M_State_4(); break;
    case 5: M_State_5(); break;
  }
}

void onI2CReceive(int howMany){
  if (howMany < 2) return;
  uint8_t key = Wire.read();
  uint8_t len = Wire.read();
  uint8_t b[4]={0};
  for (uint8_t i=0; i<len && Wire.available() && i<sizeof(b); i++) b[i]=Wire.read();

  switch(key){
    case K_START_SIGNAL: if (len>=1 && b[0]==1) Start_Signal = true; break;
    case K_MISTAKE_ACK:  Mistake_Ack = true; break;
    case K_GAME_LOST:    Game_Lost   = true; break;
    case K_GAME_WON:     Game_Won    = true; break;
    default: break; // Keyboard uses no config KVs
  }
}

void onI2CRequest(){
  Wire.write((uint8_t)M_State);   // master polls our state
}

void M_State_0() { // Awaiting Setup
  //Current step
    //NA
  //Next step
  if (digitalRead(ResetBt_pin) == LOW) {
    delay(50); // Simple debounce
    if (digitalRead(ResetBt_pin) == LOW) { // Confirm button press
      M_State = 1;
    }
  }
  //LED Control : Bleu Continu
    digitalWrite(RedLED_pin, HIGH);
    digitalWrite(GreenLED_pin, HIGH);
    digitalWrite(BlueLED_pin, LOW);
}
void M_State_1() {
  static bool did=false;
  if (!did) { Precheck(); did=true; }
  if (Device_Ready) M_State = 2;

  digitalWrite(RedLED_pin, Device_Ready);
  digitalWrite(GreenLED_pin, !Device_Ready);
  digitalWrite(BlueLED_pin, HIGH);
  delay(blinkInterval);
}

void M_State_2() {
  if (Start_Signal) { 
    Start_Signal = false;
    ButtonStep = 0;
    PressedButton = -1;
    for (int i=0;i<4;i++) digitalWrite(GreenLEDPin[i], LOW);
    M_State = 3;
  }
  //LED Control : Clignote Bleu
  unsigned long currentMillis = millis();
  if (currentMillis - previousMillis >= blinkInterval) {
    previousMillis = currentMillis;
    ledState = !ledState;
    digitalWrite(RedLED_pin, HIGH);
    digitalWrite(GreenLED_pin, HIGH);
    digitalWrite(BlueLED_pin, !ledState);
  }
}
void M_State_3() { // Game In Progress
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
void M_State_4() { // Mistake cycle
  //Current step
   //Mistake_Ack received form Master
  //Next step
  if (!Game_Lost && Mistake_Ack) {
    M_State = 3;
    Mistake_Ack = false;
  }
  //LED Control : Clignote Rouge une fois
    digitalWrite(RedLED_pin, LOW);
    digitalWrite(GreenLED_pin, HIGH);
    digitalWrite(BlueLED_pin, HIGH);

}
void M_State_5() { // Module Solved
  //Current step
  //...
  //Next step
  //NA
  //LED Control : Vert Continu
    digitalWrite(RedLED_pin, HIGH);
    digitalWrite(GreenLED_pin, LOW);
    digitalWrite(BlueLED_pin, HIGH);
}
void Precheck() { // Checking readyness
  Device_Ready = false;
  ReadKey();
  ReadBt();
  SolvePuzzle();
  Device_Ready = (AnswerPos >= 0 ); // Définir la condition de succès
}
void GameLoop() { // Game In Progress
  //Current step
    //Les étapes du jeu
  ReadBt();
  // Button check
  if (PressedButton != -1) {
    //Serial.println(PressedButton);
    if(KeyOrder[ButtonStep] == PressedButton) {
      digitalWrite(GreenLEDPin[PressedButton], HIGH);
      if(ButtonStep == 3) {Module_Solved = true;}
      else {ButtonStep ++;}
    }
    else {
      Mistake_Made = true;
    }
  } 
}

void ReadKey(){
  //Fill Raw array M0
  digitalWrite(M0_enable_pin, LOW);
  digitalWrite(M1_enable_pin, HIGH);
  // loop through channels 0 - 15 of M0
  for (byte i = 0; i < 16; i++) {
    mux.channel(i);
    analogRead(M0_signal_pin); // Dummy read to settle
    delay(3); // settling time
    //Serial.print(analogRead(M0_signal_pin));//debug
    M0Array[i] = analogRead(M0_signal_pin);
  }
  digitalWrite(M0_enable_pin, HIGH);
  digitalWrite(M1_enable_pin, LOW);
  delay(3); // settling time
  // loop through channels 0 - 15 of M1
  for (byte i = 0; i < 16; i++) {
    mux.channel(i);
    analogRead(M1_signal_pin); // Dummy read
    delay(3); // settling time
    //Serial.print(analogRead(M1_signal_pin));//debug
    M1Array[i] = analogRead(M1_signal_pin);
  }

  //Decode
    // --- Decode M0: Keys 1, 2, 3 ---
    for (int k = 0; k < 3; k++) {
      byte keyValue = 0;
      for (int b = 0; b < 5; b++) {
        int idx = k * 5 + b;           // 0–4, 5–9, 10–14
        if (M0Array[idx] > threshold) {
          keyValue |= (1 << (4 - b));  // MSB first
        }
      }
      KeyCodes[k] = keyValue;
    }

    // --- Decode M1: Key 4 and Button Press ---
    byte keyValue = 0;
    for (int b = 0; b < 5; b++) {
      if (M1Array[b] > threshold) {
        keyValue |= (1 << (4 - b));
      }
    }
    KeyCodes[3] = keyValue;

    // --- Debug Output ---
    for (int i = 0; i < 4; i++) {
      //Serial.print("Key ");
      //Serial.print(i + 1);
      //Serial.print(": ");
      //Serial.println(KeyCodes[i], BIN);
  }
}

void ReadBt(){
    // Button detection with index correction
    // Mapping: Button 1 (pin 8) = M1Array[5], Button 4 (pin 5) = M1Array[8]
    // So mapping from physical to logical: Btn1 = 0, Btn2 = 1, Btn3 = 2, Btn4 = 3
    const byte buttonOrder[4] = {5, 6, 7, 8}; // Physical pin mapping in array
    PressedButton = -1;
    for (int i = 0; i < 4; i++) {
      if (M1Array[buttonOrder[i]] > threshold) {  // Active-HIGH with pulldown
        PressedButton = i;          // Logical button number
        break;
      }
    }

}

void SolvePuzzle() {
  AnswerPos = -1;  // Default if no column matches
  for (int col = 0; col < 6; col++) {
    bool allFound = true;
    for (int k = 0; k < 4; k++) {
      bool foundInColumn = false;
      for (int row = 0; row < 7; row++) {
        if (puzzleTable[row][col] == KeyCodes[k]) {
          foundInColumn = true;
          AnswerPosArray[k] = row;  // Record row where key was found
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

    // Sort keyOrder[] based on FoundRowForKey values using a simple bubble sort
  for (int i = 0; i < 3; i++) {
    for (int j = i + 1; j < 4; j++) {
      if (AnswerPosArray[KeyOrder[i]] > AnswerPosArray[KeyOrder[j]]) {
        // Swap key indices
        int temp = KeyOrder[i];
        KeyOrder[i] = KeyOrder[j];
        KeyOrder[j] = temp;
      }
    }
  }
}