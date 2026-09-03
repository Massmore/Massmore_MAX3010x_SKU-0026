/*
  ไฟล์นี้สร้างจากตัวอย่างชื่อเดียวกันในโฟลเดอร์ ArduinoIDE
  เนื้อหาเหมือนกันทุกบรรทัด ต่างแค่ #include <Arduino.h> ที่ PlatformIO ต้องการ
  วิธีใช้: คัดลอกไฟล์นี้ไปทับ PlatformIO/src/main.cpp แล้วกด Upload
*/

#include <Arduino.h>

/*
  08_MultiLED_Green - โหมด multi-LED และการใช้ LED สีเขียว

  โหมด multi-LED ให้เรากำหนดเองได้ว่าในหนึ่งรอบการวัด ชิปจะยิง LED ดวงไหน
  บ้างและเรียงลำดับอย่างไร โดยแบ่งเป็น 4 ช่องเวลา (time slot)
  ข้อมูลที่ออกจาก FIFO จะเรียงตามลำดับช่องเวลาที่เปิดไว้

  ประโยชน์
    - MAX30105 / MAX30101 ใช้ LED สีเขียวได้ ซึ่งทนการขยับของนิ้วดีกว่า
      อินฟราเรดมาก จึงนิยมใช้ในนาฬิกาข้อมือสำหรับวัดชีพจรตอนออกกำลังกาย
    - จัดลำดับการยิงเองเพื่อลดสัญญาณรบกวนระหว่างช่อง
    - ใช้ค่ากระแสจากรีจิสเตอร์ PILOT_PA (slot แบบ pilot) เพื่อทำ proximity

  หมายเหตุสำหรับบอร์ด Massmore SKU-0026
    บอร์ดนี้ใช้ชิป MAX30102 ซึ่ง "ไม่มี" LED สีเขียว มีแค่แดงกับอินฟราเรด
    ตัวอย่างนี้จะตรวจรุ่นให้เองแล้วสาธิตเท่าที่ชิปทำได้
    ถ้าเป็น MAX30102 จะสาธิต multi-LED 2 ช่อง (แดง + IR)
    ถ้าเป็น MAX30105 จะสาธิต 3 ช่อง (แดง + IR + เขียว)

  by Massmore  |  MIT License
*/

#include <Massmore_MAX3010x.h>
#include <Wire.h>

#define PIN_SDA 21
#define PIN_SCL 22

MassmoreMAX3010x sensor;

static bool greenAvailable = false;

void setup() {
  Serial.begin(115200);
  delay(300);

  Serial.println();
  Serial.println("Massmore MAX3010x - 08 โหมด multi-LED");
  Serial.println("======================================");

  if (!sensor.begin(Wire, MASSMORE_MAX3010X_I2C_ADDRESS, PIN_SDA, PIN_SCL)) {
    Serial.print("เชื่อมต่อไม่สำเร็จ: ");
    Serial.println(sensor.lastErrorString());
    while (true) {
      delay(1000);
    }
  }

  greenAvailable = sensor.hasGreenLed();

  Serial.print("ชิปที่ตรวจพบ  ");
  Serial.println(sensor.getVariantName());
  Serial.print("มี LED สีเขียว  ");
  Serial.println(greenAvailable ? "มี" : "ไม่มี (เป็น MAX30102)");
  Serial.print("มี proximity ในตัว  ");
  Serial.println(sensor.hasProximity() ? "มี" : "ไม่มี");
  Serial.println();

  /* ตั้งค่าพื้นฐานก่อน โดยยังไม่เข้าโหมด multi-LED */
  sensor.setup(0x1F, MASSMORE_MAX3010X_SMP_AVE_4,
               MASSMORE_MAX3010X_MODE_SPO2, MASSMORE_MAX3010X_RATE_400,
               MASSMORE_MAX3010X_PULSE_215US,
               MASSMORE_MAX3010X_ADC_RANGE_4096);

  /* กำหนดว่าแต่ละช่องเวลาจะยิงดวงไหน
     ลำดับนี้จะเป็นลำดับของข้อมูลที่ออกมาจาก FIFO ด้วย */
  sensor.setMultiLedSlot(1, MASSMORE_MAX3010X_SLOT_RED);
  sensor.setMultiLedSlot(2, MASSMORE_MAX3010X_SLOT_IR);
  if (greenAvailable) {
    sensor.setMultiLedSlot(3, MASSMORE_MAX3010X_SLOT_GREEN);
    sensor.setPulseAmplitudeGreen(0x1F);
  } else {
    sensor.setMultiLedSlot(3, MASSMORE_MAX3010X_SLOT_NONE);
  }
  sensor.setMultiLedSlot(4, MASSMORE_MAX3010X_SLOT_NONE);

  /* เข้าโหมด multi-LED เป็นขั้นสุดท้าย ไลบรารีจะนับจำนวนช่องให้เอง */
  sensor.setMode(MASSMORE_MAX3010X_MODE_MULTI_LED);
  sensor.clearFifo();

  Serial.print("จำนวนช่องข้อมูลต่อ 1 ตัวอย่าง = ");
  Serial.println(sensor.getActiveChannels());
  Serial.println();
  if (greenAvailable) {
    Serial.println("แดง\tIR\tเขียว");
  } else {
    Serial.println("แดง\tIR");
  }
}

void loop() {
  static uint32_t lastMs = 0;

  if (!sensor.update()) {
    return;
  }

  while (sensor.available()) {
    const uint32_t red = sensor.getRed();
    const uint32_t ir = sensor.getIR();
    const uint32_t green = sensor.getGreen();
    sensor.nextSample();

    /* พิมพ์ทุก 100 ms พอ ไม่งั้นหน้าจอไหลเร็วเกินอ่าน */
    if (millis() - lastMs < 100) {
      continue;
    }
    lastMs = millis();

    Serial.print(red);
    Serial.print('\t');
    Serial.print(ir);
    if (greenAvailable) {
      Serial.print('\t');
      Serial.print(green);
    }
    Serial.println();
  }
}
