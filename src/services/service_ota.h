#pragma once

#include "config.h"

namespace ptc {

enum class OtaState : uint8_t {
    kIdle,
    kChecking,
    kUpToDate,
    kAvailable,
    kDownloading,
    kDownloaded,
    kInstalling,
    kRebooting,
    kError,
};

void service_ota_init();
void service_ota_tick(DeviceConfig& config, AppState& state);
void service_ota_request_start();
void service_ota_check_github();
void service_ota_download_github();
void service_ota_install_latest_github();
void service_ota_apply_update();
bool service_ota_update_available();
bool service_ota_update_ready();
bool service_ota_reboot_required();
String service_ota_github_status();
String service_ota_latest_version();
uint8_t service_ota_progress();
OtaState service_ota_state();
bool service_ota_exclusive();
bool service_ota_enabled();
bool service_ota_ready();
bool service_ota_updating();
String service_ota_last_error();

} // namespace ptc
