/*
  05_Factory_Test — Outgoing QA/QC สำหรับบอร์ด Massmore MAX30102 (SKU-0026)

  รันเองทันทีหลังบูต พิมพ์ 'r' + Enter ใน Serial Monitor เพื่อทดสอบซ้ำ
  Serial 115200 baud — บรรทัดที่ขึ้นต้นด้วย '#' คือรูปแบบให้ Massmore Web Serial Monitor parse

  Test sequence
    1. BUS_SCAN       สแกน I2C Bus ต้องพบอุปกรณ์ที่ 0x57
    2. CHIP_ID        PART_ID (0xFF) = 0x15   |  REV_ID (0xFE) ไม่ใช่ 0x00/0xFF
    3. AUTHENTICITY   verifyChip() 11 ข้อ ต้องผ่านครบ → GENUINE
    4. RANGE_TEMP     Die temperature -40..+85 C
    5. LED_RESPONSE   เปิด LED แล้ว ADC ต้องสูงกว่าตอนปิด LED
    6. RANGE_RED / RANGE_IR   ค่าเฉลี่ยขณะ LED เปิดต้องอยู่ใน 1..262142 (ไม่ saturate)
    7. CONTINUOUS     อ่าน 20 samples ต่อเนื่อง ไม่ TIMEOUT ไม่ saturate
    8. INT_PIN        (ถ้าต่อ) ขา INT ต้องดึงต่ำเมื่อ FIFO almost full และปล่อยสูงเมื่ออ่าน status

  Default wiring (Primary test MCU = Classic ESP32)
    VIN -> 3V3   GND -> GND   SDA -> GPIO 21   SCL -> GPIO 22   INT -> GPIO 4 (optional)

  Designed and Manufactured by Massmore | MIT License
*/

#include <Wire.h>
#include <Massmore_MAX3010x.h>
#include <math.h>

/* ---- Pins are hardcoded ONLY in this sketch ---- */
#if defined(CONFIG_IDF_TARGET_ESP32S3)
#define PIN_SDA 8
#define PIN_SCL 9
#define PIN_INT 4
#define MCU_NAME "ESP32-S3"
#elif defined(ESP32)
#define PIN_SDA 21
#define PIN_SCL 22
#define PIN_INT 4
#define MCU_NAME "ESP32"
#elif defined(__AVR__)
#define PIN_INT 2 /* Nano: A4/A5 fixed for I2C */
#define MCU_NAME "AVR"
#else
#define PIN_INT -1
#define MCU_NAME "UNKNOWN"
#endif

#define FT_VERSION "v1.0"
#define FT_PRODUCT "Massmore_MAX3010x"
#define FT_LED_POWER 0x1F
#define FT_CONTINUOUS_SAMPLES 20

Massmore_MAX3010x sensor;

static bool g_pass = true;
static const char *g_reason = "";

/* -------------------------------------------------------------------------
   Helpers
   ------------------------------------------------------------------------- */

static void printHex8(uint8_t v) {
  Serial.print(F("0x"));
  if (v < 0x10) Serial.print('0');
  Serial.print(v, HEX);
}

static void resultHex(const __FlashStringHelper *name, bool pass, uint8_t value) {
  Serial.print(F("#RESULT "));
  Serial.print(name);
  Serial.print(pass ? F(" PASS ") : F(" FAIL "));
  printHex8(value);
  Serial.println();
}

static void resultStr(const __FlashStringHelper *name, bool pass, const char *value) {
  Serial.print(F("#RESULT "));
  Serial.print(name);
  Serial.print(pass ? F(" PASS ") : F(" FAIL "));
  Serial.println(value);
}

static void resultFloat(const __FlashStringHelper *name, bool pass, float value) {
  Serial.print(F("#RESULT "));
  Serial.print(name);
  Serial.print(pass ? F(" PASS ") : F(" FAIL "));
  Serial.println(value, 2);
}

static void resultU32(const __FlashStringHelper *name, bool pass, uint32_t value) {
  Serial.print(F("#RESULT "));
  Serial.print(name);
  Serial.print(pass ? F(" PASS ") : F(" FAIL "));
  Serial.println(value);
}

static void fail(const char *reason) {
  if (g_pass) {
    g_pass = false;
    g_reason = reason;
  }
}

/* เก็บสถิติจาก N samples (Blocking readAll ทีละตัว) */
struct Stats {
  uint32_t count, timeouts, saturated;
  uint32_t minIr, maxIr, minRed, maxRed;
  uint32_t sumIr, sumRed; /* 20 x 262143 < 2^32 */
  uint32_t elapsedMs;
};

static void capture(Stats &s, uint8_t samples) {
  s.count = s.timeouts = s.saturated = 0;
  s.minIr = s.minRed = 0xFFFFFFFFUL;
  s.maxIr = s.maxRed = 0;
  s.sumIr = s.sumRed = 0;
  sensor.requestConversion();
  const uint32_t start = millis();
  for (uint8_t i = 0; i < samples; i++) {
    Massmore_MAX3010x::Readings r;
    if (!sensor.readAll(r, 300)) {
      s.timeouts++;
      continue;
    }
    s.count++;
    s.sumIr += r.ir;
    s.sumRed += r.red;
    if (r.ir < s.minIr) s.minIr = r.ir;
    if (r.ir > s.maxIr) s.maxIr = r.ir;
    if (r.red < s.minRed) s.minRed = r.red;
    if (r.red > s.maxRed) s.maxRed = r.red;
    if (r.ir >= MASSMORE_MAX3010X_DATA_MAX || r.red >= MASSMORE_MAX3010X_DATA_MAX) s.saturated++;
  }
  s.elapsedMs = (uint32_t)(millis() - start);
  if (s.count == 0) s.minIr = s.minRed = 0;
}

/* -------------------------------------------------------------------------
   Test steps
   ------------------------------------------------------------------------- */

static bool testBusScan() {
  bool found = false;
  uint8_t devices = 0;
  Serial.print(F("I2C devices:"));
  for (uint8_t addr = 1; addr < 127; addr++) {
    Wire.beginTransmission(addr);
    if (Wire.endTransmission() == 0) {
      devices++;
      Serial.print(' ');
      printHex8(addr);
      if (addr == MASSMORE_MAX3010X_I2C_ADDRESS) found = true;
    }
  }
  if (devices == 0) Serial.print(F(" none"));
  Serial.println();
  resultHex(F("BUS_SCAN"), found, found ? MASSMORE_MAX3010X_I2C_ADDRESS : 0x00);
  if (!found) fail("BUS_SCAN_NO_DEVICE");
  return found;
}

static bool testChipId() {
  if (!sensor.begin(Wire, PIN_INT)) {
    const uint8_t id = sensor.readPartID();
    resultHex(F("CHIP_ID"), false, id);
    Serial.print(F("begin() failed: "));
    Serial.println(sensor.lastErrorString());
    fail(sensor.lastError() == Massmore_MAX3010x::ErrorCode::WRONG_ID ? "CHIP_ID_MISMATCH" : "BUS_ERROR");
    return false;
  }
  const uint8_t id = sensor.readPartID();
  const uint8_t rev = sensor.readRevisionID();
  resultHex(F("CHIP_ID"), id == MASSMORE_MAX3010X_PART_ID_EXPECTED, id);
  const bool revOk = (rev != 0x00 && rev != 0xFF);
  resultHex(F("REV_ID"), revOk, rev);
  if (!revOk) fail("REV_ID_INVALID");
  Serial.print(F("Variant: "));
  Serial.println(sensor.getVariantName());
  Serial.println(F("Serial number: not available on MAX3010x"));
  return revOk;
}

static bool testAuthenticity() {
  const Massmore_MAX3010x::Genuine g = sensor.verifyChip();
  const uint16_t mask = sensor.getVerifyMask();
  for (uint8_t i = 0; i < Massmore_MAX3010x::CHK_COUNT; i++) {
    Serial.print((mask & (1u << i)) ? F("  [ok]   ") : F("  [FAIL] "));
    Serial.println(Massmore_MAX3010x::getVerifyCheckName(i));
  }
  Serial.print(F("Checks passed: "));
  Serial.print(sensor.getVerifyPassCount());
  Serial.print('/');
  Serial.println(Massmore_MAX3010x::CHK_COUNT);

  const bool pass = (g == Massmore_MAX3010x::Genuine::PASS);
  resultStr(F("AUTHENTICITY"), pass, pass ? "GENUINE" : "SUSPECT");
  if (!pass) fail("AUTHENTICITY_SUSPECT");
  return pass;
}

static void testTemperature() {
  const float t = sensor.readTemperature(150);
  const bool ok = !isnan(t) && t > -40.0f && t < 85.0f;
  resultFloat(F("RANGE_TEMP"), ok, isnan(t) ? -999.0f : t);
  if (!ok) fail("TEMP_OUT_OF_RANGE");
}

static void testLedAndRange() {
  Stats dark, lit;

  /* LED off */
  sensor.setupDefault(0x00);
  capture(dark, 10);
  const uint32_t darkMean = dark.count ? (dark.sumIr + dark.sumRed) / (2 * dark.count) : 0;

  /* LED on */
  sensor.setupDefault(FT_LED_POWER);
  capture(lit, 10);
  const uint32_t litMean = lit.count ? (lit.sumIr + lit.sumRed) / (2 * lit.count) : 0;

  const bool ledOk = (lit.count > 0) && (litMean > darkMean + 200);
  Serial.print(F("ADC mean dark="));
  Serial.print(darkMean);
  Serial.print(F(" lit="));
  Serial.println(litMean);
  resultU32(F("LED_RESPONSE"), ledOk, litMean > darkMean ? litMean - darkMean : 0);
  if (!ledOk) fail("LED_NO_RESPONSE");

  const uint32_t meanRed = lit.count ? lit.sumRed / lit.count : 0;
  const uint32_t meanIr = lit.count ? lit.sumIr / lit.count : 0;
  const bool redOk = lit.count > 0 && meanRed >= 1 && lit.maxRed < MASSMORE_MAX3010X_DATA_MAX;
  const bool irOk = lit.count > 0 && meanIr >= 1 && lit.maxIr < MASSMORE_MAX3010X_DATA_MAX;
  resultU32(F("RANGE_RED"), redOk, meanRed);
  resultU32(F("RANGE_IR"), irOk, meanIr);
  if (!redOk) fail("RANGE_RED_INVALID");
  if (!irOk) fail("RANGE_IR_INVALID");
}

static void testContinuous() {
  Stats s;
  capture(s, FT_CONTINUOUS_SAMPLES);
  /* 20 samples @ 50 Hz ~ 400 ms; ยอมให้ถึง 2000 ms */
  const bool ok = (s.count == FT_CONTINUOUS_SAMPLES) && (s.timeouts == 0) && (s.saturated == 0) &&
                  (s.elapsedMs < 2000);
  Serial.print(F("Continuous: "));
  Serial.print(s.count);
  Serial.print(F(" ok, "));
  Serial.print(s.timeouts);
  Serial.print(F(" timeout, "));
  Serial.print(s.saturated);
  Serial.print(F(" saturated, "));
  Serial.print(s.elapsedMs);
  Serial.print(F(" ms, IR "));
  Serial.print(s.minIr);
  Serial.print('-');
  Serial.println(s.maxIr);

  Serial.print(F("#RESULT CONTINUOUS "));
  Serial.print(ok ? F("PASS ") : F("FAIL "));
  Serial.print(s.count);
  Serial.print('/');
  Serial.println(FT_CONTINUOUS_SAMPLES);
  if (!ok) fail(s.timeouts ? "CONTINUOUS_TIMEOUT" : "CONTINUOUS_FAIL");
}

static void testIntPin() {
#if PIN_INT >= 0
  pinMode(PIN_INT, INPUT_PULLUP); /* INT เป็น open-drain */
  sensor.setFifoAlmostFull(15);   /* แจ้งเมื่อมี 17 samples */
  sensor.enableInterrupt(Massmore_MAX3010x::InterruptSource::ALMOST_FULL, true);
  sensor.getInterruptStatus1(); /* clear flag ค้าง */
  sensor.requestConversion();

  bool wentLow = false;
  const uint32_t start = millis();
  while ((uint32_t)(millis() - start) < 1500) {
    if (digitalRead(PIN_INT) == LOW) {
      wentLow = true;
      break;
    }
  }
  bool released = false;
  if (wentLow) {
    sensor.getInterruptStatus1(); /* read-to-clear → ขา INT ต้องปล่อยสูง */
    delay(2);
    released = (digitalRead(PIN_INT) == HIGH);
  }
  sensor.disableAllInterrupts();
  sensor.getInterruptStatus1();

  const bool ok = wentLow && released;
  resultStr(F("INT_PIN"), ok, ok ? "LOW_HIGH" : (wentLow ? "STUCK_LOW" : "NO_ASSERT"));
  if (!ok) fail("INT_PIN_FAIL");
#else
  Serial.println(F("INT_PIN: skipped (PIN_INT = -1)"));
#endif
}

/* -------------------------------------------------------------------------
   Runner
   ------------------------------------------------------------------------- */

static void runFactoryTest() {
  g_pass = true;
  g_reason = "";

  Serial.println();
  Serial.println(F("#MASSMORE_FACTORY_TEST " FT_VERSION));
  Serial.println(F("#PRODUCT " FT_PRODUCT));
  Serial.println(F("#MCU " MCU_NAME));
  Serial.println(F("#LIBRARY " MASSMORE_MAX3010X_VERSION_STRING));

  bool proceed = testBusScan();
  if (proceed) proceed = testChipId();
  if (proceed) proceed = testAuthenticity();
  if (proceed) {
    testTemperature();
    testLedAndRange();
    testContinuous();
    testIntPin();
  }
  sensor.setAllLedsOff();
  sensor.shutdown();

  if (g_pass) {
    Serial.println(F("#VERDICT PASS"));
    Serial.println(F("[PASS] SENSOR QA PASSED - READY TO SHIP"));
  } else {
    Serial.print(F("#VERDICT FAIL "));
    Serial.println(g_reason);
    Serial.print(F("[FAIL] QA CHECK FAILED: "));
    Serial.println(g_reason);
  }
  Serial.println(F("Type 'r' + Enter to run again."));
}

void setup() {
  Serial.begin(115200);
  delay(500);
#if defined(ESP32)
  Wire.begin(PIN_SDA, PIN_SCL);
#else
  Wire.begin();
#endif
  Wire.setClock(400000);
  runFactoryTest();
}

void loop() {
  if (Serial.available()) {
    const int c = Serial.read();
    if (c == 'r' || c == 'R') runFactoryTest();
  }
}
