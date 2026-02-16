#pragma once

#include <lvgl.h>
#include "config.h"

namespace ptc {

void ui_qr_build(lv_obj_t* parent, const DeviceConfig& config, AppState& state);

} // namespace ptc
