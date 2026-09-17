#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <Wire.h>
#include <Adafruit_VL53L0X.h>
#include <LovyanGFX.hpp>
#include <ESP32Servo.h>
#include "ui.h"

#define SCREEN_WIDTH  240
#define SCREEN_HEIGHT 320

// =========================================================================
// 1. ตั้งค่า WiFi และ MQTT (เชื่อมต่อกับ HiveMQ Cloud ตาม index.html)
// =========================================================================
const char* WIFI_SSID     = "Nitipat.c";     // ชื่อ WiFi ของคุณ
const char* WIFI_PASSWORD = "25198301";      // รหัสผ่าน WiFi ของคุณ

const char* MQTT_BROKER   = "42556a1a83004b9f8407776620ae63e7.s1.eu.hivemq.cloud";
const int   MQTT_PORT     = 8883; // TLS/SSL Port
const char* MQTT_USER     = "admin";
const char* MQTT_PASS     = "12345678";

// Topics สำหรับสื่อสารกับ Web Interface
const char* TOPIC_CMD         = "feeder/nitipat/cmd";
const char* TOPIC_FOOD_LEVEL  = "feeder/nitipat/food_level";
const char* TOPIC_LOG_FEED    = "feeder/nitipat/ui_log_feed";
const char* TOPIC_LOG_SYS     = "feeder/nitipat/ui_log_sys";
const char* TOPIC_SCHEDULE_UI = "feeder/nitipat/ui_schedule";

// =========================================================================
// 2. ขาพินเซนเซอร์วัดระยะ VL53L0X (I2C) บน ESP32-C6
// =========================================================================
#define I2C_SDA_PIN        18   // ขา SDA (GPIO 18)
#define I2C_SCL_PIN        19   // ขา SCL (GPIO 19)
#define DIST_MIN_MM         0   // ระยะ 0 ซม. (0 มม.) -> 0%
#define DIST_MAX_MM       100   // ระยะ 10 ซม. (100 มม.) -> 100%

// =========================================================================
// 3. ขาพิน Servo Motor บน ESP32-C6 (30-pin DevKit)
// =========================================================================
#define SERVO_PIN      0   // ขาสัญญาณ Servo (GPIO 0)
#define SERVO_POS_IDLE 0   // องศาเริ่มต้น (ปิดช่องอาหาร)
#define SERVO_POS_FEED 90  // องศาเปิดให้อาหาร (หมุนไป 90 องศา)

// =========================================================================
// 4. ขาพินจอ TFT และ Touch บน ESP32-C6
// =========================================================================
#define TFT_SCLK_PIN   6   // SCK (แชร์กับ T_CLK)
#define TFT_MOSI_PIN   7   // SDI/MOSI (แชร์กับ T_DIN)
#define TFT_MISO_PIN   2   // T_DO/MISO (อ่านค่าจาก Touch)
#define TFT_CS_PIN     4   // LCD CS
#define TFT_DC_PIN     5   // LCD DC
#define TFT_RST_PIN   10   // LCD RESET
#define TFT_BL_PIN    -1   // ขา LED

#define TOUCH_CS_PIN   3   // T_CS (ชิป HR2046 / XPT2046)
#define TOUCH_IRQ_PIN  1   // T_IRQ

// =========================================================================
// ตั้งค่าคลาส LovyanGFX สำหรับ ST7789 + HR2046 บน ESP32-C6
// =========================================================================
class LGFX : public lgfx::LGFX_Device {
    lgfx::Panel_ST7789      _panel_instance;
    lgfx::Bus_SPI           _bus_instance;
    lgfx::Touch_XPT2046     _touch_instance;

public:
    LGFX(void) {
        {
            auto cfg = _bus_instance.config();
            cfg.spi_host = SPI2_HOST;
            cfg.spi_mode = 0;
            cfg.freq_write = 40000000;
            cfg.freq_read  = 16000000;
            cfg.spi_3wire  = false;
            cfg.use_lock   = true;
            cfg.dma_channel = SPI_DMA_CH_AUTO;
            cfg.pin_sclk = TFT_SCLK_PIN;
            cfg.pin_mosi = TFT_MOSI_PIN;
            cfg.pin_miso = TFT_MISO_PIN;
            cfg.pin_dc   = TFT_DC_PIN;
            _bus_instance.config(cfg);
            _panel_instance.setBus(&_bus_instance);
        }
        {
            auto cfg = _panel_instance.config();
            cfg.pin_cs           = TFT_CS_PIN;
            cfg.pin_rst          = TFT_RST_PIN;
            cfg.pin_busy         = -1;
            cfg.panel_width      = SCREEN_WIDTH;
            cfg.panel_height     = SCREEN_HEIGHT;
            cfg.offset_x         = 0;
            cfg.offset_y         = 0;
            cfg.offset_rotation  = 0;
            cfg.dummy_read_pixel = 8;
            cfg.dummy_read_bits  = 1;
            cfg.readable         = false;
            cfg.invert           = false;
            cfg.rgb_order        = false;
            cfg.dlen_16bit       = false;
            cfg.bus_shared       = true;
            _panel_instance.config(cfg);
        }
        {
            auto cfg = _touch_instance.config();
            cfg.x_min      = 0;
            cfg.x_max      = 4095;
            cfg.y_min      = 0;
            cfg.y_max      = 4095;
            cfg.pin_int    = -1;
            cfg.bus_shared = true;
            cfg.offset_rotation = 0;
            cfg.spi_host   = SPI2_HOST;
            cfg.freq       = 1000000;
            cfg.pin_sclk   = TFT_SCLK_PIN;
            cfg.pin_mosi   = TFT_MOSI_PIN;
            cfg.pin_miso   = TFT_MISO_PIN;
            cfg.pin_cs     = TOUCH_CS_PIN;
            _touch_instance.config(cfg);
            _panel_instance.setTouch(&_touch_instance);
        }
        setPanel(&_panel_instance);
    }
};

LGFX tft;

lgfx::LGFX_Device& getTft() {
    return tft;
}

// =========================================================================
// ตัวแปรและอ็อบเจกต์ฮาร์ดแวร์ & เครือข่าย
// =========================================================================
WiFiClientSecure netClient;
PubSubClient mqttClient(netClient);

Servo fishServo;
Adafruit_VL53L0X lox = Adafruit_VL53L0X();
bool vl53l0x_available = false;

// ตัวแปรสถานะของระบบ
bool timer1_active = true;
bool timer2_active = false;
int food_percent = 0;
bool web_time_synced = false;

// ตารางเวลาให้อาหารอัตโนมัติ (สูงสุด 5 มื้อ ตาม web interface)
#define MAX_SCHEDULES 5
String scheduleTimes[MAX_SCHEDULES] = { "08:00", "17:00", "--:--", "--:--", "--:--" };
int lastFedMinute = -1;

// =========================================================================
// ฟังก์ชันจัดการเวลา Internal RTC
// =========================================================================
void initInternalRTC() {
    const char* compileDate = __DATE__;
    const char* compileTime = __TIME__;

    struct tm tm;
    memset(&tm, 0, sizeof(struct tm));

    char monthStr[4];
    int day, year, hour, minute, second;
    sscanf(compileDate, "%s %d %d", monthStr, &day, &year);
    sscanf(compileTime, "%d:%d:%d", &hour, &minute, &second);

    const char* months = "JanFebMarAprMayJunJulAugSepOctNovDec";
    char* p = strstr(months, monthStr);
    int month = (p != NULL) ? (p - months) / 3 : 0;

    tm.tm_year = year - 1900;
    tm.tm_mon  = month;
    tm.tm_mday = day;
    tm.tm_hour = hour;
    tm.tm_min  = minute;
    tm.tm_sec  = second;

    time_t t = mktime(&tm);
    struct timeval now = { .tv_sec = t, .tv_usec = 0 };
    settimeofday(&now, NULL);

    Serial.printf("[RTC] Synced from compile: %02d/%02d/%04d %02d:%02d:%02d\n",
                  day, month + 1, year, hour, minute, second);
}

// ส่งข้อความ Log ไปยัง Web Interface ผ่าน MQTT
void publishWebLog(const char* type, const char* message) {
    if (!mqttClient.connected()) return;

    time_t now;
    time(&now);
    struct tm* t = localtime(&now);
    char timeBuf[12];
    snprintf(timeBuf, sizeof(timeBuf), "%02d:%02d:%02d", t->tm_hour, t->tm_min, t->tm_sec);

    char payload[128];
    snprintf(payload, sizeof(payload), "[%s] %s", timeBuf, message);

    if (strcmp(type, "feed") == 0) {
        mqttClient.publish(TOPIC_LOG_FEED, payload, true);
    } else {
        mqttClient.publish(TOPIC_LOG_SYS, payload, true);
    }
}

// ฟังก์ชันสั่ง Servo ทำงานให้อาหารปลา
void triggerFishFeed(const char* sourceDesc = "Manual Fed") {
    Serial.printf(">> [ACTION] Feeding Fish (%s)...\n", sourceDesc);

    // 1. อัปเดต UI หน้าจอ
    ui_draw_feed_button(true);
    ui_draw_logs(sourceDesc, "Feeding...");

    // 2. ขยับ Servo ไปตำแหน่งเปิดช่องอาหาร (90 องศา)
    fishServo.write(SERVO_POS_FEED);
    delay(500);

    // 3. ขยับ Servo กลับมาปิดช่องอาหาร (0 องศา)
    fishServo.write(SERVO_POS_IDLE);
    delay(300);

    // 4. คืนสถานะปุ่มและอัปเดต Log บนหน้าจอ
    ui_draw_feed_button(false);
    ui_draw_logs(sourceDesc, "Done");

    // 5. ส่ง Log และระดับอาหารล่าสุดไปยัง Web Interface
    publishWebLog("feed", sourceDesc);
    if (mqttClient.connected()) {
        char buf[8];
        snprintf(buf, sizeof(buf), "%d", food_percent);
        mqttClient.publish(TOPIC_FOOD_LEVEL, buf, true);
    }

    Serial.println(">> [ACTION] Feeding Complete!");
}

// =========================================================================
// ฟังก์ชัน Callback เมื่อได้รับคำสั่งจาก Web Interface ทาง MQTT
// =========================================================================
void mqttCallback(char* topic, byte* payload, unsigned int length) {
    String message = "";
    for (unsigned int i = 0; i < length; i++) {
        message += (char)payload[i];
    }
    message.trim();
    Serial.printf("[MQTT RX] %s -> %s\n", topic, message.c_str());

    // 1. รับคำสั่งให้อาหารทันทีจากเว็บ: "FEED_NOW"
    if (message == "FEED_NOW") {
        triggerFishFeed("Web Manual Fed");
    }
    // 2. รับเวลาปัจจุบันที่ส่งมาจาก Web Interface: "SYNC_TIME:HH:MM:SS"
    else if (message.startsWith("SYNC_TIME:")) {
        String timeStr = message.substring(10);
        int h = 0, m = 0, s = 0;
        if (sscanf(timeStr.c_str(), "%d:%d:%d", &h, &m, &s) >= 2) {
            time_t now;
            time(&now);
            struct tm* timeinfo = localtime(&now);
            timeinfo->tm_hour = h;
            timeinfo->tm_min = m;
            timeinfo->tm_sec = s;
            time_t t = mktime(timeinfo);
            struct timeval tv = { .tv_sec = t, .tv_usec = 0 };
            settimeofday(&tv, NULL);
            web_time_synced = true;

            char buf[10];
            snprintf(buf, sizeof(buf), "%02d:%02d", h, m);
            ui_draw_header(buf, true);
            Serial.printf(">> [RTC] Synchronized time from Web: %02d:%02d:%02d\n", h, m, s);
        }
    }
    // 3. รับการตั้งเวลาจากเว็บ: "SET_SCHEDULE:08:00,17:00,--:--,--:--,--:--"
    else if (message.startsWith("SET_SCHEDULE:")) {
        String data = message.substring(13);
        int slotIndex = 0;
        int startIndex = 0;
        while (startIndex < data.length() && slotIndex < MAX_SCHEDULES) {
            int commaIndex = data.indexOf(',', startIndex);
            if (commaIndex == -1) commaIndex = data.length();
            String tStr = data.substring(startIndex, commaIndex);
            tStr.trim();
            scheduleTimes[slotIndex] = tStr;
            slotIndex++;
            startIndex = commaIndex + 1;
        }

        // อัปเดตตารางเวลา Timer 1 และ 2 บนหน้าจอ TFT
        if (scheduleTimes[0] != "--:--" && scheduleTimes[0] != "") {
            ui_set_timer1_text(scheduleTimes[0].c_str());
            timer1_active = true;
            ui_draw_timer1_toggle(true);
        } else {
            ui_set_timer1_text("--:--");
            timer1_active = false;
            ui_draw_timer1_toggle(false);
        }

        if (scheduleTimes[1] != "--:--" && scheduleTimes[1] != "") {
            ui_set_timer2_text(scheduleTimes[1].c_str());
            timer2_active = true;
            ui_draw_timer2_toggle(true);
        } else {
            ui_set_timer2_text("--:--");
            timer2_active = false;
            ui_draw_timer2_toggle(false);
        }

        Serial.println(">> [SCHEDULE] Updated from Web Interface!");
    }
}

// =========================================================================
// ฟังก์ชันเชื่อมต่อ WiFi และ MQTT Broker
// =========================================================================
unsigned long lastMqttRetry = 0;

void connectMQTT() {
    if (WiFi.status() != WL_CONNECTED) return;
    if (mqttClient.connected()) return;

    if (millis() - lastMqttRetry > 4000) {
        lastMqttRetry = millis();
        Serial.print("Connecting to HiveMQ Cloud MQTT Broker...");
        
        String clientId = "ESP32C6_Feeder_" + String((uint32_t)ESP.getEfuseMac(), HEX);
        if (mqttClient.connect(clientId.c_str(), MQTT_USER, MQTT_PASS)) {
            Serial.println(" Connected! ✅");

            // Subscribe Topics คำสั่งจาก Web
            mqttClient.subscribe(TOPIC_CMD);
            mqttClient.subscribe(TOPIC_SCHEDULE_UI);

            // ส่งระดับอาหารและสถานะขึ้น Web ทันทีที่ต่อติด
            char buf[8];
            snprintf(buf, sizeof(buf), "%d", food_percent);
            mqttClient.publish(TOPIC_FOOD_LEVEL, buf, true);
            publishWebLog("sys", "ESP32-C6 เชื่อมต่อออนไลน์");
            ui_draw_logs("Online", "Ready");
        } else {
            Serial.printf(" Failed! (rc=%d)\n", mqttClient.state());
        }
    }
}

void setup() {
    Serial.begin(115200);
    delay(500);
    Serial.println("\n\n==========================================");
    Serial.println("   AutoFISH UI + Web Interface (MQTT)");
    Serial.println("==========================================");

    // 1. เริ่มต้น RTC
    initInternalRTC();

    // 2. เริ่มต้นหน้าจอ TFT
    tft.init();
    tft.setRotation(0);

    // 3. โหลดค่า Calibrate ทัชสกรีน
    uint16_t calData[8] = { 329, 267, 3886, 221, 296, 3790, 3867, 3713 };
    tft.setTouchCalibrate(calData);
    Serial.println("Touch Calibration applied!");

    // 4. เริ่มต้น Servo Motor
    ESP32PWM::allocateTimer(0);
    ESP32PWM::allocateTimer(1);
    fishServo.setPeriodHertz(50);
    fishServo.attach(SERVO_PIN, 500, 2400);
    fishServo.write(SERVO_POS_IDLE);

    // 5. เริ่มต้นเซนเซอร์วัดระยะ VL53L0X
    Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
    if (!lox.begin()) {
        Serial.println(">> [WARNING] VL53L0X not found!");
        vl53l0x_available = false;
    } else {
        Serial.println(">> [OK] VL53L0X ToF Sensor Ready!");
        vl53l0x_available = true;
    }

    // 6. วาดโครงสร้าง UI หลัก
    ui_init_draw();

    // 7. กำหนดค่าเริ่มต้นให้กับหน้าจอ (แสดงเวลาจริงจาก RTC ทันทีตั้งแต่เริ่มเปิดเครื่อง)
    time_t now;
    time(&now);
    struct tm* timeinfo = localtime(&now);
    char timeStr[10];
    snprintf(timeStr, sizeof(timeStr), "%02d:%02d", timeinfo->tm_hour, timeinfo->tm_min);
    ui_draw_header(timeStr, false);

    ui_set_timer1_text(scheduleTimes[0].c_str());
    ui_set_timer2_text(scheduleTimes[1].c_str());
    ui_draw_timer1_toggle(timer1_active);
    ui_draw_timer2_toggle(timer2_active);
    ui_draw_food_level(0);
    ui_draw_logs("Ready", "Connecting WiFi");

    // 8. เริ่มเชื่อมต่อ WiFi
    Serial.printf("Connecting to WiFi: %s\n", WIFI_SSID);
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

    // 9. ตั้งค่า MQTT TLS/SSL
    netClient.setInsecure();
    mqttClient.setServer(MQTT_BROKER, MQTT_PORT);
    mqttClient.setBufferSize(512); // ขยาย Buffer ขนาด 512 bytes
    mqttClient.setCallback(mqttCallback);

    Serial.println("AutoFISH System Ready!");
}

unsigned long lastTouchTime = 0;
unsigned long lastClockUpdate = 0;
unsigned long lastSensorRead = 0;
float filtered_dist_mm = -1.0;
float last_reported_dist_mm = -999.0;
bool ntp_synced = false;

void loop() {
    // 1. จัดการการเชื่อมต่อ WiFi & MQTT & NTP
    if (WiFi.status() == WL_CONNECTED) {
        if (!ntp_synced) {
            ntp_synced = true;
            configTime(7 * 3600, 0, "pool.ntp.org", "time.google.com");
        }

        if (!mqttClient.connected()) {
            connectMQTT();
        } else {
            mqttClient.loop();
        }
    } else {
        ntp_synced = false;
    }

    // 2. อัปเดตเวลาบน Header ทุกๆ 1 วินาที (Flicker-Free: จะรีเฟรชเฉพาะเมื่อตัวเลขนาทีเปลี่ยน)
    if (millis() - lastClockUpdate >= 1000) {
        lastClockUpdate = millis();

        time_t now;
        time(&now);
        struct tm* timeinfo = localtime(&now);

        char timeStr[10];
        snprintf(timeStr, sizeof(timeStr), "%02d:%02d", timeinfo->tm_hour, timeinfo->tm_min);
        ui_draw_header(timeStr, mqttClient.connected());

        // ตรวจสอบตารางเวลาอัตโนมัติ (Schedule Feed) ที่วินาที 0
        if (timeinfo->tm_sec == 0) {
            for (int i = 0; i < MAX_SCHEDULES; i++) {
                if (scheduleTimes[i] != "--:--" && scheduleTimes[i] != "") {
                    int schH = 0, schM = 0;
                    if (sscanf(scheduleTimes[i].c_str(), "%d:%d", &schH, &schM) == 2) {
                        if (timeinfo->tm_hour == schH && timeinfo->tm_min == schM && lastFedMinute != timeinfo->tm_min) {
                            lastFedMinute = timeinfo->tm_min;
                            char logText[32];
                            snprintf(logText, sizeof(logText), "Auto (%02d:%02d)", schH, schM);
                            triggerFishFeed(logText);
                        }
                    }
                }
            }
        }
    }

    // 3. อ่านค่าระดับอาหารจากเซนเซอร์ VL53L0X พร้อม Low-Pass Filter และ Deadband +-3mm
    if (vl53l0x_available && (millis() - lastSensorRead >= 800)) {
        lastSensorRead = millis();
        VL53L0X_RangingMeasurementData_t measure;
        lox.rangingTest(&measure, false);

        if (measure.RangeStatus != 4) {
            int raw_dist = measure.RangeMilliMeter;

            // กรองสัญญาณความถี่สูง (EMA Filter) เพื่อให้ค่านุ่มนวล
            if (filtered_dist_mm < 0) {
                filtered_dist_mm = raw_dist;
                last_reported_dist_mm = raw_dist;
            } else {
                filtered_dist_mm = (0.80 * filtered_dist_mm) + (0.20 * raw_dist);
            }

            // ตรวจสอบ Threshold: หากระยะเปลี่ยนเกิน +- 3 มม. จากระยะล่าสุด ค่อยอัปเดตหน้าจอ
            if (abs(filtered_dist_mm - last_reported_dist_mm) >= 3.0) {
                last_reported_dist_mm = filtered_dist_mm;

                int calc_percent = map((int)filtered_dist_mm, DIST_MIN_MM, DIST_MAX_MM, 0, 100);
                calc_percent = constrain(calc_percent, 0, 100);

                if (calc_percent != food_percent) {
                    food_percent = calc_percent;
                    ui_draw_food_level(food_percent);
                    Serial.printf("[VL53L0X] Smooth: %.1f mm -> Food Level: %d %%\n",
                                  filtered_dist_mm, food_percent);

                    // ส่งค่าขึ้น Web Interface ทาง MQTT
                    if (mqttClient.connected()) {
                        char buf[8];
                        snprintf(buf, sizeof(buf), "%d", food_percent);
                        mqttClient.publish(TOPIC_FOOD_LEVEL, buf, true);
                    }
                }
            }
        }
    }

    // 4. ตรวจสอบ Touch Events บนหน้าจอ TFT
    uint16_t touchX, touchY;
    bool touched = tft.getTouch(&touchX, &touchY);

    if (touched && (millis() - lastTouchTime > 300)) {
        lastTouchTime = millis();
        Serial.printf("[TOUCH] X: %d, Y: %d\n", touchX, touchY);

        // กดปุ่ม FEED (ปุ่มกลมด้านล่าง) -> สั่ง Servo ทำงาน
        if (is_touch_feed_btn(touchX, touchY)) {
            triggerFishFeed("TFT Manual Fed");
        }
        // กดสวิตช์ Toggle 1
        else if (is_touch_toggle1(touchX, touchY)) {
            timer1_active = !timer1_active;
            Serial.printf(">> Timer 1 Toggle: %s\n", timer1_active ? "ON" : "OFF");
            ui_draw_timer1_toggle(timer1_active);
        }
        // กดสวิตช์ Toggle 2
        else if (is_touch_toggle2(touchX, touchY)) {
            timer2_active = !timer2_active;
            Serial.printf(">> Timer 2 Toggle: %s\n", timer2_active ? "ON" : "OFF");
            ui_draw_timer2_toggle(timer2_active);
        }
    }

    delay(10);
}
