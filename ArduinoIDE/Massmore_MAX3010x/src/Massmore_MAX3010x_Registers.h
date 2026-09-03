/*!
 * @file Massmore_MAX3010x_Registers.h
 * @brief ตารางรีจิสเตอร์และค่าคงที่ของชิปตระกูล MAX3010x
 *        (MAX30101 / MAX30102 / MAX30105)
 *
 * ทุกค่าในไฟล์นี้คัดมาจาก datasheet ของ Analog Devices / Maxim Integrated โดยตรง
 *   MAX30101  https://www.analog.com/media/en/technical-documentation/data-sheets/MAX30101.pdf
 *   MAX30102  https://www.analog.com/media/en/technical-documentation/data-sheets/MAX30102.pdf
 *   MAX30105  https://www.analog.com/media/en/technical-documentation/data-sheets/MAX30105.pdf
 *
 * หมายเหตุ  MAX30100 ใช้ตารางรีจิสเตอร์คนละชุดกันโดยสิ้นเชิง (FIFO 16 บิต,
 * PART_ID = 0x11) ไลบรารีชุดนี้จึงไม่รองรับ MAX30100 และจะแจ้งเป็น
 * MASSMORE_MAX3010X_ERR_WRONG_CHIP ให้ทราบตั้งแต่ begin()
 *
 * @copyright Copyright (c) 2026 Massmore Biz Co., Ltd.
 * @license MIT
 */

#ifndef MASSMORE_MAX3010X_REGISTERS_H
#define MASSMORE_MAX3010X_REGISTERS_H

#include <stdint.h>

/* =========================================================================
   ที่อยู่บนบัส I2C
   ========================================================================= */

/*! ที่อยู่ I2C แบบ 7 บิตของชิป MAX3010x ทุกตัว ตรึงมาจากโรงงาน เปลี่ยนไม่ได้
    (datasheet ระบุเป็นไบต์ 0xAE สำหรับเขียน / 0xAF สำหรับอ่าน ซึ่งก็คือ
     0x57 เลื่อนซ้าย 1 บิตนั่นเอง) */
#define MASSMORE_MAX3010X_I2C_ADDRESS 0x57

/* =========================================================================
   กลุ่ม Status / Interrupt
   ========================================================================= */

#define MASSMORE_MAX3010X_REG_INT_STATUS_1 0x00 /*!< สถานะอินเทอร์รัปต์ชุดที่ 1 (อ่านแล้วเคลียร์) */
#define MASSMORE_MAX3010X_REG_INT_STATUS_2 0x01 /*!< สถานะอินเทอร์รัปต์ชุดที่ 2 (อ่านแล้วเคลียร์) */
#define MASSMORE_MAX3010X_REG_INT_ENABLE_1 0x02 /*!< เปิด/ปิดอินเทอร์รัปต์ชุดที่ 1 */
#define MASSMORE_MAX3010X_REG_INT_ENABLE_2 0x03 /*!< เปิด/ปิดอินเทอร์รัปต์ชุดที่ 2 */

/* บิตใน INT_STATUS_1 และ INT_ENABLE_1 (ตำแหน่งบิตตรงกัน ยกเว้น PWR_RDY ที่อ่านได้อย่างเดียว) */
#define MASSMORE_MAX3010X_INT_A_FULL (1u << 7)   /*!< FIFO ใกล้เต็มตามที่ตั้งไว้ */
#define MASSMORE_MAX3010X_INT_PPG_RDY (1u << 6)  /*!< มีข้อมูล PPG ชุดใหม่พร้อมอ่าน */
#define MASSMORE_MAX3010X_INT_ALC_OVF (1u << 5)  /*!< วงจรตัดแสงรบกวน (ALC) ล้น แสงแวดล้อมแรงเกิน */
#define MASSMORE_MAX3010X_INT_PROX (1u << 4)     /*!< ตรวจพบวัตถุเข้าใกล้ (เฉพาะ MAX30105) */
#define MASSMORE_MAX3010X_INT_PWR_RDY (1u << 0)  /*!< ไฟเลี้ยงพร้อม อ่านได้อย่างเดียว ตั้งค่าไม่ได้ */

/* บิตใน INT_STATUS_2 และ INT_ENABLE_2 */
#define MASSMORE_MAX3010X_INT_DIE_TEMP_RDY (1u << 1) /*!< วัดอุณหภูมิแกนชิปเสร็จแล้ว */

/* =========================================================================
   กลุ่ม FIFO
   ========================================================================= */

#define MASSMORE_MAX3010X_REG_FIFO_WR_PTR 0x04   /*!< ตัวชี้ตำแหน่งเขียน 5 บิต (0-31) */
#define MASSMORE_MAX3010X_REG_OVF_COUNTER 0x05   /*!< จำนวนตัวอย่างที่หายไปเพราะ FIFO ล้น */
#define MASSMORE_MAX3010X_REG_FIFO_RD_PTR 0x06   /*!< ตัวชี้ตำแหน่งอ่าน 5 บิต (0-31) */
#define MASSMORE_MAX3010X_REG_FIFO_DATA 0x07     /*!< ประตูอ่านข้อมูล อ่านซ้ำได้เรื่อย ๆ */
#define MASSMORE_MAX3010X_REG_FIFO_CONFIG 0x08   /*!< ตั้งค่า FIFO */

#define MASSMORE_MAX3010X_FIFO_DEPTH 32          /*!< FIFO ลึก 32 ตัวอย่าง */
#define MASSMORE_MAX3010X_BYTES_PER_CHANNEL 3    /*!< ข้อมูล 18 บิต ส่งมา 3 ไบต์ต่อ 1 ช่อง LED */

/* มาสก์ของ FIFO_CONFIG */
#define MASSMORE_MAX3010X_MASK_SMP_AVE 0x1F      /*!< เก็บบิต 4:0 ไว้ ล้างบิต 7:5 */
#define MASSMORE_MAX3010X_MASK_ROLLOVER 0xEF     /*!< เก็บทุกบิต ล้างบิต 4 */
#define MASSMORE_MAX3010X_MASK_A_FULL 0xF0       /*!< เก็บบิต 7:4 ไว้ ล้างบิต 3:0 */
#define MASSMORE_MAX3010X_BIT_ROLLOVER_EN (1u << 4)

/* =========================================================================
   กลุ่ม Configuration
   ========================================================================= */

#define MASSMORE_MAX3010X_REG_MODE_CONFIG 0x09   /*!< SHDN / RESET / MODE */
#define MASSMORE_MAX3010X_REG_SPO2_CONFIG 0x0A   /*!< ช่วง ADC / อัตราสุ่ม / ความกว้างพัลส์ */

#define MASSMORE_MAX3010X_BIT_SHUTDOWN (1u << 7) /*!< เข้าโหมดประหยัดไฟ */
#define MASSMORE_MAX3010X_BIT_RESET (1u << 6)    /*!< รีเซ็ตชิป ชิปเคลียร์บิตนี้เองเมื่อเสร็จ */

#define MASSMORE_MAX3010X_MASK_SHUTDOWN 0x7F     /*!< ล้างบิต 7 */
#define MASSMORE_MAX3010X_MASK_RESET 0xBF        /*!< ล้างบิต 6 */
#define MASSMORE_MAX3010X_MASK_MODE 0xF8         /*!< ล้างบิต 2:0 */
#define MASSMORE_MAX3010X_MASK_ADC_RANGE 0x9F    /*!< ล้างบิต 6:5 */
#define MASSMORE_MAX3010X_MASK_SAMPLE_RATE 0xE3  /*!< ล้างบิต 4:2 */
#define MASSMORE_MAX3010X_MASK_PULSE_WIDTH 0xFC  /*!< ล้างบิต 1:0 */

/* ค่าของฟิลด์ MODE[2:0] ใน MODE_CONFIG */
#define MASSMORE_MAX3010X_MODE_VAL_HR 0x02        /*!< ใช้ LED1 ตัวเดียว (MAX30102 = สีแดง) */
#define MASSMORE_MAX3010X_MODE_VAL_SPO2 0x03      /*!< ใช้ LED1 + LED2 (แดง + อินฟราเรด) */
#define MASSMORE_MAX3010X_MODE_VAL_MULTI_LED 0x07 /*!< เลือกเองได้สูงสุด 4 ช่องเวลา (time slot) */

/* =========================================================================
   กลุ่มกระแส LED
   ========================================================================= */

#define MASSMORE_MAX3010X_REG_LED1_PA 0x0C       /*!< กระแส LED1 = สีแดง */
#define MASSMORE_MAX3010X_REG_LED2_PA 0x0D       /*!< กระแส LED2 = อินฟราเรด */
#define MASSMORE_MAX3010X_REG_LED3_PA 0x0E       /*!< กระแส LED3 = สีเขียว (MAX30101 / MAX30105) */
#define MASSMORE_MAX3010X_REG_LED4_PA 0x0F       /*!< กระแส LED4 = สีเขียวตัวที่สอง มีเฉพาะ MAX30101 */
#define MASSMORE_MAX3010X_REG_PILOT_PA 0x10      /*!< กระแส LED ตอนอยู่ในโหมด proximity มีเฉพาะ MAX30105 */

/* หมายเหตุเรื่องความต่างระหว่างรุ่น ซึ่งไลบรารีใช้เดารุ่นให้อัตโนมัติ
     MAX30102  ไม่มี LED3_PA, ไม่มี LED4_PA, ไม่มี proximity
     MAX30101  มี LED3_PA และ LED4_PA (สีเขียวสองดวง) แต่ตัด proximity ออกไปแล้ว
               ตั้งแต่ datasheet รีวิชัน 1 จึงไม่มีทั้ง PILOT_PA และ PROX_INT_THRESH
     MAX30105  มี LED3_PA, ไม่มี LED4_PA, มี proximity ครบทั้ง PILOT_PA และ threshold */

/*! กระแส LED 1 สเต็ป มีค่าประมาณ 0.2 mA ต่อ 1 หน่วยของรีจิสเตอร์
    ค่า 0x00 = ปิด, 0xFF ~ 51 mA (ค่าตามที่ datasheet ระบุคือ 50 mA typ) */
#define MASSMORE_MAX3010X_LED_STEP_MA 0.2f

/* =========================================================================
   กลุ่ม Multi-LED (เลือกว่าช่องเวลาไหนยิง LED ดวงใด)
   ========================================================================= */

#define MASSMORE_MAX3010X_REG_MULTI_LED_1 0x11   /*!< SLOT2[6:4] , SLOT1[2:0] */
#define MASSMORE_MAX3010X_REG_MULTI_LED_2 0x12   /*!< SLOT4[6:4] , SLOT3[2:0] */

#define MASSMORE_MAX3010X_MASK_SLOT_ODD 0xF8     /*!< ล้างบิต 2:0 (slot 1 และ 3) */
#define MASSMORE_MAX3010X_MASK_SLOT_EVEN 0x8F    /*!< ล้างบิต 6:4 (slot 2 และ 4) */

/* =========================================================================
   กลุ่มอุณหภูมิแกนชิป
   ========================================================================= */

#define MASSMORE_MAX3010X_REG_DIE_TEMP_INT 0x1F  /*!< ส่วนจำนวนเต็ม 8 บิตแบบมีเครื่องหมาย */
#define MASSMORE_MAX3010X_REG_DIE_TEMP_FRAC 0x20 /*!< ส่วนทศนิยม 4 บิต ละเอียด 0.0625 องศา */
#define MASSMORE_MAX3010X_REG_DIE_TEMP_CONFIG 0x21 /*!< บิต 0 = TEMP_EN สั่งวัด 1 ครั้งแล้วเคลียร์เอง */
#define MASSMORE_MAX3010X_BIT_TEMP_EN (1u << 0)
#define MASSMORE_MAX3010X_TEMP_FRAC_STEP 0.0625f

/* =========================================================================
   กลุ่ม Proximity (เฉพาะ MAX30105)
   ========================================================================= */

#define MASSMORE_MAX3010X_REG_PROX_INT_THRESH 0x30 /*!< ระดับ IR ที่ถือว่ามีวัตถุเข้าใกล้ */

/* =========================================================================
   กลุ่มรหัสประจำตัวชิป
   ========================================================================= */

#define MASSMORE_MAX3010X_REG_REVISION_ID 0xFE   /*!< เลขรีวิชันของซิลิคอน */
#define MASSMORE_MAX3010X_REG_PART_ID 0xFF       /*!< เลขรุ่น อ่านได้อย่างเดียว */

/*! ค่า PART_ID ของ MAX30101 / MAX30102 / MAX30105 ทั้งสามรุ่นเท่ากันหมด */
#define MASSMORE_MAX3010X_PART_ID_EXPECTED 0x15
/*! ค่า PART_ID ของ MAX30100 ซึ่งไลบรารีนี้ไม่รองรับ ใช้เพื่อแจ้งเตือนผู้ใช้ */
#define MASSMORE_MAX3010X_PART_ID_MAX30100 0x11

/* =========================================================================
   ขอบเขตของข้อมูลดิบ
   ========================================================================= */

/*! ข้อมูลจาก FIFO เป็นเลข 18 บิต ชิดซ้ายในกรอบ 3 ไบต์ จึงต้องมาสก์ทิ้ง 6 บิตบน */
#define MASSMORE_MAX3010X_DATA_MASK 0x0003FFFFUL
/*! ค่าสูงสุดที่ ADC 18 บิตแสดงได้ ใช้ตรวจว่าสัญญาณอิ่มตัวหรือไม่ */
#define MASSMORE_MAX3010X_DATA_MAX 0x0003FFFFUL

#endif /* MASSMORE_MAX3010X_REGISTERS_H */
