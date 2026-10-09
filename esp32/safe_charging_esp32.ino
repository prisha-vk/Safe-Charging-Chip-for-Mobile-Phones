#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <OneWire.h>
#include <DallasTemperature.h>


// =====================================================
// LCD
// =====================================================

LiquidCrystal_I2C lcd(0x27, 16, 2);


// =====================================================
// PYNQ UART
// =====================================================

// UART2
// GPIO16 = RX2
// GPIO17 = TX2

HardwareSerial PYNQUART(2);


// =====================================================
// SENSOR PINS
// =====================================================

const int DS18B20_PIN = 19;
const int ACS712_PIN  = 35;
const int VOLTAGE_PIN = 32;


// =====================================================
// FAULT POTENTIOMETERS
// =====================================================

const int POT_VOLT = 33;
const int POT_CURR = 36;   // VP
const int POT_TEMP = 39;   // VN


// =====================================================
// OUTPUTS
// =====================================================

const int RELAY_PIN  = 26;
const int BUZZER_PIN = 25;

const int LED_GREEN  = 2;
const int LED_YELLOW = 4;
const int LED_RED    = 15;


// =====================================================
// BUTTONS
// =====================================================

const int BTN_START = 27;
const int BTN_RESET = 14;
const int BTN_TEST  = 13;
const int BTN_MENU  = 23;


// =====================================================
// DS18B20
// =====================================================

OneWire oneWire(DS18B20_PIN);
DallasTemperature temperatureSensor(&oneWire);


// =====================================================
// ACS712 CALIBRATION
// =====================================================

// Your measured zero-current value
const float ACS_ZERO_MV = 1749.5;

// ACS712 5A sensitivity
const float ACS_SENSITIVITY = 185.0;

// External divider correction
const float ACS_DIVIDER_FACTOR = 1.5;


// =====================================================
// SAFETY THRESHOLDS
// =====================================================

const float MAX_VOLTAGE = 5.5;
const float MAX_CURRENT = 7.5;

const float TEMP_WARNING    = 32.0;
const float MAX_TEMPERATURE = 40.0;

const int FAULT_POT_LIMIT = 90;


// =====================================================
// SYSTEM VARIABLES
// =====================================================

bool systemRunning = false;
bool faultLatched = false;
bool manualFault = false;

bool tempSensorFault = false;

unsigned long lastSample = 0;
unsigned long lastLCD = 0;
unsigned long lastUART = 0;

float voltage = 0;
float current = 0;
float temperature = 0;
float power = 0;

int potV = 0;
int potI = 0;
int potT = 0;


// =====================================================
// SETUP
// =====================================================

void setup() {

  // ===================================================
  // USB SERIAL MONITOR
  // ===================================================

  Serial.begin(115200);


  // ===================================================
  // PYNQ UART
  // ===================================================

  // GPIO16 = RX2
  // GPIO17 = TX2

  PYNQUART.begin(
    115200,
    SERIAL_8N1,
    16,
    17
  );


  // ===================================================
  // ADC
  // ===================================================

  analogReadResolution(12);

  analogSetPinAttenuation(
    ACS712_PIN,
    ADC_11db
  );

  analogSetPinAttenuation(
    VOLTAGE_PIN,
    ADC_11db
  );

  analogSetPinAttenuation(
    POT_VOLT,
    ADC_11db
  );

  analogSetPinAttenuation(
    POT_CURR,
    ADC_11db
  );

  analogSetPinAttenuation(
    POT_TEMP,
    ADC_11db
  );


  // ===================================================
  // DS18B20
  // ===================================================

  temperatureSensor.begin();

  // 10-bit resolution
  temperatureSensor.setResolution(10);


  // ===================================================
  // OUTPUTS
  // ===================================================

  pinMode(RELAY_PIN, OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);

  pinMode(LED_GREEN, OUTPUT);
  pinMode(LED_YELLOW, OUTPUT);
  pinMode(LED_RED, OUTPUT);


  // ===================================================
  // BUTTONS
  // ===================================================

  pinMode(BTN_START, INPUT_PULLUP);
  pinMode(BTN_RESET, INPUT_PULLUP);
  pinMode(BTN_TEST, INPUT_PULLUP);
  pinMode(BTN_MENU, INPUT_PULLUP);


  // ===================================================
  // SAFE STARTUP
  // ===================================================

  // Relay module is active LOW
  // HIGH = relay OFF

  digitalWrite(RELAY_PIN, HIGH);

  digitalWrite(BUZZER_PIN, LOW);

  digitalWrite(LED_GREEN, LOW);
  digitalWrite(LED_YELLOW, LOW);
  digitalWrite(LED_RED, LOW);


  // ===================================================
  // LCD
  // ===================================================

  Wire.begin(21, 22);

  lcd.init();
  lcd.backlight();

  lcd.clear();

  lcd.setCursor(0, 0);
  lcd.print("SAFETY SYSTEM");

  lcd.setCursor(0, 1);
  lcd.print("INITIALIZING");

  delay(2000);


  // ===================================================
  // SERIAL START MESSAGE
  // ===================================================

  Serial.println();
  Serial.println("==============================");
  Serial.println(" ADAPTER SAFETY CONTROLLER");
  Serial.println("==============================");

  Serial.println("DS18B20 INITIALIZED");
  Serial.println("PYNQ UART INITIALIZED");
  Serial.println("TX2 = GPIO17");
  Serial.println("RX2 = GPIO16");
}


// =====================================================
// READ SENSORS
// =====================================================

void readSensors() {

  // ===================================================
  // DS18B20 TEMPERATURE
  // ===================================================

  temperatureSensor.requestTemperatures();

  float dsTemperature =
      temperatureSensor.getTempCByIndex(0);

  if (dsTemperature == DEVICE_DISCONNECTED_C) {

    tempSensorFault = true;
    temperature = -127.0;

  } else {

    tempSensorFault = false;
    temperature = dsTemperature;
  }


  // ===================================================
  // VOLTAGE SENSOR
  // ===================================================

  float sensorVoltage =
      analogReadMilliVolts(VOLTAGE_PIN) / 1000.0;

  // Common 0-25V module ≈ 5:1
  voltage = sensorVoltage * 5.0;


  // ===================================================
  // ACS712
  // ===================================================

  float acs_mV =
      analogReadMilliVolts(ACS712_PIN);

  float difference =
      acs_mV - ACS_ZERO_MV;

  // Convert ESP32-side voltage difference
  // back to ACS712 sensor output difference

  float sensorDifference =
      difference * ACS_DIVIDER_FACTOR;

  current =
      sensorDifference / ACS_SENSITIVITY;

  // Direction is not important for this prototype
  current = abs(current);


  // ===================================================
  // POWER
  // ===================================================

  power = voltage * current;


  // ===================================================
  // FAULT POTS
  // ===================================================

  potV = map(
    analogRead(POT_VOLT),
    0,
    4095,
    0,
    100
  );

  potI = map(
    analogRead(POT_CURR),
    0,
    4095,
    0,
    100
  );

  potT = map(
    analogRead(POT_TEMP),
    0,
    4095,
    0,
    100
  );
}


// =====================================================
// SAFETY CHECK
// =====================================================

void safetyCheck() {

  bool voltageFault =
      voltage > MAX_VOLTAGE;

  bool currentFault =
      current > MAX_CURRENT;

  bool temperatureFault =
      temperature > MAX_TEMPERATURE;

  // DS18B20 disconnected
  bool sensorFault =
      tempSensorFault;

  bool potVoltageFault =
      potV >= FAULT_POT_LIMIT;

  bool potCurrentFault =
      potI >= FAULT_POT_LIMIT;

  bool potTemperatureFault =
      potT >= FAULT_POT_LIMIT;

  bool anyFault =
      voltageFault ||
      currentFault ||
      temperatureFault ||
      sensorFault ||
      potVoltageFault ||
      potCurrentFault ||
      potTemperatureFault ||
      manualFault;

  if (anyFault) {

    faultLatched = true;
  }
}


// =====================================================
// OUTPUT CONTROL
// =====================================================

void updateOutputs() {

  // ===================================================
  // FAULT
  // ===================================================

  if (faultLatched) {

    // Relay OFF
    digitalWrite(RELAY_PIN, HIGH);

    digitalWrite(LED_GREEN, LOW);
    digitalWrite(LED_YELLOW, LOW);
    digitalWrite(LED_RED, HIGH);

    digitalWrite(BUZZER_PIN, HIGH);

    return;
  }


  // ===================================================
  // SYSTEM NOT STARTED
  // ===================================================

  if (!systemRunning) {

    digitalWrite(RELAY_PIN, HIGH);

    digitalWrite(LED_GREEN, LOW);
    digitalWrite(LED_YELLOW, LOW);
    digitalWrite(LED_RED, LOW);

    digitalWrite(BUZZER_PIN, LOW);

    return;
  }


  // ===================================================
  // WARNING
  // ===================================================

  bool warning =
      temperature >= TEMP_WARNING ||
      potV >= 70 ||
      potI >= 70 ||
      potT >= 70;

  if (warning) {

    // Relay remains ON during warning
    digitalWrite(RELAY_PIN, LOW);

    digitalWrite(LED_GREEN, LOW);
    digitalWrite(LED_YELLOW, HIGH);
    digitalWrite(LED_RED, LOW);

    digitalWrite(BUZZER_PIN, LOW);

    return;
  }


  // ===================================================
  // NORMAL
  // ===================================================

  digitalWrite(RELAY_PIN, LOW);

  digitalWrite(LED_GREEN, HIGH);
  digitalWrite(LED_YELLOW, LOW);
  digitalWrite(LED_RED, LOW);

  digitalWrite(BUZZER_PIN, LOW);
}


// =====================================================
// LCD
// =====================================================

void updateLCD() {

  if (millis() - lastLCD < 2000)
    return;

  lastLCD = millis();

  lcd.clear();


  // ===================================================
  // SENSOR FAULT
  // ===================================================

  if (tempSensorFault) {

    lcd.setCursor(0, 0);
    lcd.print("TEMP SENSOR ERR");

    lcd.setCursor(0, 1);
    lcd.print("RELAY CUT OFF");

    return;
  }


  // ===================================================
  // SYSTEM FAULT
  // ===================================================

  if (faultLatched) {

    lcd.setCursor(0, 0);
    lcd.print("!! FAULT !!");

    lcd.setCursor(0, 1);
    lcd.print("RELAY CUT OFF");

    return;
  }


  // ===================================================
  // SYSTEM NOT STARTED
  // ===================================================

  if (!systemRunning) {

    lcd.setCursor(0, 0);
    lcd.print("SYSTEM READY");

    lcd.setCursor(0, 1);
    lcd.print("Press START");

    return;
  }


  // ===================================================
  // NORMAL MONITORING
  // ===================================================

  lcd.setCursor(0, 0);

  lcd.print("V:");
  lcd.print(voltage, 2);

  lcd.print(" I:");
  lcd.print(current, 2);

  lcd.setCursor(0, 1);

  lcd.print("T:");
  lcd.print(temperature, 1);

  lcd.print(" P:");
  lcd.print(power, 1);
}


// =====================================================
// BUTTONS
// =====================================================

void checkButtons() {

  // ===================================================
  // START
  // ===================================================

  if (digitalRead(BTN_START) == LOW) {

    systemRunning = true;

    Serial.println("SYSTEM STARTED");

    delay(300);
  }


  // ===================================================
  // RESET
  // ===================================================

  if (digitalRead(BTN_RESET) == LOW) {

    faultLatched = false;
    manualFault = false;

    Serial.println("FAULT RESET");

    delay(300);
  }


  // ===================================================
  // MANUAL FAULT TEST
  // ===================================================

  if (digitalRead(BTN_TEST) == LOW) {

    manualFault = true;

    Serial.println("MANUAL FAULT TEST");

    delay(300);
  }


  // ===================================================
  // MENU
  // ===================================================

  if (digitalRead(BTN_MENU) == LOW) {

    Serial.println("MENU BUTTON");

    delay(300);
  }
}


// =====================================================
// UART DATA FOR PYNQ
// =====================================================
void sendToPYNQ() {

    int voltage_mV =
        (int)(voltage * 1000.0);

    int current_mA =
        (int)(current * 1000.0);

    int temperature_centi =
        (int)(temperature * 100.0);

    PYNQUART.print("V=");
    PYNQUART.print(voltage_mV);

    PYNQUART.print(",I=");
    PYNQUART.print(current_mA);

    PYNQUART.print(",T=");
    PYNQUART.print(temperature_centi);

    PYNQUART.print(",F=");
    PYNQUART.print(faultLatched ? 1 : 0);

    PYNQUART.print(",S=");
    PYNQUART.println(tempSensorFault ? 0 : 1);
}

void sendToDashboardSerial() {
    int voltage_mV = (int)(voltage * 1000.0);
    int current_mA = (int)(current * 1000.0);
    int temperature_centi = (int)(temperature * 100.0);

    Serial.print("V=");
    Serial.print(voltage_mV);

    Serial.print(",I=");
    Serial.print(current_mA);

    Serial.print(",T=");
    Serial.print(temperature_centi);

    Serial.print(",PV=");
    Serial.print(potV);

    Serial.print(",PI=");
    Serial.print(potI);

    Serial.print(",PT=");
    Serial.print(potT);

    Serial.print(",F=");
    Serial.print(faultLatched ? 1 : 0);

    Serial.print(",S=");
    Serial.println(tempSensorFault ? 0 : 1);
}

void loop() {

  checkButtons();

  if (millis() - lastSample >= 500) {
    lastSample = millis();

    readSensors();
    safetyCheck();
    updateOutputs();

    sendToDashboardSerial();
}
  updateLCD();

  sendToPYNQ();
}