#include "service_log.h"

#include <vector>
#include <time.h>

#include <ArduinoJson.h>

#include "service_storage.h"

namespace ptc {

namespace {

struct LogEntry {
    uint32_t ts = 0;
    String message;
};

std::vector<LogEntry> g_entries;
uint16_t g_dirty_count = 0;
constexpr uint16_t kFlushEveryN = 5;

void save_logs_to_nvs() {
    StaticJsonDocument<2048> doc;
    JsonArray array = doc.to<JsonArray>();
    for (const auto& entry : g_entries) {
        JsonObject obj = array.createNestedObject();
        obj["ts"] = entry.ts;
        obj["msg"] = entry.message;
    }
    String json;
    serializeJson(doc, json);
    service_storage_save_logs(json);
    g_dirty_count = 0;
}

} // namespace

void service_log_init() {
    g_entries.reserve(kMaxLogEntries);

    String cached;
    if (service_storage_load_logs(cached)) {
        StaticJsonDocument<2048> doc;
        if (deserializeJson(doc, cached) == DeserializationError::Ok) {
            for (JsonObject obj : doc.as<JsonArray>()) {
                LogEntry entry;
                entry.ts = obj["ts"] | 0;
                entry.message = String(obj["msg"] | "");
                g_entries.push_back(entry);
            }
        }
    }
}

void service_log_tick(DeviceConfig& config, AppState& state) {
    (void)config;
    (void)state;
}

void service_log_add(const String& message) {
    if (g_entries.size() >= kMaxLogEntries) {
        g_entries.erase(g_entries.begin());
    }

    LogEntry entry;
    entry.ts = static_cast<uint32_t>(time(nullptr));
    entry.message = message;
    g_entries.push_back(entry);

    g_dirty_count++;
    if (g_dirty_count >= kFlushEveryN) {
        save_logs_to_nvs();
    }
}

uint16_t service_log_count() {
    return static_cast<uint16_t>(g_entries.size());
}

bool service_log_get(uint16_t index, uint32_t& ts_out, String& msg_out) {
    if (index >= g_entries.size()) {
        return false;
    }
    const auto& entry = g_entries[index];
    ts_out = entry.ts;
    msg_out = entry.message;
    return true;
}

} // namespace ptc
