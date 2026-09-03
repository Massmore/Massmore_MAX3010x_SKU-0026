/*
  12_FactoryTest - ชุดทดสอบโรงงานสำหรับบอร์ด Massmore MAX30102 (SKU-0026)

  รันเองทันทีหลังบูต ไม่ต้องพิมพ์อะไร  พิมพ์ r เพื่อทดสอบซ้ำ

  สิ่งที่ทดสอบ
    ด่านที่ 1  สแกนบัส I2C หา 0x57
    ด่านที่ 2  ตรวจตัวตนของชิปว่าเป็น MAX3010x ของแท้ 11 ข้อ
    ถ้าสองด่านนี้ไม่ผ่าน จะไม่เข้า RUN TEST เลย เพราะทดสอบต่อไปก็ไม่มีความหมาย
    RUN TEST   ทดสอบทุกความสามารถของชิปอีก 26 หัวข้อ

  การต่อสาย (ค่าปริยายของบอร์ด Massmore ESP32 Breakout / Qwiic)
    VIN -> 3V3 หรือ 5V     SDA -> GPIO 21
    GND -> GND             SCL -> GPIO 22
    INT -> GPIO 4  (ต่อหรือไม่ต่อก็ได้ ถ้าไม่ต่อหัวข้อ INT_PIN จะขึ้น WARN)

  บรรทัดที่ขึ้นต้นด้วย # มีไว้ให้โปรแกรมฝั่งเว็บอ่านอัตโนมัติ
    #RESULT,<ลำดับ>,<ชื่อหัวข้อ>,<PASS|FAIL|WARN>,<รายละเอียด>
    #DEVICE,<addr>,<variant>,<part_id>,<rev_id>,<genuine>,<ผ่านกี่ข้อจาก11>
    #VERDICT,<PASS|FAIL>,<ผ่าน>,<ไม่ผ่าน>,<เตือน>

  by Massmore  |  MIT License
*/

#include <Massmore_MAX3010x.h>
#include <Massmore_MAX3010x_Algorithms.h>
#include <Wire.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

/* -------------------------------------------------------------------------
   การตั้งค่า
   ------------------------------------------------------------------------- */

#define PIN_SDA 21
#define PIN_SCL 22
#define PIN_INT 4 /* ใส่ -1 ถ้าไม่ได้ต่อ */

#define I2C_FREQ_NORMAL 100000UL
#define I2C_FREQ_FAST 400000UL

#define FT_DETAIL_LEN 160
#define FT_MAX_TESTS 40

/* -------------------------------------------------------------------------
   โครงสร้างเก็บผล
   ------------------------------------------------------------------------- */

enum FtStatus { FT_PASS = 0, FT_WARN, FT_FAIL };

struct FtResult {
  const char *name;
  FtStatus status;
  char detail[FT_DETAIL_LEN];
};

static FtResult g_results[FT_MAX_TESTS];
static uint8_t g_resultCount = 0;

static MassmoreMAX3010x sensor;

/* ข้อมูลอุปกรณ์ที่ตรวจพบ */
static uint8_t g_foundAddress = 0;
static uint8_t g_partId = 0;
static uint8_t g_revId = 0;
static massmore_max3010x_genuine_t g_genuine = MASSMORE_MAX3010X_GENUINE_UNKNOWN;
static uint8_t g_genuinePassCount = 0;
static const char *g_variantName = "-";

/* ธงของอินเทอร์รัปต์บนขา INT */
static volatile bool g_intFired = false;

#if defined(ESP32)
void IRAM_ATTR onIntPin() { g_intFired = true; }
#else
void onIntPin() { g_intFired = true; }
#endif

/* -------------------------------------------------------------------------
   ตัวช่วย
   ------------------------------------------------------------------------- */

/*!
 * แปลงเลขทศนิยมเป็นข้อความทศนิยมสองตำแหน่ง
 * ไม่ใช้ %f ใน snprintf เพราะ newlib-nano บนบางเป้าหมายตัดการรองรับ float ทิ้ง
 */
static const char *ftF2(char *buffer, size_t size, float value) {
  if (isnan(value)) {
    snprintf(buffer, size, "nan");
    return buffer;
  }
  bool negative = value < 0.0f;
  if (negative) {
    value = -value;
  }
  long scaled = (long)(value * 100.0f + 0.5f);
  snprintf(buffer, size, "%s%ld.%02ld", negative ? "-" : "", scaled / 100,
           scaled % 100);
  return buffer;
}

/*!
 * คัดลอกข้อความลงบัฟเฟอร์โดยไม่ตัดกลางตัวอักษรไทย
 * ตัวอักษรไทยใน UTF-8 ใช้ 3 ไบต์ ถ้าตัดตรงกลางจะขึ้นเป็นสี่เหลี่ยม
 * ฟังก์ชันนี้จะถอยกลับจนพ้นไบต์ต่อเนื่อง (10xxxxxx) ก่อนปิดท้ายด้วย 0
 */
static void ftCopyDetail(char *dest, size_t size, const char *source) {
  if (size == 0) {
    return;
  }
  size_t length = strlen(source);
  if (length >= size) {
    length = size - 1;
    while (length > 0 && ((unsigned char)source[length] & 0xC0) == 0x80) {
      length--;
    }
  }
  memcpy(dest, source, length);
  dest[length] = '\0';
}

static const char *ftStatusText(FtStatus status) {
  switch (status) {
    case FT_PASS: return "PASS";
    case FT_WARN: return "WARN";
    default: return "FAIL";
  }
}

/*! บันทึกผลหนึ่งหัวข้อ พร้อมพิมพ์ออกทันทีทั้งแบบคนอ่านและแบบเครื่องอ่าน */
static void ftAdd(const char *name, FtStatus status, const char *detail) {
  if (g_resultCount >= FT_MAX_TESTS) {
    return;
  }
  FtResult *result = &g_results[g_resultCount];
  result->name = name;
  result->status = status;
  ftCopyDetail(result->detail, FT_DETAIL_LEN, detail);
  g_resultCount++;

  /* บัฟเฟอร์เผื่อไว้กว้างพอสำหรับชื่อหัวข้อบวกรายละเอียดเต็มความยาว */
  char line[FT_DETAIL_LEN * 2 + 64];
  snprintf(line, sizeof(line), "  [%s] %-18s %s", ftStatusText(status), name,
           result->detail);
  Serial.println(line);

  snprintf(line, sizeof(line), "#RESULT,%u,%s,%s,%s", (unsigned)g_resultCount,
           name, ftStatusText(status), result->detail);
  Serial.println(line);
}

static void ftAddf(const char *name, FtStatus status, const char *format,
                   long a, long b) {
  char buffer[FT_DETAIL_LEN];
  snprintf(buffer, sizeof(buffer), format, a, b);
  ftAdd(name, status, buffer);
}

/*! อ่านข้อมูลจาก FIFO ต่อเนื่องตามเวลาที่กำหนด แล้วคืนสถิติที่ได้ */
struct FtCapture {
  uint32_t count;
  uint32_t minIr;
  uint32_t maxIr;
  uint32_t minRed;
  uint32_t maxRed;
  double sumIr;
  double sumRed;
};

static void ftCapture(FtCapture &out, uint32_t durationMs) {
  out.count = 0;
  out.minIr = 0xFFFFFFFFUL;
  out.maxIr = 0;
  out.minRed = 0xFFFFFFFFUL;
  out.maxRed = 0;
  out.sumIr = 0.0;
  out.sumRed = 0.0;

  sensor.clearFifo();
  const uint32_t start = millis();
  while ((uint32_t)(millis() - start) < durationMs) {
    if (sensor.update()) {
      while (sensor.available()) {
        const uint32_t ir = sensor.getIR();
        const uint32_t red = sensor.getRed();
        sensor.nextSample();

        out.count++;
        out.sumIr += (double)ir;
        out.sumRed += (double)red;
        if (ir < out.minIr) out.minIr = ir;
        if (ir > out.maxIr) out.maxIr = ir;
        if (red < out.minRed) out.minRed = red;
        if (red > out.maxRed) out.maxRed = red;
      }
    }
  }
  if (out.count == 0) {
    out.minIr = 0;
    out.minRed = 0;
  }
}

/* -------------------------------------------------------------------------
   ด่านที่ 1  สแกนบัส I2C
   ------------------------------------------------------------------------- */

static bool gateScanBus() {
  Serial.println("ด่านที่ 1  สแกนบัส I2C");

  uint8_t found = 0;
  char detail[FT_DETAIL_LEN];
  detail[0] = '\0';

  for (uint8_t address = 1; address < 127; address++) {
    Wire.beginTransmission(address);
    if (Wire.endTransmission() == 0) {
      found++;
      char item[16];
      snprintf(item, sizeof(item), "%s0x%02X", (found > 1) ? " " : "", address);
      strncat(detail, item, sizeof(detail) - strlen(detail) - 1);
      if (address == MASSMORE_MAX3010X_I2C_ADDRESS) {
        g_foundAddress = address;
      }
    }
  }

  if (g_foundAddress == 0) {
    /* ใช้บัฟเฟอร์กว้างเป็นสองเท่า เพราะรายการที่อยู่บนบัสอาจยาวเกือบเต็ม
       แล้วปล่อยให้ ftAdd ตัดให้พอดีอีกทีโดยไม่ตัดกลางตัวอักษรไทย */
    char message[FT_DETAIL_LEN * 2];
    snprintf(message, sizeof(message),
             "ไม่พบ 0x57 บนบัส พบทั้งหมด %u ตัว (%s)", (unsigned)found,
             found ? detail : "ไม่มีเลย");
    ftAdd("I2C_SCAN", FT_FAIL, message);
    return false;
  }

  char message[FT_DETAIL_LEN * 2];
  snprintf(message, sizeof(message), "พบ MAX3010x ที่ 0x57  อุปกรณ์บนบัส %u ตัว (%s)",
           (unsigned)found, detail);
  ftAdd("I2C_SCAN", FT_PASS, message);
  return true;
}

/* -------------------------------------------------------------------------
   ด่านที่ 2  ตรวจตัวตนของชิป
   ------------------------------------------------------------------------- */

static bool gateVerifyChip() {
  Serial.println("ด่านที่ 2  ตรวจตัวตนของชิป");

  if (!sensor.begin(Wire, MASSMORE_MAX3010X_I2C_ADDRESS)) {
    char message[FT_DETAIL_LEN];
    snprintf(message, sizeof(message), "begin ไม่สำเร็จ: %s (PART_ID 0x%02X)",
             sensor.lastErrorString(), (unsigned)sensor.readPartID());
    ftAdd("CHIP_BEGIN", FT_FAIL, message);
    return false;
  }

  g_partId = sensor.readPartID();
  g_revId = sensor.readRevisionID();
  g_variantName = sensor.getVariantName();

  g_genuine = sensor.verifyChip();
  g_genuinePassCount = sensor.getVerifyPassCount();
  const uint16_t mask = sensor.getVerifyMask();

  /* พิมพ์รายละเอียดรายข้อให้ช่างเห็นว่าไม่ผ่านข้อไหน */
  for (uint8_t i = 0; i < MASSMORE_MAX3010X_CHK_COUNT; i++) {
    Serial.print("      ");
    Serial.print((mask & (1u << i)) ? "ผ่าน   " : "ไม่ผ่าน ");
    Serial.print(i + 1);
    Serial.print(". ");
    Serial.println(MassmoreMAX3010x::getVerifyCheckName(i));
  }

  char message[FT_DETAIL_LEN];
  snprintf(message, sizeof(message),
           "%s PART_ID 0x%02X REV 0x%02X ผ่าน %u/%u ข้อ", g_variantName,
           (unsigned)g_partId, (unsigned)g_revId, (unsigned)g_genuinePassCount,
           (unsigned)MASSMORE_MAX3010X_CHK_COUNT);

  if (g_genuine == MASSMORE_MAX3010X_GENUINE_PASS) {
    ftAdd("CHIP_GENUINE", FT_PASS, message);
    return true;
  }
  if (g_genuine == MASSMORE_MAX3010X_GENUINE_PARTIAL) {
    ftAdd("CHIP_GENUINE", FT_WARN, message);
    return true;
  }
  ftAdd("CHIP_GENUINE", FT_FAIL, message);
  return false;
}

/* -------------------------------------------------------------------------
   RUN TEST  ทดสอบความสามารถทั้งหมด
   ------------------------------------------------------------------------- */

static void testRegisterAccess() {
  /* 1. เขียนอ่านรีจิสเตอร์กระแส LED ด้วยหลายแพตเทิร์น */
  const uint8_t patterns[] = {0x00, 0x55, 0xAA, 0xFF, 0x01, 0x80};
  bool ok = true;
  uint8_t badPattern = 0;
  for (uint8_t i = 0; i < sizeof(patterns) && ok; i++) {
    uint8_t readback = 0;
    if (!sensor.writeRegister8(MASSMORE_MAX3010X_REG_LED2_PA, patterns[i]) ||
        !sensor.readRegister8(MASSMORE_MAX3010X_REG_LED2_PA, readback) ||
        readback != patterns[i]) {
      ok = false;
      badPattern = patterns[i];
    }
  }
  sensor.writeRegister8(MASSMORE_MAX3010X_REG_LED2_PA, 0x00);
  if (ok) {
    ftAdd("REG_RW", FT_PASS, "เขียนอ่าน LED2_PA ครบ 6 แพตเทิร์นตรงทุกค่า");
  } else {
    ftAddf("REG_RW", FT_FAIL, "อ่านกลับไม่ตรงที่แพตเทิร์น 0x%02lX%.0ld", (long)badPattern, 0L);
  }

  /* 2. รีจิสเตอร์อ่านอย่างเดียวต้องเขียนทับไม่ได้ */
  sensor.writeRegister8(MASSMORE_MAX3010X_REG_PART_ID, 0x00);
  const uint8_t partAfter = sensor.readPartID();
  if (partAfter == MASSMORE_MAX3010X_PART_ID_EXPECTED) {
    ftAdd("REG_READONLY", FT_PASS, "PART_ID ยังเป็น 0x15 หลังพยายามเขียนทับ");
  } else {
    ftAddf("REG_READONLY", FT_FAIL, "PART_ID กลายเป็น 0x%02lX%.0ld หลังเขียนทับ",
           (long)partAfter, 0L);
  }

  /* 3. บิตสงวนใน MODE_CONFIG ต้องอ่านกลับมาเป็นศูนย์ */
  sensor.writeRegister8(MASSMORE_MAX3010X_REG_MODE_CONFIG, 0x38);
  uint8_t modeBack = 0xFF;
  sensor.readRegister8(MASSMORE_MAX3010X_REG_MODE_CONFIG, modeBack);
  sensor.writeRegister8(MASSMORE_MAX3010X_REG_MODE_CONFIG, 0x00);
  if ((modeBack & 0x38) == 0x00) {
    ftAdd("RESERVED_BITS", FT_PASS, "บิตสงวน 5:3 ของ MODE_CONFIG เป็นศูนย์");
  } else {
    ftAddf("RESERVED_BITS", FT_FAIL, "MODE_CONFIG อ่านกลับได้ 0x%02lX%.0ld",
           (long)modeBack, 0L);
  }
}

static void testFifoPointers() {
  /* ตัวชี้ FIFO เป็นฟิลด์ 5 บิต ค่าเกิน 31 ต้องวนกลับ */
  bool ok = true;
  uint8_t readback = 0;

  sensor.writeRegister8(MASSMORE_MAX3010X_REG_FIFO_WR_PTR, 0x1F);
  sensor.readRegister8(MASSMORE_MAX3010X_REG_FIFO_WR_PTR, readback);
  if ((readback & 0x1F) != 0x1F) ok = false;

  sensor.writeRegister8(MASSMORE_MAX3010X_REG_FIFO_WR_PTR, 0x20);
  sensor.readRegister8(MASSMORE_MAX3010X_REG_FIFO_WR_PTR, readback);
  if ((readback & 0x1F) != 0x00) ok = false;

  sensor.clearFifo();
  ftAdd("FIFO_PTR", ok ? FT_PASS : FT_FAIL,
        ok ? "ตัวชี้ FIFO เป็นฟิลด์ 5 บิตและวนกลับถูกต้อง"
           : "ตัวชี้ FIFO ไม่วนกลับตามที่ datasheet ระบุ");

  /* clearFifo() ต้องทำให้ตัวชี้ทั้งสามเป็นศูนย์ */
  const uint8_t wr = sensor.getWritePointer();
  const uint8_t rd = sensor.getReadPointer();
  const uint8_t ovf = sensor.getOverflowCounter();
  if (wr == 0 && rd == 0 && ovf == 0) {
    ftAdd("FIFO_CLEAR", FT_PASS, "clearFifo ล้างตัวชี้ครบทั้งสามตัว");
  } else {
    char message[FT_DETAIL_LEN];
    snprintf(message, sizeof(message), "หลัง clearFifo ได้ WR=%u RD=%u OVF=%u",
             (unsigned)wr, (unsigned)rd, (unsigned)ovf);
    ftAdd("FIFO_CLEAR", FT_FAIL, message);
  }
}

static void testModes() {
  struct ModeCase {
    const char *name;
    massmore_max3010x_mode_t mode;
    uint8_t expectedChannels;
  };
  const ModeCase cases[] = {
      {"MODE_HR", MASSMORE_MAX3010X_MODE_HR, 1},
      {"MODE_SPO2", MASSMORE_MAX3010X_MODE_SPO2, 2},
  };

  for (uint8_t i = 0; i < 2; i++) {
    sensor.setup(0x20, MASSMORE_MAX3010X_SMP_AVE_4, cases[i].mode,
                 MASSMORE_MAX3010X_RATE_400, MASSMORE_MAX3010X_PULSE_411US,
                 MASSMORE_MAX3010X_ADC_RANGE_4096);

    FtCapture capture;
    ftCapture(capture, 300);

    massmore_max3010x_config_t cfg;
    sensor.readConfiguration(cfg);

    const bool ok = (cfg.mode == cases[i].mode) &&
                    (cfg.activeChannels == cases[i].expectedChannels) &&
                    (capture.count > 5);

    char message[FT_DETAIL_LEN];
    snprintf(message, sizeof(message), "ช่องข้อมูล %u ตัวอย่างที่ได้ %lu ชุดใน 300 ms",
             (unsigned)cfg.activeChannels, (unsigned long)capture.count);
    ftAdd(cases[i].name, ok ? FT_PASS : FT_FAIL, message);
  }

  /* โหมด multi-LED สองช่อง (บอร์ดนี้ไม่มี LED เขียว) */
  sensor.setup(0x20, MASSMORE_MAX3010X_SMP_AVE_4,
               MASSMORE_MAX3010X_MODE_SPO2, MASSMORE_MAX3010X_RATE_400,
               MASSMORE_MAX3010X_PULSE_411US, MASSMORE_MAX3010X_ADC_RANGE_4096);
  sensor.setMultiLedSlot(1, MASSMORE_MAX3010X_SLOT_RED);
  sensor.setMultiLedSlot(2, MASSMORE_MAX3010X_SLOT_IR);
  sensor.setMultiLedSlot(3, MASSMORE_MAX3010X_SLOT_NONE);
  sensor.setMultiLedSlot(4, MASSMORE_MAX3010X_SLOT_NONE);
  sensor.setMode(MASSMORE_MAX3010X_MODE_MULTI_LED);

  FtCapture capture;
  ftCapture(capture, 300);
  const uint8_t channels = sensor.getActiveChannels();
  const bool ok = (channels == 2) && (capture.count > 5);

  char message[FT_DETAIL_LEN];
  snprintf(message, sizeof(message), "ช่องเวลาที่เปิด %u ได้ข้อมูล %lu ชุด",
           (unsigned)channels, (unsigned long)capture.count);
  ftAdd("MODE_MULTI_LED", ok ? FT_PASS : FT_FAIL, message);
}

static void testSampleRate() {
  /* วัดอัตราข้อมูลจริงเทียบกับที่ตั้งไว้ ยอมให้คลาดเคลื่อนได้ 25 เปอร์เซ็นต์
     เพราะเวลาที่ใช้คุย I2C และ jitter ของ millis() มีผลอยู่บ้าง */
  struct RateCase {
    massmore_max3010x_rate_t rate;
    massmore_max3010x_smp_ave_t average;
    massmore_max3010x_pulse_width_t pulse;
  };
  const RateCase cases[] = {
      {MASSMORE_MAX3010X_RATE_100, MASSMORE_MAX3010X_SMP_AVE_1,
       MASSMORE_MAX3010X_PULSE_411US},
      {MASSMORE_MAX3010X_RATE_400, MASSMORE_MAX3010X_SMP_AVE_8,
       MASSMORE_MAX3010X_PULSE_411US},
      {MASSMORE_MAX3010X_RATE_800, MASSMORE_MAX3010X_SMP_AVE_4,
       MASSMORE_MAX3010X_PULSE_215US},
  };
  const char *names[] = {"RATE_100HZ", "RATE_50HZ", "RATE_200HZ"};

  for (uint8_t i = 0; i < 3; i++) {
    sensor.setup(0x20, cases[i].average, MASSMORE_MAX3010X_MODE_SPO2,
                 cases[i].rate, cases[i].pulse,
                 MASSMORE_MAX3010X_ADC_RANGE_4096);

    const float expected = sensor.getEffectiveSampleRate();
    FtCapture capture;
    ftCapture(capture, 1000);
    const float actual = (float)capture.count;

    const float lower = expected * 0.75f;
    const float upper = expected * 1.25f;
    const bool ok = (actual >= lower && actual <= upper);

    char expectedText[16];
    char actualText[16];
    char message[FT_DETAIL_LEN];
    snprintf(message, sizeof(message), "ตั้งไว้ %s Hz วัดได้ %s Hz",
             ftF2(expectedText, sizeof(expectedText), expected),
             ftF2(actualText, sizeof(actualText), actual));
    ftAdd(names[i], ok ? FT_PASS : FT_FAIL, message);
  }
}

static void testAdcAndLed() {
  sensor.setup(0x00, MASSMORE_MAX3010X_SMP_AVE_4, MASSMORE_MAX3010X_MODE_SPO2,
               MASSMORE_MAX3010X_RATE_400, MASSMORE_MAX3010X_PULSE_411US,
               MASSMORE_MAX3010X_ADC_RANGE_4096);

  /* 1. ปิด LED ทุกดวง ค่าที่อ่านได้ต้องต่ำ ถ้าสูงแปลว่ามีแสงรั่วหรือวงจรเสีย */
  sensor.setPulseAmplitudeRed(0x00);
  sensor.setPulseAmplitudeIR(0x00);
  delay(60);
  FtCapture dark;
  ftCapture(dark, 300);
  const float darkAvgIr =
      (dark.count > 0) ? (float)(dark.sumIr / (double)dark.count) : 0.0f;

  {
    char valueText[16];
    char message[FT_DETAIL_LEN];
    snprintf(message, sizeof(message), "ปิด LED แล้วค่าเฉลี่ย IR = %s",
             ftF2(valueText, sizeof(valueText), darkAvgIr));
    /* ค่ามืดควรต่ำกว่า 5000 นับ ถ้าสูงกว่านี้แปลว่าโดนแสงแรงหรือมีแสงรั่ว */
    ftAdd("LED_OFF_DARK", (dark.count > 0 && darkAvgIr < 5000.0f) ? FT_PASS : FT_WARN,
          message);
  }

  /* 2. เปิดเฉพาะ LED สีแดง ช่องแดงต้องขยับ */
  sensor.setPulseAmplitudeRed(0x7F);
  sensor.setPulseAmplitudeIR(0x00);
  delay(80);
  FtCapture redOn;
  ftCapture(redOn, 300);
  const float redAvg =
      (redOn.count > 0) ? (float)(redOn.sumRed / (double)redOn.count) : 0.0f;
  const float darkAvgRed =
      (dark.count > 0) ? (float)(dark.sumRed / (double)dark.count) : 0.0f;
  {
    char valueText[16];
    char message[FT_DETAIL_LEN];
    snprintf(message, sizeof(message), "เปิดไฟแดงแล้วช่องแดงเฉลี่ย %s (ตอนมืด %ld)",
             ftF2(valueText, sizeof(valueText), redAvg), (long)darkAvgRed);
    ftAdd("LED_RED", (redAvg > darkAvgRed + 200.0f) ? FT_PASS : FT_FAIL, message);
  }

  /* 3. เปิดเฉพาะอินฟราเรด ช่อง IR ต้องขยับ */
  sensor.setPulseAmplitudeRed(0x00);
  sensor.setPulseAmplitudeIR(0x7F);
  delay(80);
  FtCapture irOn;
  ftCapture(irOn, 300);
  const float irAvg =
      (irOn.count > 0) ? (float)(irOn.sumIr / (double)irOn.count) : 0.0f;
  {
    char valueText[16];
    char message[FT_DETAIL_LEN];
    snprintf(message, sizeof(message), "เปิดไฟ IR แล้วช่อง IR เฉลี่ย %s (ตอนมืด %ld)",
             ftF2(valueText, sizeof(valueText), irAvg), (long)darkAvgIr);
    ftAdd("LED_IR", (irAvg > darkAvgIr + 200.0f) ? FT_PASS : FT_FAIL, message);
  }

  /* 4. ไล่กระแส LED เป็นขั้น ค่าที่อ่านได้ต้องเพิ่มตามแบบ monotonic */
  {
    const uint8_t levels[] = {0x10, 0x30, 0x60, 0xA0};
    float previous = -1.0f;
    bool increasing = true;
    for (uint8_t i = 0; i < 4; i++) {
      sensor.setPulseAmplitudeIR(levels[i]);
      delay(60);
      FtCapture step;
      ftCapture(step, 200);
      const float average =
          (step.count > 0) ? (float)(step.sumIr / (double)step.count) : 0.0f;
      /* ยอมให้เท่ากันได้ถ้าอิ่มตัวแล้ว แต่ห้ามลดลงอย่างมีนัยสำคัญ */
      if (previous >= 0.0f && average + 500.0f < previous) {
        increasing = false;
      }
      previous = average;
    }
    ftAdd("LED_LINEARITY", increasing ? FT_PASS : FT_FAIL,
          increasing ? "ค่าที่อ่านได้เพิ่มตามกระแส LED ทั้ง 4 ขั้น"
                     : "ค่าที่อ่านได้ลดลงเมื่อเพิ่มกระแส ผิดปกติ");
  }

  /* 5. ช่วง ADC ที่แคบลงต้องให้ค่าที่อ่านได้สูงขึ้นเมื่อแสงเท่าเดิม */
  {
    sensor.setPulseAmplitudeIR(0x20);
    sensor.setAdcRange(MASSMORE_MAX3010X_ADC_RANGE_16384);
    delay(60);
    FtCapture wide;
    ftCapture(wide, 250);
    const float wideAvg =
        (wide.count > 0) ? (float)(wide.sumIr / (double)wide.count) : 0.0f;

    sensor.setAdcRange(MASSMORE_MAX3010X_ADC_RANGE_2048);
    delay(60);
    FtCapture narrow;
    ftCapture(narrow, 250);
    const float narrowAvg =
        (narrow.count > 0) ? (float)(narrow.sumIr / (double)narrow.count) : 0.0f;

    char message[FT_DETAIL_LEN];
    snprintf(message, sizeof(message), "ช่วง 16384 nA ได้ %ld  ช่วง 2048 nA ได้ %ld",
             (long)wideAvg, (long)narrowAvg);
    /* ช่วงแคบไวกว่า ค่าจึงควรสูงกว่า (หรืออิ่มตัวไปเลย) */
    ftAdd("ADC_RANGE", (narrowAvg >= wideAvg) ? FT_PASS : FT_FAIL, message);
    sensor.setAdcRange(MASSMORE_MAX3010X_ADC_RANGE_4096);
  }
}

static void testSampleAverage() {
  /* การเฉลี่ยตัวอย่างต้องทำให้สัญญาณรบกวนลดลงจริง
     วัดจากส่วนเบี่ยงเบนของค่าที่อ่านได้ตอนเปิด LED นิ่ง ๆ */
  sensor.setup(0x40, MASSMORE_MAX3010X_SMP_AVE_1, MASSMORE_MAX3010X_MODE_SPO2,
               MASSMORE_MAX3010X_RATE_800, MASSMORE_MAX3010X_PULSE_215US,
               MASSMORE_MAX3010X_ADC_RANGE_4096);
  delay(80);
  FtCapture raw;
  ftCapture(raw, 400);
  const uint32_t rawSpread = (raw.count > 0) ? (raw.maxIr - raw.minIr) : 0;

  sensor.setSampleAverage(MASSMORE_MAX3010X_SMP_AVE_32);
  delay(80);
  FtCapture averaged;
  ftCapture(averaged, 400);
  const uint32_t avgSpread =
      (averaged.count > 0) ? (averaged.maxIr - averaged.minIr) : 0;

  char message[FT_DETAIL_LEN];
  snprintf(message, sizeof(message), "ไม่เฉลี่ยแกว่ง %lu  เฉลี่ย 32 ตัวแกว่ง %lu",
           (unsigned long)rawSpread, (unsigned long)avgSpread);
  /* ต้องได้ข้อมูลจริงทั้งสองรอบ และการเฉลี่ยต้องไม่ทำให้แย่ลง */
  const bool ok = (raw.count > 0) && (averaged.count > 0) &&
                  (avgSpread <= rawSpread + 200);
  ftAdd("SAMPLE_AVERAGE", ok ? FT_PASS : FT_WARN, message);
}

static void testTemperature() {
  /* 1. อ่านแบบบล็อก */
  const float celsius = sensor.readTemperature();
  char valueText[16];
  char message[FT_DETAIL_LEN];
  snprintf(message, sizeof(message), "อุณหภูมิแกนชิป %s องศาเซลเซียส",
           ftF2(valueText, sizeof(valueText), celsius));
  const bool ok = !isnan(celsius) && celsius > 0.0f && celsius < 70.0f;
  ftAdd("DIE_TEMP", ok ? FT_PASS : FT_FAIL, message);

  /* 2. อ่านแบบไม่บล็อก ต้องได้ค่าใกล้เคียงกันและเสร็จภายในเวลาที่กำหนด */
  const uint32_t start = millis();
  bool ready = false;
  float nonBlocking = NAN;
  if (sensor.startTemperatureConversion()) {
    while ((uint32_t)(millis() - start) < 200) {
      if (sensor.isTemperatureReady()) {
        nonBlocking = sensor.getTemperatureResult();
        ready = true;
        break;
      }
      delay(1);
    }
  }
  const uint32_t elapsed = millis() - start;

  char text2[16];
  char message2[FT_DETAIL_LEN];
  snprintf(message2, sizeof(message2), "ได้ %s องศา ใช้เวลา %lu ms",
           ftF2(text2, sizeof(text2), nonBlocking), (unsigned long)elapsed);
  const bool ok2 = ready && !isnan(nonBlocking) &&
                   fabsf(nonBlocking - celsius) < 5.0f;
  ftAdd("TEMP_NONBLOCK", ok2 ? FT_PASS : FT_FAIL, message2);
}

static void testInterrupts() {
  /* 1. เขียนอ่านรีจิสเตอร์ enable ได้ตรง */
  sensor.disableAllInterrupts();
  sensor.enableInterruptAlmostFull(true);
  sensor.enableInterruptDataReady(true);
  const uint8_t enable1 = sensor.readRegister8(MASSMORE_MAX3010X_REG_INT_ENABLE_1);
  const bool enableOk = (enable1 & MASSMORE_MAX3010X_INT_A_FULL) &&
                        (enable1 & MASSMORE_MAX3010X_INT_PPG_RDY);
  {
    char message[FT_DETAIL_LEN];
    snprintf(message, sizeof(message), "INT_ENABLE_1 อ่านกลับได้ 0x%02X",
             (unsigned)enable1);
    ftAdd("INT_ENABLE", enableOk ? FT_PASS : FT_FAIL, message);
  }

  /* 2. ปล่อยให้ FIFO สะสมจนแจ้ง A_FULL แล้วดูว่าแฟล็กขึ้นจริง */
  sensor.setup(0x20, MASSMORE_MAX3010X_SMP_AVE_4, MASSMORE_MAX3010X_MODE_SPO2,
               MASSMORE_MAX3010X_RATE_400, MASSMORE_MAX3010X_PULSE_411US,
               MASSMORE_MAX3010X_ADC_RANGE_4096);
  sensor.setFifoAlmostFull(15);
  sensor.disableAllInterrupts();
  sensor.enableInterruptAlmostFull(true);
  sensor.getInterruptStatus1();
  sensor.clearFifo();
  g_intFired = false;

  bool flagged = false;
  const uint32_t start = millis();
  while ((uint32_t)(millis() - start) < 1500) {
    const uint8_t status = sensor.getInterruptStatus1();
    if (status & MASSMORE_MAX3010X_INT_A_FULL) {
      flagged = true;
      break;
    }
    delay(10);
  }
  {
    char message[FT_DETAIL_LEN];
    snprintf(message, sizeof(message), "แฟล็ก A_FULL %s ภายใน %lu ms",
             flagged ? "ขึ้นแล้ว" : "ไม่ขึ้น", (unsigned long)(millis() - start));
    ftAdd("INT_A_FULL", flagged ? FT_PASS : FT_FAIL, message);
  }

  /* 3. ขา INT จริงถูกดึงลงต่ำหรือไม่ (ต่อสายหรือไม่ต่อก็ได้) */
#if PIN_INT >= 0
  sensor.getInterruptStatus1();
  sensor.clearFifo();
  g_intFired = false;
  const uint32_t pinStart = millis();
  while ((uint32_t)(millis() - pinStart) < 1500 && !g_intFired) {
    delay(10);
  }
  if (g_intFired) {
    ftAddf("INT_PIN", FT_PASS, "ขา INT (GPIO %ld) ถูกดึงลงต่ำจริง%.0ld",
           (long)PIN_INT, 0L);
  } else {
    ftAddf("INT_PIN", FT_WARN,
           "ไม่มีสัญญาณบน GPIO %ld อาจไม่ได้ต่อสาย INT ซึ่งไม่ใช่ข้อบังคับ%.0ld",
           (long)PIN_INT, 0L);
  }
  sensor.getInterruptStatus1();
#else
  ftAdd("INT_PIN", FT_WARN, "ข้ามการทดสอบ เพราะตั้ง PIN_INT เป็น -1");
#endif

  sensor.disableAllInterrupts();
}

static void testOverflow() {
  /* ปิด rollover แล้วปล่อยให้ FIFO ล้น ตัวนับ OVF ต้องเดินขึ้น */
  sensor.setup(0x20, MASSMORE_MAX3010X_SMP_AVE_1, MASSMORE_MAX3010X_MODE_SPO2,
               MASSMORE_MAX3010X_RATE_800, MASSMORE_MAX3010X_PULSE_215US,
               MASSMORE_MAX3010X_ADC_RANGE_4096);
  sensor.setFifoRollover(false);
  sensor.clearFifo();
  delay(400); /* 800 Hz นาน 400 ms = 320 ตัวอย่าง มากกว่า FIFO 32 ช่องแน่นอน */

  const uint8_t overflow = sensor.getOverflowCounter();
  char message[FT_DETAIL_LEN];
  snprintf(message, sizeof(message), "ตัวนับ OVF = %u หลังปล่อยให้ล้น 400 ms",
           (unsigned)overflow);
  ftAdd("FIFO_OVERFLOW", (overflow > 0) ? FT_PASS : FT_FAIL, message);

  sensor.setFifoRollover(true);
  sensor.clearFifo();
}

static void testBusSpeed() {
  /* ชิปรองรับ I2C ถึง 400 kHz ตาม datasheet ต้องอ่านได้ถูกต้องทั้งสองความเร็ว */
  Wire.setClock(I2C_FREQ_FAST);
  delay(5);
  const uint8_t fastId = sensor.readPartID();

  Wire.setClock(I2C_FREQ_NORMAL);
  delay(5);
  const uint8_t slowId = sensor.readPartID();

  Wire.setClock(I2C_FREQ_FAST);

  const bool ok = (fastId == MASSMORE_MAX3010X_PART_ID_EXPECTED) &&
                  (slowId == MASSMORE_MAX3010X_PART_ID_EXPECTED);
  char message[FT_DETAIL_LEN];
  snprintf(message, sizeof(message), "400 kHz ได้ 0x%02X  100 kHz ได้ 0x%02X",
           (unsigned)fastId, (unsigned)slowId);
  ftAdd("I2C_SPEED", ok ? FT_PASS : FT_FAIL, message);
}

static void testPowerModes() {
  /* shutdown แล้วต้องไม่มีข้อมูลใหม่ออกมา ปลุกแล้วต้องกลับมาทำงานได้ */
  sensor.setup(0x20, MASSMORE_MAX3010X_SMP_AVE_4, MASSMORE_MAX3010X_MODE_SPO2,
               MASSMORE_MAX3010X_RATE_400, MASSMORE_MAX3010X_PULSE_411US,
               MASSMORE_MAX3010X_ADC_RANGE_4096);
  FtCapture awake;
  ftCapture(awake, 300);

  sensor.shutdown();
  delay(30);
  FtCapture asleep;
  ftCapture(asleep, 300);

  sensor.wakeUp();
  delay(60);
  FtCapture wokeUp;
  ftCapture(wokeUp, 300);

  char message[FT_DETAIL_LEN];
  snprintf(message, sizeof(message), "ตื่น %lu  หลับ %lu  ปลุกแล้ว %lu ตัวอย่าง",
           (unsigned long)awake.count, (unsigned long)asleep.count,
           (unsigned long)wokeUp.count);
  const bool ok = (awake.count > 5) && (asleep.count == 0) && (wokeUp.count > 5);
  ftAdd("SHUTDOWN_WAKE", ok ? FT_PASS : FT_FAIL, message);

  /* ตรวจว่าค่าที่ตั้งไว้ไม่หายหลัง shutdown ตาม datasheet */
  massmore_max3010x_config_t cfg;
  sensor.readConfiguration(cfg);
  const bool kept = (cfg.mode == MASSMORE_MAX3010X_MODE_SPO2) &&
                    (cfg.ledIr == 0x20) && (!cfg.shutdown);
  ftAdd("CONFIG_RETAIN", kept ? FT_PASS : FT_FAIL,
        kept ? "ค่าที่ตั้งไว้ยังอยู่ครบหลังหลับและตื่น"
             : "ค่าที่ตั้งไว้หายไปหลัง shutdown");
}

static void testAlgorithms() {
  /* ทดสอบอัลกอริทึมด้วยสัญญาณสังเคราะห์ จะได้รู้ว่าโค้ดคำนวณถูกโดยไม่ต้องมีนิ้ว
     สร้างคลื่นไซน์ 1.25 Hz = 75 ครั้งต่อนาที บนฐาน DC 80000 */
  MassmoreMAX3010xBeatDetector detector;
  const float rate = 50.0f;
  const float targetBpm = 75.0f;
  detector.begin(rate);
  detector.setFingerThreshold(1000);

  const uint16_t totalSamples = 500; /* 10 วินาที */
  for (uint16_t i = 0; i < totalSamples; i++) {
    const float phase = 2.0f * 3.14159265f * (targetBpm / 60.0f) * (float)i / rate;
    const float value = 80000.0f + 1500.0f * sinf(phase);
    detector.check((uint32_t)value);
  }

  const float measured = detector.getAverageBeatsPerMinute();
  char valueText[16];
  char message[FT_DETAIL_LEN];
  snprintf(message, sizeof(message), "ป้อนคลื่น 75.0 BPM อัลกอริทึมอ่านได้ %s BPM",
           ftF2(valueText, sizeof(valueText), measured));
  const bool ok = (detector.getBeatCount() >= 5) && fabsf(measured - targetBpm) < 4.0f;
  ftAdd("ALGO_HEARTRATE", ok ? FT_PASS : FT_FAIL, message);
}

static void testSpO2Algorithm() {
  /* สร้างสัญญาณ PPG สังเคราะห์ที่มีอัตราส่วน AC/DC ตามที่ต้องการ
     แล้วดูว่าตัวคำนวณให้ค่า R ตรงตามที่ป้อนเข้าไปหรือไม่
     ตั้งเป้าที่ R = 0.60 ซึ่งควรได้ SpO2 ราว 96.9 เปอร์เซ็นต์ */
  MassmoreMAX3010xSpO2 calculator;
  const float rate = 50.0f;
  calculator.begin(rate, 25);
  calculator.setFingerThreshold(1000);

  const float dcIr = 100000.0f;
  const float dcRed = 90000.0f;
  const float acFractionIr = 0.020f;          /* AC/DC ของ IR = 2.0% */
  const float acFractionRed = 0.020f * 0.60f; /* ทำให้ R = 0.60 พอดี */

  bool computed = false;
  for (uint16_t i = 0; i < 600; i++) {
    const float phase = 2.0f * 3.14159265f * 1.2f * (float)i / rate;
    const float ir = dcIr * (1.0f + acFractionIr * sinf(phase));
    const float red = dcRed * (1.0f + acFractionRed * sinf(phase));
    if (calculator.add((uint32_t)red, (uint32_t)ir)) {
      computed = true;
    }
  }

  const massmore_max3010x_spo2_result_t &result = calculator.getResult();
  char ratioText[16];
  char spo2Text[16];
  char message[FT_DETAIL_LEN];
  snprintf(message, sizeof(message), "ป้อน R=0.60 คำนวณได้ R=%s SpO2=%s",
           ftF2(ratioText, sizeof(ratioText), result.ratio),
           ftF2(spo2Text, sizeof(spo2Text), result.spo2));

  const bool ok = computed && result.spo2Valid &&
                  fabsf(result.ratio - 0.60f) < 0.05f &&
                  result.spo2 > 90.0f && result.spo2 < 100.0f;
  ftAdd("ALGO_SPO2", ok ? FT_PASS : FT_FAIL, message);
}

static void testLibraryConsistency() {
  /* ตรวจว่าค่าที่ไลบรารีจำไว้ตรงกับที่อ่านกลับมาจากชิปจริง
     ถ้าไม่ตรง แปลว่ามีบั๊กในการซิงก์สถานะ */
  sensor.setup(0x33, MASSMORE_MAX3010X_SMP_AVE_16, MASSMORE_MAX3010X_MODE_SPO2,
               MASSMORE_MAX3010X_RATE_200, MASSMORE_MAX3010X_PULSE_118US,
               MASSMORE_MAX3010X_ADC_RANGE_8192);

  massmore_max3010x_config_t cfg;
  const bool read = sensor.readConfiguration(cfg);

  const bool ok = read && cfg.sampleAverage == MASSMORE_MAX3010X_SMP_AVE_16 &&
                  cfg.sampleRate == MASSMORE_MAX3010X_RATE_200 &&
                  cfg.pulseWidth == MASSMORE_MAX3010X_PULSE_118US &&
                  cfg.adcRange == MASSMORE_MAX3010X_ADC_RANGE_8192 &&
                  cfg.ledRed == 0x33 && cfg.ledIr == 0x33 &&
                  cfg.fifoRollover;

  char message[FT_DETAIL_LEN];
  snprintf(message, sizeof(message),
           "อ่านกลับได้ ave=%u rate=%u pw=%u range=%u led=0x%02X",
           (unsigned)cfg.sampleAverage, (unsigned)cfg.sampleRate,
           (unsigned)cfg.pulseWidth, (unsigned)cfg.adcRange,
           (unsigned)cfg.ledRed);
  ftAdd("CONFIG_READBACK", ok ? FT_PASS : FT_FAIL, message);
}

static void testPpgQuality() {
  /* ทดสอบสุดท้าย ให้ช่างวางนิ้วเพื่อยืนยันว่าภาคออปติกใช้งานได้จริง
     ถ้าไม่มีคนวางนิ้วภายในเวลาที่กำหนด จะขึ้น WARN ไม่ถือว่าบอร์ดเสีย */
  sensor.setup(0x24, MASSMORE_MAX3010X_SMP_AVE_8, MASSMORE_MAX3010X_MODE_SPO2,
               MASSMORE_MAX3010X_RATE_400, MASSMORE_MAX3010X_PULSE_411US,
               MASSMORE_MAX3010X_ADC_RANGE_4096);

  Serial.println("      >>> วางนิ้วบนเซ็นเซอร์ภายใน 8 วินาที (ข้ามได้ถ้าทดสอบอัตโนมัติ)");

  MassmoreMAX3010xSpO2 oximeter;
  oximeter.begin(sensor.getEffectiveSampleRate(), 25);

  const uint32_t start = millis();
  bool fingerSeen = false;
  float bestPi = 0.0f;
  float bestSpo2 = 0.0f;
  float bestBpm = 0.0f;

  while ((uint32_t)(millis() - start) < 8000) {
    if (sensor.update()) {
      while (sensor.available()) {
        const uint32_t red = sensor.getRed();
        const uint32_t ir = sensor.getIR();
        sensor.nextSample();

        if (oximeter.add(red, ir)) {
          const massmore_max3010x_spo2_result_t &r = oximeter.getResult();
          if (r.fingerPresent) {
            fingerSeen = true;
            if (r.perfusionIr > bestPi) {
              bestPi = r.perfusionIr;
            }
            if (r.spo2Valid) {
              bestSpo2 = r.spo2;
            }
            if (r.heartRateValid) {
              bestBpm = r.heartRate;
            }
          }
        }
      }
    }
    delay(2);
  }

  if (!fingerSeen) {
    ftAdd("PPG_FINGER", FT_WARN,
          "ไม่มีการวางนิ้วในช่วงทดสอบ ข้ามหัวข้อนี้ ไม่ถือว่าบอร์ดเสีย");
    return;
  }

  char piText[16];
  char spo2Text[16];
  char bpmText[16];
  char message[FT_DETAIL_LEN];
  snprintf(message, sizeof(message), "PI สูงสุด %s%%  SpO2 %s%%  BPM %s",
           ftF2(piText, sizeof(piText), bestPi),
           ftF2(spo2Text, sizeof(spo2Text), bestSpo2),
           ftF2(bpmText, sizeof(bpmText), bestBpm));
  /* ได้สัญญาณชีพจรจริง PI ต้องเกิน 0.1% ขึ้นไป */
  ftAdd("PPG_FINGER", (bestPi > 0.1f) ? FT_PASS : FT_WARN, message);
}

/* -------------------------------------------------------------------------
   ตัวจัดการหลัก
   ------------------------------------------------------------------------- */

static void printSummary(bool ranFullTest) {
  uint8_t passed = 0;
  uint8_t failed = 0;
  uint8_t warned = 0;

  for (uint8_t i = 0; i < g_resultCount; i++) {
    switch (g_results[i].status) {
      case FT_PASS: passed++; break;
      case FT_WARN: warned++; break;
      default: failed++; break;
    }
  }

  Serial.println();
  Serial.println("==================================================");
  Serial.println(" สรุปผลการทดสอบ");
  Serial.println("==================================================");
  Serial.print("  ชิปที่ตรวจพบ   ");
  Serial.println(g_variantName);
  Serial.print("  PART_ID        0x");
  Serial.println(g_partId, HEX);
  Serial.print("  REVISION_ID    0x");
  Serial.println(g_revId, HEX);
  Serial.print("  ตรวจของแท้     ผ่าน ");
  Serial.print(g_genuinePassCount);
  Serial.print("/");
  Serial.print(MASSMORE_MAX3010X_CHK_COUNT);
  Serial.println(" ข้อ");
  Serial.println("--------------------------------------------------");
  Serial.print("  ผ่าน ");
  Serial.print(passed);
  Serial.print("   ไม่ผ่าน ");
  Serial.print(failed);
  Serial.print("   เตือน ");
  Serial.println(warned);

  if (failed > 0) {
    Serial.println();
    Serial.println("  หัวข้อที่ไม่ผ่าน");
    for (uint8_t i = 0; i < g_resultCount; i++) {
      if (g_results[i].status == FT_FAIL) {
        Serial.print("    - ");
        Serial.print(g_results[i].name);
        Serial.print("  ");
        Serial.println(g_results[i].detail);
      }
    }
  }

  const bool overallPass = (failed == 0) && ranFullTest;

  Serial.println();
  Serial.print("  ผลรวม  ");
  Serial.println(overallPass ? ">>> ผ่าน <<<" : ">>> ไม่ผ่าน <<<");
  Serial.println("==================================================");

  char line[128];
  snprintf(line, sizeof(line), "#DEVICE,0x%02X,%s,0x%02X,0x%02X,%u,%u",
           (unsigned)(g_foundAddress ? g_foundAddress
                                     : MASSMORE_MAX3010X_I2C_ADDRESS),
           g_variantName, (unsigned)g_partId, (unsigned)g_revId,
           (unsigned)g_genuine, (unsigned)g_genuinePassCount);
  Serial.println(line);

  snprintf(line, sizeof(line), "#VERDICT,%s,%u,%u,%u",
           overallPass ? "PASS" : "FAIL", (unsigned)passed, (unsigned)failed,
           (unsigned)warned);
  Serial.println(line);
  Serial.println();
}

static void runAllTests() {
  g_resultCount = 0;
  g_foundAddress = 0;
  g_partId = 0;
  g_revId = 0;
  g_genuine = MASSMORE_MAX3010X_GENUINE_UNKNOWN;
  g_genuinePassCount = 0;
  g_variantName = "-";

  Serial.println();
  Serial.println("==================================================");
  Serial.println(" Massmore MAX30102 (SKU-0026) - ชุดทดสอบโรงงาน");
  Serial.print(" ไลบรารีเวอร์ชัน ");
  Serial.println(MASSMORE_MAX3010X_VERSION_STRING);
  Serial.println("==================================================");
  Serial.println();

  if (!gateScanBus()) {
    Serial.println();
    Serial.println("หยุดการทดสอบ เพราะไม่พบเซ็นเซอร์บนบัส");
    Serial.println("  1. ตรวจว่าเสียบสาย Qwiic แน่นทั้งสองฝั่ง");
    Serial.println("  2. ตรวจว่ามีไฟเลี้ยงเข้าบอร์ด (VIN และ GND)");
    Serial.println("  3. ตรวจว่า SDA อยู่ที่ GPIO 21 และ SCL อยู่ที่ GPIO 22");
    printSummary(false);
    return;
  }

  Serial.println();
  if (!gateVerifyChip()) {
    Serial.println();
    Serial.println("หยุดการทดสอบ เพราะชิปไม่ผ่านการตรวจตัวตน");
    Serial.println("  บอร์ดนี้อาจใช้ชิปที่ไม่ใช่ของแท้ หรือชิปเสียหาย");
    printSummary(false);
    return;
  }

  Serial.println();
  Serial.println("RUN TEST  ทดสอบความสามารถทั้งหมด");
  Serial.println();

  testRegisterAccess();
  testFifoPointers();
  testModes();
  testSampleRate();
  testAdcAndLed();
  testSampleAverage();
  testTemperature();
  testInterrupts();
  testOverflow();
  testBusSpeed();
  testPowerModes();
  testLibraryConsistency();
  testAlgorithms();
  testSpO2Algorithm();
  testPpgQuality();

  printSummary(true);
}

void setup() {
  Serial.begin(115200);
  delay(500);

  Wire.begin(PIN_SDA, PIN_SCL, I2C_FREQ_FAST);

#if PIN_INT >= 0
  pinMode(PIN_INT, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(PIN_INT), onIntPin, FALLING);
#endif

  runAllTests();

  Serial.println("พิมพ์ r แล้วกด Enter เพื่อทดสอบซ้ำ");
}

void loop() {
  if (Serial.available()) {
    const char c = (char)Serial.read();
    if (c == 'r' || c == 'R') {
      runAllTests();
      Serial.println("พิมพ์ r แล้วกด Enter เพื่อทดสอบซ้ำ");
    }
  }
  delay(20);
}
