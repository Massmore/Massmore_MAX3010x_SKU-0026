/*
  ไฟล์นี้สร้างจากตัวอย่างชื่อเดียวกันในโฟลเดอร์ ArduinoIDE
  เนื้อหาเหมือนกันทุกบรรทัด ต่างแค่ #include <Arduino.h> ที่ PlatformIO ต้องการ
  วิธีใช้: คัดลอกไฟล์นี้ไปทับ PlatformIO/src/main.cpp แล้วกด Upload
*/

#include <Arduino.h>

/*
  06_Configuration - ตั้งค่าทุกหัวข้อของชิปแล้วอ่านกลับมาตรวจสอบ

  ตัวอย่างนี้เป็นเหมือน "แผงควบคุม" ให้เห็นว่าชิปตั้งอะไรได้บ้าง และการปรับ
  แต่ละอย่างมีผลต่ออัตราข้อมูลกับความละเอียดอย่างไร

  ความสัมพันธ์ที่ต้องเข้าใจให้ดี
    อัตราข้อมูลที่ออกจาก FIFO = อัตราสุ่ม / จำนวนตัวอย่างที่เฉลี่ย
    เช่น 400 Hz เฉลี่ย 8 ตัว จะได้ 50 ชุดต่อวินาที

    ความกว้างพัลส์กำหนดความละเอียด ADC ไปด้วย
      69 us  -> 15 บิต   118 us -> 16 บิต
      215 us -> 17 บิต   411 us -> 18 บิต

    ยิ่งอัตราสุ่มสูง ยิ่งต้องใช้พัลส์แคบลง เพราะชิปต้องยิง LED ให้ทันในแต่ละคาบ
    ถ้าตั้งค่าที่เป็นไปไม่ได้ ชิปจะให้ข้อมูลออกมาช้ากว่าที่ตั้งไว้
    ดูตารางความเข้ากันได้ในหัวข้อ SpO2 Configuration ของ datasheet

  ตัวอย่างนี้จะไล่ตั้งค่าหลายชุด แล้ววัดอัตราข้อมูลจริงที่วัดได้ให้ดู

  by Massmore  |  MIT License
*/

#include <Massmore_MAX3010x.h>
#include <Wire.h>

#define PIN_SDA 21
#define PIN_SCL 22

MassmoreMAX3010x sensor;

/* ชื่อค่าต่าง ๆ เป็นข้อความ ไว้พิมพ์ให้อ่านง่าย */
static const char *modeNames[] = {"HR (LED เดียว)", "SPO2 (แดง+IR)", "MULTI_LED"};
static const uint16_t rateValues[] = {50, 100, 200, 400, 800, 1000, 1600, 3200};
static const uint16_t pulseValues[] = {69, 118, 215, 411};
static const uint8_t adcBits[] = {15, 16, 17, 18};
static const uint16_t rangeValues[] = {2048, 4096, 8192, 16384};
static const uint8_t averageValues[] = {1, 2, 4, 8, 16, 32};

void printConfiguration() {
  massmore_max3010x_config_t cfg;
  if (!sensor.readConfiguration(cfg)) {
    Serial.print("อ่านการตั้งค่าไม่สำเร็จ: ");
    Serial.println(sensor.lastErrorString());
    return;
  }

  Serial.println("--------------------------------------------------");
  Serial.print("  ชิป            "); Serial.print(sensor.getVariantName());
  Serial.print("  PART_ID 0x"); Serial.print(cfg.partId, HEX);
  Serial.print("  REV_ID 0x"); Serial.println(cfg.revisionId, HEX);
  Serial.print("  โหมด           "); Serial.println(modeNames[(uint8_t)cfg.mode]);
  Serial.print("  เฉลี่ยตัวอย่าง  "); Serial.print(averageValues[(uint8_t)cfg.sampleAverage]);
  Serial.println(" ตัว");
  Serial.print("  อัตราสุ่ม       "); Serial.print(rateValues[(uint8_t)cfg.sampleRate]);
  Serial.println(" Hz");
  Serial.print("  ความกว้างพัลส์ "); Serial.print(pulseValues[(uint8_t)cfg.pulseWidth]);
  Serial.print(" us  -> ADC "); Serial.print(adcBits[(uint8_t)cfg.pulseWidth]);
  Serial.println(" บิต");
  Serial.print("  ช่วง ADC       "); Serial.print(rangeValues[(uint8_t)cfg.adcRange]);
  Serial.println(" nA");
  Serial.print("  FIFO rollover  "); Serial.println(cfg.fifoRollover ? "เปิด" : "ปิด");
  Serial.print("  FIFO A_FULL    เหลือที่ว่าง "); Serial.print(cfg.fifoAlmostFull);
  Serial.println(" ช่องจึงแจ้งเตือน");
  Serial.print("  กระแส LED      แดง "); Serial.print(MassmoreMAX3010x::ledCodeToMilliAmp(cfg.ledRed), 1);
  Serial.print(" mA   IR "); Serial.print(MassmoreMAX3010x::ledCodeToMilliAmp(cfg.ledIr), 1);
  Serial.println(" mA");
  Serial.print("  ช่องข้อมูล/ชุด  "); Serial.println(cfg.activeChannels);
  Serial.print("  อัตราข้อมูลจริง "); Serial.print(cfg.effectiveRateHz, 1);
  Serial.println(" ชุดต่อวินาที (ตามทฤษฎี)");
  Serial.println("--------------------------------------------------");
}

/* วัดอัตราข้อมูลจริงโดยนับตัวอย่างที่ได้จริงในเวลาที่กำหนด */
float measureActualRate(uint32_t durationMs) {
  sensor.clearFifo();
  delay(50);
  sensor.clearFifo();

  uint32_t counted = 0;
  const uint32_t start = millis();
  while (millis() - start < durationMs) {
    if (sensor.update()) {
      while (sensor.available()) {
        counted++;
        sensor.nextSample();
      }
    }
  }
  return (float)counted * 1000.0f / (float)durationMs;
}

void trySetting(const char *title, massmore_max3010x_smp_ave_t average,
                massmore_max3010x_rate_t rate,
                massmore_max3010x_pulse_width_t pulse) {
  Serial.println();
  Serial.print(">>> ");
  Serial.println(title);

  sensor.setSampleAverage(average);
  sensor.setSampleRate(rate);
  sensor.setPulseWidth(pulse);

  Serial.print("  ตามทฤษฎี ");
  Serial.print(sensor.getEffectiveSampleRate(), 1);
  Serial.print(" Hz   วัดจริงได้ ");
  Serial.print(measureActualRate(2000), 1);
  Serial.println(" Hz");
}

void setup() {
  Serial.begin(115200);
  delay(300);

  Serial.println();
  Serial.println("Massmore MAX3010x - 06 ตั้งค่าและตรวจสอบ");
  Serial.println("=========================================");

  if (!sensor.begin(Wire, MASSMORE_MAX3010X_I2C_ADDRESS, PIN_SDA, PIN_SCL)) {
    Serial.print("เชื่อมต่อไม่สำเร็จ: ");
    Serial.println(sensor.lastErrorString());
    while (true) {
      delay(1000);
    }
  }

  Serial.println();
  Serial.println("[1] ค่าหลังรีเซ็ต (ทุกอย่างเป็นศูนย์ ชิปยังไม่ทำงาน)");
  printConfiguration();

  Serial.println();
  Serial.println("[2] หลังเรียก setupDefault()");
  sensor.setupDefault(0x1F);
  printConfiguration();

  Serial.println();
  Serial.println("[3] ทดลองเปลี่ยนค่าแล้ววัดอัตราข้อมูลจริง");
  trySetting("ช้าและนิ่งที่สุด  50 Hz เฉลี่ย 32 ตัว", MASSMORE_MAX3010X_SMP_AVE_32,
             MASSMORE_MAX3010X_RATE_50, MASSMORE_MAX3010X_PULSE_411US);
  trySetting("มาตรฐาน  400 Hz เฉลี่ย 8 ตัว", MASSMORE_MAX3010X_SMP_AVE_8,
             MASSMORE_MAX3010X_RATE_400, MASSMORE_MAX3010X_PULSE_411US);
  trySetting("เร็ว  800 Hz เฉลี่ย 4 ตัว พัลส์ 215 us", MASSMORE_MAX3010X_SMP_AVE_4,
             MASSMORE_MAX3010X_RATE_800, MASSMORE_MAX3010X_PULSE_215US);
  trySetting("เร็วมาก  1600 Hz ไม่เฉลี่ย พัลส์ 69 us", MASSMORE_MAX3010X_SMP_AVE_1,
             MASSMORE_MAX3010X_RATE_1600, MASSMORE_MAX3010X_PULSE_69US);

  Serial.println();
  Serial.println("[4] เปรียบเทียบช่วง ADC โดยดูค่าที่อ่านได้จริง");
  sensor.setupDefault(0x3F);
  const uint16_t ranges[] = {2048, 4096, 8192, 16384};
  for (uint8_t i = 0; i < 4; i++) {
    sensor.setAdcRange((massmore_max3010x_adc_range_t)i);
    sensor.clearFifo();
    delay(200);
    sensor.update();

    uint32_t maxIr = 0;
    while (sensor.available()) {
      if (sensor.getIR() > maxIr) maxIr = sensor.getIR();
      sensor.nextSample();
    }
    Serial.print("  ช่วง ");
    Serial.print(ranges[i]);
    Serial.print(" nA  ค่าสูงสุดที่อ่านได้ ");
    Serial.print(maxIr);
    Serial.println(maxIr >= 262000UL ? "  <-- อิ่มตัวแล้ว ต้องขยายช่วง" : "");
  }

  Serial.println();
  Serial.println("[5] กลับไปใช้ค่ามาตรฐาน");
  sensor.setupDefault(0x1F);
  printConfiguration();
  Serial.println();
  Serial.println("จบการสาธิต จากนี้จะพิมพ์ค่าดิบไปเรื่อย ๆ");
}

void loop() {
  static uint32_t lastMs = 0;
  if (sensor.update()) {
    while (sensor.available()) {
      const uint32_t red = sensor.getRed();
      const uint32_t ir = sensor.getIR();
      sensor.nextSample();

      if (millis() - lastMs >= 200) {
        lastMs = millis();
        Serial.print("แดง ");
        Serial.print(red);
        Serial.print("   IR ");
        Serial.println(ir);
      }
    }
  }
}
