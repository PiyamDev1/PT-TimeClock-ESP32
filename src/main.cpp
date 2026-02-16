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

void setup() {
    Serial.begin(115200);
    delay(200);

    ptc::service_storage_init();
    ptc::service_storage_load_config(g_config, g_state);

    ptc::service_wifi_init();
    ptc::service_time_init();
    ptc::service_http_init();
    ptc::service_qr_init();
    ptc::service_log_init();
    ptc::service_ota_init();

    lv_init();

    lv_disp_t* disp = nullptr;
    if (!ptc::display_driver_init(&disp)) {
        Serial.println("Display init failed");
    }

    if (!ptc::touch_driver_init()) {
        Serial.println("Touch init failed");
    } else {
        static lv_indev_drv_t indev_drv;
        lv_indev_drv_init(&indev_drv);
        indev_drv.type = LV_INDEV_TYPE_POINTER;
        indev_drv.read_cb = ptc::touch_driver_read;
        lv_indev_drv_register(&indev_drv);
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

    ptc::ui_root_init(g_config, g_state);
}

void loop() {
    ptc::service_wifi_tick(g_config, g_state);
    ptc::service_time_tick(g_config, g_state);
    ptc::service_http_tick(g_config, g_state);
    ptc::service_qr_tick(g_config, g_state);
    ptc::service_log_tick(g_config, g_state);
    ptc::service_ota_tick(g_config, g_state);

    lv_timer_handler();
    delay(5);
}
