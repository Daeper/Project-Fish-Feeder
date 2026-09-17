#include <Arduino.h>
#include <LovyanGFX.hpp>
#include <lvgl.h>

#include "ui.h"

#define SCREEN_WIDTH  240
#define SCREEN_HEIGHT 320

// =========================================================================
// ขาพิน ESP32-C6 ตามที่คุณต่อจริง
// =========================================================================
#define TFT_SCLK_PIN   6   // SCK (แชร์กับ T_CLK)
#define TFT_MOSI_PIN   7   // SDI/MOSI (แชร์กับ T_DIN)
#define TFT_MISO_PIN   2   // T_DO/MISO (อ่านค่าจาก Touch)
#define TFT_CS_PIN     4   // LCD CS (ชิปซีเลกต์ของ ST7789V)
#define TFT_DC_PIN     5   // LCD DC / RS
#define TFT_RST_PIN   10   // LCD RESET
#define TFT_BL_PIN    -1   // ขา LED (ต่อไฟ 3.3V แล้วให้ใส่ -1)

#define TOUCH_CS_PIN   3   // T_CS (ชิปซีเลกต์ Touch XPT2046)
#define TOUCH_IRQ_PIN  1   // T_IRQ (แจ้งเตือนเมื่อสัมผัส)

// =========================================================================
// ตั้งค่าคลาส LovyanGFX สำหรับ ST7789 + XPT2046 บน ESP32-C6
// =========================================================================
class LGFX : public lgfx::LGFX_Device {
    lgfx::Panel_ST7789      _panel_instance; // จอ ST7789
    lgfx::Bus_SPI           _bus_instance;
    lgfx::Touch_XPT2046     _touch_instance; // ชิป Touch XPT2046

public:
    LGFX(void) {
        {
            auto cfg = _bus_instance.config();
            cfg.spi_host = SPI2_HOST;     // ESP32-C6 ใช้ SPI2_HOST
            cfg.spi_mode = 0;             // Mode 0
            cfg.freq_write = 40000000;    // 40MHz
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
            cfg.readable         = false; // ST7789 แบบ SPI 4-wire มักไม่มี MISO จากจอ
            cfg.invert           = false; // หากสีกลับด้าน (เช่น พื้นดำกลายเป็นขาว) ให้เปลี่ยนเป็น true
            cfg.rgb_order        = false; // false = RGB, true = BGR
            cfg.dlen_16bit       = false;
            cfg.bus_shared       = true;  // แชร์ SPI บัสร่วมกับ Touch
            _panel_instance.config(cfg);
        }
        {
            auto cfg = _touch_instance.config();
            cfg.x_min      = 200;
            cfg.x_max      = 3800;
            cfg.y_min      = 200;
            cfg.y_max      = 3800;
            cfg.pin_int    = -1;
            cfg.bus_shared = true;
            cfg.offset_rotation = 0;
            cfg.spi_host   = SPI2_HOST;
            cfg.freq       = 1000000;      // 1MHz สำหรับ Touch
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

// บัฟเฟอร์วาดภาพสำหรับ LVGL 9
#define DRAW_BUF_SIZE (SCREEN_WIDTH * 10 * sizeof(lv_color_t))
uint8_t draw_buf[DRAW_BUF_SIZE];

// Callback ส่งพิกเซลลงจอ (รูปแบบ LVGL 9)
void my_disp_flush(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map) {
    uint32_t w = (area->x2 - area->x1 + 1);
    uint32_t h = (area->y2 - area->y1 + 1);

    tft.startWrite();
    tft.setAddrWindow(area->x1, area->y1, w, h);
    tft.pushPixels((uint16_t *)px_map, w * h, true);
    tft.endWrite();

    lv_display_flush_ready(disp);
}

// Callback อ่านค่าสัมผัส (รูปแบบ LVGL 9)
void my_touchpad_read(lv_indev_t *indev, lv_indev_data_t *data) {
    uint16_t touchX, touchY;
    bool touched = tft.getTouch(&touchX, &touchY);

    if (touched) {
        data->state = LV_INDEV_STATE_PRESSED;
        data->point.x = touchX;
        data->point.y = touchY;
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
    }
}

void setup() {
    Serial.begin(115200);
    delay(500);
    Serial.println("Initializing Display...");

    tft.init();
    tft.setRotation(0);

    // นำค่า Calibration ที่ได้มาใส่เพื่อให้ทัชตรงตำแหน่ง 100%
    uint16_t calData[8] = { 329, 267, 3886, 221, 296, 3790, 3867, 3713 };
    tft.setTouchCalibrate(calData);

    // ทดสอบหน้าจอเบื้องต้นก่อนเริ่ม LVGL
    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(TFT_GREEN);
    tft.setTextSize(2);
    tft.setCursor(10, 20);
    tft.println("Starting LVGL 9...");
    delay(500);

    lv_init();

    // 1. ลงทะเบียน Display ใน LVGL 9
    lv_display_t *disp = lv_display_create(SCREEN_WIDTH, SCREEN_HEIGHT);
    lv_display_set_flush_cb(disp, my_disp_flush);
    lv_display_set_buffers(disp, draw_buf, NULL, sizeof(draw_buf), LV_DISPLAY_RENDER_MODE_PARTIAL);

    // 2. ลงทะเบียน Touch Input ใน LVGL 9
    lv_indev_t *indev = lv_indev_create();
    lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(indev, my_touchpad_read);

    // โหลด UI ที่ Export จาก SquareLine Studio
    ui_init();

    Serial.println("LVGL 9 + ST7789 + XPT2046 Ready on ESP32-C6!");
}

void loop() {
    lv_timer_handler();
    delay(5);
}