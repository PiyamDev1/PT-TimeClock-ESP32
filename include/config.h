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
};

struct Notice {
    String id;
    String title;
    String body;
    String created_at;
};

} // namespace ptc
