/*
  09_LowPower_DeepSleep - ประหยัดไฟทั้งฝั่งเซ็นเซอร์และฝั่ง ESP32

  เซ็นเซอร์ตัวนี้กินไฟหลักเป็นสิบมิลลิแอมป์เวลาเปิด LED แรง ๆ ซึ่งเยอะมาก
  สำหรับงานที่ใช้แบตเตอรี่ ตัวอย่างนี้แสดงวิธีลดการกินไฟสามระดับ

  ระดับที่ 1  ลดกระแส LED
      กินไฟลดลงเกือบเป็นสัดส่วนตรง แต่สัญญาณก็อ่อนลงตาม
  ระดับที่ 2  สั่ง shutdown() ให้ชิปหลับ
      datasheet ระบุกระแสในโหมดนี้ไว้ระดับไมโครแอมป์ ค่าที่ตั้งไว้ยังอยู่ครบ
      ปลุกด้วย wakeUp() แล้วรอสัญญาณเข้าที่สักครู่ก่อนเริ่มอ่าน
  ระดับที่ 3  ให้ ESP32 เข้า deep sleep ไปด้วย
      ต้องสั่ง shutdown() เซ็นเซอร์ก่อนเสมอ ไม่งั้น ESP32 หลับแต่เซ็นเซอร์
      ยังเปิด LED กินไฟอยู่ ซึ่งเป็นความผิดพลาดที่เจอบ่อยที่สุด

  ตัวอย่างนี้จะวัดชีพจร 10 วินาที แล้วหลับ 20 วินาที วนไปเรื่อย ๆ
  ค่าที่วัดได้จะถูกเก็บไว้ในหน่วยความจำ RTC ซึ่งรอดจากการ deep sleep

  by Massmore  |  MIT License
*/

#include <Massmore_MAX3010x.h>
#include <Massmore_MAX3010x_Algorithms.h>
#include <Wire.h>

#define PIN_SDA 21
#define PIN_SCL 22

#define MEASURE_SECONDS 10
#define SLEEP_SECONDS 20

MassmoreMAX3010x sensor;
MassmoreMAX3010xBeatDetector beatDetector;

/* ตัวแปรใน RTC_DATA_ATTR จะไม่หายตอน deep sleep (เฉพาะ ESP32) */
#if defined(ESP32)
RTC_DATA_ATTR uint32_t cycleCount = 0;
RTC_DATA_ATTR float lastBpm = 0.0f;
#else
uint32_t cycleCount = 0;
float lastBpm = 0.0f;
#endif

void goToSleep() {
  Serial.println();
  Serial.println("กำลังเข้าสู่โหมดประหยัดไฟ");

  /* ขั้นตอนสำคัญ ปิด LED และสั่งเซ็นเซอร์หลับก่อนเสมอ */
  sensor.setAllLedsOff();
  sensor.shutdown();
  Serial.println("  เซ็นเซอร์หลับแล้ว (กระแสระดับไมโครแอมป์)");

#if defined(ESP32)
  Serial.print("  ESP32 จะหลับ ");
  Serial.print(SLEEP_SECONDS);
  Serial.println(" วินาที แล้วบูตใหม่เอง");
  Serial.flush();

  esp_sleep_enable_timer_wakeup((uint64_t)SLEEP_SECONDS * 1000000ULL);
  esp_deep_sleep_start();
  /* โค้ดหลังบรรทัดนี้จะไม่ถูกเรียก ESP32 จะเริ่มที่ setup() ใหม่ */
#else
  /* บอร์ดที่ไม่ใช่ ESP32 สาธิตแค่การหลับของเซ็นเซอร์ */
  Serial.print("  รอ ");
  Serial.print(SLEEP_SECONDS);
  Serial.println(" วินาที แล้วปลุกเซ็นเซอร์");
  delay((uint32_t)SLEEP_SECONDS * 1000UL);
  sensor.wakeUp();
  sensor.setupDefault(0x1F);
#endif
}

void setup() {
  Serial.begin(115200);
  delay(300);

  cycleCount++;

  Serial.println();
  Serial.println("Massmore MAX3010x - 09 ประหยัดไฟและ deep sleep");
  Serial.println("================================================");
  Serial.print("รอบที่ ");
  Serial.println(cycleCount);
  if (lastBpm > 0.0f) {
    Serial.print("ค่าที่วัดได้รอบก่อน ");
    Serial.print(lastBpm, 1);
    Serial.println(" BPM (ข้อมูลรอดจาก deep sleep มาได้)");
  }

  if (!sensor.begin(Wire, MASSMORE_MAX3010X_I2C_ADDRESS, PIN_SDA, PIN_SCL)) {
    Serial.print("เชื่อมต่อไม่สำเร็จ: ");
    Serial.println(sensor.lastErrorString());
    goToSleep();
    return;
  }

  /* ตั้งค่าให้ประหยัดไฟ  กระแส LED ต่ำลง เฉลี่ยเยอะขึ้นเพื่อชดเชยสัญญาณรบกวน
     อัตราสุ่มต่ำลงก็ช่วยลดจำนวนครั้งที่ LED ต้องยิงต่อวินาที */
  sensor.setup(0x14, MASSMORE_MAX3010X_SMP_AVE_8, MASSMORE_MAX3010X_MODE_SPO2,
               MASSMORE_MAX3010X_RATE_400, MASSMORE_MAX3010X_PULSE_411US,
               MASSMORE_MAX3010X_ADC_RANGE_4096);

  beatDetector.begin(sensor.getEffectiveSampleRate());
  beatDetector.setFingerThreshold(15000);

  Serial.print("เปิดวัด ");
  Serial.print(MEASURE_SECONDS);
  Serial.println(" วินาที วางนิ้วบนเซ็นเซอร์ตอนนี้");

  const uint32_t start = millis();
  while (millis() - start < (uint32_t)MEASURE_SECONDS * 1000UL) {
    if (sensor.update()) {
      while (sensor.available()) {
        const uint32_t ir = sensor.getIR();
        sensor.nextSample();
        if (beatDetector.check(ir)) {
          Serial.print("  เต้น  ");
          Serial.print(beatDetector.getAverageBeatsPerMinute(), 1);
          Serial.println(" BPM");
        }
      }
    }
    delay(2);
  }

  if (beatDetector.getBeatCount() >= 3) {
    lastBpm = beatDetector.getAverageBeatsPerMinute();
    Serial.print("สรุปรอบนี้ ");
    Serial.print(lastBpm, 1);
    Serial.print(" BPM จากการเต้น ");
    Serial.print(beatDetector.getBeatCount());
    Serial.println(" ครั้ง");
  } else {
    Serial.println("จับจังหวะไม่ได้ในรอบนี้ อาจไม่ได้วางนิ้ว");
  }

  goToSleep();
}

void loop() {
  /* บน ESP32 จะไม่มีทางมาถึงจุดนี้ เพราะ deep sleep ทำให้บูตใหม่ที่ setup() */
  delay(1000);
}
