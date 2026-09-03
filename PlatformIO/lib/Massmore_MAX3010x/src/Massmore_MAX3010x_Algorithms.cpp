/*!
 * @file Massmore_MAX3010x_Algorithms.cpp
 * @brief การทำงานภายในของอัลกอริทึมจับชีพจรและคำนวณ SpO2
 *
 * @copyright Copyright (c) 2026 Massmore Biz Co., Ltd.
 * @license MIT
 */

#include "Massmore_MAX3010x_Algorithms.h"

#include <math.h>

/* ความถี่ตัดของตัวกรองผ่านสูงที่ใช้ลบองค์ประกอบ DC ออก หน่วย Hz
   0.5 Hz = 30 ครั้งต่อนาที ต่ำกว่านี้ถือเป็นการเลื่อนของฐานสัญญาณ ไม่ใช่ชีพจร */
#define ALGO_HIGHPASS_HZ 0.5f

/* ความถี่ตัดของตัวกรองผ่านต่ำ 4 Hz = 240 ครั้งต่อนาที
   สูงกว่านี้เป็นสัญญาณรบกวนจากไฟบ้านและการขยับนิ้ว */
#define ALGO_LOWPASS_HZ 4.0f

/* เศษส่วนของแอมพลิจูดที่ใช้เป็นเส้นเกณฑ์ขาขึ้นและขาลง
   ต่างกันเพื่อสร้างฮิสเทอรีซิส กันการนับซ้ำตอนสัญญาณสั่นรอบเส้นเกณฑ์ */
#define ALGO_THRESHOLD_RISE 0.62f
#define ALGO_THRESHOLD_FALL 0.38f

/* แอมพลิจูดต่ำสุดที่ยอมรับว่าเป็นคลื่นชีพจรจริง ไม่ใช่สัญญาณรบกวน */
#define ALGO_MIN_AMPLITUDE 25.0f

/* =========================================================================
   ตัวจับจังหวะการเต้นของหัวใจ
   ========================================================================= */

MassmoreMAX3010xBeatDetector::MassmoreMAX3010xBeatDetector() {
  _fingerThreshold = MASSMORE_MAX3010X_FINGER_THRESHOLD;
  _minBpm = 30.0f;
  _maxBpm = 220.0f;
  begin(50.0f);
}

void MassmoreMAX3010xBeatDetector::begin(float sampleRateHz) {
  if (sampleRateHz < 10.0f) {
    sampleRateHz = 10.0f;
  }
  _sampleRate = sampleRateHz;

  /* ค่าคงที่ของตัวกรองขั้วเดียว  alpha = 1 - exp(-2 pi fc / fs)
     ยิ่ง alpha น้อย ตัวกรองยิ่งช้าและนิ่ง */
  _dcAlpha = 1.0f - expf(-2.0f * 3.14159265f * ALGO_HIGHPASS_HZ / _sampleRate);
  _lpAlpha = 1.0f - expf(-2.0f * 3.14159265f * ALGO_LOWPASS_HZ / _sampleRate);

  /* ยอดและท้องคลื่นคลายตัวเข้าหากันจนหมดในเวลาประมาณ 2 วินาที
     ทำให้เกณฑ์ปรับตามคนที่สัญญาณแรงหรืออ่อนได้เอง */
  _envelopeDecay = 1.0f / (2.0f * _sampleRate);

  /* อุ่นเครื่องหนึ่งวินาทีก่อนเริ่มนับ ระหว่างนี้ตัวกรองกำลังเข้าที่ */
  _warmupSamples = (uint32_t)(_sampleRate * 1.0f);

  reset();
}

void MassmoreMAX3010xBeatDetector::reset() {
  _dc = 0.0f;
  _filtered = 0.0f;
  _envelopeHigh = 0.0f;
  _envelopeLow = 0.0f;
  _aboveThreshold = false;
  _primed = false;
  _sampleIndex = 0;
  _lastBeatIndex = 0;
  _bpm = 0.0f;
  _bpmAverage = 0.0f;
  _bpmHistoryCount = 0;
  _bpmHistoryIndex = 0;
  _beatCount = 0;
  for (uint8_t i = 0; i < MASSMORE_MAX3010X_BPM_AVERAGE_COUNT; i++) {
    _bpmHistory[i] = 0.0f;
  }
}

void MassmoreMAX3010xBeatDetector::setBpmRange(float minBpm, float maxBpm) {
  if (minBpm < 20.0f) minBpm = 20.0f;
  if (maxBpm > 300.0f) maxBpm = 300.0f;
  if (maxBpm <= minBpm) return;
  _minBpm = minBpm;
  _maxBpm = maxBpm;
}

bool MassmoreMAX3010xBeatDetector::check(uint32_t sample) {
  const float value = (float)sample;
  _sampleIndex++;

  /* ตัวอย่างแรกสุด ตั้งค่าเริ่มต้นให้ตัวกรองเท่ากับสัญญาณเลย
     จะได้ไม่ต้องรอให้ค่าไต่ขึ้นมาจากศูนย์ */
  if (_sampleIndex == 1) {
    _dc = value;
    _filtered = 0.0f;
    _envelopeHigh = 0.0f;
    _envelopeLow = 0.0f;
    return false;
  }

  /* ขั้นที่ 1  ตัด DC ทิ้ง */
  _dc += (value - _dc) * _dcAlpha;
  const float ac = value - _dc;

  /* ขั้นที่ 2  กรองความถี่สูงทิ้ง */
  _filtered += (ac - _filtered) * _lpAlpha;

  /* ขั้นที่ 3  ติดตามยอดและท้องคลื่นแบบปรับตัวเอง */
  if (_filtered > _envelopeHigh) {
    _envelopeHigh = _filtered;
  } else {
    _envelopeHigh -= (_envelopeHigh - _envelopeLow) * _envelopeDecay;
  }
  if (_filtered < _envelopeLow) {
    _envelopeLow = _filtered;
  } else {
    _envelopeLow += (_envelopeHigh - _envelopeLow) * _envelopeDecay;
  }

  if (_sampleIndex < _warmupSamples) {
    return false;
  }
  _primed = true;

  /* ไม่มีนิ้ววางอยู่ ก็ไม่มีอะไรให้จับ */
  if (!isFingerPresent()) {
    _aboveThreshold = false;
    return false;
  }

  const float amplitude = _envelopeHigh - _envelopeLow;
  if (amplitude < ALGO_MIN_AMPLITUDE) {
    /* สัญญาณแบนเกินกว่าจะเป็นชีพจร เช่น วางนิ้วหลวมหรือขยับอยู่ */
    _aboveThreshold = false;
    return false;
  }

  const float riseLevel = _envelopeLow + amplitude * ALGO_THRESHOLD_RISE;
  const float fallLevel = _envelopeLow + amplitude * ALGO_THRESHOLD_FALL;

  /* ขั้นที่ 4  นับเมื่อสัญญาณตัดเส้นเกณฑ์ขาขึ้น
     หมายเหตุ  ไม่สำคัญว่าคลื่นชีพจรจะขึ้นหรือลงตอนหัวใจบีบ เพราะเรานับ
     "ช่วงห่างระหว่างการตัดเส้นแบบเดียวกันสองครั้ง" ซึ่งเท่ากับคาบการเต้นเสมอ */
  if (!_aboveThreshold && _filtered > riseLevel) {
    _aboveThreshold = true;

    const uint32_t deltaSamples = _sampleIndex - _lastBeatIndex;
    /* ช่วงห้ามนับซ้ำ คิดจากอัตราการเต้นสูงสุดที่ยอมรับ */
    const uint32_t refractory = (uint32_t)(_sampleRate * 60.0f / _maxBpm);

    if (_lastBeatIndex != 0 && deltaSamples >= refractory) {
      const float bpm = 60.0f * _sampleRate / (float)deltaSamples;
      if (bpm >= _minBpm && bpm <= _maxBpm) {
        _bpm = bpm;
        _bpmHistory[_bpmHistoryIndex] = bpm;
        _bpmHistoryIndex =
            (uint8_t)((_bpmHistoryIndex + 1) % MASSMORE_MAX3010X_BPM_AVERAGE_COUNT);
        if (_bpmHistoryCount < MASSMORE_MAX3010X_BPM_AVERAGE_COUNT) {
          _bpmHistoryCount++;
        }
        float sum = 0.0f;
        for (uint8_t i = 0; i < _bpmHistoryCount; i++) {
          sum += _bpmHistory[i];
        }
        _bpmAverage = sum / (float)_bpmHistoryCount;
        _beatCount++;
        _lastBeatIndex = _sampleIndex;
        return true;
      }
    }
    /* ครั้งแรกสุด หรือครั้งที่ค่าหลุดช่วง ใช้เป็นจุดอ้างอิงของครั้งถัดไป */
    _lastBeatIndex = _sampleIndex;
    return false;
  }

  if (_aboveThreshold && _filtered < fallLevel) {
    _aboveThreshold = false;
  }

  return false;
}

/* =========================================================================
   ตัวคำนวณ SpO2
   ========================================================================= */

MassmoreMAX3010xSpO2::MassmoreMAX3010xSpO2() {
  /* เส้นโค้งสอบเทียบมาตรฐานที่ใช้กันทั่วไปในงาน pulse oximetry */
  _calA = -45.060f;
  _calB = 30.354f;
  _calC = 94.845f;
  _fingerThreshold = MASSMORE_MAX3010X_FINGER_THRESHOLD;
  begin(50.0f, 25);
}

void MassmoreMAX3010xSpO2::begin(float sampleRateHz, uint16_t updateEverySamples) {
  if (sampleRateHz < 10.0f) {
    sampleRateHz = 10.0f;
  }
  _sampleRate = sampleRateHz;
  _updateEvery = (updateEverySamples == 0) ? 1 : updateEverySamples;
  _beat.begin(sampleRateHz);
  _beat.setFingerThreshold(_fingerThreshold);
  reset();
}

void MassmoreMAX3010xSpO2::reset() {
  _index = 0;
  _filled = 0;
  _sinceUpdate = 0;
  _beat.reset();

  _result.spo2 = 0.0f;
  _result.heartRate = 0.0f;
  _result.ratio = 0.0f;
  _result.perfusionIr = 0.0f;
  _result.perfusionRed = 0.0f;
  _result.dcIr = 0.0f;
  _result.dcRed = 0.0f;
  _result.spo2Valid = false;
  _result.heartRateValid = false;
  _result.fingerPresent = false;
}

void MassmoreMAX3010xSpO2::setCalibration(float a, float b, float c) {
  _calA = a;
  _calB = b;
  _calC = c;
}

void MassmoreMAX3010xSpO2::setFingerThreshold(uint32_t threshold) {
  _fingerThreshold = threshold;
  _beat.setFingerThreshold(threshold);
}

float MassmoreMAX3010xSpO2::getFillRatio() const {
  return (float)_filled / (float)MASSMORE_MAX3010X_SPO2_WINDOW;
}

bool MassmoreMAX3010xSpO2::add(uint32_t red, uint32_t ir) {
  _red[_index] = red;
  _ir[_index] = ir;
  _index = (uint16_t)((_index + 1) % MASSMORE_MAX3010X_SPO2_WINDOW);
  if (_filled < MASSMORE_MAX3010X_SPO2_WINDOW) {
    _filled++;
  }

  /* ป้อนให้ตัวจับจังหวะไปพร้อมกัน จะได้อัตราการเต้นมาใช้ในผลชุดเดียวกัน */
  _beat.check(ir);

  _sinceUpdate++;
  if (_filled < MASSMORE_MAX3010X_SPO2_WINDOW) {
    /* ยังเก็บข้อมูลไม่เต็มหน้าต่าง ยังคำนวณไม่ได้ */
    return false;
  }
  if (_sinceUpdate < _updateEvery) {
    return false;
  }
  _sinceUpdate = 0;

  compute();
  return true;
}

void MassmoreMAX3010xSpO2::compute() {
  const uint16_t n = MASSMORE_MAX3010X_SPO2_WINDOW;

  /* ---- หาค่าเฉลี่ย ซึ่งก็คือองค์ประกอบ DC ---- */
  double sumRed = 0.0;
  double sumIr = 0.0;
  for (uint16_t i = 0; i < n; i++) {
    sumRed += (double)_red[i];
    sumIr += (double)_ir[i];
  }
  const float dcRed = (float)(sumRed / (double)n);
  const float dcIr = (float)(sumIr / (double)n);

  /* ---- หาค่า RMS ของส่วนที่แกว่ง ซึ่งก็คือองค์ประกอบ AC ----
     ใช้ RMS แทนการวัดยอดถึงท้อง เพราะจุดยอดเดียวที่โดนสัญญาณรบกวน
     ทำให้ค่ายอดถึงท้องเพี้ยนได้ทั้งหน้าต่าง แต่ RMS เฉลี่ยความผิดพลาดออกไป */
  double sumSqRed = 0.0;
  double sumSqIr = 0.0;
  for (uint16_t i = 0; i < n; i++) {
    const double dRed = (double)_red[i] - (double)dcRed;
    const double dIr = (double)_ir[i] - (double)dcIr;
    sumSqRed += dRed * dRed;
    sumSqIr += dIr * dIr;
  }
  const float acRed = (float)sqrt(sumSqRed / (double)n);
  const float acIr = (float)sqrt(sumSqIr / (double)n);

  _result.dcRed = dcRed;
  _result.dcIr = dcIr;
  _result.fingerPresent = (dcIr >= (float)_fingerThreshold);

  /* แปลง RMS เป็นแอมพลิจูดยอดถึงท้องโดยประมาณ (คลื่นคล้ายไซน์)
     ตัวคูณนี้หักล้างกันไปในสูตร R แต่คงไว้เพื่อให้ค่า perfusion index
     ออกมาเป็นหน่วยเดียวกับที่วงการใช้กัน */
  const float ppRed = acRed * 2.828427f;
  const float ppIr = acIr * 2.828427f;

  _result.perfusionRed = (dcRed > 1.0f) ? (ppRed / dcRed * 100.0f) : 0.0f;
  _result.perfusionIr = (dcIr > 1.0f) ? (ppIr / dcIr * 100.0f) : 0.0f;

  /* ---- อัตราการเต้น มาจากตัวจับจังหวะที่ป้อนข้อมูลคู่ขนานไว้แล้ว ---- */
  _result.heartRate = _beat.getAverageBeatsPerMinute();
  _result.heartRateValid =
      _result.fingerPresent && _beat.getBeatCount() >= 3 &&
      _result.heartRate > 0.0f;

  /* ---- คำนวณ R แล้วแปลงเป็นเปอร์เซ็นต์ ---- */
  _result.spo2Valid = false;
  _result.ratio = 0.0f;

  if (!_result.fingerPresent || dcRed < 1000.0f || dcIr < 1000.0f ||
      acIr < 1.0f) {
    _result.spo2 = 0.0f;
    return;
  }

  const float ratio = (acRed / dcRed) / (acIr / dcIr);
  _result.ratio = ratio;

  /* ค่า R ที่เป็นไปได้ทางสรีรวิทยาอยู่ราว 0.4 (SpO2 100%) ถึง 3.4 (SpO2 0%)
     ค่านอกช่วงนี้แปลว่าสัญญาณเสีย ไม่ใช่ค่าออกซิเจนจริง */
  if (ratio < 0.3f || ratio > 3.0f) {
    _result.spo2 = 0.0f;
    return;
  }

  /* ดัชนีการไหลเวียนต่ำกว่า 0.05% แปลว่าสัญญาณชีพจรจมอยู่ในสัญญาณรบกวน */
  if (_result.perfusionIr < 0.05f) {
    _result.spo2 = 0.0f;
    return;
  }

  float spo2 = _calA * ratio * ratio + _calB * ratio + _calC;
  if (spo2 > 100.0f) {
    spo2 = 100.0f;
  }
  if (spo2 < 70.0f) {
    spo2 = 70.0f;
  }

  _result.spo2 = spo2;
  _result.spo2Valid = true;
}
