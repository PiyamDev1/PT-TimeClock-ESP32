#pragma once

#include <Arduino.h>

namespace ptc {

static constexpr const char* kFirmwareVersion = "0.1.0";
static constexpr uint32_t kDefaultQrIntervalSec = 20;
static constexpr uint16_t kMaxLogEntries = 200;
static constexpr uint16_t kDefaultDisplayRotation = 0;

struct DeviceConfig {
    String device_id;
    String device_secret;
    String location_id;
    String location_name;
    uint32_t qr_interval_sec = kDefaultQrIntervalSec;
    uint16_t display_rotation = kDefaultDisplayRotation;
};

struct AppState {
    bool time_sync_ok = false;
    bool wifi_connected = false;
    bool provisioning_complete = false;
    bool device_active = true;
};

struct TouchCalibration {
    uint16_t raw_min_x = 0;
    uint16_t raw_max_x = 799;
    uint16_t raw_min_y = 0;
    uint16_t raw_max_y = 479;
    bool invert_x = false;
    bool invert_y = false;
    bool swap_xy = false;
    float scale_x = 1.0f;
    float offset_x = 0.0f;
    float scale_y = 1.0f;
    float offset_y = 0.0f;
    bool use_affine = false;
    float affine_xx = 1.0f;
    float affine_xy = 0.0f;
    float affine_x0 = 0.0f;
    float affine_yx = 0.0f;
    float affine_yy = 1.0f;
    float affine_y0 = 0.0f;
    bool valid = false;
};

struct Notice {
    String id;
    String title;
    String body;
    String created_at;
};

} // namespace ptc
