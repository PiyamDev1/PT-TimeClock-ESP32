#include "ui_qr.h"

#include <Arduino.h>
#include <algorithm>
#include <vector>

#include <esp_heap_caps.h>

#include <qrcode.h>

#include "services/service_http.h"
#include "services/service_qr.h"
#include "ui_theme.h"

namespace ptc {

namespace {

constexpr int kQrSize = 360;

struct QrUi {
    lv_obj_t* canvas = nullptr;
    lv_color_t* canvas_buf = nullptr;
    lv_obj_t* qr_box = nullptr;
    lv_obj_t* code_value = nullptr;
    lv_obj_t* arc = nullptr;
    lv_obj_t* countdown = nullptr;
    lv_obj_t* qr_label = nullptr;
    const DeviceConfig* config = nullptr;
    String last_payload;
};

constexpr int kQrVersion = 10;
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

    int size = qrcode.size;
    int total_modules = size + kQrQuietZone * 2;
    int scale = kQrSize / total_modules;
    if (scale < 1) {
        scale = 1;
    }
    int drawn_size = total_modules * scale;
    int offset = (kQrSize - drawn_size) / 2;

    const lv_color_t white = theme::white();
    const lv_color_t black = theme::black();
    std::fill_n(ui.canvas_buf, kQrSize * kQrSize, white);

    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            if (qrcode_getModule(&qrcode, x, y)) {
                const int pixel_x = offset + (x + kQrQuietZone) * scale;
                const int pixel_y = offset + (y + kQrQuietZone) * scale;
                for (int row = 0; row < scale; ++row) {
                    lv_color_t* destination = ui.canvas_buf + ((pixel_y + row) * kQrSize) + pixel_x;
                    std::fill_n(destination, scale, black);
                }
            }
        }
    }
    lv_obj_invalidate(ui.canvas);
}

void animate_pulse(lv_obj_t* target) {
    LV_UNUSED(target);
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
    lv_obj_set_style_bg_color(ui.qr_box, theme::white(), 0);
    lv_obj_set_style_border_color(ui.qr_box, theme::maroon(), 0);
    lv_obj_set_style_border_width(ui.qr_box, 2, 0);
    lv_obj_clear_flag(ui.qr_box, LV_OBJ_FLAG_SCROLLABLE);

    ui.canvas_buf = static_cast<lv_color_t*>(heap_caps_malloc(
        kQrSize * kQrSize * sizeof(lv_color_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (ui.canvas_buf) {
        ui.canvas = lv_canvas_create(ui.qr_box);
        lv_canvas_set_buffer(ui.canvas, ui.canvas_buf, kQrSize, kQrSize, LV_IMG_CF_TRUE_COLOR);
        lv_obj_center(ui.canvas);
        lv_canvas_fill_bg(ui.canvas, theme::white(), LV_OPA_COVER);
    }

    ui.qr_label = lv_label_create(ui.qr_box);
    lv_label_set_text(ui.qr_label, "Waiting for QR");
    lv_obj_set_style_text_color(ui.qr_label, theme::black(), 0);
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
    lv_obj_set_style_arc_color(ui.arc, theme::dark_grey(), LV_PART_MAIN);
    lv_obj_set_style_arc_color(ui.arc, theme::maroon_light(), LV_PART_INDICATOR);

    ui.countdown = lv_label_create(meta_row);
    lv_label_set_text(ui.countdown, "Refresh --s");
    lv_obj_set_style_text_color(ui.countdown, theme::text_soft(), 0);

    lv_obj_t* code_box = lv_obj_create(parent);
    lv_obj_set_width(code_box, lv_pct(100));
    lv_obj_set_height(code_box, 88);
    lv_obj_set_style_radius(code_box, 16, 0);
    lv_obj_set_style_bg_color(code_box, theme::surface(), 0);
    lv_obj_set_style_border_width(code_box, 1, 0);
    lv_obj_set_style_border_color(code_box, theme::border(), 0);
    lv_obj_set_flex_flow(code_box, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(code_box, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_ver(code_box, 12, 0);
    lv_obj_clear_flag(code_box, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* code_hint = lv_label_create(code_box);
    lv_label_set_text(code_hint, "Manual code");
    lv_obj_set_style_text_color(code_hint, theme::text_muted(), 0);

    ui.code_value = lv_label_create(code_box);
    lv_label_set_text(ui.code_value, "Unavailable");
    lv_obj_set_style_text_color(ui.code_value, theme::text_soft(), 0);

    lv_timer_create([](lv_timer_t* timer) {
        auto* ui_ptr = static_cast<QrUi*>(timer->user_data);
        if (!ui_ptr || !ui_ptr->config || !ui_ptr->qr_box || !lv_obj_is_visible(ui_ptr->qr_box)) {
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

        if (ui_ptr->code_value) {
            const String code = service_http_manual_code_display();
            if (!code.isEmpty()) {
                lv_label_set_text(ui_ptr->code_value, code.c_str());
                lv_obj_set_style_text_color(ui_ptr->code_value, theme::white(), 0);
            } else if (service_http_manual_code_pending()) {
                lv_label_set_text(ui_ptr->code_value, "Loading...");
                lv_obj_set_style_text_color(ui_ptr->code_value, theme::text_soft(), 0);
            } else {
                lv_label_set_text(ui_ptr->code_value, "Unavailable");
                lv_obj_set_style_text_color(ui_ptr->code_value, theme::text_soft(), 0);
            }
        }
    }, 1000, &ui);
}

} // namespace ptc
