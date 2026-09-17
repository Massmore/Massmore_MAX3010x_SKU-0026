/*
  03_NonBlocking_Multitask — Non-blocking FSM API + งานอื่นใน loop() พร้อมกัน

  loop() ไม่ถูก block เลย:
    - Task A: sensor.update() ดึงข้อมูลจาก FIFO เมื่อมี → getReadings() → BeatDetector นับชีพจร
    - Task B: กระพริบ LED ทุก 500 ms ด้วย millis() (rollover-safe)
    - Task C: อ่าน Die Temperature แบบ non-blocking ทุก 2 วินาที
      (startTemperatureConversion → isTemperatureReady → getTemperatureResult)

  ถ้า LED กระพริบสม่ำเสมอ = loop() ไม่ถูก block จริง

  Wiring (Massmore MOMO ESP32-S3): SDA -> GPIO 14, SCL -> GPIO 15
  Wiring (Classic ESP32)              Wiring (Arduino Nano)
    VIN -> 3V3   SDA -> GPIO 21        VIN -> 5V   SDA -> A4
    GND -> GND   SCL -> GPIO 22        GND -> GND  SCL -> A5

  Designed and Manufactured by Massmore | MIT License
*/

#include <Wire.h>
#include <Massmore_MAX3010x.h>

#if defined(CONFIG_IDF_TARGET_ESP32S3)
#define PIN_SDA 14 /* Massmore MOMO ESP32-S3 */
#define PIN_SCL 15
#elif defined(ESP32)
#define PIN_SDA 21
#define PIN_SCL 22
#endif
#ifndef LED_BUILTIN
#define LED_BUILTIN 2
#endif

Massmore_MAX3010x sensor;
Massmore_MAX3010x::BeatDetector beat;

static uint32_t lastBlinkMs = 0;
static uint32_t lastTempMs = 0;
static bool tempPending = false;
static uint32_t sampleCount = 0;

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println();
  Serial.println(F("Massmore_MAX3010x - 03_NonBlocking_Multitask"));
  pinMode(LED_BUILTIN, OUTPUT);

#if defined(ESP32)
  Wire.begin(PIN_SDA, PIN_SCL);
#else
  Wire.begin();
#endif
  Wire.setClock(100000); /* 100 kHz: เสถียรบนบัสที่มีหลายอุปกรณ์ */

  if (!sensor.begin(Wire)) {
    Serial.print(F("Sensor not found: "));
    Serial.println(sensor.lastErrorString());
    while (true) delay(1000);
  }
  sensor.setupDefault(0x1F);
  beat.begin(sensor.getEffectiveSampleRate()); /* 50 Hz */
  sensor.requestConversion();                  /* เริ่มรอบเก็บข้อมูลใหม่ */
  Serial.println(F("Place finger on the sensor..."));
}

void loop() {
  const uint32_t now = millis();

  /* ---- Task A: sensor FSM (ไม่ block) ---- */
  sensor.update(); /* คืน false ทันทีถ้า FIFO ยังว่าง */
  while (sensor.isDataReady()) {
    Massmore_MAX3010x::Readings r;
    sensor.getReadings(r);
    sampleCount++;
    if (beat.check(r.ir)) {
      Serial.print(F("Beat! BPM="));
      Serial.print(beat.getBeatsPerMinute(), 1);
      Serial.print(F("  avg="));
      Serial.print(beat.getAverageBeatsPerMinute(), 1);
      Serial.print(F("  samples="));
      Serial.println(sampleCount);
    }
  }

  /* ---- Task B: blink LED (rollover-safe) ---- */
  if ((uint32_t)(now - lastBlinkMs) >= 500) {
    lastBlinkMs = now;
    digitalWrite(LED_BUILTIN, !digitalRead(LED_BUILTIN));
  }

  /* ---- Task C: die temperature FSM ---- */
  if (!tempPending && (uint32_t)(now - lastTempMs) >= 2000) {
    lastTempMs = now;
    tempPending = sensor.startTemperatureConversion();
  }
  if (tempPending && sensor.isTemperatureReady()) {
    tempPending = false;
    Serial.print(F("Die temp: "));
    Serial.print(sensor.getTemperatureResult(), 2);
    Serial.print(F(" C  finger="));
    Serial.println(beat.isFingerPresent() ? F("yes") : F("no"));
  }
}
