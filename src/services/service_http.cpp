#include "service_http.h"

#include <HTTPClient.h>
#include <WiFiClientSecure.h>

#include <ArduinoJson.h>
#include <vector>
#include <time.h>

#include "secrets.h"
#include "service_log.h"
#include "service_storage.h"
#include "service_wifi.h"

namespace ptc {

namespace {

std::vector<Notice> g_notices;
uint32_t g_last_config_ms = 0;
uint32_t g_last_notice_ms = 0;
uint32_t g_last_heartbeat_ms = 0;
bool g_force_notice = false;
uint32_t g_last_notice_ts = 0;
bool g_api_ok = false;
String g_last_error;

constexpr uint32_t kConfigIntervalMs = 300000;
constexpr uint32_t kNoticeIntervalMs = 180000;
constexpr uint32_t kHeartbeatIntervalMs = 60000;

String build_url(const char* path) {
    return String(secrets::kApiBaseUrl) + path;
}

bool http_post_json(const String& url, const String& payload, String& response) {
    WiFiClientSecure client;
    client.setInsecure();
    HTTPClient http;
    if (!http.begin(client, url)) {
        g_api_ok = false;
        g_last_error = "HTTP begin failed";
        return false;
    }
    http.addHeader("Content-Type", "application/json");
    int code = http.POST(payload);
    if (code > 0) {
        response = http.getString();
    }
    http.end();
    g_api_ok = code >= 200 && code < 300;
    if (!g_api_ok) {
        g_last_error = "HTTP POST failed";
    } else {
        g_last_error = "";
    }
    return code >= 200 && code < 300;
}

bool http_get_json(const String& url, String& response) {
    WiFiClientSecure client;
    client.setInsecure();
    HTTPClient http;
    if (!http.begin(client, url)) {
        g_api_ok = false;
        g_last_error = "HTTP begin failed";
        return false;
    }
    int code = http.GET();
    if (code > 0) {
        response = http.getString();
    }
    http.end();
    g_api_ok = code >= 200 && code < 300;
    if (!g_api_ok) {
        g_last_error = "HTTP GET failed";
    } else {
        g_last_error = "";
    }
    return code >= 200 && code < 300;
}

void handle_register(DeviceConfig& config) {
    if (config.device_id.length() == 0) {
        uint8_t mac[6] = {0};
        WiFi.macAddress(mac);
        char buf[32];
        snprintf(buf, sizeof(buf), "ESP32S3-%02X%02X%02X%02X%02X%02X",
            mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
        config.device_id = String(buf);
    }

    StaticJsonDocument<256> doc;
    doc["device_id"] = config.device_id;
    doc["device_name"] = config.device_id;
    doc["firmware_version"] = kFirmwareVersion;

    String payload;
    serializeJson(doc, payload);

    String response;
    if (!http_post_json(build_url("/api/timeclock/devices/register"), payload, response)) {
        service_log_add("Register failed");
        return;
    }

    StaticJsonDocument<512> res;
    if (deserializeJson(res, response) != DeserializationError::Ok) {
        service_log_add("Register parse error");
        return;
    }

    config.device_secret = String(res["secret"] | "");
    config.location_id = String(res["location_id"] | "");
    config.location_name = String(res["location_name"] | "");
    config.qr_interval_sec = res["qr_interval_sec"] | kDefaultQrIntervalSec;

    service_storage_save_config(config);
    service_log_add("Device registered");
}

void handle_config(DeviceConfig& config) {
    String response;
    String url = build_url("/api/timeclock/devices/config?device_id=") + config.device_id;
    if (!http_get_json(url, response)) {
        service_log_add("Config fetch failed");
        return;
    }

    StaticJsonDocument<512> res;
    if (deserializeJson(res, response) != DeserializationError::Ok) {
        service_log_add("Config parse error");
        return;
    }

    config.location_id = String(res["location_id"] | config.location_id);
    config.location_name = String(res["location_name"] | config.location_name);
    config.qr_interval_sec = res["qr_interval_sec"] | config.qr_interval_sec;
    service_storage_save_config(config);
}

void handle_notices(DeviceConfig& config) {
    String response;
    String url = build_url("/api/timeclock/notices?device_id=") + config.device_id;
    if (!http_get_json(url, response)) {
        service_log_add("Notices fetch failed");
        return;
    }

    StaticJsonDocument<1024> doc;
    if (deserializeJson(doc, response) != DeserializationError::Ok) {
        service_log_add("Notices parse error");
        return;
    }

    g_notices.clear();
    for (JsonObject item : doc.as<JsonArray>()) {
        Notice notice;
        notice.id = String(item["id"] | "");
        notice.title = String(item["title"] | "");
        notice.body = String(item["body"] | "");
        notice.created_at = String(item["created_at"] | "");
        g_notices.push_back(notice);
    }

    g_last_notice_ts = static_cast<uint32_t>(time(nullptr));
    String json;
    serializeJson(doc, json);
    service_storage_save_notices(json, g_last_notice_ts);
}

void handle_heartbeat(DeviceConfig& config) {
    StaticJsonDocument<256> doc;
    doc["device_id"] = config.device_id;
    doc["firmware_version"] = kFirmwareVersion;
    doc["ip"] = WiFi.localIP().toString();
    doc["last_seen_at"] = static_cast<uint32_t>(time(nullptr));

    String payload;
    serializeJson(doc, payload);

    String response;
    http_post_json(build_url("/api/timeclock/devices/heartbeat"), payload, response);
}

} // namespace

void service_http_init() {
    g_notices.reserve(8);

    String cached;
    uint32_t ts = 0;
    if (service_storage_load_notices(cached, ts)) {
        StaticJsonDocument<1024> doc;
        if (deserializeJson(doc, cached) == DeserializationError::Ok) {
            g_notices.clear();
            for (JsonObject item : doc.as<JsonArray>()) {
                Notice notice;
                notice.id = String(item["id"] | "");
                notice.title = String(item["title"] | "");
                notice.body = String(item["body"] | "");
                notice.created_at = String(item["created_at"] | "");
                g_notices.push_back(notice);
            }
            g_last_notice_ts = ts;
        }
    }
}

void service_http_tick(DeviceConfig& config, AppState& state) {
    if (!service_wifi_is_connected()) {
        return;
    }

    if (!state.provisioning_complete) {
        handle_register(config);
        state.provisioning_complete = config.device_secret.length() > 0;
        return;
    }

    uint32_t now = millis();
    if (now - g_last_config_ms > kConfigIntervalMs) {
        g_last_config_ms = now;
        handle_config(config);
    }

    if (g_force_notice || (now - g_last_notice_ms > kNoticeIntervalMs)) {
        g_last_notice_ms = now;
        handle_notices(config);
        g_force_notice = false;
    }

    if (now - g_last_heartbeat_ms > kHeartbeatIntervalMs) {
        g_last_heartbeat_ms = now;
        handle_heartbeat(config);
    }
}

void service_http_force_notices_fetch() {
    g_force_notice = true;
}

bool service_http_has_notices() {
    return !g_notices.empty();
}

uint16_t service_http_notice_count() {
    return static_cast<uint16_t>(g_notices.size());
}

bool service_http_get_notice(uint16_t index, Notice& out_notice) {
    if (index >= g_notices.size()) {
        return false;
    }
    out_notice = g_notices[index];
    return true;
}

uint32_t service_http_last_notice_ts() {
    return g_last_notice_ts;
}

bool service_http_api_ok() {
    return g_api_ok;
}

String service_http_last_error() {
    return g_last_error;
}

} // namespace ptc
