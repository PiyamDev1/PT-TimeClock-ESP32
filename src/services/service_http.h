#pragma once

#include "config.h"

namespace ptc {

void service_http_init();
void service_http_tick(DeviceConfig& config, AppState& state);
bool service_http_registration_in_progress();
void service_http_retry_registration();
void service_http_force_notices_fetch();
bool service_http_has_notices();
uint16_t service_http_notice_count();
bool service_http_get_notice(uint16_t index, Notice& out_notice);
uint32_t service_http_last_notice_ts();
String service_http_manual_code_display();
uint32_t service_http_manual_code_expires_at();
bool service_http_manual_code_pending();
bool service_http_api_ok();
String service_http_last_error();

} // namespace ptc
