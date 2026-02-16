#pragma once

#include <lvgl.h>

namespace ptc {

bool touch_driver_init();
void touch_driver_read(lv_indev_drv_t* drv, lv_indev_data_t* data);

} // namespace ptc
