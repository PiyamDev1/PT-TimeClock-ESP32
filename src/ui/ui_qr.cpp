#include "ui_qr.h"

#include <Arduino.h>
#include <vector>

#include <esp_heap_caps.h>

#include <qrcode.h>

#include "services/service_qr.h"

namespace ptc {

namespace {

constexpr int kQrSize = 360;
constexpr int kCodeRevealMs = 10000;

struct QrUi {
    lv_obj_t* canvas = nullptr;
    lv_color_t* canvas_buf = nullptr;
    lv_obj_t* qr_box = nullptr;
    lv_obj_t* code_value = nullptr;
    lv_obj_t* toast = nullptr;
    lv_timer_t* hide_timer = nullptr;
    lv_obj_t* arc = nullptr;
    lv_obj_t* countdown = nullptr;
    lv_obj_t* qr_label = nullptr;
    const DeviceConfig* config = nullptr;
    String last_payload;
    bool revealed = false;
};

constexpr int kQrVersion = 15;
constexpr int kQrQuietZone = 4;

void draw_qr(QrUi& ui, const String& payload) {
    if (!ui.canvas || !ui.canvas_buf) {
        return;
    }

    QRCode qrcode;
    static std::vector<uint8_t> qrcode_data;
    size_t buffer_size = qrcode_getBufferSize(kQrVersion);
    if (qrcode_data.size() != buffer_size) {
        qrcode_data.assign(buffer_size, 0);
    }

    qrcode_initText(&qrcode, qrcode_data.data(), kQrVersion, ECC_LOW, payload.c_str());

    lv_canvas_fill_bg(ui.canvas, lv_color_hex(0xFFFFFF), LV_OPA_COVER);

    int size = qrcode.size;
    int total_modules = size + kQrQuietZone * 2;
    int scale = kQrSize / total_modules;
    if (scale < 1) {
        scale = 1;
    }
    int drawn_size = total_modules * scale;
    int offset = (kQrSize - drawn_size) / 2;

    lv_draw_rect_dsc_t rect_dsc;
    lv_draw_rect_dsc_init(&rect_dsc);
    rect_dsc.bg_color = lv_color_hex(0x0A0F12);
    rect_dsc.bg_opa = LV_OPA_COVER;
    rect_dsc.border_width = 0;

    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            if (qrcode_getModule(&qrcode, x, y)) {
                lv_area_t area;
                area.x1 = offset + (x + kQrQuietZone) * scale;
                area.y1 = offset + (y + kQrQuietZone) * scale;
                area.x2 = area.x1 + scale - 1;
                area.y2 = area.y1 + scale - 1;
                lv_canvas_draw_rect(ui.canvas, area.x1, area.y1, scale, scale, &rect_dsc);
            }
        }
    }
}

void animate_pulse(lv_obj_t* target) {
    if (!target) {
        return;
    }

    lv_anim_t anim;
    lv_anim_init(&anim);
    lv_anim_set_var(&anim, target);
    lv_anim_set_time(&anim, 120);
    lv_anim_set_playback_time(&anim, 120);
    lv_anim_set_values(&anim, 256, 264);
    lv_anim_set_exec_cb(&anim, [](void* obj, int32_t value) {
        lv_obj_set_style_transform_zoom(static_cast<lv_obj_t*>(obj), value, 0);
    });
    lv_anim_start(&anim);
}

void hide_toast_cb(lv_timer_t* timer) {
    auto* ui = static_cast<QrUi*>(timer->user_data);
    if (ui->toast) {
        lv_obj_add_flag(ui->toast, LV_OBJ_FLAG_HIDDEN);
    }
    lv_timer_del(timer);
}

void hide_code_cb(lv_timer_t* timer) {
    auto* ui = static_cast<QrUi*>(timer->user_data);
    if (!ui || !ui->code_value) {
        return;
    }
    ui->revealed = false;
    lv_label_set_text(ui->code_value, "Tap to reveal");
    lv_timer_del(timer);
    ui->hide_timer = nullptr;
}

void show_toast(QrUi& ui, const char* text) {
    if (!ui.toast) {
        return;
    }
    lv_label_set_text(ui.toast, text);
    lv_obj_clear_flag(ui.toast, LV_OBJ_FLAG_HIDDEN);
    lv_timer_create(hide_toast_cb, 2000, &ui);
}

void code_event_cb(lv_event_t* event) {
    if (lv_event_get_code(event) != LV_EVENT_CLICKED) {
        return;
    }

    auto* ui = static_cast<QrUi*>(lv_event_get_user_data(event));
    if (!ui || !ui->code_value) {
        return;
    }

    if (ui->revealed) {
        show_toast(*ui, "Code shown");
        return;
    }

    ui->revealed = true;
    String manual = service_qr_manual_code();
    if (manual.length() == 0) {
        manual = "Waiting for sync";
    }
    lv_label_set_text(ui->code_value, manual.c_str());

    if (ui->hide_timer) {
        lv_timer_del(ui->hide_timer);
    }
    ui->hide_timer = lv_timer_create(hide_code_cb, kCodeRevealMs, ui);
}

} // namespace

void ui_qr_build(lv_obj_t* parent, const DeviceConfig& config, AppState& state) {
    LV_UNUSED(config);
    LV_UNUSED(state);

    static QrUi ui;
    ui.config = &config;

    lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(parent, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_ver(parent, 18, 0);
    lv_obj_set_style_pad_hor(parent, 18, 0);
    lv_obj_clear_flag(parent, LV_OBJ_FLAG_SCROLLABLE);

    ui.qr_box = lv_obj_create(parent);
    lv_obj_set_size(ui.qr_box, kQrSize, kQrSize);
    lv_obj_set_style_radius(ui.qr_box, 12, 0);
    lv_obj_set_style_bg_color(ui.qr_box, lv_color_hex(0xF7F9FA), 0);
    lv_obj_set_style_border_color(ui.qr_box, lv_color_hex(0x2D3E4A), 0);
    lv_obj_set_style_border_width(ui.qr_box, 2, 0);
    lv_obj_clear_flag(ui.qr_box, LV_OBJ_FLAG_SCROLLABLE);

    ui.canvas_buf = static_cast<lv_color_t*>(heap_caps_malloc(
        kQrSize * kQrSize * sizeof(lv_color_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (ui.canvas_buf) {
        ui.canvas = lv_canvas_create(ui.qr_box);
        lv_canvas_set_buffer(ui.canvas, ui.canvas_buf, kQrSize, kQrSize, LV_IMG_CF_TRUE_COLOR);
        lv_obj_center(ui.canvas);
        lv_canvas_fill_bg(ui.canvas, lv_color_hex(0xFFFFFF), LV_OPA_COVER);
    }

    ui.qr_label = lv_label_create(ui.qr_box);
    lv_label_set_text(ui.qr_label, "Waiting for QR");
    lv_obj_set_style_text_color(ui.qr_label, lv_color_hex(0x0A0F12), 0);
    lv_obj_center(ui.qr_label);

    lv_obj_t* meta_row = lv_obj_create(parent);
    lv_obj_set_size(meta_row, lv_pct(100), 120);
    lv_obj_clear_flag(meta_row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(meta_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(meta_row, LV_FLEX_ALIGN_SPACE_AROUND, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_bg_opa(meta_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(meta_row, 0, 0);

    ui.arc = lv_arc_create(meta_row);
    lv_obj_set_size(ui.arc, 120, 120);
    lv_arc_set_rotation(ui.arc, 270);
    lv_arc_set_bg_angles(ui.arc, 0, 360);
    lv_arc_set_value(ui.arc, 0);
    lv_obj_clear_flag(ui.arc, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_arc_width(ui.arc, 12, LV_PART_MAIN);
    lv_obj_set_style_arc_width(ui.arc, 12, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(ui.arc, lv_color_hex(0x193241), LV_PART_MAIN);
    lv_obj_set_style_arc_color(ui.arc, lv_color_hex(0x4BC3B5), LV_PART_INDICATOR);

    ui.countdown = lv_label_create(meta_row);
    lv_label_set_text(ui.countdown, "Refresh --s");
    lv_obj_set_style_text_color(ui.countdown, lv_color_hex(0xCDE6EF), 0);

    lv_obj_t* code_box = lv_obj_create(parent);
    lv_obj_set_width(code_box, lv_pct(100));
    lv_obj_set_height(code_box, 88);
    lv_obj_set_style_radius(code_box, 16, 0);
    lv_obj_set_style_bg_color(code_box, lv_color_hex(0x101B22), 0);
    lv_obj_set_style_border_width(code_box, 1, 0);
    lv_obj_set_style_border_color(code_box, lv_color_hex(0x1F3644), 0);
    lv_obj_set_flex_flow(code_box, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(code_box, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_ver(code_box, 12, 0);
    lv_obj_clear_flag(code_box, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(code_box, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t* code_hint = lv_label_create(code_box);
    lv_label_set_text(code_hint, "Manual code");
    lv_obj_set_style_text_color(code_hint, lv_color_hex(0x8FB1C0), 0);

    ui.code_value = lv_label_create(code_box);
    lv_label_set_text(ui.code_value, "Tap to reveal");
    lv_obj_set_style_text_color(ui.code_value, lv_color_hex(0xE9F5F9), 0);

    lv_obj_add_event_cb(code_box, code_event_cb, LV_EVENT_CLICKED, &ui);

    ui.toast = lv_label_create(parent);
    lv_label_set_text(ui.toast, "Code shown");
    lv_obj_set_style_bg_color(ui.toast, lv_color_hex(0x0A1218), 0);
    lv_obj_set_style_bg_opa(ui.toast, LV_OPA_80, 0);
    lv_obj_set_style_text_color(ui.toast, lv_color_hex(0xE9F5F9), 0);
    lv_obj_set_style_pad_hor(ui.toast, 14, 0);
    lv_obj_set_style_pad_ver(ui.toast, 8, 0);
    lv_obj_set_style_radius(ui.toast, 10, 0);
    lv_obj_add_flag(ui.toast, LV_OBJ_FLAG_HIDDEN);
    lv_obj_align(ui.toast, LV_ALIGN_TOP_MID, 0, 16);

    lv_timer_create([](lv_timer_t* timer) {
        auto* ui_ptr = static_cast<QrUi*>(timer->user_data);
        if (!ui_ptr || !ui_ptr->config) {
            return;
        }

        uint32_t interval = service_qr_interval_sec();
        if (interval == 0) {
            interval = 1;
        }
        uint32_t remain = service_qr_seconds_remaining();
        uint32_t pct = (interval - remain) * 100 / interval;

        if (ui_ptr->arc) {
            lv_arc_set_value(ui_ptr->arc, pct);
        }

        if (ui_ptr->countdown) {
            char buf[24];
            snprintf(buf, sizeof(buf), "Refresh %lus", static_cast<unsigned long>(remain));
            lv_label_set_text(ui_ptr->countdown, buf);
        }

        String payload = service_qr_payload();
        bool has_payload = payload.length() > 0;
        if (ui_ptr->qr_label) {
            lv_label_set_text(ui_ptr->qr_label, has_payload ? "" : "Waiting for QR");
        }

        if (has_payload && payload != ui_ptr->last_payload) {
            ui_ptr->last_payload = payload;
            draw_qr(*ui_ptr, payload);
            animate_pulse(ui_ptr->qr_box);
        }

        if (ui_ptr->canvas) {
            if (has_payload) {
                lv_obj_clear_flag(ui_ptr->canvas, LV_OBJ_FLAG_HIDDEN);
            } else {
                lv_obj_add_flag(ui_ptr->canvas, LV_OBJ_FLAG_HIDDEN);
            }
        }
    }, 1000, &ui);
}

} // namespace ptc
