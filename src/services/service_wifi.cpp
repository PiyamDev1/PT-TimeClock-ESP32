#include "service_wifi.h"

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiManager.h>
#include <esp_wifi.h>

#include "service_log.h"
#include "service_storage.h"

namespace {

WiFiManager g_wifi_manager;
bool g_portal_running = false;
bool g_connected = false;
uint32_t g_last_attempt_ms = 0;
bool g_connecting = false;
uint32_t g_connect_start_ms = 0;
uint32_t g_retry_delay_ms = 2000;
volatile uint8_t g_last_disconnect_reason = 0;
String g_pending_ssid;
String g_pending_password;
String g_candidate_ssid;
String g_candidate_password;
bool g_connect_pending = false;
uint32_t g_connect_pending_ms = 0;

constexpr uint32_t kRetryMinMs = 2000;
constexpr uint32_t kRetryMaxMs = 60000;
constexpr uint32_t kConnectTimeoutMs = 30000;

const char* connection_failure_message(uint8_t reason) {
    switch (reason) {
        case WIFI_REASON_NO_AP_FOUND:
            return "Network not found. Use a 2.4 GHz Wi-Fi network.";
        case WIFI_REASON_AUTH_FAIL:
            return "Router rejected authentication (reason 202).";
        case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT:
            return "WPA key handshake timed out (reason 15).";
        case WIFI_REASON_HANDSHAKE_TIMEOUT:
            return "WPA handshake timed out (reason 204).";
        case WIFI_REASON_BEACON_TIMEOUT:
            return "Wi-Fi signal was lost or is too weak.";
        case WIFI_REASON_ASSOC_FAIL:
        case WIFI_REASON_CONNECTION_FAIL:
        case WIFI_REASON_TIMEOUT:
            return "The router did not accept the connection.";
        default:
            return "Connection timed out. Check the SSID and password.";
    }
}

void wifi_event_handler(WiFiEvent_t event, WiFiEventInfo_t info) {
    if (event == ARDUINO_EVENT_WIFI_STA_GOT_IP) {
        Serial.printf("[WIFI] connected ip=%s rssi=%lddBm channel=%ld\n",
            WiFi.localIP().toString().c_str(),
            static_cast<long>(WiFi.RSSI()),
            static_cast<long>(WiFi.channel()));
        return;
    }

    if (event == ARDUINO_EVENT_WIFI_STA_DISCONNECTED) {
        const uint8_t reason = info.wifi_sta_disconnected.reason;
        g_last_disconnect_reason = reason;
        Serial.printf("[WIFI] disconnected reason=%u (%s)\n",
            static_cast<unsigned>(reason),
            WiFi.disconnectReasonName(static_cast<wifi_err_reason_t>(reason)));
    }
}

String build_ap_ssid() {
    uint8_t mac[6] = {0};
    WiFi.macAddress(mac);
    char buf[24];
    snprintf(buf, sizeof(buf), "PT-Timeclock-%02X%02X", mac[4], mac[5]);
    return String(buf);
}

} // namespace

namespace ptc {

void service_wifi_init() {
    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false);
    WiFi.setTxPower(WIFI_POWER_17dBm);
    WiFi.setAutoReconnect(false);
    WiFi.onEvent(wifi_event_handler);
    g_wifi_manager.setConfigPortalBlocking(false);
    g_wifi_manager.setConfigPortalTimeout(180);
    Serial.printf("[WIFI] initialized tx_power=%d sleep=off\n", static_cast<int>(WiFi.getTxPower()));

    String ssid;
    String password;
    if (service_storage_load_wifi(ssid, password)) {
        service_wifi_connect(ssid, password);
        Serial.printf("[WIFI] queued stored SD network ssid=%s\n", ssid.c_str());
    }
}

void service_wifi_tick(DeviceConfig& config, AppState& state) {
    (void)config;

    if (g_connect_pending) {
        const int16_t scan_state = WiFi.scanComplete();
        if (scan_state == WIFI_SCAN_RUNNING) {
            esp_wifi_scan_stop();
            WiFi.scanDelete();
            g_connect_pending_ms = millis();
            Serial.println("[WIFI] active scan stopped before connect");
            return;
        }

        if (millis() - g_connect_pending_ms < 100) {
            return;
        }

        g_connect_pending = false;
        g_connect_start_ms = millis();
        Serial.printf("[WIFI] connecting ssid=%s password_length=%u\n",
            g_pending_ssid.c_str(), static_cast<unsigned>(g_pending_password.length()));
        WiFi.begin(g_pending_ssid.c_str(), g_pending_password.c_str());
        g_pending_ssid = "";
        g_pending_password = "";
    }

    if (WiFi.status() == WL_CONNECTED) {
        if (!g_connected) {
            g_connected = true;
            state.wifi_connected = true;
            g_connecting = false;
            g_retry_delay_ms = kRetryMinMs;
            g_last_disconnect_reason = 0;
            if (!g_candidate_ssid.isEmpty()) {
                String stored_ssid;
                String stored_password;
                if (!service_storage_load_wifi(stored_ssid, stored_password) ||
                    stored_ssid != g_candidate_ssid || stored_password != g_candidate_password) {
                    service_storage_save_wifi(g_candidate_ssid, g_candidate_password);
                    Serial.println("[WIFI] successful credentials saved to SD");
                }
                g_candidate_ssid = "";
                g_candidate_password = "";
            }
            service_log_add("Wi-Fi connected");
            Serial.printf("[WIFI] online ssid=%s rssi=%lddBm\n",
                WiFi.SSID().c_str(), static_cast<long>(WiFi.RSSI()));
        }
        g_portal_running = false;
        return;
    }

    if (g_connected) {
        g_connected = false;
        state.wifi_connected = false;
        service_log_add("Wi-Fi disconnected");
    }

    if (g_connecting && millis() - g_connect_start_ms > kConnectTimeoutMs) {
        g_connecting = false;
        Serial.printf("[WIFI] connect timeout reason=%u message=%s\n",
            static_cast<unsigned>(g_last_disconnect_reason),
            connection_failure_message(g_last_disconnect_reason));
    }

    if (g_portal_running) {
        g_wifi_manager.process();
        return;
    }

    if (g_connecting) {
        return;
    }

    uint32_t now = millis();
    if (now - g_last_attempt_ms < g_retry_delay_ms) {
        return;
    }

    String retry_ssid;
    String retry_password;
    if (!service_storage_load_wifi(retry_ssid, retry_password)) {
        return;
    }

    g_last_attempt_ms = now;
    g_connecting = true;
    g_connect_pending = true;
    g_connect_pending_ms = now;
    g_connect_start_ms = now;
    g_pending_ssid = retry_ssid;
    g_pending_password = retry_password;
    g_candidate_ssid = retry_ssid;
    g_candidate_password = retry_password;
    Serial.printf("[WIFI] retrying SD network ssid=%s\n", retry_ssid.c_str());

    if (g_retry_delay_ms < kRetryMaxMs) {
        uint32_t next = g_retry_delay_ms * 2;
        g_retry_delay_ms = next > kRetryMaxMs ? kRetryMaxMs : next;
    }
}

void service_wifi_start_portal() {
    if (g_portal_running) {
        service_log_add("Wi-Fi portal already running");
        return;
    }

    WiFi.mode(WIFI_AP_STA);
    delay(50);

    String ap_ssid = build_ap_ssid();
    bool started = g_wifi_manager.startConfigPortal(ap_ssid.c_str());
    g_portal_running = started;

    if (started) {
        service_log_add("Wi-Fi portal started");
        Serial.printf("[WIFI] portal started: %s\n", ap_ssid.c_str());
    } else {
        service_log_add("Wi-Fi portal start failed");
        Serial.println("[WIFI] portal start failed");
    }
}

void service_wifi_connect(const String& ssid, const String& password) {
    g_portal_running = false;
    g_connecting = true;
    g_connect_pending = true;
    g_connect_pending_ms = millis();
    g_connect_start_ms = g_connect_pending_ms;
    g_last_attempt_ms = g_connect_pending_ms;
    g_retry_delay_ms = kRetryMinMs;
    g_last_disconnect_reason = 0;
    g_pending_ssid = ssid;
    g_pending_password = password;
    g_candidate_ssid = ssid;
    g_candidate_password = password;
    Serial.println("[WIFI] connection queued");
}

bool service_wifi_is_connected() {
    return WiFi.status() == WL_CONNECTED;
}

bool service_wifi_portal_active() {
    return g_portal_running;
}

bool service_wifi_is_connecting() {
    return g_connecting;
}

String service_wifi_connection_message() {
    if (WiFi.status() == WL_CONNECTED) {
        return String("Connected, signal ") + WiFi.RSSI() + " dBm";
    }
    return String(connection_failure_message(g_last_disconnect_reason));
}

void service_wifi_clear_credentials() {
    WiFi.disconnect(true, true);
    service_storage_clear_wifi();
    g_connected = false;
    g_connecting = false;
    g_portal_running = false;
    g_connect_pending = false;
    g_pending_ssid = "";
    g_pending_password = "";
    g_candidate_ssid = "";
    g_candidate_password = "";
    g_retry_delay_ms = kRetryMinMs;
    g_last_attempt_ms = 0;
    g_last_disconnect_reason = 0;
}

} // namespace ptc
