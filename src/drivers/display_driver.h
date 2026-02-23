#pragma once

#include <lvgl.h>

namespace ptc {

bool display_driver_init(lv_disp_t** out_disp);
void display_driver_show_test_pattern();
void display_driver_set_backlight(bool on);
bool display_driver_is_backlight_on();
void display_driver_set_backlight_dimmed(bool dimmed);
bool display_driver_is_backlight_dimmed();
void display_driver_set_render_enabled(bool enabled);
bool display_driver_is_render_enabled();

} // namespace ptc
