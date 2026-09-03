/*!
 * @file Massmore_MAX3010x.cpp
 * @brief การทำงานภายในของไลบรารี Massmore_MAX3010x
 *
 * @copyright Copyright (c) 2026 Massmore Biz Co., Ltd.
 * @license MIT
 */

#include "Massmore_MAX3010x.h"

#include <math.h>

/* จำนวนไบต์สูงสุดที่ยอมอ่านต่อการทำธุรกรรม I2C หนึ่งครั้ง
   ตั้งไว้ 30 เพราะบัฟเฟอร์ Wire ของบอร์ดเก่าบางรุ่นมีแค่ 32 ไบต์
   และ 30 หารด้วย 3 และ 6 ลงตัว ทำให้ไม่ตัดกลางตัวอย่าง */
#define MAX3010X_I2C_CHUNK 30

/* =========================================================================
   ตัวสร้าง
   ========================================================================= */

MassmoreMAX3010x::MassmoreMAX3010x()
    : _wire(NULL),
      _address(MASSMORE_MAX3010X_I2C_ADDRESS),
      _begun(false),
      _timeoutMs(MASSMORE_MAX3010X_TIMEOUT_DEFAULT_MS),
      _lastError(MASSMORE_MAX3010X_OK),
      _variant(MASSMORE_MAX3010X_VARIANT_AUTO),
      _variantForced(false),
      _verifyMask(0),
      _mode(MASSMORE_MAX3010X_MODE_SPO2),
      _sampleAverage(MASSMORE_MAX3010X_SMP_AVE_1),
      _sampleRate(MASSMORE_MAX3010X_RATE_50),
      _activeChannels(2),
      _head(0),
      _tail(0),
      _count(0),
      _lastSampleMs(0) {
  for (uint16_t i = 0; i < MASSMORE_MAX3010X_BUFFER_SIZE; i++) {
    _bufRed[i] = 0;
    _bufIr[i] = 0;
    _bufGreen[i] = 0;
    _bufMs[i] = 0;
  }
}

/* =========================================================================
   ชั้นล่างสุด คุยกับบัส I2C
   ========================================================================= */

bool MassmoreMAX3010x::readRegister8(uint8_t reg, uint8_t &value) {
  if (_wire == NULL) {
    _lastError = MASSMORE_MAX3010X_ERR_NOT_BEGUN;
    return false;
  }

  _wire->beginTransmission(_address);
  _wire->write(reg);
  /* ใช้ endTransmission(false) ไม่ได้กับชิปตัวนี้ทุกบอร์ด จึงปิดธุรกรรมให้จบ
     แล้วค่อยเปิดใหม่ตอนอ่าน ซึ่ง datasheet รองรับทั้งสองแบบ */
  if (_wire->endTransmission() != 0) {
    _lastError = MASSMORE_MAX3010X_ERR_I2C_WRITE;
    return false;
  }

  if (_wire->requestFrom((uint8_t)_address, (uint8_t)1) != 1) {
    _lastError = MASSMORE_MAX3010X_ERR_I2C_READ;
    return false;
  }

  value = (uint8_t)_wire->read();
  _lastError = MASSMORE_MAX3010X_OK;
  return true;
}

uint8_t MassmoreMAX3010x::readRegister8(uint8_t reg) {
  uint8_t value = 0;
  if (!readRegister8(reg, value)) {
    return 0;
  }
  return value;
}

bool MassmoreMAX3010x::writeRegister8(uint8_t reg, uint8_t value) {
  if (_wire == NULL) {
    _lastError = MASSMORE_MAX3010X_ERR_NOT_BEGUN;
    return false;
  }

  _wire->beginTransmission(_address);
  _wire->write(reg);
  _wire->write(value);
  if (_wire->endTransmission() != 0) {
    _lastError = MASSMORE_MAX3010X_ERR_I2C_WRITE;
    return false;
  }

  _lastError = MASSMORE_MAX3010X_OK;
  return true;
}

bool MassmoreMAX3010x::maskRegister8(uint8_t reg, uint8_t mask, uint8_t value) {
  uint8_t current = 0;
  if (!readRegister8(reg, current)) {
    return false;
  }
  /* mask คือบิตที่ต้องการ "เก็บไว้" ตามแบบที่ datasheet เขียนไว้ในตาราง */
  current = (uint8_t)((current & mask) | value);
  return writeRegister8(reg, current);
}

bool MassmoreMAX3010x::readRegisterBurst(uint8_t reg, uint8_t *buffer,
                                         uint8_t length) {
  if (_wire == NULL) {
    _lastError = MASSMORE_MAX3010X_ERR_NOT_BEGUN;
    return false;
  }
  if (buffer == NULL || length == 0) {
    _lastError = MASSMORE_MAX3010X_ERR_BAD_ARG;
    return false;
  }

  _wire->beginTransmission(_address);
  _wire->write(reg);
  if (_wire->endTransmission() != 0) {
    _lastError = MASSMORE_MAX3010X_ERR_I2C_WRITE;
    return false;
  }

  uint8_t received = 0;
  while (received < length) {
    uint8_t want = (uint8_t)(length - received);
    if (want > MAX3010X_I2C_CHUNK) {
      want = MAX3010X_I2C_CHUNK;
    }
    uint8_t got = (uint8_t)_wire->requestFrom((uint8_t)_address, want);
    if (got != want) {
      _lastError = MASSMORE_MAX3010X_ERR_I2C_READ;
      return false;
    }
    for (uint8_t i = 0; i < got; i++) {
      buffer[received + i] = (uint8_t)_wire->read();
    }
    received = (uint8_t)(received + got);
  }

  _lastError = MASSMORE_MAX3010X_OK;
  return true;
}

bool MassmoreMAX3010x::waitForBit(uint8_t reg, uint8_t bitMask, bool wantSet,
                                  uint32_t timeoutMs) {
  uint32_t start = millis();
  while ((uint32_t)(millis() - start) < timeoutMs) {
    uint8_t value = 0;
    if (!readRegister8(reg, value)) {
      return false;
    }
    bool isSet = (value & bitMask) != 0;
    if (isSet == wantSet) {
      return true;
    }
    delay(1);
  }
  _lastError = MASSMORE_MAX3010X_ERR_TIMEOUT;
  return false;
}

/* =========================================================================
   เริ่มต้นใช้งาน
   ========================================================================= */

bool MassmoreMAX3010x::begin(TwoWire &wire, uint8_t address, int8_t sdaPin,
                             int8_t sclPin, uint32_t frequency) {
  _wire = &wire;
  _address = address;
  _begun = false;
  _verifyMask = 0;

#ifdef ARDUINO
  if (sdaPin >= 0 && sclPin >= 0) {
    /* ESP32 Arduino core 3.x รับพารามิเตอร์ครบสามตัวได้ในคำสั่งเดียว */
#if defined(ESP32) || defined(ESP8266)
    _wire->begin((int)sdaPin, (int)sclPin, frequency);
#else
    (void)sdaPin;
    (void)sclPin;
    _wire->begin();
    _wire->setClock(frequency);
#endif
  } else {
    /* ผู้ใช้เปิดบัสเองมาแล้ว เราแค่ปรับความถี่ให้ */
    _wire->setClock(frequency);
  }
#else
  (void)sdaPin;
  (void)sclPin;
  (void)frequency;
#endif

  if (!isConnected()) {
    _lastError = MASSMORE_MAX3010X_ERR_NO_DEVICE;
    return false;
  }

  uint8_t partId = 0;
  if (!readRegister8(MASSMORE_MAX3010X_REG_PART_ID, partId)) {
    return false;
  }
  if (partId != MASSMORE_MAX3010X_PART_ID_EXPECTED) {
    /* PART_ID 0x11 คือ MAX30100 ซึ่งใช้ตารางรีจิสเตอร์คนละชุด ไลบรารีนี้ไม่รองรับ */
    _lastError = MASSMORE_MAX3010X_ERR_WRONG_CHIP;
    return false;
  }

  _begun = true;

  if (!softReset()) {
    _begun = false;
    return false;
  }

  if (!_variantForced) {
    detectVariant();
  }

  flush();
  _lastError = MASSMORE_MAX3010X_OK;
  return true;
}

bool MassmoreMAX3010x::isConnected() {
  if (_wire == NULL) {
    _lastError = MASSMORE_MAX3010X_ERR_NOT_BEGUN;
    return false;
  }
  _wire->beginTransmission(_address);
  if (_wire->endTransmission() != 0) {
    _lastError = MASSMORE_MAX3010X_ERR_NO_DEVICE;
    return false;
  }
  _lastError = MASSMORE_MAX3010X_OK;
  return true;
}

bool MassmoreMAX3010x::softReset() {
  if (!maskRegister8(MASSMORE_MAX3010X_REG_MODE_CONFIG,
                     MASSMORE_MAX3010X_MASK_RESET,
                     MASSMORE_MAX3010X_BIT_RESET)) {
    return false;
  }
  /* บิต RESET เคลียร์ตัวเองเมื่อชิปรีเซ็ตเสร็จ datasheet บอกว่าใช้เวลาไม่นาน
     ใช้ค่าเวลารอที่ผู้ใช้ตั้งไว้ด้วย setTimeout() เผื่อบัสช้าหรือสายยาว */
  if (!waitForBit(MASSMORE_MAX3010X_REG_MODE_CONFIG,
                  MASSMORE_MAX3010X_BIT_RESET, false, _timeoutMs)) {
    return false;
  }

  /* ค่าภายในกลับไปเป็นค่าโรงงาน จึงต้องซิงก์สำเนาในหน่วยความจำตาม */
  _mode = MASSMORE_MAX3010X_MODE_HR;
  _sampleAverage = MASSMORE_MAX3010X_SMP_AVE_1;
  _sampleRate = MASSMORE_MAX3010X_RATE_50;
  recomputeChannels();
  flush();
  return true;
}

bool MassmoreMAX3010x::shutdown() {
  return maskRegister8(MASSMORE_MAX3010X_REG_MODE_CONFIG,
                       MASSMORE_MAX3010X_MASK_SHUTDOWN,
                       MASSMORE_MAX3010X_BIT_SHUTDOWN);
}

bool MassmoreMAX3010x::wakeUp() {
  return maskRegister8(MASSMORE_MAX3010X_REG_MODE_CONFIG,
                       MASSMORE_MAX3010X_MASK_SHUTDOWN, 0x00);
}

bool MassmoreMAX3010x::setupDefault(uint8_t ledPowerLevel) {
  /* ชุดค่าที่เหมาะกับการวางนิ้วบนเซ็นเซอร์ ผ่านการลองจริงกับบอร์ด Massmore
     อัตราสุ่ม 400 Hz เฉลี่ย 8 ตัว จะได้ข้อมูลออกมา 50 ชุดต่อวินาที
     ซึ่งเป็นค่าที่อัลกอริทึม SpO2 ในไลบรารีนี้ตั้งเป็นค่าปริยาย */
  return setup(ledPowerLevel, MASSMORE_MAX3010X_SMP_AVE_8,
               MASSMORE_MAX3010X_MODE_SPO2, MASSMORE_MAX3010X_RATE_400,
               MASSMORE_MAX3010X_PULSE_411US,
               MASSMORE_MAX3010X_ADC_RANGE_4096);
}

bool MassmoreMAX3010x::setup(uint8_t ledPowerLevel,
                             massmore_max3010x_smp_ave_t sampleAverage,
                             massmore_max3010x_mode_t mode,
                             massmore_max3010x_rate_t sampleRate,
                             massmore_max3010x_pulse_width_t pulseWidth,
                             massmore_max3010x_adc_range_t adcRange) {
  if (!_begun) {
    _lastError = MASSMORE_MAX3010X_ERR_NOT_BEGUN;
    return false;
  }

  bool ok = true;
  ok = ok && softReset();
  ok = ok && setFifoAlmostFull(15);   /* แจ้งเตือนตั้งแต่ FIFO มี 17 ตัวอย่าง */
  ok = ok && setFifoRollover(true);   /* ยอมให้ข้อมูลเก่าถูกทับ ดีกว่าค้าง */
  ok = ok && setSampleAverage(sampleAverage);
  ok = ok && setAdcRange(adcRange);
  ok = ok && setSampleRate(sampleRate);
  ok = ok && setPulseWidth(pulseWidth);

  ok = ok && setPulseAmplitudeRed(ledPowerLevel);
  ok = ok && setPulseAmplitudeIR(ledPowerLevel);
  if (hasGreenLed()) {
    ok = ok && setPulseAmplitudeGreen(
                   mode == MASSMORE_MAX3010X_MODE_MULTI_LED ? ledPowerLevel : 0);
  }
  if (hasProximity()) {
    ok = ok && setPulseAmplitudeProximity(ledPowerLevel);
  }

  /* ตั้งช่องเวลาให้สอดคล้องกับโหมด เผื่อผู้ใช้เลือก multi-LED */
  if (mode == MASSMORE_MAX3010X_MODE_MULTI_LED) {
    ok = ok && setMultiLedSlot(1, MASSMORE_MAX3010X_SLOT_RED);
    ok = ok && setMultiLedSlot(2, MASSMORE_MAX3010X_SLOT_IR);
    ok = ok && setMultiLedSlot(3, hasGreenLed() ? MASSMORE_MAX3010X_SLOT_GREEN
                                                : MASSMORE_MAX3010X_SLOT_NONE);
    ok = ok && setMultiLedSlot(4, MASSMORE_MAX3010X_SLOT_NONE);
  }

  /* ตั้งโหมดเป็นขั้นสุดท้าย เพราะชิปเริ่มยิง LED ทันทีที่ตั้งโหมด */
  ok = ok && setMode(mode);
  ok = ok && clearFifo();

  flush();
  return ok;
}

/* =========================================================================
   การตั้งค่าแต่ละหัวข้อ
   ========================================================================= */

void MassmoreMAX3010x::recomputeChannels() {
  switch (_mode) {
    case MASSMORE_MAX3010X_MODE_HR:
      _activeChannels = 1;
      break;
    case MASSMORE_MAX3010X_MODE_SPO2:
      _activeChannels = 2;
      break;
    case MASSMORE_MAX3010X_MODE_MULTI_LED:
    default: {
      /* ในโหมด multi-LED จำนวนช่องข้อมูลเท่ากับจำนวนช่องเวลาที่ไม่ได้ปิดไว้
         ต้องอ่านกลับจากชิปเพราะผู้ใช้ตั้งเองได้อิสระ */
      uint8_t slots = 0;
      uint8_t reg1 = 0;
      uint8_t reg2 = 0;
      if (readRegister8(MASSMORE_MAX3010X_REG_MULTI_LED_1, reg1) &&
          readRegister8(MASSMORE_MAX3010X_REG_MULTI_LED_2, reg2)) {
        if ((reg1 & 0x07) != 0) slots++;
        if (((reg1 >> 4) & 0x07) != 0) slots++;
        if ((reg2 & 0x07) != 0) slots++;
        if (((reg2 >> 4) & 0x07) != 0) slots++;
      }
      _activeChannels = (slots == 0) ? 1 : slots;
      break;
    }
  }
}

bool MassmoreMAX3010x::setMode(massmore_max3010x_mode_t mode) {
  uint8_t value;
  switch (mode) {
    case MASSMORE_MAX3010X_MODE_HR:
      value = MASSMORE_MAX3010X_MODE_VAL_HR;
      break;
    case MASSMORE_MAX3010X_MODE_SPO2:
      value = MASSMORE_MAX3010X_MODE_VAL_SPO2;
      break;
    case MASSMORE_MAX3010X_MODE_MULTI_LED:
      value = MASSMORE_MAX3010X_MODE_VAL_MULTI_LED;
      break;
    default:
      _lastError = MASSMORE_MAX3010X_ERR_BAD_ARG;
      return false;
  }
  if (!maskRegister8(MASSMORE_MAX3010X_REG_MODE_CONFIG,
                     MASSMORE_MAX3010X_MASK_MODE, value)) {
    return false;
  }
  _mode = mode;
  recomputeChannels();
  flush();
  return true;
}

bool MassmoreMAX3010x::setSampleAverage(massmore_max3010x_smp_ave_t average) {
  if ((uint8_t)average > (uint8_t)MASSMORE_MAX3010X_SMP_AVE_32) {
    _lastError = MASSMORE_MAX3010X_ERR_BAD_ARG;
    return false;
  }
  if (!maskRegister8(MASSMORE_MAX3010X_REG_FIFO_CONFIG,
                     MASSMORE_MAX3010X_MASK_SMP_AVE,
                     (uint8_t)((uint8_t)average << 5))) {
    return false;
  }
  _sampleAverage = average;
  return true;
}

bool MassmoreMAX3010x::setFifoRollover(bool enable) {
  return maskRegister8(MASSMORE_MAX3010X_REG_FIFO_CONFIG,
                       MASSMORE_MAX3010X_MASK_ROLLOVER,
                       enable ? MASSMORE_MAX3010X_BIT_ROLLOVER_EN : 0x00);
}

bool MassmoreMAX3010x::setFifoAlmostFull(uint8_t spacesLeft) {
  if (spacesLeft > 15) {
    _lastError = MASSMORE_MAX3010X_ERR_BAD_ARG;
    return false;
  }
  return maskRegister8(MASSMORE_MAX3010X_REG_FIFO_CONFIG,
                       MASSMORE_MAX3010X_MASK_A_FULL, spacesLeft);
}

bool MassmoreMAX3010x::setAdcRange(massmore_max3010x_adc_range_t range) {
  if ((uint8_t)range > (uint8_t)MASSMORE_MAX3010X_ADC_RANGE_16384) {
    _lastError = MASSMORE_MAX3010X_ERR_BAD_ARG;
    return false;
  }
  return maskRegister8(MASSMORE_MAX3010X_REG_SPO2_CONFIG,
                       MASSMORE_MAX3010X_MASK_ADC_RANGE,
                       (uint8_t)((uint8_t)range << 5));
}

bool MassmoreMAX3010x::setSampleRate(massmore_max3010x_rate_t rate) {
  if ((uint8_t)rate > (uint8_t)MASSMORE_MAX3010X_RATE_3200) {
    _lastError = MASSMORE_MAX3010X_ERR_BAD_ARG;
    return false;
  }
  if (!maskRegister8(MASSMORE_MAX3010X_REG_SPO2_CONFIG,
                     MASSMORE_MAX3010X_MASK_SAMPLE_RATE,
                     (uint8_t)((uint8_t)rate << 2))) {
    return false;
  }
  _sampleRate = rate;
  return true;
}

bool MassmoreMAX3010x::setPulseWidth(massmore_max3010x_pulse_width_t width) {
  if ((uint8_t)width > (uint8_t)MASSMORE_MAX3010X_PULSE_411US) {
    _lastError = MASSMORE_MAX3010X_ERR_BAD_ARG;
    return false;
  }
  return maskRegister8(MASSMORE_MAX3010X_REG_SPO2_CONFIG,
                       MASSMORE_MAX3010X_MASK_PULSE_WIDTH, (uint8_t)width);
}

bool MassmoreMAX3010x::setPulseAmplitudeRed(uint8_t value) {
  return writeRegister8(MASSMORE_MAX3010X_REG_LED1_PA, value);
}

bool MassmoreMAX3010x::setPulseAmplitudeIR(uint8_t value) {
  return writeRegister8(MASSMORE_MAX3010X_REG_LED2_PA, value);
}

bool MassmoreMAX3010x::setPulseAmplitudeGreen(uint8_t value) {
  if (!hasGreenLed()) {
    _lastError = MASSMORE_MAX3010X_ERR_UNSUPPORTED;
    return false;
  }
  return writeRegister8(MASSMORE_MAX3010X_REG_LED3_PA, value);
}

bool MassmoreMAX3010x::setPulseAmplitudeProximity(uint8_t value) {
  /* MAX30102 และ MAX30101 ไม่มีรีจิสเตอร์ PILOT_PA การเขียนลงไปจึงไม่มีผล
     แจ้งกลับเป็นข้อผิดพลาดดีกว่าปล่อยให้ผู้ใช้เข้าใจผิดว่าตั้งค่าสำเร็จ */
  if (!hasProximity()) {
    _lastError = MASSMORE_MAX3010X_ERR_UNSUPPORTED;
    return false;
  }
  return writeRegister8(MASSMORE_MAX3010X_REG_PILOT_PA, value);
}

bool MassmoreMAX3010x::setAllLedsOff() {
  bool ok = true;
  ok = ok && setPulseAmplitudeRed(0);
  ok = ok && setPulseAmplitudeIR(0);
  if (hasGreenLed()) {
    ok = ok && setPulseAmplitudeGreen(0);
  }
  if (hasProximity()) {
    ok = ok && setPulseAmplitudeProximity(0);
  }
  return ok;
}

bool MassmoreMAX3010x::setMultiLedSlot(uint8_t slotNumber,
                                       massmore_max3010x_slot_t device) {
  if (slotNumber < 1 || slotNumber > 4) {
    _lastError = MASSMORE_MAX3010X_ERR_BAD_ARG;
    return false;
  }
  if ((uint8_t)device > 7) {
    _lastError = MASSMORE_MAX3010X_ERR_BAD_ARG;
    return false;
  }
  if ((device == MASSMORE_MAX3010X_SLOT_GREEN ||
       device == MASSMORE_MAX3010X_SLOT_GREEN_PILOT) &&
      !hasGreenLed()) {
    _lastError = MASSMORE_MAX3010X_ERR_UNSUPPORTED;
    return false;
  }

  uint8_t reg = (slotNumber <= 2) ? MASSMORE_MAX3010X_REG_MULTI_LED_1
                                  : MASSMORE_MAX3010X_REG_MULTI_LED_2;
  bool isOddSlot = (slotNumber == 1 || slotNumber == 3);

  bool ok;
  if (isOddSlot) {
    ok = maskRegister8(reg, MASSMORE_MAX3010X_MASK_SLOT_ODD, (uint8_t)device);
  } else {
    ok = maskRegister8(reg, MASSMORE_MAX3010X_MASK_SLOT_EVEN,
                       (uint8_t)((uint8_t)device << 4));
  }
  if (!ok) {
    return false;
  }
  if (_mode == MASSMORE_MAX3010X_MODE_MULTI_LED) {
    recomputeChannels();
    flush();
  }
  return true;
}

bool MassmoreMAX3010x::setProximityThreshold(uint8_t threshold) {
  if (!hasProximity()) {
    _lastError = MASSMORE_MAX3010X_ERR_UNSUPPORTED;
    return false;
  }
  return writeRegister8(MASSMORE_MAX3010X_REG_PROX_INT_THRESH, threshold);
}

/* =========================================================================
   อินเทอร์รัปต์
   ========================================================================= */

uint8_t MassmoreMAX3010x::getInterruptStatus1() {
  return readRegister8(MASSMORE_MAX3010X_REG_INT_STATUS_1);
}

uint8_t MassmoreMAX3010x::getInterruptStatus2() {
  return readRegister8(MASSMORE_MAX3010X_REG_INT_STATUS_2);
}

bool MassmoreMAX3010x::enableInterruptAlmostFull(bool enable) {
  return maskRegister8(MASSMORE_MAX3010X_REG_INT_ENABLE_1,
                       (uint8_t)~MASSMORE_MAX3010X_INT_A_FULL,
                       enable ? MASSMORE_MAX3010X_INT_A_FULL : 0);
}

bool MassmoreMAX3010x::enableInterruptDataReady(bool enable) {
  return maskRegister8(MASSMORE_MAX3010X_REG_INT_ENABLE_1,
                       (uint8_t)~MASSMORE_MAX3010X_INT_PPG_RDY,
                       enable ? MASSMORE_MAX3010X_INT_PPG_RDY : 0);
}

bool MassmoreMAX3010x::enableInterruptAmbientLightOverflow(bool enable) {
  return maskRegister8(MASSMORE_MAX3010X_REG_INT_ENABLE_1,
                       (uint8_t)~MASSMORE_MAX3010X_INT_ALC_OVF,
                       enable ? MASSMORE_MAX3010X_INT_ALC_OVF : 0);
}

bool MassmoreMAX3010x::enableInterruptProximity(bool enable) {
  if (!hasProximity()) {
    _lastError = MASSMORE_MAX3010X_ERR_UNSUPPORTED;
    return false;
  }
  return maskRegister8(MASSMORE_MAX3010X_REG_INT_ENABLE_1,
                       (uint8_t)~MASSMORE_MAX3010X_INT_PROX,
                       enable ? MASSMORE_MAX3010X_INT_PROX : 0);
}

bool MassmoreMAX3010x::enableInterruptDieTemperature(bool enable) {
  return maskRegister8(MASSMORE_MAX3010X_REG_INT_ENABLE_2,
                       (uint8_t)~MASSMORE_MAX3010X_INT_DIE_TEMP_RDY,
                       enable ? MASSMORE_MAX3010X_INT_DIE_TEMP_RDY : 0);
}

bool MassmoreMAX3010x::disableAllInterrupts() {
  bool ok = writeRegister8(MASSMORE_MAX3010X_REG_INT_ENABLE_1, 0x00);
  ok = writeRegister8(MASSMORE_MAX3010X_REG_INT_ENABLE_2, 0x00) && ok;
  return ok;
}

/* =========================================================================
   FIFO
   ========================================================================= */

bool MassmoreMAX3010x::clearFifo() {
  bool ok = true;
  ok = writeRegister8(MASSMORE_MAX3010X_REG_FIFO_WR_PTR, 0x00) && ok;
  ok = writeRegister8(MASSMORE_MAX3010X_REG_OVF_COUNTER, 0x00) && ok;
  ok = writeRegister8(MASSMORE_MAX3010X_REG_FIFO_RD_PTR, 0x00) && ok;
  flush();
  return ok;
}

uint8_t MassmoreMAX3010x::getWritePointer() {
  return (uint8_t)(readRegister8(MASSMORE_MAX3010X_REG_FIFO_WR_PTR) & 0x1F);
}

uint8_t MassmoreMAX3010x::getReadPointer() {
  return (uint8_t)(readRegister8(MASSMORE_MAX3010X_REG_FIFO_RD_PTR) & 0x1F);
}

uint8_t MassmoreMAX3010x::getOverflowCounter() {
  return (uint8_t)(readRegister8(MASSMORE_MAX3010X_REG_OVF_COUNTER) & 0x1F);
}

uint8_t MassmoreMAX3010x::getSamplesInFifo() {
  uint8_t writePtr = 0;
  uint8_t readPtr = 0;
  if (!readRegister8(MASSMORE_MAX3010X_REG_FIFO_WR_PTR, writePtr)) {
    return 0;
  }
  if (!readRegister8(MASSMORE_MAX3010X_REG_FIFO_RD_PTR, readPtr)) {
    return 0;
  }
  writePtr &= 0x1F;
  readPtr &= 0x1F;

  int16_t pending = (int16_t)writePtr - (int16_t)readPtr;
  if (pending < 0) {
    /* ตัวชี้วนกลับไปต้นแถวแล้ว บวกความลึกของ FIFO คืนให้ */
    pending += MASSMORE_MAX3010X_FIFO_DEPTH;
  }

  if (pending == 0) {
    /* ตัวชี้ทั้งสองชี้ที่เดียวกันได้สองกรณี คือ "ว่างสนิท" กับ "เต็มพอดี 32 ตัว"
       แยกสองกรณีนี้ด้วยตัวนับ overflow ซึ่งจะเดินขึ้นเฉพาะตอนที่ FIFO เต็มแล้ว
       ถ้าไม่แยก ข้อมูลเต็ม FIFO จะถูกมองข้ามไปทั้งชุดเมื่อ loop() ช้ากว่าปกติ */
    uint8_t overflow = 0;
    if (readRegister8(MASSMORE_MAX3010X_REG_OVF_COUNTER, overflow) &&
        (overflow & 0x1F) != 0) {
      return MASSMORE_MAX3010X_FIFO_DEPTH;
    }
  }

  return (uint8_t)pending;
}

void MassmoreMAX3010x::pushSample(uint32_t red, uint32_t ir, uint32_t green) {
  const uint32_t now = millis();

  _bufRed[_head] = red;
  _bufIr[_head] = ir;
  _bufGreen[_head] = green;
  /* เก็บเวลาแยกรายตัวอย่าง ไม่ใช่เก็บตัวเดียวรวม เพราะผู้ใช้อาจทยอยอ่าน
     ข้ามหลายรอบของ loop() แล้วเวลาที่ติดมากับตัวอย่างเก่าจะเพี้ยน */
  _bufMs[_head] = now;

  _head = (uint8_t)((_head + 1) % MASSMORE_MAX3010X_BUFFER_SIZE);
  if (_count < MASSMORE_MAX3010X_BUFFER_SIZE) {
    _count++;
  } else {
    /* บัฟเฟอร์เต็ม ตัวเก่าสุดถูกทับ จึงต้องเลื่อนหางตาม */
    _tail = (uint8_t)((_tail + 1) % MASSMORE_MAX3010X_BUFFER_SIZE);
  }
  _lastSampleMs = now;
}

bool MassmoreMAX3010x::update() {
  if (!_begun) {
    _lastError = MASSMORE_MAX3010X_ERR_NOT_BEGUN;
    return false;
  }

  uint8_t pending = getSamplesInFifo();
  if (pending == 0) {
    _lastError = MASSMORE_MAX3010X_ERR_NO_DATA;
    return false;
  }

  const uint8_t bytesPerSample =
      (uint8_t)(_activeChannels * MASSMORE_MAX3010X_BYTES_PER_CHANNEL);

  /* อ่านทีละก้อน โดยให้ขนาดก้อนหารด้วยขนาดตัวอย่างลงตัวเสมอ
     จะได้ไม่มีตัวอย่างไหนถูกตัดครึ่งคาบัส */
  uint8_t samplesPerChunk = (uint8_t)(MAX3010X_I2C_CHUNK / bytesPerSample);
  if (samplesPerChunk == 0) {
    samplesPerChunk = 1;
  }

  uint8_t buffer[MAX3010X_I2C_CHUNK];
  uint8_t fetched = 0;

  /* ตั้งตัวชี้รีจิสเตอร์ไปที่ FIFO_DATA หนึ่งครั้ง จากนั้นชิปจะป้อนข้อมูล
     ต่อเนื่องเองโดยไม่ขยับตัวชี้รีจิสเตอร์ (ตามที่ datasheet ระบุไว้) */
  while (fetched < pending) {
    uint8_t want = (uint8_t)(pending - fetched);
    if (want > samplesPerChunk) {
      want = samplesPerChunk;
    }
    uint8_t bytes = (uint8_t)(want * bytesPerSample);

    if (!readRegisterBurst(MASSMORE_MAX3010X_REG_FIFO_DATA, buffer, bytes)) {
      return fetched > 0;
    }

    for (uint8_t s = 0; s < want; s++) {
      const uint8_t *p = &buffer[s * bytesPerSample];
      uint32_t channel[4] = {0, 0, 0, 0};

      for (uint8_t c = 0; c < _activeChannels && c < 4; c++) {
        uint32_t value = ((uint32_t)p[c * 3] << 16) |
                         ((uint32_t)p[c * 3 + 1] << 8) |
                         (uint32_t)p[c * 3 + 2];
        /* ข้อมูลเป็นเลข 18 บิตชิดซ้ายในกรอบ 24 บิต จึงต้องมาสก์ 6 บิตบนทิ้ง */
        channel[c] = value & MASSMORE_MAX3010X_DATA_MASK;
      }

      /* จัดช่องข้อมูลให้ตรงกับความหมายของแต่ละโหมด */
      switch (_mode) {
        case MASSMORE_MAX3010X_MODE_HR:
          /* โหมดนี้ชิปยิง LED1 ดวงเดียว ซึ่งบน MAX30102 คือสีแดง */
          pushSample(channel[0], 0, 0);
          break;
        case MASSMORE_MAX3010X_MODE_SPO2:
          pushSample(channel[0], channel[1], 0);
          break;
        case MASSMORE_MAX3010X_MODE_MULTI_LED:
        default:
          /* ค่าปริยายของ setup() คือ slot1=แดง slot2=IR slot3=เขียว */
          pushSample(channel[0], channel[1], channel[2]);
          break;
      }
    }
    fetched = (uint8_t)(fetched + want);
  }

  if (pending >= MASSMORE_MAX3010X_FIFO_DEPTH) {
    /* เพิ่งกวาดข้อมูลชุดที่ FIFO ล้นออกมาหมดแล้ว ต้องล้างตัวนับ overflow ด้วย
       ไม่งั้นรอบถัดไปที่ FIFO ว่างจริงจะถูกเข้าใจผิดว่าเต็มอีกครั้ง */
    writeRegister8(MASSMORE_MAX3010X_REG_OVF_COUNTER, 0x00);
  }

  _lastError = MASSMORE_MAX3010X_OK;
  return true;
}

uint8_t MassmoreMAX3010x::available() const { return _count; }

void MassmoreMAX3010x::nextSample() {
  if (_count == 0) {
    return;
  }
  _tail = (uint8_t)((_tail + 1) % MASSMORE_MAX3010X_BUFFER_SIZE);
  _count--;
}

uint32_t MassmoreMAX3010x::getRed() const {
  return (_count == 0) ? 0 : _bufRed[_tail];
}

uint32_t MassmoreMAX3010x::getIR() const {
  return (_count == 0) ? 0 : _bufIr[_tail];
}

uint32_t MassmoreMAX3010x::getGreen() const {
  return (_count == 0) ? 0 : _bufGreen[_tail];
}

bool MassmoreMAX3010x::peekSample(massmore_max3010x_sample_t &out) const {
  if (_count == 0) {
    return false;
  }
  out.red = _bufRed[_tail];
  out.ir = _bufIr[_tail];
  out.green = _bufGreen[_tail];
  out.timestampMs = _bufMs[_tail];
  return true;
}

bool MassmoreMAX3010x::readSample(massmore_max3010x_sample_t &out,
                                  uint32_t timeoutMs) {
  uint32_t start = millis();
  do {
    if (_count > 0 || update()) {
      if (peekSample(out)) {
        nextSample();
        return true;
      }
    }
    delay(1);
  } while ((uint32_t)(millis() - start) < timeoutMs);

  _lastError = MASSMORE_MAX3010X_ERR_TIMEOUT;
  return false;
}

void MassmoreMAX3010x::flush() {
  _head = 0;
  _tail = 0;
  _count = 0;
  _lastSampleMs = 0;
}

/* =========================================================================
   อุณหภูมิแกนชิป
   ========================================================================= */

bool MassmoreMAX3010x::startTemperatureConversion() {
  return writeRegister8(MASSMORE_MAX3010X_REG_DIE_TEMP_CONFIG,
                        MASSMORE_MAX3010X_BIT_TEMP_EN);
}

bool MassmoreMAX3010x::isTemperatureReady() {
  uint8_t config = 0;
  if (!readRegister8(MASSMORE_MAX3010X_REG_DIE_TEMP_CONFIG, config)) {
    return false;
  }
  /* บิต TEMP_EN เคลียร์ตัวเองเมื่อวัดเสร็จ */
  if ((config & MASSMORE_MAX3010X_BIT_TEMP_EN) == 0) {
    return true;
  }
  /* เผื่อไว้อีกทาง ดูแฟล็ก DIE_TEMP_RDY ในรีจิสเตอร์สถานะชุดที่ 2 */
  uint8_t status = 0;
  if (readRegister8(MASSMORE_MAX3010X_REG_INT_STATUS_2, status) &&
      (status & MASSMORE_MAX3010X_INT_DIE_TEMP_RDY)) {
    return true;
  }
  return false;
}

float MassmoreMAX3010x::getTemperatureResult() {
  uint8_t integerPart = 0;
  uint8_t fractionPart = 0;
  if (!readRegister8(MASSMORE_MAX3010X_REG_DIE_TEMP_INT, integerPart)) {
    return NAN;
  }
  if (!readRegister8(MASSMORE_MAX3010X_REG_DIE_TEMP_FRAC, fractionPart)) {
    return NAN;
  }
  /* ส่วนจำนวนเต็มเป็นเลขมีเครื่องหมายแบบ 2's complement
     ส่วนทศนิยมเป็นบวกเสมอ ละเอียดขั้นละ 0.0625 องศา */
  int8_t signedInteger = (int8_t)integerPart;
  return (float)signedInteger +
         (float)(fractionPart & 0x0F) * MASSMORE_MAX3010X_TEMP_FRAC_STEP;
}

float MassmoreMAX3010x::readTemperature(uint32_t timeoutMs) {
  if (!_begun) {
    _lastError = MASSMORE_MAX3010X_ERR_NOT_BEGUN;
    return NAN;
  }
  if (!startTemperatureConversion()) {
    return NAN;
  }

  uint32_t start = millis();
  while ((uint32_t)(millis() - start) < timeoutMs) {
    if (isTemperatureReady()) {
      return getTemperatureResult();
    }
    delay(1);
  }

  _lastError = MASSMORE_MAX3010X_ERR_TIMEOUT;
  return NAN;
}

float MassmoreMAX3010x::readTemperatureF(uint32_t timeoutMs) {
  float celsius = readTemperature(timeoutMs);
  if (isnan(celsius)) {
    return NAN;
  }
  return celsius * 1.8f + 32.0f;
}

/* =========================================================================
   รหัสประจำตัวชิปและการเดารุ่น
   ========================================================================= */

uint8_t MassmoreMAX3010x::readPartID() {
  return readRegister8(MASSMORE_MAX3010X_REG_PART_ID);
}

uint8_t MassmoreMAX3010x::readRevisionID() {
  return readRegister8(MASSMORE_MAX3010X_REG_REVISION_ID);
}

void MassmoreMAX3010x::setVariant(massmore_max3010x_variant_t variant) {
  _variant = variant;
  _variantForced = (variant != MASSMORE_MAX3010X_VARIANT_AUTO);
}

bool MassmoreMAX3010x::hasGreenLed() const {
  return _variant == MASSMORE_MAX3010X_VARIANT_MAX30101 ||
         _variant == MASSMORE_MAX3010X_VARIANT_MAX30105;
}

bool MassmoreMAX3010x::hasProximity() const {
  return _variant == MASSMORE_MAX3010X_VARIANT_MAX30105;
}

const char *MassmoreMAX3010x::getVariantName() const {
  switch (_variant) {
    case MASSMORE_MAX3010X_VARIANT_MAX30102:
      return "MAX30102";
    case MASSMORE_MAX3010X_VARIANT_MAX30101:
      return "MAX30101";
    case MASSMORE_MAX3010X_VARIANT_MAX30105:
      return "MAX30105";
    default:
      return "MAX3010x";
  }
}

bool MassmoreMAX3010x::detectVariant() {
  /* ทั้งสามรุ่นคืน PART_ID = 0x15 เท่ากันหมด จึงต้องเดาจากรีจิสเตอร์ที่
     "มีเฉพาะบางรุ่น" แทน  รีจิสเตอร์ที่ไม่มีอยู่จริงจะอ่านกลับมาเป็น 0 เสมอ
     แม้จะเขียนค่าลงไปแล้วก็ตาม

       LED3_PA (0x0E)          มีใน MAX30101 และ MAX30105 ไม่มีใน MAX30102
       LED4_PA (0x0F)          มีเฉพาะ MAX30101 ซึ่งมีไดรเวอร์ LED สีเขียวสองดวง
       PROX_INT_THRESH (0x30)  มีเฉพาะ MAX30105  ส่วน MAX30101 ตัดฟังก์ชัน
                               proximity ออกไปแล้วตั้งแต่ datasheet รีวิชัน 1 */
  _variant = MASSMORE_MAX3010X_VARIANT_MAX30102;

  const uint8_t probe = 0x2A; /* ค่าทดสอบ ไม่ใช่ 0x00 และไม่ใช่ 0xFF */
  uint8_t savedLed3 = 0;
  uint8_t readback = 0;

  readRegister8(MASSMORE_MAX3010X_REG_LED3_PA, savedLed3);
  if (!writeRegister8(MASSMORE_MAX3010X_REG_LED3_PA, probe)) {
    return false;
  }
  if (!readRegister8(MASSMORE_MAX3010X_REG_LED3_PA, readback)) {
    return false;
  }
  writeRegister8(MASSMORE_MAX3010X_REG_LED3_PA, savedLed3);

  if (readback != probe) {
    /* ไม่มีไดรเวอร์ LED ช่องที่ 3 แปลว่าเป็น MAX30102 */
    _lastError = MASSMORE_MAX3010X_OK;
    return true;
  }

  /* มี LED3 แล้ว ลองหาไดรเวอร์ช่องที่ 4 ซึ่งมีเฉพาะ MAX30101 */
  uint8_t savedLed4 = 0;
  readRegister8(MASSMORE_MAX3010X_REG_LED4_PA, savedLed4);
  if (writeRegister8(MASSMORE_MAX3010X_REG_LED4_PA, probe) &&
      readRegister8(MASSMORE_MAX3010X_REG_LED4_PA, readback)) {
    writeRegister8(MASSMORE_MAX3010X_REG_LED4_PA, savedLed4);
    if (readback == probe) {
      _variant = MASSMORE_MAX3010X_VARIANT_MAX30101;
      _lastError = MASSMORE_MAX3010X_OK;
      return true;
    }
  }

  /* ไม่มี LED4 เหลือยืนยันด้วยรีจิสเตอร์ proximity ว่าเป็น MAX30105 จริง */
  uint8_t savedProx = 0;
  readRegister8(MASSMORE_MAX3010X_REG_PROX_INT_THRESH, savedProx);
  if (!writeRegister8(MASSMORE_MAX3010X_REG_PROX_INT_THRESH, probe)) {
    return false;
  }
  if (!readRegister8(MASSMORE_MAX3010X_REG_PROX_INT_THRESH, readback)) {
    return false;
  }
  writeRegister8(MASSMORE_MAX3010X_REG_PROX_INT_THRESH, savedProx);

  /* ถ้าไม่พบทั้ง LED4 และ proximity ก็ยังถือว่าเป็น MAX30105 เพราะมี LED สีเขียว
     ครบตามคุณสมบัติหลักของรุ่นนั้น และเป็นรุ่นที่พบบ่อยกว่าในบอร์ดทั่วไป */
  _variant = MASSMORE_MAX3010X_VARIANT_MAX30105;
  (void)readback;
  _lastError = MASSMORE_MAX3010X_OK;
  return true;
}

uint8_t MassmoreMAX3010x::milliAmpToLedCode(float milliAmp) {
  /* เทียบกับตัวเองเพื่อจับ NaN ซึ่งไม่เท่ากับอะไรเลยแม้แต่ตัวมันเอง
     ถ้าไม่ดักไว้ การแปลง NaN เป็น uint8_t จะเป็นพฤติกรรมที่ไม่นิยาม */
  if (!(milliAmp == milliAmp) || milliAmp <= 0.0f) {
    return 0;
  }
  float code = milliAmp / MASSMORE_MAX3010X_LED_STEP_MA + 0.5f;
  if (code > 255.0f) {
    return 255;
  }
  return (uint8_t)code;
}

float MassmoreMAX3010x::getEffectiveSampleRate() const {
  static const uint16_t rateTable[8] = {50,  100,  200,  400,
                                        800, 1000, 1600, 3200};
  static const uint8_t averageTable[6] = {1, 2, 4, 8, 16, 32};

  uint8_t rateIndex = (uint8_t)_sampleRate;
  uint8_t aveIndex = (uint8_t)_sampleAverage;
  if (rateIndex > 7) rateIndex = 7;
  if (aveIndex > 5) aveIndex = 5;

  return (float)rateTable[rateIndex] / (float)averageTable[aveIndex];
}

/* =========================================================================
   อ่านการตั้งค่ากลับมาจากชิป
   ========================================================================= */

bool MassmoreMAX3010x::readConfiguration(massmore_max3010x_config_t &out) {
  if (!_begun) {
    _lastError = MASSMORE_MAX3010X_ERR_NOT_BEGUN;
    return false;
  }

  uint8_t modeConfig = 0;
  uint8_t spo2Config = 0;
  uint8_t fifoConfig = 0;
  bool ok = true;

  ok = readRegister8(MASSMORE_MAX3010X_REG_MODE_CONFIG, modeConfig) && ok;
  ok = readRegister8(MASSMORE_MAX3010X_REG_SPO2_CONFIG, spo2Config) && ok;
  ok = readRegister8(MASSMORE_MAX3010X_REG_FIFO_CONFIG, fifoConfig) && ok;
  ok = readRegister8(MASSMORE_MAX3010X_REG_PART_ID, out.partId) && ok;
  ok = readRegister8(MASSMORE_MAX3010X_REG_REVISION_ID, out.revisionId) && ok;
  ok = readRegister8(MASSMORE_MAX3010X_REG_LED1_PA, out.ledRed) && ok;
  ok = readRegister8(MASSMORE_MAX3010X_REG_LED2_PA, out.ledIr) && ok;
  ok = readRegister8(MASSMORE_MAX3010X_REG_LED3_PA, out.ledGreen) && ok;
  ok = readRegister8(MASSMORE_MAX3010X_REG_PILOT_PA, out.ledPilot) && ok;
  if (!ok) {
    return false;
  }

  /* ค่า MODE 0 และ 1 เป็นค่าที่ datasheet ระบุว่า "ห้ามใช้" ซึ่งเป็นค่าหลังรีเซ็ต
     ตอนที่ยังไม่มีใครสั่งโหมดให้ชิป กรณีนั้นห้ามเดาเป็น SPO2 เด็ดขาด
     เพราะจะทำให้ไลบรารีคิดว่ามีข้อมูล 2 ช่องแล้วแกะ FIFO ผิดความยาว */
  bool modeValid = true;
  switch (modeConfig & 0x07) {
    case MASSMORE_MAX3010X_MODE_VAL_HR:
      out.mode = MASSMORE_MAX3010X_MODE_HR;
      break;
    case MASSMORE_MAX3010X_MODE_VAL_SPO2:
      out.mode = MASSMORE_MAX3010X_MODE_SPO2;
      break;
    case MASSMORE_MAX3010X_MODE_VAL_MULTI_LED:
      out.mode = MASSMORE_MAX3010X_MODE_MULTI_LED;
      break;
    default:
      /* ชิปยังไม่ได้ตั้งโหมด รายงานค่าที่ไลบรารีจำไว้แทน แล้วไม่แตะสถานะภายใน */
      out.mode = _mode;
      modeValid = false;
      break;
  }

  out.shutdown = (modeConfig & MASSMORE_MAX3010X_BIT_SHUTDOWN) != 0;
  out.adcRange = (massmore_max3010x_adc_range_t)((spo2Config >> 5) & 0x03);
  out.sampleRate = (massmore_max3010x_rate_t)((spo2Config >> 2) & 0x07);
  out.pulseWidth = (massmore_max3010x_pulse_width_t)(spo2Config & 0x03);

  uint8_t average = (uint8_t)((fifoConfig >> 5) & 0x07);
  if (average > (uint8_t)MASSMORE_MAX3010X_SMP_AVE_32) {
    average = (uint8_t)MASSMORE_MAX3010X_SMP_AVE_32;
  }
  out.sampleAverage = (massmore_max3010x_smp_ave_t)average;
  out.fifoRollover = (fifoConfig & MASSMORE_MAX3010X_BIT_ROLLOVER_EN) != 0;
  out.fifoAlmostFull = (uint8_t)(fifoConfig & 0x0F);

  /* ซิงก์สำเนาภายในให้ตรงกับชิปจริง แล้วค่อยคำนวณค่าที่เหลือ */
  _sampleRate = out.sampleRate;
  _sampleAverage = out.sampleAverage;
  if (modeValid) {
    _mode = out.mode;
    recomputeChannels();
  }

  out.variant = _variant;
  out.activeChannels = _activeChannels;
  out.effectiveRateHz = getEffectiveSampleRate();

  _lastError = MASSMORE_MAX3010X_OK;
  return true;
}

/* =========================================================================
   ตรวจสอบว่าเป็นชิปแท้
   ========================================================================= */

const char *MassmoreMAX3010x::getVerifyCheckName(uint8_t index) {
  switch (index) {
    case 0: return "ACK ที่ address 0x57";
    case 1: return "PART_ID = 0x15";
    case 2: return "REV_ID สมเหตุสมผล";
    case 3: return "บิต RESET เคลียร์ตัวเองได้";
    case 4: return "ค่าหลังรีเซ็ตตรงตาม datasheet";
    case 5: return "เขียนอ่านรีจิสเตอร์ได้ตรง";
    case 6: return "PART_ID เขียนทับไม่ได้";
    case 7: return "บิตสงวนเป็นศูนย์";
    case 8: return "ตัวชี้ FIFO เป็นฟิลด์ 5 บิต";
    case 9: return "เซ็นเซอร์อุณหภูมิในตัวทำงาน";
    case 10: return "ADC ตอบสนองเมื่อเปิด LED";
    default: return "ไม่ทราบ";
  }
}

uint8_t MassmoreMAX3010x::getVerifyPassCount() const {
  uint8_t count = 0;
  for (uint8_t i = 0; i < MASSMORE_MAX3010X_CHK_COUNT; i++) {
    if (_verifyMask & (1u << i)) {
      count++;
    }
  }
  return count;
}

massmore_max3010x_genuine_t MassmoreMAX3010x::verifyChip() {
  _verifyMask = 0;

  /* ---- ข้อ 1  ชิปตอบ ACK ที่ address ---- */
  if (!isConnected()) {
    return MASSMORE_MAX3010X_GENUINE_NOT_MAX3010X;
  }
  _verifyMask |= MASSMORE_MAX3010X_CHK_ACK;

  /* ---- ข้อ 2  PART_ID ต้องเป็น 0x15 ---- */
  uint8_t partId = 0;
  if (!readRegister8(MASSMORE_MAX3010X_REG_PART_ID, partId)) {
    return MASSMORE_MAX3010X_GENUINE_NOT_MAX3010X;
  }
  if (partId != MASSMORE_MAX3010X_PART_ID_EXPECTED) {
    /* ไม่ใช่ชิปตระกูลนี้แน่นอน ไม่ต้องตรวจต่อ */
    return MASSMORE_MAX3010X_GENUINE_NOT_MAX3010X;
  }
  _verifyMask |= MASSMORE_MAX3010X_CHK_PART_ID;

  /* ---- ข้อ 3  REV_ID ต้องไม่ใช่ 0x00 และไม่ใช่ 0xFF ----
     ชิปแท้ที่ผลิตจริงจะมีเลขรีวิชันเสมอ ของเลียนแบบมักปล่อยว่างหรือค้างที่ FF */
  uint8_t revId = 0;
  if (readRegister8(MASSMORE_MAX3010X_REG_REVISION_ID, revId) &&
      revId != 0x00 && revId != 0xFF) {
    _verifyMask |= MASSMORE_MAX3010X_CHK_REV_ID;
  }

  /* ---- ข้อ 4  สั่ง RESET แล้วบิตต้องเคลียร์ตัวเองภายใน 100 ms ---- */
  bool resetOk = maskRegister8(MASSMORE_MAX3010X_REG_MODE_CONFIG,
                               MASSMORE_MAX3010X_MASK_RESET,
                               MASSMORE_MAX3010X_BIT_RESET) &&
                 waitForBit(MASSMORE_MAX3010X_REG_MODE_CONFIG,
                            MASSMORE_MAX3010X_BIT_RESET, false, _timeoutMs);
  if (resetOk) {
    _verifyMask |= MASSMORE_MAX3010X_CHK_RESET;
  }
  _mode = MASSMORE_MAX3010X_MODE_HR;
  _sampleAverage = MASSMORE_MAX3010X_SMP_AVE_1;
  _sampleRate = MASSMORE_MAX3010X_RATE_50;
  recomputeChannels();
  flush();

  /* ---- ข้อ 5  ค่าหลังรีเซ็ตต้องตรงกับตาราง power-on-reset ของ datasheet ---- */
  if (resetOk) {
    bool defaultsOk = true;
    const uint8_t zeroRegs[] = {
        MASSMORE_MAX3010X_REG_INT_ENABLE_1, MASSMORE_MAX3010X_REG_INT_ENABLE_2,
        MASSMORE_MAX3010X_REG_FIFO_WR_PTR,  MASSMORE_MAX3010X_REG_OVF_COUNTER,
        MASSMORE_MAX3010X_REG_FIFO_RD_PTR,  MASSMORE_MAX3010X_REG_FIFO_CONFIG,
        MASSMORE_MAX3010X_REG_MODE_CONFIG,  MASSMORE_MAX3010X_REG_SPO2_CONFIG,
        MASSMORE_MAX3010X_REG_LED1_PA,      MASSMORE_MAX3010X_REG_LED2_PA,
        MASSMORE_MAX3010X_REG_MULTI_LED_1,  MASSMORE_MAX3010X_REG_MULTI_LED_2};
    for (uint8_t i = 0; i < sizeof(zeroRegs); i++) {
      uint8_t value = 0xAA;
      if (!readRegister8(zeroRegs[i], value) || value != 0x00) {
        defaultsOk = false;
        break;
      }
    }
    if (defaultsOk) {
      _verifyMask |= MASSMORE_MAX3010X_CHK_POR_DEFAULT;
    }
  }

  /* ---- ข้อ 6  เขียนค่าลงรีจิสเตอร์อ่านเขียนได้แล้วต้องอ่านกลับมาตรง ---- */
  {
    bool rwOk = true;
    const uint8_t patterns[] = {0x55, 0xAA, 0x01, 0xFE};
    for (uint8_t i = 0; i < sizeof(patterns) && rwOk; i++) {
      uint8_t readback = 0;
      if (!writeRegister8(MASSMORE_MAX3010X_REG_LED1_PA, patterns[i]) ||
          !readRegister8(MASSMORE_MAX3010X_REG_LED1_PA, readback) ||
          readback != patterns[i]) {
        rwOk = false;
      }
    }
    writeRegister8(MASSMORE_MAX3010X_REG_LED1_PA, 0x00);
    if (rwOk) {
      _verifyMask |= MASSMORE_MAX3010X_CHK_RW;
    }
  }

  /* ---- ข้อ 7  PART_ID เป็นรีจิสเตอร์อ่านอย่างเดียวจริงหรือไม่ ----
     ชิปแท้จะไม่ยอมให้เขียนทับ ของเลียนแบบที่จำลองด้วย MCU มักเขียนทับได้ */
  {
    writeRegister8(MASSMORE_MAX3010X_REG_PART_ID, 0x00);
    uint8_t readback = 0;
    if (readRegister8(MASSMORE_MAX3010X_REG_PART_ID, readback) &&
        readback == MASSMORE_MAX3010X_PART_ID_EXPECTED) {
      _verifyMask |= MASSMORE_MAX3010X_CHK_READONLY;
    }
  }

  /* ---- ข้อ 8  บิตสงวน 5:3 ของ MODE_CONFIG ต้องอ่านกลับมาเป็นศูนย์เสมอ ---- */
  {
    writeRegister8(MASSMORE_MAX3010X_REG_MODE_CONFIG, 0x38);
    uint8_t readback = 0xFF;
    if (readRegister8(MASSMORE_MAX3010X_REG_MODE_CONFIG, readback) &&
        (readback & 0x38) == 0x00) {
      _verifyMask |= MASSMORE_MAX3010X_CHK_RESERVED;
    }
    writeRegister8(MASSMORE_MAX3010X_REG_MODE_CONFIG, 0x00);
  }

  /* ---- ข้อ 9  ตัวชี้ FIFO เป็นฟิลด์ 5 บิต เขียน 0x20 ต้องวนกลับเป็น 0 ---- */
  {
    bool pointerOk = true;
    uint8_t readback = 0;
    /* เทียบทั้งไบต์ ไม่ใช่แค่ 5 บิตล่าง เพราะบิต 7:5 ของชิปแท้อ่านได้ 0 เสมอ
       ของเลียนแบบที่ทำตัวชี้เป็นไบต์เต็มจะเก็บ 0x20 ไว้แล้วอ่านกลับมาไม่ใช่ 0 */
    if (!writeRegister8(MASSMORE_MAX3010X_REG_FIFO_WR_PTR, 0x1F) ||
        !readRegister8(MASSMORE_MAX3010X_REG_FIFO_WR_PTR, readback) ||
        readback != 0x1F) {
      pointerOk = false;
    }
    if (pointerOk) {
      if (!writeRegister8(MASSMORE_MAX3010X_REG_FIFO_WR_PTR, 0x20) ||
          !readRegister8(MASSMORE_MAX3010X_REG_FIFO_WR_PTR, readback) ||
          readback != 0x00) {
        pointerOk = false;
      }
    }
    writeRegister8(MASSMORE_MAX3010X_REG_FIFO_WR_PTR, 0x00);
    if (pointerOk) {
      _verifyMask |= MASSMORE_MAX3010X_CHK_FIFO_PTR;
    }
  }

  /* ---- ข้อ 10  เซ็นเซอร์อุณหภูมิในตัวต้องให้ค่าที่เป็นไปได้ ----
     ชิปทำงานได้ในช่วง -40 ถึง 85 องศา เราใช้เกณฑ์กว้างกว่าอุณหภูมิห้องเล็กน้อย
     เพื่อให้ทดสอบได้ทั้งในห้องแอร์และในสายการผลิตที่ร้อนกว่า */
  {
    float dieTemp = readTemperature(120);
    if (!isnan(dieTemp) && dieTemp > -20.0f && dieTemp < 85.0f) {
      _verifyMask |= MASSMORE_MAX3010X_CHK_TEMP;
    }
  }

  /* ---- ข้อ 11  เปิด LED แล้วค่าที่ ADC อ่านได้ต้องขยับจริง ----
     เป็นการพิสูจน์ว่ามีภาคออปติกอยู่จริง ไม่ใช่แค่ MCU จำลองรีจิสเตอร์ */
  {
    setFifoRollover(true);
    setSampleAverage(MASSMORE_MAX3010X_SMP_AVE_4);
    setAdcRange(MASSMORE_MAX3010X_ADC_RANGE_4096);
    setSampleRate(MASSMORE_MAX3010X_RATE_400);
    setPulseWidth(MASSMORE_MAX3010X_PULSE_411US);

    /* รอบแรก ปิด LED ทุกดวง ค่าที่อ่านได้ควรต่ำมาก */
    setPulseAmplitudeRed(0);
    setPulseAmplitudeIR(0);
    setMode(MASSMORE_MAX3010X_MODE_SPO2);
    clearFifo();
    delay(60);
    update();
    uint32_t darkMax = 0;
    while (available()) {
      if (getIR() > darkMax) darkMax = getIR();
      if (getRed() > darkMax) darkMax = getRed();
      nextSample();
    }

    /* รอบสอง เปิด LED แรง ๆ ค่าที่อ่านได้ต้องสูงขึ้นอย่างมีนัยสำคัญ */
    setPulseAmplitudeRed(0x7F);
    setPulseAmplitudeIR(0x7F);
    clearFifo();
    delay(80);
    update();
    uint32_t litMax = 0;
    while (available()) {
      if (getIR() > litMax) litMax = getIR();
      if (getRed() > litMax) litMax = getRed();
      nextSample();
    }

    /* เกณฑ์ผ่าน  ต้องมีค่าจริงออกมา และต้องต่างจากตอนปิด LED ชัดเจน */
    if (litMax > 0 && litMax > darkMax + 200) {
      _verifyMask |= MASSMORE_MAX3010X_CHK_LED;
    }

    setPulseAmplitudeRed(0);
    setPulseAmplitudeIR(0);
    setMode(MASSMORE_MAX3010X_MODE_HR);
    clearFifo();
  }

  uint8_t passCount = getVerifyPassCount();
  if (passCount == MASSMORE_MAX3010X_CHK_COUNT) {
    return MASSMORE_MAX3010X_GENUINE_PASS;
  }
  if (passCount >= MASSMORE_MAX3010X_CHK_COUNT - 2) {
    return MASSMORE_MAX3010X_GENUINE_PARTIAL;
  }
  return MASSMORE_MAX3010X_GENUINE_SUSPECT;
}

/* =========================================================================
   ข้อความอธิบายข้อผิดพลาด
   ========================================================================= */

const char *MassmoreMAX3010x::errorToString(massmore_max3010x_error_t error) {
  switch (error) {
    case MASSMORE_MAX3010X_OK:
      return "สำเร็จ";
    case MASSMORE_MAX3010X_ERR_NOT_BEGUN:
      return "ยังไม่ได้เรียก begin()";
    case MASSMORE_MAX3010X_ERR_NO_DEVICE:
      return "ไม่พบอุปกรณ์บนบัส I2C ที่ address นี้";
    case MASSMORE_MAX3010X_ERR_I2C_WRITE:
      return "เขียนข้อมูลลงบัส I2C ไม่สำเร็จ";
    case MASSMORE_MAX3010X_ERR_I2C_READ:
      return "อ่านข้อมูลจากบัส I2C ได้ไม่ครบ";
    case MASSMORE_MAX3010X_ERR_TIMEOUT:
      return "รอเกินเวลาที่กำหนด";
    case MASSMORE_MAX3010X_ERR_WRONG_CHIP:
      return "PART_ID ไม่ใช่ 0x15 อาจเป็น MAX30100 หรือชิปคนละตัว";
    case MASSMORE_MAX3010X_ERR_NO_DATA:
      return "ยังไม่มีข้อมูลใหม่ใน FIFO";
    case MASSMORE_MAX3010X_ERR_BAD_ARG:
      return "พารามิเตอร์ที่ส่งมาไม่ถูกต้อง";
    case MASSMORE_MAX3010X_ERR_UNSUPPORTED:
      return "ชิปรุ่นที่ต่ออยู่ไม่มีความสามารถนี้";
    default:
      return "ข้อผิดพลาดที่ไม่รู้จัก";
  }
}

const char *MassmoreMAX3010x::lastErrorString() const {
  return errorToString(_lastError);
}
