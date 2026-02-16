#include "ui_log.h"

#include "services/service_log.h"

#include <time.h>
#include <cstring>

namespace ptc {

namespace {

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

} // namespace

void ui_log_build(lv_obj_t* parent, const DeviceConfig& config, AppState& state) {
    LV_UNUSED(config);
    LV_UNUSED(state);

    static lv_obj_t* list = nullptr;

    lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(parent, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(parent, 16, 0);
    lv_obj_set_style_pad_row(parent, 10, 0);

    lv_obj_t* title = lv_label_create(parent);
    lv_label_set_text(title, "Activity log");
    lv_obj_set_style_text_color(title, lv_color_hex(0xE9F5F9), 0);

    list = lv_list_create(parent);
    lv_obj_set_size(list, lv_pct(100), lv_pct(90));
    lv_obj_set_style_radius(list, 14, 0);
    lv_obj_set_style_bg_color(list, lv_color_hex(0x0E1C25), 0);
    lv_obj_set_style_border_color(list, lv_color_hex(0x1C3442), 0);
    lv_obj_set_style_border_width(list, 1, 0);

    lv_timer_create([](lv_timer_t* timer) {
        auto* list_ptr = static_cast<lv_obj_t*>(timer->user_data);
        if (!list_ptr) {
            return;
        }

        lv_obj_clean(list_ptr);
        uint16_t count = service_log_count();
        if (count == 0) {
            lv_list_add_text(list_ptr, "No activity yet");
            return;
        }

        lv_obj_t* last_btn = nullptr;
        for (uint16_t i = 0; i < count; ++i) {
            uint32_t ts = 0;
            String msg;
            if (service_log_get(i, ts, msg)) {
                char buf[32];
                time_t ts_time = static_cast<time_t>(ts);
                struct tm* tm_info = localtime(&ts_time);
                if (tm_info) {
                    strftime(buf, sizeof(buf), "%H:%M", tm_info);
                } else {
                    strncpy(buf, "--:--", sizeof(buf));
                    buf[sizeof(buf) - 1] = '\0';
                }
                String line = String(buf) + " " + msg;
                last_btn = lv_list_add_btn(list_ptr, LV_SYMBOL_OK, line.c_str());
                animate_fade_in(last_btn, i * 60);
            }
        }

        if (last_btn) {
            lv_obj_scroll_to_view(last_btn, LV_ANIM_OFF);
        }
    }, 3000, list);
}

} // namespace ptc
