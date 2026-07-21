#include "service_qr.h"

#include <ArduinoJson.h>
#include <time.h>
#include <esp_system.h>

#include "service_auth.h"
#include "service_log.h"

namespace ptc {

namespace {

constexpr uint32_t kMinimumPairingLifetimeSec = 30;

String g_payload;
uint32_t g_last_gen_ms = 0;
uint32_t g_interval_sec = kDefaultQrIntervalSec;

String random_nonce() {
    return service_auth_random_nonce(12);
}

void generate_payload(DeviceConfig& config) {
    if (config.device_secret.length() == 0) {
        return;
    }

    uint32_t ts = static_cast<uint32_t>(time(nullptr));
    String nonce = random_nonce();
    String message = config.device_id + "." + String(ts) + "." + nonce;
    String sig = service_auth_hmac_sha256_base64url(config.device_secret, message);

    StaticJsonDocument<256> doc;
    doc["v"] = 1;
    doc["device_id"] = config.device_id;
    doc["ts"] = ts;
    doc["nonce"] = nonce;
    doc["sig"] = sig;

    String json;
    serializeJson(doc, json);
    const String encoded = service_auth_base64url_encode(
        reinterpret_cast<const uint8_t*>(json.c_str()), json.length());
    g_payload = encoded.isEmpty() ? "" : String("ptc1:") + encoded;
}

} // namespace

void service_qr_init() {
    randomSeed(esp_random());
}

void service_qr_tick(DeviceConfig& config, AppState& state) {
    if (!state.time_sync_ok || !state.device_active || config.device_secret.length() == 0) {
        g_payload = "";
        return;
    }

    const uint32_t configured_interval = config.qr_interval_sec
        ? config.qr_interval_sec
        : kDefaultQrIntervalSec;
    // Manual codes are bound to this payload and remain valid for 30 seconds.
    g_interval_sec = max(configured_interval, kMinimumPairingLifetimeSec);
    uint32_t interval_ms = g_interval_sec * 1000;
    if (!g_payload.isEmpty() && millis() - g_last_gen_ms < interval_ms) {
        return;
    }

    g_last_gen_ms = millis();
    generate_payload(config);
    service_log_add("QR refreshed");
}

String service_qr_payload() {
    return g_payload;
}

uint32_t service_qr_seconds_remaining() {
    if (g_interval_sec == 0 || g_payload.isEmpty()) {
        return 0;
    }
    const uint32_t elapsed = (millis() - g_last_gen_ms) / 1000;
    return elapsed >= g_interval_sec ? 0 : g_interval_sec - elapsed;
}

uint32_t service_qr_interval_sec() {
    return g_interval_sec;
}

uint32_t service_qr_last_refresh_ms() {
    return g_last_gen_ms;
}

} // namespace ptc
