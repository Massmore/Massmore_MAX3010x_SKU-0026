/*
  ไฟล์นี้สร้างจากตัวอย่างชื่อเดียวกันในโฟลเดอร์ ArduinoIDE
  เนื้อหาเหมือนกันทุกบรรทัด ต่างแค่ #include <Arduino.h> ที่ PlatformIO ต้องการ
  วิธีใช้: คัดลอกไฟล์นี้ไปทับ PlatformIO/src/main.cpp แล้วกด Upload
*/

#include <Arduino.h>

/*
  04_PresenceDetect - ตรวจจับว่ามีวัตถุหรือนิ้วเข้ามาใกล้

  MAX30102 ไม่มีวงจร proximity ในตัวเหมือน MAX30105 แต่เราทำเองได้ง่ายมาก
  เพราะค่าที่ ADC อ่านได้ก็คือปริมาณแสงที่สะท้อนกลับมาอยู่แล้ว
  ตัวอย่างนี้จึงเปรียบเทียบค่าอินฟราเรดกับเส้นฐานที่วัดไว้ตอนไม่มีอะไรบัง

  หลักการ
    1. ตอนเริ่มต้น เก็บค่าเฉลี่ยขณะไม่มีวัตถุไว้เป็นเส้นฐาน (baseline)
    2. ระหว่างทำงาน ถ้าค่าปัจจุบันสูงกว่าเส้นฐานเกินเกณฑ์ = มีวัตถุเข้ามา
    3. ใช้เกณฑ์เข้าและออกคนละค่า (ฮิสเทอรีซิส) กันการกระพริบไปมา
    4. เส้นฐานค่อย ๆ ขยับตามตัวเองตอนไม่มีวัตถุ เพื่อชดเชยแสงรอบข้างที่เปลี่ยน

  งานที่เอาไปต่อยอดได้
    สวิตช์ไร้สัมผัส เครื่องจ่ายเจลอัตโนมัติ ปลุกจอเมื่อมีคนเข้าใกล้
    หรือใช้เป็นตัวปลุกเซ็นเซอร์ก่อนเริ่มวัดชีพจรจริง เพื่อประหยัดไฟ

  ถ้าใช้ MAX30105 จะมีวงจร proximity ในตัว ใช้ setProximityThreshold()
  แล้วรออินเทอร์รัปต์ PROX ได้เลย ไม่ต้องคำนวณเอง

  by Massmore  |  MIT License
*/

#include <Massmore_MAX3010x.h>
#include <Wire.h>

#define PIN_SDA 21
#define PIN_SCL 22

/* ค่าที่สูงกว่าเส้นฐานเท่านี้ ถือว่ามีวัตถุเข้ามา */
#define PRESENCE_ENTER_DELTA 8000UL
/* ค่าที่ต้องลดต่ำกว่าเท่านี้ จึงจะถือว่าวัตถุออกไปแล้ว */
#define PRESENCE_EXIT_DELTA 4000UL

MassmoreMAX3010x sensor;

static float baseline = 0.0f;
static bool objectPresent = false;
static uint32_t enterMs = 0;

void setup() {
  Serial.begin(115200);
  delay(300);

  Serial.println();
  Serial.println("Massmore MAX3010x - 04 ตรวจจับวัตถุเข้าใกล้");
  Serial.println("============================================");

  if (!sensor.begin(Wire, MASSMORE_MAX3010X_I2C_ADDRESS, PIN_SDA, PIN_SCL)) {
    Serial.print("เชื่อมต่อไม่สำเร็จ: ");
    Serial.println(sensor.lastErrorString());
    while (true) {
      delay(1000);
    }
  }

  /* งานตรวจจับไม่ต้องใช้ช่องแดง จึงใช้โหมด HR ที่ยิง LED ดวงเดียว
     แต่บน MAX30102 โหมด HR ใช้ LED1 (สีแดง) เราจึงยังคงตั้งกระแสให้ LED1
     ถ้าอยากใช้อินฟราเรดล้วนเพื่อไม่ให้เห็นแสงแดงกะพริบ ให้ใช้โหมด SPO2
     แล้วปิดกระแส LED1 เหลือแต่ LED2 */
  sensor.setup(0x00, MASSMORE_MAX3010X_SMP_AVE_16, MASSMORE_MAX3010X_MODE_SPO2,
               MASSMORE_MAX3010X_RATE_400, MASSMORE_MAX3010X_PULSE_215US,
               MASSMORE_MAX3010X_ADC_RANGE_8192);
  sensor.setPulseAmplitudeRed(0x00); /* ปิดไฟแดง ไม่ต้องให้คนเห็นว่ากะพริบ */
  sensor.setPulseAmplitudeIR(0x3F);  /* อินฟราเรดแรงหน่อย จะได้ตรวจได้ไกลขึ้น */

  Serial.println("กำลังวัดเส้นฐาน อย่าเอาอะไรมาบังเซ็นเซอร์ 2 วินาที...");

  uint32_t start = millis();
  double sum = 0.0;
  uint32_t count = 0;
  while (millis() - start < 2000) {
    if (sensor.update()) {
      while (sensor.available()) {
        sum += (double)sensor.getIR();
        count++;
        sensor.nextSample();
      }
    }
  }
  baseline = (count > 0) ? (float)(sum / (double)count) : 0.0f;

  Serial.print("เส้นฐาน = ");
  Serial.println(baseline, 0);
  Serial.println("พร้อมแล้ว ลองเอานิ้วหรือมือเข้ามาใกล้");
  Serial.println();
}

void loop() {
  if (!sensor.update()) {
    return;
  }

  while (sensor.available()) {
    const uint32_t ir = sensor.getIR();
    sensor.nextSample();

    const float delta = (float)ir - baseline;

    if (!objectPresent && delta > (float)PRESENCE_ENTER_DELTA) {
      objectPresent = true;
      enterMs = millis();
      Serial.print("ตรวจพบวัตถุ  ค่าที่อ่านได้ ");
      Serial.print(ir);
      Serial.print("  สูงกว่าเส้นฐาน ");
      Serial.println(delta, 0);
    } else if (objectPresent && delta < (float)PRESENCE_EXIT_DELTA) {
      objectPresent = false;
      Serial.print("วัตถุออกไปแล้ว  อยู่นาน ");
      Serial.print(millis() - enterMs);
      Serial.println(" ms");
      Serial.println();
    }

    /* ปรับเส้นฐานตามแสงรอบข้างที่เปลี่ยนไป ทำเฉพาะตอนไม่มีวัตถุบัง */
    if (!objectPresent) {
      baseline += ((float)ir - baseline) * 0.002f;
    }
  }
}
