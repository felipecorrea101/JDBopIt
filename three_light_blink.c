// Three light blink: LEDs on Arduino pins 8, 9, 10 take turns while the
// button on pin 2 is held. After release, the current cycle finishes and
// the LEDs stay off until the button is pressed again.
const int LEDS[3] = {8, 9, 10};
const int BTN = 2;

void setup() {
  for (int i = 0; i < 3; i++) pinMode(LEDS[i], OUTPUT);
  pinMode(BTN, INPUT_PULLUP);   // pressed = LOW
}

void loop() {
  if (digitalRead(BTN) == LOW) {
    for (int i = 0; i < 3; i++) {
      digitalWrite(LEDS[i], HIGH); delay(300);
      digitalWrite(LEDS[i], LOW);
    }
  }
}
