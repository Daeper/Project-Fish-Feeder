#include <Wire.h>
#include "Adafruit_VL53L0X.h"

// กำหนดขา I2C
#define I2C_SDA 5
#define I2C_SCL 6

// สร้างออบเจกต์สำหรับเซนเซอร์
Adafruit_VL53L0X lox = Adafruit_VL53L0X();

void setup() {
  Serial.begin(115200);

  // รอจนกว่า Serial จะพร้อมใช้งาน
  while (!Serial) {
    delay(1);
  }

  Serial.println("เริ่มการทดสอบ VL53L0X");

  // ตั้งค่าเริ่มต้นให้ I2C ใช้ขาที่เรากำหนด (SDA = 5, SCL = 6)
  Wire.begin(I2C_SDA, I2C_SCL);

  // ตรวจสอบการเชื่อมต่อกับเซนเซอร์ (Library จะใช้ Wire ที่เราเพิ่งตั้งค่า)
  if (!lox.begin()) {
    Serial.println("ไม่พบเซนเซอร์ VL53L0X โปรดตรวจสอบการต่อสาย!");
    while(1); // หยุดการทำงานหากไม่พบเซนเซอร์
  }
  
  Serial.println("พบเซนเซอร์ พร้อมทำงาน\n");
}

void loop() {
  VL53L0X_RangingMeasurementData_t measure;
    
  Serial.print("กำลังวัดระยะ... ");
  
  // สั่งให้เซนเซอร์อ่านค่า
  lox.rangingTest(&measure, false);

  // ค่า RangeStatus ที่ไม่ใช่ 4 หมายถึงวัดค่าได้สำเร็จ
  if (measure.RangeStatus != 4) {  
    Serial.print("ระยะทาง (มม.): "); 
    Serial.println(measure.RangeMilliMeter);
  } else {
    Serial.println("อยู่นอกระยะการวัด (Out of range)");
  }
    
  delay(500); // หน่วงเวลา 0.5 วินาทีก่อนวัดค่าครั้งต่อไป
}