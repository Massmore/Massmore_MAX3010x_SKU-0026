# Massmore_MAX3010x — สำหรับ Arduino IDE

โฟลเดอร์นี้คือไลบรารีในรูปแบบที่ Arduino IDE ต้องการ

## ติดตั้ง

คัดลอกโฟลเดอร์ **`Massmore_MAX3010x`** ทั้งโฟลเดอร์ (ไม่ใช่โฟลเดอร์ `ArduinoIDE` ตัวนี้)
ไปวางไว้ที่

| ระบบปฏิบัติการ | ตำแหน่ง |
|---|---|
| macOS | `~/Documents/Arduino/libraries/` |
| Windows | `Documents\Arduino\libraries\` |
| Linux | `~/Arduino/libraries/` |

ผลลัพธ์ที่ถูกต้องจะเป็นแบบนี้

```
Arduino/libraries/Massmore_MAX3010x/
├── library.properties
├── keywords.txt
├── src/
└── examples/
```

ปิดแล้วเปิด Arduino IDE ใหม่ จากนั้นเปิดตัวอย่างได้จาก
**File → Examples → Massmore_MAX3010x**

## สิ่งที่ต้องมีก่อน

ติดตั้ง **esp32 by Espressif Systems เวอร์ชัน 3.x** ใน Boards Manager
(Tools → Board → Boards Manager แล้วค้นคำว่า esp32)

ไลบรารีนี้ไม่ต้องพึ่งไลบรารีอื่นเลยนอกจาก `Wire` ซึ่งมากับ core อยู่แล้ว

## ตั้งค่าก่อน Upload

| หัวข้อ | ค่า |
|---|---|
| Board | ESP32 Dev Module (หรือบอร์ดที่ใช้จริง) |
| Upload Speed | 512000 · ถ้าอัปโหลดไม่ผ่านให้ลด 460800 หรือ 115200 |
| Serial Monitor | 115200 |

## ตัวอย่าง

ทั้ง 12 ชุดตั้งค่าปริยายไว้ที่ **SDA = GPIO 21, SCL = GPIO 22**
ถ้าใช้บอร์ดอื่นให้แก้สองบรรทัดบนสุดของไฟล์

| # | ตัวอย่าง | เนื้อหา |
|---|---|---|
| 01 | BasicReading | อ่านค่าดิบช่องแดงและอินฟราเรด |
| 02 | HeartRate | จับจังหวะการเต้นของหัวใจ |
| 03 | SpO2_HeartRate | คำนวณออกซิเจนในเลือดพร้อมชีพจร |
| 04 | PresenceDetect | ตรวจจับวัตถุเข้าใกล้ |
| 05 | DieTemperature | อ่านอุณหภูมิแกนชิป |
| 06 | Configuration | ตั้งค่าทุกหัวข้อแล้ววัดอัตราข้อมูลจริง |
| 07 | Interrupt_FIFO | ใช้ขา INT แทนการวนถามชิป |
| 08 | MultiLED_Green | โหมด multi-LED และ LED สีเขียว |
| 09 | LowPower_DeepSleep | ประหยัดไฟและ deep sleep |
| 10 | ChipID_Genuine | สแกนบัสและตรวจชิปแท้ 11 ข้อ |
| 11 | Advanced_AutoGain | ปรับกระแส LED อัตโนมัติ |
| 12 | FactoryTest | ชุดทดสอบโรงงาน 26 หัวข้อ |

ซอร์สในโฟลเดอร์ `src/` เป็นชุดเดียวกับที่อยู่ใน `PlatformIO/lib/Massmore_MAX3010x/src/`
แก้ที่ไหนก็ต้องซิงก์อีกฝั่งเสมอ

รายละเอียดทั้งหมดอยู่ใน [README หลักของรีโป](../README.md)
