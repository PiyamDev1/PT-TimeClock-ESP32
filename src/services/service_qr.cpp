#include "service_qr.h"

#include <ArduinoJson.h>
#include <mbedtls/md.h>
#include <mbedtls/base64.h>
#include <vector>
#include <time.h>
#include <esp_system.h>

#include "service_log.h"

namespace ptc {

namespace {

String g_payload;
String g_manual_code;
uint32_t g_last_gen_ms = 0;
uint32_t g_interval_sec = kDefaultQrIntervalSec;

String base64url_encode(const uint8_t* data, size_t len) {
    size_t out_len = 0;
    size_t buf_len = ((len + 2) / 3) * 4 + 1;
    std::vector<uint8_t> out(buf_len);

    if (mbedtls_base64_encode(out.data(), out.size(), &out_len, data, len) != 0) {
        return "";
    }

    String encoded(reinterpret_cast<char*>(out.data()), out_len);
    encoded.replace("+", "-");
    encoded.replace("/", "_");
    encoded.replace("=", "");
    return encoded;
}

String hmac_sha256_base64url(const String& secret, const String& message) {
    uint8_t output[32];
    mbedtls_md_context_t ctx;
    mbedtls_md_init(&ctx);
    const mbedtls_md_info_t* info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    mbedtls_md_setup(&ctx, info, 1);
    mbedtls_md_hmac_starts(&ctx, reinterpret_cast<const uint8_t*>(secret.c_str()), secret.length());
    mbedtls_md_hmac_update(&ctx, reinterpret_cast<const uint8_t*>(message.c_str()), message.length());
    mbedtls_md_hmac_finish(&ctx, output);
    mbedtls_md_free(&ctx);

    return base64url_encode(output, sizeof(output));
}

String random_nonce() {
    const char* chars = "abcdefghijklmnopqrstuvwxyz0123456789";
    String out;
    for (int i = 0; i < 10; ++i) {
        int idx = random(0, 36);
        out += chars[idx];
    }
    return out;
}

void generate_payload(DeviceConfig& config) {
    if (config.device_secret.length() == 0) {
        return;
    }

    uint32_t ts = static_cast<uint32_t>(time(nullptr));
    String nonce = random_nonce();
    String message = config.device_id + "." + String(ts) + "." + nonce;
    String sig = hmac_sha256_base64url(config.device_secret, message);

    StaticJsonDocument<256> doc;
    doc["v"] = 1;
    doc["device_id"] = config.device_id;
    doc["ts"] = ts;
    doc["nonce"] = nonce;
    doc["sig"] = sig;

    String json;
    serializeJson(doc, json);
    g_payload = String("ptc1:") + json;
    g_manual_code = message;
}

} // namespace

void service_qr_init() {
    randomSeed(esp_random());
}

void service_qr_tick(DeviceConfig& config, AppState& state) {
    if (!state.time_sync_ok || config.device_secret.length() == 0) {
        return;
    }

    g_interval_sec = config.qr_interval_sec ? config.qr_interval_sec : kDefaultQrIntervalSec;
    uint32_t interval_ms = g_interval_sec * 1000;
    if (millis() - g_last_gen_ms < interval_ms) {
        return;
    }

    g_last_gen_ms = millis();
    generate_payload(config);
    service_log_add("QR refreshed");
}

String service_qr_payload() {
    return g_payload;
}

String service_qr_manual_code() {
    return g_manual_code;
}

uint32_t service_qr_seconds_remaining() {
    if (g_interval_sec == 0) {
        return 0;
    }
    uint32_t elapsed = millis() / 1000;
    uint32_t remain = g_interval_sec - (elapsed % g_interval_sec);
    return remain == g_interval_sec ? 0 : remain;
}

uint32_t service_qr_interval_sec() {
    return g_interval_sec;
}

uint32_t service_qr_last_refresh_ms() {
    return g_last_gen_ms;
}

} // namespace ptc
