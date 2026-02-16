#pragma once

#include "config.h"

namespace ptc {

void service_qr_init();
void service_qr_tick(DeviceConfig& config, AppState& state);
String service_qr_payload();
String service_qr_manual_code();
uint32_t service_qr_seconds_remaining();
uint32_t service_qr_interval_sec();
uint32_t service_qr_last_refresh_ms();

} // namespace ptc
