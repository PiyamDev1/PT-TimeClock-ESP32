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
const char* kKeyDeviceActive = "dev_active";
const char* kKeyNoticesJson = "notices_json";
const char* kKeyNoticesTs = "notices_ts";
const char* kKeyLogsJson = "logs_json";
const char* kKeyTouchMinX = "t_min_x";
const char* kKeyTouchMaxX = "t_max_x";
const char* kKeyTouchMinY = "t_min_y";
const char* kKeyTouchMaxY = "t_max_y";
const char* kKeyTouchInvX = "t_inv_x";
const char* kKeyTouchInvY = "t_inv_y";
const char* kKeyTouchSwap = "t_swap";
const char* kKeyTouchScaleX = "t_sc_x";
const char* kKeyTouchOffsetX = "t_of_x";
const char* kKeyTouchScaleY = "t_sc_y";
const char* kKeyTouchOffsetY = "t_of_y";
const char* kKeyTouchUseAffine = "t_aff_u";
const char* kKeyTouchAffXX = "t_aff_xx";
const char* kKeyTouchAffXY = "t_aff_xy";
const char* kKeyTouchAffX0 = "t_aff_x0";
const char* kKeyTouchAffYX = "t_aff_yx";
const char* kKeyTouchAffYY = "t_aff_yy";
const char* kKeyTouchAffY0 = "t_aff_y0";
const char* kKeyTouchValid = "t_valid";

} // namespace

void service_storage_init() {
    g_prefs.begin(kNamespace, false);
}

void service_storage_load_config(DeviceConfig& config, AppState& state) {
    config.device_id = g_prefs.isKey(kKeyDeviceId) ? g_prefs.getString(kKeyDeviceId, "") : "";
    config.device_secret = g_prefs.isKey(kKeyDeviceSecret) ? g_prefs.getString(kKeyDeviceSecret, "") : "";
    config.location_id = g_prefs.isKey(kKeyLocationId) ? g_prefs.getString(kKeyLocationId, "") : "";
    config.location_name = g_prefs.isKey(kKeyLocationName) ? g_prefs.getString(kKeyLocationName, "") : "";
    config.qr_interval_sec = g_prefs.getUInt(kKeyQrInterval, kDefaultQrIntervalSec);
    config.display_rotation = g_prefs.getUShort(kKeyDisplayRotation, kDefaultDisplayRotation);

    state.time_sync_ok = g_prefs.getBool(kKeyTimeSyncOk, false);
    state.device_active = g_prefs.getBool(kKeyDeviceActive, true);
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

void service_storage_save_device_active(bool active) {
    g_prefs.putBool(kKeyDeviceActive, active);
}

void service_storage_save_notices(const String& json, uint32_t ts) {
    g_prefs.putString(kKeyNoticesJson, json);
    g_prefs.putUInt(kKeyNoticesTs, ts);
}

bool service_storage_load_notices(String& json, uint32_t& ts) {
    json = g_prefs.isKey(kKeyNoticesJson) ? g_prefs.getString(kKeyNoticesJson, "") : "";
    ts = g_prefs.getUInt(kKeyNoticesTs, 0);
    return json.length() > 0;
}

void service_storage_save_logs(const String& json) {
    g_prefs.putString(kKeyLogsJson, json);
}

bool service_storage_load_logs(String& json) {
    json = g_prefs.isKey(kKeyLogsJson) ? g_prefs.getString(kKeyLogsJson, "") : "";
    return json.length() > 0;
}

void service_storage_save_touch_calibration(const TouchCalibration& calibration) {
    g_prefs.putUShort(kKeyTouchMinX, calibration.raw_min_x);
    g_prefs.putUShort(kKeyTouchMaxX, calibration.raw_max_x);
    g_prefs.putUShort(kKeyTouchMinY, calibration.raw_min_y);
    g_prefs.putUShort(kKeyTouchMaxY, calibration.raw_max_y);
    g_prefs.putBool(kKeyTouchInvX, calibration.invert_x);
    g_prefs.putBool(kKeyTouchInvY, calibration.invert_y);
    g_prefs.putBool(kKeyTouchSwap, calibration.swap_xy);
    g_prefs.putFloat(kKeyTouchScaleX, calibration.scale_x);
    g_prefs.putFloat(kKeyTouchOffsetX, calibration.offset_x);
    g_prefs.putFloat(kKeyTouchScaleY, calibration.scale_y);
    g_prefs.putFloat(kKeyTouchOffsetY, calibration.offset_y);
    g_prefs.putBool(kKeyTouchUseAffine, calibration.use_affine);
    g_prefs.putFloat(kKeyTouchAffXX, calibration.affine_xx);
    g_prefs.putFloat(kKeyTouchAffXY, calibration.affine_xy);
    g_prefs.putFloat(kKeyTouchAffX0, calibration.affine_x0);
    g_prefs.putFloat(kKeyTouchAffYX, calibration.affine_yx);
    g_prefs.putFloat(kKeyTouchAffYY, calibration.affine_yy);
    g_prefs.putFloat(kKeyTouchAffY0, calibration.affine_y0);
    g_prefs.putBool(kKeyTouchValid, calibration.valid);
}

bool service_storage_load_touch_calibration(TouchCalibration& calibration) {
    if (!g_prefs.isKey(kKeyTouchValid)) {
        calibration = TouchCalibration{};
        return false;
    }

    calibration.raw_min_x = g_prefs.getUShort(kKeyTouchMinX, 0);
    calibration.raw_max_x = g_prefs.getUShort(kKeyTouchMaxX, 799);
    calibration.raw_min_y = g_prefs.getUShort(kKeyTouchMinY, 0);
    calibration.raw_max_y = g_prefs.getUShort(kKeyTouchMaxY, 479);
    calibration.invert_x = g_prefs.getBool(kKeyTouchInvX, false);
    calibration.invert_y = g_prefs.getBool(kKeyTouchInvY, false);
    calibration.swap_xy = g_prefs.getBool(kKeyTouchSwap, false);
    calibration.scale_x = g_prefs.isKey(kKeyTouchScaleX) ? g_prefs.getFloat(kKeyTouchScaleX, 1.0f) : 1.0f;
    calibration.offset_x = g_prefs.isKey(kKeyTouchOffsetX) ? g_prefs.getFloat(kKeyTouchOffsetX, 0.0f) : 0.0f;
    calibration.scale_y = g_prefs.isKey(kKeyTouchScaleY) ? g_prefs.getFloat(kKeyTouchScaleY, 1.0f) : 1.0f;
    calibration.offset_y = g_prefs.isKey(kKeyTouchOffsetY) ? g_prefs.getFloat(kKeyTouchOffsetY, 0.0f) : 0.0f;
    calibration.use_affine = g_prefs.getBool(kKeyTouchUseAffine, false);
    calibration.affine_xx = g_prefs.isKey(kKeyTouchAffXX) ? g_prefs.getFloat(kKeyTouchAffXX, 1.0f) : 1.0f;
    calibration.affine_xy = g_prefs.isKey(kKeyTouchAffXY) ? g_prefs.getFloat(kKeyTouchAffXY, 0.0f) : 0.0f;
    calibration.affine_x0 = g_prefs.isKey(kKeyTouchAffX0) ? g_prefs.getFloat(kKeyTouchAffX0, 0.0f) : 0.0f;
    calibration.affine_yx = g_prefs.isKey(kKeyTouchAffYX) ? g_prefs.getFloat(kKeyTouchAffYX, 0.0f) : 0.0f;
    calibration.affine_yy = g_prefs.isKey(kKeyTouchAffYY) ? g_prefs.getFloat(kKeyTouchAffYY, 1.0f) : 1.0f;
    calibration.affine_y0 = g_prefs.isKey(kKeyTouchAffY0) ? g_prefs.getFloat(kKeyTouchAffY0, 0.0f) : 0.0f;
    calibration.valid = g_prefs.getBool(kKeyTouchValid, false);
    return calibration.valid;
}

void service_storage_clear_all() {
    g_prefs.clear();
}

} // namespace ptc
