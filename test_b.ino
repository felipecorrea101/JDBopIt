/*
  UNO TEST SKETCH B - Hold Button, Color Code, Launch Dial, Toggle Switch, Switch Bank
  For the Adafruit 1447 16x2 LCD (parallel HD44780, NOT I2C).
  Same game logic as the Mega version, trimmed to fit an Arduino Uno.
  Open the Serial Monitor at 9600 baud to see debug info.

  Libraries: LiquidCrystal (built in, nothing to install)

  NOTE: to save pins, the Hold task and the Color Code task use the SAME four
  physical buttons here. On the Mega they are separate sets of buttons.

  PIN MAP (Arduino Uno)
  -------------------------------------------------
  LCD pin  1 (VSS)  -> GND
  LCD pin  2 (VDD)  -> 5V
  LCD pin  3 (V0)   -> middle leg of contrast pot (pot outer legs to 5V and GND)
  LCD pin  4 (RS)   -> D13
  LCD pin  5 (RW)   -> GND
  LCD pin  6 (E)    -> A0
  LCD pins 7-10     -> not connected
  LCD pin 11 (D4)   -> A1
  LCD pin 12 (D5)   -> A2
  LCD pin 13 (D6)   -> A3
  LCD pin 14 (D7)   -> A4
  LCD pin 15 (LED+) -> 5V
  LCD pin 16 (LED-) -> GND

  Buttons Red, Green, Blue, Yellow: D2, D3, D4, D5
  Single toggle switch:             D6
  Switch bank (4 toggles):          D7, D8, D9, D10
  Dial (potentiometer):             A5  (outer legs to 5V and GND, middle leg to A5)

  Buttons and switches wire between the pin and GND (INPUT_PULLUP).
  After GAME OVER, press the RED button to restart.
*/

#include <LiquidCrystal.h>
#include <EEPROM.h>

// ---- debug output to Serial Monitor (set to 0 to turn off) ----
#define DEBUG 1
#if DEBUG
  #define LOG(...)   Serial.print(__VA_ARGS__)
  #define LOGLN(...) Serial.println(__VA_ARGS__)
#else
  #define LOG(...)   do {} while (0)
  #define LOGLN(...) do {} while (0)
#endif

// ======================= HARDWARE =======================
//                 RS, E,  D4, D5, D6, D7
LiquidCrystal lcd1(13, A0, A1, A2, A3, A4);   // Player 1 screen

const uint8_t DIAL_PIN   = A5;
const uint8_t SWITCH_PIN = 6;
const uint8_t BANK_PINS[4] = {7, 8, 9, 10};

// ======================= DEBOUNCED BUTTON =======================
struct Button {
  uint8_t pin;
  bool stable = HIGH, lastRead = HIGH, pressedEvt = false, releasedEvt = false;
  unsigned long changed = 0, pressTime = 0;
  Button(uint8_t p) : pin(p) {}

  void begin() {
    pinMode(pin, INPUT_PULLUP);
  }

  void update() {
    bool r = digitalRead(pin);
    if (r != lastRead) { lastRead = r; changed = millis(); }
    if (millis() - changed > 15 && r != stable) {
      stable = r;
      if (stable == LOW) { pressedEvt = true; pressTime = changed; }
      else releasedEvt = true;
    }
  }

  bool wasPressed()  {
    bool e = pressedEvt;  pressedEvt = false;  return e;
  }

  bool wasReleased() {
    bool e = releasedEvt; releasedEvt = false; return e;
  }

  void clear() {
    pressedEvt = releasedEvt = false;
  }
};

// Same four physical buttons used for both the Hold and Color Code tasks
Button holdBtn[4]  = {Button(2), Button(3), Button(4), Button(5)};
Button colorBtn[4] = {Button(2), Button(3), Button(4), Button(5)};
Button startBtn(11);
const char COLORS[4] = {'R', 'G', 'B', 'Y'};
const char* COLOR_NAMES[4] = {"RED", "GREEN", "BLUE", "YELLOW"};

// ======================= GAME STATE =======================
enum Task { T_HOLD, T_COLOR, T_DIAL, T_BANK, NUM_TASKS };
enum Phase { WAITING, PLAYING, RESULT, GAMEOVER };

Phase phase = WAITING;
Task current, lastTask = NUM_TASKS;
unsigned long taskStart, taskLimit, resultUntil;
int score = 0, lives = 3;

// per-task variables
char code[5], entry[5];
byte entryLen;
bool holding, stopShown;
unsigned long holdStart, holdStopAt, stopShownAt;
int holdColor;
int dialTarget, lastZone, lastTarget;
bool inZone;
unsigned long zoneStart;
bool switchTarget;
int lastSwitch;
byte bankTarget, lastBank, lastBankTarget = 255;

// tuning
const unsigned long HOLD_REACT = 2500;   // ms P1 has to release after RELEASE appears
const unsigned long DIAL_HOLD  = 1000;   // ms dial must stay in zone
const char* ZONES[2] = {"LEFT", "RIGHT"};

// ======================= HELPERS =======================
void show(LiquidCrystal &l, const char* a, const char* b = "") {
  char buf[17];
  snprintf(buf, sizeof(buf), "%-16s", a); l.setCursor(0, 0); l.print(buf);
  snprintf(buf, sizeof(buf), "%-16s", b); l.setCursor(0, 1); l.print(buf);
}

const int DIAL_LEFT_MAX  = 100;    // reading at or below this = all the way left
const int DIAL_RIGHT_MIN = 923;    // reading at or above this = all the way right

int zoneOf(int v) {
  if (v <= DIAL_LEFT_MAX)  return 0;   // LEFT
  if (v >= DIAL_RIGHT_MIN) return 1;   // RIGHT
  return -1;                           // anywhere in between
}

byte readBank() {
  byte v = 0;
  for (int i = 0; i < 4; i++) if (digitalRead(BANK_PINS[i]) == LOW) v |= (1 << i);
  return v;
}

void clearAllEvents() {
  for (int i = 0; i < 4; i++) {
    colorBtn[i].clear();
    holdBtn[i].clear();
  }
  startBtn.clear();
}

// Seed from a counter stored in EEPROM, so every power-up differs
// (no floating pin needed).
void seedRandom() {
  unsigned long n;
  EEPROM.get(0, n);
  n++;
  EEPROM.put(0, n);
  randomSeed(n * 2654435761UL ^ micros());
}

// ======================= TASK SETUP =======================
void startTask(Task t) {
  current = t;
  taskStart = millis();
  // time limit starts at 15 seconds and ends at 7 seconds
  taskLimit = max(7000L, 15000L - (long)score * 80L);
  entryLen = 0;
  memset(entry, 0, sizeof(entry));
  clearAllEvents();
  char buf[17];

  switch (t) {
    case T_HOLD:
      holding = false;
      stopShown = false;
      holdColor = random(4);
      show(lcd1, "HOLD BUTTON:", COLOR_NAMES[holdColor]);
      LOG("TASK: hold "); LOGLN(COLOR_NAMES[holdColor]);
      break;

    case T_COLOR:
      for (int i = 0; i < 4; i++) code[i] = COLORS[random(4)];
      code[4] = 0;
      snprintf(buf, sizeof(buf), "%c %c %c %c", code[0], code[1], code[2], code[3]);
      show(lcd1, "COLOR CODE:", buf);
      LOG("TASK: color code = "); LOGLN(code);
      break;

    case T_DIAL:
      do {
        dialTarget = random(2);
      } while (dialTarget == lastTarget);
      lastTarget = dialTarget;
      inZone = false;
      lastZone = -1;
      show(lcd1, "SET LAUNCH DIAL:", ZONES[dialTarget]);
      LOG("TASK: dial target = "); LOGLN(ZONES[dialTarget]);
      break;

    case T_BANK:
      do {
        bankTarget = random(16);
      } while (bankTarget == readBank() || bankTarget == lastBankTarget);
      lastBankTarget = bankTarget;           // remember it for next time
      lastBank = 255;
      snprintf(buf, sizeof(buf), "%d %d %d %d",
              bankTarget & 1, (bankTarget >> 1) & 1,
              (bankTarget >> 2) & 1, (bankTarget >> 3) & 1);
      show(lcd1, "SWITCH CODE:", buf);
      LOG("TASK: bank target = "); LOGLN(buf);
      break;

    default: break;
  }
}

void startRandomTask() {
  Task t;
  do {
    t = (Task)random(NUM_TASKS);
  } while (t == lastTask);

  lastTask = t;
  phase = PLAYING;
  startTask(t);
}

void endTask(bool success) {
  char buf[17];
  if (success) {
    score++;
    snprintf(buf, sizeof(buf), "Score: %d", score);
    show(lcd1, "SUCCESS!", buf);
    LOGLN("RESULT: success");
  } else {
    lives--;
    snprintf(buf, sizeof(buf), "Lives: %d", lives);
    show(lcd1, "FAILED!", buf);
    LOGLN("RESULT: failed");
  }
  phase = RESULT;
  resultUntil = millis() + 1500;
}

void gameOver() {
  char buf[17];
  snprintf(buf, sizeof(buf), "Score:%d RED=go", score);
  show(lcd1, "GAME OVER", buf);
  clearAllEvents();                      // ignore presses made during the result screen
  phase = GAMEOVER;
  LOGLN("GAME OVER");
}

// ======================= TASK LOGIC =======================
void runTask() {

  switch (current) {

    case T_HOLD:
      // P1 hasn't started holding yet
      if (!holding) {
        for (int i = 0; i < 4; i++) {
          if (holdBtn[i].wasPressed()) {
            if (i == holdColor) {
              holding = true;
              stopShown = false;
              holdStart = holdBtn[i].pressTime;
              holdBtn[i].clear();                    // drop any stale release event
              holdStopAt = random(3000, 8001);       // random 3-8 s
              show(lcd1, "HOLD BUTTON:", COLOR_NAMES[holdColor]);
              LOG("Holding, release prompt in (ms): "); LOGLN(holdStopAt);
            } else {
              endTask(false);
              break;
            }
          }
        }
      }

      if (holding) {
        // random time reached: tell P1 to release
        if (!stopShown && millis() - holdStart >= holdStopAt) {
          stopShown = true;
          stopShownAt = millis();
          show(lcd1, "RELEASE BUTTON:", COLOR_NAMES[holdColor]);
          LOGLN("RELEASE prompt shown");
        }

        // P1 never let go in time
        if (stopShown && millis() - stopShownAt > HOLD_REACT) {
          holding = false;
          endTask(false);
          return;
        }

        // P1 released
        if (holdBtn[holdColor].wasReleased()) {
          holding = false;
          if (stopShown) {
            endTask(true);
            return;
          }
          LOGLN("Released too early");
          endTask(false);
          return;
        }
      }
      break;

    case T_COLOR:
      for (int i = 0; i < 4; i++) {
        if (colorBtn[i].wasPressed() && entryLen < 4) {
          entry[entryLen++] = COLORS[i];
          LOG("Pressed: "); LOGLN(COLORS[i]);
        }
      }
      if (entryLen == 4) {
        if (strncmp(entry, code, 4) == 0) {
          endTask(true);
          return;
        }
        endTask(false);                  // wrong code: lose a life
        return;
      }
      break;

    case T_DIAL: {
      int z = zoneOf(analogRead(DIAL_PIN));
      if (z != lastZone) {
        lastZone = z;
        LOG("Dial zone: "); LOGLN(ZONES[z]);
      }
      if (z == dialTarget) {
        if (!inZone) { inZone = true; zoneStart = millis(); }
        else if (millis() - zoneStart >= DIAL_HOLD) {
          endTask(true);
          return;
        }
      } else {
        inZone = false;
      }
      break;
    }

    case T_BANK: {
      byte b = readBank();
      if (b != lastBank) {
        lastBank = b;
        LOG("Bank (switch 4..1): "); LOGLN(b, BIN);
      }
      if (b == bankTarget) {
        endTask(true);
        return;
      }
    }

    default: break;
  }

  // Timeout (paused while P1 is mid-hold)
  if (!(current == T_HOLD && holding) && millis() - taskStart > taskLimit) {
    LOGLN("Task timed out");
    endTask(false);
  }
}

// ======================= ARDUINO ENTRY POINTS =======================
void setup() {
#if DEBUG
  Serial.begin(9600);
#endif
  lcd1.begin(16, 2);

  for (int i = 0; i < 4; i++) { holdBtn[i].begin(); colorBtn[i].begin(); }
  pinMode(SWITCH_PIN, INPUT_PULLUP);
  for (int i = 0; i < 4; i++) pinMode(BANK_PINS[i], INPUT_PULLUP);
  startBtn.begin();

  seedRandom();

  show(lcd1, "TEST B", "Press START");
  LOGLN("Waiting for START button");
}

void loop() {
  for (int i = 0; i < 4; i++) {
    holdBtn[i].update();
    colorBtn[i].update();
  }
  startBtn.update();

  switch (phase) {
    case WAITING:
      if (startBtn.wasPressed()) {
        score = 0; lives = 3; lastTask = NUM_TASKS;
        LOGLN("START pressed");
        startRandomTask();
      }
      break;

    case PLAYING:
      runTask();
      break;

    case RESULT:
      if (millis() >= resultUntil) {
        if (lives <= 0) gameOver();
        else startRandomTask();
      }
      break;

    case GAMEOVER:
      if (startBtn.wasPressed()) {       // START button restarts
        score = 0; lives = 3; lastTask = NUM_TASKS;
        startRandomTask();
      }
      break;
  }
}
