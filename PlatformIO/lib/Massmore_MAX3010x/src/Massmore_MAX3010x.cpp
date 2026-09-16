/**
 * @file    Massmore_MAX3010x.cpp
 * @brief   การทำงานภายในของไลบรารี Massmore_MAX3010x
 *
 * @copyright Copyright (c) 2026 Massmore Biz Co., Ltd.
 * @license   MIT
 */

#include "Massmore_MAX3010x.h"

#include <math.h>

/* จำนวน byte สูงสุดต่อ I2C transaction — 30 เพราะ Wire buffer ของ AVR มี 32 bytes
   และ 30 หารด้วย 3 และ 6 ลงตัว จึงไม่ตัดกลาง sample */
#define MAX3010X_I2C_CHUNK 30

#define MAX3010X_ALGO_HIGHPASS_HZ 0.5f  /* ต่ำกว่านี้ = baseline drift ไม่ใช่ชีพจร */
#define MAX3010X_ALGO_LOWPASS_HZ 4.0f   /* สูงกว่านี้ = noise จากไฟบ้าน / การขยับ */
#define MAX3010X_ALGO_RMS_HZ 0.25f      /* หน้าต่าง RMS ประมาณ 4 วินาที */
#define MAX3010X_ALGO_THRESHOLD_RISE 0.62f
#define MAX3010X_ALGO_THRESHOLD_FALL 0.38f
#define MAX3010X_ALGO_MIN_AMPLITUDE 25.0f
#define MAX3010X_TWO_PI 6.28318531f

/* =========================================================================
   Constructor
   ========================================================================= */

Massmore_MAX3010x::Massmore_MAX3010x()
    : _wire(NULL),
      _intPin(-1),
      _begun(false),
      _timeoutMs(100),
      _lastError(ErrorCode::NOT_BEGUN),
      _variant(Variant::AUTO),
      _variantForced(false),
      _verifyMask(0),
      _mode(Mode::SPO2),
      _sampleAverage(SampleAverage::X1),
      _sampleRate(SampleRate::HZ_50),
      _activeChannels(2),
      _head(0),
      _tail(0),
      _count(0) {
  for (uint16_t i = 0; i < MASSMORE_MAX3010X_BUFFER_SIZE; i++) {
    _bufRed[i] = _bufIr[i] = _bufGreen[i] = _bufMs[i] = 0;
  }
}

/* =========================================================================
   Low-level I2C
   ========================================================================= */

bool Massmore_MAX3010x::readRegister8(uint8_t reg, uint8_t &value) {
  if (_wire == NULL) {
    _lastError = ErrorCode::NOT_BEGUN;
    return false;
  }
  _wire->beginTransmission(MASSMORE_MAX3010X_I2C_ADDRESS);
  _wire->write(reg);
  /* ปิด transaction แล้วเปิดใหม่ตอนอ่าน (datasheet รองรับทั้ง repeated-start และ stop) */
  if (_wire->endTransmission() != 0) {
    _lastError = ErrorCode::BUS_ERROR;
    return false;
  }
  if (_wire->requestFrom((uint8_t)MASSMORE_MAX3010X_I2C_ADDRESS, (uint8_t)1) != 1) {
    _lastError = ErrorCode::BUS_ERROR;
    return false;
  }
  value = (uint8_t)_wire->read();
  _lastError = ErrorCode::OK;
  return true;
}

uint8_t Massmore_MAX3010x::readRegister8(uint8_t reg) {
  uint8_t value = 0;
  if (!readRegister8(reg, value)) return 0;
  return value;
}

bool Massmore_MAX3010x::writeRegister8(uint8_t reg, uint8_t value) {
  if (_wire == NULL) {
    _lastError = ErrorCode::NOT_BEGUN;
    return false;
  }
  _wire->beginTransmission(MASSMORE_MAX3010X_I2C_ADDRESS);
  _wire->write(reg);
  _wire->write(value);
  if (_wire->endTransmission() != 0) {
    _lastError = ErrorCode::BUS_ERROR;
    return false;
  }
  _lastError = ErrorCode::OK;
  return true;
}

bool Massmore_MAX3010x::maskRegister8(uint8_t reg, uint8_t keepMask, uint8_t value) {
  uint8_t current = 0;
  if (!readRegister8(reg, current)) return false;
  /* Read-Modify-Write: keepMask = bits ที่ต้อง "เก็บไว้" ตามตารางใน datasheet */
  current = (uint8_t)((current & keepMask) | (value & (uint8_t)~keepMask));
  return writeRegister8(reg, current);
}

bool Massmore_MAX3010x::readRegisterBurst(uint8_t reg, uint8_t *buffer, uint8_t length) {
  if (_wire == NULL) {
    _lastError = ErrorCode::NOT_BEGUN;
    return false;
  }
  if (buffer == NULL || length == 0) {
    _lastError = ErrorCode::BAD_ARG;
    return false;
  }
  _wire->beginTransmission(MASSMORE_MAX3010X_I2C_ADDRESS);
  _wire->write(reg);
  if (_wire->endTransmission() != 0) {
    _lastError = ErrorCode::BUS_ERROR;
    return false;
  }
  uint8_t received = 0;
  while (received < length) {
    uint8_t want = (uint8_t)(length - received);
    if (want > MAX3010X_I2C_CHUNK) want = MAX3010X_I2C_CHUNK;
    uint8_t got = (uint8_t)_wire->requestFrom((uint8_t)MASSMORE_MAX3010X_I2C_ADDRESS, want);
    if (got != want) {
      _lastError = ErrorCode::BUS_ERROR;
      return false;
    }
    for (uint8_t i = 0; i < got; i++) buffer[received + i] = (uint8_t)_wire->read();
    received = (uint8_t)(received + got);
  }
  _lastError = ErrorCode::OK;
  return true;
}

bool Massmore_MAX3010x::waitForBit(uint8_t reg, uint8_t bitMask, bool wantSet, uint32_t timeoutMs) {
  const uint32_t start = millis();
  while ((uint32_t)(millis() - start) < timeoutMs) {
    uint8_t value = 0;
    if (!readRegister8(reg, value)) return false;
    if (((value & bitMask) != 0) == wantSet) return true;
    delay(1);
  }
  _lastError = ErrorCode::TIMEOUT;
  return false;
}

/* =========================================================================
   begin / reset / power
   ========================================================================= */

bool Massmore_MAX3010x::begin(TwoWire &wirePort, int8_t intPin) {
  _wire = &wirePort;
  _intPin = intPin;
  _begun = false;
  _verifyMask = 0;

  if (!isConnected()) {
    _lastError = ErrorCode::NOT_FOUND;
    return false;
  }
  if (!verifyChipID()) return false; /* lastError = WRONG_ID หรือ BUS_ERROR */

  _begun = true;
  if (!softReset()) {
    _begun = false;
    return false;
  }
  if (!_variantForced) detectVariant();

  flush();
  _lastError = ErrorCode::OK;
  return true;
}

bool Massmore_MAX3010x::isConnected() {
  if (_wire == NULL) {
    _lastError = ErrorCode::NOT_BEGUN;
    return false;
  }
  _wire->beginTransmission(MASSMORE_MAX3010X_I2C_ADDRESS);
  if (_wire->endTransmission() != 0) {
    _lastError = ErrorCode::NOT_FOUND;
    return false;
  }
  _lastError = ErrorCode::OK;
  return true;
}

bool Massmore_MAX3010x::softReset() {
  if (!maskRegister8(MASSMORE_MAX3010X_REG_MODE_CONFIG, MASSMORE_MAX3010X_MASK_RESET,
                     MASSMORE_MAX3010X_BIT_RESET)) {
    return false;
  }
  /* RESET bit self-clear เมื่อชิป reset เสร็จ (datasheet ไม่ระบุเวลาตายตัว จึงใช้ timeout) */
  if (!waitForBit(MASSMORE_MAX3010X_REG_MODE_CONFIG, MASSMORE_MAX3010X_BIT_RESET, false, _timeoutMs)) {
    return false;
  }
  _mode = Mode::HEART_RATE;
  _sampleAverage = SampleAverage::X1;
  _sampleRate = SampleRate::HZ_50;
  recomputeChannels();
  flush();
  return true;
}

bool Massmore_MAX3010x::shutdown() {
  return maskRegister8(MASSMORE_MAX3010X_REG_MODE_CONFIG, MASSMORE_MAX3010X_MASK_SHUTDOWN,
                       MASSMORE_MAX3010X_BIT_SHUTDOWN);
}

bool Massmore_MAX3010x::wakeUp() {
  return maskRegister8(MASSMORE_MAX3010X_REG_MODE_CONFIG, MASSMORE_MAX3010X_MASK_SHUTDOWN, 0x00);
}

bool Massmore_MAX3010x::setupDefault(uint8_t ledPower) {
  return setup(ledPower, SampleAverage::X8, Mode::SPO2, SampleRate::HZ_400, PulseWidth::US_411,
               AdcRange::NA_4096);
}

bool Massmore_MAX3010x::setup(uint8_t ledPower, SampleAverage average, Mode mode, SampleRate rate,
                              PulseWidth width, AdcRange range) {
  if (!_begun) {
    _lastError = ErrorCode::NOT_BEGUN;
    return false;
  }
  bool ok = softReset();
  ok = ok && setFifoAlmostFull(15);
  ok = ok && setFifoRollover(true);
  ok = ok && setSampleAverage(average);
  ok = ok && setAdcRange(range);
  ok = ok && setSampleRate(rate);
  ok = ok && setPulseWidth(width);
  ok = ok && setPulseAmplitudeRed(ledPower);
  ok = ok && setPulseAmplitudeIR(ledPower);
  if (hasGreenLed()) ok = ok && setPulseAmplitudeGreen(mode == Mode::MULTI_LED ? ledPower : 0);
  if (hasProximity()) ok = ok && setPulseAmplitudeProximity(ledPower);

  if (mode == Mode::MULTI_LED) {
    ok = ok && setMultiLedSlot(1, Slot::RED);
    ok = ok && setMultiLedSlot(2, Slot::IR);
    ok = ok && setMultiLedSlot(3, hasGreenLed() ? Slot::GREEN : Slot::NONE);
    ok = ok && setMultiLedSlot(4, Slot::NONE);
  }
  /* ตั้ง mode เป็นขั้นสุดท้าย เพราะชิปเริ่มยิง LED ทันทีที่ตั้ง */
  ok = ok && setMode(mode);
  ok = ok && clearFifo();
  flush();
  return ok;
}

/* =========================================================================
   Configuration
   ========================================================================= */

void Massmore_MAX3010x::recomputeChannels() {
  switch (_mode) {
    case Mode::HEART_RATE:
      _activeChannels = 1;
      break;
    case Mode::SPO2:
      _activeChannels = 2;
      break;
    default: {
      /* Multi-LED: จำนวน channel = จำนวน slot ที่ไม่ใช่ NONE ต้องอ่านจากชิป */
      uint8_t slots = 0, reg1 = 0, reg2 = 0;
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

bool Massmore_MAX3010x::setMode(Mode mode) {
  uint8_t value;
  switch (mode) {
    case Mode::HEART_RATE: value = MASSMORE_MAX3010X_MODE_VAL_HR; break;
    case Mode::SPO2: value = MASSMORE_MAX3010X_MODE_VAL_SPO2; break;
    case Mode::MULTI_LED: value = MASSMORE_MAX3010X_MODE_VAL_MULTI_LED; break;
    default:
      _lastError = ErrorCode::BAD_ARG;
      return false;
  }
  if (!maskRegister8(MASSMORE_MAX3010X_REG_MODE_CONFIG, MASSMORE_MAX3010X_MASK_MODE, value)) return false;
  _mode = mode;
  recomputeChannels();
  flush();
  return true;
}

bool Massmore_MAX3010x::setSampleAverage(SampleAverage average) {
  if ((uint8_t)average > (uint8_t)SampleAverage::X32) {
    _lastError = ErrorCode::BAD_ARG;
    return false;
  }
  if (!maskRegister8(MASSMORE_MAX3010X_REG_FIFO_CONFIG, MASSMORE_MAX3010X_MASK_SMP_AVE,
                     (uint8_t)((uint8_t)average << 5))) {
    return false;
  }
  _sampleAverage = average;
  return true;
}

bool Massmore_MAX3010x::setFifoRollover(bool enable) {
  return maskRegister8(MASSMORE_MAX3010X_REG_FIFO_CONFIG, MASSMORE_MAX3010X_MASK_ROLLOVER,
                       enable ? MASSMORE_MAX3010X_BIT_ROLLOVER_EN : 0x00);
}

bool Massmore_MAX3010x::setFifoAlmostFull(uint8_t spacesLeft) {
  if (spacesLeft > 15) {
    _lastError = ErrorCode::BAD_ARG;
    return false;
  }
  return maskRegister8(MASSMORE_MAX3010X_REG_FIFO_CONFIG, MASSMORE_MAX3010X_MASK_A_FULL, spacesLeft);
}

bool Massmore_MAX3010x::setAdcRange(AdcRange range) {
  if ((uint8_t)range > (uint8_t)AdcRange::NA_16384) {
    _lastError = ErrorCode::BAD_ARG;
    return false;
  }
  return maskRegister8(MASSMORE_MAX3010X_REG_SPO2_CONFIG, MASSMORE_MAX3010X_MASK_ADC_RANGE,
                       (uint8_t)((uint8_t)range << 5));
}

bool Massmore_MAX3010x::setSampleRate(SampleRate rate) {
  if ((uint8_t)rate > (uint8_t)SampleRate::HZ_3200) {
    _lastError = ErrorCode::BAD_ARG;
    return false;
  }
  if (!maskRegister8(MASSMORE_MAX3010X_REG_SPO2_CONFIG, MASSMORE_MAX3010X_MASK_SAMPLE_RATE,
                     (uint8_t)((uint8_t)rate << 2))) {
    return false;
  }
  _sampleRate = rate;
  return true;
}

bool Massmore_MAX3010x::setPulseWidth(PulseWidth width) {
  if ((uint8_t)width > (uint8_t)PulseWidth::US_411) {
    _lastError = ErrorCode::BAD_ARG;
    return false;
  }
  return maskRegister8(MASSMORE_MAX3010X_REG_SPO2_CONFIG, MASSMORE_MAX3010X_MASK_PULSE_WIDTH, (uint8_t)width);
}

bool Massmore_MAX3010x::setPulseAmplitudeRed(uint8_t value) {
  return writeRegister8(MASSMORE_MAX3010X_REG_LED1_PA, value);
}
bool Massmore_MAX3010x::setPulseAmplitudeIR(uint8_t value) {
  return writeRegister8(MASSMORE_MAX3010X_REG_LED2_PA, value);
}
bool Massmore_MAX3010x::setPulseAmplitudeGreen(uint8_t value) {
  if (!hasGreenLed()) {
    _lastError = ErrorCode::UNSUPPORTED;
    return false;
  }
  return writeRegister8(MASSMORE_MAX3010X_REG_LED3_PA, value);
}
bool Massmore_MAX3010x::setPulseAmplitudeProximity(uint8_t value) {
  if (!hasProximity()) {
    _lastError = ErrorCode::UNSUPPORTED;
    return false;
  }
  return writeRegister8(MASSMORE_MAX3010X_REG_PILOT_PA, value);
}

bool Massmore_MAX3010x::setAllLedsOff() {
  bool ok = setPulseAmplitudeRed(0);
  ok = ok && setPulseAmplitudeIR(0);
  if (hasGreenLed()) ok = ok && setPulseAmplitudeGreen(0);
  if (hasProximity()) ok = ok && setPulseAmplitudeProximity(0);
  return ok;
}

bool Massmore_MAX3010x::setMultiLedSlot(uint8_t slotNumber, Slot device) {
  if (slotNumber < 1 || slotNumber > 4 || (uint8_t)device > 7) {
    _lastError = ErrorCode::BAD_ARG;
    return false;
  }
  if ((device == Slot::GREEN || device == Slot::GREEN_PILOT) && !hasGreenLed()) {
    _lastError = ErrorCode::UNSUPPORTED;
    return false;
  }
  const uint8_t reg = (slotNumber <= 2) ? MASSMORE_MAX3010X_REG_MULTI_LED_1 : MASSMORE_MAX3010X_REG_MULTI_LED_2;
  const bool odd = (slotNumber == 1 || slotNumber == 3);
  bool ok = odd ? maskRegister8(reg, MASSMORE_MAX3010X_MASK_SLOT_ODD, (uint8_t)device)
                : maskRegister8(reg, MASSMORE_MAX3010X_MASK_SLOT_EVEN, (uint8_t)((uint8_t)device << 4));
  if (!ok) return false;
  if (_mode == Mode::MULTI_LED) {
    recomputeChannels();
    flush();
  }
  return true;
}

bool Massmore_MAX3010x::setProximityThreshold(uint8_t threshold) {
  if (!hasProximity()) {
    _lastError = ErrorCode::UNSUPPORTED;
    return false;
  }
  return writeRegister8(MASSMORE_MAX3010X_REG_PROX_INT_THRESH, threshold);
}

/* =========================================================================
   Interrupts
   ========================================================================= */

bool Massmore_MAX3010x::enableInterrupt(InterruptSource source, bool enable) {
  uint8_t reg = MASSMORE_MAX3010X_REG_INT_ENABLE_1;
  uint8_t bit;
  switch (source) {
    case InterruptSource::ALMOST_FULL: bit = MASSMORE_MAX3010X_INT_A_FULL; break;
    case InterruptSource::DATA_READY: bit = MASSMORE_MAX3010X_INT_PPG_RDY; break;
    case InterruptSource::AMBIENT_OVERFLOW: bit = MASSMORE_MAX3010X_INT_ALC_OVF; break;
    case InterruptSource::PROXIMITY:
      if (!hasProximity()) {
        _lastError = ErrorCode::UNSUPPORTED;
        return false;
      }
      bit = MASSMORE_MAX3010X_INT_PROX;
      break;
    case InterruptSource::DIE_TEMP_READY:
      reg = MASSMORE_MAX3010X_REG_INT_ENABLE_2;
      bit = MASSMORE_MAX3010X_INT_DIE_TEMP_RDY;
      break;
    default:
      _lastError = ErrorCode::BAD_ARG;
      return false;
  }
  return maskRegister8(reg, (uint8_t)~bit, enable ? bit : 0);
}

bool Massmore_MAX3010x::disableAllInterrupts() {
  bool ok = writeRegister8(MASSMORE_MAX3010X_REG_INT_ENABLE_1, 0x00);
  ok = writeRegister8(MASSMORE_MAX3010X_REG_INT_ENABLE_2, 0x00) && ok;
  return ok;
}

uint8_t Massmore_MAX3010x::getInterruptStatus1() { return readRegister8(MASSMORE_MAX3010X_REG_INT_STATUS_1); }
uint8_t Massmore_MAX3010x::getInterruptStatus2() { return readRegister8(MASSMORE_MAX3010X_REG_INT_STATUS_2); }

/* =========================================================================
   FIFO
   ========================================================================= */

bool Massmore_MAX3010x::clearFifo() {
  bool ok = writeRegister8(MASSMORE_MAX3010X_REG_FIFO_WR_PTR, 0x00);
  ok = writeRegister8(MASSMORE_MAX3010X_REG_OVF_COUNTER, 0x00) && ok;
  ok = writeRegister8(MASSMORE_MAX3010X_REG_FIFO_RD_PTR, 0x00) && ok;
  flush();
  return ok;
}

uint8_t Massmore_MAX3010x::getWritePointer() { return (uint8_t)(readRegister8(MASSMORE_MAX3010X_REG_FIFO_WR_PTR) & 0x1F); }
uint8_t Massmore_MAX3010x::getReadPointer() { return (uint8_t)(readRegister8(MASSMORE_MAX3010X_REG_FIFO_RD_PTR) & 0x1F); }
uint8_t Massmore_MAX3010x::getOverflowCounter() { return (uint8_t)(readRegister8(MASSMORE_MAX3010X_REG_OVF_COUNTER) & 0x1F); }

uint8_t Massmore_MAX3010x::getSamplesInFifo() {
  uint8_t writePtr = 0, readPtr = 0;
  if (!readRegister8(MASSMORE_MAX3010X_REG_FIFO_WR_PTR, writePtr)) return 0;
  if (!readRegister8(MASSMORE_MAX3010X_REG_FIFO_RD_PTR, readPtr)) return 0;
  writePtr &= 0x1F;
  readPtr &= 0x1F;
  int16_t pending = (int16_t)writePtr - (int16_t)readPtr;
  if (pending < 0) pending += MASSMORE_MAX3010X_FIFO_DEPTH;
  if (pending == 0) {
    /* pointer เท่ากันได้ทั้ง "ว่าง" และ "เต็ม 32" — แยกด้วย overflow counter */
    uint8_t overflow = 0;
    if (readRegister8(MASSMORE_MAX3010X_REG_OVF_COUNTER, overflow) && (overflow & 0x1F) != 0) {
      return MASSMORE_MAX3010X_FIFO_DEPTH;
    }
  }
  return (uint8_t)pending;
}

void Massmore_MAX3010x::pushSample(uint32_t red, uint32_t ir, uint32_t green) {
  _bufRed[_head] = red;
  _bufIr[_head] = ir;
  _bufGreen[_head] = green;
  _bufMs[_head] = millis();
  _head = (uint8_t)((_head + 1) % MASSMORE_MAX3010X_BUFFER_SIZE);
  if (_count < MASSMORE_MAX3010X_BUFFER_SIZE) {
    _count++;
  } else {
    _tail = (uint8_t)((_tail + 1) % MASSMORE_MAX3010X_BUFFER_SIZE); /* ทับตัวเก่าสุด */
  }
}

bool Massmore_MAX3010x::requestConversion() {
  if (!_begun) {
    _lastError = ErrorCode::NOT_BEGUN;
    return false;
  }
  return clearFifo();
}

bool Massmore_MAX3010x::update() {
  if (!_begun) {
    _lastError = ErrorCode::NOT_BEGUN;
    return false;
  }
  const uint8_t pending = getSamplesInFifo();
  if (pending == 0) {
    _lastError = ErrorCode::NOT_READY;
    return false;
  }
  const uint8_t bytesPerSample = (uint8_t)(_activeChannels * MASSMORE_MAX3010X_BYTES_PER_CHANNEL);
  uint8_t samplesPerChunk = (uint8_t)(MAX3010X_I2C_CHUNK / bytesPerSample);
  if (samplesPerChunk == 0) samplesPerChunk = 1;

  uint8_t buffer[MAX3010X_I2C_CHUNK];
  uint8_t fetched = 0;
  while (fetched < pending) {
    uint8_t want = (uint8_t)(pending - fetched);
    if (want > samplesPerChunk) want = samplesPerChunk;
    const uint8_t bytes = (uint8_t)(want * bytesPerSample);
    if (!readRegisterBurst(MASSMORE_MAX3010X_REG_FIFO_DATA, buffer, bytes)) return fetched > 0;

    for (uint8_t s = 0; s < want; s++) {
      const uint8_t *p = &buffer[s * bytesPerSample];
      uint32_t channel[4] = {0, 0, 0, 0};
      for (uint8_t c = 0; c < _activeChannels && c < 4; c++) {
        uint32_t v = ((uint32_t)p[c * 3] << 16) | ((uint32_t)p[c * 3 + 1] << 8) | (uint32_t)p[c * 3 + 2];
        channel[c] = v & MASSMORE_MAX3010X_DATA_MASK; /* 18-bit ในกรอบ 24-bit */
      }
      switch (_mode) {
        case Mode::HEART_RATE: pushSample(channel[0], 0, 0); break;
        case Mode::SPO2: pushSample(channel[0], channel[1], 0); break;
        default: pushSample(channel[0], channel[1], channel[2]); break;
      }
    }
    fetched = (uint8_t)(fetched + want);
  }
  if (pending >= MASSMORE_MAX3010X_FIFO_DEPTH) {
    /* กวาดข้อมูลที่ล้นออกหมดแล้ว ต้องล้าง overflow counter ด้วย */
    writeRegister8(MASSMORE_MAX3010X_REG_OVF_COUNTER, 0x00);
  }
  _lastError = ErrorCode::OK;
  return true;
}

bool Massmore_MAX3010x::getReadings(Readings &out) {
  if (_count == 0) {
    _lastError = ErrorCode::NOT_READY;
    return false;
  }
  out.red = _bufRed[_tail];
  out.ir = _bufIr[_tail];
  out.green = _bufGreen[_tail];
  out.timestampMs = _bufMs[_tail];
  nextSample();
  _lastError = ErrorCode::OK;
  return true;
}

void Massmore_MAX3010x::nextSample() {
  if (_count == 0) return;
  _tail = (uint8_t)((_tail + 1) % MASSMORE_MAX3010X_BUFFER_SIZE);
  _count--;
}

void Massmore_MAX3010x::flush() {
  _head = _tail = _count = 0;
}

bool Massmore_MAX3010x::readAll(Readings &out, uint32_t timeoutMs) {
  if (!_begun) {
    _lastError = ErrorCode::NOT_BEGUN;
    return false;
  }
  const uint32_t start = millis();
  do {
    if (_count > 0 || update()) {
      if (getReadings(out)) return true;
    }
    delay(1);
  } while ((uint32_t)(millis() - start) < timeoutMs);
  _lastError = ErrorCode::TIMEOUT;
  return false;
}

/* =========================================================================
   Die temperature
   ========================================================================= */

bool Massmore_MAX3010x::startTemperatureConversion() {
  return writeRegister8(MASSMORE_MAX3010X_REG_DIE_TEMP_CONFIG, MASSMORE_MAX3010X_BIT_TEMP_EN);
}

bool Massmore_MAX3010x::isTemperatureReady() {
  uint8_t config = 0;
  if (!readRegister8(MASSMORE_MAX3010X_REG_DIE_TEMP_CONFIG, config)) return false;
  if ((config & MASSMORE_MAX3010X_BIT_TEMP_EN) == 0) return true; /* TEMP_EN self-clear */
  uint8_t status = 0;
  if (readRegister8(MASSMORE_MAX3010X_REG_INT_STATUS_2, status) && (status & MASSMORE_MAX3010X_INT_DIE_TEMP_RDY)) {
    return true;
  }
  _lastError = ErrorCode::NOT_READY;
  return false;
}

float Massmore_MAX3010x::getTemperatureResult() {
  uint8_t integerPart = 0, fractionPart = 0;
  if (!readRegister8(MASSMORE_MAX3010X_REG_DIE_TEMP_INT, integerPart)) return NAN;
  if (!readRegister8(MASSMORE_MAX3010X_REG_DIE_TEMP_FRAC, fractionPart)) return NAN;
  /* TINT = signed 8-bit (2's complement), TFRAC = 4-bit x 0.0625 C */
  return (float)(int8_t)integerPart + (float)(fractionPart & 0x0F) * MASSMORE_MAX3010X_TEMP_FRAC_STEP;
}

float Massmore_MAX3010x::readTemperature(uint32_t timeoutMs) {
  if (!_begun) {
    _lastError = ErrorCode::NOT_BEGUN;
    return NAN;
  }
  if (!startTemperatureConversion()) return NAN;
  const uint32_t start = millis();
  while ((uint32_t)(millis() - start) < timeoutMs) {
    if (isTemperatureReady()) return getTemperatureResult();
    delay(1);
  }
  _lastError = ErrorCode::TIMEOUT;
  return NAN;
}

/* =========================================================================
   Identity / variant
   ========================================================================= */

uint8_t Massmore_MAX3010x::readPartID() { return readRegister8(MASSMORE_MAX3010X_REG_PART_ID); }
uint8_t Massmore_MAX3010x::readRevisionID() { return readRegister8(MASSMORE_MAX3010X_REG_REVISION_ID); }

bool Massmore_MAX3010x::verifyChipID() {
  uint8_t partId = 0;
  if (!readRegister8(MASSMORE_MAX3010X_REG_PART_ID, partId)) return false;
  if (partId != MASSMORE_MAX3010X_PART_ID_EXPECTED) {
    _lastError = ErrorCode::WRONG_ID;
    return false;
  }
  _lastError = ErrorCode::OK;
  return true;
}

uint32_t Massmore_MAX3010x::getSerialNumber() {
  /* MAX3010x ไม่มี serial-number / lot register ใน datasheet — คืน 0 */
  return 0;
}

bool Massmore_MAX3010x::isGenuine() { return verifyChip() == Genuine::PASS; }

void Massmore_MAX3010x::setVariant(Variant variant) {
  _variant = variant;
  _variantForced = (variant != Variant::AUTO);
}

bool Massmore_MAX3010x::hasGreenLed() const {
  return _variant == Variant::MAX30101 || _variant == Variant::MAX30105;
}
bool Massmore_MAX3010x::hasProximity() const { return _variant == Variant::MAX30105; }

const char *Massmore_MAX3010x::getVariantName() const {
  switch (_variant) {
    case Variant::MAX30102: return "MAX30102";
    case Variant::MAX30101: return "MAX30101";
    case Variant::MAX30105: return "MAX30105";
    default: return "MAX3010x";
  }
}

bool Massmore_MAX3010x::detectVariant() {
  /* ทุกรุ่นคืน PART_ID 0x15 จึงเดาจาก register ที่มีเฉพาะรุ่น (register ที่ไม่มีจริงอ่านได้ 0)
       LED3_PA (0x0E)  MAX30101 / MAX30105
       LED4_PA (0x0F)  MAX30101 เท่านั้น
       PROX_INT_THRESH (0x30)  MAX30105 เท่านั้น */
  _variant = Variant::MAX30102;
  const uint8_t probe = 0x2A;
  uint8_t saved = 0, readback = 0;

  readRegister8(MASSMORE_MAX3010X_REG_LED3_PA, saved);
  if (!writeRegister8(MASSMORE_MAX3010X_REG_LED3_PA, probe)) return false;
  if (!readRegister8(MASSMORE_MAX3010X_REG_LED3_PA, readback)) return false;
  writeRegister8(MASSMORE_MAX3010X_REG_LED3_PA, saved);
  if (readback != probe) {
    _lastError = ErrorCode::OK;
    return true; /* ไม่มี LED3 = MAX30102 */
  }

  readRegister8(MASSMORE_MAX3010X_REG_LED4_PA, saved);
  if (writeRegister8(MASSMORE_MAX3010X_REG_LED4_PA, probe) && readRegister8(MASSMORE_MAX3010X_REG_LED4_PA, readback)) {
    writeRegister8(MASSMORE_MAX3010X_REG_LED4_PA, saved);
    if (readback == probe) {
      _variant = Variant::MAX30101;
      _lastError = ErrorCode::OK;
      return true;
    }
  }
  _variant = Variant::MAX30105;
  _lastError = ErrorCode::OK;
  return true;
}

uint8_t Massmore_MAX3010x::milliAmpToLedCode(float milliAmp) {
  if (!(milliAmp == milliAmp) || milliAmp <= 0.0f) return 0; /* NaN guard */
  float code = milliAmp / MASSMORE_MAX3010X_LED_STEP_MA + 0.5f;
  return (code > 255.0f) ? 255 : (uint8_t)code;
}

float Massmore_MAX3010x::getEffectiveSampleRate() const {
  static const uint16_t rateTable[8] = {50, 100, 200, 400, 800, 1000, 1600, 3200};
  static const uint8_t averageTable[6] = {1, 2, 4, 8, 16, 32};
  uint8_t r = (uint8_t)_sampleRate, a = (uint8_t)_sampleAverage;
  if (r > 7) r = 7;
  if (a > 5) a = 5;
  return (float)rateTable[r] / (float)averageTable[a];
}

bool Massmore_MAX3010x::readConfiguration(Config &out) {
  if (!_begun) {
    _lastError = ErrorCode::NOT_BEGUN;
    return false;
  }
  uint8_t modeConfig = 0, spo2Config = 0, fifoConfig = 0;
  bool ok = readRegister8(MASSMORE_MAX3010X_REG_MODE_CONFIG, modeConfig);
  ok = readRegister8(MASSMORE_MAX3010X_REG_SPO2_CONFIG, spo2Config) && ok;
  ok = readRegister8(MASSMORE_MAX3010X_REG_FIFO_CONFIG, fifoConfig) && ok;
  ok = readRegister8(MASSMORE_MAX3010X_REG_PART_ID, out.partId) && ok;
  ok = readRegister8(MASSMORE_MAX3010X_REG_REVISION_ID, out.revisionId) && ok;
  ok = readRegister8(MASSMORE_MAX3010X_REG_LED1_PA, out.ledRed) && ok;
  ok = readRegister8(MASSMORE_MAX3010X_REG_LED2_PA, out.ledIr) && ok;
  ok = readRegister8(MASSMORE_MAX3010X_REG_LED3_PA, out.ledGreen) && ok;
  ok = readRegister8(MASSMORE_MAX3010X_REG_PILOT_PA, out.ledPilot) && ok;
  if (!ok) return false;

  bool modeValid = true;
  switch (modeConfig & 0x07) {
    case MASSMORE_MAX3010X_MODE_VAL_HR: out.mode = Mode::HEART_RATE; break;
    case MASSMORE_MAX3010X_MODE_VAL_SPO2: out.mode = Mode::SPO2; break;
    case MASSMORE_MAX3010X_MODE_VAL_MULTI_LED: out.mode = Mode::MULTI_LED; break;
    default: out.mode = _mode; modeValid = false; break; /* MODE 0/1 = "do not use" หลัง POR */
  }
  out.shutdown = (modeConfig & MASSMORE_MAX3010X_BIT_SHUTDOWN) != 0;
  out.adcRange = (AdcRange)((spo2Config >> 5) & 0x03);
  out.sampleRate = (SampleRate)((spo2Config >> 2) & 0x07);
  out.pulseWidth = (PulseWidth)(spo2Config & 0x03);
  uint8_t average = (uint8_t)((fifoConfig >> 5) & 0x07);
  if (average > (uint8_t)SampleAverage::X32) average = (uint8_t)SampleAverage::X32;
  out.sampleAverage = (SampleAverage)average;
  out.fifoRollover = (fifoConfig & MASSMORE_MAX3010X_BIT_ROLLOVER_EN) != 0;
  out.fifoAlmostFull = (uint8_t)(fifoConfig & 0x0F);

  _sampleRate = out.sampleRate;
  _sampleAverage = out.sampleAverage;
  if (modeValid) {
    _mode = out.mode;
    recomputeChannels();
  }
  out.variant = _variant;
  out.activeChannels = _activeChannels;
  out.effectiveRateHz = getEffectiveSampleRate();
  _lastError = ErrorCode::OK;
  return true;
}

/* =========================================================================
   Authenticity (11 checks)
   ========================================================================= */

const char *Massmore_MAX3010x::getVerifyCheckName(uint8_t index) {
  switch (index) {
    case 0: return "ACK at 0x57";
    case 1: return "PART_ID == 0x15";
    case 2: return "REV_ID valid";
    case 3: return "RESET self-clears";
    case 4: return "POR defaults";
    case 5: return "Register R/W";
    case 6: return "PART_ID read-only";
    case 7: return "Reserved bits zero";
    case 8: return "FIFO pointer 5-bit";
    case 9: return "Die temp sane";
    case 10: return "LED/ADC response";
    default: return "?";
  }
}

uint8_t Massmore_MAX3010x::getVerifyPassCount() const {
  uint8_t count = 0;
  for (uint8_t i = 0; i < CHK_COUNT; i++) {
    if (_verifyMask & (1u << i)) count++;
  }
  return count;
}

Massmore_MAX3010x::Genuine Massmore_MAX3010x::verifyChip() {
  _verifyMask = 0;

  /* 1. ACK */
  if (!isConnected()) return Genuine::NOT_MAX3010X;
  _verifyMask |= CHK_ACK;

  /* 2. PART_ID */
  uint8_t partId = 0;
  if (!readRegister8(MASSMORE_MAX3010X_REG_PART_ID, partId)) return Genuine::NOT_MAX3010X;
  if (partId != MASSMORE_MAX3010X_PART_ID_EXPECTED) return Genuine::NOT_MAX3010X;
  _verifyMask |= CHK_PART_ID;

  /* 3. REV_ID ไม่ใช่ 0x00 / 0xFF */
  uint8_t revId = 0;
  if (readRegister8(MASSMORE_MAX3010X_REG_REVISION_ID, revId) && revId != 0x00 && revId != 0xFF) {
    _verifyMask |= CHK_REV_ID;
  }

  /* 4. RESET self-clear */
  const bool resetOk =
      maskRegister8(MASSMORE_MAX3010X_REG_MODE_CONFIG, MASSMORE_MAX3010X_MASK_RESET, MASSMORE_MAX3010X_BIT_RESET) &&
      waitForBit(MASSMORE_MAX3010X_REG_MODE_CONFIG, MASSMORE_MAX3010X_BIT_RESET, false, _timeoutMs);
  if (resetOk) _verifyMask |= CHK_RESET;
  _mode = Mode::HEART_RATE;
  _sampleAverage = SampleAverage::X1;
  _sampleRate = SampleRate::HZ_50;
  recomputeChannels();
  flush();

  /* 5. POR defaults ตามตาราง Register Map ของ datasheet (ทุกตัวเป็น 0x00) */
  if (resetOk) {
    static const uint8_t zeroRegs[] = {
        MASSMORE_MAX3010X_REG_INT_ENABLE_1, MASSMORE_MAX3010X_REG_INT_ENABLE_2, MASSMORE_MAX3010X_REG_FIFO_WR_PTR,
        MASSMORE_MAX3010X_REG_OVF_COUNTER,  MASSMORE_MAX3010X_REG_FIFO_RD_PTR,  MASSMORE_MAX3010X_REG_FIFO_CONFIG,
        MASSMORE_MAX3010X_REG_MODE_CONFIG,  MASSMORE_MAX3010X_REG_SPO2_CONFIG,  MASSMORE_MAX3010X_REG_LED1_PA,
        MASSMORE_MAX3010X_REG_LED2_PA,      MASSMORE_MAX3010X_REG_MULTI_LED_1,  MASSMORE_MAX3010X_REG_MULTI_LED_2};
    bool defaultsOk = true;
    for (uint8_t i = 0; i < sizeof(zeroRegs); i++) {
      uint8_t value = 0xAA;
      if (!readRegister8(zeroRegs[i], value) || value != 0x00) {
        defaultsOk = false;
        break;
      }
    }
    if (defaultsOk) _verifyMask |= CHK_POR_DEFAULT;
  }

  /* 6. R/W pattern บน LED1_PA */
  {
    static const uint8_t patterns[] = {0x55, 0xAA, 0x01, 0xFE};
    bool rwOk = true;
    for (uint8_t i = 0; i < sizeof(patterns) && rwOk; i++) {
      uint8_t readback = 0;
      if (!writeRegister8(MASSMORE_MAX3010X_REG_LED1_PA, patterns[i]) ||
          !readRegister8(MASSMORE_MAX3010X_REG_LED1_PA, readback) || readback != patterns[i]) {
        rwOk = false;
      }
    }
    writeRegister8(MASSMORE_MAX3010X_REG_LED1_PA, 0x00);
    if (rwOk) _verifyMask |= CHK_RW;
  }

  /* 7. PART_ID ต้อง read-only (ของเลียนแบบที่จำลองด้วย MCU มักเขียนทับได้) */
  {
    writeRegister8(MASSMORE_MAX3010X_REG_PART_ID, 0x00);
    uint8_t readback = 0;
    if (readRegister8(MASSMORE_MAX3010X_REG_PART_ID, readback) && readback == MASSMORE_MAX3010X_PART_ID_EXPECTED) {
      _verifyMask |= CHK_READONLY;
    }
  }

  /* 8. Reserved bits 5:3 ของ MODE_CONFIG ต้องอ่านได้ 0 */
  {
    writeRegister8(MASSMORE_MAX3010X_REG_MODE_CONFIG, 0x38);
    uint8_t readback = 0xFF;
    if (readRegister8(MASSMORE_MAX3010X_REG_MODE_CONFIG, readback) && (readback & 0x38) == 0x00) {
      _verifyMask |= CHK_RESERVED;
    }
    writeRegister8(MASSMORE_MAX3010X_REG_MODE_CONFIG, 0x00);
  }

  /* 9. FIFO pointer เป็น 5-bit field: เขียน 0x20 ต้องอ่านกลับ 0x00 */
  {
    bool ptrOk = true;
    uint8_t readback = 0;
    if (!writeRegister8(MASSMORE_MAX3010X_REG_FIFO_WR_PTR, 0x1F) ||
        !readRegister8(MASSMORE_MAX3010X_REG_FIFO_WR_PTR, readback) || readback != 0x1F) {
      ptrOk = false;
    }
    if (ptrOk && (!writeRegister8(MASSMORE_MAX3010X_REG_FIFO_WR_PTR, 0x20) ||
                  !readRegister8(MASSMORE_MAX3010X_REG_FIFO_WR_PTR, readback) || readback != 0x00)) {
      ptrOk = false;
    }
    writeRegister8(MASSMORE_MAX3010X_REG_FIFO_WR_PTR, 0x00);
    if (ptrOk) _verifyMask |= CHK_FIFO_PTR;
  }

  /* 10. Die temperature อยู่ในช่วง operating range (-40..+85 C) */
  {
    const float dieTemp = readTemperature(120);
    if (!isnan(dieTemp) && dieTemp > -40.0f && dieTemp < 85.0f) _verifyMask |= CHK_TEMP;
  }

  /* 11. เปิด LED แล้ว ADC ต้องขยับจริง (พิสูจน์ว่ามี optical front-end) */
  {
    setFifoRollover(true);
    setSampleAverage(SampleAverage::X4);
    setAdcRange(AdcRange::NA_4096);
    setSampleRate(SampleRate::HZ_400);
    setPulseWidth(PulseWidth::US_411);

    setPulseAmplitudeRed(0);
    setPulseAmplitudeIR(0);
    setMode(Mode::SPO2);
    clearFifo();
    delay(60);
    update();
    uint32_t darkMax = 0;
    while (available()) {
      if (getIR() > darkMax) darkMax = getIR();
      if (getRed() > darkMax) darkMax = getRed();
      nextSample();
    }

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
    if (litMax > 0 && litMax > darkMax + 200) _verifyMask |= CHK_LED;

    setPulseAmplitudeRed(0);
    setPulseAmplitudeIR(0);
    setMode(Mode::HEART_RATE);
    clearFifo();
  }

  const uint8_t passCount = getVerifyPassCount();
  if (passCount == CHK_COUNT) return Genuine::PASS;
  if (passCount >= CHK_COUNT - 2) return Genuine::PARTIAL;
  return Genuine::SUSPECT;
}

/* =========================================================================
   Error strings (English — machine-readable Factory Test output)
   ========================================================================= */

const char *Massmore_MAX3010x::errorToString(ErrorCode error) {
  switch (error) {
    case ErrorCode::OK: return "OK";
    case ErrorCode::NOT_FOUND: return "NOT_FOUND";
    case ErrorCode::WRONG_ID: return "WRONG_ID";
    case ErrorCode::TIMEOUT: return "TIMEOUT";
    case ErrorCode::BUS_ERROR: return "BUS_ERROR";
    case ErrorCode::NOT_READY: return "NOT_READY";
    case ErrorCode::NOT_BEGUN: return "NOT_BEGUN";
    case ErrorCode::BAD_ARG: return "BAD_ARG";
    case ErrorCode::UNSUPPORTED: return "UNSUPPORTED";
    default: return "UNKNOWN";
  }
}

/* =========================================================================
   BeatDetector
   ========================================================================= */

Massmore_MAX3010x::BeatDetector::BeatDetector() {
  _fingerThreshold = MASSMORE_MAX3010X_FINGER_THRESHOLD;
  _minBpm = 30.0f;
  _maxBpm = 220.0f;
  begin(50.0f);
}

void Massmore_MAX3010x::BeatDetector::begin(float sampleRateHz) {
  if (sampleRateHz < 10.0f) sampleRateHz = 10.0f;
  _sampleRate = sampleRateHz;
  /* one-pole filter: alpha = 1 - exp(-2*pi*fc/fs) */
  _dcAlpha = 1.0f - expf(-MAX3010X_TWO_PI * MAX3010X_ALGO_HIGHPASS_HZ / _sampleRate);
  _lpAlpha = 1.0f - expf(-MAX3010X_TWO_PI * MAX3010X_ALGO_LOWPASS_HZ / _sampleRate);
  _envelopeDecay = 1.0f / (2.0f * _sampleRate); /* envelope คลายตัวใน ~2 s */
  _warmupSamples = (uint32_t)(_sampleRate * 1.0f);
  reset();
}

void Massmore_MAX3010x::BeatDetector::reset() {
  _dc = _filtered = _envelopeHigh = _envelopeLow = 0.0f;
  _aboveThreshold = false;
  _primed = false;
  _sampleIndex = _lastBeatIndex = 0;
  _bpm = _bpmAverage = 0.0f;
  _bpmHistoryCount = _bpmHistoryIndex = 0;
  _beatCount = 0;
  for (uint8_t i = 0; i < MASSMORE_MAX3010X_BPM_AVERAGE_COUNT; i++) _bpmHistory[i] = 0.0f;
}

void Massmore_MAX3010x::BeatDetector::setBpmRange(float minBpm, float maxBpm) {
  if (minBpm < 20.0f) minBpm = 20.0f;
  if (maxBpm > 300.0f) maxBpm = 300.0f;
  if (maxBpm <= minBpm) return;
  _minBpm = minBpm;
  _maxBpm = maxBpm;
}

bool Massmore_MAX3010x::BeatDetector::check(uint32_t sample) {
  const float value = (float)sample;
  _sampleIndex++;
  if (_sampleIndex == 1) {
    _dc = value; /* seed filter ด้วยค่าแรก จะได้ไม่ต้องไต่จากศูนย์ */
    _filtered = _envelopeHigh = _envelopeLow = 0.0f;
    return false;
  }
  _dc += (value - _dc) * _dcAlpha;          /* high-pass: ตัด DC */
  const float ac = value - _dc;
  _filtered += (ac - _filtered) * _lpAlpha; /* low-pass: ตัด noise */

  if (_filtered > _envelopeHigh) _envelopeHigh = _filtered;
  else _envelopeHigh -= (_envelopeHigh - _envelopeLow) * _envelopeDecay;
  if (_filtered < _envelopeLow) _envelopeLow = _filtered;
  else _envelopeLow += (_envelopeHigh - _envelopeLow) * _envelopeDecay;

  if (_sampleIndex < _warmupSamples) return false;
  _primed = true;

  if (!isFingerPresent()) {
    _aboveThreshold = false;
    return false;
  }
  const float amplitude = _envelopeHigh - _envelopeLow;
  if (amplitude < MAX3010X_ALGO_MIN_AMPLITUDE) {
    _aboveThreshold = false;
    return false;
  }
  const float riseLevel = _envelopeLow + amplitude * MAX3010X_ALGO_THRESHOLD_RISE;
  const float fallLevel = _envelopeLow + amplitude * MAX3010X_ALGO_THRESHOLD_FALL;

  if (!_aboveThreshold && _filtered > riseLevel) {
    _aboveThreshold = true;
    const uint32_t delta = _sampleIndex - _lastBeatIndex;
    const uint32_t refractory = (uint32_t)(_sampleRate * 60.0f / _maxBpm);
    if (_lastBeatIndex != 0 && delta >= refractory) {
      const float bpm = 60.0f * _sampleRate / (float)delta;
      if (bpm >= _minBpm && bpm <= _maxBpm) {
        _bpm = bpm;
        _bpmHistory[_bpmHistoryIndex] = bpm;
        _bpmHistoryIndex = (uint8_t)((_bpmHistoryIndex + 1) % MASSMORE_MAX3010X_BPM_AVERAGE_COUNT);
        if (_bpmHistoryCount < MASSMORE_MAX3010X_BPM_AVERAGE_COUNT) _bpmHistoryCount++;
        float sum = 0.0f;
        for (uint8_t i = 0; i < _bpmHistoryCount; i++) sum += _bpmHistory[i];
        _bpmAverage = sum / (float)_bpmHistoryCount;
        _beatCount++;
        _lastBeatIndex = _sampleIndex;
        return true;
      }
    }
    _lastBeatIndex = _sampleIndex;
    return false;
  }
  if (_aboveThreshold && _filtered < fallLevel) _aboveThreshold = false;
  return false;
}

/* =========================================================================
   Oximeter (streaming ratio-of-ratios)
   ========================================================================= */

Massmore_MAX3010x::Oximeter::Oximeter() {
  _calA = -45.060f;
  _calB = 30.354f;
  _calC = 94.845f;
  _fingerThreshold = MASSMORE_MAX3010X_FINGER_THRESHOLD;
  begin(50.0f, 25);
}

void Massmore_MAX3010x::Oximeter::begin(float sampleRateHz, uint16_t updateEverySamples) {
  if (sampleRateHz < 10.0f) sampleRateHz = 10.0f;
  _sampleRate = sampleRateHz;
  _updateEvery = (updateEverySamples == 0) ? 1 : updateEverySamples;
  _dcAlpha = 1.0f - expf(-MAX3010X_TWO_PI * MAX3010X_ALGO_HIGHPASS_HZ / _sampleRate);
  _lpAlpha = 1.0f - expf(-MAX3010X_TWO_PI * MAX3010X_ALGO_LOWPASS_HZ / _sampleRate);
  _rmsAlpha = 1.0f - expf(-MAX3010X_TWO_PI * MAX3010X_ALGO_RMS_HZ / _sampleRate);
  _warmupSamples = (uint32_t)(_sampleRate * 4.0f); /* 4 s ให้ RMS average เข้าที่ */
  _beat.begin(sampleRateHz);
  _beat.setFingerThreshold(_fingerThreshold);
  reset();
}

void Massmore_MAX3010x::Oximeter::reset() {
  _dcRed = _dcIr = _lpRed = _lpIr = _msRed = _msIr = 0.0f;
  _samples = 0;
  _sinceUpdate = 0;
  _beat.reset();
  _result.spo2 = _result.heartRate = _result.ratio = 0.0f;
  _result.perfusionIr = _result.perfusionRed = _result.dcIr = _result.dcRed = 0.0f;
  _result.spo2Valid = _result.heartRateValid = _result.fingerPresent = false;
}

void Massmore_MAX3010x::Oximeter::setCalibration(float a, float b, float c) {
  _calA = a;
  _calB = b;
  _calC = c;
}

void Massmore_MAX3010x::Oximeter::setFingerThreshold(uint32_t threshold) {
  _fingerThreshold = threshold;
  _beat.setFingerThreshold(threshold);
}

float Massmore_MAX3010x::Oximeter::getFillRatio() const {
  if (_warmupSamples == 0) return 1.0f;
  return (_samples >= _warmupSamples) ? 1.0f : (float)_samples / (float)_warmupSamples;
}

bool Massmore_MAX3010x::Oximeter::add(uint32_t red, uint32_t ir) {
  const float r = (float)red, i = (float)ir;
  _samples++;
  if (_samples == 1) {
    _dcRed = r;
    _dcIr = i;
  } else {
    _dcRed += (r - _dcRed) * _dcAlpha;
    _dcIr += (i - _dcIr) * _dcAlpha;
    _lpRed += ((r - _dcRed) - _lpRed) * _lpAlpha;
    _lpIr += ((i - _dcIr) - _lpIr) * _lpAlpha;
    _msRed += (_lpRed * _lpRed - _msRed) * _rmsAlpha; /* mean-square = RMS^2 */
    _msIr += (_lpIr * _lpIr - _msIr) * _rmsAlpha;
  }
  _beat.check(ir);

  _sinceUpdate++;
  if (_samples < _warmupSamples) return false;
  if (_sinceUpdate < _updateEvery) return false;
  _sinceUpdate = 0;
  compute();
  return true;
}

void Massmore_MAX3010x::Oximeter::compute() {
  const float dcRed = _dcRed, dcIr = _dcIr;
  const float acRed = sqrtf(_msRed > 0.0f ? _msRed : 0.0f);
  const float acIr = sqrtf(_msIr > 0.0f ? _msIr : 0.0f);

  _result.dcRed = dcRed;
  _result.dcIr = dcIr;
  _result.fingerPresent = (dcIr >= (float)_fingerThreshold);

  /* RMS → peak-to-peak โดยประมาณ (x 2*sqrt(2)) เพื่อให้ Perfusion Index เป็นหน่วยสากล */
  const float ppRed = acRed * 2.828427f, ppIr = acIr * 2.828427f;
  _result.perfusionRed = (dcRed > 1.0f) ? (ppRed / dcRed * 100.0f) : 0.0f;
  _result.perfusionIr = (dcIr > 1.0f) ? (ppIr / dcIr * 100.0f) : 0.0f;

  _result.heartRate = _beat.getAverageBeatsPerMinute();
  _result.heartRateValid = _result.fingerPresent && _beat.getBeatCount() >= 3 && _result.heartRate > 0.0f;

  _result.spo2Valid = false;
  _result.ratio = 0.0f;
  if (!_result.fingerPresent || dcRed < 1000.0f || dcIr < 1000.0f || acIr < 1.0f) {
    _result.spo2 = 0.0f;
    return;
  }
  const float ratio = (acRed / dcRed) / (acIr / dcIr);
  _result.ratio = ratio;
  if (ratio < 0.3f || ratio > 3.0f) { /* นอกช่วงที่เป็นไปได้ทางสรีรวิทยา */
    _result.spo2 = 0.0f;
    return;
  }
  if (_result.perfusionIr < 0.05f) { /* ชีพจรจมใน noise */
    _result.spo2 = 0.0f;
    return;
  }
  float spo2 = _calA * ratio * ratio + _calB * ratio + _calC;
  if (spo2 > 100.0f) spo2 = 100.0f;
  if (spo2 < 70.0f) spo2 = 70.0f;
  _result.spo2 = spo2;
  _result.spo2Valid = true;
}
