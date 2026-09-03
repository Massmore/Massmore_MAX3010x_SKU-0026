/*!
 * @file massmore_max3010x_host_shim.h
 * @brief ตัวจำลอง Arduino และชิป MAX30102 สำหรับคอมไพล์และรันชุดทดสอบ
 *        บนเครื่อง PC โดยไม่ต้องมีบอร์ดจริง
 *
 * ไฟล์นี้ไม่ได้ถูกคอมไพล์ตอน build ลงบอร์ดจริง ไลบรารีจะ include ไฟล์นี้
 * เฉพาะตอนที่ไม่ได้นิยามมาโคร ARDUINO เท่านั้น
 *
 * ตัวจำลองชิปทำตามพฤติกรรมที่ระบุไว้ใน datasheet ให้ครบพอที่จะทดสอบตรรกะ
 * ของไลบรารีได้จริง ได้แก่
 *   - ตารางรีจิสเตอร์ทั้งหมด พร้อมค่าเริ่มต้นหลัง power-on-reset
 *   - PART_ID เป็นรีจิสเตอร์อ่านอย่างเดียว
 *   - บิตสงวนใน MODE_CONFIG อ่านกลับมาเป็นศูนย์เสมอ
 *   - ตัวชี้ FIFO เป็นฟิลด์ 5 บิต
 *   - บิต RESET เคลียร์ตัวเองเมื่ออ่านครั้งถัดไป
 *   - FIFO ผลิตข้อมูลตามอัตราที่ตั้งไว้ พร้อมตัวนับ overflow
 *   - เซ็นเซอร์อุณหภูมิในตัว
 *
 * @copyright Copyright (c) 2026 Massmore Biz Co., Ltd.
 * @license MIT
 */

#ifndef MASSMORE_MAX3010X_HOST_SHIM_H
#define MASSMORE_MAX3010X_HOST_SHIM_H

#include <stdint.h>
#include <stddef.h>
#include <string.h>

/* ---------- ส่วนที่จำลอง Arduino ---------- */

uint32_t millis();
uint32_t micros();
void delay(uint32_t ms);
/*! เดินเวลาจำลองไปข้างหน้า ใช้ในชุดทดสอบเพื่อไม่ต้องรอจริง */
void hostAdvanceTime(uint32_t ms);
void hostResetTime();

/* ---------- ตัวจำลองชิป MAX30102 ---------- */

class MockMax3010x {
 public:
  MockMax3010x();

  void reset();          /*!< คืนค่าทุกอย่างเป็นค่าโรงงาน */
  void tick(uint32_t ms); /*!< เดินเวลาไปข้างหน้า ผลิตข้อมูลลง FIFO ตามอัตราที่ตั้งไว้ */

  uint8_t readRegister(uint8_t reg);
  void writeRegister(uint8_t reg, uint8_t value);

  /* ปุ่มปรับพฤติกรรมสำหรับชุดทดสอบ */
  bool present;          /*!< ชิปตอบ ACK หรือไม่ */
  uint8_t partId;        /*!< เปลี่ยนได้เพื่อจำลองชิปผิดรุ่น */
  uint8_t revisionId;
  bool partIdWritable;   /*!< true = จำลองของเลียนแบบที่เขียนทับ PART_ID ได้ */
  bool reservedBitsStick;/*!< true = จำลองของเลียนแบบที่เก็บบิตสงวนไว้ */
  bool pointerFullByte;  /*!< true = จำลองตัวชี้ FIFO ที่ไม่ใช่ 5 บิต */
  bool hasLed3;          /*!< true = จำลอง MAX30101/30105 */
  bool hasLed4;          /*!< true = จำลอง MAX30101 ซึ่งมีไดรเวอร์ LED สีเขียวสองดวง */
  bool hasProximity;     /*!< true = จำลอง MAX30105 */
  float dieTemperature;  /*!< อุณหภูมิที่จะให้ชิปรายงาน */

  uint32_t writeCount;   /*!< นับจำนวนครั้งที่มีการเขียนรีจิสเตอร์ */
  uint32_t readCount;    /*!< นับจำนวนครั้งที่มีการอ่านรีจิสเตอร์ */

  /* สถานะ FIFO */
  uint8_t writePointer;
  uint8_t readPointer;
  uint8_t overflowCounter;
  /*!< จำนวนตัวอย่างที่ค้างอยู่จริง เก็บแยกจากตัวชี้ เพราะตัวชี้สองตัวที่ชี้ที่เดียวกัน
       หมายถึงได้ทั้ง "ว่างสนิท" และ "เต็มพอดี 32 ตัว" เหมือนชิปจริงทุกประการ */
  uint8_t fifoCount;
  uint32_t fifoRed[32];
  uint32_t fifoIr[32];
  uint32_t fifoGreen[32];

  uint8_t activeChannels() const;
  float effectiveRate() const;

 private:
  uint8_t _regs[0x40];
  uint8_t _prox;
  uint32_t _sampleCounter;
  float _sampleAccumulator;
  uint8_t _fifoByteIndex; /*!< ตำแหน่งไบต์ที่กำลังอ่านอยู่ในตัวอย่างปัจจุบัน */
  bool _tempPending;

  void pushSample();
};

extern MockMax3010x g_mockChip;

/* ---------- ตัวจำลองบัส I2C ---------- */

class TwoWire {
 public:
  TwoWire();

  void begin();
  void begin(int sda, int scl, uint32_t frequency);
  void setClock(uint32_t frequency);

  void beginTransmission(uint8_t address);
  size_t write(uint8_t value);
  uint8_t endTransmission();
  uint8_t endTransmission(bool stop);
  uint8_t requestFrom(uint8_t address, uint8_t quantity);
  int available();
  int read();

  uint32_t clockHz;

 private:
  uint8_t _address;
  uint8_t _outBuffer[8];
  uint8_t _outLength;
  uint8_t _inBuffer[64];
  uint8_t _inLength;
  uint8_t _inIndex;
  uint8_t _pendingRegister;
};

extern TwoWire Wire;

#endif /* MASSMORE_MAX3010X_HOST_SHIM_H */
