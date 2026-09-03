/*
  ไฟล์นี้สร้างจากตัวอย่างชื่อเดียวกันในโฟลเดอร์ ArduinoIDE
  เนื้อหาเหมือนกันทุกบรรทัด ต่างแค่ #include <Arduino.h> ที่ PlatformIO ต้องการ
  วิธีใช้: คัดลอกไฟล์นี้ไปทับ PlatformIO/src/main.cpp แล้วกด Upload
*/

#include <Arduino.h>

/*
  02_HeartRate - วัดอัตราการเต้นของหัวใจ

  ใช้คลาส MassmoreMAX3010xBeatDetector ที่มากับไลบรารี ป้อนค่าอินฟราเรด
  เข้าไปทีละตัวอย่าง แล้วมันจะบอกกลับมาว่าตัวอย่างไหนคือจังหวะเต้นครั้งใหม่

  วิธีวางนิ้วให้ได้ค่านิ่ง
    วางปลายนิ้วชี้คลุมหน้าต่างเซ็นเซอร์ให้มิด กดแนบพอประมาณ อย่ากดแรง
    เพราะจะไปบีบหลอดเลือดจนคลื่นชีพจรหายไป แล้วอยู่นิ่ง ๆ สัก 10 วินาที
    ตัวเลขจะเริ่มเข้าที่หลังจับได้ครบ 6 ครั้ง

  ค่าที่พิมพ์ออกมา
    IR       ค่าดิบอินฟราเรด ใช้ดูว่าวางนิ้วแน่นพอไหม
    BPM      อัตราการเต้นจากช่วงห่างสองครั้งล่าสุด
    เฉลี่ย   ค่าเฉลี่ย 6 ครั้งล่าสุด ตัวเลขนี้นิ่งกว่า ควรใช้ตัวนี้แสดงผล

  ตัวอย่างนี้ไม่ใช่เครื่องมือแพทย์ ใช้เพื่อการเรียนรู้เท่านั้น

  by Massmore  |  MIT License
*/

#include <Massmore_MAX3010x.h>
#include <Massmore_MAX3010x_Algorithms.h>
#include <Wire.h>

#define PIN_SDA 21
#define PIN_SCL 22

MassmoreMAX3010x sensor;
MassmoreMAX3010xBeatDetector beatDetector;

static uint32_t lastPrintMs = 0;

void setup() {
  Serial.begin(115200);
  delay(300);

  Serial.println();
  Serial.println("Massmore MAX3010x - 02 วัดอัตราการเต้นของหัวใจ");
  Serial.println("===============================================");

  if (!sensor.begin(Wire, MASSMORE_MAX3010X_I2C_ADDRESS, PIN_SDA, PIN_SCL)) {
    Serial.print("เชื่อมต่อไม่สำเร็จ: ");
    Serial.println(sensor.lastErrorString());
    while (true) {
      delay(1000);
    }
  }

  sensor.setupDefault(0x1F);

  /* ต้องบอกอัตราข้อมูลจริงให้ตัวจับจังหวะทราบ ไม่งั้น BPM จะเพี้ยนทั้งหมด
     เพราะมันคำนวณจากจำนวนตัวอย่างที่นับได้ระหว่างสองจังหวะ */
  beatDetector.begin(sensor.getEffectiveSampleRate());

  Serial.println("วางปลายนิ้วบนเซ็นเซอร์แล้วอยู่นิ่ง ๆ");
  Serial.println();
}

void loop() {
  if (sensor.update()) {
    while (sensor.available()) {
      const uint32_t ir = sensor.getIR();
      sensor.nextSample();

      if (beatDetector.check(ir)) {
        /* จังหวะใหม่ พิมพ์ทันทีเพื่อให้เห็นการตอบสนองแบบเรียลไทม์ */
        Serial.print("เต้น! BPM ");
        Serial.print(beatDetector.getBeatsPerMinute(), 1);
        Serial.print("  เฉลี่ย ");
        Serial.print(beatDetector.getAverageBeatsPerMinute(), 1);
        Serial.print("  ครั้งที่ ");
        Serial.println(beatDetector.getBeatCount());
      }
    }
  }

  /* รายงานสถานะทุกครึ่งวินาที เผื่อผู้ใช้ยังไม่ได้วางนิ้ว */
  if (millis() - lastPrintMs >= 500) {
    lastPrintMs = millis();
    if (!beatDetector.isFingerPresent()) {
      Serial.print("ยังไม่พบนิ้ว  ระดับ IR = ");
      Serial.println(beatDetector.getDcLevel(), 0);
    }
  }
}
