#pragma once

#include "config.h"

namespace ptc {

void service_storage_init();
void service_storage_load_config(DeviceConfig& config, AppState& state);
void service_storage_save_config(const DeviceConfig& config);
void service_storage_save_time_sync(bool ok);
void service_storage_save_notices(const String& json, uint32_t ts);
bool service_storage_load_notices(String& json, uint32_t& ts);
void service_storage_save_logs(const String& json);
bool service_storage_load_logs(String& json);
void service_storage_clear_all();

} // namespace ptc
