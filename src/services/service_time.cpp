#include "service_time.h"

#include <time.h>

#include "service_log.h"
#include "service_storage.h"

namespace ptc {

namespace {

constexpr const char* kTimezone = "GMT0BST,M3.5.0/1,M10.5.0/2";
constexpr uint32_t kSyncRetryMs = 5000;
uint32_t g_last_sync_ms = 0;

bool is_time_valid() {
    time_t now = time(nullptr);
    return now > 1700000000;
}

} // namespace

void service_time_init() {
    configTzTime(kTimezone, "pool.ntp.org", "time.nist.gov");
    Serial.println("[TIME] timezone Europe/London (GMT/BST)");
}

void service_time_tick(DeviceConfig& config, AppState& state) {
    (void)config;

    if (is_time_valid()) {
        if (!state.time_sync_ok) {
            state.time_sync_ok = true;
            service_storage_save_time_sync(true);
            service_log_add("Time synced");
            const time_t now = time(nullptr);
            struct tm local_time;
            char local_text[40] = {0};
            localtime_r(&now, &local_time);
            strftime(local_text, sizeof(local_text), "%Y-%m-%d %H:%M:%S %Z", &local_time);
            Serial.printf("[TIME] synced epoch=%lu local=%s\n",
                static_cast<unsigned long>(now),
                local_text);
        }
        return;
    }

    if (state.time_sync_ok) {
        state.time_sync_ok = false;
        service_storage_save_time_sync(false);
    }

    if (millis() - g_last_sync_ms < kSyncRetryMs) {
        return;
    }

    g_last_sync_ms = millis();
    configTzTime(kTimezone, "pool.ntp.org", "time.nist.gov");
    Serial.println("[TIME] waiting for SNTP");
}

void service_time_force_sync() {
    g_last_sync_ms = millis() - kSyncRetryMs;
    configTzTime(kTimezone, "pool.ntp.org", "time.nist.gov");
}

} // namespace ptc
