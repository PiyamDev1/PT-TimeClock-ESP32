#include "service_log.h"

#include <vector>
#include <time.h>

#include "service_storage.h"

namespace ptc {

namespace {

std::vector<StoredActivity> g_activity;
uint32_t g_revision = 0;
constexpr uint16_t kActivityCacheEntries = 50;

} // namespace

void service_log_init() {
    service_storage_load_recent_activity(g_activity, kActivityCacheEntries);
    g_revision++;
}

void service_log_tick(DeviceConfig& config, AppState& state) {
    (void)config;
    (void)state;
}

void service_log_add(const String& message) {
    if (message.isEmpty()) {
        return;
    }
    service_storage_append_system_log(static_cast<uint32_t>(time(nullptr)), message);
}

void service_log_add_activity(
    const String& user,
    const String& action,
    uint32_t timestamp,
    const String& event_id) {
    if (user.isEmpty() || (action != "clocked in" && action != "clocked out")) {
        return;
    }
    if (timestamp == 0) {
        timestamp = static_cast<uint32_t>(time(nullptr));
    }
    if (!event_id.isEmpty()) {
        for (const auto& existing : g_activity) {
            if (existing.event_id == event_id) {
                return;
            }
        }
    }

    StoredActivity entry;
    entry.event_id = event_id;
    entry.timestamp = timestamp;
    entry.user = user;
    entry.action = action;
    if (g_activity.size() >= kActivityCacheEntries) {
        g_activity.erase(g_activity.begin());
    }
    g_activity.push_back(entry);
    service_storage_append_activity(event_id, timestamp, user, action);
    g_revision++;
}

uint16_t service_log_count() {
    return static_cast<uint16_t>(g_activity.size());
}

uint32_t service_log_revision() {
    return g_revision;
}

bool service_log_get_activity(
    uint16_t index,
    uint32_t& timestamp_out,
    String& user_out,
    String& action_out) {
    if (index >= g_activity.size()) {
        return false;
    }
    const auto& entry = g_activity[g_activity.size() - 1 - index];
    timestamp_out = entry.timestamp;
    user_out = entry.user;
    action_out = entry.action;
    return true;
}

} // namespace ptc
