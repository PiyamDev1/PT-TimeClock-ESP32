#pragma once

#include "config.h"

namespace ptc {

void service_ota_init();
void service_ota_tick(DeviceConfig& config, AppState& state);
void service_ota_request_start();
void service_ota_check_github();
void service_ota_download_github();
void service_ota_apply_update();
bool service_ota_update_available();
bool service_ota_update_ready();
String service_ota_github_status();
bool service_ota_ready();
bool service_ota_updating();
String service_ota_last_error();

} // namespace ptc
