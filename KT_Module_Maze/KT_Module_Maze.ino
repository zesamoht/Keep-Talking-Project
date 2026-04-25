#include <Wire.h>
#include <Adafruit_NeoPixel.h>
#include <avr/pgmspace.h>

// I2C address for Maze module (must match master)
#define I2C_ADDR_MAZE 5

// Key/Value protocol (same as master)
enum : uint8_t {
  K_BATTERY_COUNT = 0x01,   // (not used by Maze)
  K_LIT_FRK       = 0x04,   // (not used by Maze)
  K_LIT_CAR       = 0x05,   // (not used by Maze)
  K_GAME_DURATION = 0x07,   // (received but not used)
  K_START_SIGNAL  = 0x08,
  K_MISTAKE_ACK   = 0x09,
  K_GAME_LOST     = 0x0A,
  K_GAME_WON      = 0x0B
};

// -------- NeoPixel Setup --------
#define LED_PIN 6
#define LED_COUNT 64
Adafruit_NeoPixel strip(LED_COUNT, LED_PIN, NEO_GRB + NEO_KHZ800);

// Direction buttons
const int btnUp    = 8;
const int btnDown  = 9;
const int btnLeft  = 10;
const int btnRight = 7;

// -------- Module Specific --------
const int mazeSize = 13;
const int cellSize = 2;  // 2x2 cells in LED grid

// Store all mazes in PROGMEM (Flash)
const byte mazes[9][mazeSize][mazeSize] PROGMEM = {
  { // Maze 0
    {3,3,3,3,3,3,3,3,3,3,3,3,3},
    {3,0,0,0,0,0,1,0,0,0,0,0,3},
    {3,0,1,1,0,0,1,0,0,1,1,1,3},
    {3,2,1,0,0,0,1,0,0,0,0,0,3},
    {3,0,1,0,0,1,1,1,1,1,0,0,3},
    {3,0,1,0,0,0,1,0,0,0,0,2,3},
    {3,0,1,0,0,0,0,0,1,1,0,0,3},
    {3,0,1,0,0,0,0,0,1,0,0,0,3},
    {3,0,1,1,1,1,1,1,1,1,1,0,3},
    {3,0,0,0,0,0,1,0,0,0,1,0,3},
    {3,0,0,1,1,0,0,0,0,1,1,0,3},
    {3,0,0,0,1,0,0,0,0,1,0,0,3},
    {3,3,3,3,3,3,3,3,3,3,3,3,3}
  },
  { // Maze 1
    {3,3,3,3,3,3,3,3,3,3,3,3,3},
    {3,0,0,0,0,0,1,0,0,0,0,0,3},
    {3,1,1,1,0,1,1,1,0,1,1,1,3},
    {3,0,0,0,1,0,0,0,1,2,0,0,3},
    {3,0,1,1,1,0,1,1,1,1,1,0,3},
    {3,0,1,0,0,0,1,0,0,0,0,0,3},
    {3,0,1,0,1,1,1,0,1,1,1,0,3},
    {3,0,0,2,1,0,0,0,1,0,1,0,3},
    {3,0,1,1,1,0,1,1,1,0,1,0,3},
    {3,0,1,0,1,0,1,0,0,0,1,0,3},
    {3,0,1,0,1,0,1,0,1,1,1,0,3},
    {3,0,1,0,0,0,1,0,0,0,0,0,3},
    {3,3,3,3,3,3,3,3,3,3,3,3,3},
  },
  { // Maze 2
    {3,3,3,3,3,3,3,3,3,3,3,3,3},
    {3,0,1,0,0,0,0,0,0,0,0,0,3},
    {3,0,1,0,1,1,1,1,1,0,1,0,3},
    {3,0,1,0,1,2,0,0,1,0,1,0,3},
    {3,0,1,0,1,0,1,1,1,0,1,0,3},
    {3,0,0,0,0,0,1,0,0,0,1,0,3},
    {3,0,1,1,1,1,1,0,1,1,1,0,3},
    {3,0,1,0,1,0,0,0,1,0,0,0,3},
    {3,0,1,0,1,0,1,1,1,1,1,0,3},
    {3,2,1,0,1,0,1,0,0,0,1,0,3},
    {3,0,1,0,1,0,1,0,1,0,1,1,3},
    {3,0,0,0,1,0,0,0,1,0,0,0,3},
    {3,3,3,3,3,3,3,3,3,3,3,3,3},
  },
  { // Maze 3
    {3,3,3,3,3,3,3,3,3,3,3,3,3},
    {3,0,1,0,0,0,1,0,0,2,0,0,3},
    {3,0,1,0,1,0,1,1,1,0,1,0,3},
    {3,0,1,0,1,0,1,0,0,0,1,0,3},
    {3,0,1,0,1,0,1,0,1,1,1,0,3},
    {3,0,0,0,1,0,1,0,1,0,0,0,3},
    {3,0,1,1,1,1,1,0,1,0,1,1,3},
    {3,0,0,0,1,0,0,0,1,0,1,0,3},
    {3,1,1,0,1,0,1,0,1,0,1,0,3},
    {3,0,0,0,1,2,1,0,1,0,0,0,3},
    {3,0,1,1,1,1,1,0,1,1,1,0,3},
    {3,0,0,0,0,0,0,0,1,0,0,0,3},
    {3,3,3,3,3,3,3,3,3,3,3,3,3},
  },
  { // Maze 4
    {3,3,3,3,3,3,3,3,3,3,3,3,3},
    {3,2,0,0,1,0,0,0,0,0,0,0,3},
    {3,0,1,0,1,1,1,1,1,1,1,0,3},
    {3,0,1,0,1,0,0,0,0,0,0,0,3},
    {3,0,1,0,1,0,1,1,1,1,1,0,3},
    {3,0,1,0,0,0,1,0,0,0,1,0,3},
    {3,0,1,1,1,1,1,0,1,1,1,0,3},
    {3,2,1,0,0,0,0,0,0,0,0,0,3},
    {3,0,1,1,1,1,1,1,1,1,1,0,3},
    {3,0,0,0,0,0,0,0,0,0,1,0,3},
    {3,0,1,1,1,1,1,1,1,0,1,0,3},
    {3,0,0,0,0,0,1,0,0,0,1,0,3},
    {3,3,3,3,3,3,3,3,3,3,3,3,3},
  },
  { // Maze 5
    {3,3,3,3,3,3,3,3,3,3,3,3,3},
    {3,0,1,0,0,0,0,2,1,0,0,0,3},
    {3,0,1,0,1,1,1,0,1,0,1,0,3},
    {3,0,0,0,0,0,1,0,0,0,1,0,3},
    {3,0,1,1,1,1,1,1,1,1,1,0,3},
    {3,0,1,0,0,0,0,0,0,0,1,0,3},
    {3,0,1,0,1,1,1,1,1,0,1,0,3},
    {3,0,1,0,0,2,1,0,0,0,0,0,3},
    {3,0,1,1,1,0,1,1,1,1,1,1,3},
    {3,0,1,0,1,0,0,0,0,0,0,0,3},
    {3,0,1,0,1,1,1,1,1,1,1,0,3},
    {3,0,0,0,0,0,0,0,0,0,0,0,3},
    {3,3,3,3,3,3,3,3,3,3,3,3,3},
  },
  { // Maze 6
    {3,3,3,3,3,3,3,3,3,3,3,3,3},
    {3,0,0,2,0,0,0,0,1,0,0,0,3},
    {3,0,1,1,1,1,1,0,1,0,1,0,3},
    {3,0,1,0,0,0,1,0,0,0,1,0,3},
    {3,0,1,0,1,1,1,1,1,1,1,0,3},
    {3,0,0,0,1,0,0,0,1,0,0,0,3},
    {3,1,1,1,1,0,1,1,1,0,1,1,3},
    {3,0,0,0,1,0,0,0,0,0,1,0,3},
    {3,0,1,0,1,0,1,1,1,1,1,0,3},
    {3,0,1,0,1,0,0,0,0,0,1,0,3},
    {3,0,1,1,1,1,1,1,1,0,1,0,3},
    {3,0,0,2,0,0,0,0,0,0,0,0,3},
    {3,3,3,3,3,3,3,3,3,3,3,3,3},
  },
  { // Maze 7
    {3,3,3,3,3,3,3,3,3,3,3,3,3},
    {3,0,0,0,0,0,0,0,0,0,0,0,3},
    {3,1,1,1,1,1,1,1,1,0,1,0,3},
    {3,0,0,0,0,0,0,0,0,0,1,0,3},
    {3,0,1,1,1,1,1,0,1,1,1,1,3},
    {3,0,0,0,1,0,0,0,1,2,0,0,3},
    {3,0,1,0,1,1,1,1,1,0,1,0,3},
    {3,0,1,0,0,0,0,0,1,0,1,0,3},
    {3,0,1,1,1,1,1,0,1,1,1,0,3},
    {3,0,1,0,0,0,0,0,0,0,1,0,3},
    {3,0,1,0,1,1,1,1,1,1,1,0,3},
    {3,0,1,0,0,0,0,2,0,0,0,0,3},
    {3,3,3,3,3,3,3,3,3,3,3,3,3},
  },
  { // Maze 8
    {3,3,3,3,3,3,3,3,3,3,3,3,3},
    {3,0,0,0,0,0,1,0,1,0,0,0,3},
    {3,0,1,1,1,0,1,0,1,0,1,0,3},
    {3,0,1,0,1,0,1,0,0,0,1,0,3},
    {3,1,1,0,1,0,1,1,1,1,1,0,3},
    {3,0,0,0,1,0,1,0,0,0,1,0,3},
    {3,0,1,0,1,0,1,0,1,0,1,0,3},
    {3,0,1,0,1,0,1,2,1,0,1,2,3},
    {3,0,1,0,1,0,1,0,1,0,1,0,3},
    {3,0,1,0,0,0,1,0,1,0,1,0,3},
    {3,0,1,1,1,1,1,0,1,0,1,0,3},
    {3,0,0,0,0,0,0,0,1,0,0,0,3},
    {3,3,3,3,3,3,3,3,3,3,3,3,3},
  }
};

// Active maze in RAM (only one copy at a time)
byte maze[mazeSize][mazeSize];

// Player and goal
int playerX = 1, playerY = 1;   // start pos in matrix
int prevX, prevY;
int goalX = 11, goalY = 11;     // example goal
int selectedMaze = 0;

int startX = 1, startY = 1;     // start cell
unsigned long lastMoveTime = 0;

// Blink vars for player on identifier
unsigned long blinkPrevMillis = 0;
bool blinkState = false;
const long blinkIntervalPlayer = 500;

// Std LEDs
const int ResetBt_pin = 2;
const int RedLED_pin = 3;
const int GreenLED_pin = 4;
const int BlueLED_pin = 5;

// LED Variables
unsigned long previousMillis = 0;
const long blinkInterval = 500;
bool ledState = LOW;

// State machine vars
int M_State = 0;
bool Mod_Info_Ok = false; // Debug on true / Normal on false
volatile bool Start_Signal = false;
volatile bool Mistake_Ack  = false;
volatile bool Game_Lost    = false;
volatile bool Game_Won     = false;

// Function interaction
bool Device_Ready = false;
bool Mistake_Made = false;
bool Module_Solved = false;

// Handshake: mark when we have received at least one KV from master
volatile uint8_t cfgMask = 0;
const uint8_t BIT_ANY_KV = 0x80;          // “saw any KV from master”
const uint8_t REQUIRED_MASK = BIT_ANY_KV;  // For Maze, this is all we need



// --------------------------------------------------
void setup() {
  pinMode(ResetBt_pin, INPUT_PULLUP);
  pinMode(RedLED_pin, OUTPUT);
  pinMode(GreenLED_pin, OUTPUT);
  pinMode(BlueLED_pin, OUTPUT);

  pinMode(btnUp, INPUT_PULLUP);
  pinMode(btnDown, INPUT_PULLUP);
  pinMode(btnLeft, INPUT_PULLUP);
  pinMode(btnRight, INPUT_PULLUP);
  randomSeed(analogRead(A0)); // ensure randomness

  //I2C & Serial setup
  Wire.begin(I2C_ADDR_MAZE);
  Wire.onReceive(onI2CReceive);
  Wire.onRequest(onI2CRequest);

  //Serial.begin(9600);

  strip.begin();
  strip.clear();
  strip.show();
}

// --------------------------------------------------
void loop() {
  //Serial.println(M_State);

  // Game state machine
  switch (M_State) {
    case 0: M_State_0(); break;
    case 1: M_State_1(); break;
    case 2: M_State_2(); break;
    case 3: M_State_3(); break;
    case 4: M_State_4(); break;
    case 5: M_State_5(); break;
  }
}

// ============ I2C HANDLERS ============
void onI2CReceive(int howMany) {
  if (howMany < 2) return;

  uint8_t key = Wire.read();
  uint8_t len = Wire.read();

  // read payload
  uint8_t buf[8];
  for (uint8_t i=0; i<len && Wire.available(); i++) buf[i] = Wire.read();

  switch (key) {
    case K_START_SIGNAL:
      if (len>=1 && buf[0]==1) {
        Start_Signal = true;
        // Your FSM should react by moving to M_State=3 in your code
        //Serial.println(F("[I2C] START_SIGNAL=1"));
      }
      // Also mark info OK (master talked to us)
      Mod_Info_Ok = true;
      break;

    case K_MISTAKE_ACK:
      Mistake_Ack = true; // your M_State_4 handler should see this and return to M_State_3
      //Serial.println(F("[I2C] MISTAKE_ACK"));
      break;

    case K_GAME_LOST:
      Game_Lost = true;   // you can switch to a terminal state in your FSM
      //Serial.println(F("[I2C] GAME_LOST"));
      break;

    default:
      // keys not used by Maze => ignore
      break;
  }
    // >>> Handshake: any KV received counts
  cfgMask |= BIT_ANY_KV;
  Mod_Info_Ok = ((cfgMask & REQUIRED_MASK) == REQUIRED_MASK);
}

void onI2CRequest() {
  // Always return current M_State as a single byte
  Wire.write((uint8_t)M_State);
}

void M_State_0() { // Awaiting Setup (module opt-in)
  if (digitalRead(ResetBt_pin) == LOW) {
    delay(50);
    if (digitalRead(ResetBt_pin) == LOW) {
      // entering state 1: clean slate
      Device_Ready  = false;
      Mod_Info_Ok   = false;
      Start_Signal  = false;
      Mistake_Ack   = false;
      Game_Lost     = false;
      Game_Won      = false;

      M_State = 1;
    }
  }
  // LED: solid blue (module idle / not selected)
  digitalWrite(RedLED_pin,   HIGH);
  digitalWrite(GreenLED_pin, HIGH);
  digitalWrite(BlueLED_pin,  LOW);
}
void M_State_1() { // Checking readiness
  if (!Device_Ready) {        // run once
    Precheck();
    drawMaze();
    Device_Ready = true;
  }

  // Stay in state 1 advertising "ready" until we receive any K/V
  if (Mod_Info_Ok) {
    M_State = 2;
  }

  // LED: one blink
  digitalWrite(RedLED_pin, Device_Ready);
  digitalWrite(GreenLED_pin, !Device_Ready);
  digitalWrite(BlueLED_pin, HIGH);
  delay(blinkInterval);
}

void M_State_2() {
  //Current step
  //Start_Signal received form Master
  //Next step
  if (Start_Signal) { M_State = 3; }
  //LED Control : Clignote Bleu
  unsigned long currentMillis = millis();
  if (currentMillis - previousMillis >= blinkInterval) {
    previousMillis = currentMillis;
    ledState = !ledState;
    digitalWrite(RedLED_pin, HIGH);
    digitalWrite(GreenLED_pin, HIGH);
    digitalWrite(BlueLED_pin, ledState);
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
void M_State_4() {
  //Current step
   //Mistake_Ack received form Master
  //Next step
  if (!Game_Lost && Mistake_Ack) {
    M_State = 3;
    Mistake_Ack = false;
  }
  digitalWrite(RedLED_pin, LOW);
  digitalWrite(GreenLED_pin, HIGH);
  digitalWrite(BlueLED_pin, HIGH);
  //Delay ??
}
void M_State_5() {
  //Current step
  //...
  //Next step
  //NA
  //LED Control : Vert Continu
  digitalWrite(RedLED_pin, HIGH);
  digitalWrite(GreenLED_pin, LOW);
  digitalWrite(BlueLED_pin, HIGH);
}

// -------- Logic --------
void Precheck() {
  // Pick a random maze (0–8)
  selectedMaze = random(0, 9);

 
  // Copy chosen maze into active maze
  for (int y = 0; y < mazeSize; y++) {
    for (int x = 0; x < mazeSize; x++) {
      maze[y][x] = pgm_read_byte(&mazes[selectedMaze][y][x]);
    }
  }

  // Collect all empty cells (value 0)
  struct Cell { int x, y; };
  Cell emptyCells[mazeSize * mazeSize];
  int emptyCount = 0;

 // Only consider odd/odd nodes (playable intersections)
  for (int y = 1; y < mazeSize; y += 2) {
    for (int x = 1; x < mazeSize; x += 2) {
      if (maze[y][x] == 0) {
        emptyCells[emptyCount++] = {x, y};
      }
    }
  }

  // Pick random start
  int startIndex = random(0, emptyCount);
  startX = emptyCells[startIndex].x;
  startY = emptyCells[startIndex].y;

  // Remove start from list
  for (int i = startIndex; i < emptyCount - 1; i++) {
    emptyCells[i] = emptyCells[i + 1];
  }
  emptyCount--;

  // Pick random goal from remaining
  int goalIndex;
do {
  goalIndex = random(0, emptyCount);
  goalX = emptyCells[goalIndex].x;
  goalY = emptyCells[goalIndex].y;
} while (goalX == startX && goalY == startY);

  // Initialize player at start
  playerX = startX;
  playerY = startY;

  // Reset game state
  Mistake_Made = false;
  Module_Solved = false;
  lastMoveTime = millis();
  Device_Ready = true;
}

void GameLoop() {
  ReadBt();
  drawMaze();
}

// Button reading
void ReadBt() {
  static uint8_t upLast=HIGH, downLast=HIGH, leftLast=HIGH, rightLast=HIGH;
  uint8_t upNow    = digitalRead(btnUp);
  uint8_t downNow  = digitalRead(btnDown);
  uint8_t leftNow  = digitalRead(btnLeft);
  uint8_t rightNow = digitalRead(btnRight);

  // press = HIGH->LOW with INPUT_PULLUP
  if (upLast==HIGH    && upNow==LOW)    tryMove(0, -2);
  if (downLast==HIGH  && downNow==LOW)  tryMove(0,  2);
  if (leftLast==HIGH  && leftNow==LOW)  tryMove(-2, 0);
  if (rightLast==HIGH && rightNow==LOW) tryMove( 2, 0);

  upLast = upNow; downLast = downNow; leftLast = leftNow; rightLast = rightNow;
}

// Try move
void tryMove(int dx, int dy) {
  int nx = playerX + dx;
  int ny = playerY + dy;

 // Bounds guard
  if (nx < 0 || nx >= mazeSize || ny < 0 || ny >= mazeSize) {
    return; // ignore out-of-bounds
  }


  // Mid cell between current and target (where walls/rim are checked)
  int mx = (playerX + nx) / 2;
  int my = (playerY + ny) / 2;

// Rim mid → ignore; Wall mid → mistake
  if (maze[my][mx] == 3) {
    return; // Outside rim → ignore move (stay in place)
  }
  if (maze[my][mx] == 1) {
    // Hit a wall → mistake
    Mistake_Made = true;
    return;
  }

  // Target rim? ignore
  if (maze[ny][nx] == 3) {
    return;
  }

  // Valid move (empty or identifier)
  playerX = nx;
  playerY = ny;

  // Check if goal reached
  if (playerX == goalX && playerY == goalY) {
    Module_Solved = true;
  }
}

// Draw Maze state
void drawMaze() {
  strip.clear();
  //First draw orange border
  uint32_t orange = strip.Color(255, 80, 0);  // light border
  for (uint8_t i = 0; i < 8; i++) {
    strip.setPixelColor(mapXY8(i, 0), orange); // top
    strip.setPixelColor(mapXY8(i, 7), orange); // bottom
    strip.setPixelColor(mapXY8(0, i), orange); // left
    strip.setPixelColor(mapXY8(7, i), orange); // right
  } 

  //Then draw maze
  unsigned long currentMillis = millis();
  if (currentMillis - blinkPrevMillis >= blinkIntervalPlayer) {
    blinkPrevMillis = currentMillis;
    blinkState = !blinkState;
  }

  bool onIdentifier = (maze[playerY][playerX] == 2);
  int playerIdx = xyToIndex(playerX, playerY);

  if (onIdentifier) {
    if (blinkState) strip.setPixelColor(playerIdx, strip.Color(255,255,255));
    else strip.setPixelColor(playerIdx, strip.Color(0,255,0));
  } else {
    strip.setPixelColor(playerIdx, strip.Color(255,255,255));
  }

  int goalIdx = xyToIndex(goalX, goalY);
  strip.setPixelColor(goalIdx, strip.Color(255,0,0));

  for (int y=0; y<mazeSize; y++) {
    for (int x=0; x<mazeSize; x++) {
      if (maze[y][x] == 2) {
        int idx = xyToIndex(x, y);
        if (!(x == playerX && y == playerY)) {
          strip.setPixelColor(idx, strip.Color(0,255,0));
        }
      }
    }
  }
  strip.show();
}

int xyToIndex(int x, int y) {
  // project 13x13 maze coords onto 6x6 playable grid inside 8x8
  int gx = (x - 1) / 2 + 1;  // 0..5
  int gy = (y - 1) / 2 + 1;  // 0..5
  // keep using the inner 6x6; indices 6 and 7 are your frame
  return mapXY8((uint8_t)gx, (uint8_t)gy);
}

// map 0..7 x/y (bottom-left origin) to physical NeoPixel index
static inline uint16_t mapXY8(uint8_t x, uint8_t y){
  uint8_t yTop = 7 - y;                 // convert to top-left origin
  return (yTop % 2 == 0) ? (yTop * 8 + x)
                         : (yTop * 8 + (7 - x));
}