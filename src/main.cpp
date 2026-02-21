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

constexpr uint32_t kScreenDimTimeoutMs = 60000;
constexpr uint32_t kScreenOffTimeoutMs = 120000;
uint32_t g_last_input_ms = 0;
bool g_display_ready = false;
uint32_t g_last_heartbeat_ms = 0;

}

void setup() {
    Serial.begin(115200);
    delay(200);
    Serial.println("[BOOT] setup start");

    ptc::service_storage_init();
    ptc::service_storage_load_config(g_config, g_state);
    g_config.display_rotation = 90;
    ptc::TouchCalibration touch_calibration;
    if (ptc::service_storage_load_touch_calibration(touch_calibration)) {
        ptc::touch_driver_set_calibration(touch_calibration);
    }

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
        Serial.println("[BOOT] UI root initialized");
    }
    g_last_input_ms = millis();
    g_last_heartbeat_ms = g_last_input_ms;
    Serial.println("[BOOT] setup complete");
}

void loop() {
    ptc::service_wifi_tick(g_config, g_state);
    ptc::service_time_tick(g_config, g_state);
    ptc::service_http_tick(g_config, g_state);
    ptc::service_qr_tick(g_config, g_state);
    ptc::service_log_tick(g_config, g_state);
    ptc::service_ota_tick(g_config, g_state);

    if (g_display_ready) {
        lv_timer_handler();
    }

    uint32_t now = millis();
    if (now - g_last_heartbeat_ms > 5000) {
        g_last_heartbeat_ms = now;
        Serial.printf("[HEARTBEAT] up=%lus display=%d wifi=%d heap=%u\n",
            static_cast<unsigned long>(now / 1000),
            g_display_ready ? 1 : 0,
            g_state.wifi_connected ? 1 : 0,
            static_cast<unsigned int>(ESP.getFreeHeap()));
    }

    if (g_display_ready && ptc::touch_driver_consume_tap_event()) {
        g_last_input_ms = millis();
        if (!ptc::display_driver_is_backlight_on()) {
            ptc::display_driver_set_backlight(true);
            ptc::service_log_add("Display wake");
        } else if (ptc::display_driver_is_backlight_dimmed()) {
            ptc::display_driver_set_backlight_dimmed(false);
            ptc::service_log_add("Display wake");
        }
    }

    uint32_t idle_ms = millis() - g_last_input_ms;
    if (g_display_ready && ptc::display_driver_is_backlight_on()) {
        if (!ptc::display_driver_is_backlight_dimmed() && idle_ms > kScreenDimTimeoutMs) {
            ptc::display_driver_set_backlight_dimmed(true);
            ptc::service_log_add("Display dim");
        }

        if (ptc::display_driver_is_backlight_dimmed() && idle_ms > kScreenOffTimeoutMs) {
            ptc::display_driver_set_backlight(false);
            ptc::service_log_add("Display sleep");
        }
    }

    delay(5);
}
