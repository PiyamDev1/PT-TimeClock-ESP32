#include "service_time.h"

#include <time.h>

#include "service_log.h"
#include "service_storage.h"

namespace ptc {

namespace {

constexpr int kGmtOffsetSec = 0;
constexpr int kDaylightOffsetSec = 0;
constexpr uint32_t kSyncRetryMs = 30000;
uint32_t g_last_sync_ms = 0;

bool is_time_valid() {
    time_t now = time(nullptr);
    return now > 1700000000;
}

} // namespace

void service_time_init() {
    configTime(kGmtOffsetSec, kDaylightOffsetSec, "pool.ntp.org", "time.nist.gov");
}

void service_time_tick(DeviceConfig& config, AppState& state) {
    (void)config;

    if (state.time_sync_ok) {
        return;
    }

    if (millis() - g_last_sync_ms < kSyncRetryMs) {
        return;
    }

    g_last_sync_ms = millis();
    if (is_time_valid()) {
        state.time_sync_ok = true;
        service_storage_save_time_sync(true);
        service_log_add("Time synced");
    }
}

void service_time_force_sync() {
    g_last_sync_ms = 0;
    configTime(kGmtOffsetSec, kDaylightOffsetSec, "pool.ntp.org", "time.nist.gov");
}

} // namespace ptc
