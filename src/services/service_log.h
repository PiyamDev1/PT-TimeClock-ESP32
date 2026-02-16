#pragma once

#include "config.h"

namespace ptc {

void service_log_init();
void service_log_tick(DeviceConfig& config, AppState& state);
void service_log_add(const String& message);
uint16_t service_log_count();
bool service_log_get(uint16_t index, uint32_t& ts_out, String& msg_out);

} // namespace ptc
