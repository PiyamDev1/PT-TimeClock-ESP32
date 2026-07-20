#pragma once

#include <lvgl.h>

namespace ptc {
namespace theme {

inline lv_color_t black() { return lv_color_hex(0x09090B); }
inline lv_color_t dark_grey() { return lv_color_hex(0x1A1A1E); }
inline lv_color_t surface() { return lv_color_hex(0x242429); }
inline lv_color_t border() { return lv_color_hex(0x3A3A40); }
inline lv_color_t maroon() { return lv_color_hex(0x800020); }
inline lv_color_t maroon_light() { return lv_color_hex(0xA51C3B); }
inline lv_color_t white() { return lv_color_hex(0xFFFFFF); }
inline lv_color_t text_soft() { return lv_color_hex(0xE7E7EA); }
inline lv_color_t text_muted() { return lv_color_hex(0xB2B2B8); }

} // namespace theme
} // namespace ptc
