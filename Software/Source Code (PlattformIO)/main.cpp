#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_MCP23X17.h>
#include "driver/gpio.h"
#include "soc/gpio_reg.h"

Adafruit_MCP23X17 mcp;

// ===================== DAC / GPIO PINS =====================
const int MSB = 14;
const int MID = 21;
const int LSB = 47;
const int OUT_PIN = 48;

// ===================== TIMER =====================
hw_timer_t *timer = NULL;

// ===================== STATE =====================
volatile uint8_t dacVal = 0;
volatile bool pinState = false;

volatile uint32_t tickCounter = 0;
const uint32_t glitchInterval = 3000; // Tickanzahl bis zum nächsten Glitch

// ===================== KOMBINIERTES REGISTER-SCHREIBEN =====================
// Schreibt MSB/MID (Bank 0) und LSB/OUT_PIN (Bank 1) jeweils mit EINEM
// Registerzugriff pro Bank -> alle vier Signale schalten so gleichzeitig
// wie es softwareseitig möglich ist.
inline void updateOutputs(uint8_t val, bool outState) {
  uint32_t bank0_set = 0, bank0_clear = 0;
  uint32_t bank1_set = 0, bank1_clear = 0;

  if (val & 0b100) bank0_set |= (1UL << MSB); else bank0_clear |= (1UL << MSB);
  if (val & 0b010) bank0_set |= (1UL << MID); else bank0_clear |= (1UL << MID);
  if (val & 0b001) bank1_set |= (1UL << (LSB - 32));     else bank1_clear |= (1UL << (LSB - 32));
  if (outState)    bank1_set |= (1UL << (OUT_PIN - 32)); else bank1_clear |= (1UL << (OUT_PIN - 32));

  // Bank 0 zuerst, Bank 1 direkt danach -> minimaler, aber konstanter Versatz
  REG_WRITE(GPIO_OUT_W1TS_REG, bank0_set);
  REG_WRITE(GPIO_OUT_W1TC_REG, bank0_clear);
  REG_WRITE(GPIO_OUT1_W1TS_REG, bank1_set);
  REG_WRITE(GPIO_OUT1_W1TC_REG, bank1_clear);
}

// ===================== ISR =====================
// WICHTIG: Läuft jetzt in JEDEM Tick mit exakt derselben Anzahl an
// Instruktionen (branchless bzgl. der eigentlichen Ausgabe) ->
// kein zusätzlicher Jitter mehr durch den Glitch-Zweig.
void IRAM_ATTR onTimer() {
  tickCounter++;
  bool glitch = (tickCounter >= glitchInterval);

  // Nächste Werte VORBERECHNEN (aber noch nicht anwenden)
  uint8_t nextVal   = glitch ? dacVal : (uint8_t)((dacVal + 1) & 0b111);
  bool    nextState = glitch ? pinState : !pinState;

  // Aktuellen (alten) Wert ausgeben -> Timing der Ausgabe ist in jedem
  // Tick identisch, unabhängig vom Glitch
  updateOutputs(dacVal, pinState);

  // Zustand für den nächsten Tick übernehmen
  dacVal    = nextVal;
  pinState  = nextState;
  if (glitch) tickCounter = 0;
}

void setup() {
  Serial.begin(115200);
  delay(1500);

  Serial.println("BOOT OK - ESP32-S3 gestartet");

  // ===== I2C INIT =====
  Wire.begin(8, 9);   // <- DEIN aktuelles Setup
  gpio_set_pull_mode((gpio_num_t)8, GPIO_PULLUP_ONLY);  // SDA
  gpio_set_pull_mode((gpio_num_t)9, GPIO_PULLUP_ONLY);  // SCL

  Serial.println("I2C gestartet");

  // ===== MCP INIT =====
  if (!mcp.begin_I2C(0x20)) {
    Serial.println("MCP NICHT GEFUNDEN!");

    while (1) {
      Serial.println("ERROR LOOP");
      delay(1000);
    }
  }

  Serial.println("MCP OK");

  // Set all pins output
  for (int i = 0; i < 16; i++) {
    mcp.pinMode(i, OUTPUT);
    mcp.digitalWrite(i, LOW);
  }

  // ---- DAC / GPIO48 ----
  pinMode(MSB, OUTPUT);
  pinMode(MID, OUTPUT);
  pinMode(LSB, OUTPUT);
  pinMode(OUT_PIN, OUTPUT);
  digitalWrite(MSB, LOW);
  digitalWrite(MID, LOW);
  digitalWrite(LSB, LOW);
  digitalWrite(OUT_PIN, LOW);

  // ---- Timer für durchgehendes DAC/GPIO48-Signal ----
  timer = timerBegin(0, 80, true);
  timerAttachInterrupt(timer, &onTimer, true);
  timerAlarmWrite(timer, 100, true); // 100 µs Takt
  timerAlarmEnable(timer);

  Serial.println("Setup fertig");
}

void loop() {

  // Richtige Reihenfolge (alle Werte -1)
  const uint8_t leds[] = {
    4, 3, 5, 6, 10, 11, 12,
    9, 8, 7, 2, 1, 0
  };

  const uint8_t count = sizeof(leds) / sizeof(leds[0]);

  // Nacheinander einschalten
  for (uint8_t i = 0; i < count; i++) {
    mcp.digitalWrite(leds[i], HIGH);
    delay(225);
  }

  delay(500);

  // Rückwärts ausschalten
  for (int i = count - 1; i >= 0; i--) {
    mcp.digitalWrite(leds[i], LOW);
    delay(225);
  }

  delay(500);
}