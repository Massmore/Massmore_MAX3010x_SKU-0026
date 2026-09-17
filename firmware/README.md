# Massmore_MAX3010x — Factory Test Firmware

Pre-compiled `05_Factory_Test` สำหรับตรวจบอร์ด **Massmore MAX30102 (SKU-0026)** ก่อนส่งลูกค้า
ไม่ต้องติดตั้ง Arduino IDE ไม่ต้อง compile — flash แล้วเปิด Serial Monitor ที่ **115200** ได้เลย

## Files

| File | Target | Flash offset | SHA-256 |
|---|---|---|---|
| `bin/Massmore_MAX3010x_FactoryTest_ESP32S3_merged.bin` | Massmore MOMO ESP32-S3 | `0x0` (bootloader + partitions + app) — **ใช้ไฟล์นี้** | `9ab69a8a…9fa23ab7` |
| `bin/Massmore_MAX3010x_FactoryTest_ESP32S3.bin` | Massmore MOMO ESP32-S3 | `0x10000` (app only) | `6e5837bc…fbb4c4c22` |

Build: pioarduino platform-espressif32 55.03.311 (Arduino-ESP32 Core 3.3.11), library 2.0.1,
env `massmore-momo-esp32s3` (USB CDC On Boot = off, Serial ออกทางชิป USB-UART)

ทดสอบบนฮาร์ดแวร์จริงแล้ว: Massmore MOMO ESP32-S3 (ESP32-S3 QFN56 rev v0.2, 16 MB flash, 8 MB PSRAM)
+ Massmore MAX30102 (PART_ID 0x15, REV_ID 0x06)

<!-- TODO: [MASSMORE_INPUT_REQUIRED: Classic ESP32 (esp32dev) binary — build ได้แล้วแต่ยังไม่ได้ทดสอบบนบอร์ดจริง จึงยังไม่แจก] -->

## Wiring

| MAX30102 | MOMO ESP32-S3 | Classic ESP32 | Required |
|---|---|---|---|
| VIN | 3V3 | 3V3 | ✅ |
| GND | GND | GND | ✅ |
| SDA | GPIO 14 | GPIO 21 | ✅ |
| SCL | GPIO 15 | GPIO 22 | ✅ |
| INT | ไม่ต่อ (`PIN_INT -1`) | GPIO 4 | สำหรับ `INT_PIN` test เท่านั้น |

I2C clock = **100 kHz** (บนบัส MOMO ที่มีหลายอุปกรณ์ 400 kHz ทำให้เกิด `ESP_ERR_INVALID_STATE`)

## Flashing

**esptool (macOS / Linux / Windows)**
```bash
pip3 install esptool
esptool.py --chip esp32s3 --port /dev/cu.usbmodemXXXX --baud 921600 \
  write_flash 0x0 bin/Massmore_MAX3010x_FactoryTest_ESP32S3_merged.bin
```
Windows: เปลี่ยน port เป็น `COM3` ฯลฯ — ถ้าขึ้น `Timed out waiting for packet header` ลด baud เป็น `512000` หรือ `460800`

**PlatformIO**
```bash
cd PlatformIO
pio run -e massmore-momo-esp32s3 -t upload -t monitor
```

**Web flasher** — ไฟล์ `_merged.bin` ใช้กับ ESP Web Tools (Chrome / Edge) ที่ offset `0x0`

## Serial output format (115200 baud)

บรรทัดที่ขึ้นต้นด้วย `#` ให้ **Massmore Web Serial Monitor** parse; บรรทัดอื่นเป็น human-readable

* `#RESULT <TEST_NAME> <PASS|FAIL> <value>` — หนึ่งบรรทัดต่อหนึ่งหัวข้อ
* `#VERDICT <PASS|FAIL> [<REASON>]` — บรรทัด `#` สุดท้าย
* บรรทัดปิด: `[PASS] SENSOR QA PASSED - READY TO SHIP` หรือ `[FAIL] QA CHECK FAILED: <REASON>`

| REASON | Meaning |
|---|---|
| `BUS_SCAN_NO_DEVICE` | ไม่พบ 0x57 บน I2C Bus — ตรวจ VIN / GND / SDA / SCL |
| `CHIP_ID_MISMATCH` | PART_ID ไม่ใช่ 0x15 (0x11 = MAX30100, 0x00/0xFF = สายสัญญาณ) |
| `BUS_ERROR` | ชิปตอบ ACK แต่ read/write ล้มเหลว — ลดความเร็ว I2C / ตรวจ pull-up |
| `REV_ID_INVALID` | REV_ID เป็น 0x00 หรือ 0xFF |
| `AUTHENTICITY_SUSPECT` | ตกข้อใดข้อหนึ่งใน 11 ข้อ — ดูรายการ `[FAIL]` |
| `TEMP_OUT_OF_RANGE` | Die temperature นอกช่วง -40..85 °C หรืออ่านไม่ได้ |
| `LED_NO_RESPONSE` | เปิด LED แล้ว ADC ไม่ขยับ — optical front-end เสีย |
| `RANGE_RED_INVALID` / `RANGE_IR_INVALID` | ค่าเฉลี่ยเป็น 0 หรือ saturate (262143) |
| `CONTINUOUS_TIMEOUT` / `CONTINUOUS_FAIL` | อ่าน 20 samples ไม่ครบ / ช้าเกิน 2 s / saturate |
| `INT_PIN_FAIL` | ขา INT ไม่ดึงต่ำ (`NO_ASSERT`) หรือไม่ปล่อยสูงหลังอ่าน status (`STUCK_LOW`) |

พิมพ์ `r` + Enter เพื่อทดสอบซ้ำโดยไม่ต้อง flash ใหม่

## Expected report (real passing board)

Massmore MOMO ESP32-S3 + Massmore MAX30102, flashed from `Massmore_MAX3010x_FactoryTest_ESP32S3_merged.bin` on 2026-09-17:

```
#MASSMORE_FACTORY_TEST v1.0
#PRODUCT Massmore_MAX3010x
#MCU ESP32-S3
#LIBRARY 2.0.1
I2C devices: 0x40 0x57 0x70
#RESULT BUS_SCAN PASS 0x57
#RESULT CHIP_ID PASS 0x15
#RESULT REV_ID PASS 0x06
Variant: MAX30102
Serial number: not available on MAX3010x
  [ok]   ACK at 0x57
  [ok]   PART_ID == 0x15
  [ok]   REV_ID valid
  [ok]   RESET self-clears
  [ok]   POR defaults
  [ok]   Register R/W
  [ok]   PART_ID read-only
  [ok]   Reserved bits zero
  [ok]   FIFO pointer 5-bit
  [ok]   Die temp sane
  [ok]   LED/ADC response
Checks passed: 11/11
#RESULT AUTHENTICITY PASS GENUINE
#RESULT RANGE_TEMP PASS 28.25
ADC mean dark=13 lit=79188
#RESULT LED_RESPONSE PASS 79175
#RESULT RANGE_RED PASS 58004
#RESULT RANGE_IR PASS 100373
Continuous: 20 ok, 0 timeout, 0 saturated, 401 ms, IR 101384-101423
#RESULT CONTINUOUS PASS 20/20
INT_PIN: skipped (PIN_INT = -1)
#VERDICT PASS
[PASS] SENSOR QA PASSED - READY TO SHIP
Type 'r' + Enter to run again.
```

ค่า `RANGE_*` และ `LED_RESPONSE` ขึ้นกับวัตถุที่อยู่หน้าเซ็นเซอร์ ค่าที่ต่างจากนี้ถือว่าปกติถ้ายัง PASS
