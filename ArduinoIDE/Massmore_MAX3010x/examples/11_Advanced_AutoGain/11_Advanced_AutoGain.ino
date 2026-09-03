/*
  11_Advanced_AutoGain - ปรับกระแส LED อัตโนมัติและอ่านสัญญาณคุณภาพสูง

  ปัญหาที่เจอจริงเวลาใช้งาน
    นิ้วแต่ละคนหนาบางไม่เท่ากัน สีผิวต่างกัน กดแรงต่างกัน ทำให้แสงสะท้อน
    กลับมาไม่เท่ากัน ถ้าตั้งกระแส LED ตายตัว จะเจอสองอาการ
      กระแสสูงเกิน -> ADC อิ่มตัว (ค่าค้างที่ 262143) สัญญาณชีพจรหายหมด
      กระแสต่ำเกิน -> สัญญาณจมอยู่ในสัญญาณรบกวน จับจังหวะไม่ได้

  ทางแก้คือ automatic gain control ปรับกระแสให้ค่า DC อยู่ในช่วงที่ดีที่สุด
  ซึ่งงานวิจัยและ application note ของ Maxim แนะนำไว้ราว 25 ถึง 50 เปอร์เซ็นต์
  ของช่วง ADC เต็มสเกล

  ตัวอย่างนี้ทำสามอย่าง
    1. ปรับกระแส LED อัตโนมัติทุก 1 วินาที ให้ DC อยู่ในกรอบเป้าหมาย
    2. ขยายช่วง ADC ให้อัตโนมัติถ้ากระแสถึงเพดานแล้วยังไม่พอ
    3. รายงานคุณภาพสัญญาณ (perfusion index) ให้ผู้ใช้ปรับท่าวางนิ้ว

  เอาไปใช้ต่อได้เลยกับงานจริงที่ต้องรองรับผู้ใช้หลายคน

  by Massmore  |  MIT License
*/

#include <Massmore_MAX3010x.h>
#include <Massmore_MAX3010x_Algorithms.h>
#include <Wire.h>

#define PIN_SDA 21
#define PIN_SCL 22

/* กรอบเป้าหมายของค่า DC คิดจาก ADC 18 บิต เต็มสเกล 262143
   30% ถึง 55% เป็นช่วงที่ให้ headroom พอสำหรับคลื่นชีพจรโดยไม่อิ่มตัว */
#define DC_TARGET_LOW 78000UL
#define DC_TARGET_HIGH 144000UL
#define DC_SATURATION 250000UL

#define LED_MIN 0x08
#define LED_MAX 0xB0

MassmoreMAX3010x sensor;
MassmoreMAX3010xSpO2 oximeter;

static uint8_t ledLevel = 0x1F;
static uint8_t adcRangeIndex = 1; /* เริ่มที่ 4096 nA */
static uint32_t lastGainMs = 0;
static uint32_t lastReportMs = 0;

/* ค่าเฉลี่ยเคลื่อนที่ของ DC ช่องอินฟราเรด ใช้ตัดสินใจปรับกระแส */
static float dcIrAverage = 0.0f;
static uint32_t dcIrPeak = 0;

void applyGain() {
  sensor.setPulseAmplitudeRed(ledLevel);
  sensor.setPulseAmplitudeIR(ledLevel);
  sensor.setAdcRange((massmore_max3010x_adc_range_t)adcRangeIndex);
}

void autoGain() {
  const uint16_t rangeNa[] = {2048, 4096, 8192, 16384};
  const uint8_t before = ledLevel;
  const uint8_t beforeRange = adcRangeIndex;

  if (dcIrAverage < 5000.0f) {
    /* ไม่มีนิ้ววางอยู่ ไม่ต้องปรับอะไร รอไว้ก่อน */
    return;
  }

  if (dcIrPeak > DC_SATURATION) {
    /* อิ่มตัวแล้ว ต้องลดกระแสลงเยอะ ๆ หรือขยายช่วง ADC */
    if (ledLevel > LED_MIN + 0x10) {
      ledLevel = (uint8_t)(ledLevel - 0x10);
    } else if (adcRangeIndex < 3) {
      adcRangeIndex++;
    }
  } else if (dcIrAverage > (float)DC_TARGET_HIGH) {
    if (ledLevel > LED_MIN + 0x04) {
      ledLevel = (uint8_t)(ledLevel - 0x04);
    }
  } else if (dcIrAverage < (float)DC_TARGET_LOW) {
    if (ledLevel < LED_MAX - 0x04) {
      ledLevel = (uint8_t)(ledLevel + 0x04);
    } else if (adcRangeIndex > 0) {
      /* กระแสถึงเพดานแล้วยังไม่พอ ลดช่วง ADC ลงเพื่อเพิ่มความไว */
      adcRangeIndex--;
      ledLevel = 0x60;
    }
  }

  if (ledLevel != before || adcRangeIndex != beforeRange) {
    applyGain();
    Serial.print("[ปรับ] กระแส LED ");
    Serial.print(MassmoreMAX3010x::ledCodeToMilliAmp(ledLevel), 1);
    Serial.print(" mA   ช่วง ADC ");
    Serial.print(rangeNa[adcRangeIndex]);
    Serial.print(" nA   (DC เฉลี่ย ");
    Serial.print(dcIrAverage, 0);
    Serial.println(")");
  }

  dcIrPeak = 0;
}

void setup() {
  Serial.begin(115200);
  delay(300);

  Serial.println();
  Serial.println("Massmore MAX3010x - 11 ปรับกระแสอัตโนมัติ");
  Serial.println("==========================================");

  if (!sensor.begin(Wire, MASSMORE_MAX3010X_I2C_ADDRESS, PIN_SDA, PIN_SCL)) {
    Serial.print("เชื่อมต่อไม่สำเร็จ: ");
    Serial.println(sensor.lastErrorString());
    while (true) {
      delay(1000);
    }
  }

  sensor.setup(ledLevel, MASSMORE_MAX3010X_SMP_AVE_8,
               MASSMORE_MAX3010X_MODE_SPO2, MASSMORE_MAX3010X_RATE_400,
               MASSMORE_MAX3010X_PULSE_411US,
               (massmore_max3010x_adc_range_t)adcRangeIndex);

  oximeter.begin(sensor.getEffectiveSampleRate(), 25);

  Serial.println("วางนิ้วแล้วดูระบบปรับกระแสให้อัตโนมัติ");
  Serial.println();
}

void loop() {
  if (sensor.update()) {
    while (sensor.available()) {
      const uint32_t red = sensor.getRed();
      const uint32_t ir = sensor.getIR();
      sensor.nextSample();

      /* ติดตามค่า DC ด้วยตัวกรองผ่านต่ำอย่างง่าย และจำค่ายอดไว้ดูการอิ่มตัว */
      if (dcIrAverage == 0.0f) {
        dcIrAverage = (float)ir;
      } else {
        dcIrAverage += ((float)ir - dcIrAverage) * 0.01f;
      }
      if (ir > dcIrPeak) {
        dcIrPeak = ir;
      }

      oximeter.add(red, ir);
    }
  }

  const uint32_t now = millis();

  if (now - lastGainMs >= 1000) {
    lastGainMs = now;
    autoGain();
  }

  if (now - lastReportMs >= 1000) {
    lastReportMs = now;

    const massmore_max3010x_spo2_result_t &r = oximeter.getResult();

    if (!r.fingerPresent) {
      Serial.print("รอวางนิ้ว...  DC = ");
      Serial.println(dcIrAverage, 0);
      return;
    }

    Serial.print("DC ");
    Serial.print(r.dcIr, 0);
    Serial.print("  PI ");
    Serial.print(r.perfusionIr, 2);
    Serial.print("%  ");

    /* บอกคุณภาพสัญญาณให้ผู้ใช้เข้าใจง่าย ๆ */
    if (r.perfusionIr >= 1.0f) {
      Serial.print("[สัญญาณดีมาก]  ");
    } else if (r.perfusionIr >= 0.3f) {
      Serial.print("[สัญญาณใช้ได้]  ");
    } else {
      Serial.print("[สัญญาณอ่อน ลองกดนิ้วให้แนบขึ้น]  ");
    }

    if (r.spo2Valid) {
      Serial.print("SpO2 ");
      Serial.print(r.spo2, 1);
      Serial.print("%  ");
    }
    if (r.heartRateValid) {
      Serial.print("BPM ");
      Serial.print(r.heartRate, 1);
    }
    Serial.println();
  }
}
