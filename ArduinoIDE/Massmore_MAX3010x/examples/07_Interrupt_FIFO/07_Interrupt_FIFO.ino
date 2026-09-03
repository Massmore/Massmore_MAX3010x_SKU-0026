/*
  07_Interrupt_FIFO - ใช้ขา INT แทนการวนถามชิปตลอดเวลา

  แทนที่จะเรียก update() รัว ๆ ให้ชิปเป็นฝ่ายบอกเราเองว่า "ข้อมูลใกล้เต็มแล้ว
  มาอ่านได้" ด้วยการดึงขา INT ลงต่ำ วิธีนี้ทำให้ CPU ว่างไปทำอย่างอื่นได้เยอะ
  และเหมาะกับงานที่ต้องประหยัดพลังงาน

  การต่อสาย เพิ่มจากตัวอย่างอื่นแค่เส้นเดียว
    INT -> GPIO 4   (ขา INT ของบอร์ดเป็นแบบ open-drain ต้องเปิด pull-up)

  วิธีทำงาน
    1. ตั้ง FIFO_A_FULL ว่าเหลือที่ว่างกี่ช่องจึงจะแจ้งเตือน
       ตั้ง 15 = แจ้งเมื่อมีข้อมูล 17 ตัวอย่าง  ตั้ง 0 = แจ้งเมื่อเต็ม 32 ตัวอย่าง
    2. เปิดอินเทอร์รัปต์ A_FULL
    3. ผูก ISR ไว้กับขา INT แบบ FALLING แล้วตั้งธงเท่านั้น ห้ามคุย I2C ใน ISR
    4. ใน loop() เห็นธงแล้วค่อยอ่าน FIFO และอ่านรีจิสเตอร์สถานะเพื่อเคลียร์แฟล็ก

  ข้อควรระวังสำคัญ
    ขา INT จะไม่ปล่อยกลับขึ้นสูงจนกว่าจะมีการ "อ่านรีจิสเตอร์สถานะ 0x00"
    ถ้าลืมอ่าน อินเทอร์รัปต์จะไม่เกิดอีกเลย

  by Massmore  |  MIT License
*/

#include <Massmore_MAX3010x.h>
#include <Wire.h>

#define PIN_SDA 21
#define PIN_SCL 22
#define PIN_INT 4

MassmoreMAX3010x sensor;

/* ธงที่ ISR ตั้ง ต้องเป็น volatile เพราะถูกแก้จากคนละบริบทกับ loop() */
static volatile bool interruptFlag = false;
static volatile uint32_t interruptCount = 0;

static uint32_t totalSamples = 0;
static uint32_t lastReportMs = 0;

/* ISR ต้องสั้นที่สุด ห้ามคุย I2C ห้าม Serial.print ห้าม delay
   IRAM_ATTR บน ESP32 บังคับให้โค้ดอยู่ในแรมภายใน เรียกได้แม้ตอนแฟลชไม่ว่าง */
#if defined(ESP32)
void IRAM_ATTR onSensorInterrupt() {
#else
void onSensorInterrupt() {
#endif
  interruptFlag = true;
  /* เขียนแบบอ่านแล้วบวกแล้วเขียนกลับ เพราะ ++ กับตัวแปร volatile
     ถูกประกาศเลิกใช้ใน C++20 ซึ่ง ESP32 core 3.x ใช้เป็นค่าปริยาย */
  interruptCount = interruptCount + 1;
}

void setup() {
  Serial.begin(115200);
  delay(300);

  Serial.println();
  Serial.println("Massmore MAX3010x - 07 อินเทอร์รัปต์และ FIFO");
  Serial.println("=============================================");

  if (!sensor.begin(Wire, MASSMORE_MAX3010X_I2C_ADDRESS, PIN_SDA, PIN_SCL)) {
    Serial.print("เชื่อมต่อไม่สำเร็จ: ");
    Serial.println(sensor.lastErrorString());
    while (true) {
      delay(1000);
    }
  }

  sensor.setupDefault(0x1F);

  /* แจ้งเตือนเมื่อ FIFO เหลือที่ว่าง 15 ช่อง = มีข้อมูลสะสม 17 ตัวอย่าง
     ที่ 50 ชุดต่อวินาที จะเกิดอินเทอร์รัปต์ราว 3 ครั้งต่อวินาที */
  sensor.setFifoAlmostFull(15);
  sensor.setFifoRollover(true);

  sensor.disableAllInterrupts();
  sensor.enableInterruptAlmostFull(true);

  /* อ่านสถานะทิ้งหนึ่งครั้งเพื่อเคลียร์แฟล็กที่ค้างอยู่ตั้งแต่ตอนเปิดเครื่อง */
  sensor.getInterruptStatus1();
  sensor.getInterruptStatus2();
  sensor.clearFifo();

  pinMode(PIN_INT, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(PIN_INT), onSensorInterrupt, FALLING);

  Serial.print("ผูกอินเทอร์รัปต์กับ GPIO ");
  Serial.println(PIN_INT);
  Serial.println("ถ้าไม่ได้ต่อสาย INT จะไม่มีอะไรเกิดขึ้น ให้ดูข้อความเตือนด้านล่าง");
  Serial.println();
}

void loop() {
  if (interruptFlag) {
    interruptFlag = false;

    /* ต้องอ่านรีจิสเตอร์สถานะ ไม่งั้นขา INT จะค้างต่ำตลอดไป */
    const uint8_t status = sensor.getInterruptStatus1();

    if (status & MASSMORE_MAX3010X_INT_A_FULL) {
      const uint8_t pending = sensor.getSamplesInFifo();
      sensor.update();

      uint32_t sumIr = 0;
      uint8_t got = 0;
      while (sensor.available()) {
        sumIr += sensor.getIR();
        got++;
        sensor.nextSample();
      }
      totalSamples += got;

      Serial.print("อินเทอร์รัปต์ A_FULL  มีค้างใน FIFO ");
      Serial.print(pending);
      Serial.print(" ตัวอย่าง  อ่านออกมาได้ ");
      Serial.print(got);
      Serial.print("  ค่าเฉลี่ย IR ");
      Serial.println(got > 0 ? (sumIr / got) : 0);
    }

    if (status & MASSMORE_MAX3010X_INT_ALC_OVF) {
      Serial.println("เตือน: วงจรตัดแสงรบกวนล้น แสงรอบข้างแรงเกินไป");
      Serial.println("       ลองบังแสง หรือลดกระแส LED ลง");
    }
  }

  /* รายงานภาพรวมทุก 5 วินาที และเตือนถ้าไม่มีอินเทอร์รัปต์เข้ามาเลย */
  if (millis() - lastReportMs >= 5000) {
    lastReportMs = millis();

    Serial.println();
    Serial.print("สรุป 5 วินาที: อินเทอร์รัปต์ ");
    Serial.print(interruptCount);
    Serial.print(" ครั้ง  ตัวอย่างสะสม ");
    Serial.print(totalSamples);
    Serial.print("  FIFO ล้นไป ");
    Serial.print(sensor.getOverflowCounter());
    Serial.println(" ตัวอย่าง");

    if (interruptCount == 0) {
      Serial.println("!! ยังไม่เกิดอินเทอร์รัปต์เลย ตรวจสอบว่า");
      Serial.print("   1. ต่อขา INT ของบอร์ดเข้ากับ GPIO ");
      Serial.println(PIN_INT);
      Serial.print("   2. ระดับลอจิกบนขา INT ขณะนี้คือ ");
      Serial.println(digitalRead(PIN_INT) ? "สูง (ปกติ)" : "ต่ำ (ค้าง)");
    }
    interruptCount = 0;
    Serial.println();
  }
}
