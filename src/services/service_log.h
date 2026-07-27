#pragma once

#include "config.h"

namespace ptc {

void service_log_init();
void service_log_tick(DeviceConfig& config, AppState& state);
void service_log_add(const String& message);
void service_log_add_activity(
    const String& user,
    const String& action,
    uint32_t timestamp = 0,
    const String& event_id = "");
uint16_t service_log_count();
uint32_t service_log_revision();
bool service_log_get_activity(
    uint16_t index,
    uint32_t& timestamp_out,
    String& user_out,
    String& action_out);

} // namespace ptc
