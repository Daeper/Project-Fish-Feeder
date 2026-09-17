#include <Arduino.h>
#include <SPI.h>
#include <LovyanGFX.hpp>

#define SCREEN_WIDTH  240
#define SCREEN_HEIGHT 320

// =========================================================================
// ขาพิน ESP32-C6
// =========================================================================
#define TFT_SCLK_PIN   6   // SCK / T_CLK
#define TFT_MOSI_PIN   7   // MOSI / T_DIN
#define TFT_MISO_PIN   2   // MISO / T_DO
#define TFT_CS_PIN     4   // LCD CS
#define TFT_DC_PIN     5   // LCD DC
#define TFT_RST_PIN   10   // LCD RESET
#define TFT_BL_PIN    -1   // LED Backlight

#define TOUCH_CS_PIN   3   // T_CS (ชิป HR2046 / XPT2046)
#define TOUCH_IRQ_PIN  1   // T_IRQ (ขา Interrupt)

// =========================================================================
// ตั้งค่า LovyanGFX (ปิด pin_int เพื่อให้บังคับโพลลิ่ง ไม่ติดปัญหาขา IRQ)
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
            cfg.panel_width      = 240;
            cfg.panel_height     = 320;
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
            // ปิด pin_int (-1) เพื่อป้องกันกรณีขา IRQ ลอยหรือไม่ตอบสนอง
            cfg.pin_int    = -1;
            cfg.bus_shared = true;
            cfg.offset_rotation = 0;
            cfg.spi_host   = SPI2_HOST;
            cfg.freq       = 1000000; // 1MHz
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

// ฟังก์ชันวาดหน้าจอสำหรับทดสอบวาดภาพหลัง Calibrate เสร็จ
void drawTestCanvas() {
    tft.fillScreen(TFT_BLACK);

    // Header แสดงสถานะและพิกัด
    tft.fillRect(0, 0, SCREEN_WIDTH, 40, TFT_NAVY);
    tft.drawFastHLine(0, 40, SCREEN_WIDTH, TFT_WHITE);
    tft.setTextColor(TFT_WHITE, TFT_NAVY);
    tft.setTextSize(1);
    tft.drawString("TOUCH SCREEN TEST (240x320)", 8, 5);
    tft.setTextColor(TFT_GREENYELLOW, TFT_NAVY);
    tft.drawString("Status: Touch & Draw anywhere", 8, 22);

    // ปุ่ม CLEAR ด้านล่าง
    tft.fillRoundRect(10, SCREEN_HEIGHT - 40, SCREEN_WIDTH - 20, 32, 6, TFT_DARKGREY);
    tft.drawRoundRect(10, SCREEN_HEIGHT - 40, SCREEN_WIDTH - 20, 32, 6, TFT_WHITE);
    tft.setTextColor(TFT_WHITE, TFT_DARKGREY);
    tft.drawCenterString("CLEAR SCREEN", SCREEN_WIDTH / 2, SCREEN_HEIGHT - 30);

    // จุดเล็ง 4 มุม (ทดสอบความแม่นยำของขอบจอ)
    tft.drawCircle(8, 48, 6, TFT_RED);
    tft.drawCircle(SCREEN_WIDTH - 8, 48, 6, TFT_GREEN);
    tft.drawCircle(8, SCREEN_HEIGHT - 48, 6, TFT_BLUE);
    tft.drawCircle(SCREEN_WIDTH - 8, SCREEN_HEIGHT - 48, 6, TFT_MAGENTA);
}

void setup() {
    Serial.begin(115200);
    delay(500);
    Serial.println("\n\n==========================================");
    Serial.println("   LovyanGFX Touch Test (Calibrated)");
    Serial.println("==========================================");

    tft.init();
    tft.setRotation(0); // 0 = Portrait (240x320)

    // ใช้ค่า Calibration ที่บันทึกไว้
    uint16_t calData[8] = { 329, 267, 3886, 221, 296, 3790, 3867, 3713 };
    tft.setTouchCalibrate(calData);

    Serial.println("Loaded Touch Calibration data successfully!");

    drawTestCanvas();
}

uint16_t prevX = 0, prevY = 0;
bool wasTouched = false;
uint16_t colorIndex = 0;
uint16_t colors[] = { TFT_CYAN, TFT_YELLOW, TFT_MAGENTA, TFT_GREEN, TFT_ORANGE, TFT_WHITE };

void loop() {
    uint16_t x, y;
    bool touched = tft.getTouch(&x, &y);

    if (touched) {
        // อัปเดตพิกัดบน Header
        tft.fillRect(0, 20, SCREEN_WIDTH, 18, TFT_NAVY);
        tft.setTextColor(TFT_GREENYELLOW, TFT_NAVY);
        char buf[32];
        snprintf(buf, sizeof(buf), "X: %3d | Y: %3d", x, y);
        tft.drawString(buf, 8, 22);

        Serial.printf("[TOUCH] X: %3d, Y: %3d\n", x, y);

        // แตะปุ่ม CLEAR
        if (y >= SCREEN_HEIGHT - 40 && y <= SCREEN_HEIGHT - 8 && x >= 10 && x <= SCREEN_WIDTH - 10) {
            drawTestCanvas();
            delay(150);
        } else if (y > 40 && y < SCREEN_HEIGHT - 40) {
            // วาดจุด/เส้นตามตำแหน่งที่แตะ
            if (wasTouched) {
                tft.drawLine(prevX, prevY, x, y, colors[colorIndex]);
                tft.fillCircle(x, y, 3, colors[colorIndex]);
            } else {
                tft.fillCircle(x, y, 4, colors[colorIndex]);
            }
            prevX = x;
            prevY = y;
        }

        wasTouched = true;
    } else {
        if (wasTouched) {
            colorIndex = (colorIndex + 1) % (sizeof(colors) / sizeof(colors[0]));
            wasTouched = false;
        }
    }

    delay(10);
}
