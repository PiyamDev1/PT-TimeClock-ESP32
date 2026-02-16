#pragma once

#include "config.h"

namespace ptc {

void service_wifi_init();
void service_wifi_tick(DeviceConfig& config, AppState& state);
void service_wifi_start_portal();
bool service_wifi_is_connected();
bool service_wifi_portal_active();
void service_wifi_connect(const String& ssid, const String& password);
bool service_wifi_is_connecting();
void service_wifi_clear_credentials();

} // namespace ptc
