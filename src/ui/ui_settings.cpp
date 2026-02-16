#include "ui_settings.h"
#include "services/service_storage.h"
#include "services/service_time.h"
#include "services/service_wifi.h"
#include "services/service_http.h"
#include "services/service_ota.h"

#include <Arduino.h>

namespace ptc {

namespace {

struct RotationContext {
    DeviceConfig* config = nullptr;
    lv_obj_t* toast = nullptr;
};

struct SettingsUi {
    lv_obj_t* wifi_value = nullptr;
    lv_obj_t* api_value = nullptr;
    lv_obj_t* ota_value = nullptr;
    lv_obj_t* ota_button = nullptr;
    lv_obj_t* github_value = nullptr;
    lv_obj_t* github_check_button = nullptr;
    lv_obj_t* github_download_button = nullptr;
    lv_obj_t* github_apply_button = nullptr;
    lv_obj_t* toast = nullptr;
};

lv_disp_rot_t rotation_to_lv(uint16_t degrees) {
    switch (degrees) {
        case 90:
            return LV_DISP_ROT_90;
        case 180:
            return LV_DISP_ROT_180;
        case 270:
            return LV_DISP_ROT_270;
        default:
            return LV_DISP_ROT_NONE;
    }
}

uint16_t index_to_rotation(uint16_t index) {
    switch (index) {
        case 1:
            return 90;
        case 2:
            return 180;
        case 3:
            return 270;
        default:
            return 0;
    }
}

void rotation_event_cb(lv_event_t* event) {
    if (lv_event_get_code(event) != LV_EVENT_VALUE_CHANGED) {
        return;
    }

    auto* ctx = static_cast<RotationContext*>(lv_event_get_user_data(event));
    auto* dropdown = lv_event_get_target(event);
    if (!ctx || !ctx->config || !dropdown) {
        return;
    }

    uint16_t selected = lv_dropdown_get_selected(dropdown);
    ctx->config->display_rotation = index_to_rotation(selected);

    lv_disp_t* disp = lv_disp_get_default();
    if (disp) {
        lv_disp_set_rotation(disp, rotation_to_lv(ctx->config->display_rotation));
    }

    service_storage_save_config(*ctx->config);
    if (ctx->toast) {
        lv_label_set_text(ctx->toast, "Rotation saved");
        lv_obj_clear_flag(ctx->toast, LV_OBJ_FLAG_HIDDEN);
        lv_timer_create([](lv_timer_t* timer) {
            auto* label = static_cast<lv_obj_t*>(timer->user_data);
            lv_obj_add_flag(label, LV_OBJ_FLAG_HIDDEN);
            lv_timer_del(timer);
        }, 1500, ctx->toast);
    }
}

String mask_secret(const String& secret) {
    if (secret.length() == 0) {
        return "Not set";
    }
    if (secret.length() <= 4) {
        return "****";
    }
    String tail = secret.substring(secret.length() - 4);
    return String("****") + tail;
}

lv_obj_t* create_field(lv_obj_t* parent, const char* label, const char* value) {
    lv_obj_t* row = lv_obj_create(parent);
    lv_obj_set_width(row, lv_pct(100));
    lv_obj_set_height(row, 64);
    lv_obj_set_style_bg_color(row, lv_color_hex(0x0E1C25), 0);
    lv_obj_set_style_border_color(row, lv_color_hex(0x1C3442), 0);
    lv_obj_set_style_border_width(row, 1, 0);
    lv_obj_set_style_radius(row, 14, 0);
    lv_obj_set_style_pad_hor(row, 14, 0);
    lv_obj_set_style_pad_ver(row, 10, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_COLUMN);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* title = lv_label_create(row);
    lv_label_set_text(title, label);
    lv_obj_set_style_text_color(title, lv_color_hex(0x8FB1C0), 0);

    lv_obj_t* val = lv_label_create(row);
    lv_label_set_text(val, value);
    lv_obj_set_style_text_color(val, lv_color_hex(0xE9F5F9), 0);

    return row;
}

lv_obj_t* create_action(lv_obj_t* parent, const char* text) {
    lv_obj_t* btn = lv_btn_create(parent);
    lv_obj_set_width(btn, lv_pct(100));
    lv_obj_set_height(btn, 60);
    lv_obj_set_style_radius(btn, 16, 0);
    lv_obj_set_style_bg_color(btn, lv_color_hex(0x153040), 0);
    lv_obj_t* label = lv_label_create(btn);
    lv_label_set_text(label, text);
    return btn;
}

} // namespace

void ui_settings_build(lv_obj_t* parent, DeviceConfig& config, AppState& state) {
    lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(parent, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(parent, 16, 0);
    lv_obj_set_style_pad_row(parent, 12, 0);

    lv_obj_t* title = lv_label_create(parent);
    lv_label_set_text(title, "Settings");
    lv_obj_set_style_text_color(title, lv_color_hex(0xE9F5F9), 0);

    const char* device_id = config.device_id.length() ? config.device_id.c_str() : "ESP32S3-XXXX";
    const char* location = config.location_name.length() ? config.location_name.c_str() : "Not assigned";

    static SettingsUi ui;

    create_field(parent, "Device ID", device_id);
    create_field(parent, "Location", location);
    create_field(parent, "Secret", mask_secret(config.device_secret).c_str());

    lv_obj_t* wifi_row = create_field(parent, "Wi-Fi status", state.wifi_connected ? "Online" : "Offline");
    ui.wifi_value = lv_obj_get_child(wifi_row, 1);

    lv_obj_t* rotation_row = lv_obj_create(parent);
    lv_obj_set_width(rotation_row, lv_pct(100));
    lv_obj_set_height(rotation_row, 70);
    lv_obj_set_style_bg_color(rotation_row, lv_color_hex(0x0E1C25), 0);
    lv_obj_set_style_border_color(rotation_row, lv_color_hex(0x1C3442), 0);
    lv_obj_set_style_border_width(rotation_row, 1, 0);
    lv_obj_set_style_radius(rotation_row, 14, 0);
    lv_obj_set_style_pad_hor(rotation_row, 14, 0);
    lv_obj_set_style_pad_ver(rotation_row, 10, 0);
    lv_obj_set_flex_flow(rotation_row, LV_FLEX_FLOW_COLUMN);
    lv_obj_clear_flag(rotation_row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* rotation_label = lv_label_create(rotation_row);
    lv_label_set_text(rotation_label, "Display rotation");
    lv_obj_set_style_text_color(rotation_label, lv_color_hex(0x8FB1C0), 0);

    static RotationContext rotation_ctx;
    rotation_ctx.config = &config;

    lv_obj_t* rotation_dropdown = lv_dropdown_create(rotation_row);
    lv_dropdown_set_options(rotation_dropdown, "0 deg\n90 deg\n180 deg\n270 deg");
    lv_obj_set_width(rotation_dropdown, lv_pct(80));

    uint16_t rotation_index = 0;
    switch (config.display_rotation) {
        case 90:
            rotation_index = 1;
            break;
        case 180:
            rotation_index = 2;
            break;
        case 270:
            rotation_index = 3;
            break;
        default:
            rotation_index = 0;
            break;
    }
    lv_dropdown_set_selected(rotation_dropdown, rotation_index);
    lv_obj_add_event_cb(rotation_dropdown, rotation_event_cb, LV_EVENT_VALUE_CHANGED, &rotation_ctx);

    lv_obj_t* wifi_btn = create_action(parent, LV_SYMBOL_WIFI " Configure Wi-Fi");
    lv_obj_add_event_cb(wifi_btn, [](lv_event_t*) {
        service_wifi_start_portal();
    }, LV_EVENT_CLICKED, nullptr);

    lv_obj_t* time_btn = create_action(parent, LV_SYMBOL_REFRESH " Sync time");
    lv_obj_add_event_cb(time_btn, [](lv_event_t*) {
        service_time_force_sync();
    }, LV_EVENT_CLICKED, nullptr);

    lv_obj_t* interval_row = lv_obj_create(parent);
    lv_obj_set_width(interval_row, lv_pct(100));
    lv_obj_set_height(interval_row, 70);
    lv_obj_set_style_bg_color(interval_row, lv_color_hex(0x0E1C25), 0);
    lv_obj_set_style_border_color(interval_row, lv_color_hex(0x1C3442), 0);
    lv_obj_set_style_border_width(interval_row, 1, 0);
    lv_obj_set_style_radius(interval_row, 14, 0);
    lv_obj_set_style_pad_hor(interval_row, 14, 0);
    lv_obj_set_style_pad_ver(interval_row, 10, 0);
    lv_obj_set_flex_flow(interval_row, LV_FLEX_FLOW_COLUMN);
    lv_obj_clear_flag(interval_row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* interval_label = lv_label_create(interval_row);
    lv_label_set_text(interval_label, "QR refresh interval");
    lv_obj_set_style_text_color(interval_label, lv_color_hex(0x8FB1C0), 0);

    lv_obj_t* dropdown = lv_dropdown_create(interval_row);
    lv_dropdown_set_options(dropdown, "5s\n10s\n20s\n30s");
    lv_obj_set_width(dropdown, lv_pct(80));
    uint16_t interval_index = 2;
    if (config.qr_interval_sec == 5) {
        interval_index = 0;
    } else if (config.qr_interval_sec == 10) {
        interval_index = 1;
    } else if (config.qr_interval_sec == 30) {
        interval_index = 3;
    }
    lv_dropdown_set_selected(dropdown, interval_index);
    lv_obj_add_event_cb(dropdown, [](lv_event_t* event) {
        auto* ctx = static_cast<RotationContext*>(lv_event_get_user_data(event));
        if (!ctx || !ctx->config) {
            return;
        }
        uint16_t selected = lv_dropdown_get_selected(lv_event_get_target(event));
        uint32_t values[] = {5, 10, 20, 30};
        ctx->config->qr_interval_sec = values[selected];
        service_storage_save_config(*ctx->config);
        if (ctx->toast) {
            lv_label_set_text(ctx->toast, "QR interval updated");
            lv_obj_clear_flag(ctx->toast, LV_OBJ_FLAG_HIDDEN);
            lv_timer_create([](lv_timer_t* timer) {
                auto* label = static_cast<lv_obj_t*>(timer->user_data);
                lv_obj_add_flag(label, LV_OBJ_FLAG_HIDDEN);
                lv_timer_del(timer);
            }, 1500, ctx->toast);
        }
    }, LV_EVENT_VALUE_CHANGED, &rotation_ctx);

    lv_obj_t* api_row = create_field(parent, "API endpoint", "Checking...");
    ui.api_value = lv_obj_get_child(api_row, 1);

    lv_obj_t* ota_row = create_field(parent, "OTA updates", "Off");
    ui.ota_value = lv_obj_get_child(ota_row, 1);

    ui.ota_button = create_action(parent, LV_SYMBOL_UPLOAD " Enable OTA");
    lv_obj_add_event_cb(ui.ota_button, [](lv_event_t*) {
        service_ota_request_start();
    }, LV_EVENT_CLICKED, nullptr);

    lv_obj_t* github_row = create_field(parent, "GitHub update", "Idle");
    ui.github_value = lv_obj_get_child(github_row, 1);

    ui.github_check_button = create_action(parent, LV_SYMBOL_REFRESH " Check GitHub update");
    lv_obj_add_event_cb(ui.github_check_button, [](lv_event_t*) {
        service_ota_check_github();
    }, LV_EVENT_CLICKED, nullptr);

    ui.github_download_button = create_action(parent, LV_SYMBOL_DOWNLOAD " Download update");
    lv_obj_add_event_cb(ui.github_download_button, [](lv_event_t*) {
        service_ota_download_github();
    }, LV_EVENT_CLICKED, nullptr);

    ui.github_apply_button = create_action(parent, LV_SYMBOL_WARNING " Reboot to apply");
    lv_obj_add_event_cb(ui.github_apply_button, [](lv_event_t*) {
        service_ota_apply_update();
    }, LV_EVENT_CLICKED, nullptr);
    lv_obj_add_flag(ui.github_apply_button, LV_OBJ_FLAG_HIDDEN);

    ui.toast = lv_label_create(parent);
    lv_label_set_text(ui.toast, "Saved");
    lv_obj_set_style_bg_color(ui.toast, lv_color_hex(0x0A1218), 0);
    lv_obj_set_style_bg_opa(ui.toast, LV_OPA_80, 0);
    lv_obj_set_style_text_color(ui.toast, lv_color_hex(0xE9F5F9), 0);
    lv_obj_set_style_pad_hor(ui.toast, 14, 0);
    lv_obj_set_style_pad_ver(ui.toast, 8, 0);
    lv_obj_set_style_radius(ui.toast, 10, 0);
    lv_obj_add_flag(ui.toast, LV_OBJ_FLAG_HIDDEN);

    rotation_ctx.toast = ui.toast;

    lv_obj_t* revoke_btn = create_action(parent, LV_SYMBOL_WARNING " Revoke device");
    lv_obj_add_event_cb(revoke_btn, [](lv_event_t*) {
        service_wifi_clear_credentials();
        service_storage_clear_all();
        ESP.restart();
    }, LV_EVENT_CLICKED, nullptr);

    lv_timer_create([](lv_timer_t* timer) {
        auto* ui_ptr = static_cast<SettingsUi*>(timer->user_data);
        if (!ui_ptr) {
            return;
        }

        if (ui_ptr->wifi_value) {
            lv_label_set_text(ui_ptr->wifi_value, service_wifi_is_connected() ? "Online" : "Offline");
        }

        if (ui_ptr->api_value) {
            if (service_http_api_ok()) {
                lv_label_set_text(ui_ptr->api_value, "Reachable");
            } else {
                String err = service_http_last_error();
                if (err.length() == 0) {
                    lv_label_set_text(ui_ptr->api_value, "Unreachable");
                } else {
                    lv_label_set_text(ui_ptr->api_value, err.c_str());
                }
            }
        }

        if (ui_ptr->ota_value) {
            if (service_ota_updating()) {
                lv_label_set_text(ui_ptr->ota_value, "Updating...");
            } else if (service_ota_ready()) {
                lv_label_set_text(ui_ptr->ota_value, "Ready");
            } else {
                lv_label_set_text(ui_ptr->ota_value, "Off");
            }
        }

        if (ui_ptr->github_value) {
            String status = service_ota_github_status();
            if (status.length() == 0) {
                status = "Idle";
            }
            lv_label_set_text(ui_ptr->github_value, status.c_str());
        }

        if (ui_ptr->github_apply_button) {
            if (service_ota_update_ready()) {
                lv_obj_clear_flag(ui_ptr->github_apply_button, LV_OBJ_FLAG_HIDDEN);
            } else {
                lv_obj_add_flag(ui_ptr->github_apply_button, LV_OBJ_FLAG_HIDDEN);
            }
        }
    }, 2000, &ui);
}

} // namespace ptc
