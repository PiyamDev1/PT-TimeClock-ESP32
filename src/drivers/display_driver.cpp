#include "display_driver.h"

#include <Arduino.h>
#include <esp_lcd_panel_rgb.h>
#include <esp_lcd_panel_ops.h>
#include <esp_heap_caps.h>

#include "pins.h"

namespace ptc {

namespace {

constexpr int kHorRes = 800;
constexpr int kVerRes = 480;
constexpr int kPclkHz = 16000000;
constexpr int kHsyncBackPorch = 88;
constexpr int kHsyncFrontPorch = 40;
constexpr int kHsyncPulseWidth = 40;
constexpr int kVsyncBackPorch = 23;
constexpr int kVsyncFrontPorch = 5;
constexpr int kVsyncPulseWidth = 5;

const int kDataGpios[16] = {
    pins::kLcdB0,
    pins::kLcdB1,
    pins::kLcdB2,
    pins::kLcdB3,
    pins::kLcdB4,
    pins::kLcdG0,
    pins::kLcdG1,
    pins::kLcdG2,
    pins::kLcdG3,
    pins::kLcdG4,
    pins::kLcdG5,
    pins::kLcdR0,
    pins::kLcdR1,
    pins::kLcdR2,
    pins::kLcdR3,
    pins::kLcdR4,
};

esp_lcd_panel_handle_t g_panel = nullptr;

void display_flush_cb(lv_disp_drv_t* disp, const lv_area_t* area, lv_color_t* color_p) {
    if (!g_panel) {
        lv_disp_flush_ready(disp);
        return;
    }

    esp_lcd_panel_draw_bitmap(g_panel,
        area->x1,
        area->y1,
        area->x2 + 1,
        area->y2 + 1,
        color_p);
    lv_disp_flush_ready(disp);
}

} // namespace

bool display_driver_init(lv_disp_t** out_disp) {
    static lv_disp_draw_buf_t draw_buf;
    static lv_disp_drv_t disp_drv;

    lv_color_t* fb = static_cast<lv_color_t*>(heap_caps_malloc(
        kHorRes * kVerRes * sizeof(lv_color_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!fb) {
        return false;
    }

    esp_lcd_rgb_panel_config_t panel_config = {};
    panel_config.clk_src = LCD_CLK_SRC_PLL160M;
    panel_config.timings.pclk_hz = kPclkHz;
    panel_config.timings.h_res = kHorRes;
    panel_config.timings.v_res = kVerRes;
    panel_config.timings.hsync_back_porch = kHsyncBackPorch;
    panel_config.timings.hsync_front_porch = kHsyncFrontPorch;
    panel_config.timings.hsync_pulse_width = kHsyncPulseWidth;
    panel_config.timings.vsync_back_porch = kVsyncBackPorch;
    panel_config.timings.vsync_front_porch = kVsyncFrontPorch;
    panel_config.timings.vsync_pulse_width = kVsyncPulseWidth;
    panel_config.timings.flags.pclk_active_neg = false;
    panel_config.timings.flags.hsync_idle_low = false;
    panel_config.timings.flags.vsync_idle_low = false;
    panel_config.data_width = 16;
    panel_config.psram_trans_align = 64;
    panel_config.flags.fb_in_psram = 1;
    for (int i = 0; i < 16; ++i) {
        panel_config.data_gpio_nums[i] = kDataGpios[i];
    }
    panel_config.de_gpio_num = pins::kLcdDe;
    panel_config.disp_gpio_num = -1;
    panel_config.pclk_gpio_num = pins::kLcdPclk;
    panel_config.vsync_gpio_num = pins::kLcdVsync;
    panel_config.hsync_gpio_num = pins::kLcdHsync;

    if (esp_lcd_new_rgb_panel(&panel_config, &g_panel) != ESP_OK) {
        return false;
    }

    if (esp_lcd_panel_init(g_panel) != ESP_OK) {
        return false;
    }

    pinMode(pins::kLcdBl, OUTPUT);
    digitalWrite(pins::kLcdBl, HIGH);

    lv_disp_draw_buf_init(&draw_buf, fb, nullptr, kHorRes * kVerRes);
    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res = kHorRes;
    disp_drv.ver_res = kVerRes;
    disp_drv.flush_cb = display_flush_cb;
    disp_drv.draw_buf = &draw_buf;
    disp_drv.full_refresh = 1;

    lv_disp_t* disp = lv_disp_drv_register(&disp_drv);
    if (out_disp) {
        *out_disp = disp;
    }

    return true;
}

} // namespace ptc
