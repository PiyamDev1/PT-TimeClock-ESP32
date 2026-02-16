#include "ui_notices.h"

#include "services/service_http.h"
#include "services/service_wifi.h"

#include <time.h>

namespace ptc {

namespace {

struct NoticesUi {
    lv_obj_t* list = nullptr;
    lv_obj_t* offline = nullptr;
};

void animate_fade_in(lv_obj_t* obj, uint32_t delay_ms) {
    if (!obj) {
        return;
    }
    lv_obj_set_style_opa(obj, LV_OPA_0, 0);
    lv_anim_t anim;
    lv_anim_init(&anim);
    lv_anim_set_var(&anim, obj);
    lv_anim_set_values(&anim, 0, 255);
    lv_anim_set_time(&anim, 220);
    lv_anim_set_delay(&anim, delay_ms);
    lv_anim_set_exec_cb(&anim, [](void* target, int32_t value) {
        lv_obj_set_style_opa(static_cast<lv_obj_t*>(target), value, 0);
    });
    lv_anim_start(&anim);
}

lv_obj_t* create_notice_card(lv_obj_t* parent, const char* title, const char* time, const char* body) {
    lv_obj_t* card = lv_obj_create(parent);
    lv_obj_set_width(card, lv_pct(100));
    lv_obj_set_style_radius(card, 14, 0);
    lv_obj_set_style_bg_color(card, lv_color_hex(0x0E1C25), 0);
    lv_obj_set_style_border_color(card, lv_color_hex(0x1C3442), 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_pad_all(card, 14, 0);
    lv_obj_set_style_pad_row(card, 8, 0);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* title_label = lv_label_create(card);
    lv_label_set_text(title_label, title);
    lv_obj_set_style_text_color(title_label, lv_color_hex(0xE9F5F9), 0);

    lv_obj_t* time_label = lv_label_create(card);
    lv_label_set_text(time_label, time);
    lv_obj_set_style_text_color(time_label, lv_color_hex(0x8FB1C0), 0);

    lv_obj_t* body_label = lv_label_create(card);
    lv_label_set_text(body_label, body);
    lv_label_set_long_mode(body_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_color(body_label, lv_color_hex(0xC9DCE4), 0);

    return card;
}

} // namespace

void ui_notices_build(lv_obj_t* parent, const DeviceConfig& config, AppState& state) {
    LV_UNUSED(config);
    LV_UNUSED(state);

    static NoticesUi ui;

    lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(parent, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(parent, 16, 0);
    lv_obj_set_style_pad_row(parent, 12, 0);

    lv_obj_t* header = lv_obj_create(parent);
    lv_obj_set_width(header, lv_pct(100));
    lv_obj_set_height(header, 56);
    lv_obj_set_style_bg_opa(header, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(header, 0, 0);
    lv_obj_set_flex_flow(header, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(header, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(header, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* title = lv_label_create(header);
    lv_label_set_text(title, "Notices");
    lv_obj_set_style_text_color(title, lv_color_hex(0xE9F5F9), 0);

    lv_obj_t* refresh_btn = lv_btn_create(header);
    lv_obj_set_height(refresh_btn, 40);
    lv_obj_set_style_radius(refresh_btn, 12, 0);
    lv_obj_set_style_bg_color(refresh_btn, lv_color_hex(0x153040), 0);
    lv_obj_t* refresh_label = lv_label_create(refresh_btn);
    lv_label_set_text(refresh_label, LV_SYMBOL_REFRESH " Refresh");
    lv_obj_add_event_cb(refresh_btn, [](lv_event_t*) {
        service_http_force_notices_fetch();
    }, LV_EVENT_CLICKED, nullptr);

    ui.offline = lv_label_create(parent);
    lv_label_set_text(ui.offline, "Offline, showing cached notices");
    lv_obj_set_style_text_color(ui.offline, lv_color_hex(0x8FB1C0), 0);

    ui.list = lv_obj_create(parent);
    lv_obj_set_width(ui.list, lv_pct(100));
    lv_obj_set_flex_flow(ui.list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_bg_opa(ui.list, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(ui.list, 0, 0);
    lv_obj_set_style_pad_row(ui.list, 12, 0);

    lv_timer_create([](lv_timer_t* timer) {
        auto* ui_ptr = static_cast<NoticesUi*>(timer->user_data);
        if (!ui_ptr || !ui_ptr->list) {
            return;
        }

        bool online = service_wifi_is_connected();
        uint32_t last_ts = service_http_last_notice_ts();
        if (ui_ptr->offline) {
            if (!online) {
                lv_label_set_text(ui_ptr->offline, "Offline, showing cached notices");
            } else if (last_ts > 0) {
                time_t ts_time = static_cast<time_t>(last_ts);
                struct tm* tm_info = localtime(&ts_time);
                char buf[48];
                if (tm_info) {
                    strftime(buf, sizeof(buf), "Last updated %H:%M", tm_info);
                    lv_label_set_text(ui_ptr->offline, buf);
                }
            } else {
                lv_label_set_text(ui_ptr->offline, "Syncing notices...");
            }
        }

        lv_obj_clean(ui_ptr->list);
        if (!service_http_has_notices()) {
            lv_obj_t* card = create_notice_card(ui_ptr->list, "No notices", "", "Notices will appear here once synced.");
            animate_fade_in(card, 0);
            return;
        }

        uint16_t count = service_http_notice_count();
        for (uint16_t i = 0; i < count; ++i) {
            Notice notice;
            if (service_http_get_notice(i, notice)) {
                lv_obj_t* card = create_notice_card(ui_ptr->list,
                    notice.title.c_str(),
                    notice.created_at.c_str(),
                    notice.body.c_str());
                animate_fade_in(card, i * 80);
            }
        }
    }, 3000, &ui);
}

} // namespace ptc
