#include "ui_root.h"
#include "ui_qr.h"
#include "ui_notices.h"
#include "ui_log.h"
#include "ui_settings.h"
#include "ui_theme.h"

#include <time.h>

#include "services/service_wifi.h"
#include "services/service_http.h"
#include "services/service_qr.h"
#include "services/service_storage.h"

#include "drivers/touch_driver.h"
#include "drivers/display_driver.h"

#include <Arduino.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <math.h>

namespace ptc {

namespace {

constexpr int kStatusBarHeight = 48;
constexpr int kTabBarHeight = 64;
constexpr uint8_t kCalibrationPointCount = 30;
constexpr uint8_t kCalibrationGridCols = 6;
constexpr uint8_t kCalibrationGridRows = 5;
constexpr uint32_t kSetupActionDebounceMs = 350;

struct StatusUi {
    lv_obj_t* wifi = nullptr;
    lv_obj_t* time_label = nullptr;
    lv_obj_t* sync = nullptr;
    lv_obj_t* qr = nullptr;
    AppState* state = nullptr;
};

struct SetupUi {
    lv_obj_t* overlay = nullptr;
    lv_obj_t* tabview = nullptr;
    lv_obj_t* card = nullptr;
    lv_obj_t* title = nullptr;
    lv_obj_t* subtitle = nullptr;
    lv_obj_t* action = nullptr;
    lv_obj_t* action_label = nullptr;
    lv_obj_t* secondary = nullptr;
    lv_obj_t* secondary_label = nullptr;
    lv_obj_t* spinner = nullptr;
    lv_obj_t* progress = nullptr;
    lv_obj_t* keyboard_overlay = nullptr;
    lv_obj_t* network_dropdown = nullptr;
    lv_obj_t* network_scan = nullptr;
    lv_obj_t* ssid_input = nullptr;
    lv_obj_t* pass_input = nullptr;
    lv_obj_t* pass_visibility = nullptr;
    lv_obj_t* keyboard = nullptr;
    lv_obj_t* keyboard_connect = nullptr;
    lv_obj_t* calibration_overlay = nullptr;
    lv_obj_t* calibration_hint = nullptr;
    lv_obj_t* calibration_target = nullptr;
    lv_timer_t* calibration_timer = nullptr;
    AppState* state = nullptr;
    bool setup_started = false;
    bool setup_dismissed = false;
    bool calibration_complete = false;
    bool calibration_touch_held = false;
    bool wifi_scan_in_progress = false;
    bool wifi_scan_fallback_used = false;
    bool display_suspended_for_wifi = false;
    uint32_t success_started_ms = 0;
    uint32_t connect_started_ms = 0;
    uint32_t wifi_scan_started_ms = 0;
    uint32_t wifi_resume_deadline_ms = 0;
    uint32_t last_action_ms = 0;
    uint16_t calibration_raw_x[kCalibrationPointCount] = {0};
    uint16_t calibration_raw_y[kCalibrationPointCount] = {0};
    int32_t calibration_target_x[kCalibrationPointCount] = {0};
    int32_t calibration_target_y[kCalibrationPointCount] = {0};
    uint8_t step = 0;
    uint8_t calibration_step = 0;
};

SetupUi* g_setup_ui = nullptr;

enum SetupStep : uint8_t {
    kStepHidden = 0,
    kStepCalibrate,
    kStepWelcome,
    kStepWifi,
    kStepConnecting,
    kStepRegistering,
    kStepRegistrationError,
    kStepSuccess,
    kStepError,
};

void apply_setup_step(SetupUi& ui, SetupStep step);

void init_styles(lv_style_t& screen, lv_style_t& status, lv_style_t& tab_btns) {
    lv_style_init(&screen);
    lv_style_set_bg_color(&screen, theme::black());
    lv_style_set_bg_opa(&screen, LV_OPA_COVER);
    lv_style_set_text_color(&screen, theme::white());

    lv_style_init(&status);
    lv_style_set_bg_color(&status, theme::maroon());
    lv_style_set_text_color(&status, theme::white());
    lv_style_set_pad_hor(&status, 16);
    lv_style_set_pad_ver(&status, 6);

    lv_style_init(&tab_btns);
    lv_style_set_bg_color(&tab_btns, theme::dark_grey());
    lv_style_set_text_color(&tab_btns, theme::text_soft());
    lv_style_set_pad_ver(&tab_btns, 10);
    lv_style_set_pad_hor(&tab_btns, 8);
}

void status_timer_cb(lv_timer_t* timer) {
    auto* ui = static_cast<StatusUi*>(timer->user_data);
    if (!ui || !ui->state) {
        return;
    }

    if (ui->wifi) {
        lv_label_set_text(ui->wifi, ui->state->wifi_connected ? LV_SYMBOL_WIFI "  Online" : LV_SYMBOL_WIFI "  Offline");
    }

    if (ui->sync) {
        lv_label_set_text(ui->sync, ui->state->time_sync_ok ? LV_SYMBOL_OK "  NTP" : LV_SYMBOL_REFRESH "  NTP");
    }

    if (ui->time_label) {
        if (ui->state->time_sync_ok) {
            time_t now = time(nullptr);
            struct tm* tm_info = localtime(&now);
            char buf[16];
            if (tm_info) {
                strftime(buf, sizeof(buf), "%H:%M", tm_info);
                lv_label_set_text(ui->time_label, buf);
            }
        } else {
            lv_label_set_text(ui->time_label, "--:--");
        }
    }

    if (ui->qr) {
        if (!ui->state->device_active) {
            lv_label_set_text(ui->qr, "QR Off");
        } else {
            uint32_t remain = service_qr_seconds_remaining();
            char buf[16];
            snprintf(buf, sizeof(buf), "QR %lus", static_cast<unsigned long>(remain));
            lv_label_set_text(ui->qr, buf);
        }
    }
}

void animate_shake(lv_obj_t* target) {
    LV_UNUSED(target);
}

void show_keyboard(SetupUi& ui, bool show) {
    if (!ui.keyboard_overlay) {
        return;
    }
    if (show) {
        if (ui.overlay) {
            lv_obj_add_flag(ui.overlay, LV_OBJ_FLAG_HIDDEN);
        }
        lv_obj_clear_flag(ui.keyboard_overlay, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(ui.keyboard_overlay, LV_OBJ_FLAG_HIDDEN);
        if (ui.overlay) {
            if (ui.state && ui.state->provisioning_complete && ui.setup_dismissed) {
                lv_obj_add_flag(ui.overlay, LV_OBJ_FLAG_HIDDEN);
            } else {
                lv_obj_clear_flag(ui.overlay, LV_OBJ_FLAG_HIDDEN);
            }
        }
    }
}

void apply_wifi_network_options(SetupUi& ui, int16_t count) {
    if (!ui.network_dropdown) {
        return;
    }

    String options = "Manual SSID";
    Serial.printf("[WIFI] scan complete networks=%d\n", static_cast<int>(count));
    if (count > 0) {
        for (int16_t i = 0; i < count; ++i) {
            String ssid = WiFi.SSID(i);
            if (ssid.length() == 0) {
                continue;
            }
            Serial.printf("[WIFI] scan ssid=%s rssi=%lddBm channel=%ld\n",
                ssid.c_str(),
                static_cast<long>(WiFi.RSSI(i)),
                static_cast<long>(WiFi.channel(i)));
            options += "\n";
            options += ssid;
        }
    }
    if (options == "Manual SSID") {
        options += "\nNo networks found";
    }

    lv_dropdown_set_options(ui.network_dropdown, options.c_str());
    lv_dropdown_set_selected(ui.network_dropdown, 0);
}

void begin_wifi_heavy_operation(SetupUi& ui, const char* title, const char* subtitle) {
    if (ui.title) {
        lv_label_set_text(ui.title, title);
    }
    if (ui.subtitle) {
        lv_label_set_text(ui.subtitle, subtitle);
    }
    if (ui.action) {
        lv_obj_add_flag(ui.action, LV_OBJ_FLAG_HIDDEN);
    }
    if (ui.secondary) {
        lv_obj_add_flag(ui.secondary, LV_OBJ_FLAG_HIDDEN);
    }
    if (ui.spinner) {
        lv_obj_clear_flag(ui.spinner, LV_OBJ_FLAG_HIDDEN);
    }
    if (ui.progress) {
        lv_obj_clear_flag(ui.progress, LV_OBJ_FLAG_HIDDEN);
    }
    if (ui.overlay) {
        lv_obj_clear_flag(ui.overlay, LV_OBJ_FLAG_HIDDEN);
    }
}

void end_wifi_heavy_operation(SetupUi& ui) {
    if (!ui.display_suspended_for_wifi) {
        return;
    }
    display_driver_set_render_enabled(true);
    ui.display_suspended_for_wifi = false;
    ui.wifi_resume_deadline_ms = 0;
}

void refresh_wifi_networks(SetupUi& ui) {
    if (!ui.network_dropdown || ui.wifi_scan_in_progress) {
        return;
    }

    begin_wifi_heavy_operation(ui, "Scanning Wi-Fi", "Looking for nearby networks...");
    lv_dropdown_set_options(ui.network_dropdown, "Scanning...");

    WiFi.mode(WIFI_STA);
    WiFi.scanDelete();
    ui.wifi_scan_in_progress = true;
    ui.wifi_scan_fallback_used = false;
    int16_t result = WiFi.scanNetworks(true, true);
    if (result >= 0) {
        apply_wifi_network_options(ui, result);
        WiFi.scanDelete();
        ui.wifi_scan_in_progress = false;
        end_wifi_heavy_operation(ui);
    }
}

void cancel_wifi_scan(SetupUi& ui) {
    if (!ui.wifi_scan_in_progress) {
        return;
    }
    esp_wifi_scan_stop();
    WiFi.scanDelete();
    ui.wifi_scan_in_progress = false;
    ui.wifi_scan_fallback_used = false;
}

void poll_wifi_networks(SetupUi& ui) {
    if (!ui.wifi_scan_in_progress) {
        return;
    }

    int16_t result = WiFi.scanComplete();
    if (result == WIFI_SCAN_RUNNING) {
        return;
    }

    if (result == WIFI_SCAN_FAILED && !ui.wifi_scan_fallback_used) {
        WiFi.scanDelete();
        ui.wifi_scan_fallback_used = true;
        WiFi.scanNetworks(true, false);
        return;
    }

    apply_wifi_network_options(ui, result > 0 ? result : 0);
    WiFi.scanDelete();
    ui.wifi_scan_in_progress = false;
    ui.wifi_scan_fallback_used = false;
    end_wifi_heavy_operation(ui);
}

void calibration_update_target(SetupUi& ui) {
    if (!ui.calibration_target || !ui.calibration_hint) {
        return;
    }

    lv_disp_t* disp = lv_disp_get_default();
    int32_t w = disp ? lv_disp_get_hor_res(disp) : 480;
    int32_t h = disp ? lv_disp_get_ver_res(disp) : 800;
    uint8_t idx = ui.calibration_step;
    if (idx >= kCalibrationPointCount) {
        idx = kCalibrationPointCount - 1;
    }

    uint8_t col = idx % kCalibrationGridCols;
    uint8_t row = idx / kCalibrationGridCols;
    int32_t x_percent = 8 + (84 * col) / (kCalibrationGridCols - 1);
    int32_t y_percent = 8 + (84 * row) / (kCalibrationGridRows - 1);

    int32_t tx = (w * x_percent) / 100;
    int32_t ty = (h * y_percent) / 100;

    ui.calibration_target_x[idx] = tx;
    ui.calibration_target_y[idx] = ty;
    lv_obj_set_pos(ui.calibration_target, tx - 10, ty - 10);

    char hint[40];
    snprintf(hint, sizeof(hint), "Tap X (%u/%u)", static_cast<unsigned>(idx + 1), static_cast<unsigned>(kCalibrationPointCount));
    lv_label_set_text(ui.calibration_hint, hint);
}

void calibration_finish(SetupUi& ui) {
    auto solve_affine_axis = [](const uint16_t* raw_x, const uint16_t* raw_y, const int32_t* target,
                                float& a, float& b, float& c) {
        float s_xx = 0.0f;
        float s_xy = 0.0f;
        float s_x1 = 0.0f;
        float s_yy = 0.0f;
        float s_y1 = 0.0f;
        float s_xt = 0.0f;
        float s_yt = 0.0f;
        float s_1t = 0.0f;

        for (int i = 0; i < kCalibrationPointCount; ++i) {
            float x = static_cast<float>(raw_x[i]);
            float y = static_cast<float>(raw_y[i]);
            float t = static_cast<float>(target[i]);
            s_xx += x * x;
            s_xy += x * y;
            s_x1 += x;
            s_yy += y * y;
            s_y1 += y;
            s_xt += x * t;
            s_yt += y * t;
            s_1t += t;
        }

        float m[3][4] = {
            {s_xx, s_xy, s_x1, s_xt},
            {s_xy, s_yy, s_y1, s_yt},
            {s_x1, s_y1, static_cast<float>(kCalibrationPointCount), s_1t},
        };

        for (int col = 0; col < 3; ++col) {
            int pivot = col;
            float pivot_abs = fabsf(m[col][col]);
            for (int row = col + 1; row < 3; ++row) {
                float v = fabsf(m[row][col]);
                if (v > pivot_abs) {
                    pivot = row;
                    pivot_abs = v;
                }
            }

            if (pivot_abs < 0.0001f) {
                return false;
            }

            if (pivot != col) {
                for (int k = col; k < 4; ++k) {
                    float tmp = m[col][k];
                    m[col][k] = m[pivot][k];
                    m[pivot][k] = tmp;
                }
            }

            float div = m[col][col];
            for (int k = col; k < 4; ++k) {
                m[col][k] /= div;
            }

            for (int row = 0; row < 3; ++row) {
                if (row == col) {
                    continue;
                }
                float factor = m[row][col];
                for (int k = col; k < 4; ++k) {
                    m[row][k] -= factor * m[col][k];
                }
            }
        }

        a = m[0][3];
        b = m[1][3];
        c = m[2][3];
        return true;
    };

    auto affine_error = [](const uint16_t* raw_x, const uint16_t* raw_y, const int32_t* target_x, const int32_t* target_y,
                           float ax, float bx, float cx, float ay, float by, float cy) {
        float error = 0.0f;
        for (int i = 0; i < kCalibrationPointCount; ++i) {
            float rx = static_cast<float>(raw_x[i]);
            float ry = static_cast<float>(raw_y[i]);
            float px = ax * rx + bx * ry + cx;
            float py = ay * rx + by * ry + cy;
            float dx = px - static_cast<float>(target_x[i]);
            float dy = py - static_cast<float>(target_y[i]);
            error += dx * dx + dy * dy;
        }
        return error;
    };

    auto fit_line = [](const uint16_t* source, const int32_t* target, float& scale, float& offset) {
        const float n = static_cast<float>(kCalibrationPointCount);
        float sum_x = 0.0f;
        float sum_y = 0.0f;
        float sum_xx = 0.0f;
        float sum_xy = 0.0f;

        for (int i = 0; i < kCalibrationPointCount; ++i) {
            float x = static_cast<float>(source[i]);
            float y = static_cast<float>(target[i]);
            sum_x += x;
            sum_y += y;
            sum_xx += x * x;
            sum_xy += x * y;
        }

        float denom = n * sum_xx - sum_x * sum_x;
        if (fabsf(denom) < 0.0001f) {
            scale = 1.0f;
            offset = 0.0f;
            return;
        }

        scale = (n * sum_xy - sum_x * sum_y) / denom;
        offset = (sum_y - scale * sum_x) / n;
    };

    auto fit_error = [](const uint16_t* source_x, const uint16_t* source_y, const int32_t* target_x, const int32_t* target_y,
                        float sx, float ox, float sy, float oy) {
        float error = 0.0f;
        for (int i = 0; i < kCalibrationPointCount; ++i) {
            float px = sx * static_cast<float>(source_x[i]) + ox;
            float py = sy * static_cast<float>(source_y[i]) + oy;
            float dx = px - static_cast<float>(target_x[i]);
            float dy = py - static_cast<float>(target_y[i]);
            error += dx * dx + dy * dy;
        }
        return error;
    };

    float sx_no_swap = 1.0f;
    float ox_no_swap = 0.0f;
    float sy_no_swap = 1.0f;
    float oy_no_swap = 0.0f;
    fit_line(ui.calibration_raw_x, ui.calibration_target_x, sx_no_swap, ox_no_swap);
    fit_line(ui.calibration_raw_y, ui.calibration_target_y, sy_no_swap, oy_no_swap);
    float err_no_swap = fit_error(ui.calibration_raw_x, ui.calibration_raw_y, ui.calibration_target_x, ui.calibration_target_y,
                                  sx_no_swap, ox_no_swap, sy_no_swap, oy_no_swap);

    float sx_swap = 1.0f;
    float ox_swap = 0.0f;
    float sy_swap = 1.0f;
    float oy_swap = 0.0f;
    fit_line(ui.calibration_raw_y, ui.calibration_target_x, sx_swap, ox_swap);
    fit_line(ui.calibration_raw_x, ui.calibration_target_y, sy_swap, oy_swap);
    float err_swap = fit_error(ui.calibration_raw_y, ui.calibration_raw_x, ui.calibration_target_x, ui.calibration_target_y,
                               sx_swap, ox_swap, sy_swap, oy_swap);

    float aff_xx = 1.0f;
    float aff_xy = 0.0f;
    float aff_x0 = 0.0f;
    float aff_yx = 0.0f;
    float aff_yy = 1.0f;
    float aff_y0 = 0.0f;
    bool affine_ok_x = solve_affine_axis(ui.calibration_raw_x, ui.calibration_raw_y, ui.calibration_target_x, aff_xx, aff_xy, aff_x0);
    bool affine_ok_y = solve_affine_axis(ui.calibration_raw_x, ui.calibration_raw_y, ui.calibration_target_y, aff_yx, aff_yy, aff_y0);
    bool affine_ok = affine_ok_x && affine_ok_y;
    float err_affine = affine_ok
        ? affine_error(ui.calibration_raw_x, ui.calibration_raw_y, ui.calibration_target_x, ui.calibration_target_y,
                       aff_xx, aff_xy, aff_x0, aff_yx, aff_yy, aff_y0)
        : INFINITY;

    bool use_swap = err_swap < err_no_swap;
    float err_decoupled = use_swap ? err_swap : err_no_swap;

    TouchCalibration calibration;
    calibration.valid = true;
    calibration.swap_xy = use_swap;
    calibration.scale_x = calibration.swap_xy ? sx_swap : sx_no_swap;
    calibration.offset_x = calibration.swap_xy ? ox_swap : ox_no_swap;
    calibration.scale_y = calibration.swap_xy ? sy_swap : sy_no_swap;
    calibration.offset_y = calibration.swap_xy ? oy_swap : oy_no_swap;
    calibration.use_affine = affine_ok && (err_affine < err_decoupled);
    calibration.affine_xx = aff_xx;
    calibration.affine_xy = aff_xy;
    calibration.affine_x0 = aff_x0;
    calibration.affine_yx = aff_yx;
    calibration.affine_yy = aff_yy;
    calibration.affine_y0 = aff_y0;

    uint16_t raw_x_min = ui.calibration_raw_x[0];
    uint16_t raw_x_max = ui.calibration_raw_x[0];
    uint16_t raw_y_min = ui.calibration_raw_y[0];
    uint16_t raw_y_max = ui.calibration_raw_y[0];
    for (int i = 1; i < kCalibrationPointCount; ++i) {
        raw_x_min = min(raw_x_min, ui.calibration_raw_x[i]);
        raw_x_max = max(raw_x_max, ui.calibration_raw_x[i]);
        raw_y_min = min(raw_y_min, ui.calibration_raw_y[i]);
        raw_y_max = max(raw_y_max, ui.calibration_raw_y[i]);
    }
    calibration.raw_min_x = raw_x_min;
    calibration.raw_max_x = raw_x_max;
    calibration.raw_min_y = raw_y_min;
    calibration.raw_max_y = raw_y_max;
    calibration.invert_x = false;
    calibration.invert_y = false;

    touch_driver_set_calibration(calibration);
    service_storage_save_touch_calibration(calibration);

    Serial.printf("[CAL] fit mode=%s swap=%d sx=%.6f ox=%.2f sy=%.6f oy=%.2f err_no_swap=%.2f err_swap=%.2f err_affine=%.2f\n",
        calibration.use_affine ? "affine" : "decoupled",
        calibration.swap_xy ? 1 : 0,
        calibration.scale_x,
        calibration.offset_x,
        calibration.scale_y,
        calibration.offset_y,
        err_no_swap,
        err_swap,
        err_affine);
    if (calibration.use_affine) {
        Serial.printf("[CAL] affine xx=%.6f xy=%.6f x0=%.2f yx=%.6f yy=%.6f y0=%.2f\n",
            calibration.affine_xx,
            calibration.affine_xy,
            calibration.affine_x0,
            calibration.affine_yx,
            calibration.affine_yy,
            calibration.affine_y0);
    }

    Serial.printf("[CAL] %u-point capture complete\n", static_cast<unsigned>(kCalibrationPointCount));
    for (uint8_t i = 0; i < kCalibrationPointCount; ++i) {
        Serial.printf("[CAL] %u target=(%ld,%ld) raw=(%u,%u)\n",
            static_cast<unsigned>(i + 1),
            static_cast<long>(ui.calibration_target_x[i]),
            static_cast<long>(ui.calibration_target_y[i]),
            static_cast<unsigned>(ui.calibration_raw_x[i]),
            static_cast<unsigned>(ui.calibration_raw_y[i]));
    }
    ui.calibration_complete = true;

    if (ui.calibration_overlay) {
        lv_obj_add_flag(ui.calibration_overlay, LV_OBJ_FLAG_HIDDEN);
    }

    apply_setup_step(ui, kStepWelcome);
}

void calibration_timer_cb(lv_timer_t* timer) {
    auto* ui = static_cast<SetupUi*>(timer->user_data);
    if (!ui || !ui->calibration_overlay || lv_obj_has_flag(ui->calibration_overlay, LV_OBJ_FLAG_HIDDEN)) {
        return;
    }

    uint16_t x = 0;
    uint16_t y = 0;
    bool pressed = false;
    if (!touch_driver_poll_raw(x, y, pressed)) {
        return;
    }

    if (pressed && !ui->calibration_touch_held) {
        ui->calibration_touch_held = true;
        ui->calibration_raw_x[ui->calibration_step] = x;
        ui->calibration_raw_y[ui->calibration_step] = y;
        Serial.printf("[CAL] tap %u target=(%ld,%ld) raw=(%u,%u)\n",
            static_cast<unsigned>(ui->calibration_step + 1),
            static_cast<long>(ui->calibration_target_x[ui->calibration_step]),
            static_cast<long>(ui->calibration_target_y[ui->calibration_step]),
            static_cast<unsigned>(x),
            static_cast<unsigned>(y));
    }

    if (!pressed && ui->calibration_touch_held) {
        ui->calibration_touch_held = false;
        if (ui->calibration_step < (kCalibrationPointCount - 1)) {
            ui->calibration_step++;
            calibration_update_target(*ui);
        } else {
            calibration_finish(*ui);
        }
    }
}

void calibration_start(SetupUi& ui) {
    ui.calibration_step = 0;
    ui.calibration_touch_held = false;
    Serial.printf("[CAL] start %u-point capture\n", static_cast<unsigned>(kCalibrationPointCount));
    if (ui.calibration_overlay) {
        lv_obj_clear_flag(ui.calibration_overlay, LV_OBJ_FLAG_HIDDEN);
    }
    calibration_update_target(ui);
}

void apply_setup_step(SetupUi& ui, SetupStep step) {
    if (ui.step == step && step != kStepConnecting) {
        return;
    }
    ui.step = step;
    switch (step) {
        case kStepCalibrate:
            lv_label_set_text(ui.title, "Touch calibration");
            lv_label_set_text(ui.subtitle, "Calibrate touch before Wi-Fi setup.");
            lv_label_set_text(ui.action_label, "Start calibration");
            lv_obj_clear_flag(ui.action, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(ui.secondary, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(ui.spinner, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(ui.progress, LV_OBJ_FLAG_HIDDEN);
            break;
        case kStepWelcome:
            lv_label_set_text(ui.title, "Timeclock setup");
            lv_label_set_text(ui.subtitle, "Enter Wi-Fi details to continue.");
            lv_label_set_text(ui.action_label, "Start setup");
            lv_obj_clear_flag(ui.action, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(ui.secondary, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(ui.spinner, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(ui.progress, LV_OBJ_FLAG_HIDDEN);
            break;
        case kStepWifi:
            lv_label_set_text(ui.title, "Connect to Wi-Fi");
            lv_label_set_text(ui.subtitle, "Open Wi-Fi setup and join your network.");
            lv_label_set_text(ui.action_label, "Open Wi-Fi setup");
            lv_obj_clear_flag(ui.action, LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(ui.secondary, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(ui.spinner, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(ui.progress, LV_OBJ_FLAG_HIDDEN);
            break;
        case kStepConnecting:
            lv_label_set_text(ui.title, "Connecting...");
            lv_label_set_text(ui.subtitle, "This may take a few seconds.");
            lv_obj_add_flag(ui.action, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(ui.secondary, LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(ui.spinner, LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(ui.progress, LV_OBJ_FLAG_HIDDEN);
            {
                const uint32_t elapsed = millis() - ui.connect_started_ms;
                const uint32_t progress_step = elapsed / 450;
                const uint32_t progress = 20 + (progress_step > 65 ? 65 : progress_step);
                lv_bar_set_value(ui.progress, static_cast<int32_t>(progress), LV_ANIM_OFF);
            }
            break;
        case kStepRegistering:
            lv_label_set_text(ui.title, "Registering device");
            lv_label_set_text(ui.subtitle, "Linking to your branch...");
            lv_obj_add_flag(ui.action, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(ui.secondary, LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(ui.spinner, LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(ui.progress, LV_OBJ_FLAG_HIDDEN);
            lv_bar_set_value(ui.progress, 65, LV_ANIM_OFF);
            break;
        case kStepRegistrationError:
            lv_label_set_text(ui.title, "Registration unavailable");
            lv_label_set_text(ui.subtitle, service_http_last_error().c_str());
            lv_label_set_text(ui.action_label, "Retry registration");
            lv_label_set_text(ui.secondary_label, "Edit Wi-Fi details");
            lv_obj_clear_flag(ui.action, LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(ui.secondary, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(ui.spinner, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(ui.progress, LV_OBJ_FLAG_HIDDEN);
            break;
        case kStepSuccess:
            lv_label_set_text(ui.title, "Setup complete");
            lv_label_set_text(ui.subtitle, "Device is ready to use.");
            lv_label_set_text(ui.action_label, "Go to Timeclock");
            lv_obj_clear_flag(ui.action, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(ui.secondary, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(ui.spinner, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(ui.progress, LV_OBJ_FLAG_HIDDEN);
            break;
        case kStepError:
            lv_label_set_text(ui.title, "Setup failed");
            lv_label_set_text(ui.subtitle, service_wifi_connection_message().c_str());
            lv_label_set_text(ui.action_label, "Retry");
            lv_label_set_text(ui.secondary_label, "Edit Wi-Fi details");
            lv_obj_clear_flag(ui.action, LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(ui.secondary, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(ui.spinner, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(ui.progress, LV_OBJ_FLAG_HIDDEN);
            animate_shake(ui.card);
            break;
        case kStepHidden:
        default:
            break;
    }
}

void setup_timer_cb(lv_timer_t* timer) {
    auto* ui = static_cast<SetupUi*>(timer->user_data);
    if (!ui || !ui->state || !ui->overlay) {
        return;
    }

    if (ui->display_suspended_for_wifi && ui->wifi_resume_deadline_ms > 0) {
        if (ui->state->wifi_connected || !service_wifi_is_connecting() || millis() > ui->wifi_resume_deadline_ms) {
            end_wifi_heavy_operation(*ui);
        }
    }

    poll_wifi_networks(*ui);

    if (ui->keyboard_overlay && !lv_obj_has_flag(ui->keyboard_overlay, LV_OBJ_FLAG_HIDDEN)) {
        return;
    }

    if (ui->state->provisioning_complete) {
        if (ui->setup_dismissed) {
            lv_obj_add_flag(ui->overlay, LV_OBJ_FLAG_HIDDEN);
            if (ui->tabview) {
                lv_obj_clear_flag(ui->tabview, LV_OBJ_FLAG_HIDDEN);
            }
            return;
        }
        lv_obj_clear_flag(ui->overlay, LV_OBJ_FLAG_HIDDEN);
        if (ui->step != kStepSuccess) {
            ui->success_started_ms = millis();
            apply_setup_step(*ui, kStepSuccess);
        }
        return;
    }

    lv_obj_clear_flag(ui->overlay, LV_OBJ_FLAG_HIDDEN);
    if (!ui->state->wifi_connected) {
        if (service_wifi_portal_active()) {
            apply_setup_step(*ui, kStepWifi);
        } else if (service_wifi_is_connecting()) {
            apply_setup_step(*ui, kStepConnecting);
        } else if (ui->setup_started) {
            if (millis() - ui->connect_started_ms < 20000) {
                apply_setup_step(*ui, kStepConnecting);
                return;
            }
            apply_setup_step(*ui, kStepError);
        } else {
            apply_setup_step(*ui, kStepWelcome);
        }
        return;
    }

    if (!service_http_registration_in_progress() && !service_http_last_error().isEmpty()) {
        apply_setup_step(*ui, kStepRegistrationError);
    } else {
        apply_setup_step(*ui, kStepRegistering);
    }
}

} // namespace

void ui_root_init(DeviceConfig& config, AppState& state) {
    static lv_style_t style_screen;
    static lv_style_t style_status;
    static lv_style_t style_tab_btns;

    lv_disp_t* display = lv_disp_get_default();
    lv_theme_t* default_theme = lv_theme_default_init(
        display, theme::maroon(), theme::maroon_light(), true, LV_FONT_DEFAULT);
    lv_disp_set_theme(display, default_theme);
    init_styles(style_screen, style_status, style_tab_btns);

    lv_obj_t* screen = lv_scr_act();
    int32_t screen_w = lv_disp_get_hor_res(NULL);
    int32_t screen_h = lv_disp_get_ver_res(NULL);
    lv_obj_add_style(screen, &style_screen, 0);

    lv_obj_t* status = lv_obj_create(screen);
    lv_obj_add_style(status, &style_status, 0);
    lv_obj_clear_flag(status, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(status, screen_w, kStatusBarHeight);
    lv_obj_align(status, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_flex_flow(status, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(status, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    static StatusUi status_ui;
    status_ui.state = &state;

    status_ui.wifi = lv_label_create(status);
    lv_label_set_text(status_ui.wifi, LV_SYMBOL_WIFI "  Offline");

    status_ui.time_label = lv_label_create(status);
    lv_label_set_text(status_ui.time_label, "--:--");

    status_ui.sync = lv_label_create(status);
    lv_label_set_text(status_ui.sync, LV_SYMBOL_REFRESH "  NTP");

    status_ui.qr = lv_label_create(status);
    lv_label_set_text(status_ui.qr, "QR --");

    lv_obj_t* tabview = lv_tabview_create(screen, LV_DIR_BOTTOM, kTabBarHeight);
    lv_obj_set_size(tabview,
        screen_w,
        screen_h - kStatusBarHeight);
    lv_obj_align(tabview, LV_ALIGN_BOTTOM_MID, 0, 0);

    lv_obj_t* tab_btns = lv_tabview_get_tab_btns(tabview);
    lv_obj_add_style(tab_btns, &style_tab_btns, 0);
    lv_obj_set_style_bg_color(tab_btns, theme::maroon(), LV_PART_ITEMS | LV_STATE_CHECKED);
    lv_obj_set_style_text_color(tab_btns, theme::white(), LV_PART_ITEMS | LV_STATE_CHECKED);

    lv_obj_t* tab_qr = lv_tabview_add_tab(tabview, LV_SYMBOL_IMAGE "\nQR");
    lv_obj_t* tab_notices = lv_tabview_add_tab(tabview, LV_SYMBOL_BELL "\nNotices");
    lv_obj_t* tab_log = lv_tabview_add_tab(tabview, LV_SYMBOL_LIST "\nLog");
    lv_obj_t* tab_settings = lv_tabview_add_tab(tabview, LV_SYMBOL_SETTINGS "\nSettings");

    ui_qr_build(tab_qr, config, state);
    ui_notices_build(tab_notices, config, state);
    ui_log_build(tab_log, config, state);
    ui_settings_build(tab_settings, config, state);

    lv_timer_create(status_timer_cb, 1000, &status_ui);

    static SetupUi setup_ui;
    g_setup_ui = &setup_ui;
    setup_ui.state = &state;
    setup_ui.tabview = tabview;
    setup_ui.calibration_complete = true;

    setup_ui.overlay = lv_obj_create(screen);
    lv_obj_set_size(setup_ui.overlay, screen_w, screen_h);
    lv_obj_set_style_bg_color(setup_ui.overlay, theme::black(), 0);
    lv_obj_set_style_bg_opa(setup_ui.overlay, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(setup_ui.overlay, 0, 0);
    lv_obj_set_style_radius(setup_ui.overlay, 0, 0);
    lv_obj_set_style_pad_all(setup_ui.overlay, 32, 0);
    lv_obj_set_flex_flow(setup_ui.overlay, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(setup_ui.overlay, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(setup_ui.overlay, LV_OBJ_FLAG_SCROLLABLE);

    setup_ui.card = setup_ui.overlay;

    setup_ui.title = lv_label_create(setup_ui.card);
    lv_label_set_text(setup_ui.title, "Timeclock setup");
    lv_obj_set_style_text_color(setup_ui.title, theme::white(), 0);
    lv_obj_set_style_text_font(setup_ui.title, LV_FONT_DEFAULT, 0);

    setup_ui.subtitle = lv_label_create(setup_ui.card);
    lv_label_set_text(setup_ui.subtitle, "Connect to Wi-Fi to continue.");
    lv_obj_set_style_text_color(setup_ui.subtitle, theme::text_muted(), 0);

    setup_ui.action = lv_btn_create(setup_ui.card);
    lv_obj_set_width(setup_ui.action, 220);
    lv_obj_set_height(setup_ui.action, 48);
    lv_obj_set_style_radius(setup_ui.action, 12, 0);
    lv_obj_set_style_bg_color(setup_ui.action, theme::maroon(), 0);
    setup_ui.action_label = lv_label_create(setup_ui.action);
    lv_label_set_text(setup_ui.action_label, "Start setup");

    setup_ui.secondary = lv_btn_create(setup_ui.card);
    lv_obj_set_width(setup_ui.secondary, 220);
    lv_obj_set_height(setup_ui.secondary, 44);
    lv_obj_set_style_radius(setup_ui.secondary, 12, 0);
    lv_obj_set_style_bg_color(setup_ui.secondary, theme::dark_grey(), 0);
    setup_ui.secondary_label = lv_label_create(setup_ui.secondary);
    lv_label_set_text(setup_ui.secondary_label, "Use on-device keyboard");
    lv_obj_add_flag(setup_ui.secondary, LV_OBJ_FLAG_HIDDEN);

    setup_ui.spinner = lv_label_create(setup_ui.card);
    lv_label_set_text(setup_ui.spinner, LV_SYMBOL_REFRESH);
    lv_obj_set_style_text_color(setup_ui.spinner, theme::white(), 0);
    lv_obj_set_size(setup_ui.spinner, 48, 48);
    lv_obj_set_style_text_align(setup_ui.spinner, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_add_flag(setup_ui.spinner, LV_OBJ_FLAG_HIDDEN);

    setup_ui.progress = lv_bar_create(setup_ui.card);
    lv_obj_set_size(setup_ui.progress, 260, 10);
    lv_obj_add_flag(setup_ui.progress, LV_OBJ_FLAG_HIDDEN);
    lv_bar_set_range(setup_ui.progress, 0, 100);
    lv_bar_set_value(setup_ui.progress, 25, LV_ANIM_OFF);

    setup_ui.keyboard_overlay = lv_obj_create(screen);
    lv_obj_set_size(setup_ui.keyboard_overlay, screen_w, screen_h);
    lv_obj_set_style_bg_color(setup_ui.keyboard_overlay, theme::black(), 0);
    lv_obj_set_style_bg_opa(setup_ui.keyboard_overlay, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(setup_ui.keyboard_overlay, 0, 0);
    lv_obj_set_style_radius(setup_ui.keyboard_overlay, 0, 0);
    lv_obj_set_style_pad_all(setup_ui.keyboard_overlay, 20, 0);
    lv_obj_set_style_pad_row(setup_ui.keyboard_overlay, 10, 0);
    lv_obj_set_flex_flow(setup_ui.keyboard_overlay, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(setup_ui.keyboard_overlay, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(setup_ui.keyboard_overlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(setup_ui.keyboard_overlay, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t* kb_card = setup_ui.keyboard_overlay;
    int32_t kb_card_w = screen_w;

    lv_obj_t* kb_title = lv_label_create(kb_card);
    lv_label_set_text(kb_title, "Manual Wi-Fi setup");
    lv_obj_set_style_text_color(kb_title, theme::white(), 0);

    setup_ui.network_dropdown = lv_dropdown_create(kb_card);
    lv_obj_set_width(setup_ui.network_dropdown, kb_card_w - 40);
    lv_dropdown_set_options(setup_ui.network_dropdown, "Manual SSID");

    setup_ui.network_scan = lv_btn_create(kb_card);
    lv_obj_set_width(setup_ui.network_scan, 180);
    lv_obj_set_height(setup_ui.network_scan, 40);
    lv_obj_set_style_radius(setup_ui.network_scan, 10, 0);
    lv_obj_t* scan_label = lv_label_create(setup_ui.network_scan);
    lv_label_set_text(scan_label, "Scan Wi-Fi");

    setup_ui.ssid_input = lv_textarea_create(kb_card);
    lv_textarea_set_placeholder_text(setup_ui.ssid_input, "Wi-Fi SSID");
    lv_obj_set_width(setup_ui.ssid_input, kb_card_w - 40);

    setup_ui.pass_input = lv_textarea_create(kb_card);
    lv_textarea_set_password_mode(setup_ui.pass_input, true);
    lv_textarea_set_placeholder_text(setup_ui.pass_input, "Wi-Fi password");
    lv_obj_set_width(setup_ui.pass_input, kb_card_w - 40);

    setup_ui.pass_visibility = lv_btn_create(kb_card);
    lv_obj_set_size(setup_ui.pass_visibility, 44, 44);
    lv_obj_set_style_radius(setup_ui.pass_visibility, 6, 0);
    lv_obj_set_style_bg_color(setup_ui.pass_visibility, theme::dark_grey(), 0);
    lv_obj_t* pass_visibility_icon = lv_label_create(setup_ui.pass_visibility);
    lv_label_set_text(pass_visibility_icon, LV_SYMBOL_EYE_OPEN);
    lv_obj_center(pass_visibility_icon);
    lv_obj_add_event_cb(setup_ui.pass_visibility, [](lv_event_t* event) {
        auto* ui = static_cast<SetupUi*>(lv_event_get_user_data(event));
        if (!ui || !ui->pass_input) {
            return;
        }
        const bool hidden = lv_textarea_get_password_mode(ui->pass_input);
        lv_textarea_set_password_mode(ui->pass_input, !hidden);
        lv_obj_t* icon = lv_obj_get_child(lv_event_get_target(event), 0);
        if (icon) {
            lv_label_set_text(icon, hidden ? LV_SYMBOL_EYE_CLOSE : LV_SYMBOL_EYE_OPEN);
        }
    }, LV_EVENT_CLICKED, &setup_ui);

    setup_ui.keyboard = lv_keyboard_create(kb_card);
    lv_obj_set_size(setup_ui.keyboard, kb_card_w - 40, 180);
    lv_keyboard_set_mode(setup_ui.keyboard, LV_KEYBOARD_MODE_TEXT_LOWER);
    lv_keyboard_set_textarea(setup_ui.keyboard, setup_ui.ssid_input);

    auto setup_action_cb2 = [](lv_event_t* event) {
        auto* ui = static_cast<SetupUi*>(lv_event_get_user_data(event));
        if (!ui || !ui->overlay || lv_obj_has_flag(ui->overlay, LV_OBJ_FLAG_HIDDEN)) {
            return;
        }
        uint32_t now = millis();
        if (now - ui->last_action_ms < kSetupActionDebounceMs) {
            return;
        }
        ui->last_action_ms = now;
        if (ui->step == kStepSuccess) {
            ui->setup_dismissed = true;
            lv_obj_add_flag(ui->overlay, LV_OBJ_FLAG_HIDDEN);
            if (ui->tabview) {
                lv_obj_clear_flag(ui->tabview, LV_OBJ_FLAG_HIDDEN);
            }
            return;
        }
        if (ui->step == kStepRegistrationError) {
            service_http_retry_registration();
            apply_setup_step(*ui, kStepRegistering);
            return;
        }
        if (ui->step == kStepError) {
            String ssid = lv_textarea_get_text(ui->ssid_input);
            String pass = lv_textarea_get_text(ui->pass_input);
            if (ssid.length() == 0) {
                show_keyboard(*ui, true);
                return;
            }
            begin_wifi_heavy_operation(*ui, "Connecting Wi-Fi", "Retrying without another scan...");
            ui->setup_started = true;
            ui->connect_started_ms = millis();
            cancel_wifi_scan(*ui);
            service_wifi_connect(ssid, pass);
            return;
        }
        show_keyboard(*ui, true);
    };
    lv_obj_add_event_cb(setup_ui.action, setup_action_cb2, LV_EVENT_CLICKED, &setup_ui);

    auto setup_secondary_cb2 = [](lv_event_t* event) {
        auto* ui = static_cast<SetupUi*>(lv_event_get_user_data(event));
        if (!ui || !ui->overlay || lv_obj_has_flag(ui->overlay, LV_OBJ_FLAG_HIDDEN)) {
            return;
        }
        uint32_t now = millis();
        if (now - ui->last_action_ms < kSetupActionDebounceMs) {
            return;
        }
        ui->last_action_ms = now;
        if (ui->step == kStepSuccess) {
            ui->setup_dismissed = true;
            lv_obj_add_flag(ui->overlay, LV_OBJ_FLAG_HIDDEN);
            return;
        }
        if (ui->step == kStepRegistrationError) {
            show_keyboard(*ui, true);
            return;
        }
        if (ui->step == kStepError) {
            show_keyboard(*ui, true);
            return;
        }
        show_keyboard(*ui, true);
    };
    lv_obj_add_event_cb(setup_ui.secondary, setup_secondary_cb2, LV_EVENT_CLICKED, &setup_ui);

    lv_obj_add_event_cb(setup_ui.network_scan, [](lv_event_t* event) {
        auto* ui = static_cast<SetupUi*>(lv_event_get_user_data(event));
        if (!ui) {
            return;
        }
        refresh_wifi_networks(*ui);
    }, LV_EVENT_CLICKED, &setup_ui);

    lv_obj_add_event_cb(setup_ui.network_dropdown, [](lv_event_t* event) {
        auto* ui = static_cast<SetupUi*>(lv_event_get_user_data(event));
        if (!ui || !ui->network_dropdown || !ui->ssid_input) {
            return;
        }

        char selected[96] = {0};
        lv_dropdown_get_selected_str(ui->network_dropdown, selected, sizeof(selected));
        if (strcmp(selected, "Manual SSID") != 0 && strcmp(selected, "No networks found") != 0 && strcmp(selected, "Scanning...") != 0) {
            lv_textarea_set_text(ui->ssid_input, selected);
        }
    }, LV_EVENT_VALUE_CHANGED, &setup_ui);

    lv_obj_add_event_cb(setup_ui.ssid_input, [](lv_event_t* event) {
        auto* ui = static_cast<SetupUi*>(lv_event_get_user_data(event));
        if (ui && ui->keyboard) {
            lv_keyboard_set_textarea(ui->keyboard, ui->ssid_input);
            lv_obj_clear_flag(ui->keyboard, LV_OBJ_FLAG_HIDDEN);
        }
    }, LV_EVENT_FOCUSED, &setup_ui);

    lv_obj_add_event_cb(setup_ui.pass_input, [](lv_event_t* event) {
        auto* ui = static_cast<SetupUi*>(lv_event_get_user_data(event));
        if (ui && ui->keyboard) {
            lv_keyboard_set_textarea(ui->keyboard, ui->pass_input);
            lv_obj_clear_flag(ui->keyboard, LV_OBJ_FLAG_HIDDEN);
        }
    }, LV_EVENT_FOCUSED, &setup_ui);

    lv_obj_add_event_cb(setup_ui.keyboard, [](lv_event_t* event) {
        auto* ui = static_cast<SetupUi*>(lv_event_get_user_data(event));
        if (!ui || !ui->keyboard) {
            return;
        }
        lv_event_code_t code = lv_event_get_code(event);
        if (code == LV_EVENT_CANCEL || code == LV_EVENT_READY) {
            if (ui->ssid_input) {
                lv_obj_clear_state(ui->ssid_input, LV_STATE_FOCUSED);
            }
            if (ui->pass_input) {
                lv_obj_clear_state(ui->pass_input, LV_STATE_FOCUSED);
            }
        }
    }, LV_EVENT_ALL, &setup_ui);

    setup_ui.keyboard_connect = lv_btn_create(kb_card);
    lv_obj_set_width(setup_ui.keyboard_connect, 220);
    lv_obj_set_height(setup_ui.keyboard_connect, 44);
    lv_obj_set_style_radius(setup_ui.keyboard_connect, 12, 0);
    lv_obj_set_style_bg_color(setup_ui.keyboard_connect, theme::maroon(), 0);
    lv_obj_t* kb_label = lv_label_create(setup_ui.keyboard_connect);
    lv_label_set_text(kb_label, "Connect");

    lv_obj_t* kb_exit = lv_btn_create(kb_card);
    lv_obj_set_width(kb_exit, 220);
    lv_obj_set_height(kb_exit, 40);
    lv_obj_set_style_radius(kb_exit, 10, 0);
    lv_obj_set_style_bg_color(kb_exit, theme::dark_grey(), 0);
    lv_obj_t* kb_exit_label = lv_label_create(kb_exit);
    lv_label_set_text(kb_exit_label, "Exit setup");

    lv_obj_add_event_cb(setup_ui.keyboard_connect, [](lv_event_t* event) {
        auto* ui = static_cast<SetupUi*>(lv_event_get_user_data(event));
        if (!ui) {
            return;
        }
        String ssid = lv_textarea_get_text(ui->ssid_input);
        String pass = lv_textarea_get_text(ui->pass_input);
        if (ssid.length() == 0) {
            animate_shake(ui->keyboard_connect);
            return;
        }
        show_keyboard(*ui, false);
        begin_wifi_heavy_operation(*ui, "Connecting Wi-Fi", "Trying the selected network...");
        ui->setup_started = true;
        ui->connect_started_ms = millis();
        ui->wifi_resume_deadline_ms = millis() + 12000;
        cancel_wifi_scan(*ui);
        service_wifi_connect(ssid, pass);
    }, LV_EVENT_CLICKED, &setup_ui);

    lv_obj_add_event_cb(kb_exit, [](lv_event_t* event) {
        auto* ui = static_cast<SetupUi*>(lv_event_get_user_data(event));
        if (!ui) {
            return;
        }
        show_keyboard(*ui, false);
    }, LV_EVENT_CLICKED, &setup_ui);

    if (state.provisioning_complete) {
        setup_ui.setup_dismissed = true;
        lv_obj_add_flag(setup_ui.overlay, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(tabview, LV_OBJ_FLAG_HIDDEN);
        apply_setup_step(setup_ui, kStepWelcome);
    }

    lv_timer_create(setup_timer_cb, 1000, &setup_ui);
}

void ui_root_open_wifi_setup() {
    if (!g_setup_ui || !g_setup_ui->keyboard_overlay) {
        Serial.println("[UI] Wi-Fi setup unavailable");
        return;
    }

    String ssid;
    String password;
    if (service_storage_load_wifi(ssid, password)) {
        lv_textarea_set_text(g_setup_ui->ssid_input, ssid.c_str());
        lv_textarea_set_text(g_setup_ui->pass_input, password.c_str());
    }

    if (g_setup_ui->network_dropdown) {
        lv_dropdown_set_options(g_setup_ui->network_dropdown, "Manual SSID");
        lv_dropdown_set_selected(g_setup_ui->network_dropdown, 0);
    }
    if (g_setup_ui->keyboard && g_setup_ui->ssid_input) {
        lv_keyboard_set_textarea(g_setup_ui->keyboard, g_setup_ui->ssid_input);
    }

    cancel_wifi_scan(*g_setup_ui);
    lv_obj_move_foreground(g_setup_ui->keyboard_overlay);
    show_keyboard(*g_setup_ui, true);
    Serial.println("[UI] Wi-Fi setup opened");
}

} // namespace ptc
