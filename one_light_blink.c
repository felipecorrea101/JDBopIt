// One light blink: LED on Arduino pin 8 (ATmega pin 14) blinks only
// while the button on Arduino pin 2 (ATmega pin 4) is held down.
const int LED = 8;
const int BTN = 2;

void setup() {
  pinMode(LED, OUTPUT);
  pinMode(BTN, INPUT_PULLUP);   // button wired to GND: pressed = LOW
}

void loop() {
  if (digitalRead(BTN) == LOW) {
    digitalWrite(LED, HIGH); delay(500);
    digitalWrite(LED, LOW);  delay(500);
  }
}
