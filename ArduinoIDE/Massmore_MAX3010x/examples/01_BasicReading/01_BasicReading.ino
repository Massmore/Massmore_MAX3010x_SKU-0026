/*
  01_BasicReading - อ่านค่าดิบจากเซ็นเซอร์ Massmore MAX30102 (SKU-0026)

  ตัวอย่างนี้เป็นจุดเริ่มต้นที่สั้นที่สุด ทำแค่สองอย่าง
    1. เชื่อมต่อเซ็นเซอร์และตั้งค่าชุดมาตรฐาน
    2. พิมพ์ค่าดิบของช่องแดงและช่องอินฟราเรดออกทาง Serial

  ค่าที่เห็นคืออะไร
    เป็นจำนวนนับจาก ADC 18 บิต (0 ถึง 262143) ที่แปรผันตามปริมาณแสงที่
    โฟโตไดโอดรับได้ ยิ่งมีวัตถุสะท้อนแสงกลับมามาก ค่ายิ่งสูง
    ตอนไม่มีอะไรวางอยู่ ค่าจะต่ำมาก (ราวหลักพันหรือน้อยกว่า)
    พอวางนิ้ว ค่าจะกระโดดขึ้นไปหลักหมื่นถึงแสน แล้วแกว่งเบา ๆ ตามจังหวะชีพจร

  การต่อสาย (บอร์ด Massmore ESP32 Breakout / Qwiic)
    VIN -> 3V3 หรือ 5V     SDA -> GPIO 21
    GND -> GND             SCL -> GPIO 22
    INT -> ไม่ต้องต่อ ตัวอย่างนี้ไม่ได้ใช้

  ลองต่อยอด
    - เปลี่ยน setupDefault(0x1F) เป็นค่าอื่นเพื่อปรับความสว่างของ LED
    - เปิด Serial Plotter ของ Arduino IDE เพื่อดูรูปคลื่นชีพจร

  by Massmore  |  MIT License
*/

#include <Massmore_MAX3010x.h>
#include <Wire.h>

#define PIN_SDA 21
#define PIN_SCL 22

MassmoreMAX3010x sensor;

void setup() {
  Serial.begin(115200);
  delay(300);

  Serial.println();
  Serial.println("Massmore MAX3010x - 01 การอ่านค่าพื้นฐาน");
  Serial.println("=========================================");

  /* begin() จะเปิดบัส I2C ให้เอง แล้วตรวจ PART_ID ว่าเป็นชิปตระกูลนี้จริง */
  if (!sensor.begin(Wire, MASSMORE_MAX3010X_I2C_ADDRESS, PIN_SDA, PIN_SCL)) {
    Serial.print("เชื่อมต่อไม่สำเร็จ: ");
    Serial.println(sensor.lastErrorString());
    Serial.println("ตรวจสายไฟและไฟเลี้ยง แล้วกดปุ่ม reset บนบอร์ด");
    while (true) {
      delay(1000);
    }
  }

  Serial.print("พบชิป ");
  Serial.print(sensor.getVariantName());
  Serial.print("  รีวิชัน 0x");
  Serial.println(sensor.readRevisionID(), HEX);

  /* ชุดค่ามาตรฐาน โหมด SpO2 (แดง + อินฟราเรด) ได้ข้อมูล 50 ชุดต่อวินาที */
  sensor.setupDefault(0x1F);

  Serial.print("อัตราข้อมูลจริง ");
  Serial.print(sensor.getEffectiveSampleRate(), 1);
  Serial.println(" ชุดต่อวินาที");
  Serial.println();
  Serial.println("แดง\tอินฟราเรด");
}

void loop() {
  /* update() ไม่บล็อก ถ้ายังไม่มีข้อมูลใหม่จะคืน false ทันที */
  if (!sensor.update()) {
    return;
  }

  while (sensor.available()) {
    Serial.print(sensor.getRed());
    Serial.print('\t');
    Serial.println(sensor.getIR());

    /* อย่าลืมเรียก nextSample() ไม่งั้นจะอ่านค่าเดิมวนไปเรื่อย ๆ */
    sensor.nextSample();
  }
}
