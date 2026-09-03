/*!
 * @file Massmore_MAX3010x.h
 * @brief ไลบรารี Arduino / PlatformIO สำหรับเซ็นเซอร์ชีพจรและออกซิเจนในเลือด
 *        ตระกูล MAX3010x ของ Analog Devices (Maxim)
 *        รองรับ MAX30102 (รุ่นที่ Massmore จำหน่าย), MAX30101 และ MAX30105
 *
 * บอร์ด Massmore MAX30102 Pulse Oximeter & Heart-Rate Sensor  SKU-0026
 * https://www.massmore.shop
 *
 * จุดเด่นของไลบรารีตัวนี้
 *   - เขียนขึ้นจาก datasheet โดยตรง ไม่พึ่งไลบรารีอื่นนอกจาก Wire
 *   - ไม่ใช้ heap เลย ไม่มี new / malloc / String ในส่วนแกน
 *   - ครอบคลุมทุกฟังก์ชันของชิป ตั้งแต่ FIFO, อินเทอร์รัปต์, multi-LED,
 *     อุณหภูมิแกนชิป, proximity ไปจนถึงการเขียนอ่านรีจิสเตอร์ตรง ๆ
 *   - มี verifyChip() ตรวจว่าเป็นชิป Maxim ของแท้ ไม่ใช่ของเลียนแบบ
 *   - มีอัลกอริทึมวัดชีพจรและ SpO2 ให้พร้อมใน Massmore_MAX3010x_Algorithms.h
 *
 * @warning บอร์ดนี้เป็นอุปกรณ์สำหรับการเรียนรู้และงานสร้างต้นแบบเท่านั้น
 *          ไม่ใช่เครื่องมือแพทย์ ห้ามใช้วินิจฉัยหรือรักษาโรค
 *
 * @copyright Copyright (c) 2026 Massmore Biz Co., Ltd.
 * @license MIT
 */

#ifndef MASSMORE_MAX3010X_H
#define MASSMORE_MAX3010X_H

#include "Massmore_MAX3010x_Registers.h"

#ifdef ARDUINO
#include <Arduino.h>
#include <Wire.h>
#else
/* ใช้ตอนคอมไพล์ host test บนเครื่อง PC ไฟล์ mock อยู่ในโฟลเดอร์ test/ */
#include "massmore_max3010x_host_shim.h"
#endif

/*! เวอร์ชันของไลบรารี */
#define MASSMORE_MAX3010X_VERSION_MAJOR 1
#define MASSMORE_MAX3010X_VERSION_MINOR 0
#define MASSMORE_MAX3010X_VERSION_PATCH 0
#define MASSMORE_MAX3010X_VERSION_STRING "1.0.0"

/*! ความถี่ I2C ปริยาย 400 kHz ซึ่งเป็นค่าสูงสุดที่ datasheet รับรอง
 *  ถ้าใช้สายยาวเกิน 30 ซม. หรือเจอข้อผิดพลาดบนบัส ให้ลดเป็น 100000UL */
#define MASSMORE_MAX3010X_I2C_FREQ_DEFAULT 400000UL

/*! เวลารอสูงสุด (ms) ตอนคุยกับชิป */
#define MASSMORE_MAX3010X_TIMEOUT_DEFAULT_MS 100

/*! ขนาดบัฟเฟอร์วงแหวนที่ไลบรารีใช้เก็บตัวอย่างจาก FIFO
 *  ตั้งทับได้ด้วย -D MASSMORE_MAX3010X_BUFFER_SIZE=... ตอนคอมไพล์
 *  ใช้หน่วยความจำ = ขนาด x 12 ไบต์ (แดง + อินฟราเรด + เขียว อย่างละ 4 ไบต์) */
#ifndef MASSMORE_MAX3010X_BUFFER_SIZE
#define MASSMORE_MAX3010X_BUFFER_SIZE 32
#endif

#if MASSMORE_MAX3010X_BUFFER_SIZE < 1 || MASSMORE_MAX3010X_BUFFER_SIZE > 255
#error "MASSMORE_MAX3010X_BUFFER_SIZE ต้องอยู่ระหว่าง 1 ถึง 255 เพราะตัวชี้บัฟเฟอร์เป็น uint8_t"
#endif

/* =========================================================================
   ชนิดข้อมูล
   ========================================================================= */

/*!
 * @brief รุ่นของชิปในตระกูล MAX3010x
 *
 * ทั้งสามรุ่นคืนค่า PART_ID = 0x15 เท่ากันหมด ชิปจึงไม่ได้บอกรุ่นตัวเองตรง ๆ
 * ไลบรารีจะเดารุ่นให้อัตโนมัติจากการทดสอบว่ามีไดรเวอร์ LED ช่องที่ 3 (สีเขียว)
 * และรีจิสเตอร์ proximity หรือไม่ ถ้าอยากตัดปัญหาให้ระบุรุ่นเองตอน begin()
 */
typedef enum {
  MASSMORE_MAX3010X_VARIANT_AUTO = 0, /*!< ให้ไลบรารีเดารุ่นให้เอง (ค่าเริ่มต้น) */
  MASSMORE_MAX3010X_VARIANT_MAX30102, /*!< 2 ดวง แดง + อินฟราเรด ไม่มี proximity */
  MASSMORE_MAX3010X_VARIANT_MAX30101, /*!< 4 ไดรเวอร์ แดง + IR + เขียว 2 ดวง */
  MASSMORE_MAX3010X_VARIANT_MAX30105  /*!< 3 ดวง แดง + IR + เขียว มี proximity */
} massmore_max3010x_variant_t;

/*!
 * @brief โหมดการทำงานของชิป
 */
typedef enum {
  MASSMORE_MAX3010X_MODE_HR = 0,   /*!< LED1 ดวงเดียว (MAX30102 คือสีแดง) 1 ช่องข้อมูล */
  MASSMORE_MAX3010X_MODE_SPO2,     /*!< แดง + อินฟราเรด 2 ช่องข้อมูล ใช้กับ SpO2 */
  MASSMORE_MAX3010X_MODE_MULTI_LED /*!< เลือกเองได้ 1-4 ช่องเวลา ใช้ LED สีเขียวได้ */
} massmore_max3010x_mode_t;

/*!
 * @brief จำนวนตัวอย่างที่ชิปเฉลี่ยให้ก่อนใส่ลง FIFO
 *
 * เฉลี่ยมากขึ้น = สัญญาณนิ่งขึ้น แต่อัตราข้อมูลที่ออกจาก FIFO ลดลงตามตัวหาร
 * เช่น ตั้งอัตราสุ่ม 400 Hz แล้วเฉลี่ย 8 ตัว จะได้ข้อมูลออกมา 50 ชุดต่อวินาที
 */
typedef enum {
  MASSMORE_MAX3010X_SMP_AVE_1 = 0, /*!< ไม่เฉลี่ย */
  MASSMORE_MAX3010X_SMP_AVE_2,
  MASSMORE_MAX3010X_SMP_AVE_4,
  MASSMORE_MAX3010X_SMP_AVE_8,
  MASSMORE_MAX3010X_SMP_AVE_16,
  MASSMORE_MAX3010X_SMP_AVE_32
} massmore_max3010x_smp_ave_t;

/*!
 * @brief ช่วงการวัดของ ADC (กระแสสูงสุดที่โฟโตไดโอดรับได้)
 *
 * ช่วงกว้างขึ้น = ไม่อิ่มตัวง่าย แต่ความละเอียดต่อขั้นหยาบลง
 */
typedef enum {
  MASSMORE_MAX3010X_ADC_RANGE_2048 = 0, /*!< 2048 nA  ละเอียด 7.81 pA ต่อขั้น */
  MASSMORE_MAX3010X_ADC_RANGE_4096,     /*!< 4096 nA  ละเอียด 15.63 pA */
  MASSMORE_MAX3010X_ADC_RANGE_8192,     /*!< 8192 nA  ละเอียด 31.25 pA */
  MASSMORE_MAX3010X_ADC_RANGE_16384     /*!< 16384 nA ละเอียด 62.5 pA */
} massmore_max3010x_adc_range_t;

/*!
 * @brief อัตราสุ่มของชิป (ครั้งต่อวินาที ก่อนนำไปเฉลี่ย)
 *
 * @note อัตราสูง ๆ ต้องใช้ความกว้างพัลส์สั้นลงด้วย ไม่งั้นชิปยิงไม่ทัน
 *       ดูตารางความเข้ากันได้ในหัวข้อ SpO2 Configuration ของ datasheet
 */
typedef enum {
  MASSMORE_MAX3010X_RATE_50 = 0,
  MASSMORE_MAX3010X_RATE_100,
  MASSMORE_MAX3010X_RATE_200,
  MASSMORE_MAX3010X_RATE_400,
  MASSMORE_MAX3010X_RATE_800,
  MASSMORE_MAX3010X_RATE_1000,
  MASSMORE_MAX3010X_RATE_1600,
  MASSMORE_MAX3010X_RATE_3200
} massmore_max3010x_rate_t;

/*!
 * @brief ความกว้างพัลส์ของ LED ซึ่งกำหนดความละเอียดของ ADC ไปพร้อมกัน
 */
typedef enum {
  MASSMORE_MAX3010X_PULSE_69US = 0, /*!< ADC 15 บิต */
  MASSMORE_MAX3010X_PULSE_118US,    /*!< ADC 16 บิต */
  MASSMORE_MAX3010X_PULSE_215US,    /*!< ADC 17 บิต */
  MASSMORE_MAX3010X_PULSE_411US     /*!< ADC 18 บิต (ค่าเริ่มต้นของไลบรารี) */
} massmore_max3010x_pulse_width_t;

/*!
 * @brief LED ที่จะให้ยิงในแต่ละช่องเวลาของโหมด multi-LED
 */
typedef enum {
  MASSMORE_MAX3010X_SLOT_NONE = 0,      /*!< ปิดช่องเวลานี้ */
  MASSMORE_MAX3010X_SLOT_RED = 1,       /*!< LED1 สีแดง */
  MASSMORE_MAX3010X_SLOT_IR = 2,        /*!< LED2 อินฟราเรด */
  MASSMORE_MAX3010X_SLOT_GREEN = 3,     /*!< LED3 สีเขียว (MAX30101 / MAX30105) */
  MASSMORE_MAX3010X_SLOT_RED_PILOT = 5, /*!< LED1 ใช้กระแสจากรีจิสเตอร์ PILOT_PA */
  MASSMORE_MAX3010X_SLOT_IR_PILOT = 6,  /*!< LED2 ใช้กระแส PILOT_PA */
  MASSMORE_MAX3010X_SLOT_GREEN_PILOT = 7 /*!< LED3 ใช้กระแส PILOT_PA */
} massmore_max3010x_slot_t;

/*!
 * @brief รหัสผลลัพธ์ของทุกฟังก์ชันที่คุยกับชิป
 *
 * ฟังก์ชันส่วนใหญ่คืน bool เพื่อให้เขียนง่าย แล้วเก็บรหัสละเอียดไว้ที่
 * lastError() ให้ไปดูตอนเกิดปัญหา
 */
typedef enum {
  MASSMORE_MAX3010X_OK = 0,          /*!< สำเร็จ */
  MASSMORE_MAX3010X_ERR_NOT_BEGUN,   /*!< ยังไม่ได้เรียก begin() */
  MASSMORE_MAX3010X_ERR_NO_DEVICE,   /*!< ไม่มีอุปกรณ์ตอบที่ address 0x57 */
  MASSMORE_MAX3010X_ERR_I2C_WRITE,   /*!< เขียนลงบัสไม่สำเร็จ */
  MASSMORE_MAX3010X_ERR_I2C_READ,    /*!< อ่านได้ไบต์ไม่ครบ */
  MASSMORE_MAX3010X_ERR_TIMEOUT,     /*!< รอเกินเวลาที่ตั้งไว้ */
  MASSMORE_MAX3010X_ERR_WRONG_CHIP,  /*!< มีอุปกรณ์อยู่ แต่ PART_ID ไม่ใช่ 0x15 */
  MASSMORE_MAX3010X_ERR_NO_DATA,     /*!< FIFO ยังไม่มีข้อมูลใหม่ */
  MASSMORE_MAX3010X_ERR_BAD_ARG,     /*!< พารามิเตอร์ไม่ถูกต้อง */
  MASSMORE_MAX3010X_ERR_UNSUPPORTED  /*!< ชิปรุ่นที่ต่ออยู่ไม่มีความสามารถนี้ */
} massmore_max3010x_error_t;

/*!
 * @brief ผลการตรวจสอบว่าเป็นชิป Maxim แท้หรือไม่
 * @see MassmoreMAX3010x::verifyChip()
 */
typedef enum {
  MASSMORE_MAX3010X_GENUINE_UNKNOWN = 0,  /*!< ยังไม่ได้ตรวจ */
  MASSMORE_MAX3010X_GENUINE_PASS,         /*!< ผ่านครบทุกข้อ = ชิปแท้ */
  MASSMORE_MAX3010X_GENUINE_PARTIAL,      /*!< ผ่านเป็นส่วนใหญ่ แต่มีบางข้อไม่ผ่าน */
  MASSMORE_MAX3010X_GENUINE_SUSPECT,      /*!< ตอบผิดหลายข้อ น่าสงสัย */
  MASSMORE_MAX3010X_GENUINE_NOT_MAX3010X  /*!< มีอุปกรณ์อยู่ แต่ไม่ใช่ MAX3010x แน่นอน */
} massmore_max3010x_genuine_t;

/*! หมายเลขข้อของ verifyChip() ใช้เป็นบิตใน getVerifyMask() */
#define MASSMORE_MAX3010X_CHK_ACK (1u << 0)        /*!< ชิป ACK ที่ address 0x57 */
#define MASSMORE_MAX3010X_CHK_PART_ID (1u << 1)    /*!< PART_ID = 0x15 ตาม datasheet */
#define MASSMORE_MAX3010X_CHK_REV_ID (1u << 2)     /*!< REV_ID ไม่ใช่ 0x00 และไม่ใช่ 0xFF */
#define MASSMORE_MAX3010X_CHK_RESET (1u << 3)      /*!< สั่ง RESET แล้วบิตเคลียร์ตัวเองได้ */
#define MASSMORE_MAX3010X_CHK_POR_DEFAULT (1u << 4)/*!< ค่าหลังรีเซ็ตตรงกับ datasheet */
#define MASSMORE_MAX3010X_CHK_RW (1u << 5)         /*!< เขียนค่าลงรีจิสเตอร์แล้วอ่านกลับได้ตรง */
#define MASSMORE_MAX3010X_CHK_READONLY (1u << 6)   /*!< เขียนทับ PART_ID ไม่ได้ (อ่านอย่างเดียวจริง) */
#define MASSMORE_MAX3010X_CHK_RESERVED (1u << 7)   /*!< บิตสงวนใน MODE_CONFIG เป็น 0 เสมอ */
#define MASSMORE_MAX3010X_CHK_FIFO_PTR (1u << 8)   /*!< ตัวชี้ FIFO ล้นกลับที่ 0 เมื่อเกิน 31 */
#define MASSMORE_MAX3010X_CHK_TEMP (1u << 9)       /*!< วัดอุณหภูมิแกนชิปได้ค่าสมเหตุสมผล */
#define MASSMORE_MAX3010X_CHK_LED (1u << 10)       /*!< เปิด LED แล้วค่าที่ ADC อ่านได้ขยับจริง */
#define MASSMORE_MAX3010X_CHK_ALL 0x07FFu          /*!< ครบทั้ง 11 ข้อ */
#define MASSMORE_MAX3010X_CHK_COUNT 11

/*!
 * @brief ข้อมูลหนึ่งชุดที่อ่านมาจาก FIFO
 */
typedef struct {
  uint32_t red;         /*!< ค่าดิบช่องสีแดง 18 บิต (0 ถ้าโหมดปัจจุบันไม่ได้ใช้) */
  uint32_t ir;          /*!< ค่าดิบช่องอินฟราเรด 18 บิต */
  uint32_t green;       /*!< ค่าดิบช่องสีเขียว 18 บิต (เฉพาะ MAX30101/30105) */
  uint32_t timestampMs; /*!< millis() ตอนที่ไลบรารีดึงข้อมูลชุดนี้ออกมา */
} massmore_max3010x_sample_t;

/*!
 * @brief ภาพรวมการตั้งค่าปัจจุบันที่อ่านกลับมาจากชิป
 */
typedef struct {
  uint8_t partId;                            /*!< ค่าจากรีจิสเตอร์ 0xFF */
  uint8_t revisionId;                        /*!< ค่าจากรีจิสเตอร์ 0xFE */
  massmore_max3010x_variant_t variant;       /*!< รุ่นที่ตรวจพบหรือที่ผู้ใช้ระบุ */
  massmore_max3010x_mode_t mode;             /*!< โหมดที่ชิปทำงานอยู่ */
  massmore_max3010x_smp_ave_t sampleAverage; /*!< จำนวนตัวอย่างที่เฉลี่ย */
  massmore_max3010x_adc_range_t adcRange;    /*!< ช่วง ADC */
  massmore_max3010x_rate_t sampleRate;       /*!< อัตราสุ่มดิบ */
  massmore_max3010x_pulse_width_t pulseWidth;/*!< ความกว้างพัลส์ */
  bool fifoRollover;                         /*!< FIFO วนทับเมื่อเต็มหรือไม่ */
  uint8_t fifoAlmostFull;                    /*!< เหลือที่ว่างกี่ช่องจึงจะแจ้ง A_FULL */
  uint8_t ledRed;                            /*!< ค่ากระแส LED1 (0-255) */
  uint8_t ledIr;                             /*!< ค่ากระแส LED2 */
  uint8_t ledGreen;                          /*!< ค่ากระแส LED3 */
  uint8_t ledPilot;                          /*!< ค่ากระแส PILOT */
  bool shutdown;                             /*!< ชิปอยู่ในโหมดประหยัดไฟหรือไม่ */
  uint8_t activeChannels;                    /*!< จำนวนช่องข้อมูลต่อ 1 ตัวอย่าง (1-4) */
  float effectiveRateHz;                     /*!< อัตราข้อมูลจริงที่ออกจาก FIFO */
} massmore_max3010x_config_t;

/* =========================================================================
   คลาสหลัก
   ========================================================================= */

/*!
 * @brief คลาสควบคุมเซ็นเซอร์ MAX30101 / MAX30102 / MAX30105
 *
 * ตัวอย่างสั้นที่สุดที่ใช้งานได้จริง
 * @code
 * #include <Massmore_MAX3010x.h>
 * MassmoreMAX3010x sensor;
 *
 * void setup() {
 *   Serial.begin(115200);
 *   if (!sensor.begin(Wire, MASSMORE_MAX3010X_I2C_ADDRESS, 21, 22)) {
 *     Serial.println("ไม่พบเซ็นเซอร์");
 *     while (1) delay(1000);
 *   }
 *   sensor.setupDefault();   // ตั้งค่าชุดมาตรฐานสำหรับวัดที่ปลายนิ้ว
 * }
 *
 * void loop() {
 *   if (sensor.update() && sensor.available()) {
 *     Serial.println(sensor.getIR());
 *     sensor.nextSample();
 *   }
 * }
 * @endcode
 */
class MassmoreMAX3010x {
 public:
  MassmoreMAX3010x();

  /* ---------------------------------------------------------------------
     เริ่มต้นใช้งาน
     --------------------------------------------------------------------- */

  /*!
   * @brief เริ่มใช้งานเซ็นเซอร์ พร้อมเปิดบัส I2C ให้ด้วย
   * @param wire อ็อบเจกต์บัส I2C ที่จะใช้ (ปกติคือ Wire)
   * @param address ที่อยู่ I2C ปกติคือ 0x57 ซึ่งชิปตรึงมาจากโรงงาน
   * @param sdaPin ขา SDA ใส่ -1 ถ้าเปิด Wire.begin() เองไว้แล้ว
   * @param sclPin ขา SCL ใส่ -1 ถ้าเปิด Wire.begin() เองไว้แล้ว
   * @param frequency ความถี่บัส หน่วย Hz
   * @return true เมื่อพบชิปและ PART_ID ถูกต้อง
   */
  bool begin(TwoWire &wire = Wire,
             uint8_t address = MASSMORE_MAX3010X_I2C_ADDRESS,
             int8_t sdaPin = -1, int8_t sclPin = -1,
             uint32_t frequency = MASSMORE_MAX3010X_I2C_FREQ_DEFAULT);

  /*!
   * @brief ตั้งค่าชุดมาตรฐานสำหรับวัดชีพจรและ SpO2 ที่ปลายนิ้ว
   *
   * ค่าที่ตั้งให้  โหมด SpO2 (แดง + IR), เฉลี่ย 8 ตัว, อัตราสุ่ม 400 Hz
   * (ได้ข้อมูลออกมา 50 ชุด/วินาที), พัลส์ 411 us = ADC 18 บิต,
   * ช่วง ADC 4096 nA, กระแส LED ~6 mA ทั้งสองดวง, FIFO วนทับได้
   *
   * @param ledPowerLevel ค่ากระแส LED 0-255 (1 หน่วย ~ 0.2 mA)
   * @return true เมื่อเขียนค่าลงชิปครบทุกตัว
   */
  bool setupDefault(uint8_t ledPowerLevel = 0x1F);

  /*!
   * @brief ตั้งค่าทั้งชุดในคำสั่งเดียว สำหรับคนที่อยากคุมเองทุกอย่าง
   */
  bool setup(uint8_t ledPowerLevel, massmore_max3010x_smp_ave_t sampleAverage,
             massmore_max3010x_mode_t mode, massmore_max3010x_rate_t sampleRate,
             massmore_max3010x_pulse_width_t pulseWidth,
             massmore_max3010x_adc_range_t adcRange);

  /*! @brief ตรวจว่ามีอุปกรณ์ตอบที่ address นี้หรือไม่ */
  bool isConnected();

  /*! @brief สั่งรีเซ็ตชิปกลับค่าโรงงาน แล้วรอจนเสร็จ (ไม่เกิน 100 ms) */
  bool softReset();

  /*! @brief เข้าโหมดประหยัดไฟ ชิปหยุดวัด แต่ยังจำค่าที่ตั้งไว้ */
  bool shutdown();

  /*! @brief ออกจากโหมดประหยัดไฟ กลับมาวัดต่อด้วยค่าเดิม */
  bool wakeUp();

  /* ---------------------------------------------------------------------
     การตั้งค่าแต่ละหัวข้อ
     --------------------------------------------------------------------- */

  bool setMode(massmore_max3010x_mode_t mode);
  bool setSampleAverage(massmore_max3010x_smp_ave_t average);
  bool setFifoRollover(bool enable);
  /*! @param spacesLeft แจ้งเตือน A_FULL เมื่อ FIFO เหลือที่ว่างกี่ช่อง (0-15) */
  bool setFifoAlmostFull(uint8_t spacesLeft);
  bool setAdcRange(massmore_max3010x_adc_range_t range);
  bool setSampleRate(massmore_max3010x_rate_t rate);
  bool setPulseWidth(massmore_max3010x_pulse_width_t width);

  bool setPulseAmplitudeRed(uint8_t value);
  bool setPulseAmplitudeIR(uint8_t value);
  bool setPulseAmplitudeGreen(uint8_t value);
  bool setPulseAmplitudeProximity(uint8_t value);
  /*! @brief ปิด LED ทุกดวง ใช้ตอนต้องการประหยัดไฟชั่วคราวโดยไม่ shutdown */
  bool setAllLedsOff();

  /*!
   * @brief กำหนดว่าช่องเวลาที่ N จะยิง LED ดวงไหน (ใช้ในโหมด multi-LED)
   * @param slotNumber 1 ถึง 4
   */
  bool setMultiLedSlot(uint8_t slotNumber, massmore_max3010x_slot_t device);

  /*! @brief ตั้งระดับ IR ที่ถือว่ามีวัตถุเข้าใกล้ (MAX30105 เท่านั้น) */
  bool setProximityThreshold(uint8_t threshold);

  /* ---------------------------------------------------------------------
     อินเทอร์รัปต์
     --------------------------------------------------------------------- */

  /*! @brief อ่านรีจิสเตอร์สถานะชุดที่ 1 การอ่านจะเคลียร์แฟล็กไปด้วย */
  uint8_t getInterruptStatus1();
  /*! @brief อ่านรีจิสเตอร์สถานะชุดที่ 2 การอ่านจะเคลียร์แฟล็กไปด้วย */
  uint8_t getInterruptStatus2();

  bool enableInterruptAlmostFull(bool enable);
  bool enableInterruptDataReady(bool enable);
  bool enableInterruptAmbientLightOverflow(bool enable);
  bool enableInterruptProximity(bool enable);
  bool enableInterruptDieTemperature(bool enable);
  /*! @brief ปิดอินเทอร์รัปต์ทุกชนิดในครั้งเดียว */
  bool disableAllInterrupts();

  /* ---------------------------------------------------------------------
     FIFO
     --------------------------------------------------------------------- */

  bool clearFifo();
  uint8_t getWritePointer();
  uint8_t getReadPointer();
  uint8_t getOverflowCounter();

  /*! @brief จำนวนตัวอย่างที่ค้างอยู่ใน FIFO ของชิป ณ ขณะนี้ */
  uint8_t getSamplesInFifo();

  /*!
   * @brief ดึงข้อมูลที่ค้างใน FIFO ของชิปทั้งหมดมาเก็บในบัฟเฟอร์ของไลบรารี
   *
   * ฟังก์ชันนี้ไม่บล็อก เรียกบ่อยแค่ไหนก็ได้ ถ้าไม่มีข้อมูลใหม่จะคืน false
   * ทันทีโดยไม่เสียเวลา
   *
   * @note ถ้าบัสมีปัญหากลางคัน แต่ดึงข้อมูลมาได้แล้วบางส่วน ฟังก์ชันจะคืน true
   *       พร้อมทิ้งรหัสข้อผิดพลาดไว้ที่ lastError() ให้ตรวจสอบเอง
   *
   * @return true เมื่อได้ข้อมูลใหม่อย่างน้อย 1 ชุด
   */
  bool update();

  /*! @brief จำนวนตัวอย่างที่ยังไม่ได้อ่านออกจากบัฟเฟอร์ของไลบรารี */
  uint8_t available() const;

  /*! @brief เลื่อนตัวชี้ไปตัวอย่างถัดไป เรียกหลังอ่าน getRed/getIR/getGreen เสร็จ */
  void nextSample();

  uint32_t getRed() const;   /*!< ค่าดิบสีแดงของตัวอย่างที่ตัวชี้อยู่ */
  uint32_t getIR() const;    /*!< ค่าดิบอินฟราเรดของตัวอย่างที่ตัวชี้อยู่ */
  uint32_t getGreen() const; /*!< ค่าดิบสีเขียวของตัวอย่างที่ตัวชี้อยู่ */

  /*! @brief คัดลอกตัวอย่างที่ตัวชี้อยู่ออกมาทั้งชุด */
  bool peekSample(massmore_max3010x_sample_t &out) const;

  /*!
   * @brief อ่านตัวอย่างเก่าที่สุดในคิว 1 ชุดแบบบล็อก เหมาะกับโค้ดง่าย ๆ
   *
   * ถ้าคิวว่างจะเรียก update() ให้เองแล้วรอจนกว่าจะมีข้อมูลหรือหมดเวลา
   * ทำงานแบบเข้าก่อนออกก่อน จึงได้ลำดับเวลาที่ถูกต้องเสมอ
   *
   * @param out โครงสร้างที่จะรับค่า
   * @param timeoutMs รอนานสุดกี่มิลลิวินาที
   */
  bool readSample(massmore_max3010x_sample_t &out, uint32_t timeoutMs = 200);

  /*! @brief ทิ้งข้อมูลเก่าในบัฟเฟอร์ของไลบรารีทั้งหมด */
  void flush();

  /* ---------------------------------------------------------------------
     อุณหภูมิแกนชิป
     --------------------------------------------------------------------- */

  /*!
   * @brief วัดอุณหภูมิแกนชิปแบบบล็อก
   * @return องศาเซลเซียส หรือ NAN เมื่อวัดไม่สำเร็จ
   * @note นี่คืออุณหภูมิของตัวชิปเอง มีไว้ชดเชยค่าความยาวคลื่น LED
   *       ไม่ใช่อุณหภูมิร่างกายและไม่ควรนำไปใช้แทนเทอร์โมมิเตอร์
   */
  float readTemperature(uint32_t timeoutMs = 100);

  /*! @brief เหมือน readTemperature() แต่คืนค่าเป็นองศาฟาเรนไฮต์ */
  float readTemperatureF(uint32_t timeoutMs = 100);

  /*! @brief สั่งเริ่มวัดอุณหภูมิแบบไม่บล็อก */
  bool startTemperatureConversion();
  /*! @brief ถามว่าการวัดอุณหภูมิเสร็จหรือยัง */
  bool isTemperatureReady();
  /*! @brief อ่านผลอุณหภูมิที่วัดเสร็จแล้ว ใช้คู่กับสองฟังก์ชันข้างบน */
  float getTemperatureResult();

  /* ---------------------------------------------------------------------
     รหัสประจำตัวชิปและการตรวจของแท้
     --------------------------------------------------------------------- */

  uint8_t readPartID();     /*!< ค่าจากรีจิสเตอร์ 0xFF ควรได้ 0x15 */
  uint8_t readRevisionID(); /*!< ค่าจากรีจิสเตอร์ 0xFE */

  /*! @brief รุ่นที่ไลบรารีใช้งานอยู่ (ที่เดาได้ หรือที่ผู้ใช้ระบุไว้) */
  massmore_max3010x_variant_t getVariant() const { return _variant; }

  /*! @brief ระบุรุ่นชิปเอง ใช้เมื่อไม่อยากให้ไลบรารีเดา */
  void setVariant(massmore_max3010x_variant_t variant);

  /*! @brief ชื่อรุ่นเป็นข้อความ เช่น "MAX30102" */
  const char *getVariantName() const;

  /*! @brief ชิปที่ต่ออยู่มี LED สีเขียวหรือไม่ */
  bool hasGreenLed() const;
  /*! @brief ชิปที่ต่ออยู่มีฟังก์ชัน proximity หรือไม่ */
  bool hasProximity() const;

  /*!
   * @brief ตรวจว่าเป็นชิป MAX3010x ของแท้จากโรงงานหรือไม่
   *
   * ตรวจ 11 ข้อ ครอบคลุมทั้งรหัสรุ่น ค่าเริ่มต้นหลังรีเซ็ต พฤติกรรมของ
   * รีจิสเตอร์อ่านอย่างเดียว บิตสงวน การวนของตัวชี้ FIFO เซ็นเซอร์อุณหภูมิ
   * ในตัว และการตอบสนองของ ADC เมื่อเปิด LED  ของเลียนแบบมักตกข้อใดข้อหนึ่ง
   *
   * @warning ฟังก์ชันนี้จะรีเซ็ตชิปและเขียนทับค่าที่ตั้งไว้ ต้องตั้งค่าใหม่หลังเรียก
   * @return ผลสรุปเป็น enum ดูรายละเอียดรายข้อได้จาก getVerifyMask()
   */
  massmore_max3010x_genuine_t verifyChip();

  /*! @brief บิตแมสก์ผลการตรวจรายข้อจาก verifyChip() */
  uint16_t getVerifyMask() const { return _verifyMask; }
  /*! @brief จำนวนข้อที่ผ่านจากทั้งหมด 11 ข้อ */
  uint8_t getVerifyPassCount() const;
  /*! @brief ชื่อข้อตรวจลำดับที่ index (0-10) เป็นข้อความ */
  static const char *getVerifyCheckName(uint8_t index);

  /* ---------------------------------------------------------------------
     อ่านค่าที่ตั้งไว้กลับมาจากชิป
     --------------------------------------------------------------------- */

  /*! @brief อ่านการตั้งค่าทั้งหมดกลับมาจากชิปจริง ไม่ใช่ค่าที่จำไว้ในตัวแปร */
  bool readConfiguration(massmore_max3010x_config_t &out);

  /*! @brief จำนวนช่องข้อมูลต่อ 1 ตัวอย่างของโหมดปัจจุบัน (1-4) */
  uint8_t getActiveChannels() const { return _activeChannels; }

  /*! @brief อัตราข้อมูลจริงที่ออกจาก FIFO หน่วย Hz (คิดการเฉลี่ยแล้ว) */
  float getEffectiveSampleRate() const;

  /*! @brief แปลงค่ารีจิสเตอร์กระแส LED เป็นมิลลิแอมป์โดยประมาณ */
  static float ledCodeToMilliAmp(uint8_t code) {
    return (float)code * MASSMORE_MAX3010X_LED_STEP_MA;
  }
  /*! @brief แปลงมิลลิแอมป์เป็นค่ารีจิสเตอร์ที่ใกล้ที่สุด */
  static uint8_t milliAmpToLedCode(float milliAmp);

  /* ---------------------------------------------------------------------
     เข้าถึงรีจิสเตอร์ตรง ๆ สำหรับงานขั้นสูง
     --------------------------------------------------------------------- */

  bool readRegister8(uint8_t reg, uint8_t &value);
  uint8_t readRegister8(uint8_t reg); /*!< รูปแบบย่อ คืน 0 เมื่ออ่านไม่สำเร็จ */
  bool writeRegister8(uint8_t reg, uint8_t value);
  /*! @brief แก้เฉพาะบางบิต โดยคง and รักษาบิตอื่นไว้ตาม mask */
  bool maskRegister8(uint8_t reg, uint8_t mask, uint8_t value);
  /*! @brief อ่านหลายไบต์ติดกันจากรีจิสเตอร์เดียว (ใช้กับ FIFO_DATA) */
  bool readRegisterBurst(uint8_t reg, uint8_t *buffer, uint8_t length);

  /* ---------------------------------------------------------------------
     สถานะและข้อผิดพลาด
     --------------------------------------------------------------------- */

  massmore_max3010x_error_t lastError() const { return _lastError; }
  /*! @brief คำอธิบายข้อผิดพลาดล่าสุดเป็นภาษาไทย */
  const char *lastErrorString() const;
  static const char *errorToString(massmore_max3010x_error_t error);

  /*! @brief ที่อยู่ I2C ที่ใช้อยู่ */
  uint8_t getAddress() const { return _address; }
  /*! @brief begin() สำเร็จแล้วหรือยัง */
  bool isBegun() const { return _begun; }
  /*! @brief ตั้งเวลารอสูงสุดตอนคุยกับชิป */
  void setTimeout(uint32_t ms) { _timeoutMs = ms; }

 private:
  TwoWire *_wire;
  uint8_t _address;
  bool _begun;
  uint32_t _timeoutMs;
  massmore_max3010x_error_t _lastError;
  massmore_max3010x_variant_t _variant;
  bool _variantForced;
  uint16_t _verifyMask;

  /* สำเนาค่าที่ตั้งไว้ เก็บไว้เพื่อคำนวณโดยไม่ต้องอ่านชิปซ้ำ */
  massmore_max3010x_mode_t _mode;
  massmore_max3010x_smp_ave_t _sampleAverage;
  massmore_max3010x_rate_t _sampleRate;
  uint8_t _activeChannels;

  /* บัฟเฟอร์วงแหวนเก็บตัวอย่างที่ดึงมาจาก FIFO */
  uint32_t _bufRed[MASSMORE_MAX3010X_BUFFER_SIZE];
  uint32_t _bufIr[MASSMORE_MAX3010X_BUFFER_SIZE];
  uint32_t _bufGreen[MASSMORE_MAX3010X_BUFFER_SIZE];
  uint32_t _bufMs[MASSMORE_MAX3010X_BUFFER_SIZE]; /*!< เวลาที่ดึงตัวอย่างนั้นออกมา */
  uint8_t _head; /*!< ตำแหน่งที่จะเขียนตัวถัดไป */
  uint8_t _tail; /*!< ตำแหน่งที่ผู้ใช้กำลังอ่าน */
  uint8_t _count;

  uint32_t _lastSampleMs;

  void pushSample(uint32_t red, uint32_t ir, uint32_t green);
  void recomputeChannels();
  bool detectVariant();
  bool waitForBit(uint8_t reg, uint8_t bitMask, bool wantSet, uint32_t timeoutMs);
};

#endif /* MASSMORE_MAX3010X_H */
