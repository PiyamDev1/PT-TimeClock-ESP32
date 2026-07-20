#include "ui_log.h"

#include "services/service_log.h"
#include "ui_theme.h"

#include <time.h>
#include <cstring>

namespace ptc {

namespace {

void animate_fade_in(lv_obj_t* obj, uint32_t delay_ms) {
    LV_UNUSED(obj);
    LV_UNUSED(delay_ms);
}

} // namespace

void ui_log_build(lv_obj_t* parent, const DeviceConfig& config, AppState& state) {
    LV_UNUSED(config);
    LV_UNUSED(state);

    static lv_obj_t* list = nullptr;
    static uint32_t rendered_revision = UINT32_MAX;

    lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(parent, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(parent, 16, 0);
    lv_obj_set_style_pad_row(parent, 10, 0);

    lv_obj_t* title = lv_label_create(parent);
    lv_label_set_text(title, "Activity log");
    lv_obj_set_style_text_color(title, theme::white(), 0);

    list = lv_list_create(parent);
    lv_obj_set_size(list, lv_pct(100), lv_pct(90));
    lv_obj_set_style_radius(list, 14, 0);
    lv_obj_set_style_bg_color(list, theme::surface(), 0);
    lv_obj_set_style_border_color(list, theme::border(), 0);
    lv_obj_set_style_border_width(list, 1, 0);

    lv_timer_create([](lv_timer_t* timer) {
        auto* list_ptr = static_cast<lv_obj_t*>(timer->user_data);
        if (!list_ptr || !lv_obj_is_visible(list_ptr)) {
            return;
        }

        uint32_t revision = service_log_revision();
        if (rendered_revision == revision) {
            return;
        }
        rendered_revision = revision;

        lv_obj_clean(list_ptr);
        uint16_t count = service_log_count();
        if (count == 0) {
            lv_list_add_text(list_ptr, "No activity yet");
            return;
        }

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
                lv_obj_t* item = lv_list_add_btn(list_ptr, LV_SYMBOL_OK, line.c_str());
                animate_fade_in(item, i * 60);
            }
        }
    }, 3000, list);
}

} // namespace ptc
