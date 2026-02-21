#include "service_wifi.h"

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiManager.h>

#include "service_log.h"

namespace {

WiFiManager g_wifi_manager;
bool g_portal_running = false;
bool g_connected = false;
uint32_t g_last_attempt_ms = 0;
bool g_connecting = false;
uint32_t g_connect_start_ms = 0;
uint32_t g_retry_delay_ms = 2000;

constexpr uint32_t kRetryMinMs = 2000;
constexpr uint32_t kRetryMaxMs = 60000;
constexpr uint32_t kConnectTimeoutMs = 20000;

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
    g_wifi_manager.setConfigPortalBlocking(false);
    g_wifi_manager.setConfigPortalTimeout(180);
}

void service_wifi_tick(DeviceConfig& config, AppState& state) {
    (void)config;

    if (WiFi.status() == WL_CONNECTED) {
        if (!g_connected) {
            g_connected = true;
            state.wifi_connected = true;
            g_connecting = false;
            g_retry_delay_ms = kRetryMinMs;
            service_log_add("Wi-Fi connected");
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

    if (WiFi.SSID().isEmpty()) {
        return;
    }

    g_last_attempt_ms = now;
    g_connecting = true;
    g_connect_start_ms = now;
    WiFi.begin();

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
    g_connect_start_ms = millis();
    g_last_attempt_ms = g_connect_start_ms;
    g_retry_delay_ms = kRetryMinMs;
    WiFi.begin(ssid.c_str(), password.c_str());
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

void service_wifi_clear_credentials() {
    WiFi.disconnect(true, true);
    g_connected = false;
    g_connecting = false;
    g_portal_running = false;
    g_retry_delay_ms = kRetryMinMs;
    g_last_attempt_ms = 0;
}

} // namespace ptc
