
#include <Servo.h>
#include <LiquidCrystal.h>
#include <EEPROM.h>
#include <avr/interrupt.h>

Servo blinds;
LiquidCrystal lcd(8, 10, 4, 5, 6, 7);

// Pin configuration
const int lightPin = A0;
const int potPin = A1;
const int servoPin = 9;
const int buttonPin = 2;
const int ledPin = 3;

// EEPROM layout
const int EEPROM_MAGIC_ADDR = 0;
const int EEPROM_MODE_ADDR = 1;
const int EEPROM_ANGLE_ADDR = 2;
const byte EEPROM_MAGIC = 0xA5;

// Timer2: 1 ms ticks
volatile unsigned long timerTicks = 0;
volatile bool buttonPressed = false;

// System state
bool manualMode = false;
int angle = 90;
int brightness = 0;
int desiredLight = 0;

// Scheduling
unsigned long lastSensor = 0;
unsigned long lastLCD = 0;
unsigned long lastButton = 0;
unsigned long lastChange = 0;

// EEPROM state
bool settingsDirty = false;
byte savedMode = 0;
byte savedAngle = 90;

// Timer2 interrupt: every 1 ms
ISR(TIMER2_COMPA_vect) {
  timerTicks++;
}

// External interrupt: button D2
void buttonISR() {
  buttonPressed = true;
}

// Read atomic timer value
unsigned long getTicks() {
  noInterrupts();
  unsigned long ticks = timerTicks;
  interrupts();
  return ticks;
}

// Configure Timer2 in CTC mode
void setupTimer2() {
  noInterrupts();

  TCCR2A = 0;
  TCCR2B = 0;
  TCNT2 = 0;

  // 16 MHz / 64 / (249 + 1) = 1000 Hz
  OCR2A = 249;

  TCCR2A |= (1 << WGM21);
  TCCR2B |= (1 << CS22);
  TIMSK2 |= (1 << OCIE2A);

  interrupts();
}

// LED control through direct GPIO registers
void updateLED() {
  if (angle >= 180) {
    PORTD |= (1 << PORTD3);
  } else {
    PORTD &= ~(1 << PORTD3);
  }
}

// Restore settings from EEPROM
void loadSettings() {
  if (EEPROM.read(EEPROM_MAGIC_ADDR) == EEPROM_MAGIC) {
    byte mode = EEPROM.read(EEPROM_MODE_ADDR);
    byte position = EEPROM.read(EEPROM_ANGLE_ADDR);

    if (mode <= 1 && position <= 180) {
      manualMode = mode;
      angle = position;
    }
  }

  savedMode = manualMode ? 1 : 0;
  savedAngle = angle;
}

// Save settings after 3 seconds without changes
void saveSettings(unsigned long now) {
  if (!settingsDirty) return;

  if (now - lastChange < 3000) return;

  byte currentMode = manualMode ? 1 : 0;
  byte currentAngle = (byte)angle;

  if (currentMode != savedMode ||
      currentAngle != savedAngle) {

    EEPROM.update(EEPROM_MAGIC_ADDR, EEPROM_MAGIC);
    EEPROM.update(EEPROM_MODE_ADDR, currentMode);
    EEPROM.update(EEPROM_ANGLE_ADDR, currentAngle);

    savedMode = currentMode;
    savedAngle = currentAngle;
  }

  settingsDirty = false;
}

void setup() {
  // Direct GPIO: D3 output
  DDRD |= (1 << DDD3);
  PORTD &= ~(1 << PORTD3);

  pinMode(buttonPin, INPUT_PULLUP);

  lcd.begin(16, 2);
  lcd.print("Smart Blinds");
  lcd.setCursor(0, 1);
  lcd.print("Starting...");

  loadSettings();

  blinds.attach(servoPin);
  blinds.write(angle);
  updateLED();

  attachInterrupt(
    digitalPinToInterrupt(buttonPin),
    buttonISR,
    FALLING
  );

  setupTimer2();

  Serial.begin(9600);
  Serial.println("Smart blinds started");

  lastChange = getTicks();
}

void loop() {
  unsigned long now = getTicks();

  // Handle external button interrupt
  if (buttonPressed) {
    noInterrupts();
    buttonPressed = false;
    interrupts();

    // Debounce: 200 ms
    if (now - lastButton >= 200) {
      manualMode = !manualMode;
      lastButton = now;

      settingsDirty = true;
      lastChange = now;

      Serial.print("Mode: ");
      Serial.println(manualMode ? "MANUAL" : "AUTO");
    }
  }

  // Sensor and servo task: every 200 ms
  if (now - lastSensor >= 200) {
    lastSensor = now;

    int light = analogRead(lightPin);
    int pot = analogRead(potPin);

    brightness = map(light, 0, 1023, 0, 100);
    desiredLight = map(pot, 0, 1023, 0, 100);

    int previousAngle = angle;

    if (manualMode) {
      // Potentiometer directly controls blinds
      angle = map(pot, 0, 1023, 0, 180);
    } else {
      // Automatic brightness regulation
      if (brightness > desiredLight + 5) {
        angle = min(angle + 5, 180);
      } else if (brightness < desiredLight - 5) {
        angle = max(angle - 5, 0);
      }
    }

    if (angle != previousAngle) {
      blinds.write(angle);
      settingsDirty = true;
      lastChange = now;
    }

    updateLED();

    Serial.print("Light: ");
    Serial.print(brightness);
    Serial.print("% | Target: ");
    Serial.print(desiredLight);
    Serial.print("% | Angle: ");
    Serial.println(angle);
  }

  // LCD task: every 500 ms
  if (now - lastLCD >= 500) {
    lastLCD = now;

    lcd.setCursor(0, 0);
    lcd.print("Light:");
    lcd.print(brightness);
    lcd.print("%      ");

    lcd.setCursor(0, 1);

    if (manualMode) {
      lcd.print("MAN ");
      lcd.print(angle);
      lcd.print(" deg       ");
    } else {
      lcd.print("SET:");
      lcd.print(desiredLight);
      lcd.print("% A:");
      lcd.print(angle);
      lcd.print("    ");
    }
  }

  // EEPROM persistence task
  saveSettings(now);
}

