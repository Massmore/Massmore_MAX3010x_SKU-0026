#!/bin/bash
# ============================================================================
#  Massmore MAX3010x (SKU-0026) - อัปโหลดเฟิร์มแวร์ Factory Test ลง ESP32
#  สำหรับ Linux
#
#  วิธีใช้      ./flash_linux.sh [พอร์ต]
#  ตัวอย่าง     ./flash_linux.sh /dev/ttyUSB0
#
#  ถ้าขึ้น Permission denied ให้เพิ่มตัวเองเข้ากลุ่ม dialout ก่อน
#      sudo usermod -a -G dialout $USER      แล้ว logout เข้าใหม่
# ============================================================================

cd "$(dirname "$0")" || exit 1

BAUD=512000
MERGED="Massmore_MAX3010x_FactoryTest_v1.0.0_esp32dev_merged.bin"

if command -v esptool.py >/dev/null 2>&1; then
  ESPTOOL="esptool.py"
elif command -v esptool >/dev/null 2>&1; then
  ESPTOOL="esptool"
elif python3 -c "import esptool" >/dev/null 2>&1; then
  ESPTOOL="python3 -m esptool"
else
  echo "ไม่พบ esptool  ติดตั้งด้วย: pip3 install esptool"
  exit 1
fi

PORT="$1"
if [ -z "$PORT" ]; then
  for p in /dev/ttyUSB* /dev/ttyACM*; do
    if [ -e "$p" ]; then
      PORT="$p"
      break
    fi
  done
fi

if [ -z "$PORT" ]; then
  echo "ไม่พบพอร์ต USB  ระบุเองได้ เช่น  ./flash_linux.sh /dev/ttyUSB0"
  exit 1
fi

echo "พอร์ต : $PORT   ความเร็ว : $BAUD"

$ESPTOOL --chip esp32 --port "$PORT" --baud $BAUD \
  --before default_reset --after hard_reset \
  write_flash -z --flash_mode dio --flash_freq 40m --flash_size detect \
  0x0 "$MERGED"

STATUS=$?
if [ $STATUS -eq 0 ]; then
  echo "อัปโหลดสำเร็จ  เปิด Serial Monitor ที่ 115200 เพื่อดูผล"
else
  echo "อัปโหลดไม่สำเร็จ (รหัส $STATUS) ลองลด BAUD เป็น 460800 หรือ 115200"
fi
exit $STATUS
