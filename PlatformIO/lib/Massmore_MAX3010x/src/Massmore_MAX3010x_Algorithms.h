/*!
 * @file Massmore_MAX3010x_Algorithms.h
 * @brief อัลกอริทึมประมวลผลสัญญาณ PPG สำหรับเซ็นเซอร์ตระกูล MAX3010x
 *        แบ่งเป็นสองตัวที่ใช้แยกกันหรือใช้ร่วมกันก็ได้
 *
 *   MassmoreMAX3010xBeatDetector  จับจังหวะการเต้นของหัวใจแบบเรียลไทม์
 *                                 กินแรมน้อยมาก ทำงานทีละตัวอย่าง
 *   MassmoreMAX3010xSpO2          คำนวณเปอร์เซ็นต์ออกซิเจนในเลือดจากหน้าต่าง
 *                                 ข้อมูลย้อนหลัง ด้วยวิธี ratio-of-ratios
 *
 * ทั้งสองตัวเขียนขึ้นใหม่ทั้งหมดจากหลักการที่อธิบายไว้ใน Application Note
 * ของ Maxim เรื่อง Recommended Configurations and Operating Profiles for
 * MAX30101/MAX30102 EV Kits และตำราเรื่อง photoplethysmography ทั่วไป
 * ไม่ได้คัดลอกซอร์สโค้ดของผู้ผลิตรายใด จึงใช้สัญญาอนุญาต MIT เหมือนไลบรารีหลัก
 *
 * @warning ผลลัพธ์จากไลบรารีนี้ใช้เพื่อการเรียนรู้และงานต้นแบบเท่านั้น
 *          ไม่ผ่านการสอบเทียบทางการแพทย์ ห้ามใช้วินิจฉัยหรือรักษาโรค
 *          ค่าที่ได้จะคลาดเคลื่อนมากเมื่อผู้วัดขยับนิ้ว มือเย็น หรือแสงรอบข้างแรง
 *
 * @copyright Copyright (c) 2026 Massmore Biz Co., Ltd.
 * @license MIT
 */

#ifndef MASSMORE_MAX3010X_ALGORITHMS_H
#define MASSMORE_MAX3010X_ALGORITHMS_H

#include <stdint.h>

#ifdef ARDUINO
#include <Arduino.h>
#else
#include <stdbool.h>
#include <stddef.h>
#endif

/*! ระดับ DC ของช่องอินฟราเรดที่ถือว่ามีนิ้ววางอยู่บนเซ็นเซอร์
 *  ค่านี้ขึ้นกับกระแส LED ที่ตั้งไว้ ปรับได้ด้วย setFingerThreshold() */
#ifndef MASSMORE_MAX3010X_FINGER_THRESHOLD
#define MASSMORE_MAX3010X_FINGER_THRESHOLD 20000UL
#endif

/*! จำนวนตัวอย่างในหน้าต่างที่ใช้คำนวณ SpO2
 *  ค่าเริ่มต้น 200 ตัวอย่าง = 4 วินาที เมื่ออัตราข้อมูล 50 Hz
 *  ใช้แรม 200 x 8 = 1600 ไบต์ ปรับได้ด้วย -D ตอนคอมไพล์ */
#ifndef MASSMORE_MAX3010X_SPO2_WINDOW
#define MASSMORE_MAX3010X_SPO2_WINDOW 200
#endif

/*! จำนวนค่า BPM ล่าสุดที่นำมาเฉลี่ยให้ตัวเลขนิ่งขึ้น */
#ifndef MASSMORE_MAX3010X_BPM_AVERAGE_COUNT
#define MASSMORE_MAX3010X_BPM_AVERAGE_COUNT 6
#endif

#if MASSMORE_MAX3010X_BPM_AVERAGE_COUNT < 1 || MASSMORE_MAX3010X_BPM_AVERAGE_COUNT > 255
#error "MASSMORE_MAX3010X_BPM_AVERAGE_COUNT ต้องอยู่ระหว่าง 1 ถึง 255 เพราะตัวนับเป็น uint8_t"
#endif

/* =========================================================================
   ตัวจับจังหวะการเต้นของหัวใจ
   ========================================================================= */

/*!
 * @brief จับจังหวะการเต้นของหัวใจจากสัญญาณ PPG ทีละตัวอย่าง
 *
 * หลักการทำงาน 4 ขั้น
 *   1. ตัดองค์ประกอบ DC ทิ้งด้วยตัวกรองผ่านสูงแบบขั้วเดียว
 *      เหลือไว้แต่คลื่นชีพจรที่แกว่งรอบศูนย์
 *   2. กรองความถี่สูงทิ้งด้วยตัวกรองผ่านต่ำ ตัดสัญญาณรบกวนจากไฟ 50 Hz
 *      และการสั่นของนิ้ว
 *   3. ติดตามยอดและท้องคลื่นแบบปรับตัวเอง (adaptive envelope) เพื่อให้ได้
 *      เส้นเกณฑ์ที่ขยับตามความแรงสัญญาณของแต่ละคน
 *   4. นับ 1 ครั้งเมื่อสัญญาณตัดเส้นเกณฑ์ขาขึ้น พร้อมช่วงห้ามนับซ้ำ
 *      (refractory) เพื่อไม่ให้ยอดคลื่นสะท้อน dicrotic notch ถูกนับเป็นครั้งใหม่
 *
 * ทำงานด้วยการนับตัวอย่าง ไม่ได้ใช้ millis() จึงให้ผลเหมือนเดิมทุกครั้ง
 * เมื่อป้อนข้อมูลชุดเดียวกัน สะดวกต่อการทดสอบ
 */
class MassmoreMAX3010xBeatDetector {
 public:
  MassmoreMAX3010xBeatDetector();

  /*!
   * @brief เตรียมตัวจับจังหวะ
   * @param sampleRateHz อัตราข้อมูลจริงที่ป้อนเข้ามา หน่วย Hz
   *        ดูค่าได้จาก MassmoreMAX3010x::getEffectiveSampleRate()
   */
  void begin(float sampleRateHz = 50.0f);

  /*! @brief ล้างสถานะทั้งหมด เริ่มนับใหม่ */
  void reset();

  /*!
   * @brief ป้อนตัวอย่างใหม่หนึ่งค่า
   * @param sample ค่าดิบจากช่องอินฟราเรด (หรือช่องอื่นที่ต้องการ)
   * @return true เฉพาะตัวอย่างที่ตรวจพบว่าเป็นจังหวะเต้นครั้งใหม่
   */
  bool check(uint32_t sample);

  /*! @brief อัตราการเต้นจากช่วงห่างของสองครั้งล่าสุด หน่วยครั้งต่อนาที */
  float getBeatsPerMinute() const { return _bpm; }
  /*! @brief อัตราการเต้นแบบเฉลี่ยย้อนหลัง ตัวเลขนิ่งกว่า แนะนำให้แสดงค่านี้ */
  float getAverageBeatsPerMinute() const { return _bpmAverage; }
  /*! @brief จำนวนครั้งที่นับได้ตั้งแต่ reset() */
  uint32_t getBeatCount() const { return _beatCount; }

  /*! @brief สัญญาณหลังผ่านตัวกรองแล้ว เอาไปวาดกราฟ Serial Plotter ได้เลย */
  float getFilteredSignal() const { return _filtered; }
  /*! @brief ระดับ DC ปัจจุบัน ใช้ดูว่าวางนิ้วแน่นพอหรือยัง */
  float getDcLevel() const { return _dc; }
  /*! @brief ความสูงของคลื่นชีพจรในหน่วยเดียวกับค่าดิบ */
  float getAmplitude() const { return _envelopeHigh - _envelopeLow; }

  /*! @brief ผ่านช่วงอุ่นเครื่องตัวกรอง 1 วินาทีแรกแล้วหรือยัง
   *  ระหว่างที่ยังไม่พร้อม ค่าที่ได้จะยังไม่น่าเชื่อถือ */
  bool isReady() const { return _primed; }

  /*! @brief มีนิ้ววางอยู่บนเซ็นเซอร์หรือไม่ (ดูจากระดับ DC) */
  bool isFingerPresent() const { return _dc >= (float)_fingerThreshold; }
  void setFingerThreshold(uint32_t threshold) { _fingerThreshold = threshold; }

  /*! @brief ตั้งช่วงอัตราการเต้นที่ยอมรับ ค่าที่หลุดช่วงจะถูกทิ้ง */
  void setBpmRange(float minBpm, float maxBpm);

 private:
  float _sampleRate;
  float _dcAlpha;       /*!< ค่าคงที่ตัวกรองผ่านสูง */
  float _lpAlpha;       /*!< ค่าคงที่ตัวกรองผ่านต่ำ */
  float _envelopeDecay; /*!< อัตราที่ยอด/ท้องคลื่นคลายตัวเข้าหากัน */

  float _dc;
  float _filtered;
  float _envelopeHigh;
  float _envelopeLow;

  bool _aboveThreshold;
  bool _primed;         /*!< ผ่านช่วงอุ่นเครื่องแล้วหรือยัง */
  uint32_t _sampleIndex;
  uint32_t _lastBeatIndex;
  uint32_t _warmupSamples;

  float _bpm;
  float _bpmAverage;
  float _bpmHistory[MASSMORE_MAX3010X_BPM_AVERAGE_COUNT];
  uint8_t _bpmHistoryCount;
  uint8_t _bpmHistoryIndex;
  uint32_t _beatCount;

  uint32_t _fingerThreshold;
  float _minBpm;
  float _maxBpm;
};

/* =========================================================================
   ตัวคำนวณ SpO2
   ========================================================================= */

/*!
 * @brief ผลการคำนวณหนึ่งรอบ
 */
typedef struct {
  float spo2;          /*!< เปอร์เซ็นต์ออกซิเจนในเลือด 70-100 */
  float heartRate;     /*!< ครั้งต่อนาที มาจากตัวจับจังหวะที่อยู่ข้างใน */
  float ratio;         /*!< ค่า R ที่ใช้คำนวณ มีไว้ให้ผู้ใช้สอบเทียบเอง */
  float perfusionIr;   /*!< ดัชนีการไหลเวียนช่อง IR หน่วยเปอร์เซ็นต์ */
  float perfusionRed;  /*!< ดัชนีการไหลเวียนช่องแดง หน่วยเปอร์เซ็นต์ */
  float dcIr;          /*!< ระดับ DC ช่อง IR */
  float dcRed;         /*!< ระดับ DC ช่องแดง */
  bool spo2Valid;      /*!< ค่า SpO2 เชื่อถือได้หรือไม่ */
  bool heartRateValid; /*!< ค่าอัตราการเต้นเชื่อถือได้หรือไม่ */
  bool fingerPresent;  /*!< ตรวจพบนิ้วบนเซ็นเซอร์หรือไม่ */
} massmore_max3010x_spo2_result_t;

/*!
 * @brief คำนวณ SpO2 ด้วยวิธี ratio-of-ratios
 *
 * วิธีนี้อาศัยข้อเท็จจริงว่าฮีโมโกลบินที่จับออกซิเจนแล้วกับที่ยังไม่จับ
 * ดูดกลืนแสงสีแดง (660 นาโนเมตร) กับแสงอินฟราเรด (880 นาโนเมตร) ไม่เท่ากัน
 * จึงเทียบอัตราส่วนของส่วนที่แกว่งตามชีพจร (AC) ต่อส่วนที่นิ่ง (DC)
 * ของทั้งสองช่วงคลื่น
 *
 *     R = (AC_แดง / DC_แดง) / (AC_IR / DC_IR)
 *
 * แล้วแปลงเป็นเปอร์เซ็นต์ด้วยเส้นโค้งสอบเทียบเชิงประจักษ์
 *
 *     SpO2 = -45.06 R^2 + 30.354 R + 94.845
 *
 * สมการนี้เป็นเส้นโค้งมาตรฐานที่ใช้กันทั่วไปในงาน pulse oximetry ระดับ
 * ผู้ผลิตอุปกรณ์อ้างอิง ไม่ได้สอบเทียบกับบอร์ดของ Massmore โดยเฉพาะ
 * ถ้าต้องการความแม่นยำจริงจังต้องเก็บข้อมูลเทียบกับเครื่องมาตรฐานเอง
 * แล้วแก้ค่าสัมประสิทธิ์ด้วย setCalibration()
 *
 * ส่วน AC วัดด้วยค่า RMS แทนการวัดยอดถึงท้อง เพราะทนสัญญาณรบกวนได้ดีกว่ามาก
 * ตัวคูณที่ใช้แปลง RMS เป็นแอมพลิจูดจะหักล้างกันไปเองในสูตรอัตราส่วน
 */
class MassmoreMAX3010xSpO2 {
 public:
  MassmoreMAX3010xSpO2();

  /*!
   * @brief เตรียมตัวคำนวณ
   * @param sampleRateHz อัตราข้อมูลจริงที่ป้อนเข้ามา หน่วย Hz
   * @param updateEverySamples คำนวณผลใหม่ทุกกี่ตัวอย่าง (0 = คำนวณทุกตัวอย่าง)
   */
  void begin(float sampleRateHz = 50.0f, uint16_t updateEverySamples = 25);

  /*! @brief ล้างหน้าต่างข้อมูลและสถานะทั้งหมด */
  void reset();

  /*!
   * @brief ป้อนข้อมูลคู่หนึ่งเข้าไป
   * @param red ค่าดิบช่องสีแดง
   * @param ir ค่าดิบช่องอินฟราเรด
   * @return true เมื่อมีผลลัพธ์ชุดใหม่ให้อ่าน
   */
  bool add(uint32_t red, uint32_t ir);

  /*! @brief ผลลัพธ์ล่าสุดทั้งชุด */
  const massmore_max3010x_spo2_result_t &getResult() const { return _result; }

  float getSpO2() const { return _result.spo2; }
  float getHeartRate() const { return _result.heartRate; }
  float getRatio() const { return _result.ratio; }
  bool isSpO2Valid() const { return _result.spo2Valid; }
  bool isFingerPresent() const { return _result.fingerPresent; }

  /*! @brief สัดส่วนข้อมูลที่เก็บได้แล้ว 0.0 ถึง 1.0 ใช้ทำแถบความคืบหน้า */
  float getFillRatio() const;

  /*!
   * @brief แก้สัมประสิทธิ์เส้นโค้งสอบเทียบ SpO2 = a R^2 + b R + c
   */
  void setCalibration(float a, float b, float c);

  void setFingerThreshold(uint32_t threshold);

  /*! @brief เข้าถึงตัวจับจังหวะที่อยู่ข้างในโดยตรง เผื่ออยากดูสัญญาณที่กรองแล้ว */
  MassmoreMAX3010xBeatDetector &beat() { return _beat; }

 private:
  uint32_t _red[MASSMORE_MAX3010X_SPO2_WINDOW];
  uint32_t _ir[MASSMORE_MAX3010X_SPO2_WINDOW];
  uint16_t _index;
  uint16_t _filled;
  uint16_t _sinceUpdate;
  uint16_t _updateEvery;

  float _sampleRate;
  float _calA;
  float _calB;
  float _calC;
  uint32_t _fingerThreshold;

  MassmoreMAX3010xBeatDetector _beat;
  massmore_max3010x_spo2_result_t _result;

  void compute();
};

#endif /* MASSMORE_MAX3010X_ALGORITHMS_H */
