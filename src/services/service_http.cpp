#include "service_http.h"

#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include <time.h>

#include <vector>

#include "secrets.h"
#include "service_auth.h"
#include "service_log.h"
#include "service_qr.h"
#include "service_storage.h"
#include "service_time.h"
#include "service_wifi.h"

namespace ptc {

namespace {

enum class RequestKind : uint8_t {
    kNone,
    kRegister,
    kConfig,
    kHeartbeat,
    kNotices,
    kManualCode,
};

struct ServiceRequest {
    RequestKind kind = RequestKind::kNone;
    String method;
    String base_url;
    String path_and_query;
    String body;
    String device_id;
    String device_secret;
    String correlation;
    bool signed_request = true;
};

struct ServiceResult {
    RequestKind kind = RequestKind::kNone;
    int status_code = 0;
    String body;
    String error;
    String correlation;
};

std::vector<Notice> g_notices;
QueueHandle_t g_request_queue = nullptr;
QueueHandle_t g_result_queue = nullptr;
bool g_request_in_progress = false;
RequestKind g_active_request = RequestKind::kNone;

uint32_t g_last_config_ms = 0;
uint32_t g_last_notice_ms = 0;
uint32_t g_last_heartbeat_ms = 0;
uint32_t g_last_notice_ts = 0;
bool g_initial_config_complete = false;
bool g_force_notice = false;

String g_manual_target_payload;
String g_manual_code_display;
uint32_t g_manual_code_expires_at = 0;
bool g_manual_done_for_payload = true;

bool g_api_ok = false;
String g_last_error;
bool g_scheduler_diagnostic_printed = false;
uint32_t g_retry_started_ms = 0;
uint32_t g_retry_delay_ms = 0;
uint32_t g_failure_backoff_ms = 5000;

uint32_t g_last_registration_attempt_ms = 0;
uint32_t g_registration_retry_ms = 5000;

constexpr uint32_t kConfigIntervalMs = 300000;
constexpr uint32_t kNoticeIntervalMs = 180000;
constexpr uint32_t kHeartbeatIntervalMs = 60000;
constexpr uint32_t kRegistrationRetryMaxMs = 60000;
constexpr uint32_t kFailureBackoffMaxMs = 300000;

const char* request_name(RequestKind kind) {
    switch (kind) {
        case RequestKind::kRegister:
            return "register";
        case RequestKind::kConfig:
            return "config";
        case RequestKind::kHeartbeat:
            return "heartbeat";
        case RequestKind::kNotices:
            return "notices";
        case RequestKind::kManualCode:
            return "manual-code";
        default:
            return "request";
    }
}

bool api_endpoint_configured() {
    const String base_url = secrets::kApiBaseUrl;
    return base_url.startsWith("https://") && base_url.indexOf("example.com") < 0;
}

bool portal_qr_contract() {
    return String(secrets::kApiBaseUrl).indexOf("ims.piyamtravel.com") >= 0;
}

bool interval_due(uint32_t last_ms, uint32_t interval_ms) {
    return last_ms == 0 || millis() - last_ms >= interval_ms;
}

bool retry_ready() {
    if (g_retry_delay_ms == 0) {
        return true;
    }
    if (millis() - g_retry_started_ms < g_retry_delay_ms) {
        return false;
    }
    g_retry_delay_ms = 0;
    return true;
}

void delay_requests(uint32_t delay_ms) {
    g_retry_started_ms = millis();
    g_retry_delay_ms = delay_ms;
}

uint32_t parse_iso_timestamp(const char* value) {
    if (!value || !value[0]) {
        return 0;
    }

    int year = 0;
    int month = 0;
    int day = 0;
    int hour = 0;
    int minute = 0;
    int second = 0;
    if (sscanf(value, "%d-%d-%dT%d:%d:%d", &year, &month, &day, &hour, &minute, &second) != 6) {
        return 0;
    }

    year -= month <= 2;
    const int era = (year >= 0 ? year : year - 399) / 400;
    const unsigned year_of_era = static_cast<unsigned>(year - era * 400);
    const unsigned adjusted_month = static_cast<unsigned>(month + (month > 2 ? -3 : 9));
    const unsigned day_of_year = (153 * adjusted_month + 2) / 5 + static_cast<unsigned>(day) - 1;
    const unsigned day_of_era = year_of_era * 365 + year_of_era / 4 - year_of_era / 100 + day_of_year;
    const int64_t days_since_epoch = static_cast<int64_t>(era) * 146097 + day_of_era - 719468;
    const int64_t parsed = days_since_epoch * 86400 + hour * 3600 + minute * 60 + second;
    return parsed > 0 && parsed <= UINT32_MAX ? static_cast<uint32_t>(parsed) : 0;
}

void clear_manual_code() {
    g_manual_code_display = "";
    g_manual_code_expires_at = 0;
}

void service_worker(void*) {
    while (true) {
        ServiceRequest* request = nullptr;
        if (xQueueReceive(g_request_queue, &request, portMAX_DELAY) != pdTRUE || !request) {
            continue;
        }

        auto* result = new ServiceResult();
        if (!result) {
            delete request;
            continue;
        }
        result->kind = request->kind;
        result->correlation = request->correlation;

        if (WiFi.status() != WL_CONNECTED) {
            result->error = "Wi-Fi disconnected";
        } else {
            WiFiClientSecure client;
            if (request->signed_request) {
                client.setCACert(service_auth_portal_root_ca());
            } else {
                client.setInsecure();
            }
            client.setTimeout(8000);

            HTTPClient http;
            http.setConnectTimeout(5000);
            http.setTimeout(8000);
            const String url = request->base_url + request->path_and_query;
            if (!http.begin(client, url)) {
                result->error = "HTTP begin failed";
            } else {
                if (request->signed_request) {
                    const String timestamp = String(static_cast<uint32_t>(time(nullptr)));
                    const String nonce = service_auth_random_nonce();
                    const String signature = service_auth_request_signature(
                        request->method,
                        request->path_and_query,
                        timestamp,
                        nonce,
                        request->body,
                        request->device_secret);
                    http.addHeader("X-PTC-Device-Id", request->device_id);
                    http.addHeader("X-PTC-Timestamp", timestamp);
                    http.addHeader("X-PTC-Nonce", nonce);
                    http.addHeader("X-PTC-Signature", signature);
                }
                if (request->method == "POST") {
                    http.addHeader("Content-Type", "application/json");
                    result->status_code = http.POST(request->body);
                } else {
                    result->status_code = http.GET();
                }
                if (result->status_code > 0) {
                    result->body = http.getString();
                } else {
                    result->error = HTTPClient::errorToString(result->status_code);
                }
                http.end();
            }
        }

        delete request;
        xQueueSend(g_result_queue, &result, portMAX_DELAY);
    }
}

bool enqueue_request(
    RequestKind kind,
    const String& method,
    const String& path_and_query,
    const String& body,
    const DeviceConfig& config,
    const String& correlation = "",
    bool signed_request = true) {
    if (g_request_in_progress || !g_request_queue) {
        return false;
    }

    auto* request = new ServiceRequest();
    if (!request) {
        g_last_error = "HTTP request allocation failed";
        return false;
    }
    request->kind = kind;
    request->method = method;
    request->base_url = secrets::kApiBaseUrl;
    request->path_and_query = path_and_query;
    request->body = body;
    request->device_id = config.device_id;
    request->device_secret = config.device_secret;
    request->correlation = correlation;
    request->signed_request = signed_request;

    if (xQueueSend(g_request_queue, &request, 0) != pdTRUE) {
        delete request;
        return false;
    }
    g_request_in_progress = true;
    g_active_request = kind;
    Serial.printf("[HTTP] %s queued\n", request_name(kind));
    return true;
}

void load_notices_from_json(const String& json, bool persist) {
    DynamicJsonDocument document(8192);
    if (deserializeJson(document, json) != DeserializationError::Ok || !document.is<JsonArray>()) {
        g_last_error = "Notices response invalid";
        service_log_add("Notices parse error");
        return;
    }

    g_notices.clear();
    for (JsonObject item : document.as<JsonArray>()) {
        Notice notice;
        notice.id = String(item["id"] | "");
        notice.title = String(item["title"] | "");
        notice.body = String(item["body"] | "");
        notice.image_url = String(item["image_url"] | "");
        notice.hyperlink_url = String(item["hyperlink_url"] | "");
        notice.display_seconds = item["display_seconds"] | 6;
        notice.sort_order = item["sort_order"] | 0;
        notice.created_at = String(item["created_at"] | "");
        notice.updated_at = String(item["updated_at"] | "");
        g_notices.push_back(notice);
    }

    g_last_notice_ts = static_cast<uint32_t>(time(nullptr));
    if (persist) {
        service_storage_save_notices(json, g_last_notice_ts);
    }
}

void apply_config_result(DeviceConfig& config, AppState& state, const ServiceResult& result) {
    StaticJsonDocument<768> response;
    if (deserializeJson(response, result.body) != DeserializationError::Ok) {
        g_api_ok = false;
        g_last_error = "Config response invalid";
        delay_requests(30000);
        return;
    }

    config.location_id = String(response["location_id"] | config.location_id.c_str());
    config.location_name = String(response["location_name"] | config.location_name.c_str());
    config.qr_interval_sec = response["qr_interval_sec"] | config.qr_interval_sec;
    state.device_active = response["is_active"] | state.device_active;
    service_storage_save_device_active(state.device_active);
    service_storage_save_config(config);
    g_last_config_ms = millis();
    g_initial_config_complete = true;
    service_log_add("Config updated");
    Serial.printf("[HTTP] config applied interval=%lus active=%d\n",
        static_cast<unsigned long>(config.qr_interval_sec),
        state.device_active ? 1 : 0);
}

void apply_registration_result(DeviceConfig& config, AppState& state, const ServiceResult& result) {
    if (result.status_code < 200 || result.status_code >= 300) {
        g_last_registration_attempt_ms = millis();
        g_registration_retry_ms = min(g_registration_retry_ms * 2, kRegistrationRetryMaxMs);
        return;
    }

    StaticJsonDocument<768> response;
    if (deserializeJson(response, result.body) != DeserializationError::Ok) {
        g_last_error = "Registration response invalid";
        return;
    }
    config.device_secret = String(response["secret"] | "");
    if (config.device_secret.isEmpty()) {
        g_last_error = "Registration secret missing";
        return;
    }
    config.location_id = String(response["location_id"] | "");
    config.location_name = String(response["location_name"] | "");
    config.qr_interval_sec = response["qr_interval_sec"] | kDefaultQrIntervalSec;
    state.device_active = response["is_active"] | true;
    state.provisioning_complete = true;
    service_storage_save_device_active(state.device_active);
    service_storage_save_config(config);
    g_registration_retry_ms = 5000;
    g_api_ok = true;
    g_last_error = "";
}

void mark_request_failure(DeviceConfig& config, AppState& state, const ServiceResult& result) {
    g_api_ok = false;
    g_last_error = result.status_code > 0
        ? String(request_name(result.kind)) + " HTTP " + result.status_code
        : String(request_name(result.kind)) + " connection failed";
    Serial.printf("[HTTP] %s status=%d\n", request_name(result.kind), result.status_code);
    service_log_add(String(request_name(result.kind)) + " failed");

    if (result.status_code == 401) {
        state.time_sync_ok = false;
        service_storage_save_time_sync(false);
        service_time_force_sync();
        delay_requests(30000);
    } else if (result.status_code == 403) {
        state.device_active = false;
        service_storage_save_device_active(false);
        clear_manual_code();
        g_manual_done_for_payload = true;
        g_initial_config_complete = true;
        g_last_config_ms = millis();
        delay_requests(kConfigIntervalMs);
    } else if (result.status_code == 429) {
        StaticJsonDocument<256> response;
        uint32_t retry_seconds = config.qr_interval_sec;
        if (deserializeJson(response, result.body) == DeserializationError::Ok) {
            retry_seconds = response["retry_after"] | retry_seconds;
        }
        delay_requests(max<uint32_t>(retry_seconds, 1) * 1000);
    } else if (result.status_code <= 0 || result.status_code >= 500) {
        delay_requests(g_failure_backoff_ms);
        g_failure_backoff_ms = min(g_failure_backoff_ms * 2, kFailureBackoffMaxMs);
    }

    if (result.status_code == 400) {
        const uint32_t now = millis();
        if (result.kind == RequestKind::kConfig) g_last_config_ms = now;
        if (result.kind == RequestKind::kHeartbeat) g_last_heartbeat_ms = now;
        if (result.kind == RequestKind::kNotices) g_last_notice_ms = now;
        if (result.kind == RequestKind::kManualCode) g_manual_done_for_payload = true;
    }
}

void apply_service_result(DeviceConfig& config, AppState& state, ServiceResult* result) {
    if (!result) {
        return;
    }
    g_request_in_progress = false;
    g_active_request = RequestKind::kNone;

    if (result->kind == RequestKind::kRegister) {
        apply_registration_result(config, state, *result);
        delete result;
        return;
    }

    if (result->status_code < 200 || result->status_code >= 300) {
        mark_request_failure(config, state, *result);
        delete result;
        return;
    }

    g_api_ok = true;
    g_last_error = "";
    g_failure_backoff_ms = 5000;
    const uint32_t now = millis();
    switch (result->kind) {
        case RequestKind::kConfig:
            apply_config_result(config, state, *result);
            break;
        case RequestKind::kHeartbeat:
            g_last_heartbeat_ms = now;
            service_log_add("Heartbeat sent");
            Serial.println("[HTTP] heartbeat accepted");
            break;
        case RequestKind::kNotices:
            load_notices_from_json(result->body, true);
            g_last_notice_ms = now;
            g_force_notice = false;
            Serial.printf("[HTTP] notices applied count=%u\n",
                static_cast<unsigned>(g_notices.size()));
            break;
        case RequestKind::kManualCode: {
            if (result->correlation != g_manual_target_payload) {
                break;
            }
            StaticJsonDocument<384> response;
            if (deserializeJson(response, result->body) != DeserializationError::Ok) {
                g_api_ok = false;
                g_last_error = "Manual code response invalid";
                g_manual_done_for_payload = true;
                break;
            }
            g_manual_code_display = String(response["code_display"] | "");
            g_manual_code_expires_at = parse_iso_timestamp(response["expires_at"] | "");
            if (g_manual_code_expires_at == 0) {
                g_manual_code_expires_at = static_cast<uint32_t>(time(nullptr)) + 30;
            }
            g_manual_done_for_payload = !g_manual_code_display.isEmpty();
            Serial.println("[HTTP] manual-code accepted");
            break;
        }
        default:
            break;
    }
    delete result;
}

void sync_manual_target(const AppState& state) {
    const uint32_t now = static_cast<uint32_t>(time(nullptr));
    if (g_manual_code_expires_at > 0 && now >= g_manual_code_expires_at) {
        clear_manual_code();
    }

    const String payload = state.time_sync_ok && state.device_active
        ? service_qr_payload()
        : String();
    if (payload != g_manual_target_payload) {
        g_manual_target_payload = payload;
        clear_manual_code();
        g_manual_done_for_payload = payload.isEmpty();
    }
}

bool enqueue_config(const DeviceConfig& config) {
    return enqueue_request(
        RequestKind::kConfig,
        "GET",
        String("/api/timeclock/devices/config?device_id=") + config.device_id,
        "",
        config);
}

bool enqueue_heartbeat(const DeviceConfig& config) {
    StaticJsonDocument<384> document;
    document["device_id"] = config.device_id;
    document["firmware_version"] = kFirmwareVersion;
    document["ip"] = WiFi.localIP().toString();
    document["wifi_rssi"] = WiFi.RSSI();
    document["free_heap"] = ESP.getFreeHeap();
    document["uptime_sec"] = millis() / 1000;
    String body;
    serializeJson(document, body);
    return enqueue_request(
        RequestKind::kHeartbeat,
        "POST",
        "/api/timeclock/devices/heartbeat",
        body,
        config);
}

bool enqueue_notices(const DeviceConfig& config) {
    return enqueue_request(
        RequestKind::kNotices,
        "GET",
        String("/api/timeclock/notices?device_id=") + config.device_id,
        "",
        config);
}

bool enqueue_manual_code(const DeviceConfig& config) {
    StaticJsonDocument<768> document;
    document["device_id"] = config.device_id;
    document["qr_payload"] = g_manual_target_payload;
    String body;
    serializeJson(document, body);
    return enqueue_request(
        RequestKind::kManualCode,
        "POST",
        "/api/timeclock/devices/manual-code",
        body,
        config,
        g_manual_target_payload);
}

bool enqueue_registration(DeviceConfig& config) {
    if (config.device_id.isEmpty()) {
        uint8_t mac[6] = {0};
        WiFi.macAddress(mac);
        char device_id[32] = {0};
        snprintf(device_id, sizeof(device_id), "ESP32S3-%02X%02X%02X%02X%02X%02X",
            mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
        config.device_id = device_id;
    }
    StaticJsonDocument<256> document;
    document["device_id"] = config.device_id;
    document["device_name"] = config.device_id;
    document["firmware_version"] = kFirmwareVersion;
    String body;
    serializeJson(document, body);
    g_last_registration_attempt_ms = millis();
    return enqueue_request(
        RequestKind::kRegister,
        "POST",
        "/api/timeclock/devices/register",
        body,
        config,
        "",
        false);
}

} // namespace

void service_http_init() {
    g_api_ok = false;
    g_last_error = "";
    g_notices.reserve(8);
    g_request_queue = xQueueCreate(1, sizeof(ServiceRequest*));
    g_result_queue = xQueueCreate(1, sizeof(ServiceResult*));
    const BaseType_t worker_result = g_request_queue && g_result_queue
        ? xTaskCreatePinnedToCore(service_worker, "portal_http", 12288, nullptr, 1, nullptr, 0)
        : errCOULD_NOT_ALLOCATE_REQUIRED_MEMORY;
    Serial.printf("[HTTP] init request_queue=%d result_queue=%d worker=%d endpoint=%d\n",
        g_request_queue ? 1 : 0,
        g_result_queue ? 1 : 0,
        worker_result == pdPASS ? 1 : 0,
        api_endpoint_configured() ? 1 : 0);
    if (!g_request_queue || !g_result_queue || worker_result != pdPASS) {
        g_last_error = "HTTP worker unavailable";
        return;
    }

    String cached;
    uint32_t timestamp = 0;
    if (service_storage_load_notices(cached, timestamp)) {
        load_notices_from_json(cached, false);
        g_last_notice_ts = timestamp;
    }
}

void service_http_tick(DeviceConfig& config, AppState& state) {
    ServiceResult* result = nullptr;
    if (g_result_queue && xQueueReceive(g_result_queue, &result, 0) == pdTRUE) {
        apply_service_result(config, state, result);
    }

    sync_manual_target(state);
    if (!service_wifi_is_connected() || !state.time_sync_ok) {
        clear_manual_code();
        return;
    }
    if (g_request_in_progress || !retry_ready()) {
        return;
    }
    if (!api_endpoint_configured()) {
        g_api_ok = false;
        g_last_error = "API endpoint is not configured";
        if (!g_scheduler_diagnostic_printed) {
            Serial.println("[HTTP] scheduler blocked endpoint=0");
            g_scheduler_diagnostic_printed = true;
        }
        return;
    }

    if (!g_scheduler_diagnostic_printed) {
        Serial.printf("[HTTP] scheduler ready provisioned=%d active=%d\n",
            state.provisioning_complete ? 1 : 0,
            state.device_active ? 1 : 0);
        g_scheduler_diagnostic_printed = true;
    }

    if (!state.provisioning_complete) {
        if (portal_qr_contract()) {
            g_api_ok = false;
            g_last_error = "Device enrollment is required in PT Portal";
            return;
        }
        if (g_last_registration_attempt_ms == 0 ||
            millis() - g_last_registration_attempt_ms >= g_registration_retry_ms) {
            enqueue_registration(config);
        }
        return;
    }

    if (!g_initial_config_complete) {
        enqueue_config(config);
        return;
    }
    if (!g_manual_done_for_payload && !g_manual_target_payload.isEmpty()) {
        enqueue_manual_code(config);
        return;
    }
    if (interval_due(g_last_config_ms, kConfigIntervalMs)) {
        enqueue_config(config);
        return;
    }
    if (interval_due(g_last_heartbeat_ms, kHeartbeatIntervalMs)) {
        enqueue_heartbeat(config);
        return;
    }
    if (g_force_notice || interval_due(g_last_notice_ms, kNoticeIntervalMs)) {
        enqueue_notices(config);
    }
}

bool service_http_registration_in_progress() {
    return g_request_in_progress && g_active_request == RequestKind::kRegister;
}

void service_http_retry_registration() {
    if (!service_http_registration_in_progress()) {
        g_last_registration_attempt_ms = 0;
        g_registration_retry_ms = 5000;
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

String service_http_manual_code_display() {
    if (g_manual_code_expires_at > 0 &&
        static_cast<uint32_t>(time(nullptr)) >= g_manual_code_expires_at) {
        return "";
    }
    return g_manual_code_display;
}

uint32_t service_http_manual_code_expires_at() {
    return g_manual_code_expires_at;
}

bool service_http_manual_code_pending() {
    return g_request_in_progress && g_active_request == RequestKind::kManualCode;
}

bool service_http_api_ok() {
    return g_api_ok;
}

String service_http_last_error() {
    return g_last_error;
}

} // namespace ptc
