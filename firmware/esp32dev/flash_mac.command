#!/bin/bash
# ============================================================================
#  Massmore MAX3010x (SKU-0026) - อัปโหลดเฟิร์มแวร์ Factory Test ลง ESP32
#  สำหรับ macOS : ดับเบิลคลิกไฟล์นี้ได้เลย
#
#  ต้องมี esptool ก่อน ถ้ายังไม่มีให้พิมพ์
#      pip3 install esptool
# ============================================================================

cd "$(dirname "$0")" || exit 1

BAUD=512000
MERGED="Massmore_MAX3010x_FactoryTest_v1.0.0_esp32dev_merged.bin"

echo "=============================================="
echo " Massmore MAX3010x Factory Test - Flash Tool"
echo "=============================================="

# หา esptool ที่ใช้ได้
if command -v esptool.py >/dev/null 2>&1; then
  ESPTOOL="esptool.py"
elif command -v esptool >/dev/null 2>&1; then
  ESPTOOL="esptool"
elif python3 -c "import esptool" >/dev/null 2>&1; then
  ESPTOOL="python3 -m esptool"
else
  echo "ไม่พบ esptool"
  echo "ติดตั้งด้วยคำสั่ง:  pip3 install esptool"
  read -r -p "กด Enter เพื่อปิดหน้าต่าง"
  exit 1
fi

# หา serial port ของบอร์ด
PORT=""
for p in /dev/cu.usbserial-* /dev/cu.usbmodem* /dev/cu.SLAB_USBtoUART* /dev/cu.wchusbserial*; do
  if [ -e "$p" ]; then
    PORT="$p"
    break
  fi
done

if [ -z "$PORT" ]; then
  echo "ไม่พบพอร์ต USB ของบอร์ด"
  echo "  1. เสียบสาย USB ให้แน่น (ต้องเป็นสายที่ส่งข้อมูลได้ ไม่ใช่สายชาร์จอย่างเดียว)"
  echo "  2. ติดตั้งไดรเวอร์ CP210x หรือ CH34x ให้เรียบร้อย"
  read -r -p "กด Enter เพื่อปิดหน้าต่าง"
  exit 1
fi

echo "พอร์ตที่พบ : $PORT"
echo "ความเร็ว   : $BAUD"
echo ""

$ESPTOOL --chip esp32 --port "$PORT" --baud $BAUD \
  --before default_reset --after hard_reset \
  write_flash -z --flash_mode dio --flash_freq 40m --flash_size detect \
  0x0 "$MERGED"

STATUS=$?
echo ""
if [ $STATUS -eq 0 ]; then
  echo "อัปโหลดสำเร็จ"
  echo "เปิด Serial Monitor ที่ 115200 เพื่อดูผลการทดสอบ เช่น"
  echo "    screen $PORT 115200      (ออกด้วย Ctrl-A แล้ว K)"
else
  echo "อัปโหลดไม่สำเร็จ (รหัส $STATUS)"
  echo "ถ้าขึ้น Timed out waiting for packet header ให้ลดความเร็วเป็น 460800 หรือ 115200"
  echo "โดยแก้บรรทัด BAUD ด้านบนของไฟล์นี้"
fi
read -r -p "กด Enter เพื่อปิดหน้าต่าง"
