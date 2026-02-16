#include "service_storage.h"

#include <Preferences.h>

namespace ptc {

namespace {

Preferences g_prefs;

const char* kNamespace = "ptc";
const char* kKeyDeviceId = "device_id";
const char* kKeyDeviceSecret = "device_secret";
const char* kKeyLocationId = "location_id";
const char* kKeyLocationName = "location_name";
const char* kKeyQrInterval = "qr_interval";
const char* kKeyTimeSyncOk = "time_sync_ok";
const char* kKeyDisplayRotation = "disp_rot";
const char* kKeyNoticesJson = "notices_json";
const char* kKeyNoticesTs = "notices_ts";
const char* kKeyLogsJson = "logs_json";

} // namespace

void service_storage_init() {
    g_prefs.begin(kNamespace, false);
}

void service_storage_load_config(DeviceConfig& config, AppState& state) {
    config.device_id = g_prefs.getString(kKeyDeviceId, "");
    config.device_secret = g_prefs.getString(kKeyDeviceSecret, "");
    config.location_id = g_prefs.getString(kKeyLocationId, "");
    config.location_name = g_prefs.getString(kKeyLocationName, "");
    config.qr_interval_sec = g_prefs.getUInt(kKeyQrInterval, kDefaultQrIntervalSec);
    config.display_rotation = g_prefs.getUShort(kKeyDisplayRotation, kDefaultDisplayRotation);

    state.time_sync_ok = g_prefs.getBool(kKeyTimeSyncOk, false);
    state.provisioning_complete = config.device_id.length() > 0 && config.device_secret.length() > 0;
}

void service_storage_save_config(const DeviceConfig& config) {
    g_prefs.putString(kKeyDeviceId, config.device_id);
    g_prefs.putString(kKeyDeviceSecret, config.device_secret);
    g_prefs.putString(kKeyLocationId, config.location_id);
    g_prefs.putString(kKeyLocationName, config.location_name);
    g_prefs.putUInt(kKeyQrInterval, config.qr_interval_sec);
    g_prefs.putUShort(kKeyDisplayRotation, config.display_rotation);
}

void service_storage_save_time_sync(bool ok) {
    g_prefs.putBool(kKeyTimeSyncOk, ok);
}

void service_storage_save_notices(const String& json, uint32_t ts) {
    g_prefs.putString(kKeyNoticesJson, json);
    g_prefs.putUInt(kKeyNoticesTs, ts);
}

bool service_storage_load_notices(String& json, uint32_t& ts) {
    json = g_prefs.getString(kKeyNoticesJson, "");
    ts = g_prefs.getUInt(kKeyNoticesTs, 0);
    return json.length() > 0;
}

void service_storage_save_logs(const String& json) {
    g_prefs.putString(kKeyLogsJson, json);
}

bool service_storage_load_logs(String& json) {
    json = g_prefs.getString(kKeyLogsJson, "");
    return json.length() > 0;
}

void service_storage_clear_all() {
    g_prefs.clear();
}

} // namespace ptc
