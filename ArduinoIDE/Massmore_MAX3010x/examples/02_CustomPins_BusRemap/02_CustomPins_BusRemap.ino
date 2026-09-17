/*
  02_CustomPins_BusRemap — ย้ายขา I2C และใช้ I2C Bus ตัวที่สอง

  ESP32 / ESP32-S3 (Arduino-ESP32 Core 3.x) มี GPIO Matrix ย้าย SDA/SCL ไปขาไหนก็ได้
  และมี Wire1 เป็น I2C Bus ตัวที่สอง — ตัวอย่างนี้ใช้ Wire1 บนขาที่ไม่ใช่ค่าเริ่มต้น
  แล้วส่ง Wire1 เข้า begin() ผ่าน bus reference injection

  Arduino Nano (ATmega328P) มี I2C hardware ชุดเดียว ขาตายตัว A4 = SDA, A5 = SCL
  ย้ายขาไม่ได้ ตัวอย่างนี้จึง fallback ไปใช้ Wire ปกติให้

  Wiring ESP32 (Classic)         Wiring ESP32-S3 (Massmore MOMO)
    SDA -> GPIO 33                 SDA -> GPIO 14
    SCL -> GPIO 32                 SCL -> GPIO 15

  Designed and Manufactured by Massmore | MIT License
*/

#include <Wire.h>
#include <Massmore_MAX3010x.h>

#if defined(CONFIG_IDF_TARGET_ESP32S3)
#define PIN_SDA 14 /* Massmore MOMO ESP32-S3 */
#define PIN_SCL 15
#define I2C_BUS Wire1
#elif defined(ESP32)
#define PIN_SDA 33
#define PIN_SCL 32
#define I2C_BUS Wire1
#else
/* AVR: fixed hardware pins A4/A5, single bus */
#define I2C_BUS Wire
#endif

Massmore_MAX3010x sensor;

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println();
  Serial.println(F("Massmore_MAX3010x - 02_CustomPins_BusRemap"));

#if defined(ESP32)
  /* Core 3.x: Wire1.begin(sda, scl[, frequency]) */
  I2C_BUS.begin(PIN_SDA, PIN_SCL, 100000); /* 100 kHz */
  Serial.print(F("Using Wire1 on SDA="));
  Serial.print(PIN_SDA);
  Serial.print(F(" SCL="));
  Serial.println(PIN_SCL);
#else
  I2C_BUS.begin();
  I2C_BUS.setClock(100000); /* 100 kHz: เสถียรบนบัสที่มีหลายอุปกรณ์ */
  Serial.println(F("AVR: using Wire on fixed pins SDA=A4 SCL=A5"));
#endif

  if (!sensor.begin(I2C_BUS)) {
    Serial.print(F("Sensor not found: "));
    Serial.println(sensor.lastErrorString());
    while (true) delay(1000);
  }
  sensor.setupDefault();
  Serial.println(F("IR"));
}

void loop() {
  Massmore_MAX3010x::Readings r;
  if (sensor.readAll(r)) Serial.println(r.ir);
}
