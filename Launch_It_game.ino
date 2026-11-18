/*
  LAUNCH IT!  -  two-player game sketch
  Board: Arduino Mega 2560 (Tools > Board > Arduino Mega or Mega 2560)
  Libraries to install (Library Manager):
    - Keypad (Mark Stanley / Alexander Brevig)
    - Adafruit LED Backpack  (also install Adafruit GFX Library and Adafruit BusIO if asked)
    - LiquidCrystal and EEPROM are built in
  Serial Monitor: 9600 baud (prints every task, answer and failure reason)

  HOW THE GAME WORKS (follows our pseudocode)
    - Press START. Every round, each player gets a task to PERFORM, and the
      instruction for it is shown on the OTHER player's screen. The other player
      reads/says it. (Player 1's task shows on Player 2's screen and vice versa.)
    - The game is a series of STAGES: one task per player. Both players act at the same time.
      A new stage only starts when BOTH have finished. Each stage = +1 point per player
      (+2 total) and the stage time limit shrinks (starts at 15 s). Any wrong action or timeout = 1 strike.
    - 3 strikes and you are out. Reach WIN_SCORE (100 points) and the rocket launches (MISSION COMPLETE).
    - Task types: PASSWORD (keypad), SYNC press, HOLD a color, COLOR code,
      launch DIAL, SWITCH code. A different sound plays for each task type.
    - COLOR letters: R = Red, G = Green, B = Black, Y = Yellow.
    - SWITCH code: 1 = toggle in the "on" position (the position that connects the
      pin to GND), 0 = other position. Check which lever direction that is on the hat.

  PIN MAP (same as the pinout CSV / Altium schematic)
    LCD P1: RS22 E23 D4-D7 = 24-27     LCD P2: RS28 E29 D4-D7 = 30-33
    Keypad P1: cols 34,35,36  rows 37,38,39,40
    Keypad P2: cols 41,42,43  rows 44,45,46,47
    Color buttons (Red,Green,Black,Yellow): P1 = D2-D5, P2 = D6-D9
    SYNC button = the RED color button (P1 D2, P2 D6). The old white sync buttons (D10/D11) are unused.
    Sync LEDs = the light in the RED button: P1 D48, P2 D49 (blinks during a SYNC task)
    Start button D12, start LED D52, speaker D13
    Dial pots: P1 A0, P2 A1
    Toggles: P1 A2-A6, P2 A7-A11  (switch to GND, INPUT_PULLUP, on = LOW)
    Bargraphs: I2C SDA D20, SCL D21
*/

#include <LiquidCrystal.h>
#include <Keypad.h>
#include <EEPROM.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_LEDBackpack.h>

// ======================= SETTINGS (change these to tune the game) =======================
#define DEBUG 1            // 1 = print to Serial Monitor
#define USE_BARGRAPH 1     // 0 = ignore the bargraphs
#define DIAL_REVERSED 0    // 1 = swap LEFT and RIGHT if the dial reads backwards
#define TEST_TASK -1       // -1 = normal game. 0 PASSWORD, 1 SYNC, 2 HOLD, 3 COLOR, 4 DIAL, 5 SWITCH
                           // = test mode: only that task, and a failure just retries (no game over)

const int  WIN_SCORE = 100;              // total points (2 per stage, so 50 stages)
const int  MAX_STRIKES = 3;             // 3 strikes and you are out
const long TIME_START = 15000;            // ms allowed per stage at the start
const long TIME_MIN   = 4000;             // shortest time allowed per stage
const long TIME_STEP  = 225;              // ms removed after every cleared stage (15 s -> 4 s by stage 50)
const unsigned long PW_VISIBLE   = 4000;  // password stays visible this long
const unsigned long SYNC_WINDOW  = 250;   // both sync presses within this many ms
const unsigned long HOLD_MIN_MS  = 3000;  // hold task: earliest RELEASE prompt
const unsigned long HOLD_MAX_MS  = 8000;  // hold task: latest RELEASE prompt
const unsigned long HOLD_REACT   = 2500;  // ms to let go after the RELEASE prompt
const unsigned long DIAL_HOLD    = 1000;  // dial must stay in the zone this long
const int DIAL_LEFT_MAX  = 100;           // reading at or below = LEFT
const int DIAL_RIGHT_MIN = 923;           // reading at or above = RIGHT
const unsigned long READY_MS        = 1500;
const unsigned long RESULT_MS       = 900;
const unsigned long RESTART_LOCK_MS = 1500;
const uint8_t BAR_ADDR_1 = 0x70;          // if you bridged the A0 jumper on one
const uint8_t BAR_ADDR_2 = 0x70;          // bargraph, set that one to 0x71

#if DEBUG
  #define LOG(...)   Serial.print(__VA_ARGS__)
  #define LOGLN(...) Serial.println(__VA_ARGS__)
#else
  #define LOG(...)   do {} while (0)
  #define LOGLN(...) do {} while (0)
#endif

// Types first: the Arduino IDE puts auto-made function prototypes above
// the first function, so any type used in a function signature must come before it.
enum Task : uint8_t { T_PASSWORD, T_SYNC, T_HOLD, T_COLOR, T_DIAL, T_SWITCH, NUM_TASKS };
enum Phase : uint8_t { WAITING, READY, PLAYING, RESULT, GAMEOVER, WON };

// ======================= HARDWARE =======================
//                RS  E   D4  D5  D6  D7
LiquidCrystal lcdA(22, 23, 24, 25, 26, 27);   // Player 1 screen
LiquidCrystal lcdB(28, 29, 30, 31, 32, 33);   // Player 2 screen
LiquidCrystal* LCD[2] = {&lcdA, &lcdB};

const byte ROWS = 4, COLS = 3;
char keys[ROWS][COLS] = {
  {'1', '2', '3'},
  {'4', '5', '6'},
  {'7', '8', '9'},
  {'*', '0', '#'}
};
byte rowPins1[ROWS] = {37, 38, 39, 40};
byte colPins1[COLS] = {34, 35, 36};
byte rowPins2[ROWS] = {44, 45, 46, 47};
byte colPins2[COLS] = {41, 42, 43};
Keypad kp1 = Keypad(makeKeymap(keys), rowPins1, colPins1, ROWS, COLS);
Keypad kp2 = Keypad(makeKeymap(keys), rowPins2, colPins2, ROWS, COLS);
Keypad* KP[2] = {&kp1, &kp2};

const byte COLOR_PINS[2][4] = {{2, 3, 4, 5}, {6, 7, 8, 9}};   // Red, Green, Black, Yellow
const char COLOR_LETTERS[5] = "RGBY";
const char* COLOR_NAMES[4] = {"RED", "GREEN", "BLACK", "YELLOW"};
const byte SYNC_LED[2]     = {48, 49};
const byte START_PIN       = 12;
const byte START_LED       = 52;
const byte SPEAKER         = 13;
const byte POT_PIN[2]      = {A0, A1};
const byte TOGGLE_PINS[2][5] = {{A2, A3, A4, A5, A6}, {A7, A8, A9, A10, A11}};

#if USE_BARGRAPH
Adafruit_24bargraph bar1 = Adafruit_24bargraph();
Adafruit_24bargraph bar2 = Adafruit_24bargraph();
#endif

// ======================= DEBOUNCED BUTTON =======================
struct Button {
  uint8_t pin = 0;
  bool stable = HIGH, lastRead = HIGH, pressedEvt = false, releasedEvt = false;
  unsigned long changed = 0, pressTime = 0;

  void begin(uint8_t p) { pin = p; pinMode(pin, INPUT_PULLUP); }

  void update() {
    bool r = digitalRead(pin);
    if (r != lastRead) { lastRead = r; changed = millis(); }
    if (millis() - changed > 15 && r != stable) {
      stable = r;
      if (stable == LOW) { pressedEvt = true; pressTime = changed; }
      else releasedEvt = true;
    }
  }
  bool wasPressed()  { bool e = pressedEvt;  pressedEvt = false;  return e; }
  bool wasReleased() { bool e = releasedEvt; releasedEvt = false; return e; }
  void clear() { pressedEvt = releasedEvt = false; }
};

Button colorBtn[2][4];
Button startBtn;

// ======================= SOUND (non-blocking) =======================
struct Note { uint16_t f; uint16_t d; uint16_t gap; };   // freq Hz, duration ms, gap ms

const Note SND_PASSWORD[] = {{1200, 80, 60}, {1200, 80, 0}};
const Note SND_SYNC[]     = {{1000, 250, 0}};
const Note SND_HOLD[]     = {{700, 120, 40}, {1100, 120, 0}};
const Note SND_COLOR[]    = {{900, 70, 30}, {1200, 70, 30}, {1500, 70, 0}};
const Note SND_DIAL[]     = {{600, 60, 10}, {800, 60, 10}, {1000, 60, 10}, {1200, 60, 10}, {1400, 60, 0}};
const Note SND_SWITCH[]   = {{1500, 70, 30}, {900, 70, 30}, {1500, 70, 0}};
const Note SND_START[]    = {{800, 100, 0}};
const Note SND_SUCCESS[]  = {{1047, 100, 30}, {1319, 100, 30}, {1568, 150, 0}};
const Note SND_FAIL[]     = {{900, 200, 20}, {700, 200, 20}, {500, 400, 0}};
const Note SND_RELEASE[]  = {{1800, 150, 0}};
const Note SND_CLICK[]    = {{2000, 30, 0}};

const byte QSIZE = 24;
Note soundQ[QSIZE];
byte qHead = 0, qTail = 0;
unsigned long nextNoteAt = 0;

void pushNote(uint16_t f, uint16_t d, uint16_t g) {
  byte n = (qTail + 1) % QSIZE;
  if (n == qHead) return;                 // queue full: drop the note
  soundQ[qTail] = {f, d, g};
  qTail = n;
}
void playSeq(const Note* s, byte count) { for (byte i = 0; i < count; i++) pushNote(s[i].f, s[i].d, s[i].gap); }
#define PLAY(arr) playSeq(arr, sizeof(arr) / sizeof(arr[0]))
bool soundIdle() { return qHead == qTail; }
void clearSound() { qHead = qTail = 0; nextNoteAt = 0; noTone(SPEAKER); }
void updateSound() {
  if (qHead != qTail && millis() >= nextNoteAt) {
    Note n = soundQ[qHead];
    qHead = (qHead + 1) % QSIZE;
    if (n.f) tone(SPEAKER, n.f, n.d);
    nextNoteAt = millis() + n.d + n.gap;
  }
}

// ======================= GAME STATE =======================

struct Job {                       // what one player must DO this round
  Task type = T_PASSWORD;
  bool done = false;
  unsigned long deadline = 0;
  char text[5] = {0};              // password digits or color letters
  byte len = 0;                    // how many entered correctly so far
  bool hidden = false;
  unsigned long hideAt = 0;
  byte color = 0;                  // hold task color
  bool holding = false, releaseShown = false;
  unsigned long holdStart = 0, holdStopAt = 0;
  byte dialTarget = 0;             // 0 = LEFT, 1 = RIGHT
  bool inZone = false;
  unsigned long zoneStart = 0;
  byte swTarget = 0;               // 5-bit toggle pattern
};

Job job[2];
Task lastTask[2] = {NUM_TASKS, NUM_TASKS};
Phase phase = WAITING;
unsigned long phaseUntil = 0, roundStart = 0;
long timeLimit = TIME_START;
int score = 0;
int strikes = 0;
bool roundFailed = false;
bool syncHas[2] = {false, false};
unsigned long syncT[2] = {0, 0};

// ======================= HELPERS =======================
void show(byte p, const char* a, const char* b = "") {
  char buf[17];
  snprintf(buf, sizeof(buf), "%-16s", a); LCD[p]->setCursor(0, 0); LCD[p]->print(buf);
  snprintf(buf, sizeof(buf), "%-16s", b); LCD[p]->setCursor(0, 1); LCD[p]->print(buf);
}
void showBoth(const char* a, const char* b = "") { show(0, a, b); show(1, a, b); }

void setStartLed(bool on) { digitalWrite(START_LED, on); }
void setSyncLeds(bool on) { digitalWrite(SYNC_LED[0], on); digitalWrite(SYNC_LED[1], on); }

int zoneOf(int v) {
  int z = -1;
  if (v <= DIAL_LEFT_MAX) z = 0;
  else if (v >= DIAL_RIGHT_MIN) z = 1;
#if DIAL_REVERSED
  if (z >= 0) z = 1 - z;
#endif
  return z;
}

byte readToggles(byte p) {
  byte v = 0;
  for (byte i = 0; i < 5; i++) if (digitalRead(TOGGLE_PINS[p][i]) == LOW) v |= (1 << i);
  return v;
}

void showBars(int sc) {
#if USE_BARGRAPH
  int bars = sc * 24 / WIN_SCORE;
  for (int i = 0; i < 24; i++) {
    uint8_t c = LED_OFF;
    if (i < bars) c = (i < 16) ? LED_GREEN : (i < 20 ? LED_YELLOW : LED_RED);
    bar1.setBar(i, c);
    bar2.setBar(i, c);
  }
  bar1.writeDisplay();
  bar2.writeDisplay();
#endif
}

// Seed from a counter stored in EEPROM so every power-up differs
void seedRandom() {
  unsigned long n;
  EEPROM.get(0, n);
  n++;
  EEPROM.put(0, n);
  randomSeed(n * 2654435761UL ^ micros());
}

void failRound(byte p, const char* why) {
  if (roundFailed) return;
  roundFailed = true;
  LOG("FAIL: P"); LOG(p + 1); LOG(" "); LOGLN(why);
}

// ======================= INSTRUCTION SCREENS =======================
// The instruction for player p is shown on the OTHER player's screen.
void showInstruction(byte p) {
  char l1[17], l2[17];
  Job &j = job[p];
  byte scr = 1 - p;
  switch (j.type) {
    case T_PASSWORD:
      snprintf(l1, sizeof(l1), "P%d PASSWORD:", p + 1);
      snprintf(l2, sizeof(l2), "%s", j.hidden ? "****" : j.text);
      break;
    case T_COLOR:
      snprintf(l1, sizeof(l1), "P%d COLOR CODE:", p + 1);
      snprintf(l2, sizeof(l2), "%c %c %c %c", j.text[0], j.text[1], j.text[2], j.text[3]);
      break;
    case T_HOLD:
      if (!j.releaseShown) {
        snprintf(l1, sizeof(l1), "P%d HOLD:", p + 1);
        snprintf(l2, sizeof(l2), "%s", COLOR_NAMES[j.color]);
      } else {
        snprintf(l1, sizeof(l1), "P%d RELEASE!", p + 1);
        snprintf(l2, sizeof(l2), "SAY STOP");
      }
      break;
    case T_DIAL:
      snprintf(l1, sizeof(l1), "P%d DIAL:", p + 1);
      snprintf(l2, sizeof(l2), "%s", j.dialTarget ? "RIGHT" : "LEFT");
      break;
    case T_SWITCH:
      snprintf(l1, sizeof(l1), "P%d SWITCHES:", p + 1);
      snprintf(l2, sizeof(l2), "%d %d %d %d %d", j.swTarget & 1, (j.swTarget >> 1) & 1,
               (j.swTarget >> 2) & 1, (j.swTarget >> 3) & 1, (j.swTarget >> 4) & 1);
      break;
    default:
      return;
  }
  show(scr, l1, l2);
}

// ======================= ROUND SETUP =======================
void initJob(byte p, Task t) {
  job[p] = Job();
  Job &j = job[p];
  j.type = t;
  j.deadline = roundStart + timeLimit;
  for (byte i = 0; i < 4; i++) colorBtn[p][i].clear();
  KP[p]->getKey();                                   // flush any old key press

  switch (t) {
    case T_PASSWORD:
      for (byte i = 0; i < 4; i++) j.text[i] = '0' + random(10);
      j.text[4] = 0;
      j.hideAt = roundStart + PW_VISIBLE;
      LOG("P"); LOG(p + 1); LOG(" PASSWORD = "); LOGLN(j.text);
      break;
    case T_COLOR:
      for (byte i = 0; i < 4; i++) j.text[i] = COLOR_LETTERS[random(4)];
      j.text[4] = 0;
      LOG("P"); LOG(p + 1); LOG(" COLOR CODE = "); LOGLN(j.text);
      break;
    case T_HOLD:
      j.color = random(4);
      LOG("P"); LOG(p + 1); LOG(" HOLD "); LOGLN(COLOR_NAMES[j.color]);
      break;
    case T_DIAL: {
      int z = zoneOf(analogRead(POT_PIN[p]));
      if (z == 0) j.dialTarget = 1;                  // already LEFT: ask for RIGHT
      else if (z == 1) j.dialTarget = 0;
      else j.dialTarget = random(2);
      LOG("P"); LOG(p + 1); LOG(" DIAL "); LOGLN(j.dialTarget ? "RIGHT" : "LEFT");
      break;
    }
    case T_SWITCH: {
      byte cur = readToggles(p);
      do { j.swTarget = random(32); } while (j.swTarget == cur);
      LOG("P"); LOG(p + 1); LOG(" SWITCH target = "); LOGLN(j.swTarget, BIN);
      break;
    }
    case T_SYNC:
      LOG("P"); LOG(p + 1); LOGLN(" SYNC");
      break;
    default: break;
  }
}

void playTaskSound(Task t) {
  switch (t) {
    case T_PASSWORD: PLAY(SND_PASSWORD); break;
    case T_SYNC:     PLAY(SND_SYNC);     break;
    case T_HOLD:     PLAY(SND_HOLD);     break;
    case T_COLOR:    PLAY(SND_COLOR);    break;
    case T_DIAL:     PLAY(SND_DIAL);     break;
    case T_SWITCH:   PLAY(SND_SWITCH);   break;
    default: break;
  }
}

void startRound() {
  roundStart = millis();
  roundFailed = false;
  syncHas[0] = syncHas[1] = false;
  timeLimit = TIME_START - (long)(score / 2) * TIME_STEP;   // score counts 2 per stage
  if (timeLimit < TIME_MIN) timeLimit = TIME_MIN;

  Task t[2];
  for (byte p = 0; p < 2; p++) {
#if TEST_TASK >= 0
    t[p] = (Task)TEST_TASK;
#else
    do { t[p] = (Task)random(NUM_TASKS); } while (t[p] == lastTask[p]);
#endif
  }
  if (t[0] == T_SYNC || t[1] == T_SYNC) t[0] = t[1] = T_SYNC;   // sync needs both players
  lastTask[0] = t[0];
  lastTask[1] = t[1];

  LOG("--- ROUND, score "); LOG(score); LOG(", time limit "); LOG(timeLimit); LOGLN(" ms");
  clearSound();
  initJob(0, t[0]);
  initJob(1, t[1]);

  if (t[0] == T_SYNC) {
    showBoth("SYNC PRESS!", "BOTH PRESS RED");
    PLAY(SND_SYNC);
  } else {
    showInstruction(0);
    showInstruction(1);
    playTaskSound(t[0]);
    playTaskSound(t[1]);
  }
  setSyncLeds(false);
  setStartLed(false);
  phase = PLAYING;
}

// ======================= TASK LOGIC =======================
void updateSync() {
  unsigned long now = millis();
  for (byte p = 0; p < 2; p++) {
    if (colorBtn[p][0].wasPressed()) { syncHas[p] = true; syncT[p] = colorBtn[p][0].pressTime; }
  }
  if (syncHas[0] && syncHas[1]) {
    long diff = (long)syncT[0] - (long)syncT[1];
    LOG("Sync difference (ms): "); LOGLN(diff);
    if (abs(diff) <= (long)SYNC_WINDOW) {
      job[0].done = job[1].done = true;
    } else {
      syncHas[0] = syncHas[1] = false;               // not in sync: try again
    }
  } else if (syncHas[0] || syncHas[1]) {
    unsigned long first = syncHas[0] ? syncT[0] : syncT[1];
    if (now - first > SYNC_WINDOW) syncHas[0] = syncHas[1] = false;   // other player too slow
  }
  bool blink = (now / 250) % 2 == 0;
  setSyncLeds(blink);
}

void updateJob(byte p, char key) {
  Job &j = job[p];
  if (j.done || j.type == T_SYNC) return;
  unsigned long now = millis();

  switch (j.type) {
    case T_PASSWORD:
      if (!j.hidden && now >= j.hideAt) { j.hidden = true; showInstruction(p); }
      if (key >= '0' && key <= '9') {
        if (key != j.text[j.len]) { failRound(p, "wrong password digit"); return; }
        j.len++;
        if (soundIdle()) PLAY(SND_CLICK);
        if (j.len >= 4) j.done = true;
      }
      break;

    case T_COLOR:
      for (byte i = 0; i < 4; i++) {
        if (colorBtn[p][i].wasPressed()) {
          if (COLOR_LETTERS[i] != j.text[j.len]) { failRound(p, "wrong color"); return; }
          j.len++;
          if (soundIdle()) pushNote(900 + 300 * i, 60, 0);
          if (j.len >= 4) { j.done = true; return; }
        }
      }
      break;

    case T_HOLD:
      if (!j.holding) {
        for (byte i = 0; i < 4; i++) {
          if (colorBtn[p][i].wasPressed()) {
            if (i != j.color) { failRound(p, "wrong hold button"); return; }
            j.holding = true;
            j.holdStart = colorBtn[p][i].pressTime;
            j.holdStopAt = random(HOLD_MIN_MS, HOLD_MAX_MS + 1);
            j.deadline = 0xFFFFFFFFUL;                // no timeout while holding
            colorBtn[p][i].clear();
            LOG("P"); LOG(p + 1); LOG(" holding, release prompt in (ms): "); LOGLN(j.holdStopAt);
          }
        }
      }
      if (j.holding) {
        if (!j.releaseShown && now - j.holdStart >= j.holdStopAt) {
          j.releaseShown = true;
          j.deadline = now + HOLD_REACT;
          showInstruction(p);
          PLAY(SND_RELEASE);
          LOG("P"); LOG(p + 1); LOGLN(" RELEASE prompt");
        }
        for (byte i = 0; i < 4; i++) {
          if (i != j.color && colorBtn[p][i].wasPressed()) { failRound(p, "pressed another button while holding"); return; }
        }
        if (colorBtn[p][j.color].wasReleased()) {
          if (j.releaseShown) j.done = true;
          else failRound(p, "released too early");
        }
      }
      break;

    case T_DIAL: {
      int z = zoneOf(analogRead(POT_PIN[p]));
      if (z == j.dialTarget) {
        if (!j.inZone) { j.inZone = true; j.zoneStart = now; }
        else if (now - j.zoneStart >= DIAL_HOLD) j.done = true;
      } else {
        j.inZone = false;
      }
      break;
    }

    case T_SWITCH:
      if (readToggles(p) == j.swTarget) j.done = true;
      break;

    default: break;
  }
}

void endRound(bool success) {
  setSyncLeds(false);
  if (success) {
    score += 2;                                    // +1 per player
    LOG("STAGE CLEARED, score = "); LOGLN(score);
    showBars(score);
    if (score >= WIN_SCORE) {
      showBoth("MISSION COMPLETE", "Press START");
      clearSound();
      for (int i = 0; i < 16; i++) pushNote(500 + i * 100, 70, 0);
      pushNote(2200, 500, 0);
      phase = WON;
      phaseUntil = millis() + RESTART_LOCK_MS;
      setStartLed(true);
      LOGLN("MISSION COMPLETE");
      return;
    }
    char buf[17];
    snprintf(buf, sizeof(buf), "Score: %d", score);
    showBoth("STAGE CLEAR!", buf);
    PLAY(SND_SUCCESS);
    phase = RESULT;
    phaseUntil = millis() + RESULT_MS;
  } else {
#if TEST_TASK >= 0
    showBoth("TEST: FAILED", "Retrying...");   // test mode: no strikes, no game over
    clearSound();
    PLAY(SND_FAIL);
    phase = RESULT;
    phaseUntil = millis() + 1500;
    return;
#endif
    strikes++;
    LOG("STRIKE "); LOG(strikes); LOG(" of "); LOGLN(MAX_STRIKES);
    char buf[17];
    if (strikes < MAX_STRIKES) {
      snprintf(buf, sizeof(buf), "Strike %d of %d", strikes, MAX_STRIKES);
      showBoth("WRONG!", buf);
      clearSound();
      PLAY(SND_FAIL);
      phase = RESULT;                              // same score, new round
      phaseUntil = millis() + 1500;
      return;
    }
    snprintf(buf, sizeof(buf), "Score:%-3d START", score);
    showBoth("GAME OVER", buf);
    clearSound();
    PLAY(SND_FAIL);
    phase = GAMEOVER;
    phaseUntil = millis() + RESTART_LOCK_MS;
    setStartLed(true);
    LOG("GAME OVER, score = "); LOGLN(score);
  }
}

void goReady() {
  score = 0;
  strikes = 0;
  timeLimit = TIME_START;
  lastTask[0] = lastTask[1] = NUM_TASKS;
  showBars(0);
  showBoth("GET READY", "...");
  clearSound();
  PLAY(SND_START);
  setStartLed(false);
  setSyncLeds(false);
  phase = READY;
  phaseUntil = millis() + READY_MS;
  startBtn.clear();
  LOGLN("START pressed");
}

// ======================= ARDUINO ENTRY POINTS =======================
void setup() {
#if DEBUG
  Serial.begin(9600);
#endif
  LCD[0]->begin(16, 2);
  LCD[1]->begin(16, 2);

  for (byte p = 0; p < 2; p++) {
    for (byte i = 0; i < 4; i++) colorBtn[p][i].begin(COLOR_PINS[p][i]);
    for (byte i = 0; i < 5; i++) pinMode(TOGGLE_PINS[p][i], INPUT_PULLUP);
    pinMode(SYNC_LED[p], OUTPUT);
  }
  startBtn.begin(START_PIN);
  pinMode(START_LED, OUTPUT);
  pinMode(SPEAKER, OUTPUT);

#if USE_BARGRAPH
  bar1.begin(BAR_ADDR_1);
  bar2.begin(BAR_ADDR_2);
  bar1.setBrightness(8);
  bar2.setBrightness(8);
#endif

  seedRandom();
  showBars(0);
  showBoth("LAUNCH IT!", "Press START");
  setStartLed(true);
  phase = WAITING;
  LOGLN("Launch It! ready. Press START.");
}

void loop() {
  unsigned long now = millis();

  for (byte p = 0; p < 2; p++) {
    for (byte i = 0; i < 4; i++) colorBtn[p][i].update();
  }
  startBtn.update();
  char key[2] = {KP[0]->getKey(), KP[1]->getKey()};
  updateSound();

  switch (phase) {
    case WAITING:
      if (startBtn.wasPressed()) goReady();
      break;

    case READY:
      if (now >= phaseUntil) startRound();
      break;

    case PLAYING: {
      if (job[0].type == T_SYNC) updateSync();
      updateJob(0, key[0]);
      updateJob(1, key[1]);
      if (!roundFailed) {
        for (byte p = 0; p < 2; p++) {
          if (!job[p].done && now > job[p].deadline) failRound(p, "timeout");
        }
      }
      if (roundFailed) endRound(false);
      else if (job[0].done && job[1].done) endRound(true);
      break;
    }

    case RESULT:
      if (now >= phaseUntil) startRound();
      break;

    case GAMEOVER:
      if (now >= phaseUntil && startBtn.wasPressed()) goReady();
      break;

    case WON:
      setSyncLeds((now / 200) % 2 == 0);              // celebrate: blink the sync LEDs
      if (now >= phaseUntil && startBtn.wasPressed()) goReady();
      break;
  }
}
