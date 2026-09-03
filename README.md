<div align="center">

<img src="docs/images/01_massmore_max30102_cover.png" alt="Massmore MAX30102 Pulse Oximeter and Heart-Rate Sensor" width="480">

# Massmore_MAX3010x

**ไลบรารี Arduino / PlatformIO สำหรับเซ็นเซอร์วัดออกซิเจนในเลือดและการเต้นของหัวใจ**
ตระกูล MAX3010x ของ Analog Devices (Maxim)

บอร์ด Massmore MAX30102 · SKU-0026

[![build](https://github.com/Massmore/Massmore_MAX3010x_SKU-0026/actions/workflows/build.yml/badge.svg)](https://github.com/Massmore/Massmore_MAX3010x_SKU-0026/actions/workflows/build.yml)
![license](https://img.shields.io/badge/license-MIT-blue)
![version](https://img.shields.io/badge/version-1.0.0-green)
![arduino](https://img.shields.io/badge/Arduino%20ESP32-3.x-teal)
![platformio](https://img.shields.io/badge/PlatformIO-pioarduino%2055.03.311-orange)

</div>

---

## ไลบรารีตัวนี้ต่างจากตัวอื่นอย่างไร

เขียนขึ้นใหม่ทั้งหมดจาก datasheet ของผู้ผลิตโดยตรง ไม่ได้ดัดแปลงจากไลบรารีของใคร
ตั้งชื่อทุกอย่างขึ้นต้นด้วย `Massmore_` จึงติดตั้งอยู่ร่วมกับไลบรารี MAX3010x
ของเจ้าอื่นในเครื่องเดียวกันได้โดยไม่ชนกัน

| หัวข้อ | รายละเอียด |
|---|---|
| ครอบคลุมทุกฟังก์ชันของชิป | FIFO 32 ตัวอย่าง, อินเทอร์รัปต์ทุกชนิด, multi-LED 4 ช่องเวลา, อุณหภูมิแกนชิป, proximity, เขียนอ่านรีจิสเตอร์ตรง |
| ไม่ใช้ heap | ไม่มี `new` / `malloc` / `String` ในส่วนแกน ใช้แรมคงที่ ตรวจสอบได้ตั้งแต่ตอนคอมไพล์ |
| ตรวจชิปแท้ 11 ข้อ | `verifyChip()` ตรวจตั้งแต่รหัสรุ่น ค่าหลังรีเซ็ต บิตสงวน ไปจนถึงการตอบสนองของ ADC เมื่อเปิด LED |
| อัลกอริทึมพร้อมใช้ | จับชีพจรแบบเรียลไทม์ และคำนวณ SpO2 ด้วยวิธี ratio-of-ratios ปรับค่าสอบเทียบเองได้ |
| เดารุ่นชิปอัตโนมัติ | แยก MAX30102 / MAX30101 / MAX30105 ให้เอง ทั้งที่ทุกรุ่นคืน `PART_ID = 0x15` เหมือนกัน |
| ทดสอบจริง | ชุดทดสอบ 149 ข้อที่คอมไพล์และรันบนเครื่อง PC ได้โดยไม่ต้องมีบอร์ด |
| คอมเมนต์ภาษาไทยทั้งไฟล์ | อ่านแล้วต่อยอดได้ทันที ไม่ต้องเดาว่าโค้ดบรรทัดนั้นทำอะไร |

> [!WARNING]
> บอร์ดนี้เป็นอุปกรณ์สำหรับการเรียนรู้และงานสร้างต้นแบบ **ไม่ใช่เครื่องมือแพทย์**
> ค่าที่ได้ไม่ผ่านการสอบเทียบทางการแพทย์ ห้ามใช้วินิจฉัย ติดตามอาการ หรือรักษาโรค
> ถ้ารู้สึกผิดปกติให้ไปพบแพทย์ อย่าตัดสินใจจากตัวเลขที่อ่านได้จากบอร์ดนี้

---

## รุ่นชิปที่รองรับ

| ชิป | PART_ID | LED | proximity ในตัว | สถานะ |
|---|---|---|---|---|
| **MAX30102** | 0x15 | แดง + อินฟราเรด | ไม่มี | **รองรับเต็มรูปแบบ** (ชิปบนบอร์ด SKU-0026) |
| MAX30101 | 0x15 | แดง + IR + เขียว 2 ดวง | ไม่มี | รองรับ |
| MAX30105 | 0x15 | แดง + IR + เขียว | มี | รองรับ |
| MAX30100 | 0x11 | แดง + อินฟราเรด | ไม่มี | **ไม่รองรับ** |

MAX30100 ใช้ตารางรีจิสเตอร์คนละชุดกันโดยสิ้นเชิง (FIFO 16 บิต ที่อยู่รีจิสเตอร์คนละชุด)
ถ้าเผลอต่อ MAX30100 เข้ามา ไลบรารีจะแจ้ง `MASSMORE_MAX3010X_ERR_WRONG_CHIP`
ตั้งแต่ `begin()` แทนที่จะปล่อยให้อ่านค่าขยะออกมา

---

## สเปกบอร์ด

| หัวข้อ | ค่า |
|---|---|
| ชิป | MAX30102 (Analog Devices / Maxim) |
| ไฟเลี้ยง | 3.0 – 5.5 V (มีเรกูเลเตอร์และตัวแปลงระดับลอจิกบนบอร์ด) |
| ขาสัญญาณ | I2C ทนได้ทั้ง 3.3 V และ 5 V |
| ที่อยู่ I2C | `0x57` ตรึงจากโรงงาน เปลี่ยนไม่ได้ |
| ความถี่บัส | สูงสุด 400 kHz |
| ความยาวคลื่น LED | แดง 660 nm · อินฟราเรด 880 nm |
| ADC | 18 บิต อัตราสุ่ม 50 – 3200 ครั้งต่อวินาที |
| FIFO | 32 ตัวอย่าง |
| ขั้วต่อ | Qwiic / STEMMA QT และแถวพินระยะ 2.54 มม. |

---

## การต่อสาย

<div align="center">
<img src="docs/images/02_massmore_max30102_pinmap.png" alt="แผนผังขาของบอร์ด Massmore MAX30102" width="440">
</div>

| ขาบนบอร์ด | ทำอะไร |
|---|---|
| **VIN** | ไฟเลี้ยง 3 – 5.5 V |
| **3Vo** | ไฟ 3.3 V ที่เรกูเลเตอร์บนบอร์ดจ่ายออกมา (จ่ายให้อุปกรณ์อื่นได้เล็กน้อย) |
| **GND** | กราวด์ |
| **SCL** | สัญญาณนาฬิกา I2C |
| **SDA** | สัญญาณข้อมูล I2C |
| **INT** | อินเทอร์รัปต์ ชนิด open-drain ต้องเปิด pull-up ฝั่งไมโครคอนโทรลเลอร์ (ต่อหรือไม่ต่อก็ใช้งานได้) |

### ต่อกับ ESP32

<div align="center">
<img src="docs/images/03_massmore_max30102_wiring_esp32.png" alt="การต่อบอร์ด Massmore MAX30102 เข้ากับ ESP32" width="440">
</div>

| MAX30102 | ESP32 |
|---|---|
| VIN | 3V3 |
| GND | GND |
| SDA | GPIO 21 |
| SCL | GPIO 22 |
| INT | GPIO 4 (ใช้เฉพาะตัวอย่างที่ 07) |

ทุกตัวอย่างในรีโปนี้ตั้งค่าปริยายไว้ที่ **SDA = 21, SCL = 22** ตามบอร์ด
Massmore ESP32 Breakout ที่มีขั้ว Qwiic ถ้าใช้บอร์ดอื่นให้แก้สองบรรทัดบนสุดของตัวอย่าง

---

## ติดตั้ง

### Arduino IDE

1. ดาวน์โหลดรีโปนี้เป็น ZIP หรือ `git clone`
2. คัดลอกโฟลเดอร์ `ArduinoIDE/Massmore_MAX3010x` ทั้งโฟลเดอร์ไปวางไว้ที่

   | ระบบปฏิบัติการ | ตำแหน่ง |
   |---|---|
   | macOS | `~/Documents/Arduino/libraries/` |
   | Windows | `Documents\Arduino\libraries\` |
   | Linux | `~/Arduino/libraries/` |

3. ปิดแล้วเปิด Arduino IDE ใหม่
4. เปิดตัวอย่างจาก **File → Examples → Massmore_MAX3010x**

ต้องติดตั้ง **esp32 by Espressif Systems เวอร์ชัน 3.x** ใน Boards Manager ก่อน

### PlatformIO

เปิดโฟลเดอร์ `PlatformIO/` ด้วย VS Code ได้เลย ไลบรารีอยู่ใน `lib/` เรียบร้อยแล้ว
ไม่ต้องประกาศ `lib_deps` เพิ่ม

```bash
cd PlatformIO
cp examples/01_BasicReading/main.cpp src/main.cpp
pio run -t upload -t monitor
```

`platformio.ini` ตรึง platform ไว้ที่ **pioarduino 55.03.311** ซึ่งให้ Arduino ESP32 core 3.3.11
เท่ากับที่ Arduino IDE รุ่นล่าสุดใช้ จึงได้ผล build ที่ซ้ำได้เสมอ

---

## เริ่มใช้งานใน 15 บรรทัด

```cpp
#include <Massmore_MAX3010x.h>
#include <Wire.h>

MassmoreMAX3010x sensor;

void setup() {
  Serial.begin(115200);
  if (!sensor.begin(Wire, MASSMORE_MAX3010X_I2C_ADDRESS, 21, 22)) {
    Serial.println(sensor.lastErrorString());
    while (1) delay(1000);
  }
  sensor.setupDefault();          // โหมด SpO2, ข้อมูล 50 ชุดต่อวินาที
}

void loop() {
  if (sensor.update()) {
    while (sensor.available()) {
      Serial.println(sensor.getIR());
      sensor.nextSample();        // ห้ามลืมบรรทัดนี้
    }
  }
}
```

### วัดชีพจรและ SpO2

```cpp
#include <Massmore_MAX3010x_Algorithms.h>

MassmoreMAX3010xSpO2 oximeter;

// ใน setup() หลัง sensor.setupDefault()
oximeter.begin(sensor.getEffectiveSampleRate(), 25);

// ใน loop()
if (oximeter.add(sensor.getRed(), sensor.getIR())) {
  const auto &r = oximeter.getResult();
  if (r.spo2Valid) {
    Serial.printf("SpO2 %.1f %%  BPM %.1f  R %.3f\n", r.spo2, r.heartRate, r.ratio);
  }
}
```

---

## ตัวอย่างทั้ง 12 ชุด

ทุกตัวอย่างมีทั้งเวอร์ชัน Arduino IDE (`.ino`) และ PlatformIO (`main.cpp`)
เนื้อหาเหมือนกันทุกบรรทัด ต่างแค่ `#include <Arduino.h>` ที่ PlatformIO ต้องการ

| # | ตัวอย่าง | ระดับ | สิ่งที่ได้เรียนรู้ |
|---|---|---|---|
| 01 | **BasicReading** | พื้นฐาน | เชื่อมต่อ ตั้งค่า และอ่านค่าดิบช่องแดงกับอินฟราเรด |
| 02 | **HeartRate** | พื้นฐาน | จับจังหวะการเต้นของหัวใจแบบเรียลไทม์ พร้อมค่าเฉลี่ยที่นิ่งกว่า |
| 03 | **SpO2_HeartRate** | พื้นฐาน | คำนวณเปอร์เซ็นต์ออกซิเจนในเลือด พร้อมดัชนีการไหลเวียน |
| 04 | **PresenceDetect** | กลาง | ทำสวิตช์ไร้สัมผัสด้วยเส้นฐานปรับตัวเองและฮิสเทอรีซิส |
| 05 | **DieTemperature** | กลาง | อ่านอุณหภูมิแกนชิป ทั้งแบบบล็อกและแบบไม่บล็อก |
| 06 | **Configuration** | กลาง | ไล่ตั้งค่าทุกหัวข้อแล้ววัดอัตราข้อมูลจริงเทียบกับทฤษฎี |
| 07 | **Interrupt_FIFO** | กลาง | ใช้ขา INT แทนการวนถามชิป พร้อม ISR ที่เขียนถูกวิธี |
| 08 | **MultiLED_Green** | สูง | จัดช่องเวลาเองในโหมด multi-LED และใช้ LED สีเขียวบน MAX30105 |
| 09 | **LowPower_DeepSleep** | สูง | ประหยัดไฟ 3 ระดับ พร้อมเก็บผลข้ามการ deep sleep ด้วยหน่วยความจำ RTC |
| 10 | **ChipID_Genuine** | สูง | สแกนบัส I2C และตรวจว่าเป็นชิปแท้จากโรงงาน 11 ข้อ |
| 11 | **Advanced_AutoGain** | สูง | ปรับกระแส LED และช่วง ADC อัตโนมัติให้เข้ากับนิ้วของแต่ละคน |
| 12 | **FactoryTest** | สูง | ชุดทดสอบโรงงาน 26 หัวข้อ พร้อมบรรทัด `#RESULT` / `#VERDICT` ให้เครื่องอ่าน |

---

## สรุป API

### เริ่มต้นและตั้งค่า

```cpp
bool begin(TwoWire &wire = Wire, uint8_t address = 0x57,
           int8_t sdaPin = -1, int8_t sclPin = -1, uint32_t frequency = 400000);
bool setupDefault(uint8_t ledPowerLevel = 0x1F);
bool setup(ledPowerLevel, sampleAverage, mode, sampleRate, pulseWidth, adcRange);

bool isConnected();
bool softReset();
bool shutdown();
bool wakeUp();
```

### ปรับค่าทีละหัวข้อ

```cpp
bool setMode(mode);                       // HR / SPO2 / MULTI_LED
bool setSampleAverage(average);           // 1, 2, 4, 8, 16, 32
bool setSampleRate(rate);                 // 50 ถึง 3200 Hz
bool setPulseWidth(width);                // 69 / 118 / 215 / 411 us
bool setAdcRange(range);                  // 2048 / 4096 / 8192 / 16384 nA
bool setFifoRollover(bool enable);
bool setFifoAlmostFull(uint8_t spacesLeft);

bool setPulseAmplitudeRed(uint8_t value);
bool setPulseAmplitudeIR(uint8_t value);
bool setPulseAmplitudeGreen(uint8_t value);      // MAX30101 / MAX30105
bool setPulseAmplitudeProximity(uint8_t value);
bool setAllLedsOff();
bool setMultiLedSlot(uint8_t slotNumber, slot); // slot 1 ถึง 4
bool setProximityThreshold(uint8_t threshold);   // MAX30105
```

### อ่านข้อมูล

```cpp
bool update();                 // ดึงจาก FIFO ของชิปมาเก็บในบัฟเฟอร์ ไม่บล็อก
uint8_t available() const;     // เหลือกี่ชุดในบัฟเฟอร์
void nextSample();             // เลื่อนไปชุดถัดไป
uint32_t getRed() / getIR() / getGreen() const;
bool peekSample(massmore_max3010x_sample_t &out) const;
bool readSample(massmore_max3010x_sample_t &out, uint32_t timeoutMs = 200);
void flush();

uint8_t getSamplesInFifo();
uint8_t getOverflowCounter();
bool clearFifo();
```

### อินเทอร์รัปต์

```cpp
uint8_t getInterruptStatus1();   // อ่านแล้วเคลียร์แฟล็ก และปล่อยขา INT
uint8_t getInterruptStatus2();
bool enableInterruptAlmostFull(bool enable);
bool enableInterruptDataReady(bool enable);
bool enableInterruptAmbientLightOverflow(bool enable);
bool enableInterruptProximity(bool enable);
bool enableInterruptDieTemperature(bool enable);
bool disableAllInterrupts();
```

### อุณหภูมิ ตัวตนของชิป และงานขั้นสูง

```cpp
float readTemperature(uint32_t timeoutMs = 100);
bool startTemperatureConversion();
bool isTemperatureReady();
float getTemperatureResult();

uint8_t readPartID() / readRevisionID();
massmore_max3010x_variant_t getVariant() const;
const char *getVariantName() const;
bool hasGreenLed() / hasProximity() const;
massmore_max3010x_genuine_t verifyChip();
uint16_t getVerifyMask() const;
uint8_t getVerifyPassCount() const;

bool readConfiguration(massmore_max3010x_config_t &out);
float getEffectiveSampleRate() const;
static float ledCodeToMilliAmp(uint8_t code);
static uint8_t milliAmpToLedCode(float milliAmp);

bool readRegister8(uint8_t reg, uint8_t &value);
bool writeRegister8(uint8_t reg, uint8_t value);
bool maskRegister8(uint8_t reg, uint8_t mask, uint8_t value);
bool readRegisterBurst(uint8_t reg, uint8_t *buffer, uint8_t length);

massmore_max3010x_error_t lastError() const;
const char *lastErrorString() const;
```

---

## อัลกอริทึมที่ให้มาด้วย

อยู่ในไฟล์แยก `Massmore_MAX3010x_Algorithms.h` จะใช้หรือไม่ใช้ก็ได้
ถ้าไม่ `#include` ก็ไม่กินพื้นที่แฟลชเลย

### MassmoreMAX3010xBeatDetector — จับจังหวะการเต้นของหัวใจ

ทำงานทีละตัวอย่าง กินแรมไม่ถึง 100 ไบต์ ประมวลผล 4 ขั้น

1. ตัดองค์ประกอบ DC ทิ้งด้วยตัวกรองผ่านสูงที่ 0.5 Hz
2. กรองความถี่สูงทิ้งที่ 4 Hz ตัดสัญญาณรบกวนจากไฟบ้านและการขยับนิ้ว
3. ติดตามยอดและท้องคลื่นแบบปรับตัวเอง ทำให้เกณฑ์ขยับตามความแรงสัญญาณของแต่ละคน
4. นับเมื่อสัญญาณตัดเส้นเกณฑ์ขาขึ้น พร้อมช่วงห้ามนับซ้ำกัน dicrotic notch

ใช้การนับตัวอย่างแทน `millis()` จึงให้ผลเหมือนเดิมทุกครั้งเมื่อป้อนข้อมูลชุดเดียวกัน
ทดสอบกับคลื่นสังเคราะห์ 50 / 75 / 120 BPM คลาดเคลื่อนไม่เกิน 3 BPM

```cpp
MassmoreMAX3010xBeatDetector detector;
detector.begin(sensor.getEffectiveSampleRate());

if (detector.check(sensor.getIR())) {
  Serial.println(detector.getAverageBeatsPerMinute());
}
```

### MassmoreMAX3010xSpO2 — คำนวณออกซิเจนในเลือด

เก็บข้อมูลย้อนหลัง 200 ตัวอย่าง (4 วินาทีที่ 50 Hz) แล้วคำนวณด้วยวิธี ratio-of-ratios

```
R    = (AC แดง / DC แดง) / (AC อินฟราเรด / DC อินฟราเรด)
SpO2 = -45.060 R² + 30.354 R + 94.845
```

ส่วน AC วัดด้วยค่า RMS แทนการวัดยอดถึงท้อง เพราะจุดยอดเดียวที่โดนสัญญาณรบกวน
ทำให้ค่ายอดถึงท้องเพี้ยนได้ทั้งหน้าต่าง แต่ RMS เฉลี่ยความผิดพลาดออกไป

สมการนี้เป็นเส้นโค้งมาตรฐานที่ใช้กันทั่วไปในวงการ **ไม่ได้สอบเทียบกับบอร์ดของ Massmore
โดยเฉพาะ** ถ้าต้องการความแม่นยำจริงจังต้องเก็บข้อมูลเทียบกับเครื่องมาตรฐานเอง
แล้วแก้สัมประสิทธิ์ด้วย `setCalibration(a, b, c)`

ผลลัพธ์ที่ได้ครบชุด

```cpp
const massmore_max3010x_spo2_result_t &r = oximeter.getResult();
r.spo2            // เปอร์เซ็นต์ออกซิเจน
r.heartRate       // ครั้งต่อนาที
r.ratio           // ค่า R ดิบ เอาไว้สอบเทียบเอง
r.perfusionIr     // ดัชนีการไหลเวียน ยิ่งสูงยิ่งวางนิ้วได้ดี
r.spo2Valid       // เชื่อถือได้หรือไม่
r.fingerPresent   // มีนิ้ววางอยู่หรือไม่
```

---

## การตรวจสอบว่าเป็นชิปแท้

ในตลาดมีบอร์ด MAX30102 ราคาถูกจำนวนมากที่ใช้ชิปเกรดตกหรือชิปย้อมแมว
ซึ่งอ่านค่าเพี้ยนหรือพังเร็ว `verifyChip()` ตรวจ 11 ข้อ ทุกข้ออิงจากพฤติกรรม
ที่ระบุไว้ใน datasheet

| # | ข้อที่ตรวจ | จับอะไรได้ |
|---|---|---|
| 1 | ชิป ACK ที่ address 0x57 | ไม่มีอุปกรณ์ หรือสายขาด |
| 2 | `PART_ID` = 0x15 | ชิปคนละรุ่น เช่น MAX30100 |
| 3 | `REV_ID` ไม่ใช่ 0x00 และไม่ใช่ 0xFF | ของเลียนแบบที่ปล่อยรีวิชันว่างไว้ |
| 4 | บิต `RESET` เคลียร์ตัวเองได้ | วงจรรีเซ็ตภายในไม่ทำงาน |
| 5 | ค่าหลังรีเซ็ตตรงตารางใน datasheet | เฟิร์มแวร์จำลองที่ไม่ได้ทำตารางค่าเริ่มต้น |
| 6 | เขียนอ่านรีจิสเตอร์ได้ตรงทุกแพตเทิร์น | บัสมีสัญญาณรบกวน หรือรีจิสเตอร์ปลอม |
| 7 | `PART_ID` เขียนทับไม่ได้ | **ของเลียนแบบที่จำลองด้วยไมโครคอนโทรลเลอร์** |
| 8 | บิตสงวนใน `MODE_CONFIG` เป็นศูนย์เสมอ | **ของเลียนแบบที่เก็บทุกบิตที่เขียนลงไป** |
| 9 | ตัวชี้ FIFO เป็นฟิลด์ 5 บิต วนกลับที่ 32 | **ของเลียนแบบที่ทำตัวชี้เป็นไบต์เต็ม** |
| 10 | เซ็นเซอร์อุณหภูมิในตัวให้ค่าที่เป็นไปได้ | ไม่มีเซ็นเซอร์อุณหภูมิจริง |
| 11 | ADC ตอบสนองเมื่อเปิด LED | **ไม่มีภาคออปติกอยู่จริง มีแต่รีจิสเตอร์** |

ข้อที่ 7, 8, 9 และ 11 คือด่านที่ของเลียนแบบตกบ่อยที่สุด เพราะคนทำมักเขียนโปรแกรม
ตอบเฉพาะรีจิสเตอร์ที่นิยมอ่านเท่านั้น

```cpp
massmore_max3010x_genuine_t verdict = sensor.verifyChip();
// MASSMORE_MAX3010X_GENUINE_PASS      ผ่านครบ 11 ข้อ = ชิปแท้
// MASSMORE_MAX3010X_GENUINE_PARTIAL   ผ่าน 9 หรือ 10 ข้อ
// MASSMORE_MAX3010X_GENUINE_SUSPECT   ผ่านน้อยกว่านั้น น่าสงสัย
// MASSMORE_MAX3010X_GENUINE_NOT_MAX3010X  ไม่ใช่ชิปตระกูลนี้

for (uint8_t i = 0; i < MASSMORE_MAX3010X_CHK_COUNT; i++) {
  bool passed = sensor.getVerifyMask() & (1u << i);
  Serial.printf("%s %s\n", passed ? "ผ่าน" : "ไม่ผ่าน",
                MassmoreMAX3010x::getVerifyCheckName(i));
}
```

> `verifyChip()` จะรีเซ็ตชิปและเขียนทับค่าที่ตั้งไว้ทั้งหมด ต้องเรียก `setup()` ใหม่หลังตรวจเสร็จ

---

## เลือกค่าตั้งให้เหมาะกับงาน

| งาน | โหมด | อัตราสุ่ม | เฉลี่ย | พัลส์ | กระแส LED |
|---|---|---|---|---|---|
| วัดชีพจรที่ปลายนิ้ว | SPO2 | 400 Hz | 8 | 411 us | 0x1F (~6 mA) |
| วัด SpO2 ให้นิ่งที่สุด | SPO2 | 400 Hz | 8 | 411 us | 0x24 (~7 mA) |
| ตรวจจับวัตถุเข้าใกล้ | SPO2 (ปิดไฟแดง) | 400 Hz | 16 | 215 us | IR 0x3F |
| ดูรูปคลื่นละเอียด | SPO2 | 800 Hz | 1 | 215 us | 0x20 |
| ประหยัดไฟด้วยแบตเตอรี่ | SPO2 | 400 Hz | 8 | 411 us | 0x14 (~4 mA) |

**อัตราข้อมูลที่ออกจาก FIFO = อัตราสุ่ม ÷ จำนวนตัวอย่างที่เฉลี่ย**
เช่น 400 Hz เฉลี่ย 8 ตัว จะได้ข้อมูลออกมา 50 ชุดต่อวินาที

ความกว้างพัลส์กำหนดความละเอียด ADC ไปพร้อมกัน

| ความกว้างพัลส์ | ความละเอียด ADC |
|---|---|
| 69 us | 15 บิต |
| 118 us | 16 บิต |
| 215 us | 17 บิต |
| 411 us | 18 บิต |

ยิ่งอัตราสุ่มสูง ยิ่งต้องใช้พัลส์แคบลง เพราะชิปต้องยิง LED ให้ทันในแต่ละคาบ
ถ้าตั้งค่าที่เป็นไปไม่ได้ ชิปจะให้ข้อมูลออกมาช้ากว่าที่ตั้งไว้
ตัวอย่างที่ 06 มีโค้ดวัดอัตราข้อมูลจริงให้ตรวจสอบเองได้

---

## แก้ปัญหาที่พบบ่อย

<details>
<summary><b>begin() ล้มเหลว ขึ้นว่าไม่พบอุปกรณ์บนบัส I2C</b></summary>

1. ตรวจว่าเสียบสาย Qwiic แน่นทั้งสองฝั่ง หรือบัดกรีขาแน่นดีแล้ว
2. ตรวจว่ามีไฟเลี้ยงเข้าที่ขา VIN และต่อ GND ร่วมกัน
3. ตรวจว่า SDA และ SCL ไม่สลับกัน (SDA = GPIO 21, SCL = GPIO 22)
4. รันตัวอย่างที่ 10 ซึ่งจะสแกนบัสทั้งเส้นแล้วบอกว่าเจออุปกรณ์ที่ address ไหนบ้าง
5. ถ้าใช้สายยาวเกิน 30 ซม. ให้ลดความถี่บัสเป็น 100 kHz

</details>

<details>
<summary><b>begin() ขึ้นว่า PART_ID ไม่ใช่ 0x15</b></summary>

อ่านค่าที่ได้จริงด้วย `sensor.readPartID()`

- ได้ **0x11** แปลว่าเป็นชิป MAX30100 ซึ่งไลบรารีนี้ไม่รองรับ
- ได้ **0x00** หรือ **0xFF** แปลว่าสายสัญญาณมีปัญหา หรือชิปไม่มีไฟเลี้ยง
- ได้ค่าอื่น แปลว่าเป็นอุปกรณ์คนละตัวที่บังเอิญใช้ address 0x57 เหมือนกัน

</details>

<details>
<summary><b>ค่าที่อ่านได้ค้างที่ 262143 ไม่ขยับเลย</b></summary>

ADC อิ่มตัวแล้ว แสงสะท้อนกลับมาแรงเกินช่วงที่ตั้งไว้ แก้ได้สองทาง

```cpp
sensor.setPulseAmplitudeRed(0x0F);   // ลดกระแส LED ลง
sensor.setPulseAmplitudeIR(0x0F);
sensor.setAdcRange(MASSMORE_MAX3010X_ADC_RANGE_16384);  // หรือขยายช่วง ADC
```

ตัวอย่างที่ 11 ทำให้อัตโนมัติทั้งหมดแล้ว เอาไปใช้ต่อได้เลย

</details>

<details>
<summary><b>ค่าที่อ่านได้ต่ำมากทั้งที่วางนิ้วแล้ว</b></summary>

- กระแส LED ต่ำเกินไป ลองเพิ่มเป็น `0x3F` หรือมากกว่า
- วางนิ้วไม่คลุมหน้าต่างเซ็นเซอร์ ต้องปิดให้มิดทั้งดวง LED และโฟโตไดโอด
- กดนิ้วแรงเกินไปจนบีบหลอดเลือด ทำให้เลือดไม่ไหลผ่าน กดแค่แนบพอดี
- มือเย็นทำให้เลือดไปเลี้ยงปลายนิ้วน้อย ลองถูมือให้อุ่นก่อน

</details>

<details>
<summary><b>BPM กระโดดไปมา ไม่นิ่ง</b></summary>

- ใช้ `getAverageBeatsPerMinute()` แทน `getBeatsPerMinute()` ค่าแรกเฉลี่ย 6 ครั้งล่าสุด
- ตรวจว่าส่ง `sensor.getEffectiveSampleRate()` เข้า `begin()` ของตัวจับจังหวะแล้วหรือยัง
  ถ้าใส่ค่าผิด BPM จะเพี้ยนเป็นสัดส่วนทั้งหมด
- อยู่นิ่ง ๆ 10 วินาที การขยับนิ้วสร้างสัญญาณรบกวนแรงกว่าคลื่นชีพจรหลายเท่า
- ดูค่า `perfusionIr` ถ้าต่ำกว่า 0.2% แปลว่าสัญญาณอ่อนเกินไป

</details>

<details>
<summary><b>SpO2 ขึ้นว่ายังไม่เชื่อถือได้ตลอดเวลา</b></summary>

ไลบรารีจะตั้ง `spo2Valid = false` เมื่อเข้าเงื่อนไขข้อใดข้อหนึ่ง

- `dcIr` ต่ำกว่าเกณฑ์ว่ามีนิ้ว (ปรับด้วย `setFingerThreshold()`)
- ค่า R อยู่นอกช่วง 0.3 ถึง 3.0 ซึ่งเป็นไปไม่ได้ทางสรีรวิทยา
- `perfusionIr` ต่ำกว่า 0.05% แปลว่าคลื่นชีพจรจมอยู่ในสัญญาณรบกวน

ทั้งสามข้อนี้แก้ด้วยการวางนิ้วให้ดีขึ้นและปรับกระแส LED เป็นหลัก

</details>

<details>
<summary><b>อินเทอร์รัปต์บนขา INT เกิดครั้งเดียวแล้วเงียบไป</b></summary>

ขา INT ของชิปเป็นแบบ open-drain และ **จะไม่ปล่อยกลับขึ้นสูงจนกว่าจะมีการอ่าน
รีจิสเตอร์สถานะ** ต้องเรียก `getInterruptStatus1()` ทุกครั้งที่จัดการอินเทอร์รัปต์เสร็จ
และอย่าลืม `pinMode(PIN_INT, INPUT_PULLUP)`

</details>

<details>
<summary><b>อัปโหลดไม่ผ่าน ขึ้น Timed out waiting for packet header</b></summary>

ลดความเร็วอัปโหลดลง แก้บรรทัด `upload_speed` ใน `platformio.ini`
จาก `512000` เป็น `460800` หรือ `115200`

</details>

---

## ชุดทดสอบ

```bash
cd PlatformIO/test
make
```

รันได้บนเครื่อง PC ทันทีโดยไม่ต้องมีบอร์ด เพราะมีตัวจำลองชิป MAX30102 อยู่ในโฟลเดอร์
ซึ่งทำตามพฤติกรรมที่ระบุใน datasheet ครบ ทั้งค่าหลังรีเซ็ต รีจิสเตอร์อ่านอย่างเดียว
บิตสงวน ตัวชี้ FIFO 5 บิต การผลิตข้อมูลตามอัตราที่ตั้งไว้ และตัวนับ overflow

ทดสอบ 149 ข้อ ครอบคลุมการเริ่มต้นใช้งาน การตั้งค่า การอ่าน FIFO (รวมกรณี FIFO
เต็มจนล้น) เวลาประจำตัวอย่าง อุณหภูมิ การตรวจชิปแท้ (รวมถึงการจำลองของเลียนแบบ
4 แบบ) โหมดประหยัดไฟ อินเทอร์รัปต์ และอัลกอริทึมทั้งสองตัว

GitHub Actions จะรันชุดนี้พร้อมกับคอมไพล์ตัวอย่างทั้ง 12 ชุดด้วย Arduino CLI ทุกครั้งที่ push

---

## โครงสร้างรีโป

```
Massmore_MAX3010x_SKU-0026/
├── ArduinoIDE/
│   └── Massmore_MAX3010x/          คัดลอกทั้งโฟลเดอร์ไปวางใน libraries/
│       ├── src/                    ซอร์สของไลบรารี
│       ├── examples/               ตัวอย่าง 12 ชุด (.ino)
│       ├── library.properties
│       └── keywords.txt
├── PlatformIO/
│   ├── platformio.ini              ตรึง pioarduino 55.03.311 (ESP32 core 3.3.11)
│   ├── src/main.cpp                ที่สำหรับวางตัวอย่างที่จะ build
│   ├── lib/Massmore_MAX3010x/      ซอร์สชุดเดียวกับฝั่ง Arduino IDE
│   ├── examples/                   ตัวอย่าง 12 ชุด (main.cpp)
│   └── test/                       ชุดทดสอบที่รันบนเครื่อง PC
├── firmware/
│   └── esp32dev/                   เฟิร์มแวร์ Factory Test พร้อมอัปโหลด
├── docs/images/                    รูปประกอบ
└── README.md
```

---

## เอกสารอ้างอิง

- [MAX30102 Datasheet](https://www.analog.com/media/en/technical-documentation/data-sheets/MAX30102.pdf) — ชิปบนบอร์ดนี้
- [MAX30101 Datasheet](https://www.analog.com/media/en/technical-documentation/data-sheets/MAX30101.pdf)
- [MAX30105 Datasheet](https://www.analog.com/media/en/technical-documentation/data-sheets/MAX30105.pdf)
- [MAX30100 Datasheet](https://datasheets.maximintegrated.com/en/ds/MAX30100.pdf) — เพื่อเทียบให้เห็นว่าทำไมถึงรองรับไม่ได้

---

## สัญญาอนุญาต

MIT License · Copyright (c) 2026 Massmore Biz Co., Ltd.

นำไปใช้ ดัดแปลง และขายต่อได้ ขอแค่ติดประกาศลิขสิทธิ์ไว้

---

<div align="center">

**by Massmore** · [massmore.shop](https://www.massmore.shop)

ประกอบและทดสอบทุกบอร์ดในประเทศไทย

</div>
