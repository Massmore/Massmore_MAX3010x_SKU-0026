/*!
 * @file massmore_max3010x_host_shim.cpp
 * @brief การทำงานของตัวจำลอง Arduino และชิป MAX30102
 *
 * @copyright Copyright (c) 2026 Massmore Biz Co., Ltd.
 * @license MIT
 */

#include "massmore_max3010x_host_shim.h"

#include "../lib/Massmore_MAX3010x/src/Massmore_MAX3010x_Registers.h"

/* ---------- เวลาจำลอง ---------- */

static uint32_t g_millis = 0;

uint32_t millis() { return g_millis; }
uint32_t micros() { return g_millis * 1000UL; }

void delay(uint32_t ms) {
  g_millis += ms;
  g_mockChip.tick(ms);
}

void hostAdvanceTime(uint32_t ms) { delay(ms); }

void hostResetTime() { g_millis = 0; }

/* ---------- ตัวจำลองชิป ---------- */

MockMax3010x g_mockChip;

MockMax3010x::MockMax3010x() {
  present = true;
  partId = MASSMORE_MAX3010X_PART_ID_EXPECTED;
  revisionId = 0x03;
  partIdWritable = false;
  reservedBitsStick = false;
  pointerFullByte = false;
  hasLed3 = false;
  hasLed4 = false;
  hasProximity = false;
  dieTemperature = 28.5f;
  writeCount = 0;
  readCount = 0;
  reset();
}

void MockMax3010x::reset() {
  memset(_regs, 0, sizeof(_regs));
  memset(fifoRed, 0, sizeof(fifoRed));
  memset(fifoIr, 0, sizeof(fifoIr));
  memset(fifoGreen, 0, sizeof(fifoGreen));
  _prox = 0;
  _sampleCounter = 0;
  _sampleAccumulator = 0.0f;
  _fifoByteIndex = 0;
  _tempPending = false;
  writePointer = 0;
  readPointer = 0;
  overflowCounter = 0;
  fifoCount = 0;
  /* หลัง power-on-reset ชิปตั้งบิต PWR_RDY ไว้ในรีจิสเตอร์สถานะ */
  _regs[MASSMORE_MAX3010X_REG_INT_STATUS_1] = MASSMORE_MAX3010X_INT_PWR_RDY;
}

uint8_t MockMax3010x::activeChannels() const {
  const uint8_t mode = _regs[MASSMORE_MAX3010X_REG_MODE_CONFIG] & 0x07;
  if (mode == MASSMORE_MAX3010X_MODE_VAL_HR) {
    return 1;
  }
  if (mode == MASSMORE_MAX3010X_MODE_VAL_SPO2) {
    return 2;
  }
  if (mode == MASSMORE_MAX3010X_MODE_VAL_MULTI_LED) {
    uint8_t slots = 0;
    const uint8_t r1 = _regs[MASSMORE_MAX3010X_REG_MULTI_LED_1];
    const uint8_t r2 = _regs[MASSMORE_MAX3010X_REG_MULTI_LED_2];
    if (r1 & 0x07) slots++;
    if ((r1 >> 4) & 0x07) slots++;
    if (r2 & 0x07) slots++;
    if ((r2 >> 4) & 0x07) slots++;
    return slots ? slots : 1;
  }
  return 0; /* โหมด 0 = ชิปยังไม่ทำงาน */
}

float MockMax3010x::effectiveRate() const {
  static const uint16_t rates[8] = {50, 100, 200, 400, 800, 1000, 1600, 3200};
  static const uint8_t averages[8] = {1, 2, 4, 8, 16, 32, 32, 32};
  const uint8_t spo2 = _regs[MASSMORE_MAX3010X_REG_SPO2_CONFIG];
  const uint8_t fifo = _regs[MASSMORE_MAX3010X_REG_FIFO_CONFIG];
  return (float)rates[(spo2 >> 2) & 0x07] / (float)averages[(fifo >> 5) & 0x07];
}

void MockMax3010x::pushSample() {
  /* ค่าที่ผลิตออกมาแปรผันตามกระแส LED ที่ตั้งไว้ เพื่อให้ทดสอบเรื่อง
     การตอบสนองของ ADC ต่อ LED ได้ บวกค่าฐานเล็กน้อยแทนแสงรั่ว */
  const uint32_t base = 60;
  const uint32_t red = base + (uint32_t)_regs[MASSMORE_MAX3010X_REG_LED1_PA] * 300UL;
  const uint32_t ir = base + (uint32_t)_regs[MASSMORE_MAX3010X_REG_LED2_PA] * 300UL;
  const uint32_t green = base + (uint32_t)_regs[MASSMORE_MAX3010X_REG_LED3_PA] * 300UL;

  const bool rollover =
      (_regs[MASSMORE_MAX3010X_REG_FIFO_CONFIG] & MASSMORE_MAX3010X_BIT_ROLLOVER_EN) != 0;

  if (fifoCount >= MASSMORE_MAX3010X_FIFO_DEPTH) {
    /* FIFO เต็มครบ 32 ตัวแล้ว ตัวชี้สองตัวชี้ที่เดียวกัน */
    if (overflowCounter < 0x1F) {
      overflowCounter++;
    }
    if (!rollover) {
      return; /* ทิ้งตัวใหม่ เก็บของเก่าไว้ */
    }
    /* วนทับ ทิ้งตัวที่เก่าที่สุด */
    readPointer = (uint8_t)((readPointer + 1) & 0x1F);
    fifoCount--;
  }

  fifoRed[writePointer] = red & MASSMORE_MAX3010X_DATA_MASK;
  fifoIr[writePointer] = ir & MASSMORE_MAX3010X_DATA_MASK;
  fifoGreen[writePointer] = green & MASSMORE_MAX3010X_DATA_MASK;
  writePointer = (uint8_t)((writePointer + 1) & 0x1F);
  fifoCount++;
  _sampleCounter++;

  _regs[MASSMORE_MAX3010X_REG_INT_STATUS_1] |= MASSMORE_MAX3010X_INT_PPG_RDY;

  const uint8_t almostFull = (uint8_t)(_regs[MASSMORE_MAX3010X_REG_FIFO_CONFIG] & 0x0F);
  if (fifoCount >= (32 - almostFull)) {
    _regs[MASSMORE_MAX3010X_REG_INT_STATUS_1] |= MASSMORE_MAX3010X_INT_A_FULL;
  }
}

void MockMax3010x::tick(uint32_t ms) {
  if (activeChannels() == 0) {
    return; /* ชิปยังไม่ได้ตั้งโหมด จึงไม่ผลิตข้อมูล */
  }
  if (_regs[MASSMORE_MAX3010X_REG_MODE_CONFIG] & MASSMORE_MAX3010X_BIT_SHUTDOWN) {
    return; /* หลับอยู่ */
  }

  _sampleAccumulator += effectiveRate() * (float)ms / 1000.0f;
  while (_sampleAccumulator >= 1.0f) {
    _sampleAccumulator -= 1.0f;
    pushSample();
  }
}

uint8_t MockMax3010x::readRegister(uint8_t reg) {
  readCount++;

  if (reg == MASSMORE_MAX3010X_REG_PART_ID) {
    return partId;
  }
  if (reg == MASSMORE_MAX3010X_REG_REVISION_ID) {
    return revisionId;
  }
  if (reg == MASSMORE_MAX3010X_REG_PROX_INT_THRESH) {
    return hasProximity ? _prox : 0x00;
  }
  if (reg == MASSMORE_MAX3010X_REG_LED3_PA) {
    return hasLed3 ? _regs[reg] : 0x00;
  }
  if (reg == MASSMORE_MAX3010X_REG_LED4_PA) {
    return hasLed4 ? _regs[reg] : 0x00;
  }

  if (reg == MASSMORE_MAX3010X_REG_FIFO_WR_PTR) {
    return writePointer;
  }
  if (reg == MASSMORE_MAX3010X_REG_FIFO_RD_PTR) {
    return readPointer;
  }
  if (reg == MASSMORE_MAX3010X_REG_OVF_COUNTER) {
    return overflowCounter;
  }

  if (reg == MASSMORE_MAX3010X_REG_FIFO_DATA) {
    const uint8_t channels = activeChannels();
    if (channels == 0) {
      return 0;
    }
    if (fifoCount == 0) {
      return 0;
    }
    const uint32_t *sources[4] = {fifoRed, fifoIr, fifoGreen, fifoGreen};
    const uint8_t channel = (uint8_t)(_fifoByteIndex / 3);
    const uint8_t byteInChannel = (uint8_t)(_fifoByteIndex % 3);
    const uint32_t value = sources[channel < 4 ? channel : 3][readPointer];
    const uint8_t out = (uint8_t)((value >> (8 * (2 - byteInChannel))) & 0xFF);

    _fifoByteIndex++;
    if (_fifoByteIndex >= channels * 3) {
      _fifoByteIndex = 0;
      readPointer = (uint8_t)((readPointer + 1) & 0x1F);
      fifoCount--;
    }
    return out;
  }

  if (reg == MASSMORE_MAX3010X_REG_DIE_TEMP_INT) {
    const int32_t whole = (int32_t)(dieTemperature < 0.0f
                                        ? -(int32_t)(-dieTemperature)
                                        : (int32_t)dieTemperature);
    return (uint8_t)(int8_t)whole;
  }
  if (reg == MASSMORE_MAX3010X_REG_DIE_TEMP_FRAC) {
    float whole = (float)(int32_t)dieTemperature;
    float fraction = dieTemperature - whole;
    if (fraction < 0.0f) {
      fraction += 1.0f;
    }
    return (uint8_t)((int)(fraction / 0.0625f) & 0x0F);
  }
  if (reg == MASSMORE_MAX3010X_REG_DIE_TEMP_CONFIG) {
    /* บิต TEMP_EN เคลียร์ตัวเองเมื่อถูกอ่านครั้งถัดไป */
    const uint8_t value = _regs[reg];
    if (_tempPending) {
      _tempPending = false;
      _regs[reg] = 0x00;
      _regs[MASSMORE_MAX3010X_REG_INT_STATUS_2] |= MASSMORE_MAX3010X_INT_DIE_TEMP_RDY;
    }
    return value;
  }

  if (reg == MASSMORE_MAX3010X_REG_INT_STATUS_1 ||
      reg == MASSMORE_MAX3010X_REG_INT_STATUS_2) {
    const uint8_t value = _regs[reg];
    _regs[reg] = 0x00; /* อ่านแล้วเคลียร์ ตามที่ datasheet ระบุ */
    return value;
  }

  if (reg == MASSMORE_MAX3010X_REG_MODE_CONFIG) {
    uint8_t value = _regs[reg];
    if (!reservedBitsStick) {
      value &= (uint8_t)~0x38; /* บิตสงวน 5:3 อ่านกลับเป็นศูนย์เสมอ */
    }
    /* บิต RESET เคลียร์ตัวเองทันทีที่การรีเซ็ตเสร็จ */
    if (value & MASSMORE_MAX3010X_BIT_RESET) {
      _regs[reg] &= (uint8_t)~MASSMORE_MAX3010X_BIT_RESET;
      value &= (uint8_t)~MASSMORE_MAX3010X_BIT_RESET;
    }
    return value;
  }

  if (reg < 0x40) {
    return _regs[reg];
  }
  return 0;
}

void MockMax3010x::writeRegister(uint8_t reg, uint8_t value) {
  writeCount++;

  if (reg == MASSMORE_MAX3010X_REG_PART_ID) {
    if (partIdWritable) {
      partId = value;
    }
    return;
  }
  if (reg == MASSMORE_MAX3010X_REG_REVISION_ID) {
    return; /* อ่านอย่างเดียว */
  }
  if (reg == MASSMORE_MAX3010X_REG_PROX_INT_THRESH) {
    if (hasProximity) {
      _prox = value;
    }
    return;
  }
  if (reg == MASSMORE_MAX3010X_REG_LED3_PA) {
    if (hasLed3) {
      _regs[reg] = value;
    }
    return;
  }
  if (reg == MASSMORE_MAX3010X_REG_LED4_PA) {
    if (hasLed4) {
      _regs[reg] = value;
    }
    return;
  }

  if (reg == MASSMORE_MAX3010X_REG_FIFO_WR_PTR) {
    writePointer = pointerFullByte ? value : (uint8_t)(value & 0x1F);
    _fifoByteIndex = 0;
    fifoCount = (uint8_t)((writePointer - readPointer) & 0x1F);
    return;
  }
  if (reg == MASSMORE_MAX3010X_REG_FIFO_RD_PTR) {
    readPointer = pointerFullByte ? value : (uint8_t)(value & 0x1F);
    _fifoByteIndex = 0;
    fifoCount = (uint8_t)((writePointer - readPointer) & 0x1F);
    return;
  }
  if (reg == MASSMORE_MAX3010X_REG_OVF_COUNTER) {
    overflowCounter = (uint8_t)(value & 0x1F);
    return;
  }

  if (reg == MASSMORE_MAX3010X_REG_DIE_TEMP_CONFIG) {
    _regs[reg] = (uint8_t)(value & MASSMORE_MAX3010X_BIT_TEMP_EN);
    if (value & MASSMORE_MAX3010X_BIT_TEMP_EN) {
      _tempPending = true;
    }
    return;
  }

  if (reg == MASSMORE_MAX3010X_REG_MODE_CONFIG) {
    if (value & MASSMORE_MAX3010X_BIT_RESET) {
      const uint8_t savedStatus = 0;
      reset();
      _regs[MASSMORE_MAX3010X_REG_INT_STATUS_1] = savedStatus;
      /* เก็บบิต RESET ไว้ให้อ่านเจอครั้งเดียว แล้วจะเคลียร์ตอนอ่าน */
      _regs[reg] = MASSMORE_MAX3010X_BIT_RESET;
      return;
    }
    _regs[reg] = value;
    _sampleAccumulator = 0.0f;
    _fifoByteIndex = 0;
    return;
  }

  if (reg < 0x40) {
    _regs[reg] = value;
  }
}

/* ---------- ตัวจำลองบัส I2C ---------- */

TwoWire Wire;

TwoWire::TwoWire()
    : clockHz(100000),
      _address(0),
      _outLength(0),
      _inLength(0),
      _inIndex(0),
      _pendingRegister(0) {}

void TwoWire::begin() {}
void TwoWire::begin(int sda, int scl, uint32_t frequency) {
  (void)sda;
  (void)scl;
  clockHz = frequency;
}
void TwoWire::setClock(uint32_t frequency) { clockHz = frequency; }

void TwoWire::beginTransmission(uint8_t address) {
  _address = address;
  _outLength = 0;
}

size_t TwoWire::write(uint8_t value) {
  if (_outLength < sizeof(_outBuffer)) {
    _outBuffer[_outLength++] = value;
  }
  return 1;
}

uint8_t TwoWire::endTransmission(bool stop) {
  (void)stop;
  return endTransmission();
}

uint8_t TwoWire::endTransmission() {
  if (_address != MASSMORE_MAX3010X_I2C_ADDRESS || !g_mockChip.present) {
    return 2; /* ไม่มีใคร ACK */
  }
  if (_outLength == 1) {
    _pendingRegister = _outBuffer[0];
  } else if (_outLength >= 2) {
    _pendingRegister = _outBuffer[0];
    for (uint8_t i = 1; i < _outLength; i++) {
      g_mockChip.writeRegister(_outBuffer[0], _outBuffer[i]);
    }
  }
  _outLength = 0;
  return 0;
}

uint8_t TwoWire::requestFrom(uint8_t address, uint8_t quantity) {
  if (address != MASSMORE_MAX3010X_I2C_ADDRESS || !g_mockChip.present) {
    _inLength = 0;
    _inIndex = 0;
    return 0;
  }
  if (quantity > sizeof(_inBuffer)) {
    quantity = sizeof(_inBuffer);
  }
  for (uint8_t i = 0; i < quantity; i++) {
    _inBuffer[i] = g_mockChip.readRegister(_pendingRegister);
  }
  _inLength = quantity;
  _inIndex = 0;
  return quantity;
}

int TwoWire::available() { return _inLength - _inIndex; }

int TwoWire::read() {
  if (_inIndex >= _inLength) {
    return -1;
  }
  return _inBuffer[_inIndex++];
}
