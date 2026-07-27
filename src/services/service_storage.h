#pragma once

#include <vector>

#include "config.h"

namespace ptc {

struct StorageStatus {
    bool mounted = false;
    String card_type;
    uint64_t capacity_bytes = 0;
    uint64_t used_bytes = 0;
    uint64_t free_bytes = 0;
};

struct StoredActivity {
    String event_id;
    uint32_t timestamp = 0;
    String user;
    String action;
};

void service_storage_init();
bool service_storage_sd_ready();
bool service_storage_get_status(StorageStatus& status);
void service_storage_load_config(DeviceConfig& config, AppState& state);
void service_storage_save_config(const DeviceConfig& config);
bool service_storage_load_wifi(String& ssid, String& password);
bool service_storage_save_wifi(const String& ssid, const String& password);
void service_storage_clear_wifi();
void service_storage_save_time_sync(bool ok);
void service_storage_save_device_active(bool active);
void service_storage_save_notices(const String& json, uint32_t ts);
bool service_storage_load_notices(String& json, uint32_t& ts);
bool service_storage_append_system_log(uint32_t timestamp, const String& message);
bool service_storage_append_activity(
    const String& event_id,
    uint32_t timestamp,
    const String& user,
    const String& action);
bool service_storage_load_recent_activity(
    std::vector<StoredActivity>& entries,
    uint16_t max_entries);
void service_storage_save_touch_calibration(const TouchCalibration& calibration);
bool service_storage_load_touch_calibration(TouchCalibration& calibration);
void service_storage_clear_all();

} // namespace ptc
