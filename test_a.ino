/*
  UNO TEST SKETCH A - Password + Synchronized Press
  For the Adafruit 1447 16x2 LCD (parallel HD44780, NOT I2C).
  Same game logic as the Mega version, trimmed to fit an Arduino Uno.
  Open the Serial Monitor at 9600 baud to see debug info.

  Libraries: LiquidCrystal (built in, nothing to install) and
             Keypad (Library Manager, by Mark Stanley / Alexander Brevig)

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

  Keypad (Adafruit phone-style, header pins left to right):
    pins 1-3 = columns 1-3 -> D6, D7, D8
    pins 4-7 = rows 1-4    -> D2, D3, D4, D5
  P1 sync button: D9
  P2 sync button: D10
  P1 sync LED:    D11   (220 ohm resistor to LED, LED to GND)
  P2 sync LED:    D12   (220 ohm resistor to LED, LED to GND)

  Buttons wire between the pin and GND (INPUT_PULLUP).
*/

#include <LiquidCrystal.h>
#include <Keypad.h>
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

const byte ROWS = 4, COLS = 3;
char keys[ROWS][COLS] = {
  {'1', '2', '3'},
  {'4', '5', '6'},
  {'7', '8', '9'},
  {'*', '0', '#'}
};
byte rowPins[ROWS] = {2, 3, 4, 5};
byte colPins[COLS] = {6, 7, 8};
Keypad keypad = Keypad(makeKeymap(keys), rowPins, colPins, ROWS, COLS);

const uint8_t P1_SYNC_LED = 11;
const uint8_t P2_SYNC_LED = 12;

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

Button p1Sync(9), p2Sync(10);

// ======================= GAME STATE =======================
enum Task { T_PASSWORD, T_SYNC, NUM_TASKS };
enum Phase { PLAYING, RESULT, GAMEOVER };

Phase phase = PLAYING;
Task current, lastTask = NUM_TASKS;
unsigned long taskStart, taskLimit, resultUntil;
int score = 0, lives = 3;

// per-task variables
char pw[5], entry[5];
byte entryLen;
bool pwHidden;
bool hasP1, hasP2;
unsigned long t1, t2;

// tuning
const unsigned long SYNC_WINDOW = 250;    // ms between the two presses
const unsigned long PW_VISIBLE  = 4000;   // ms password stays visible

// ======================= HELPERS =======================
void show(LiquidCrystal &l, const char* a, const char* b = "") {
  char buf[17];
  snprintf(buf, sizeof(buf), "%-16s", a); l.setCursor(0, 0); l.print(buf);
  snprintf(buf, sizeof(buf), "%-16s", b); l.setCursor(0, 1); l.print(buf);
}

void clearAllEvents() {
  p1Sync.clear();
  p2Sync.clear();
}

void syncLeds(bool on) {
  digitalWrite(P1_SYNC_LED, on);
  digitalWrite(P2_SYNC_LED, on);
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
  syncLeds(false);
  taskStart = millis();
  // time limit starts at 15 seconds and ends at 7 seconds
  taskLimit = max(7000L, 15000L - (long)score * 80L);
  entryLen = 0;
  memset(entry, 0, sizeof(entry));
  clearAllEvents();

  switch (t) {
    case T_PASSWORD:
      for (int i = 0; i < 4; i++) pw[i] = '0' + random(10);
      pw[4] = 0;
      pwHidden = false;
      show(lcd1, "PASSWORD:", pw);
      LOG("TASK: password = "); LOGLN(pw);
      break;

    case T_SYNC:
      hasP1 = hasP2 = false;
      show(lcd1, "", "");                // blank screen, the LEDs are the cue
      LOGLN("TASK: sync press");
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
  syncLeds(false);
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
  snprintf(buf, sizeof(buf), "Score:%d SYNC=go", score);
  show(lcd1, "GAME OVER", buf);
  clearAllEvents();                      // ignore presses made during the result screen
  phase = GAMEOVER;
  LOGLN("GAME OVER");
}

// ======================= TASK LOGIC =======================
void runTask(char key) {

  switch (current) {

    case T_PASSWORD:
      if (!pwHidden && millis() - taskStart > PW_VISIBLE) {
        pwHidden = true;
        show(lcd1, "PASSWORD:", "****");
      }
      if (key >= '0' && key <= '9' && entryLen < 4) {
        entry[entryLen++] = key;
        LOG("Key: "); LOGLN(key);
      }
      if (entryLen >= 4) {
        if (strncmp(entry, pw, 4) == 0) {
          endTask(true);
          return;
        }
        endTask(false);
        return;
      }
      break;

    case T_SYNC:
      syncLeds((millis() / 250) % 2 == 0);   // blink every 250 ms, no delay()

      if (p1Sync.wasPressed()) { hasP1 = true; t1 = p1Sync.pressTime; LOGLN("P1 pressed"); }
      if (p2Sync.wasPressed()) { hasP2 = true; t2 = p2Sync.pressTime; LOGLN("P2 pressed"); }

      if (hasP1 && hasP2) {
        long diff = (long)t1 - (long)t2;
        LOG("Sync difference (ms): "); LOGLN(diff);
        if (abs(diff) <= (long)SYNC_WINDOW) {
          endTask(true);
          return;
        }
        hasP1 = hasP2 = false;           // not in sync: try again
      } else if ((hasP1 || hasP2) &&
                 millis() - (hasP1 ? t1 : t2) > SYNC_WINDOW) {
        LOGLN("Other player too slow, reset");
        hasP1 = hasP2 = false;
      }
      break;

    default: break;
  }

  // Timeout
  if (millis() - taskStart > taskLimit) {
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

  pinMode(P1_SYNC_LED, OUTPUT);
  pinMode(P2_SYNC_LED, OUTPUT);
  syncLeds(false);

  p1Sync.begin();
  p2Sync.begin();

  seedRandom();

  show(lcd1, "TEST A", "Get ready...");
  delay(2000);
  startRandomTask();
}

void loop() {
  p1Sync.update();
  p2Sync.update();
  char key = keypad.getKey();

  switch (phase) {
    case PLAYING:
      runTask(key);
      break;

    case RESULT:
      if (millis() >= resultUntil) {
        if (lives <= 0) gameOver();
        else startRandomTask();
      }
      break;

    case GAMEOVER:
      if (p1Sync.wasPressed()) {
        score = 0; lives = 3; lastTask = NUM_TASKS;
        startRandomTask();
      }
      break;
  }
}
