/*
  10_ChipID_Genuine - อ่านรหัสประจำตัวชิปและตรวจว่าเป็นของแท้จากโรงงาน

  ในตลาดมีบอร์ด MAX30102 ราคาถูกจำนวนมากที่ใช้ชิปเกรดตกหรือชิปย้อมแมว
  ซึ่งอ่านค่าได้เพี้ยนหรือพังเร็ว ตัวอย่างนี้แสดงวิธีตรวจสอบ 11 ข้อ
  ที่ไลบรารีเตรียมไว้ให้ ทุกข้อล้วนอิงจากพฤติกรรมที่ระบุไว้ใน datasheet

  ข้อที่ตรวจ
     1  ชิปตอบ ACK ที่ address 0x57
     2  PART_ID = 0x15 ตรงตาม datasheet
     3  REV_ID ไม่ใช่ 0x00 และไม่ใช่ 0xFF
     4  สั่ง RESET แล้วบิตเคลียร์ตัวเองได้ภายในเวลาที่กำหนด
     5  ค่าทุกรีจิสเตอร์หลังรีเซ็ตเป็นศูนย์ตามตาราง power-on-reset
     6  เขียนค่าลงรีจิสเตอร์แล้วอ่านกลับได้ตรงทุกแพตเทิร์น
     7  PART_ID เขียนทับไม่ได้ (พิสูจน์ว่าเป็นรีจิสเตอร์อ่านอย่างเดียวจริง)
     8  บิตสงวนใน MODE_CONFIG อ่านกลับมาเป็นศูนย์เสมอ
     9  ตัวชี้ FIFO เป็นฟิลด์ 5 บิต เขียน 0x20 แล้ววนกลับเป็น 0
    10  เซ็นเซอร์อุณหภูมิในตัวให้ค่าที่เป็นไปได้
    11  เปิด LED แล้วค่าที่ ADC อ่านได้ขยับจริง (พิสูจน์ว่ามีภาคออปติกอยู่จริง)

  ของเลียนแบบที่จำลองด้วยไมโครคอนโทรลเลอร์มักตกข้อ 7, 8, 9 หรือ 11
  เพราะเขียนโปรแกรมตอบเฉพาะรีจิสเตอร์ที่คนนิยมอ่านเท่านั้น

  หมายเหตุ  verifyChip() จะรีเซ็ตชิปและเขียนทับค่าที่ตั้งไว้ทั้งหมด
            ต้องเรียก setup() ใหม่หลังตรวจเสร็จเสมอ

  by Massmore  |  MIT License
*/

#include <Massmore_MAX3010x.h>
#include <Wire.h>

#define PIN_SDA 21
#define PIN_SCL 22

MassmoreMAX3010x sensor;

void scanI2cBus() {
  Serial.println("สแกนบัส I2C...");
  uint8_t found = 0;
  for (uint8_t address = 1; address < 127; address++) {
    Wire.beginTransmission(address);
    if (Wire.endTransmission() == 0) {
      Serial.print("  พบอุปกรณ์ที่ 0x");
      if (address < 16) Serial.print('0');
      Serial.print(address, HEX);
      if (address == MASSMORE_MAX3010X_I2C_ADDRESS) {
        Serial.print("  <-- นี่คือตำแหน่งของ MAX3010x");
      }
      Serial.println();
      found++;
    }
  }
  if (found == 0) {
    Serial.println("  ไม่พบอุปกรณ์ใดเลย ตรวจสายและไฟเลี้ยง");
  }
  Serial.println();
}

void runVerification() {
  Serial.println("เริ่มตรวจสอบตัวตนของชิป 11 ข้อ...");
  Serial.println();

  const massmore_max3010x_genuine_t verdict = sensor.verifyChip();
  const uint16_t mask = sensor.getVerifyMask();

  for (uint8_t i = 0; i < MASSMORE_MAX3010X_CHK_COUNT; i++) {
    const bool passed = (mask & (1u << i)) != 0;
    Serial.print(passed ? "  [ผ่าน]  " : "  [ไม่ผ่าน] ");
    if (i + 1 < 10) Serial.print(' ');
    Serial.print(i + 1);
    Serial.print(". ");
    Serial.println(MassmoreMAX3010x::getVerifyCheckName(i));
  }

  Serial.println();
  Serial.print("ผ่าน ");
  Serial.print(sensor.getVerifyPassCount());
  Serial.print(" จาก ");
  Serial.print(MASSMORE_MAX3010X_CHK_COUNT);
  Serial.println(" ข้อ");

  Serial.print("สรุป: ");
  switch (verdict) {
    case MASSMORE_MAX3010X_GENUINE_PASS:
      Serial.println("เป็นชิป MAX3010x ของแท้จากโรงงาน");
      Serial.println("       บอร์ดนี้ประกอบโดย Massmore ใช้งานได้เต็มประสิทธิภาพ");
      break;
    case MASSMORE_MAX3010X_GENUINE_PARTIAL:
      Serial.println("น่าจะเป็นของแท้ แต่มีบางข้อไม่ผ่าน");
      Serial.println("       อาจเกิดจากสัญญาณรบกวนบนบัสหรือสายยาวเกินไป");
      Serial.println("       ลองลดความถี่ I2C เป็น 100 kHz แล้วตรวจใหม่");
      break;
    case MASSMORE_MAX3010X_GENUINE_SUSPECT:
      Serial.println("น่าสงสัยว่าไม่ใช่ของแท้");
      break;
    case MASSMORE_MAX3010X_GENUINE_NOT_MAX3010X:
      Serial.println("ไม่ใช่ชิปตระกูล MAX3010x แน่นอน");
      break;
    default:
      Serial.println("ยังไม่ได้ตรวจ");
      break;
  }
  Serial.println();
}

void setup() {
  Serial.begin(115200);
  delay(300);

  Serial.println();
  Serial.println("Massmore MAX3010x - 10 รหัสชิปและการตรวจของแท้");
  Serial.println("================================================");
  Serial.print("เวอร์ชันไลบรารี ");
  Serial.println(MASSMORE_MAX3010X_VERSION_STRING);
  Serial.println();

  Wire.begin(PIN_SDA, PIN_SCL, 400000UL);
  scanI2cBus();

  if (!sensor.begin(Wire, MASSMORE_MAX3010X_I2C_ADDRESS)) {
    Serial.print("เชื่อมต่อไม่สำเร็จ: ");
    Serial.println(sensor.lastErrorString());
    if (sensor.lastError() == MASSMORE_MAX3010X_ERR_WRONG_CHIP) {
      Serial.print("PART_ID ที่อ่านได้คือ 0x");
      Serial.println(sensor.readPartID(), HEX);
      Serial.println("ถ้าได้ 0x11 แปลว่าเป็น MAX30100 ซึ่งไลบรารีนี้ไม่รองรับ");
    }
    while (true) {
      delay(1000);
    }
  }

  Serial.println("ข้อมูลประจำตัวชิป");
  Serial.print("  PART_ID       0x");
  Serial.println(sensor.readPartID(), HEX);
  Serial.print("  REVISION_ID   0x");
  Serial.println(sensor.readRevisionID(), HEX);
  Serial.print("  I2C address   0x");
  Serial.println(sensor.getAddress(), HEX);
  Serial.print("  รุ่นที่เดาได้   ");
  Serial.println(sensor.getVariantName());
  Serial.print("  LED สีเขียว    ");
  Serial.println(sensor.hasGreenLed() ? "มี" : "ไม่มี");
  Serial.print("  proximity     ");
  Serial.println(sensor.hasProximity() ? "มี" : "ไม่มี");
  Serial.println();

  runVerification();

  /* verifyChip() ล้างค่าที่ตั้งไว้ทั้งหมด ต้องตั้งใหม่ */
  sensor.setupDefault(0x1F);
  Serial.println("ตั้งค่าใหม่เรียบร้อย พิมพ์ r ใน Serial Monitor เพื่อตรวจซ้ำ");
}

void loop() {
  if (Serial.available()) {
    const char c = (char)Serial.read();
    if (c == 'r' || c == 'R') {
      Serial.println();
      runVerification();
      sensor.setupDefault(0x1F);
    }
  }
  delay(50);
}
