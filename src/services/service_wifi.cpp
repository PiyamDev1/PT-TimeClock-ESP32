#include "service_wifi.h"

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
    if (WiFi.status() == WL_CONNECTED) {
        if (!g_connected) {
            g_connected = true;
            state.wifi_connected = true;
            g_connecting = false;
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

    if (g_connecting && millis() - g_connect_start_ms > 20000) {
        g_connecting = false;
    }

    if (g_portal_running) {
        g_wifi_manager.process();
        return;
    }

    if (millis() - g_last_attempt_ms < 5000) {
        return;
    }

    g_last_attempt_ms = millis();
    if (WiFi.SSID().isEmpty()) {
        return;
    }

    WiFi.begin();
}

void service_wifi_start_portal() {
    if (g_portal_running) {
        return;
    }

    String ap_ssid = build_ap_ssid();
    g_portal_running = true;
    g_wifi_manager.startConfigPortal(ap_ssid.c_str());
}

void service_wifi_connect(const String& ssid, const String& password) {
    g_portal_running = false;
    g_connecting = true;
    g_connect_start_ms = millis();
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
}

} // namespace ptc
