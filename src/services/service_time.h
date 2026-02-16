#pragma once

#include "config.h"

namespace ptc {

void service_time_init();
void service_time_tick(DeviceConfig& config, AppState& state);
void service_time_force_sync();

} // namespace ptc
