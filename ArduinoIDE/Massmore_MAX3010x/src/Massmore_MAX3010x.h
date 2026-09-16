/**
 * @file    Massmore_MAX3010x.h
 * @brief   ไลบรารี Arduino / PlatformIO สำหรับ Pulse Oximeter & Heart-Rate Sensor
 *          ตระกูล MAX3010x (MAX30102 / MAX30101 / MAX30105)
 *
 * บอร์ด Massmore MAX30102 Pulse Oximeter and Heart-Rate Sensor (SKU-0026)
 * Designed and Manufactured by Massmore — https://www.massmore.shop
 *
 * หลักการออกแบบตาม Massmore Standard Library Specification
 *   - ไม่ hardcode GPIO และไม่เรียก Wire.begin() ในไลบรารี — sketch เป็นเจ้าของ I2C Bus
 *   - Bus reference injection ผ่าน begin(TwoWire &wirePort)
 *   - Dual API: Simple Blocking (readAll / readTemperature) และ Non-blocking FSM
 *     (requestConversion / update / isDataReady / getReadings)
 *   - Zero dynamic allocation, Read-Modify-Write ด้วย bitmask, ErrorCode + lastError()
 *   - verifyChipID() / getSerialNumber() / isGenuine() ตรวจของแท้จากค่าใน datasheet
 *   - อัลกอริทึม Heart-Rate (BeatDetector) และ SpO2 (Oximeter) แบบ streaming
 *     ไม่ใช้ window buffer จึงรันบน ATmega328P (2 KB SRAM) ได้
 *
 * @warning บอร์ดนี้เป็นอุปกรณ์สำหรับการเรียนรู้และงานต้นแบบเท่านั้น ไม่ใช่เครื่องมือแพทย์
 *          ห้ามใช้วินิจฉัย ติดตามอาการ หรือรักษาโรค
 *
 * @copyright Copyright (c) 2026 Massmore Biz Co., Ltd.
 * @license   MIT
 */

#ifndef MASSMORE_MAX3010X_H
#define MASSMORE_MAX3010X_H

#include <Arduino.h>
#include <Wire.h>
#include <stdint.h>

/* =========================================================================
   Library version
   ========================================================================= */
#define MASSMORE_MAX3010X_VERSION_MAJOR 2
#define MASSMORE_MAX3010X_VERSION_MINOR 0
#define MASSMORE_MAX3010X_VERSION_PATCH 0
#define MASSMORE_MAX3010X_VERSION_STRING "2.0.0"

/* =========================================================================
   Register map (คัดจาก datasheet MAX30102 Rev 1, MAX30101, MAX30105 โดยตรง)
   ========================================================================= */

/** I2C Address 7-bit ของชิป MAX3010x ทุกรุ่น ตรึงจากโรงงาน เปลี่ยนไม่ได้
 *  (datasheet เขียนเป็น 0xAE write / 0xAF read = 0x57 << 1) */
#define MASSMORE_MAX3010X_I2C_ADDRESS 0x57

/* Status / Interrupt */
#define MASSMORE_MAX3010X_REG_INT_STATUS_1 0x00 /**< Interrupt Status 1 (read-to-clear) */
#define MASSMORE_MAX3010X_REG_INT_STATUS_2 0x01 /**< Interrupt Status 2 (read-to-clear) */
#define MASSMORE_MAX3010X_REG_INT_ENABLE_1 0x02 /**< Interrupt Enable 1 */
#define MASSMORE_MAX3010X_REG_INT_ENABLE_2 0x03 /**< Interrupt Enable 2 */

#define MASSMORE_MAX3010X_INT_A_FULL (1u << 7)       /**< FIFO almost full */
#define MASSMORE_MAX3010X_INT_PPG_RDY (1u << 6)      /**< New PPG sample ready */
#define MASSMORE_MAX3010X_INT_ALC_OVF (1u << 5)      /**< Ambient light cancellation overflow */
#define MASSMORE_MAX3010X_INT_PROX (1u << 4)         /**< Proximity threshold (MAX30105 only) */
#define MASSMORE_MAX3010X_INT_PWR_RDY (1u << 0)      /**< Power ready (read-only) */
#define MASSMORE_MAX3010X_INT_DIE_TEMP_RDY (1u << 1) /**< Die temperature ready (Status 2) */

/* FIFO */
#define MASSMORE_MAX3010X_REG_FIFO_WR_PTR 0x04
#define MASSMORE_MAX3010X_REG_OVF_COUNTER 0x05
#define MASSMORE_MAX3010X_REG_FIFO_RD_PTR 0x06
#define MASSMORE_MAX3010X_REG_FIFO_DATA 0x07
#define MASSMORE_MAX3010X_REG_FIFO_CONFIG 0x08

#define MASSMORE_MAX3010X_FIFO_DEPTH 32       /**< FIFO ลึก 32 samples */
#define MASSMORE_MAX3010X_BYTES_PER_CHANNEL 3 /**< 18-bit ส่งมา 3 bytes ต่อ 1 LED channel */

/* FIFO_CONFIG bitmask (bits to KEEP during Read-Modify-Write) */
#define MASSMORE_MAX3010X_MASK_SMP_AVE 0x1F  /**< SMP_AVE[2:0] = bits 7:5 */
#define MASSMORE_MAX3010X_MASK_ROLLOVER 0xEF /**< FIFO_ROLLOVER_EN = bit 4 */
#define MASSMORE_MAX3010X_MASK_A_FULL 0xF0   /**< FIFO_A_FULL[3:0] = bits 3:0 */
#define MASSMORE_MAX3010X_BIT_ROLLOVER_EN (1u << 4)

/* Mode / SpO2 configuration */
#define MASSMORE_MAX3010X_REG_MODE_CONFIG 0x09
#define MASSMORE_MAX3010X_REG_SPO2_CONFIG 0x0A

#define MASSMORE_MAX3010X_BIT_SHUTDOWN (1u << 7)
#define MASSMORE_MAX3010X_BIT_RESET (1u << 6)
#define MASSMORE_MAX3010X_MASK_SHUTDOWN 0x7F
#define MASSMORE_MAX3010X_MASK_RESET 0xBF
#define MASSMORE_MAX3010X_MASK_MODE 0xF8
#define MASSMORE_MAX3010X_MASK_ADC_RANGE 0x9F
#define MASSMORE_MAX3010X_MASK_SAMPLE_RATE 0xE3
#define MASSMORE_MAX3010X_MASK_PULSE_WIDTH 0xFC

#define MASSMORE_MAX3010X_MODE_VAL_HR 0x02
#define MASSMORE_MAX3010X_MODE_VAL_SPO2 0x03
#define MASSMORE_MAX3010X_MODE_VAL_MULTI_LED 0x07

/* LED pulse amplitude */
#define MASSMORE_MAX3010X_REG_LED1_PA 0x0C  /**< Red */
#define MASSMORE_MAX3010X_REG_LED2_PA 0x0D  /**< IR */
#define MASSMORE_MAX3010X_REG_LED3_PA 0x0E  /**< Green (MAX30101 / MAX30105) */
#define MASSMORE_MAX3010X_REG_LED4_PA 0x0F  /**< Green #2 (MAX30101 only) */
#define MASSMORE_MAX3010X_REG_PILOT_PA 0x10 /**< Proximity pilot (MAX30105 only) */
#define MASSMORE_MAX3010X_LED_STEP_MA 0.2f  /**< ~0.2 mA ต่อ 1 LSB, 0xFF ~ 51 mA */

/* Multi-LED slot control */
#define MASSMORE_MAX3010X_REG_MULTI_LED_1 0x11 /**< SLOT2[6:4], SLOT1[2:0] */
#define MASSMORE_MAX3010X_REG_MULTI_LED_2 0x12 /**< SLOT4[6:4], SLOT3[2:0] */
#define MASSMORE_MAX3010X_MASK_SLOT_ODD 0xF8
#define MASSMORE_MAX3010X_MASK_SLOT_EVEN 0x8F

/* Die temperature */
#define MASSMORE_MAX3010X_REG_DIE_TEMP_INT 0x1F
#define MASSMORE_MAX3010X_REG_DIE_TEMP_FRAC 0x20
#define MASSMORE_MAX3010X_REG_DIE_TEMP_CONFIG 0x21
#define MASSMORE_MAX3010X_BIT_TEMP_EN (1u << 0)
#define MASSMORE_MAX3010X_TEMP_FRAC_STEP 0.0625f

/* Proximity (MAX30105 only) */
#define MASSMORE_MAX3010X_REG_PROX_INT_THRESH 0x30

/* Part ID */
#define MASSMORE_MAX3010X_REG_REVISION_ID 0xFE
#define MASSMORE_MAX3010X_REG_PART_ID 0xFF
#define MASSMORE_MAX3010X_PART_ID_EXPECTED 0x15 /**< MAX30101 / MAX30102 / MAX30105 */
#define MASSMORE_MAX3010X_PART_ID_MAX30100 0x11 /**< MAX30100 (ไม่รองรับ) */

/* Data limits */
#define MASSMORE_MAX3010X_DATA_MASK 0x0003FFFFUL
#define MASSMORE_MAX3010X_DATA_MAX 0x0003FFFFUL /**< ADC 18-bit full scale = 262143 */

/* Datasheet timing (MAX30102): temperature conversion ~29 ms typ */
#define MASSMORE_MAX3010X_TEMP_CONV_MS 29

/* =========================================================================
   Compile-time tuning
   ========================================================================= */

/** ขนาด ring buffer ที่ไลบรารีใช้พักข้อมูลจาก FIFO (หน่วย = sample, 16 bytes/sample)
 *  AVR (2 KB SRAM) ใช้ 8, MCU อื่นใช้ 32 = เท่า FIFO ของชิป
 *  ปรับได้ด้วย -DMASSMORE_MAX3010X_BUFFER_SIZE=<1..255> */
#ifndef MASSMORE_MAX3010X_BUFFER_SIZE
#if defined(__AVR__)
#define MASSMORE_MAX3010X_BUFFER_SIZE 8
#else
#define MASSMORE_MAX3010X_BUFFER_SIZE 32
#endif
#endif
#if MASSMORE_MAX3010X_BUFFER_SIZE < 1 || MASSMORE_MAX3010X_BUFFER_SIZE > 255
#error "MASSMORE_MAX3010X_BUFFER_SIZE must be 1..255"
#endif

/** ระดับ DC ของ IR channel ที่ถือว่ามีนิ้ววางบน sensor (ขึ้นกับกระแส LED) */
#ifndef MASSMORE_MAX3010X_FINGER_THRESHOLD
#define MASSMORE_MAX3010X_FINGER_THRESHOLD 20000UL
#endif

/** จำนวนค่า BPM ล่าสุดที่นำมาเฉลี่ย */
#ifndef MASSMORE_MAX3010X_BPM_AVERAGE_COUNT
#define MASSMORE_MAX3010X_BPM_AVERAGE_COUNT 6
#endif

/* =========================================================================
   Main driver class
   ========================================================================= */

/**
 * @brief คลาสควบคุมเซ็นเซอร์ MAX30102 / MAX30101 / MAX30105 ของ Massmore
 *
 * @code
 * #include <Wire.h>
 * #include <Massmore_MAX3010x.h>
 * Massmore_MAX3010x sensor;
 *
 * void setup() {
 *   Serial.begin(115200);
 *   Wire.begin(21, 22);            // ESP32: sketch เป็นเจ้าของ I2C Bus
 *   Wire.setClock(400000);
 *   if (!sensor.begin(Wire)) { while (1) delay(1000); }
 *   sensor.setupDefault();         // SpO2 mode, 50 samples/s
 * }
 *
 * void loop() {
 *   Massmore_MAX3010x::Readings r;
 *   if (sensor.readAll(r)) { Serial.println(r.ir); }
 * }
 * @endcode
 */
class Massmore_MAX3010x {
 public:
  /* ---------------------------------------------------------------------
     Types
     --------------------------------------------------------------------- */

  /** @brief รหัสข้อผิดพลาดล่าสุด ดูได้จาก lastError() */
  enum class ErrorCode : uint8_t {
    OK = 0,      /**< สำเร็จ */
    NOT_FOUND,   /**< ไม่มีอุปกรณ์ตอบ (NACK) ที่ address 0x57 */
    WRONG_ID,    /**< PART_ID ไม่ใช่ 0x15 (เช่น MAX30100 = 0x11) */
    TIMEOUT,     /**< รอเกินเวลาที่กำหนด */
    BUS_ERROR,   /**< I2C write/read ล้มเหลวหรือได้ byte ไม่ครบ */
    NOT_READY,   /**< ยังไม่มีข้อมูลใหม่ใน FIFO / conversion ยังไม่เสร็จ */
    NOT_BEGUN,   /**< ยังไม่ได้เรียก begin() สำเร็จ */
    BAD_ARG,     /**< parameter ไม่ถูกต้อง */
    UNSUPPORTED  /**< ชิปรุ่นที่ต่ออยู่ไม่มีความสามารถนี้ */
  };

  /** @brief รุ่นชิป (ทุกรุ่นคืน PART_ID 0x15 เท่ากัน ไลบรารีเดารุ่นจาก register ที่มีเฉพาะรุ่น) */
  enum class Variant : uint8_t { AUTO = 0, MAX30102, MAX30101, MAX30105 };

  /** @brief โหมดการทำงานของชิป (MODE[2:0]) */
  enum class Mode : uint8_t {
    HEART_RATE = 0, /**< LED1 (Red) ช่องเดียว */
    SPO2,           /**< Red + IR สองช่อง */
    MULTI_LED       /**< กำหนด time slot เอง 1-4 ช่อง */
  };

  /** @brief จำนวน sample ที่ชิปเฉลี่ยก่อนใส่ FIFO (SMP_AVE) */
  enum class SampleAverage : uint8_t { X1 = 0, X2, X4, X8, X16, X32 };

  /** @brief ช่วง ADC (SPO2_ADC_RGE) หน่วย nA full scale */
  enum class AdcRange : uint8_t { NA_2048 = 0, NA_4096, NA_8192, NA_16384 };

  /** @brief Sample rate (SPO2_SR) หน่วย Hz */
  enum class SampleRate : uint8_t {
    HZ_50 = 0, HZ_100, HZ_200, HZ_400, HZ_800, HZ_1000, HZ_1600, HZ_3200
  };

  /** @brief ความกว้าง LED pulse (LED_PW) ซึ่งกำหนด ADC resolution ไปด้วย */
  enum class PulseWidth : uint8_t {
    US_69 = 0, /**< 15-bit */
    US_118,    /**< 16-bit */
    US_215,    /**< 17-bit */
    US_411     /**< 18-bit */
  };

  /** @brief LED ที่ยิงในแต่ละ time slot ของ Multi-LED mode */
  enum class Slot : uint8_t {
    NONE = 0, RED = 1, IR = 2, GREEN = 3, RED_PILOT = 5, IR_PILOT = 6, GREEN_PILOT = 7
  };

  /** @brief แหล่ง Interrupt ที่เปิด/ปิดได้ */
  enum class InterruptSource : uint8_t {
    ALMOST_FULL,     /**< A_FULL */
    DATA_READY,      /**< PPG_RDY */
    AMBIENT_OVERFLOW,/**< ALC_OVF */
    PROXIMITY,       /**< PROX_INT (MAX30105 only) */
    DIE_TEMP_READY   /**< DIE_TEMP_RDY */
  };

  /** @brief ผลสรุปของ verifyChip() */
  enum class Genuine : uint8_t {
    UNKNOWN = 0,  /**< ยังไม่ได้ตรวจ */
    PASS,         /**< ผ่านครบทุกข้อ */
    PARTIAL,      /**< ผ่านเกือบครบ (ตกไม่เกิน 2 ข้อ) */
    SUSPECT,      /**< ตกหลายข้อ น่าสงสัย */
    NOT_MAX3010X  /**< ไม่ใช่ชิป MAX3010x */
  };

  /** Bit ของ getVerifyMask() (ข้อละ 1 bit, ครบ 11 ข้อ) */
  static const uint16_t CHK_ACK = 1u << 0;         /**< ACK ที่ 0x57 */
  static const uint16_t CHK_PART_ID = 1u << 1;     /**< PART_ID = 0x15 */
  static const uint16_t CHK_REV_ID = 1u << 2;      /**< REV_ID ไม่ใช่ 0x00 / 0xFF */
  static const uint16_t CHK_RESET = 1u << 3;       /**< RESET bit self-clear */
  static const uint16_t CHK_POR_DEFAULT = 1u << 4; /**< ค่าหลัง reset ตรง datasheet */
  static const uint16_t CHK_RW = 1u << 5;          /**< write/read-back ตรง */
  static const uint16_t CHK_READONLY = 1u << 6;    /**< PART_ID เขียนทับไม่ได้ */
  static const uint16_t CHK_RESERVED = 1u << 7;    /**< reserved bits อ่านได้ 0 */
  static const uint16_t CHK_FIFO_PTR = 1u << 8;    /**< FIFO pointer เป็น 5-bit field */
  static const uint16_t CHK_TEMP = 1u << 9;        /**< die temperature สมเหตุสมผล */
  static const uint16_t CHK_LED = 1u << 10;        /**< ADC ตอบสนองเมื่อเปิด LED */
  static const uint8_t CHK_COUNT = 11;
  static const uint16_t CHK_ALL = 0x07FF;

  /** @brief ข้อมูล 1 sample จาก FIFO */
  struct Readings {
    uint32_t red;         /**< Red channel 18-bit (0 ถ้า mode ไม่ได้ใช้) */
    uint32_t ir;          /**< IR channel 18-bit */
    uint32_t green;       /**< Green channel 18-bit (MAX30101/30105) */
    uint32_t timestampMs; /**< millis() ตอนดึงออกจาก FIFO */
  };

  /** @brief การตั้งค่าที่อ่านกลับจากชิปจริง */
  struct Config {
    uint8_t partId;
    uint8_t revisionId;
    Variant variant;
    Mode mode;
    SampleAverage sampleAverage;
    AdcRange adcRange;
    SampleRate sampleRate;
    PulseWidth pulseWidth;
    bool fifoRollover;
    uint8_t fifoAlmostFull;
    uint8_t ledRed;
    uint8_t ledIr;
    uint8_t ledGreen;
    uint8_t ledPilot;
    bool shutdown;
    uint8_t activeChannels;
    float effectiveRateHz;
  };

  /* ---------------------------------------------------------------------
     Construction / begin
     --------------------------------------------------------------------- */

  Massmore_MAX3010x();

  /**
   * @brief  เริ่มใช้งานเซ็นเซอร์ (ไม่เรียก Wire.begin() — sketch ต้องเปิด I2C Bus เองก่อน)
   * @param  wirePort  I2C Bus ที่จะใช้ (Wire หรือ Wire1)
   * @param  intPin    GPIO ที่ต่อขา INT ของบอร์ด, -1 = ไม่ได้ต่อ
   * @return true เมื่อพบชิป, PART_ID = 0x15 และ soft-reset สำเร็จ
   */
  bool begin(TwoWire &wirePort = Wire, int8_t intPin = -1);

  /** @brief ตรวจว่ามีอุปกรณ์ ACK ที่ address 0x57 หรือไม่ */
  bool isConnected();
  /** @brief Soft-reset กลับค่าโรงงาน แล้วรอจน RESET bit self-clear */
  bool softReset();
  /** @brief เข้า Shutdown mode (หยุดวัด, จำค่า register ไว้) */
  bool shutdown();
  /** @brief ออกจาก Shutdown mode */
  bool wakeUp();

  /**
   * @brief  ตั้งค่าชุดมาตรฐานสำหรับวัดที่ปลายนิ้ว
   *         SpO2 mode, SMP_AVE 8, 400 Hz (ได้ 50 samples/s), 411 us (18-bit),
   *         ADC 4096 nA, FIFO rollover, LED ~6 mA
   * @param  ledPower  กระแส LED 0-255 (~0.2 mA/LSB)
   */
  bool setupDefault(uint8_t ledPower = 0x1F);

  /** @brief ตั้งค่าทั้งชุดในคำสั่งเดียว */
  bool setup(uint8_t ledPower, SampleAverage average, Mode mode, SampleRate rate,
             PulseWidth width, AdcRange range);

  /* ---------------------------------------------------------------------
     Individual configuration (Read-Modify-Write ด้วย bitmask)
     --------------------------------------------------------------------- */
  bool setMode(Mode mode);
  bool setSampleAverage(SampleAverage average);
  bool setFifoRollover(bool enable);
  /** @param spacesLeft แจ้ง A_FULL เมื่อ FIFO เหลือที่ว่างกี่ช่อง (0-15) */
  bool setFifoAlmostFull(uint8_t spacesLeft);
  bool setAdcRange(AdcRange range);
  bool setSampleRate(SampleRate rate);
  bool setPulseWidth(PulseWidth width);
  bool setPulseAmplitudeRed(uint8_t value);
  bool setPulseAmplitudeIR(uint8_t value);
  bool setPulseAmplitudeGreen(uint8_t value);
  bool setPulseAmplitudeProximity(uint8_t value);
  bool setAllLedsOff();
  /** @param slotNumber 1-4 */
  bool setMultiLedSlot(uint8_t slotNumber, Slot device);
  bool setProximityThreshold(uint8_t threshold);

  /* ---------------------------------------------------------------------
     Interrupts (ขา INT เป็น open-drain ต้องเปิด pull-up ฝั่ง MCU)
     --------------------------------------------------------------------- */
  bool enableInterrupt(InterruptSource source, bool enable);
  bool disableAllInterrupts();
  /** @brief อ่าน Interrupt Status 1 — การอ่านจะ clear flag และปล่อยขา INT */
  uint8_t getInterruptStatus1();
  /** @brief อ่าน Interrupt Status 2 */
  uint8_t getInterruptStatus2();
  /** @brief GPIO ของขา INT ที่ส่งมาตอน begin() (-1 = ไม่ได้ต่อ) */
  int8_t getIntPin() const { return _intPin; }

  /* ---------------------------------------------------------------------
     Simple Blocking API
     --------------------------------------------------------------------- */

  /**
   * @brief  อ่าน sample ที่เก่าที่สุดแบบ Blocking (รอจนมีข้อมูลหรือ timeout)
   * @param  out        โครงสร้างรับค่า Red / IR / Green
   * @param  timeoutMs  รอนานสุด (ms)
   * @return true เมื่อได้ข้อมูล, false = ดู lastError()
   */
  bool readAll(Readings &out, uint32_t timeoutMs = 200);

  /**
   * @brief  อ่าน Die Temperature แบบ Blocking
   * @return องศาเซลเซียส หรือ NAN เมื่อไม่สำเร็จ (ดู lastError())
   * @note   เป็นอุณหภูมิของตัวชิป ไม่ใช่อุณหภูมิร่างกาย
   */
  float readTemperature(uint32_t timeoutMs = 100);

  /* ---------------------------------------------------------------------
     Advanced Non-blocking FSM API
     --------------------------------------------------------------------- */

  /** @brief ล้าง FIFO ของชิปและ buffer ในไลบรารี แล้วเริ่มรอบเก็บข้อมูลใหม่ */
  bool requestConversion();
  /**
   * @brief  ดึงข้อมูลที่ค้างใน FIFO ของชิปมาเก็บใน ring buffer (ไม่ Block)
   * @return true เมื่อได้ sample ใหม่อย่างน้อย 1 ชุด
   */
  bool update();
  /** @brief มี sample ที่ยังไม่ได้อ่านใน buffer หรือไม่ */
  bool isDataReady() const { return _count > 0; }
  /** @brief ดึง sample ที่เก่าที่สุดออกจาก buffer (FIFO order) */
  bool getReadings(Readings &out);

  /** @brief จำนวน sample ที่ยังไม่ได้อ่านใน buffer */
  uint8_t available() const { return _count; }
  /** @brief เลื่อนไป sample ถัดไป (ใช้คู่กับ getRed/getIR/getGreen) */
  void nextSample();
  uint32_t getRed() const { return (_count == 0) ? 0 : _bufRed[_tail]; }
  uint32_t getIR() const { return (_count == 0) ? 0 : _bufIr[_tail]; }
  uint32_t getGreen() const { return (_count == 0) ? 0 : _bufGreen[_tail]; }
  /** @brief ทิ้งข้อมูลใน buffer ของไลบรารีทั้งหมด */
  void flush();

  /** @brief สั่งเริ่มวัด Die Temperature แบบไม่ Block */
  bool startTemperatureConversion();
  /** @brief วัดอุณหภูมิเสร็จหรือยัง */
  bool isTemperatureReady();
  /** @brief อ่านผลอุณหภูมิที่วัดเสร็จแล้ว (NAN เมื่อไม่สำเร็จ) */
  float getTemperatureResult();

  /* ---------------------------------------------------------------------
     FIFO utilities
     --------------------------------------------------------------------- */
  bool clearFifo();
  uint8_t getWritePointer();
  uint8_t getReadPointer();
  uint8_t getOverflowCounter();
  /** @brief จำนวน sample ที่ค้างใน FIFO ของชิป */
  uint8_t getSamplesInFifo();

  /* ---------------------------------------------------------------------
     Chip identity / authenticity (ตรวจของแท้)
     --------------------------------------------------------------------- */

  /** @brief อ่าน PART_ID (0xFF) เทียบค่าคาดหวัง 0x15 จาก datasheet */
  bool verifyChipID();
  /** @brief MAX3010x ไม่มี serial-number register — คืน 0 เสมอ */
  uint32_t getSerialNumber();
  /**
   * @brief  Heuristic ตรวจของแท้แบบเร็ว = verifyChip() ผ่านครบทุกข้อ
   * @warning จะ reset ชิปและเขียนทับค่าที่ตั้งไว้ ต้อง setup() ใหม่หลังเรียก
   */
  bool isGenuine();
  /**
   * @brief  ตรวจของแท้ 11 ข้อ (PART_ID, REV_ID, reset, POR defaults, R/W,
   *         read-only, reserved bits, FIFO pointer wrap, die temp, LED response)
   * @warning จะ reset ชิปและเขียนทับค่าที่ตั้งไว้ ต้อง setup() ใหม่หลังเรียก
   */
  Genuine verifyChip();
  uint16_t getVerifyMask() const { return _verifyMask; }
  uint8_t getVerifyPassCount() const;
  /** @brief ชื่อข้อตรวจ (English, index 0-10) */
  static const char *getVerifyCheckName(uint8_t index);

  uint8_t readPartID();
  uint8_t readRevisionID();
  Variant getVariant() const { return _variant; }
  void setVariant(Variant variant);
  const char *getVariantName() const;
  bool hasGreenLed() const;
  bool hasProximity() const;

  /* ---------------------------------------------------------------------
     Configuration read-back / helpers
     --------------------------------------------------------------------- */
  bool readConfiguration(Config &out);
  uint8_t getActiveChannels() const { return _activeChannels; }
  /** @brief อัตราข้อมูลจริงที่ออกจาก FIFO (sample rate / average) หน่วย Hz */
  float getEffectiveSampleRate() const;
  static float ledCodeToMilliAmp(uint8_t code) { return (float)code * MASSMORE_MAX3010X_LED_STEP_MA; }
  static uint8_t milliAmpToLedCode(float milliAmp);

  /* ---------------------------------------------------------------------
     Raw register access
     --------------------------------------------------------------------- */
  bool readRegister8(uint8_t reg, uint8_t &value);
  uint8_t readRegister8(uint8_t reg);
  bool writeRegister8(uint8_t reg, uint8_t value);
  /** @brief Read-Modify-Write: reg = (reg & keepMask) | value */
  bool maskRegister8(uint8_t reg, uint8_t keepMask, uint8_t value);
  bool readRegisterBurst(uint8_t reg, uint8_t *buffer, uint8_t length);

  /* ---------------------------------------------------------------------
     Status
     --------------------------------------------------------------------- */
  ErrorCode lastError() const { return _lastError; }
  /** @brief คำอธิบาย error (English) */
  static const char *errorToString(ErrorCode error);
  const char *lastErrorString() const { return errorToString(_lastError); }
  bool isBegun() const { return _begun; }
  void setTimeout(uint32_t ms) { _timeoutMs = ms; }

  /* =====================================================================
     Signal-processing helpers (optional, streaming, zero heap)
     ===================================================================== */

  /**
   * @brief จับจังหวะการเต้นของหัวใจจากสัญญาณ PPG ทีละ sample
   *
   * ขั้นตอน: High-pass (ตัด DC) → Low-pass (ตัด noise) → adaptive envelope →
   * นับเมื่อสัญญาณตัด threshold ขาขึ้น พร้อม refractory period
   * ใช้ RAM ต่ำกว่า 100 bytes ทำงานได้บน ATmega328P
   */
  class BeatDetector {
   public:
    BeatDetector();
    /** @param sampleRateHz อัตราข้อมูลจริง (ดู getEffectiveSampleRate()) */
    void begin(float sampleRateHz = 50.0f);
    void reset();
    /** @return true เฉพาะ sample ที่ตรวจพบจังหวะเต้นครั้งใหม่ */
    bool check(uint32_t sample);
    float getBeatsPerMinute() const { return _bpm; }
    float getAverageBeatsPerMinute() const { return _bpmAverage; }
    uint32_t getBeatCount() const { return _beatCount; }
    float getFilteredSignal() const { return _filtered; }
    float getDcLevel() const { return _dc; }
    float getAmplitude() const { return _envelopeHigh - _envelopeLow; }
    bool isReady() const { return _primed; }
    bool isFingerPresent() const { return _dc >= (float)_fingerThreshold; }
    void setFingerThreshold(uint32_t threshold) { _fingerThreshold = threshold; }
    void setBpmRange(float minBpm, float maxBpm);

   private:
    float _sampleRate, _dcAlpha, _lpAlpha, _envelopeDecay;
    float _dc, _filtered, _envelopeHigh, _envelopeLow;
    bool _aboveThreshold, _primed;
    uint32_t _sampleIndex, _lastBeatIndex, _warmupSamples;
    float _bpm, _bpmAverage;
    float _bpmHistory[MASSMORE_MAX3010X_BPM_AVERAGE_COUNT];
    uint8_t _bpmHistoryCount, _bpmHistoryIndex;
    uint32_t _beatCount, _fingerThreshold;
    float _minBpm, _maxBpm;
  };

  /** @brief ผลลัพธ์ของ Oximeter */
  struct OximeterResult {
    float spo2;          /**< % ออกซิเจนในเลือด 70-100 */
    float heartRate;     /**< BPM จาก BeatDetector ภายใน */
    float ratio;         /**< ค่า R (ratio-of-ratios) สำหรับสอบเทียบเอง */
    float perfusionIr;   /**< Perfusion Index ช่อง IR (%) */
    float perfusionRed;  /**< Perfusion Index ช่อง Red (%) */
    float dcIr;
    float dcRed;
    bool spo2Valid;
    bool heartRateValid;
    bool fingerPresent;
  };

  /**
   * @brief คำนวณ SpO2 ด้วยวิธี ratio-of-ratios แบบ streaming (ไม่มี window buffer)
   *
   *   R    = (AC_red / DC_red) / (AC_ir / DC_ir)   — AC วัดเป็น RMS ผ่าน exponential average
   *   SpO2 = a·R² + b·R + c   (ค่าเริ่มต้น -45.060, 30.354, 94.845)
   *
   * เส้นโค้งสอบเทียบเป็นค่ามาตรฐานทั่วไป ไม่ได้สอบเทียบกับบอร์ดนี้โดยเฉพาะ
   * ปรับได้ด้วย setCalibration()
   */
  class Oximeter {
   public:
    Oximeter();
    /**
     * @param sampleRateHz        อัตราข้อมูลจริง
     * @param updateEverySamples  คำนวณผลใหม่ทุกกี่ sample (0 = ทุก sample)
     */
    void begin(float sampleRateHz = 50.0f, uint16_t updateEverySamples = 25);
    void reset();
    /** @return true เมื่อมีผลลัพธ์ชุดใหม่ */
    bool add(uint32_t red, uint32_t ir);
    const OximeterResult &getResult() const { return _result; }
    float getSpO2() const { return _result.spo2; }
    float getHeartRate() const { return _result.heartRate; }
    float getRatio() const { return _result.ratio; }
    bool isSpO2Valid() const { return _result.spo2Valid; }
    bool isFingerPresent() const { return _result.fingerPresent; }
    /** @brief สัดส่วน warm-up 0.0-1.0 */
    float getFillRatio() const;
    void setCalibration(float a, float b, float c);
    void setFingerThreshold(uint32_t threshold);
    BeatDetector &beat() { return _beat; }

   private:
    float _sampleRate, _dcAlpha, _lpAlpha, _rmsAlpha;
    float _dcRed, _dcIr, _lpRed, _lpIr, _msRed, _msIr;
    uint32_t _samples, _warmupSamples;
    uint16_t _sinceUpdate, _updateEvery;
    float _calA, _calB, _calC;
    uint32_t _fingerThreshold;
    BeatDetector _beat;
    OximeterResult _result;
    void compute();
  };

 private:
  TwoWire *_wire;
  int8_t _intPin;
  bool _begun;
  uint32_t _timeoutMs;
  ErrorCode _lastError;
  Variant _variant;
  bool _variantForced;
  uint16_t _verifyMask;

  Mode _mode;
  SampleAverage _sampleAverage;
  SampleRate _sampleRate;
  uint8_t _activeChannels;

  uint32_t _bufRed[MASSMORE_MAX3010X_BUFFER_SIZE];
  uint32_t _bufIr[MASSMORE_MAX3010X_BUFFER_SIZE];
  uint32_t _bufGreen[MASSMORE_MAX3010X_BUFFER_SIZE];
  uint32_t _bufMs[MASSMORE_MAX3010X_BUFFER_SIZE];
  uint8_t _head, _tail, _count;

  void pushSample(uint32_t red, uint32_t ir, uint32_t green);
  void recomputeChannels();
  bool detectVariant();
  bool waitForBit(uint8_t reg, uint8_t bitMask, bool wantSet, uint32_t timeoutMs);
};

#endif /* MASSMORE_MAX3010X_H */
