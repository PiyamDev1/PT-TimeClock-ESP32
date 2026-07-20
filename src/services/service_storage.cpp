#include "service_storage.h"

#include <ArduinoJson.h>
#include <Preferences.h>
#include <SD.h>
#include <SPI.h>

#include "pins.h"
#include "secrets.h"

namespace ptc {

namespace {

Preferences g_prefs;
bool g_sd_ready = false;

constexpr uint32_t kSdFrequencyHz = 10000000;
constexpr const char* kNamespace = "ptc";
constexpr const char* kStorageDir = "/ptc";
constexpr const char* kMarkerPath = "/ptc/.initialized";
constexpr const char* kConfigPath = "/ptc/config.json";
constexpr const char* kStatePath = "/ptc/state.json";
constexpr const char* kWifiPath = "/ptc/wifi.json";
constexpr const char* kNoticesPath = "/ptc/notices.json";
constexpr const char* kNoticesMetaPath = "/ptc/notices.meta.json";
constexpr const char* kLogsPath = "/ptc/logs.json";
constexpr const char* kCalibrationPath = "/ptc/calibration.json";

constexpr const char* kKeyDeviceId = "device_id";
constexpr const char* kKeyDeviceSecret = "device_secret";
constexpr const char* kKeyLocationId = "location_id";
constexpr const char* kKeyLocationName = "location_name";
constexpr const char* kKeyQrInterval = "qr_interval";
constexpr const char* kKeyTimeSyncOk = "time_sync_ok";
constexpr const char* kKeyDisplayRotation = "disp_rot";
constexpr const char* kKeyDeviceActive = "dev_active";
constexpr const char* kKeyNoticesJson = "notices_json";
constexpr const char* kKeyNoticesTs = "notices_ts";
constexpr const char* kKeyLogsJson = "logs_json";
constexpr const char* kKeyTouchMinX = "t_min_x";
constexpr const char* kKeyTouchMaxX = "t_max_x";
constexpr const char* kKeyTouchMinY = "t_min_y";
constexpr const char* kKeyTouchMaxY = "t_max_y";
constexpr const char* kKeyTouchInvX = "t_inv_x";
constexpr const char* kKeyTouchInvY = "t_inv_y";
constexpr const char* kKeyTouchSwap = "t_swap";
constexpr const char* kKeyTouchScaleX = "t_sc_x";
constexpr const char* kKeyTouchOffsetX = "t_of_x";
constexpr const char* kKeyTouchScaleY = "t_sc_y";
constexpr const char* kKeyTouchOffsetY = "t_of_y";
constexpr const char* kKeyTouchUseAffine = "t_aff_u";
constexpr const char* kKeyTouchAffXX = "t_aff_xx";
constexpr const char* kKeyTouchAffXY = "t_aff_xy";
constexpr const char* kKeyTouchAffX0 = "t_aff_x0";
constexpr const char* kKeyTouchAffYX = "t_aff_yx";
constexpr const char* kKeyTouchAffYY = "t_aff_yy";
constexpr const char* kKeyTouchAffY0 = "t_aff_y0";
constexpr const char* kKeyTouchValid = "t_valid";

bool read_sd_text(const char* path, String& output) {
    if (!g_sd_ready) {
        return false;
    }

    String selected_path = path;
    if (!SD.exists(path)) {
        const String backup_path = String(path) + ".bak";
        if (!SD.exists(backup_path)) {
            return false;
        }
        selected_path = backup_path;
    }

    File file = SD.open(selected_path.c_str(), FILE_READ);
    if (!file || file.isDirectory() || file.size() > 131072) {
        if (file) {
            file.close();
        }
        return false;
    }
    output = file.readString();
    file.close();
    return true;
}

bool write_sd_text_atomic(const char* path, const String& content) {
    if (!g_sd_ready) {
        return false;
    }

    const String temp_path = String(path) + ".tmp";
    const String backup_path = String(path) + ".bak";
    SD.remove(temp_path.c_str());

    File file = SD.open(temp_path.c_str(), FILE_WRITE);
    if (!file) {
        return false;
    }
    const size_t written = file.print(content);
    file.flush();
    file.close();
    if (written != content.length()) {
        SD.remove(temp_path.c_str());
        return false;
    }

    SD.remove(backup_path.c_str());
    if (SD.exists(path) && !SD.rename(path, backup_path.c_str())) {
        SD.remove(temp_path.c_str());
        return false;
    }
    if (!SD.rename(temp_path.c_str(), path)) {
        if (SD.exists(backup_path.c_str())) {
            SD.rename(backup_path.c_str(), path);
        }
        return false;
    }
    SD.remove(backup_path.c_str());
    return true;
}

void load_state_from_sd(AppState& state) {
    String json;
    if (!read_sd_text(kStatePath, json)) {
        return;
    }
    StaticJsonDocument<192> doc;
    if (deserializeJson(doc, json) != DeserializationError::Ok) {
        return;
    }
    state.time_sync_ok = doc["time_sync_ok"] | false;
    state.device_active = doc["device_active"] | true;
}

void update_state_on_sd(const char* key, bool value) {
    if (!g_sd_ready) {
        return;
    }
    StaticJsonDocument<192> doc;
    String json;
    if (read_sd_text(kStatePath, json)) {
        deserializeJson(doc, json);
    }
    doc[key] = value;
    json = "";
    serializeJson(doc, json);
    write_sd_text_atomic(kStatePath, json);
}

void remove_sd_file(const char* path) {
    if (!g_sd_ready) {
        return;
    }
    SD.remove(path);
    SD.remove((String(path) + ".tmp").c_str());
    SD.remove((String(path) + ".bak").c_str());
}

} // namespace

void service_storage_init() {
    g_prefs.begin(kNamespace, false);

    pinMode(pins::kSdCs, OUTPUT);
    digitalWrite(pins::kSdCs, HIGH);
    SPI.begin(pins::kSdSck, pins::kSdMiso, pins::kSdMosi, pins::kSdCs);
    g_sd_ready = SD.begin(pins::kSdCs, SPI, kSdFrequencyHz) && SD.cardType() != CARD_NONE;
    if (!g_sd_ready) {
        Serial.println("[STORAGE] SD card unavailable; using NVS fallback");
        return;
    }

    if (!SD.exists(kStorageDir)) {
        SD.mkdir(kStorageDir);
    }
    Serial.printf("[STORAGE] SD mounted size=%lluMB\n", SD.cardSize() / (1024ULL * 1024ULL));

    if (!SD.exists(kMarkerPath)) {
        if (strlen(secrets::kDefaultWifiSsid) > 0) {
            service_storage_save_wifi(secrets::kDefaultWifiSsid, secrets::kDefaultWifiPassword);
            Serial.println("[STORAGE] default Wi-Fi saved to SD");
        }
        write_sd_text_atomic(kMarkerPath, "1\n");
    }
}

bool service_storage_sd_ready() {
    return g_sd_ready;
}

void service_storage_load_config(DeviceConfig& config, AppState& state) {
    config.device_id = g_prefs.getString(kKeyDeviceId, "");
    config.device_secret = g_prefs.getString(kKeyDeviceSecret, "");
    config.location_id = g_prefs.getString(kKeyLocationId, "");
    config.location_name = g_prefs.getString(kKeyLocationName, "");
    config.qr_interval_sec = g_prefs.getUInt(kKeyQrInterval, kDefaultQrIntervalSec);
    config.display_rotation = g_prefs.getUShort(kKeyDisplayRotation, kDefaultDisplayRotation);
    state.time_sync_ok = g_prefs.getBool(kKeyTimeSyncOk, false);
    state.device_active = g_prefs.getBool(kKeyDeviceActive, true);

    String json;
    bool loaded_from_sd = false;
    if (read_sd_text(kConfigPath, json)) {
        StaticJsonDocument<1024> doc;
        if (deserializeJson(doc, json) == DeserializationError::Ok) {
            config.device_id = String(doc["device_id"] | config.device_id.c_str());
            config.device_secret = String(doc["device_secret"] | config.device_secret.c_str());
            config.location_id = String(doc["location_id"] | config.location_id.c_str());
            config.location_name = String(doc["location_name"] | config.location_name.c_str());
            config.qr_interval_sec = doc["qr_interval_sec"] | config.qr_interval_sec;
            config.display_rotation = doc["display_rotation"] | config.display_rotation;
            loaded_from_sd = true;
        }
    }
    load_state_from_sd(state);

    bool seeded_device = false;
    const bool has_default_identity =
        strlen(secrets::kDefaultDeviceId) > 0 && strlen(secrets::kDefaultDeviceSecret) > 0;
    if (has_default_identity && (config.device_id.isEmpty() || config.device_secret.isEmpty())) {
        config.device_id = secrets::kDefaultDeviceId;
        config.device_secret = secrets::kDefaultDeviceSecret;
        seeded_device = true;
    } else {
        if (config.device_id.isEmpty() && strlen(secrets::kDefaultDeviceId) > 0) {
            config.device_id = secrets::kDefaultDeviceId;
            seeded_device = true;
        }
        if (config.device_secret.isEmpty() && strlen(secrets::kDefaultDeviceSecret) > 0) {
            config.device_secret = secrets::kDefaultDeviceSecret;
            seeded_device = true;
        }
    }
    if (config.location_name.isEmpty() && strlen(secrets::kDefaultDeviceLocation) > 0) {
        config.location_name = secrets::kDefaultDeviceLocation;
        seeded_device = true;
    }
    state.provisioning_complete = !config.device_id.isEmpty() && !config.device_secret.isEmpty();
    Serial.printf("[STORAGE] device=%s provisioned=%d%s\n",
        config.device_id.c_str(),
        state.provisioning_complete,
        seeded_device ? " (seeded)" : "");

    if (g_sd_ready && (!loaded_from_sd || seeded_device)) {
        service_storage_save_config(config);
    }
}

void service_storage_save_config(const DeviceConfig& config) {
    g_prefs.putString(kKeyDeviceId, config.device_id);
    g_prefs.putString(kKeyDeviceSecret, config.device_secret);
    g_prefs.putString(kKeyLocationId, config.location_id);
    g_prefs.putString(kKeyLocationName, config.location_name);
    g_prefs.putUInt(kKeyQrInterval, config.qr_interval_sec);
    g_prefs.putUShort(kKeyDisplayRotation, config.display_rotation);

    StaticJsonDocument<1024> doc;
    doc["device_id"] = config.device_id;
    doc["device_secret"] = config.device_secret;
    doc["location_id"] = config.location_id;
    doc["location_name"] = config.location_name;
    doc["qr_interval_sec"] = config.qr_interval_sec;
    doc["display_rotation"] = config.display_rotation;
    String json;
    serializeJson(doc, json);
    write_sd_text_atomic(kConfigPath, json);
}

bool service_storage_load_wifi(String& ssid, String& password) {
    ssid = "";
    password = "";
    String json;
    if (!read_sd_text(kWifiPath, json)) {
        return false;
    }
    StaticJsonDocument<512> doc;
    if (deserializeJson(doc, json) != DeserializationError::Ok) {
        return false;
    }
    ssid = String(doc["ssid"] | "");
    password = String(doc["password"] | "");
    return !ssid.isEmpty();
}

bool service_storage_save_wifi(const String& ssid, const String& password) {
    if (!g_sd_ready || ssid.isEmpty()) {
        return false;
    }
    StaticJsonDocument<512> doc;
    doc["ssid"] = ssid;
    doc["password"] = password;
    String json;
    serializeJson(doc, json);
    return write_sd_text_atomic(kWifiPath, json);
}

void service_storage_clear_wifi() {
    remove_sd_file(kWifiPath);
}

void service_storage_save_time_sync(bool ok) {
    g_prefs.putBool(kKeyTimeSyncOk, ok);
    update_state_on_sd("time_sync_ok", ok);
}

void service_storage_save_device_active(bool active) {
    g_prefs.putBool(kKeyDeviceActive, active);
    update_state_on_sd("device_active", active);
}

void service_storage_save_notices(const String& json, uint32_t ts) {
    g_prefs.putString(kKeyNoticesJson, json);
    g_prefs.putUInt(kKeyNoticesTs, ts);
    write_sd_text_atomic(kNoticesPath, json);
    StaticJsonDocument<96> meta;
    meta["timestamp"] = ts;
    String meta_json;
    serializeJson(meta, meta_json);
    write_sd_text_atomic(kNoticesMetaPath, meta_json);
}

bool service_storage_load_notices(String& json, uint32_t& ts) {
    if (read_sd_text(kNoticesPath, json)) {
        ts = 0;
        String meta_json;
        StaticJsonDocument<96> meta;
        if (read_sd_text(kNoticesMetaPath, meta_json) &&
            deserializeJson(meta, meta_json) == DeserializationError::Ok) {
            ts = meta["timestamp"] | 0;
        }
        return !json.isEmpty();
    }
    json = g_prefs.isKey(kKeyNoticesJson) ? g_prefs.getString(kKeyNoticesJson, "") : "";
    ts = g_prefs.getUInt(kKeyNoticesTs, 0);
    return !json.isEmpty();
}

void service_storage_save_logs(const String& json) {
    g_prefs.putString(kKeyLogsJson, json);
    write_sd_text_atomic(kLogsPath, json);
}

bool service_storage_load_logs(String& json) {
    if (read_sd_text(kLogsPath, json)) {
        return !json.isEmpty();
    }
    json = g_prefs.isKey(kKeyLogsJson) ? g_prefs.getString(kKeyLogsJson, "") : "";
    return !json.isEmpty();
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

    StaticJsonDocument<768> doc;
    doc["raw_min_x"] = calibration.raw_min_x;
    doc["raw_max_x"] = calibration.raw_max_x;
    doc["raw_min_y"] = calibration.raw_min_y;
    doc["raw_max_y"] = calibration.raw_max_y;
    doc["invert_x"] = calibration.invert_x;
    doc["invert_y"] = calibration.invert_y;
    doc["swap_xy"] = calibration.swap_xy;
    doc["scale_x"] = calibration.scale_x;
    doc["offset_x"] = calibration.offset_x;
    doc["scale_y"] = calibration.scale_y;
    doc["offset_y"] = calibration.offset_y;
    doc["use_affine"] = calibration.use_affine;
    doc["affine_xx"] = calibration.affine_xx;
    doc["affine_xy"] = calibration.affine_xy;
    doc["affine_x0"] = calibration.affine_x0;
    doc["affine_yx"] = calibration.affine_yx;
    doc["affine_yy"] = calibration.affine_yy;
    doc["affine_y0"] = calibration.affine_y0;
    doc["valid"] = calibration.valid;
    String json;
    serializeJson(doc, json);
    write_sd_text_atomic(kCalibrationPath, json);
}

bool service_storage_load_touch_calibration(TouchCalibration& calibration) {
    String json;
    if (read_sd_text(kCalibrationPath, json)) {
        StaticJsonDocument<768> doc;
        if (deserializeJson(doc, json) == DeserializationError::Ok) {
            calibration.raw_min_x = doc["raw_min_x"] | 0;
            calibration.raw_max_x = doc["raw_max_x"] | 799;
            calibration.raw_min_y = doc["raw_min_y"] | 0;
            calibration.raw_max_y = doc["raw_max_y"] | 479;
            calibration.invert_x = doc["invert_x"] | false;
            calibration.invert_y = doc["invert_y"] | false;
            calibration.swap_xy = doc["swap_xy"] | false;
            calibration.scale_x = doc["scale_x"] | 1.0f;
            calibration.offset_x = doc["offset_x"] | 0.0f;
            calibration.scale_y = doc["scale_y"] | 1.0f;
            calibration.offset_y = doc["offset_y"] | 0.0f;
            calibration.use_affine = doc["use_affine"] | false;
            calibration.affine_xx = doc["affine_xx"] | 1.0f;
            calibration.affine_xy = doc["affine_xy"] | 0.0f;
            calibration.affine_x0 = doc["affine_x0"] | 0.0f;
            calibration.affine_yx = doc["affine_yx"] | 0.0f;
            calibration.affine_yy = doc["affine_yy"] | 1.0f;
            calibration.affine_y0 = doc["affine_y0"] | 0.0f;
            calibration.valid = doc["valid"] | false;
            return calibration.valid;
        }
    }

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
    calibration.scale_x = g_prefs.getFloat(kKeyTouchScaleX, 1.0f);
    calibration.offset_x = g_prefs.getFloat(kKeyTouchOffsetX, 0.0f);
    calibration.scale_y = g_prefs.getFloat(kKeyTouchScaleY, 1.0f);
    calibration.offset_y = g_prefs.getFloat(kKeyTouchOffsetY, 0.0f);
    calibration.use_affine = g_prefs.getBool(kKeyTouchUseAffine, false);
    calibration.affine_xx = g_prefs.getFloat(kKeyTouchAffXX, 1.0f);
    calibration.affine_xy = g_prefs.getFloat(kKeyTouchAffXY, 0.0f);
    calibration.affine_x0 = g_prefs.getFloat(kKeyTouchAffX0, 0.0f);
    calibration.affine_yx = g_prefs.getFloat(kKeyTouchAffYX, 0.0f);
    calibration.affine_yy = g_prefs.getFloat(kKeyTouchAffYY, 1.0f);
    calibration.affine_y0 = g_prefs.getFloat(kKeyTouchAffY0, 0.0f);
    calibration.valid = g_prefs.getBool(kKeyTouchValid, false);
    return calibration.valid;
}

void service_storage_clear_all() {
    g_prefs.clear();
    remove_sd_file(kConfigPath);
    remove_sd_file(kStatePath);
    remove_sd_file(kWifiPath);
    remove_sd_file(kNoticesPath);
    remove_sd_file(kNoticesMetaPath);
    remove_sd_file(kLogsPath);
    remove_sd_file(kCalibrationPath);
    remove_sd_file(kMarkerPath);
}

} // namespace ptc
