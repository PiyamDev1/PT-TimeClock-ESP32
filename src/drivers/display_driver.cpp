#include "display_driver.h"

#include <Arduino.h>
#include <esp_heap_caps.h>
#include <Arduino_GFX_Library.h>

#include "pins.h"

namespace ptc {

namespace {

constexpr int kHorRes = 800;
constexpr int kVerRes = 480;
constexpr int kPclkHz = 15000000;
constexpr int kHsyncPolarity = 0;
constexpr int kHsyncBackPorch = 8;
constexpr int kHsyncFrontPorch = 8;
constexpr int kHsyncPulseWidth = 4;
constexpr int kVsyncPolarity = 0;
constexpr int kVsyncBackPorch = 8;
constexpr int kVsyncFrontPorch = 8;
constexpr int kVsyncPulseWidth = 4;
constexpr int kPclkActiveNeg = 1;
constexpr int kNoPsramBufferLines = 30;
constexpr uint8_t kBacklightDutyOn = 255;
constexpr uint8_t kBacklightDutyDim = 48;

Arduino_ESP32RGBPanel* g_bus = nullptr;
Arduino_RPi_DPI_RGBPanel* g_gfx = nullptr;
bool g_backlight_on = false;
bool g_backlight_dimmed = false;

void apply_backlight_duty(uint8_t duty) {
    pinMode(pins::kLcdBl, OUTPUT);
    digitalWrite(pins::kLcdBl, duty > 0 ? HIGH : LOW);
}

void display_flush_cb(lv_disp_drv_t* disp, const lv_area_t* area, lv_color_t* color_p) {
    if (!g_gfx) {
        lv_disp_flush_ready(disp);
        return;
    }

    uint32_t w = area->x2 - area->x1 + 1;
    uint32_t h = area->y2 - area->y1 + 1;

#if (LV_COLOR_16_SWAP != 0)
    g_gfx->draw16bitBeRGBBitmap(area->x1, area->y1, reinterpret_cast<uint16_t*>(&color_p->full), w, h);
#else
    g_gfx->draw16bitRGBBitmap(area->x1, area->y1, reinterpret_cast<uint16_t*>(&color_p->full), w, h);
#endif

    g_gfx->flush();

    lv_disp_flush_ready(disp);
}

} // namespace

bool display_driver_init(lv_disp_t** out_disp) {
    static lv_disp_draw_buf_t draw_buf;
    static lv_disp_drv_t disp_drv;
    static lv_color_t* lv_draw_buf = nullptr;

    Serial.println("display_driver_init: start");

    g_bus = new Arduino_ESP32RGBPanel(
        GFX_NOT_DEFINED, GFX_NOT_DEFINED, GFX_NOT_DEFINED,
        pins::kLcdDe, pins::kLcdVsync, pins::kLcdHsync, pins::kLcdPclk,
        pins::kLcdR0, pins::kLcdR1, pins::kLcdR2, pins::kLcdR3, pins::kLcdR4,
        pins::kLcdG0, pins::kLcdG1, pins::kLcdG2, pins::kLcdG3, pins::kLcdG4, pins::kLcdG5,
        pins::kLcdB0, pins::kLcdB1, pins::kLcdB2, pins::kLcdB3, pins::kLcdB4);

    g_gfx = new Arduino_RPi_DPI_RGBPanel(
        g_bus,
        kHorRes, kHsyncPolarity, kHsyncFrontPorch, kHsyncPulseWidth, kHsyncBackPorch,
        kVerRes, kVsyncPolarity, kVsyncFrontPorch, kVsyncPulseWidth, kVsyncBackPorch,
        kPclkActiveNeg, kPclkHz, false);

    if (!g_gfx) {
        Serial.println("display_driver_init: panel alloc failed");
        return false;
    }

    g_gfx->begin();

    if (!g_gfx->getFramebuffer()) {
        Serial.println("display_driver_init: Arduino_GFX begin failed");
        return false;
    }

    g_gfx->fillScreen(BLACK);

    lv_draw_buf = static_cast<lv_color_t*>(heap_caps_malloc(
        kHorRes * kNoPsramBufferLines * sizeof(lv_color_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA | MALLOC_CAP_8BIT));
    size_t lv_buf_pixels = kHorRes * kNoPsramBufferLines;
    if (!lv_draw_buf) {
        Serial.println("display_driver_init: draw buffer alloc failed");
        return false;
    }

    display_driver_set_backlight(true);

    lv_disp_draw_buf_init(&draw_buf, lv_draw_buf, nullptr, lv_buf_pixels);
    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res = kHorRes;
    disp_drv.ver_res = kVerRes;
    disp_drv.flush_cb = display_flush_cb;
    disp_drv.draw_buf = &draw_buf;
    disp_drv.sw_rotate = 1;
    disp_drv.full_refresh = 0;

    lv_disp_t* disp = lv_disp_drv_register(&disp_drv);
    Serial.println("display_driver_init: ready (backend=Arduino_GFX)");
    if (out_disp) {
        *out_disp = disp;
    }

    return true;
}

void display_driver_show_test_pattern() {
    if (!g_gfx) {
        Serial.println("display_driver_test: panel not ready");
        return;
    }

    g_gfx->fillScreen(RED);
    Serial.println("display_driver_test: RED");
    delay(700);
    g_gfx->fillScreen(GREEN);
    Serial.println("display_driver_test: GREEN");
    delay(700);
    g_gfx->fillScreen(BLUE);
    Serial.println("display_driver_test: BLUE");
    delay(700);
    g_gfx->fillScreen(BLACK);
    Serial.println("display_driver_test: BLACK");
    delay(300);
}

void display_driver_set_backlight(bool on) {
    g_backlight_on = on;
    g_backlight_dimmed = false;
    apply_backlight_duty(on ? kBacklightDutyOn : 0);
}

bool display_driver_is_backlight_on() {
    return g_backlight_on;
}

void display_driver_set_backlight_dimmed(bool dimmed) {
    if (!g_backlight_on) {
        g_backlight_dimmed = false;
        return;
    }

    g_backlight_dimmed = dimmed;
    apply_backlight_duty(dimmed ? kBacklightDutyDim : kBacklightDutyOn);
}

bool display_driver_is_backlight_dimmed() {
    return g_backlight_dimmed;
}

} // namespace ptc
