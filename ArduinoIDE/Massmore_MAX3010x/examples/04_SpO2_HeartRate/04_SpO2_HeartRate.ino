/*
  04_SpO2_HeartRate — วัด SpO2 และอัตราการเต้นของหัวใจด้วย Oximeter (ratio-of-ratios)

  Oximeter เป็นอัลกอริทึม streaming ไม่ใช้ window buffer จึงรันบน Arduino Nano ได้
    R    = (AC_red / DC_red) / (AC_ir / DC_ir)
    SpO2 = -45.060 R^2 + 30.354 R + 94.845   (ปรับได้ด้วย setCalibration)

  วิธีใช้: วางปลายนิ้วคลุมหน้าต่างเซ็นเซอร์ กดแนบพอดี อยู่นิ่ง ~5 วินาที
  ค่า spo2Valid จะเป็น true เมื่อสัญญาณดีพอ (Perfusion Index >= 0.05 %, R อยู่ในช่วง 0.3-3.0)

  WARNING: ไม่ใช่เครื่องมือแพทย์ ค่าที่ได้ไม่ผ่านการสอบเทียบทางการแพทย์

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

Massmore_MAX3010x sensor;
Massmore_MAX3010x::Oximeter oximeter;

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println();
  Serial.println(F("Massmore_MAX3010x - 04_SpO2_HeartRate"));

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
  /* LED สว่างขึ้นเล็กน้อย (0x24 ~ 7 mA) ให้ SpO2 นิ่งขึ้น */
  sensor.setupDefault(0x24);
  /* คำนวณผลใหม่ทุก 25 samples = 2 ครั้งต่อวินาทีที่ 50 Hz */
  oximeter.begin(sensor.getEffectiveSampleRate(), 25);
  Serial.println(F("Place finger on the sensor and hold still..."));
}

void loop() {
  sensor.update();
  while (sensor.isDataReady()) {
    Massmore_MAX3010x::Readings r;
    sensor.getReadings(r);

    if (oximeter.add(r.red, r.ir)) {
      const Massmore_MAX3010x::OximeterResult &res = oximeter.getResult();
      if (!res.fingerPresent) {
        Serial.println(F("No finger"));
      } else {
        Serial.print(F("SpO2="));
        if (res.spo2Valid) Serial.print(res.spo2, 1); else Serial.print(F("--"));
        Serial.print(F(" %  HR="));
        if (res.heartRateValid) Serial.print(res.heartRate, 0); else Serial.print(F("--"));
        Serial.print(F(" bpm  PI="));
        Serial.print(res.perfusionIr, 2);
        Serial.print(F(" %  R="));
        Serial.println(res.ratio, 3);
      }
    }
  }
}
