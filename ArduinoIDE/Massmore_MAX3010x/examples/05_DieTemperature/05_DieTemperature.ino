/*
  05_DieTemperature - อ่านอุณหภูมิแกนชิป ทั้งแบบบล็อกและไม่บล็อก

  ชิป MAX3010x มีเซ็นเซอร์อุณหภูมิอยู่ในตัว ความละเอียด 0.0625 องศา
  จุดประสงค์จริง ๆ ของมันคือให้เฟิร์มแวร์ชดเชยการเลื่อนของความยาวคลื่น LED
  ตามอุณหภูมิ ไม่ใช่ไว้วัดอุณหภูมิร่างกาย

  สิ่งที่ต้องเข้าใจ
    - ค่าที่ได้คืออุณหภูมิของ "ตัวชิป" ซึ่งจะสูงกว่าอุณหภูมิห้องอยู่หลายองศา
      เพราะ LED และวงจรภายในสร้างความร้อนเอง ยิ่งตั้งกระแส LED สูงยิ่งร้อน
    - ห้ามเอาไปใช้แทนเทอร์โมมิเตอร์วัดไข้เด็ดขาด
    - datasheet ระบุความแม่นยำไว้ที่ประมาณ +/- 1 องศา

  ตัวอย่างนี้แสดงสองวิธี
    วิธีที่ 1  readTemperature() แบบบล็อก เขียนง่าย รอไม่นาน เหมาะกับงานทั่วไป
    วิธีที่ 2  แบบไม่บล็อก สั่งวัดแล้วไปทำอย่างอื่นต่อ ค่อยกลับมาเก็บผล
              เหมาะกับงานที่ห้ามค้างแม้แต่มิลลิวินาทีเดียว

  by Massmore  |  MIT License
*/

#include <Massmore_MAX3010x.h>
#include <Wire.h>

#define PIN_SDA 21
#define PIN_SCL 22

MassmoreMAX3010x sensor;

/* สถานะของการวัดแบบไม่บล็อก */
static bool conversionRunning = false;
static uint32_t conversionStartMs = 0;
static uint32_t nextRequestMs = 0;

void setup() {
  Serial.begin(115200);
  delay(300);

  Serial.println();
  Serial.println("Massmore MAX3010x - 05 อุณหภูมิแกนชิป");
  Serial.println("======================================");

  if (!sensor.begin(Wire, MASSMORE_MAX3010X_I2C_ADDRESS, PIN_SDA, PIN_SCL)) {
    Serial.print("เชื่อมต่อไม่สำเร็จ: ");
    Serial.println(sensor.lastErrorString());
    while (true) {
      delay(1000);
    }
  }

  sensor.setupDefault(0x1F);

  /* ---- วิธีที่ 1 แบบบล็อก ---- */
  Serial.println();
  Serial.println("[วิธีที่ 1] อ่านแบบบล็อก");
  const uint32_t t0 = micros();
  const float celsius = sensor.readTemperature();
  const uint32_t elapsed = micros() - t0;

  if (isnan(celsius)) {
    Serial.print("อ่านไม่สำเร็จ: ");
    Serial.println(sensor.lastErrorString());
  } else {
    Serial.print("  อุณหภูมิ ");
    Serial.print(celsius, 2);
    Serial.print(" องศาเซลเซียส  (");
    Serial.print(celsius * 1.8f + 32.0f, 2);
    Serial.println(" องศาฟาเรนไฮต์)");
    Serial.print("  ใช้เวลา ");
    Serial.print(elapsed / 1000.0f, 2);
    Serial.println(" มิลลิวินาที ระหว่างนี้โปรแกรมทำอย่างอื่นไม่ได้เลย");
  }

  Serial.println();
  Serial.println("[วิธีที่ 2] อ่านแบบไม่บล็อก วัดใหม่ทุก 2 วินาที");
  Serial.println();
}

void loop() {
  const uint32_t now = millis();

  /* ถึงเวลาสั่งวัดรอบใหม่ */
  if (!conversionRunning && (int32_t)(now - nextRequestMs) >= 0) {
    if (sensor.startTemperatureConversion()) {
      conversionRunning = true;
      conversionStartMs = now;
    }
  }

  /* ระหว่างที่ชิปกำลังวัด เรายังอ่าน FIFO ต่อได้ตามปกติ ไม่มีอะไรค้าง */
  if (sensor.update()) {
    while (sensor.available()) {
      sensor.nextSample(); /* ตัวอย่างนี้ยังไม่ได้ใช้ค่า PPG จึงแค่ทิ้งไป */
    }
  }

  /* วนกลับมาถามว่าวัดเสร็จหรือยัง */
  if (conversionRunning && sensor.isTemperatureReady()) {
    conversionRunning = false;
    const float celsius = sensor.getTemperatureResult();

    Serial.print("อุณหภูมิแกนชิป ");
    Serial.print(celsius, 2);
    Serial.print(" องศาเซลเซียส   (ใช้เวลา ");
    Serial.print(now - conversionStartMs);
    Serial.println(" ms แต่ CPU ว่างตลอด)");

    nextRequestMs = now + 2000;
  }

  /* กันกรณีชิปไม่ตอบ จะได้ไม่ค้างอยู่ในสถานะกำลังวัดตลอดไป */
  if (conversionRunning && (now - conversionStartMs) > 200) {
    conversionRunning = false;
    nextRequestMs = now + 2000;
    Serial.println("วัดอุณหภูมิไม่สำเร็จ ข้ามรอบนี้ไปก่อน");
  }
}
