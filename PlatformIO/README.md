# Massmore_MAX3010x — สำหรับ PlatformIO

เปิดโฟลเดอร์ **`PlatformIO`** นี้ด้วย VS Code ได้เลย (ต้องมีส่วนขยาย PlatformIO IDE)
ไลบรารีอยู่ใน `lib/` เรียบร้อยแล้ว ไม่ต้องประกาศ `lib_deps` เพิ่ม

## ใช้งาน

```bash
cd PlatformIO
cp examples/01_BasicReading/main.cpp src/main.cpp
pio run -t upload -t monitor
```

หรือเลือกตัวอย่างที่ต้องการจาก `examples/` แล้วคัดลอกทับ `src/main.cpp` จากนั้นกด Upload
บนแถบล่างของ VS Code

## บอร์ดที่เตรียม env ไว้ให้

| env | บอร์ด |
|---|---|
| `esp32dev` | ESP32 ตัวคลาสสิก · **ค่าปริยาย** |
| `esp32-s3-devkitc-1` | ESP32-S3 |
| `esp32-s2-saola-1` | ESP32-S2 |
| `esp32-c3-devkitm-1` | ESP32-C3 |
| `esp32-c6-devkitc-1` | ESP32-C6 |

```bash
pio run -e esp32-s3-devkitc-1 -t upload
```

## หมายเหตุเรื่องเวอร์ชัน

`platformio.ini` ตรึง platform ไว้ที่

```
https://github.com/pioarduino/platform-espressif32/releases/download/55.03.311/platform-espressif32.zip
```

เพราะ platform `espressif32` ตัวทางการยังส่ง Arduino core 2.0.x อยู่
ส่วน pioarduino fork ตัวนี้ให้ **Arduino ESP32 core 3.3.11** ซึ่งเท่ากับที่
Arduino IDE รุ่นล่าสุดใช้ และการตรึงเวอร์ชันไว้ทำให้ build ซ้ำได้ผลเหมือนเดิมเสมอ

## อัปโหลดไม่ผ่าน

แก้บรรทัดนี้ใน `platformio.ini`

```ini
upload_speed = 512000
```

เป็น `460800` หรือ `115200`

## ชุดทดสอบบนเครื่อง PC

```bash
cd test
make
```

คอมไพล์ด้วย g++ ธรรมดา รันได้ทันทีโดยไม่ต้องมีบอร์ด เพราะมีตัวจำลองชิป MAX30102
อยู่ในโฟลเดอร์ ทดสอบ 149 ข้อ ครอบคลุมตั้งแต่การอ่าน FIFO ไปจนถึงอัลกอริทึม SpO2

```bash
make build    # คอมไพล์อย่างเดียว
make clean    # ลบไฟล์ที่สร้างขึ้น
```

ซอร์สใน `lib/Massmore_MAX3010x/src/` เป็นชุดเดียวกับที่อยู่ใน
`ArduinoIDE/Massmore_MAX3010x/src/` แก้ที่ไหนก็ต้องซิงก์อีกฝั่งเสมอ

รายละเอียดทั้งหมดอยู่ใน [README หลักของรีโป](../README.md)
