#include "service_http.h"

#include <HTTPClient.h>
#include <WiFiClientSecure.h>

#include <ArduinoJson.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
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
QueueHandle_t g_registration_results = nullptr;
bool g_registration_in_progress = false;
uint32_t g_next_registration_ms = 0;
uint32_t g_registration_retry_ms = 5000;

constexpr uint32_t kConfigIntervalMs = 300000;
constexpr uint32_t kNoticeIntervalMs = 180000;
constexpr uint32_t kHeartbeatIntervalMs = 60000;
constexpr uint32_t kRegistrationRetryMaxMs = 60000;

struct RegistrationTaskContext {
    char url[256] = {0};
    char payload[384] = {0};
};

struct RegistrationTaskResult {
    int16_t http_code = 0;
    char response[768] = {0};
    char error[96] = {0};
};

String build_url(const char* path) {
    return String(secrets::kApiBaseUrl) + path;
}

bool api_endpoint_configured() {
    const String base_url = secrets::kApiBaseUrl;
    return base_url.startsWith("http://") || base_url.startsWith("https://")
        ? base_url.indexOf("example.com") < 0
        : false;
}

bool portal_qr_contract() {
    return String(secrets::kApiBaseUrl).indexOf("ims.piyamtravel.com") >= 0;
}

void registration_task(void* parameter) {
    auto* context = static_cast<RegistrationTaskContext*>(parameter);
    RegistrationTaskResult result;

    WiFiClientSecure client;
    client.setInsecure();
    client.setTimeout(8000);

    HTTPClient http;
    http.setConnectTimeout(5000);
    http.setTimeout(8000);
    if (!http.begin(client, context->url)) {
        strlcpy(result.error, "HTTP begin failed", sizeof(result.error));
    } else {
        http.addHeader("Content-Type", "application/json");
        result.http_code = static_cast<int16_t>(http.POST(context->payload));
        if (result.http_code > 0) {
            const String response = http.getString();
            strlcpy(result.response, response.c_str(), sizeof(result.response));
        } else {
            snprintf(result.error, sizeof(result.error), "Connection failed (%d)", result.http_code);
        }
        http.end();
    }

    if (g_registration_results) {
        xQueueOverwrite(g_registration_results, &result);
    }
    delete context;
    vTaskDelete(nullptr);
}

bool start_registration(DeviceConfig& config) {
    if (config.device_id.length() == 0) {
        uint8_t mac[6] = {0};
        WiFi.macAddress(mac);
        char device_id[32];
        snprintf(device_id, sizeof(device_id), "ESP32S3-%02X%02X%02X%02X%02X%02X",
            mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
        config.device_id = device_id;
    }

    StaticJsonDocument<256> doc;
    doc["device_id"] = config.device_id;
    doc["device_name"] = config.device_id;
    doc["firmware_version"] = kFirmwareVersion;

    String payload;
    serializeJson(doc, payload);

    auto* context = new RegistrationTaskContext();
    if (!context) {
        g_last_error = "Registration memory allocation failed";
        return false;
    }
    const String url = build_url("/api/timeclock/devices/register");
    strlcpy(context->url, url.c_str(), sizeof(context->url));
    strlcpy(context->payload, payload.c_str(), sizeof(context->payload));

    const BaseType_t created = xTaskCreatePinnedToCore(
        registration_task, "http_register", 8192, context, 1, nullptr, 0);
    if (created != pdPASS) {
        delete context;
        g_last_error = "Registration task could not start";
        return false;
    }

    g_registration_in_progress = true;
    g_last_error = "";
    Serial.println("[HTTP] registration started in background");
    return true;
}

void apply_registration_result(DeviceConfig& config, AppState& state, const RegistrationTaskResult& result) {
    g_registration_in_progress = false;
    if (result.http_code < 200 || result.http_code >= 300) {
        g_api_ok = false;
        g_last_error = result.error[0]
            ? String(result.error)
            : String("Registration HTTP error ") + result.http_code;
        service_log_add("Register failed");
        g_next_registration_ms = millis() + g_registration_retry_ms;
        g_registration_retry_ms = min(g_registration_retry_ms * 2, kRegistrationRetryMaxMs);
        Serial.printf("[HTTP] registration failed: %s\n", g_last_error.c_str());
        return;
    }

    StaticJsonDocument<512> response;
    if (deserializeJson(response, result.response) != DeserializationError::Ok) {
        g_api_ok = false;
        g_last_error = "Registration response was invalid";
        service_log_add("Register parse error");
        g_next_registration_ms = millis() + g_registration_retry_ms;
        return;
    }

    config.device_secret = String(response["secret"] | "");
    if (config.device_secret.isEmpty()) {
        g_api_ok = false;
        g_last_error = "Registration response did not include a secret";
        g_next_registration_ms = millis() + g_registration_retry_ms;
        return;
    }

    config.location_id = String(response["location_id"] | "");
    config.location_name = String(response["location_name"] | "");
    config.qr_interval_sec = response["qr_interval_sec"] | kDefaultQrIntervalSec;
    const bool active = response["is_active"] | true;
    state.device_active = active;
    state.provisioning_complete = true;
    service_storage_save_device_active(active);
    service_storage_save_config(config);
    service_log_add("Device registered");
    g_api_ok = true;
    g_last_error = "";
    g_registration_retry_ms = 5000;
    Serial.println("[HTTP] registration complete");
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

void handle_config(DeviceConfig& config, AppState& state) {
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
    bool active = res["is_active"] | state.device_active;
    if (state.device_active != active) {
        state.device_active = active;
        service_storage_save_device_active(active);
        service_log_add(active ? "Device active" : "Device inactive");
    }
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
    g_api_ok = false;
    g_last_error = "";
    g_notices.reserve(8);
    g_registration_results = xQueueCreate(1, sizeof(RegistrationTaskResult));

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

    // PT Portal verifies the QR when an employee scans it. A provisioned
    // display does not POST to the legacy registration/heartbeat endpoints.
    if (state.provisioning_complete) {
        g_api_ok = api_endpoint_configured();
        g_last_error = g_api_ok ? "" : "API endpoint is not configured";
        return;
    }

    if (portal_qr_contract()) {
        g_api_ok = false;
        g_last_error = "Device enrollment is required in PT-Portal";
        return;
    }

    RegistrationTaskResult result;
    if (g_registration_results && xQueueReceive(g_registration_results, &result, 0) == pdTRUE) {
        apply_registration_result(config, state, result);
    }
    if (state.provisioning_complete || g_registration_in_progress) {
        return;
    }
    if (!api_endpoint_configured()) {
        g_last_error = "API endpoint is not configured";
        return;
    }
    if (millis() >= g_next_registration_ms) {
        start_registration(config);
    }
    if (!state.provisioning_complete) {
        return;
    }
}

bool service_http_registration_in_progress() {
    return g_registration_in_progress;
}

void service_http_retry_registration() {
    if (!g_registration_in_progress) {
        g_next_registration_ms = 0;
        g_last_error = "";
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
