#include <Arduino.h>
#include <lvgl.h>

#include "config.h"
#include "pins.h"
#include "secrets.h"

#include "services/service_storage.h"
#include "services/service_wifi.h"
#include "services/service_time.h"
#include "services/service_qr.h"
#include "services/service_log.h"
#include "services/service_http.h"
#include "services/service_ota.h"

#include "ui/ui_root.h"
#include "drivers/display_driver.h"
#include "drivers/touch_driver.h"

ptc::DeviceConfig g_config;
ptc::AppState g_state;

namespace {

constexpr uint32_t kScreenOffTimeoutMs = 60000;
constexpr uint32_t kWifiTickIntervalMs = 25;
constexpr uint32_t kTimeTickIntervalMs = 200;
constexpr uint32_t kHttpTickIntervalMs = 40;
constexpr uint32_t kQrTickIntervalMs = 80;
constexpr uint32_t kLogTickIntervalMs = 120;
constexpr uint32_t kOtaTickIntervalMs = 60;
constexpr uint32_t kDisplayRefreshIntervalMs = 30;
constexpr uint32_t kMaxUiSleepMs = 5;

uint32_t g_last_input_ms = 0;
bool g_display_ready = false;
uint32_t g_last_heartbeat_ms = 0;
uint32_t g_last_wifi_tick_ms = 0;
uint32_t g_last_time_tick_ms = 0;
uint32_t g_last_http_tick_ms = 0;
uint32_t g_last_qr_tick_ms = 0;
uint32_t g_last_log_tick_ms = 0;
uint32_t g_last_ota_tick_ms = 0;
uint32_t g_last_display_refresh_ms = 0;
lv_disp_t* g_display = nullptr;

}

void setup() {
    Serial.begin(115200);
    delay(200);
    Serial.println("[BOOT] setup start");

    ptc::service_storage_init();
    ptc::service_storage_load_config(g_config, g_state);
    g_config.display_rotation = 270;

    ptc::service_wifi_init();
    ptc::service_time_init();
    ptc::service_http_init();
    ptc::service_qr_init();
    ptc::service_log_init();
    ptc::service_ota_init();

    lv_init();
    Serial.println("[BOOT] LVGL init done");

    lv_disp_t* disp = nullptr;
    if (!ptc::display_driver_init(&disp)) {
        Serial.println("Display init failed");
        g_display_ready = false;
    } else {
        g_display_ready = true;
        Serial.println("[BOOT] Display init done");
    }

    if (g_display_ready && !ptc::touch_driver_init()) {
        Serial.println("Touch init failed");
    } else if (g_display_ready) {
        static lv_indev_drv_t indev_drv;
        lv_indev_drv_init(&indev_drv);
        indev_drv.type = LV_INDEV_TYPE_POINTER;
        indev_drv.read_cb = ptc::touch_driver_read;
        lv_indev_drv_register(&indev_drv);
        Serial.println("[BOOT] Touch input registered");
    }

    if (disp) {
        g_display = disp;
        lv_disp_set_rotation(disp, g_config.display_rotation == 90
            ? LV_DISP_ROT_90
            : g_config.display_rotation == 180
                ? LV_DISP_ROT_180
                : g_config.display_rotation == 270
                    ? LV_DISP_ROT_270
                    : LV_DISP_ROT_NONE);
    }

    if (g_display_ready) {
        ptc::ui_root_init(g_config, g_state);
        if (g_display && g_display->refr_timer) {
            lv_timer_pause(g_display->refr_timer);
        }
        Serial.println("[BOOT] UI root initialized");
    }
    g_last_input_ms = millis();
    g_last_heartbeat_ms = g_last_input_ms;
    Serial.println("[BOOT] setup complete");
}

void loop() {
    uint32_t now_ms = millis();

    if (g_display_ready) {
        ptc::touch_driver_tick();

        const bool wake_event = ptc::touch_driver_consume_wake_event();
        const bool tap_event = ptc::touch_driver_consume_tap_event();
        if (!ptc::display_driver_is_backlight_on()) {
            if (wake_event || tap_event) {
                ptc::touch_driver_suppress_until_release();
                ptc::display_driver_set_render_enabled(true);
                ptc::display_driver_set_backlight(true);
                g_last_input_ms = now_ms;
                lv_obj_invalidate(lv_scr_act());
                lv_refr_now(g_display);
                ptc::service_log_add("Display wake");
            }
        } else if (tap_event) {
            g_last_input_ms = now_ms;
        }
    }

    if (now_ms - g_last_wifi_tick_ms >= kWifiTickIntervalMs) {
        g_last_wifi_tick_ms = now_ms;
        ptc::service_wifi_tick(g_config, g_state);
    }
    if (now_ms - g_last_time_tick_ms >= kTimeTickIntervalMs) {
        g_last_time_tick_ms = now_ms;
        ptc::service_time_tick(g_config, g_state);
    }
    if (now_ms - g_last_http_tick_ms >= kHttpTickIntervalMs) {
        g_last_http_tick_ms = now_ms;
        ptc::service_http_tick(g_config, g_state);
    }
    if (now_ms - g_last_qr_tick_ms >= kQrTickIntervalMs) {
        g_last_qr_tick_ms = now_ms;
        ptc::service_qr_tick(g_config, g_state);
    }
    if (now_ms - g_last_log_tick_ms >= kLogTickIntervalMs) {
        g_last_log_tick_ms = now_ms;
        ptc::service_log_tick(g_config, g_state);
    }
    if (now_ms - g_last_ota_tick_ms >= kOtaTickIntervalMs) {
        g_last_ota_tick_ms = now_ms;
        ptc::service_ota_tick(g_config, g_state);
    }

    uint32_t sleep_ms = 1;
    if (g_display_ready) {
        uint32_t next = lv_timer_handler();
        sleep_ms = next == 0 ? 1 : min(next, kMaxUiSleepMs);

        if (ptc::display_driver_is_backlight_on() &&
            now_ms - g_last_display_refresh_ms >= kDisplayRefreshIntervalMs) {
            g_last_display_refresh_ms = now_ms;
            lv_refr_now(g_display);
        }
    }

    uint32_t now = now_ms;
    if (now - g_last_heartbeat_ms > 5000) {
        g_last_heartbeat_ms = now;
        Serial.printf("[HEARTBEAT] up=%lus display=%d wifi=%d heap=%u\n",
            static_cast<unsigned long>(now / 1000),
            g_display_ready ? 1 : 0,
            g_state.wifi_connected ? 1 : 0,
            static_cast<unsigned int>(ESP.getFreeHeap()));
    }

    uint32_t idle_ms = millis() - g_last_input_ms;
    if (g_display_ready && ptc::display_driver_is_backlight_on()) {
        if (idle_ms > kScreenOffTimeoutMs) {
            ptc::touch_driver_prepare_for_screen_off();
            ptc::display_driver_set_render_enabled(false);
            ptc::display_driver_set_backlight(false);
            ptc::service_log_add("Display sleep");
        }
    }

    delay(sleep_ms);
}
