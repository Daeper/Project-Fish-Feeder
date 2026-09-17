#ifndef UI_H
#define UI_H

#include <Arduino.h>
#include <LovyanGFX.hpp>

// ประกาศอ็อบเจกต์ tft เพื่อให้ ui.cpp และ main.ino เรียกใช้งานร่วมกันได้
extern lgfx::LGFX_Device& getTft();

// ฟังก์ชันวาด UI
void ui_init_draw();
void ui_draw_header(const char* timeStr, bool wifiConnected);
void ui_draw_timer1_toggle(bool isOn);
void ui_draw_timer2_toggle(bool isOn);
void ui_set_timer1_text(const char* timeStr);
void ui_set_timer2_text(const char* timeStr);
void ui_draw_food_level(int percent);
void ui_draw_logs(const char* log1, const char* log2);
void ui_draw_feed_button(bool pressed);

// พิกัดปุ่มสำหรับตรวจสอบการแตะ Touch
bool is_touch_feed_btn(uint16_t x, uint16_t y);
bool is_touch_toggle1(uint16_t x, uint16_t y);
bool is_touch_toggle2(uint16_t x, uint16_t y);

#endif // UI_H
