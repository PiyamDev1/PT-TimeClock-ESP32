#include "ui_root.h"
#include "ui_qr.h"
#include "ui_notices.h"
#include "ui_log.h"
#include "ui_settings.h"

#include <time.h>

#include "services/service_wifi.h"
#include "services/service_qr.h"

#include <Arduino.h>

namespace ptc {

namespace {

constexpr int kStatusBarHeight = 48;
constexpr int kTabBarHeight = 64;

struct StatusUi {
    lv_obj_t* wifi = nullptr;
    lv_obj_t* time_label = nullptr;
    lv_obj_t* sync = nullptr;
    lv_obj_t* qr = nullptr;
    AppState* state = nullptr;
};

struct SetupUi {
    lv_obj_t* overlay = nullptr;
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
    lv_obj_t* ssid_input = nullptr;
    lv_obj_t* pass_input = nullptr;
    lv_obj_t* keyboard = nullptr;
    lv_obj_t* keyboard_connect = nullptr;
    AppState* state = nullptr;
    bool setup_started = false;
    uint32_t success_started_ms = 0;
    uint32_t connect_started_ms = 0;
    uint8_t step = 0;
};

enum SetupStep : uint8_t {
    kStepHidden = 0,
    kStepWelcome,
    kStepWifi,
    kStepConnecting,
    kStepRegistering,
    kStepSuccess,
    kStepError,
};

void init_styles(lv_style_t& screen, lv_style_t& status, lv_style_t& tab_btns) {
    lv_style_init(&screen);
    lv_style_set_bg_color(&screen, lv_color_hex(0x0C1B25));
    lv_style_set_bg_grad_color(&screen, lv_color_hex(0x112A38));
    lv_style_set_bg_grad_dir(&screen, LV_GRAD_DIR_VER);
    lv_style_set_text_color(&screen, lv_color_hex(0xF2F5F7));

    lv_style_init(&status);
    lv_style_set_bg_color(&status, lv_color_hex(0x0A1218));
    lv_style_set_text_color(&status, lv_color_hex(0xE9F0F3));
    lv_style_set_pad_hor(&status, 16);
    lv_style_set_pad_ver(&status, 6);

    lv_style_init(&tab_btns);
    lv_style_set_bg_color(&tab_btns, lv_color_hex(0x0B141B));
    lv_style_set_text_color(&tab_btns, lv_color_hex(0xB7C7D1));
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
        uint32_t remain = service_qr_seconds_remaining();
        char buf[16];
        snprintf(buf, sizeof(buf), "QR %lus", static_cast<unsigned long>(remain));
        lv_label_set_text(ui->qr, buf);
    }
}

void animate_shake(lv_obj_t* target) {
    if (!target) {
        return;
    }

    lv_anim_t anim;
    lv_anim_init(&anim);
    lv_anim_set_var(&anim, target);
    lv_anim_set_time(&anim, 250);
    lv_anim_set_repeat_count(&anim, 1);
    lv_anim_set_playback_time(&anim, 250);
    lv_anim_set_values(&anim, -8, 8);
    lv_anim_set_exec_cb(&anim, [](void* obj, int32_t value) {
        lv_obj_set_style_translate_x(static_cast<lv_obj_t*>(obj), value, 0);
    });
    lv_anim_start(&anim);
}

void show_keyboard(SetupUi& ui, bool show) {
    if (!ui.keyboard_overlay) {
        return;
    }
    if (show) {
        lv_obj_clear_flag(ui.keyboard_overlay, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(ui.keyboard_overlay, LV_OBJ_FLAG_HIDDEN);
    }
}

void apply_setup_step(SetupUi& ui, SetupStep step) {
    ui.step = step;
    switch (step) {
        case kStepWelcome:
            lv_label_set_text(ui.title, "Timeclock setup");
            lv_label_set_text(ui.subtitle, "Connect to Wi-Fi to continue.");
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
            lv_bar_set_value(ui.progress, 35, LV_ANIM_ON);
            break;
        case kStepRegistering:
            lv_label_set_text(ui.title, "Registering device");
            lv_label_set_text(ui.subtitle, "Linking to your branch...");
            lv_obj_add_flag(ui.action, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(ui.secondary, LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(ui.spinner, LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(ui.progress, LV_OBJ_FLAG_HIDDEN);
            lv_bar_set_value(ui.progress, 65, LV_ANIM_ON);
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
            lv_label_set_text(ui.subtitle, "Check Wi-Fi and try again.");
            lv_label_set_text(ui.action_label, "Retry");
            lv_obj_clear_flag(ui.action, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(ui.secondary, LV_OBJ_FLAG_HIDDEN);
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

    if (ui->state->provisioning_complete) {
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

    apply_setup_step(*ui, kStepRegistering);
}

} // namespace

void ui_root_init(DeviceConfig& config, AppState& state) {
    static lv_style_t style_screen;
    static lv_style_t style_status;
    static lv_style_t style_tab_btns;

    init_styles(style_screen, style_status, style_tab_btns);

    lv_obj_t* screen = lv_scr_act();
    lv_obj_add_style(screen, &style_screen, 0);

    lv_obj_t* status = lv_obj_create(screen);
    lv_obj_add_style(status, &style_status, 0);
    lv_obj_clear_flag(status, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(status, lv_disp_get_hor_res(NULL), kStatusBarHeight);
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
        lv_disp_get_hor_res(NULL),
        lv_disp_get_ver_res(NULL) - kStatusBarHeight);
    lv_obj_align(tabview, LV_ALIGN_BOTTOM_MID, 0, 0);

    lv_obj_t* tab_btns = lv_tabview_get_tab_btns(tabview);
    lv_obj_add_style(tab_btns, &style_tab_btns, 0);

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
    setup_ui.state = &state;

    setup_ui.overlay = lv_obj_create(screen);
    lv_obj_set_size(setup_ui.overlay, lv_disp_get_hor_res(NULL), lv_disp_get_ver_res(NULL));
    lv_obj_set_style_bg_color(setup_ui.overlay, lv_color_hex(0x0A1218), 0);
    lv_obj_set_style_bg_opa(setup_ui.overlay, LV_OPA_90, 0);
    lv_obj_clear_flag(setup_ui.overlay, LV_OBJ_FLAG_SCROLLABLE);

    setup_ui.card = lv_obj_create(setup_ui.overlay);
    lv_obj_set_size(setup_ui.card, 520, 280);
    lv_obj_center(setup_ui.card);
    lv_obj_set_style_radius(setup_ui.card, 18, 0);
    lv_obj_set_style_bg_color(setup_ui.card, lv_color_hex(0x112331), 0);
    lv_obj_set_style_border_color(setup_ui.card, lv_color_hex(0x1E3A4B), 0);
    lv_obj_set_style_border_width(setup_ui.card, 1, 0);
    lv_obj_set_style_pad_all(setup_ui.card, 18, 0);
    lv_obj_set_flex_flow(setup_ui.card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(setup_ui.card, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    setup_ui.title = lv_label_create(setup_ui.card);
    lv_label_set_text(setup_ui.title, "Timeclock setup");
    lv_obj_set_style_text_color(setup_ui.title, lv_color_hex(0xE9F5F9), 0);
    lv_obj_set_style_text_font(setup_ui.title, LV_FONT_DEFAULT, 0);

    setup_ui.subtitle = lv_label_create(setup_ui.card);
    lv_label_set_text(setup_ui.subtitle, "Connect to Wi-Fi to continue.");
    lv_obj_set_style_text_color(setup_ui.subtitle, lv_color_hex(0x8FB1C0), 0);

    setup_ui.action = lv_btn_create(setup_ui.card);
    lv_obj_set_width(setup_ui.action, 220);
    lv_obj_set_height(setup_ui.action, 48);
    lv_obj_set_style_radius(setup_ui.action, 12, 0);
    lv_obj_set_style_bg_color(setup_ui.action, lv_color_hex(0x1A3B4C), 0);
    setup_ui.action_label = lv_label_create(setup_ui.action);
    lv_label_set_text(setup_ui.action_label, "Start setup");

    setup_ui.secondary = lv_btn_create(setup_ui.card);
    lv_obj_set_width(setup_ui.secondary, 220);
    lv_obj_set_height(setup_ui.secondary, 44);
    lv_obj_set_style_radius(setup_ui.secondary, 12, 0);
    lv_obj_set_style_bg_color(setup_ui.secondary, lv_color_hex(0x12222C), 0);
    setup_ui.secondary_label = lv_label_create(setup_ui.secondary);
    lv_label_set_text(setup_ui.secondary_label, "Use on-device keyboard");
    lv_obj_add_flag(setup_ui.secondary, LV_OBJ_FLAG_HIDDEN);

    setup_ui.spinner = lv_spinner_create(setup_ui.card, 1000, 60);
    lv_obj_set_size(setup_ui.spinner, 48, 48);
    lv_obj_add_flag(setup_ui.spinner, LV_OBJ_FLAG_HIDDEN);

    setup_ui.progress = lv_bar_create(setup_ui.card);
    lv_obj_set_size(setup_ui.progress, 260, 10);
    lv_obj_add_flag(setup_ui.progress, LV_OBJ_FLAG_HIDDEN);
    lv_bar_set_range(setup_ui.progress, 0, 100);
    lv_bar_set_value(setup_ui.progress, 25, LV_ANIM_ON);

    lv_obj_add_event_cb(setup_ui.action, [](lv_event_t* event) {
        auto* ui = static_cast<SetupUi*>(lv_event_get_user_data(event));
        if (!ui) {
            return;
        }

        if (ui->step == kStepSuccess) {
            lv_obj_add_flag(ui->overlay, LV_OBJ_FLAG_HIDDEN);
            return;
        }

        ui->setup_started = true;
        ui->connect_started_ms = millis();
        service_wifi_start_portal();
    }, LV_EVENT_CLICKED, &setup_ui);

    lv_obj_add_event_cb(setup_ui.secondary, [](lv_event_t* event) {
        auto* ui = static_cast<SetupUi*>(lv_event_get_user_data(event));
        if (!ui) {
            return;
        }
        show_keyboard(*ui, true);
    }, LV_EVENT_CLICKED, &setup_ui);

    setup_ui.keyboard_overlay = lv_obj_create(screen);
    lv_obj_set_size(setup_ui.keyboard_overlay, lv_disp_get_hor_res(NULL), lv_disp_get_ver_res(NULL));
    lv_obj_set_style_bg_color(setup_ui.keyboard_overlay, lv_color_hex(0x0A1218), 0);
    lv_obj_set_style_bg_opa(setup_ui.keyboard_overlay, LV_OPA_90, 0);
    lv_obj_add_flag(setup_ui.keyboard_overlay, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t* kb_card = lv_obj_create(setup_ui.keyboard_overlay);
    lv_obj_set_size(kb_card, 620, 360);
    lv_obj_center(kb_card);
    lv_obj_set_style_radius(kb_card, 18, 0);
    lv_obj_set_style_bg_color(kb_card, lv_color_hex(0x112331), 0);
    lv_obj_set_style_border_color(kb_card, lv_color_hex(0x1E3A4B), 0);
    lv_obj_set_style_border_width(kb_card, 1, 0);
    lv_obj_set_style_pad_all(kb_card, 16, 0);
    lv_obj_set_flex_flow(kb_card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(kb_card, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t* kb_title = lv_label_create(kb_card);
    lv_label_set_text(kb_title, "Manual Wi-Fi setup");
    lv_obj_set_style_text_color(kb_title, lv_color_hex(0xE9F5F9), 0);

    setup_ui.ssid_input = lv_textarea_create(kb_card);
    lv_textarea_set_placeholder_text(setup_ui.ssid_input, "Wi-Fi SSID");
    lv_obj_set_width(setup_ui.ssid_input, 520);

    setup_ui.pass_input = lv_textarea_create(kb_card);
    lv_textarea_set_password_mode(setup_ui.pass_input, true);
    lv_textarea_set_placeholder_text(setup_ui.pass_input, "Wi-Fi password");
    lv_obj_set_width(setup_ui.pass_input, 520);

    setup_ui.keyboard = lv_keyboard_create(kb_card);
    lv_keyboard_set_mode(setup_ui.keyboard, LV_KEYBOARD_MODE_TEXT_LOWER);
    lv_keyboard_set_textarea(setup_ui.keyboard, setup_ui.ssid_input);

    lv_obj_add_event_cb(setup_ui.ssid_input, [](lv_event_t* event) {
        auto* ui = static_cast<SetupUi*>(lv_event_get_user_data(event));
        if (ui && ui->keyboard) {
            lv_keyboard_set_textarea(ui->keyboard, ui->ssid_input);
        }
    }, LV_EVENT_FOCUSED, &setup_ui);

    lv_obj_add_event_cb(setup_ui.pass_input, [](lv_event_t* event) {
        auto* ui = static_cast<SetupUi*>(lv_event_get_user_data(event));
        if (ui && ui->keyboard) {
            lv_keyboard_set_textarea(ui->keyboard, ui->pass_input);
        }
    }, LV_EVENT_FOCUSED, &setup_ui);

    setup_ui.keyboard_connect = lv_btn_create(kb_card);
    lv_obj_set_width(setup_ui.keyboard_connect, 220);
    lv_obj_set_height(setup_ui.keyboard_connect, 44);
    lv_obj_set_style_radius(setup_ui.keyboard_connect, 12, 0);
    lv_obj_set_style_bg_color(setup_ui.keyboard_connect, lv_color_hex(0x1A3B4C), 0);
    lv_obj_t* kb_label = lv_label_create(setup_ui.keyboard_connect);
    lv_label_set_text(kb_label, "Connect");

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
        ui->setup_started = true;
        ui->connect_started_ms = millis();
        service_wifi_connect(ssid, pass);
    }, LV_EVENT_CLICKED, &setup_ui);

    lv_timer_create(setup_timer_cb, 1000, &setup_ui);
}

} // namespace ptc
