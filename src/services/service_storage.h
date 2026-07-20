#pragma once

#include "config.h"

namespace ptc {

void service_storage_init();
bool service_storage_sd_ready();
void service_storage_load_config(DeviceConfig& config, AppState& state);
void service_storage_save_config(const DeviceConfig& config);
bool service_storage_load_wifi(String& ssid, String& password);
bool service_storage_save_wifi(const String& ssid, const String& password);
void service_storage_clear_wifi();
void service_storage_save_time_sync(bool ok);
void service_storage_save_device_active(bool active);
void service_storage_save_notices(const String& json, uint32_t ts);
bool service_storage_load_notices(String& json, uint32_t& ts);
void service_storage_save_logs(const String& json);
bool service_storage_load_logs(String& json);
void service_storage_save_touch_calibration(const TouchCalibration& calibration);
bool service_storage_load_touch_calibration(TouchCalibration& calibration);
void service_storage_clear_all();

} // namespace ptc
