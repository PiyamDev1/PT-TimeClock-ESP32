#pragma once

#include <lvgl.h>
#include "config.h"

namespace ptc {

void ui_settings_build(lv_obj_t* parent, DeviceConfig& config, AppState& state);

} // namespace ptc
