/*!
 * @file test_massmore_max3010x.cpp
 * @brief ชุดทดสอบไลบรารี Massmore_MAX3010x ที่รันบนเครื่อง PC ได้เลย
 *        ไม่ต้องมีบอร์ดจริง ใช้ตัวจำลองชิปในไฟล์ host shim
 *
 * วิธีรัน
 *     cd PlatformIO/test
 *     make
 *
 * @copyright Copyright (c) 2026 Massmore Biz Co., Ltd.
 * @license MIT
 */

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "../lib/Massmore_MAX3010x/src/Massmore_MAX3010x.h"
#include "../lib/Massmore_MAX3010x/src/Massmore_MAX3010x_Algorithms.h"
#include "massmore_max3010x_host_shim.h"

static int g_passed = 0;
static int g_failed = 0;

static void check(bool condition, const char *name) {
  if (condition) {
    g_passed++;
    printf("  [ผ่าน]   %s\n", name);
  } else {
    g_failed++;
    printf("  [ไม่ผ่าน] %s\n", name);
  }
}

static void checkNear(float actual, float expected, float tolerance,
                      const char *name) {
  const bool ok = fabsf(actual - expected) <= tolerance;
  if (ok) {
    g_passed++;
    printf("  [ผ่าน]   %s (ได้ %.3f คาดหวัง %.3f)\n", name, (double)actual,
           (double)expected);
  } else {
    g_failed++;
    printf("  [ไม่ผ่าน] %s (ได้ %.3f คาดหวัง %.3f)\n", name, (double)actual,
           (double)expected);
  }
}

static void section(const char *title) {
  printf("\n%s\n", title);
}

/* เตรียมชิปจำลองให้กลับสู่สภาพเริ่มต้นก่อนทุกหัวข้อ */
static void freshChip() {
  hostResetTime();
  g_mockChip.present = true;
  g_mockChip.partId = MASSMORE_MAX3010X_PART_ID_EXPECTED;
  g_mockChip.revisionId = 0x03;
  g_mockChip.partIdWritable = false;
  g_mockChip.reservedBitsStick = false;
  g_mockChip.pointerFullByte = false;
  g_mockChip.hasLed3 = false;
  g_mockChip.hasLed4 = false;
  g_mockChip.hasProximity = false;
  g_mockChip.dieTemperature = 28.5f;
  g_mockChip.reset();
}

/* ------------------------------------------------------------------ */

static void testBeginAndIdentity() {
  section("1. การเริ่มต้นใช้งานและรหัสประจำตัวชิป");

  freshChip();
  MassmoreMAX3010x sensor;
  check(sensor.begin(Wire, MASSMORE_MAX3010X_I2C_ADDRESS), "begin สำเร็จเมื่อมีชิปอยู่");
  check(sensor.isBegun(), "isBegun เป็นจริงหลัง begin");
  check(sensor.readPartID() == 0x15, "PART_ID อ่านได้ 0x15");
  check(sensor.readRevisionID() == 0x03, "REVISION_ID อ่านได้ตามที่จำลองไว้");
  check(sensor.getVariant() == MASSMORE_MAX3010X_VARIANT_MAX30102,
        "เดารุ่นได้เป็น MAX30102 เมื่อไม่มี LED3");
  check(!sensor.hasGreenLed(), "MAX30102 ไม่มี LED สีเขียว");
  check(!sensor.hasProximity(), "MAX30102 ไม่มี proximity");

  /* ไม่มีชิปบนบัส */
  freshChip();
  g_mockChip.present = false;
  MassmoreMAX3010x missing;
  check(!missing.begin(Wire), "begin ล้มเหลวเมื่อไม่มีชิป");
  check(missing.lastError() == MASSMORE_MAX3010X_ERR_NO_DEVICE,
        "รหัสข้อผิดพลาดเป็น NO_DEVICE");

  /* ชิปผิดรุ่น เช่น MAX30100 ที่ PART_ID = 0x11 */
  freshChip();
  g_mockChip.partId = MASSMORE_MAX3010X_PART_ID_MAX30100;
  MassmoreMAX3010x wrong;
  check(!wrong.begin(Wire), "begin ล้มเหลวเมื่อ PART_ID ไม่ใช่ 0x15");
  check(wrong.lastError() == MASSMORE_MAX3010X_ERR_WRONG_CHIP,
        "รหัสข้อผิดพลาดเป็น WRONG_CHIP สำหรับ MAX30100");

  /* จำลอง MAX30105 */
  freshChip();
  g_mockChip.hasLed3 = true;
  g_mockChip.hasProximity = true;
  MassmoreMAX3010x max30105;
  check(max30105.begin(Wire), "begin สำเร็จกับชิปที่มี LED3 และ proximity");
  check(max30105.getVariant() == MASSMORE_MAX3010X_VARIANT_MAX30105,
        "เดารุ่นได้เป็น MAX30105");
  check(max30105.hasGreenLed(), "MAX30105 มี LED สีเขียว");

  /* จำลอง MAX30101 มี LED3 และ LED4 แต่ตัดฟังก์ชัน proximity ออกไปแล้ว */
  freshChip();
  g_mockChip.hasLed3 = true;
  g_mockChip.hasLed4 = true;
  g_mockChip.hasProximity = false;
  MassmoreMAX3010x max30101;
  check(max30101.begin(Wire), "begin สำเร็จกับชิปที่มี LED3 อย่างเดียว");
  check(max30101.getVariant() == MASSMORE_MAX3010X_VARIANT_MAX30101,
        "เดารุ่นได้เป็น MAX30101 จากการมีไดรเวอร์ LED ช่องที่ 4");
  check(!max30101.hasProximity(), "MAX30101 ไม่มี proximity");
  check(!max30101.setPulseAmplitudeProximity(0x10),
        "ตั้งกระแส proximity ไม่ได้บน MAX30101");

  /* ระบุรุ่นเองต้องมีผลเหนือการเดา */
  freshChip();
  MassmoreMAX3010x forced;
  forced.setVariant(MASSMORE_MAX3010X_VARIANT_MAX30105);
  forced.begin(Wire);
  check(forced.getVariant() == MASSMORE_MAX3010X_VARIANT_MAX30105,
        "setVariant มีผลเหนือการเดารุ่นอัตโนมัติ");
  check(strcmp(forced.getVariantName(), "MAX30105") == 0,
        "getVariantName คืนชื่อรุ่นถูกต้อง");
}

static void testConfiguration() {
  section("2. การตั้งค่าและการอ่านกลับ");

  freshChip();
  MassmoreMAX3010x sensor;
  sensor.begin(Wire);

  check(sensor.setup(0x33, MASSMORE_MAX3010X_SMP_AVE_16,
                     MASSMORE_MAX3010X_MODE_SPO2, MASSMORE_MAX3010X_RATE_200,
                     MASSMORE_MAX3010X_PULSE_118US,
                     MASSMORE_MAX3010X_ADC_RANGE_8192),
        "setup สำเร็จ");

  massmore_max3010x_config_t cfg;
  check(sensor.readConfiguration(cfg), "readConfiguration สำเร็จ");
  check(cfg.mode == MASSMORE_MAX3010X_MODE_SPO2, "อ่านโหมดกลับได้ตรง");
  check(cfg.sampleAverage == MASSMORE_MAX3010X_SMP_AVE_16, "อ่านการเฉลี่ยกลับได้ตรง");
  check(cfg.sampleRate == MASSMORE_MAX3010X_RATE_200, "อ่านอัตราสุ่มกลับได้ตรง");
  check(cfg.pulseWidth == MASSMORE_MAX3010X_PULSE_118US,
        "อ่านความกว้างพัลส์กลับได้ตรง");
  check(cfg.adcRange == MASSMORE_MAX3010X_ADC_RANGE_8192, "อ่านช่วง ADC กลับได้ตรง");
  check(cfg.ledRed == 0x33 && cfg.ledIr == 0x33, "อ่านกระแส LED กลับได้ตรง");
  check(cfg.fifoRollover, "setup เปิด FIFO rollover ให้อัตโนมัติ");
  check(cfg.activeChannels == 2, "โหมด SPO2 มี 2 ช่องข้อมูล");
  checkNear(cfg.effectiveRateHz, 200.0f / 16.0f, 0.01f,
            "อัตราข้อมูลจริง = อัตราสุ่ม หารด้วยจำนวนที่เฉลี่ย");

  /* โหมด HR มีช่องเดียว */
  sensor.setMode(MASSMORE_MAX3010X_MODE_HR);
  check(sensor.getActiveChannels() == 1, "โหมด HR มี 1 ช่องข้อมูล");

  /* พารามิเตอร์นอกช่วงต้องถูกปฏิเสธ */
  check(!sensor.setFifoAlmostFull(16), "setFifoAlmostFull ปฏิเสธค่าเกิน 15");
  check(sensor.lastError() == MASSMORE_MAX3010X_ERR_BAD_ARG,
        "รหัสข้อผิดพลาดเป็น BAD_ARG");
  check(!sensor.setMultiLedSlot(0, MASSMORE_MAX3010X_SLOT_RED),
        "setMultiLedSlot ปฏิเสธหมายเลขช่อง 0");
  check(!sensor.setMultiLedSlot(5, MASSMORE_MAX3010X_SLOT_RED),
        "setMultiLedSlot ปฏิเสธหมายเลขช่อง 5");
  check(!sensor.setPulseAmplitudeGreen(0x10),
        "ตั้งกระแส LED เขียวไม่ได้บนชิปที่ไม่มี LED เขียว");
  check(sensor.lastError() == MASSMORE_MAX3010X_ERR_UNSUPPORTED,
        "รหัสข้อผิดพลาดเป็น UNSUPPORTED");

  /* ช่องเวลาของโหมด multi-LED ต้องเข้ารหัสลงรีจิสเตอร์ถูกตำแหน่ง */
  sensor.setMultiLedSlot(1, MASSMORE_MAX3010X_SLOT_RED);
  sensor.setMultiLedSlot(2, MASSMORE_MAX3010X_SLOT_IR);
  sensor.setMultiLedSlot(3, MASSMORE_MAX3010X_SLOT_NONE);
  sensor.setMultiLedSlot(4, MASSMORE_MAX3010X_SLOT_NONE);
  const uint8_t slotReg1 = sensor.readRegister8(MASSMORE_MAX3010X_REG_MULTI_LED_1);
  const uint8_t slotReg2 = sensor.readRegister8(MASSMORE_MAX3010X_REG_MULTI_LED_2);
  check(slotReg1 == 0x21, "ช่องเวลา 1 และ 2 เข้ารหัสเป็น 0x21");
  check(slotReg2 == 0x00, "ช่องเวลา 3 และ 4 ปิดอยู่");

  sensor.setMode(MASSMORE_MAX3010X_MODE_MULTI_LED);
  check(sensor.getActiveChannels() == 2, "โหมด multi-LED นับช่องที่เปิดได้ถูก");

  /* แปลงหน่วยกระแส */
  checkNear(MassmoreMAX3010x::ledCodeToMilliAmp(0x32), 10.0f, 0.01f,
            "แปลงค่ารีจิสเตอร์ 0x32 เป็น 10 mA");
  check(MassmoreMAX3010x::milliAmpToLedCode(10.0f) == 0x32,
        "แปลง 10 mA กลับเป็น 0x32");
  check(MassmoreMAX3010x::milliAmpToLedCode(999.0f) == 0xFF,
        "ค่ากระแสเกินพิกัดถูกจำกัดที่ 0xFF");
  check(MassmoreMAX3010x::milliAmpToLedCode(-1.0f) == 0x00,
        "ค่ากระแสติดลบกลายเป็น 0");
}

static void testFifo() {
  section("3. การอ่าน FIFO");

  freshChip();
  MassmoreMAX3010x sensor;
  sensor.begin(Wire);
  /* 400 Hz เฉลี่ย 8 ตัว = 50 ชุดต่อวินาที */
  sensor.setup(0x10, MASSMORE_MAX3010X_SMP_AVE_8, MASSMORE_MAX3010X_MODE_SPO2,
               MASSMORE_MAX3010X_RATE_400, MASSMORE_MAX3010X_PULSE_411US,
               MASSMORE_MAX3010X_ADC_RANGE_4096);

  check(!sensor.update(), "update คืน false เมื่อ FIFO ยังว่าง");
  check(sensor.lastError() == MASSMORE_MAX3010X_ERR_NO_DATA,
        "รหัสข้อผิดพลาดเป็น NO_DATA");

  hostAdvanceTime(200); /* ควรได้ราว 10 ตัวอย่าง */
  check(sensor.getSamplesInFifo() == 10, "ชิปผลิตข้อมูลได้ 10 ชุดใน 200 ms");
  check(sensor.update(), "update ดึงข้อมูลออกมาได้");
  check(sensor.available() == 10, "บัฟเฟอร์ของไลบรารีมีข้อมูล 10 ชุด");

  /* ค่าที่ได้ต้องตรงกับที่ตัวจำลองผลิต คือ 60 + กระแส LED x 300 */
  const uint32_t expected = 60 + 0x10 * 300;
  check(sensor.getRed() == expected, "ค่าช่องแดงถูกต้อง");
  check(sensor.getIR() == expected, "ค่าช่องอินฟราเรดถูกต้อง");
  check(sensor.getGreen() == 0, "โหมด SPO2 ไม่มีข้อมูลช่องเขียว");

  sensor.nextSample();
  check(sensor.available() == 9, "nextSample ลดจำนวนที่เหลือลง 1");

  sensor.flush();
  check(sensor.available() == 0, "flush ล้างบัฟเฟอร์หมด");
  check(sensor.getRed() == 0, "อ่านค่าจากบัฟเฟอร์ว่างได้ 0");

  /* โหมด HR มีช่องเดียว ข้อมูลต้องลงช่องแดง */
  sensor.setup(0x20, MASSMORE_MAX3010X_SMP_AVE_1, MASSMORE_MAX3010X_MODE_HR,
               MASSMORE_MAX3010X_RATE_100, MASSMORE_MAX3010X_PULSE_411US,
               MASSMORE_MAX3010X_ADC_RANGE_4096);
  hostAdvanceTime(100);
  sensor.update();
  check(sensor.available() > 0, "โหมด HR ให้ข้อมูลออกมา");
  check(sensor.getRed() == 60 + 0x20 * 300, "โหมด HR ข้อมูลอยู่ในช่องแดง");
  check(sensor.getIR() == 0, "โหมด HR ไม่มีข้อมูลช่องอินฟราเรด");

  /* readSample แบบบล็อก */
  sensor.flush();
  massmore_max3010x_sample_t sample;
  hostAdvanceTime(50);
  check(sensor.readSample(sample), "readSample อ่านได้หนึ่งชุด");
  check(sample.red == 60 + 0x20 * 300, "ค่าใน readSample ถูกต้อง");

  /* clearFifo ต้องล้างตัวชี้ทั้งหมด */
  hostAdvanceTime(100);
  check(sensor.clearFifo(), "clearFifo สำเร็จ");
  check(sensor.getWritePointer() == 0 && sensor.getReadPointer() == 0,
        "ตัวชี้ FIFO เป็นศูนย์หลัง clearFifo");
  check(sensor.getSamplesInFifo() == 0, "ไม่มีข้อมูลค้างหลัง clearFifo");

  /* ตัวนับ overflow ทำงานเมื่อปิด rollover */
  sensor.setup(0x10, MASSMORE_MAX3010X_SMP_AVE_1, MASSMORE_MAX3010X_MODE_SPO2,
               MASSMORE_MAX3010X_RATE_800, MASSMORE_MAX3010X_PULSE_215US,
               MASSMORE_MAX3010X_ADC_RANGE_4096);
  sensor.setFifoRollover(false);
  sensor.clearFifo();
  hostAdvanceTime(200); /* 800 Hz นาน 200 ms = 160 ตัวอย่าง มากกว่า FIFO 32 ช่อง */
  check(sensor.getOverflowCounter() > 0, "ตัวนับ overflow เดินขึ้นเมื่อ FIFO ล้น");
}

/* กรณีที่ FIFO เต็มพอดี 32 ตัวอย่าง ตัวชี้อ่านกับเขียนจะชี้ที่เดียวกัน
   เหมือนตอน FIFO ว่างสนิท ต้องแยกให้ออก ไม่งั้นข้อมูลทั้ง FIFO จะหายไปทั้งชุด */
static void testFifoFullBoundary() {
  section("3.1 FIFO เต็มจนล้น");

  freshChip();
  MassmoreMAX3010x sensor;
  sensor.begin(Wire);
  sensor.setup(0x10, MASSMORE_MAX3010X_SMP_AVE_1, MASSMORE_MAX3010X_MODE_SPO2,
               MASSMORE_MAX3010X_RATE_100, MASSMORE_MAX3010X_PULSE_411US,
               MASSMORE_MAX3010X_ADC_RANGE_4096);
  sensor.setFifoRollover(true);
  sensor.clearFifo();

  /* 100 Hz นาน 500 ms = 50 ตัวอย่าง มากกว่าความลึก FIFO 32 ช่อง
     สถานการณ์นี้เกิดจริงเมื่อ loop() ช้ากว่าปกติ เช่นไปทำงานอื่นค้างไว้ */
  hostAdvanceTime(500);

  check(g_mockChip.fifoCount == 32, "ชิปจำลองมีข้อมูลเต็ม FIFO 32 ตัวอย่าง");
  check(g_mockChip.writePointer == g_mockChip.readPointer,
        "ตัวชี้อ่านและเขียนชี้ที่เดียวกันเหมือนตอน FIFO ว่าง");
  check(sensor.getOverflowCounter() > 0, "ตัวนับ overflow เดินขึ้นแล้ว");
  check(sensor.getSamplesInFifo() == 32,
        "getSamplesInFifo แยกออกว่าเต็ม 32 ไม่ใช่ว่าง 0");

  check(sensor.update(), "update ยังดึงข้อมูลออกมาได้ทั้งที่ FIFO ล้น");
  check(sensor.available() == 32, "ได้ข้อมูลครบ 32 ชุด");
  const uint32_t expected = 60 + 0x10 * 300;
  check(sensor.getIR() == expected, "ค่าที่ได้ถูกต้อง ไม่ใช่ข้อมูลขยะ");

  while (sensor.available()) {
    sensor.nextSample();
  }

  /* หลังกวาดข้อมูลออกหมด ต้องล้างตัวนับ overflow ไม่งั้นรอบถัดไปที่ FIFO
     ว่างจริงจะถูกเข้าใจผิดว่าเต็มอีกครั้งแล้วอ่านข้อมูลขยะออกมา */
  check(sensor.getOverflowCounter() == 0, "ตัวนับ overflow ถูกล้างหลังกวาดข้อมูล");
  check(sensor.getSamplesInFifo() == 0, "FIFO ว่างจริงหลังอ่านหมด");
  check(!sensor.update(), "update คืน false เมื่อ FIFO ว่างจริง");

  /* เดินเวลาต่ออีกครั้ง ต้องกลับมาอ่านได้ตามปกติ */
  hostAdvanceTime(100);
  check(sensor.update(), "กลับมาอ่านข้อมูลได้ตามปกติหลังจากนั้น");
  check(sensor.available() == 10, "ได้ข้อมูล 10 ชุดตามอัตราที่ตั้งไว้");
}

/* ค่า MODE 0 หลังรีเซ็ตเป็นค่าที่ datasheet ห้ามใช้ ไลบรารีต้องไม่เดาเป็น SPO2
   ไม่งั้นจะแกะ FIFO ผิดความยาวเมื่อชิปเริ่มทำงาน */
static void testConfigurationBeforeSetup() {
  section("3.2 อ่านการตั้งค่าก่อนที่ชิปจะถูกตั้งโหมด");

  freshChip();
  MassmoreMAX3010x sensor;
  sensor.begin(Wire);

  check(sensor.getActiveChannels() == 1, "หลัง begin ไลบรารีถือว่ามี 1 ช่องข้อมูล");

  massmore_max3010x_config_t cfg;
  check(sensor.readConfiguration(cfg), "readConfiguration สำเร็จ");
  check(cfg.mode == MASSMORE_MAX3010X_MODE_HR,
        "ไม่เดาเป็น SPO2 ทั้งที่ชิปยังไม่ได้ตั้งโหมด");
  check(sensor.getActiveChannels() == 1,
        "จำนวนช่องข้อมูลที่จำไว้ไม่ถูกเปลี่ยนเป็น 2");

  /* ตั้งโหมดจริงแล้วต้องอ่านกลับได้ถูกต้องตามปกติ */
  sensor.setup(0x10, MASSMORE_MAX3010X_SMP_AVE_1, MASSMORE_MAX3010X_MODE_SPO2,
               MASSMORE_MAX3010X_RATE_100, MASSMORE_MAX3010X_PULSE_411US,
               MASSMORE_MAX3010X_ADC_RANGE_4096);
  sensor.readConfiguration(cfg);
  check(cfg.mode == MASSMORE_MAX3010X_MODE_SPO2, "ตั้งโหมดแล้วอ่านกลับได้ตรง");
  check(sensor.getActiveChannels() == 2, "จำนวนช่องข้อมูลเป็น 2 ตามโหมด SPO2");
}

/* เวลาที่ติดมากับแต่ละตัวอย่างต้องเป็นเวลาของตัวอย่างนั้นจริง ไม่ใช่เวลาของชุดล่าสุด */
static void testSampleTimestamp() {
  section("3.3 เวลาประจำตัวอย่าง");

  freshChip();
  MassmoreMAX3010x sensor;
  sensor.begin(Wire);
  sensor.setup(0x10, MASSMORE_MAX3010X_SMP_AVE_1, MASSMORE_MAX3010X_MODE_SPO2,
               MASSMORE_MAX3010X_RATE_100, MASSMORE_MAX3010X_PULSE_411US,
               MASSMORE_MAX3010X_ADC_RANGE_4096);
  sensor.clearFifo();

  hostAdvanceTime(100);
  sensor.update();
  massmore_max3010x_sample_t first;
  check(sensor.peekSample(first), "อ่านตัวอย่างชุดแรกได้");
  const uint32_t firstMs = first.timestampMs;

  /* ยังไม่อ่านชุดแรกออก แล้วปล่อยให้ update รอบใหม่เข้ามาเพิ่ม */
  hostAdvanceTime(100);
  sensor.update();

  massmore_max3010x_sample_t again;
  sensor.peekSample(again);
  check(again.timestampMs == firstMs,
        "เวลาของตัวอย่างเก่าไม่ถูกเขียนทับด้วยเวลาของชุดใหม่");

  /* ไล่อ่านไปจนถึงตัวอย่างของชุดที่สอง เวลาต้องมากกว่าเดิม */
  uint32_t laterMs = firstMs;
  while (sensor.available()) {
    massmore_max3010x_sample_t s;
    sensor.peekSample(s);
    laterMs = s.timestampMs;
    sensor.nextSample();
  }
  check(laterMs > firstMs, "ตัวอย่างชุดหลังมีเวลามากกว่าชุดแรก");
}

static void testTemperature() {
  section("4. อุณหภูมิแกนชิป");

  freshChip();
  MassmoreMAX3010x sensor;
  sensor.begin(Wire);

  g_mockChip.dieTemperature = 28.5f;
  checkNear(sensor.readTemperature(), 28.5f, 0.07f, "อ่านอุณหภูมิบวกได้ถูกต้อง");

  g_mockChip.dieTemperature = 36.75f;
  checkNear(sensor.readTemperature(), 36.75f, 0.07f, "อ่านค่าที่มีเศษ 0.75 ได้ถูกต้อง");

  g_mockChip.dieTemperature = 36.75f;
  checkNear(sensor.readTemperatureF(), 98.15f, 0.15f, "แปลงเป็นฟาเรนไฮต์ถูกต้อง");

  /* แบบไม่บล็อก */
  g_mockChip.dieTemperature = 30.25f;
  check(sensor.startTemperatureConversion(), "สั่งวัดแบบไม่บล็อกได้");
  check(sensor.isTemperatureReady(), "ถามแล้วพร้อมให้อ่านผล");
  checkNear(sensor.getTemperatureResult(), 30.25f, 0.07f,
            "ผลจากการวัดแบบไม่บล็อกถูกต้อง");
}

static void testVerifyChip() {
  section("5. การตรวจสอบว่าเป็นชิปแท้");

  freshChip();
  MassmoreMAX3010x good;
  good.begin(Wire);
  const massmore_max3010x_genuine_t verdict = good.verifyChip();
  printf("       ผ่าน %u จาก %u ข้อ  บิตแมสก์ 0x%04X\n",
         (unsigned)good.getVerifyPassCount(),
         (unsigned)MASSMORE_MAX3010X_CHK_COUNT, (unsigned)good.getVerifyMask());
  for (uint8_t i = 0; i < MASSMORE_MAX3010X_CHK_COUNT; i++) {
    if ((good.getVerifyMask() & (1u << i)) == 0) {
      printf("       ข้อที่ไม่ผ่าน: %s\n", MassmoreMAX3010x::getVerifyCheckName(i));
    }
  }
  check(verdict == MASSMORE_MAX3010X_GENUINE_PASS, "ชิปแท้ผ่านครบทุกข้อ");
  check(good.getVerifyPassCount() == MASSMORE_MAX3010X_CHK_COUNT,
        "นับจำนวนข้อที่ผ่านได้ครบ 11");

  /* ของเลียนแบบที่เขียนทับ PART_ID ได้ */
  freshChip();
  g_mockChip.partIdWritable = true;
  MassmoreMAX3010x fake1;
  fake1.begin(Wire);
  fake1.verifyChip();
  check((fake1.getVerifyMask() & MASSMORE_MAX3010X_CHK_READONLY) == 0,
        "จับได้ว่า PART_ID เขียนทับได้");

  /* ของเลียนแบบที่เก็บบิตสงวนไว้ */
  freshChip();
  g_mockChip.reservedBitsStick = true;
  MassmoreMAX3010x fake2;
  fake2.begin(Wire);
  fake2.verifyChip();
  check((fake2.getVerifyMask() & MASSMORE_MAX3010X_CHK_RESERVED) == 0,
        "จับได้ว่าบิตสงวนไม่เป็นศูนย์");

  /* ของเลียนแบบที่ตัวชี้ FIFO ไม่ใช่ฟิลด์ 5 บิต */
  freshChip();
  g_mockChip.pointerFullByte = true;
  MassmoreMAX3010x fake3;
  fake3.begin(Wire);
  fake3.verifyChip();
  check((fake3.getVerifyMask() & MASSMORE_MAX3010X_CHK_FIFO_PTR) == 0,
        "จับได้ว่าตัวชี้ FIFO ไม่วนกลับ");

  /* REV_ID ที่ค้างที่ 0xFF */
  freshChip();
  g_mockChip.revisionId = 0xFF;
  MassmoreMAX3010x fake4;
  fake4.begin(Wire);
  fake4.verifyChip();
  check((fake4.getVerifyMask() & MASSMORE_MAX3010X_CHK_REV_ID) == 0,
        "จับได้ว่า REV_ID เป็น 0xFF");

  /* ชื่อข้อตรวจต้องมีครบและไม่เป็นค่าว่าง */
  bool namesOk = true;
  for (uint8_t i = 0; i < MASSMORE_MAX3010X_CHK_COUNT; i++) {
    if (MassmoreMAX3010x::getVerifyCheckName(i)[0] == '\0') {
      namesOk = false;
    }
  }
  check(namesOk, "ชื่อข้อตรวจครบทั้ง 11 ข้อ");
}

static void testErrorStrings() {
  section("6. ข้อความอธิบายข้อผิดพลาด");

  bool ok = true;
  for (int i = 0; i <= (int)MASSMORE_MAX3010X_ERR_UNSUPPORTED; i++) {
    const char *text =
        MassmoreMAX3010x::errorToString((massmore_max3010x_error_t)i);
    if (text == NULL || text[0] == '\0') {
      ok = false;
    }
  }
  check(ok, "ทุกรหัสข้อผิดพลาดมีข้อความอธิบาย");

  /* เรียกฟังก์ชันก่อน begin ต้องคืนค่าผิดพลาดอย่างสุภาพ ไม่พัง */
  freshChip();
  MassmoreMAX3010x sensor;
  uint8_t value = 0xAA;
  check(!sensor.readRegister8(MASSMORE_MAX3010X_REG_PART_ID, value),
        "อ่านรีจิสเตอร์ก่อน begin คืน false");
  check(sensor.lastError() == MASSMORE_MAX3010X_ERR_NOT_BEGUN,
        "รหัสข้อผิดพลาดเป็น NOT_BEGUN");
  check(!sensor.update(), "update ก่อน begin คืน false");
  check(!sensor.setup(0x10, MASSMORE_MAX3010X_SMP_AVE_1,
                      MASSMORE_MAX3010X_MODE_SPO2, MASSMORE_MAX3010X_RATE_50,
                      MASSMORE_MAX3010X_PULSE_411US,
                      MASSMORE_MAX3010X_ADC_RANGE_2048),
        "setup ก่อน begin คืน false");
  check(sensor.lastErrorString() != NULL && sensor.lastErrorString()[0] != '\0',
        "lastErrorString คืนข้อความที่ใช้ได้");
}

static void testBeatDetector() {
  section("7. อัลกอริทึมจับจังหวะการเต้นของหัวใจ");

  /* ป้อนคลื่นไซน์ที่รู้ความถี่แน่นอน แล้วดูว่าอ่านค่ากลับมาได้ตรงไหม */
  const float rate = 50.0f;
  const float targets[] = {50.0f, 75.0f, 120.0f};

  for (int t = 0; t < 3; t++) {
    MassmoreMAX3010xBeatDetector detector;
    detector.begin(rate);
    detector.setFingerThreshold(1000);

    for (int i = 0; i < 1000; i++) { /* 20 วินาที */
      const float phase =
          2.0f * 3.14159265f * (targets[t] / 60.0f) * (float)i / rate;
      const float value = 90000.0f + 2000.0f * sinf(phase);
      detector.check((uint32_t)value);
    }

    char name[96];
    snprintf(name, sizeof(name), "จับจังหวะที่ %.0f BPM ได้แม่นยำ", (double)targets[t]);
    checkNear(detector.getAverageBeatsPerMinute(), targets[t], 3.0f, name);
  }

  /* ไม่มีนิ้ววาง ต้องไม่นับอะไรเลย */
  MassmoreMAX3010xBeatDetector idle;
  idle.begin(rate);
  for (int i = 0; i < 500; i++) {
    const float phase = 2.0f * 3.14159265f * 1.25f * (float)i / rate;
    idle.check((uint32_t)(500.0f + 100.0f * sinf(phase)));
  }
  check(idle.getBeatCount() == 0, "ไม่นับจังหวะเมื่อไม่มีนิ้ววางอยู่");
  check(!idle.isFingerPresent(), "รายงานว่าไม่มีนิ้ววางอยู่");

  /* สัญญาณแบนราบ ไม่มีคลื่นชีพจร ต้องไม่นับ */
  MassmoreMAX3010xBeatDetector flat;
  flat.begin(rate);
  for (int i = 0; i < 500; i++) {
    flat.check(90000);
  }
  check(flat.getBeatCount() == 0, "ไม่นับจังหวะเมื่อสัญญาณแบนราบ");
  check(flat.isFingerPresent(), "ยังรายงานว่ามีนิ้ววางอยู่เพราะ DC สูง");

  /* reset ต้องล้างทุกอย่าง */
  MassmoreMAX3010xBeatDetector resettable;
  resettable.begin(rate);
  resettable.setFingerThreshold(1000);
  for (int i = 0; i < 600; i++) {
    const float phase = 2.0f * 3.14159265f * 1.25f * (float)i / rate;
    resettable.check((uint32_t)(90000.0f + 2000.0f * sinf(phase)));
  }
  check(resettable.getBeatCount() > 0, "นับจังหวะได้ก่อน reset");
  resettable.reset();
  check(resettable.getBeatCount() == 0, "reset ล้างตัวนับจังหวะ");
  check(resettable.getAverageBeatsPerMinute() == 0.0f, "reset ล้างค่า BPM");
}

static void testSpO2() {
  section("8. อัลกอริทึมคำนวณ SpO2");

  const float rate = 50.0f;

  struct Case {
    float ratio;
    float expectedSpo2;
  };
  /* SpO2 = -45.06 R^2 + 30.354 R + 94.845 */
  const Case cases[] = {
      {0.50f, 98.76f},
      {0.60f, 96.82f},
      {0.80f, 90.28f},
  };

  for (int c = 0; c < 3; c++) {
    MassmoreMAX3010xSpO2 calculator;
    calculator.begin(rate, 25);
    calculator.setFingerThreshold(1000);

    const float dcIr = 100000.0f;
    const float dcRed = 90000.0f;
    const float acIr = 0.020f;
    const float acRed = acIr * cases[c].ratio;

    for (int i = 0; i < 800; i++) {
      const float phase = 2.0f * 3.14159265f * 1.25f * (float)i / rate;
      const float ir = dcIr * (1.0f + acIr * sinf(phase));
      const float red = dcRed * (1.0f + acRed * sinf(phase));
      calculator.add((uint32_t)red, (uint32_t)ir);
    }

    char name[96];
    snprintf(name, sizeof(name), "R = %.2f ให้ค่า R ที่คำนวณตรง", (double)cases[c].ratio);
    checkNear(calculator.getRatio(), cases[c].ratio, 0.02f, name);

    snprintf(name, sizeof(name), "R = %.2f ให้ SpO2 ตามสมการ", (double)cases[c].ratio);
    checkNear(calculator.getSpO2(), cases[c].expectedSpo2, 1.0f, name);
    check(calculator.isSpO2Valid(), "ผลลัพธ์ถูกทำเครื่องหมายว่าเชื่อถือได้");
    check(calculator.isFingerPresent(), "ตรวจพบนิ้วจากระดับ DC");
  }

  /* ไม่มีนิ้ว ต้องไม่ให้ค่า */
  MassmoreMAX3010xSpO2 empty;
  empty.begin(rate, 25);
  for (int i = 0; i < 400; i++) {
    empty.add(500, 600);
  }
  check(!empty.isSpO2Valid(), "ไม่ให้ค่า SpO2 เมื่อไม่มีนิ้ว");
  check(!empty.isFingerPresent(), "รายงานว่าไม่มีนิ้ว");

  /* หน้าต่างยังไม่เต็ม ต้องยังไม่คำนวณ */
  MassmoreMAX3010xSpO2 filling;
  filling.begin(rate, 25);
  filling.setFingerThreshold(1000);
  bool computedEarly = false;
  for (int i = 0; i < MASSMORE_MAX3010X_SPO2_WINDOW - 1; i++) {
    if (filling.add(90000, 100000)) {
      computedEarly = true;
    }
  }
  check(!computedEarly, "ไม่คำนวณจนกว่าหน้าต่างข้อมูลจะเต็ม");
  checkNear(filling.getFillRatio(), 0.995f, 0.01f, "รายงานสัดส่วนข้อมูลที่เก็บได้");

  /* ปรับสัมประสิทธิ์เองได้ */
  MassmoreMAX3010xSpO2 custom;
  custom.begin(rate, 25);
  custom.setFingerThreshold(1000);
  custom.setCalibration(0.0f, 0.0f, 99.0f); /* คงที่ 99 ไม่ว่าค่า R เท่าไร */
  for (int i = 0; i < 600; i++) {
    const float phase = 2.0f * 3.14159265f * 1.25f * (float)i / rate;
    const float ir = 100000.0f * (1.0f + 0.02f * sinf(phase));
    const float red = 90000.0f * (1.0f + 0.012f * sinf(phase));
    custom.add((uint32_t)red, (uint32_t)ir);
  }
  checkNear(custom.getSpO2(), 99.0f, 0.01f, "setCalibration เปลี่ยนผลลัพธ์ได้จริง");
}

static void testPowerModes() {
  section("9. โหมดประหยัดไฟ");

  freshChip();
  MassmoreMAX3010x sensor;
  sensor.begin(Wire);
  sensor.setup(0x20, MASSMORE_MAX3010X_SMP_AVE_8, MASSMORE_MAX3010X_MODE_SPO2,
               MASSMORE_MAX3010X_RATE_400, MASSMORE_MAX3010X_PULSE_411US,
               MASSMORE_MAX3010X_ADC_RANGE_4096);

  hostAdvanceTime(200);
  check(sensor.getSamplesInFifo() > 0, "มีข้อมูลออกมาตอนชิปตื่น");

  sensor.clearFifo();
  check(sensor.shutdown(), "สั่งหลับสำเร็จ");
  hostAdvanceTime(200);
  check(sensor.getSamplesInFifo() == 0, "ไม่มีข้อมูลออกมาตอนชิปหลับ");

  check(sensor.wakeUp(), "สั่งปลุกสำเร็จ");
  hostAdvanceTime(200);
  check(sensor.getSamplesInFifo() > 0, "มีข้อมูลออกมาอีกครั้งหลังปลุก");

  /* ค่าที่ตั้งไว้ต้องยังอยู่ */
  massmore_max3010x_config_t cfg;
  sensor.readConfiguration(cfg);
  check(cfg.ledIr == 0x20 && cfg.mode == MASSMORE_MAX3010X_MODE_SPO2,
        "ค่าที่ตั้งไว้ยังอยู่ครบหลังหลับและตื่น");

  check(sensor.setAllLedsOff(), "ปิด LED ทุกดวงสำเร็จ");
  sensor.readConfiguration(cfg);
  check(cfg.ledRed == 0 && cfg.ledIr == 0, "กระแส LED เป็นศูนย์ทุกดวง");
}

static void testInterruptRegisters() {
  section("10. รีจิสเตอร์อินเทอร์รัปต์");

  freshChip();
  MassmoreMAX3010x sensor;
  sensor.begin(Wire);

  check(sensor.disableAllInterrupts(), "ปิดอินเทอร์รัปต์ทุกชนิดสำเร็จ");
  check(sensor.readRegister8(MASSMORE_MAX3010X_REG_INT_ENABLE_1) == 0x00,
        "INT_ENABLE_1 เป็นศูนย์");

  sensor.enableInterruptAlmostFull(true);
  check(sensor.readRegister8(MASSMORE_MAX3010X_REG_INT_ENABLE_1) ==
            MASSMORE_MAX3010X_INT_A_FULL,
        "เปิด A_FULL แล้วบิตขึ้นถูกตำแหน่ง");

  sensor.enableInterruptDataReady(true);
  check(sensor.readRegister8(MASSMORE_MAX3010X_REG_INT_ENABLE_1) ==
            (MASSMORE_MAX3010X_INT_A_FULL | MASSMORE_MAX3010X_INT_PPG_RDY),
        "เปิด PPG_RDY เพิ่มโดยไม่ลบบิตเดิม");

  sensor.enableInterruptAlmostFull(false);
  check(sensor.readRegister8(MASSMORE_MAX3010X_REG_INT_ENABLE_1) ==
            MASSMORE_MAX3010X_INT_PPG_RDY,
        "ปิด A_FULL แล้วบิตอื่นยังอยู่");

  check(!sensor.enableInterruptProximity(true),
        "เปิด proximity ไม่ได้บนชิปที่ไม่มีความสามารถนี้");

  sensor.enableInterruptDieTemperature(true);
  check(sensor.readRegister8(MASSMORE_MAX3010X_REG_INT_ENABLE_2) ==
            MASSMORE_MAX3010X_INT_DIE_TEMP_RDY,
        "เปิดอินเทอร์รัปต์อุณหภูมิได้");

  /* แฟล็ก A_FULL ต้องขึ้นเมื่อ FIFO สะสมถึงเกณฑ์ และหายไปเมื่ออ่านแล้ว */
  sensor.setup(0x10, MASSMORE_MAX3010X_SMP_AVE_1, MASSMORE_MAX3010X_MODE_SPO2,
               MASSMORE_MAX3010X_RATE_400, MASSMORE_MAX3010X_PULSE_411US,
               MASSMORE_MAX3010X_ADC_RANGE_4096);
  sensor.setFifoAlmostFull(15);
  sensor.clearFifo();
  sensor.getInterruptStatus1();
  hostAdvanceTime(100); /* 400 Hz นาน 100 ms = 40 ตัวอย่าง เกินเกณฑ์แน่นอน */
  const uint8_t status = sensor.getInterruptStatus1();
  check((status & MASSMORE_MAX3010X_INT_A_FULL) != 0, "แฟล็ก A_FULL ขึ้นเมื่อถึงเกณฑ์");
  check((sensor.getInterruptStatus1() & MASSMORE_MAX3010X_INT_A_FULL) == 0,
        "อ่านสถานะแล้วแฟล็กถูกเคลียร์");
}

int main() {
  printf("==================================================\n");
  printf(" ชุดทดสอบไลบรารี Massmore_MAX3010x %s\n",
         MASSMORE_MAX3010X_VERSION_STRING);
  printf("==================================================\n");

  testBeginAndIdentity();
  testConfiguration();
  testFifo();
  testFifoFullBoundary();
  testConfigurationBeforeSetup();
  testSampleTimestamp();
  testTemperature();
  testVerifyChip();
  testErrorStrings();
  testBeatDetector();
  testSpO2();
  testPowerModes();
  testInterruptRegisters();

  printf("\n==================================================\n");
  printf(" ผ่าน %d   ไม่ผ่าน %d\n", g_passed, g_failed);
  printf("==================================================\n");

  return (g_failed == 0) ? 0 : 1;
}
