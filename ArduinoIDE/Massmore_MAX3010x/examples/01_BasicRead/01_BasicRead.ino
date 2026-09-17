/*
  01_BasicRead — Massmore MAX30102 Pulse Oximeter & Heart-Rate Sensor (SKU-0026)

  ตัวอย่างพื้นฐานที่สุด ใช้ Simple Blocking API
    1. sketch เปิด I2C Bus เอง (ไลบรารีไม่แตะ Wire.begin() ตามมาตรฐาน Massmore)
    2. begin() ตรวจ PART_ID = 0x15 แล้ว setupDefault() ตั้งค่า SpO2 mode 50 samples/s
    3. readAll() รอจนได้ sample แล้วพิมพ์ Red / IR ออก Serial (เปิด Serial Plotter ดูรูปคลื่นได้)

  ค่าที่เห็นคือ ADC 18-bit (0-262143) ยังไม่มีนิ้ว = หลักร้อย-พัน, วางนิ้ว = หลักหมื่น-แสน
  แล้วแกว่งเบา ๆ ตามชีพจร

  Wiring (Massmore MOMO ESP32-S3): SDA -> GPIO 14, SCL -> GPIO 15
  Wiring (Classic ESP32 / Qwiic)      Wiring (Arduino Nano)
    VIN -> 3V3        SDA -> GPIO 21    VIN -> 5V    SDA -> A4
    GND -> GND        SCL -> GPIO 22    GND -> GND   SCL -> A5

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

Massmore_MAX3010x sensor;

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println();
  Serial.println(F("Massmore_MAX3010x - 01_BasicRead"));

  /* sketch เป็นเจ้าของ I2C Bus: กำหนดขาและความเร็วเอง */
#if defined(ESP32)
  Wire.begin(PIN_SDA, PIN_SCL);
#else
  Wire.begin(); /* AVR: SDA = A4, SCL = A5 (fixed hardware pins) */
#endif
  Wire.setClock(100000); /* 100 kHz เสถียรที่สุดเมื่อมีหลายอุปกรณ์บนบัส (ชิปรองรับถึง 400 kHz) */

  if (!sensor.begin(Wire)) {
    Serial.print(F("Sensor not found: "));
    Serial.println(sensor.lastErrorString());
    while (true) delay(1000);
  }
  Serial.print(F("Found "));
  Serial.print(sensor.getVariantName());
  Serial.print(F("  REV_ID 0x"));
  Serial.println(sensor.readRevisionID(), HEX);

  sensor.setupDefault(0x1F); /* SpO2 mode, 400 Hz / avg 8 = 50 samples/s, LED ~6 mA */
  Serial.println(F("Red\tIR"));
}

void loop() {
  Massmore_MAX3010x::Readings r;
  /* Blocking: รอจนมี sample (สูงสุด 200 ms) */
  if (sensor.readAll(r, 200)) {
    Serial.print(r.red);
    Serial.print('\t');
    Serial.println(r.ir);
  } else {
    Serial.print(F("read error: "));
    Serial.println(sensor.lastErrorString());
  }
}
