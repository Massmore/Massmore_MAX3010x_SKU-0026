/*
  ไฟล์นี้สร้างจากตัวอย่างชื่อเดียวกันในโฟลเดอร์ ArduinoIDE
  เนื้อหาเหมือนกันทุกบรรทัด ต่างแค่ #include <Arduino.h> ที่ PlatformIO ต้องการ
  วิธีใช้: คัดลอกไฟล์นี้ไปทับ PlatformIO/src/main.cpp แล้วกด Upload
*/

#include <Arduino.h>

/*
  03_SpO2_HeartRate - วัดออกซิเจนในเลือดพร้อมอัตราการเต้นของหัวใจ

  ใช้คลาส MassmoreMAX3010xSpO2 ซึ่งเก็บข้อมูลย้อนหลัง 200 ตัวอย่าง (4 วินาที)
  แล้วคำนวณใหม่ทุกครึ่งวินาที ด้วยวิธี ratio-of-ratios

      R = (AC แดง / DC แดง) / (AC อินฟราเรด / DC อินฟราเรด)
      SpO2 = -45.06 R^2 + 30.354 R + 94.845

  ค่าที่พิมพ์ออกมา
    SpO2   เปอร์เซ็นต์ออกซิเจนในเลือด คนปกติอยู่ราว 95 ถึง 100
    BPM    อัตราการเต้นของหัวใจ
    R      ค่าอัตราส่วนดิบ เอาไว้สอบเทียบเองถ้าต้องการความแม่นยำสูงขึ้น
    PI     ดัชนีการไหลเวียน ยิ่งสูงยิ่งวางนิ้วได้ดี ต่ำกว่า 0.2% ควรขยับนิ้วใหม่

  คำเตือนสำคัญ
    ค่าที่ได้ยังไม่ผ่านการสอบเทียบกับเครื่องมือมาตรฐานทางการแพทย์
    ห้ามใช้ตัดสินใจเรื่องสุขภาพ ถ้ารู้สึกผิดปกติให้ไปพบแพทย์
    ถ้าต้องการปรับให้ตรงกับเครื่องอ้างอิงของคุณเอง ใช้ setCalibration()

  by Massmore  |  MIT License
*/

#include <Massmore_MAX3010x.h>
#include <Massmore_MAX3010x_Algorithms.h>
#include <Wire.h>

#define PIN_SDA 21
#define PIN_SCL 22

MassmoreMAX3010x sensor;
MassmoreMAX3010xSpO2 oximeter;

void setup() {
  Serial.begin(115200);
  delay(300);

  Serial.println();
  Serial.println("Massmore MAX3010x - 03 วัด SpO2 และชีพจร");
  Serial.println("=========================================");

  if (!sensor.begin(Wire, MASSMORE_MAX3010X_I2C_ADDRESS, PIN_SDA, PIN_SCL)) {
    Serial.print("เชื่อมต่อไม่สำเร็จ: ");
    Serial.println(sensor.lastErrorString());
    while (true) {
      delay(1000);
    }
  }

  /* SpO2 ต้องใช้ทั้งช่องแดงและช่องอินฟราเรด จึงต้องอยู่ในโหมด SPO2
     กระแส LED 0x24 (~7 mA) ให้สัญญาณแรงพอโดยไม่ทำให้ ADC อิ่มตัว */
  sensor.setup(0x24, MASSMORE_MAX3010X_SMP_AVE_8, MASSMORE_MAX3010X_MODE_SPO2,
               MASSMORE_MAX3010X_RATE_400, MASSMORE_MAX3010X_PULSE_411US,
               MASSMORE_MAX3010X_ADC_RANGE_4096);

  /* คำนวณผลใหม่ทุก 25 ตัวอย่าง = ทุกครึ่งวินาทีเมื่อข้อมูลออกมา 50 Hz */
  oximeter.begin(sensor.getEffectiveSampleRate(), 25);

  Serial.println("วางปลายนิ้วบนเซ็นเซอร์ รอประมาณ 5 วินาทีให้ข้อมูลเต็มหน้าต่าง");
  Serial.println();
}

void loop() {
  if (!sensor.update()) {
    return;
  }

  while (sensor.available()) {
    const uint32_t red = sensor.getRed();
    const uint32_t ir = sensor.getIR();
    sensor.nextSample();

    /* add() คืน true เมื่อคำนวณผลชุดใหม่เสร็จ */
    if (!oximeter.add(red, ir)) {
      continue;
    }

    const massmore_max3010x_spo2_result_t &r = oximeter.getResult();

    if (!r.fingerPresent) {
      Serial.println("ยังไม่พบนิ้วบนเซ็นเซอร์");
      continue;
    }

    if (!r.spo2Valid) {
      Serial.print("สัญญาณยังไม่นิ่ง  PI = ");
      Serial.print(r.perfusionIr, 2);
      Serial.println(" %  ลองขยับนิ้วให้คลุมหน้าต่างเซ็นเซอร์ให้มิดกว่านี้");
      continue;
    }

    Serial.print("SpO2 ");
    Serial.print(r.spo2, 1);
    Serial.print(" %   BPM ");
    if (r.heartRateValid) {
      Serial.print(r.heartRate, 1);
    } else {
      Serial.print("--");
    }
    Serial.print("   R ");
    Serial.print(r.ratio, 3);
    Serial.print("   PI ");
    Serial.print(r.perfusionIr, 2);
    Serial.println(" %");
  }
}
