# Massmore_MAX3010x — Factory Test Firmware

Pre-compiled `05_Factory_Test` สำหรับตรวจบอร์ด **Massmore MAX30102 (SKU-0026)** ก่อนส่งลูกค้า
ไม่ต้องติดตั้ง Arduino IDE ไม่ต้อง compile — flash แล้วเปิด Serial Monitor ที่ **115200** ได้เลย

## Files

| File | Target | Flash offset |
|---|---|---|
| `bin/Massmore_MAX3010x_FactoryTest_ESP32.bin` | Classic ESP32 (esp32dev) | `0x10000` (app only) |
| `bin/Massmore_MAX3010x_FactoryTest_ESP32_merged.bin` | Classic ESP32 (esp32dev) | `0x0` (bootloader + partitions + app — **ใช้ไฟล์นี้**) |

Build: pioarduino platform-espressif32 55.03.311 (Arduino-ESP32 Core 3.3.11), library 2.0.0, flash `dio` / 40 MHz / 4 MB

<!-- TODO: [MASSMORE_INPUT_REQUIRED: binaries are generated only after a real hardware [PASS] — see §11 of the standard] -->

## Wiring (Primary test MCU = Classic ESP32)

| MAX30102 | ESP32 | Required |
|---|---|---|
| VIN | 3V3 | ✅ |
| GND | GND | ✅ |
| SDA | GPIO 21 | ✅ |
| SCL | GPIO 22 | ✅ |
| INT | GPIO 4 | ✅ for `INT_PIN` test (ตั้ง `PIN_INT -1` ใน sketch ถ้าไม่ต่อ) |

Qwiic cable เสียบเส้นเดียวได้ VIN / GND / SDA / SCL ครบ

## Flashing

**esptool (macOS / Linux / Windows)**
```bash
pip3 install esptool
esptool.py --chip esp32 --port /dev/cu.usbserial-XXXX --baud 921600 \
  --before default_reset --after hard_reset write_flash -z \
  --flash_mode dio --flash_freq 40m --flash_size detect \
  0x0 bin/Massmore_MAX3010x_FactoryTest_ESP32_merged.bin
```
Windows: เปลี่ยน port เป็น `COM3` ฯลฯ — ถ้าขึ้น `Timed out waiting for packet header` ลด baud เป็น `512000` หรือ `460800`

**PlatformIO**
```bash
cd PlatformIO
pio run -e esp32dev -t upload -t monitor
```

**Web flasher** — ไฟล์ `_merged.bin` ใช้กับ ESP Web Tools (Chrome / Edge) ที่ offset `0x0`

## Serial output format (115200 baud)

บรรทัดที่ขึ้นต้นด้วย `#` ให้ **Massmore Web Serial Monitor** parse; บรรทัดอื่นเป็น human-readable

```
#MASSMORE_FACTORY_TEST v1.0
#PRODUCT Massmore_MAX3010x
#MCU ESP32
#RESULT BUS_SCAN PASS 0x57
#RESULT CHIP_ID PASS 0x15
#RESULT REV_ID PASS 0x<value>
#RESULT AUTHENTICITY PASS GENUINE
#RESULT RANGE_TEMP PASS <deg C>
#RESULT LED_RESPONSE PASS <lit - dark counts>
#RESULT RANGE_RED PASS <mean>
#RESULT RANGE_IR PASS <mean>
#RESULT CONTINUOUS PASS 20/20
#RESULT INT_PIN PASS LOW_HIGH
#VERDICT PASS
[PASS] SENSOR QA PASSED - READY TO SHIP
```

กรณีไม่ผ่าน:
```
#RESULT CHIP_ID FAIL 0x00
#VERDICT FAIL CHIP_ID_MISMATCH
[FAIL] QA CHECK FAILED: CHIP_ID_MISMATCH
```

| REASON | Meaning |
|---|---|
| `BUS_SCAN_NO_DEVICE` | ไม่พบ 0x57 บน I2C Bus — ตรวจ VIN / GND / SDA / SCL |
| `CHIP_ID_MISMATCH` | PART_ID ไม่ใช่ 0x15 (0x11 = MAX30100, 0x00/0xFF = สายสัญญาณ) |
| `REV_ID_INVALID` | REV_ID เป็น 0x00 หรือ 0xFF |
| `AUTHENTICITY_SUSPECT` | ตกข้อใดข้อหนึ่งใน 11 ข้อ — ดูรายการ `[FAIL]` ด้านบน |
| `TEMP_OUT_OF_RANGE` | Die temperature นอกช่วง -40..85 °C หรืออ่านไม่ได้ |
| `LED_NO_RESPONSE` | เปิด LED แล้ว ADC ไม่ขยับ — optical front-end เสีย |
| `RANGE_RED_INVALID` / `RANGE_IR_INVALID` | ค่าเฉลี่ยเป็น 0 หรือ saturate (262143) |
| `CONTINUOUS_TIMEOUT` / `CONTINUOUS_FAIL` | อ่าน 20 samples ไม่ครบ / ช้าเกิน 2 s / saturate |
| `INT_PIN_FAIL` | ขา INT ไม่ดึงต่ำ (`NO_ASSERT`) หรือไม่ปล่อยสูงหลังอ่าน status (`STUCK_LOW`) |

พิมพ์ `r` + Enter เพื่อทดสอบซ้ำโดยไม่ต้อง flash ใหม่

## Expected report (real passing board)

<!-- TODO: [MASSMORE_INPUT_REQUIRED: paste verbatim Serial output from a real passing board after hardware test] -->

```
(pending hardware test)
```
