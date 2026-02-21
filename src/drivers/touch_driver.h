#pragma once

#include <lvgl.h>

#include "config.h"

namespace ptc {

bool touch_driver_init();
void touch_driver_read(lv_indev_drv_t* drv, lv_indev_data_t* data);
bool touch_driver_consume_tap_event();
bool touch_driver_poll_raw(uint16_t& x, uint16_t& y, bool& pressed);
void touch_driver_set_calibration(const TouchCalibration& calibration);
TouchCalibration touch_driver_get_calibration();

} // namespace ptc
