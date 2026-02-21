#include "service_ota.h"

#include <ArduinoOTA.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <Update.h>
#include <WiFiClientSecure.h>

#include "config.h"
#include "secrets.h"
#include "service_wifi.h"

namespace ptc {

namespace {

bool g_ready = false;
bool g_updating = false;
String g_last_error;
String g_hostname;
bool g_requested = false;
bool g_update_available = false;
bool g_update_ready = false;
bool g_reboot_required = false;
String g_latest_version;
String g_latest_asset_url;
String g_github_status = "Idle";

constexpr const char* kFirmwareAssetName = "firmware.bin";

} // namespace

String github_api_url() {
    return String("https://api.github.com/repos/") + secrets::kGithubOwner + "/" + secrets::kGithubRepo + "/releases/latest";
}

bool github_config_valid() {
    return strlen(secrets::kGithubOwner) > 0 &&
        strlen(secrets::kGithubRepo) > 0 &&
        String(secrets::kGithubOwner) != "OWNER" &&
        String(secrets::kGithubRepo) != "REPO";
}

bool github_token_valid() {
    return strlen(secrets::kGithubToken) > 0;
}

bool github_get_latest_release(String& out_version, String& out_asset_url) {
    if (!github_config_valid()) {
        g_last_error = "GitHub repo not set";
        return false;
    }
    if (!service_wifi_is_connected()) {
        g_last_error = "Wi-Fi offline";
        return false;
    }

    WiFiClientSecure client;
    client.setInsecure();
    HTTPClient http;
    const String url = github_api_url();
    if (!http.begin(client, url)) {
        g_last_error = "GitHub HTTP begin failed";
        return false;
    }
    http.addHeader("User-Agent", "ptc-esp32");
    if (github_token_valid()) {
        http.addHeader("Authorization", String("token ") + secrets::kGithubToken);
    }
    http.addHeader("Accept", "application/vnd.github+json");
    int code = http.GET();
    String response;
    if (code > 0) {
        response = http.getString();
    }
    http.end();
    if (code < 200 || code >= 300) {
        if ((code == 401 || code == 403 || code == 404) && !github_token_valid()) {
            g_last_error = "GitHub token required for private repo";
        } else {
            g_last_error = String("GitHub API failed (") + code + ")";
        }
        return false;
    }

    StaticJsonDocument<4096> doc;
    if (deserializeJson(doc, response) != DeserializationError::Ok) {
        g_last_error = "GitHub JSON parse failed";
        return false;
    }

    out_version = String(doc["tag_name"] | "");
    out_asset_url = "";
    for (JsonObject asset : doc["assets"].as<JsonArray>()) {
        String name = String(asset["name"] | "");
        if (name == kFirmwareAssetName) {
            if (github_token_valid()) {
                out_asset_url = String(asset["url"] | "");
            } else {
                out_asset_url = String(asset["browser_download_url"] | "");
            }
            break;
        }
    }
    if (out_version.length() == 0) {
        g_last_error = "No release tag";
        return false;
    }
    if (out_asset_url.length() == 0) {
        g_last_error = "Missing firmware.bin asset";
        return false;
    }
    return true;
}

bool github_download_asset(const String& asset_url) {
    if (!service_wifi_is_connected()) {
        g_last_error = "Wi-Fi offline";
        return false;
    }

    WiFiClientSecure client;
    client.setInsecure();
    HTTPClient http;
    if (!http.begin(client, asset_url)) {
        g_last_error = "Asset HTTP begin failed";
        return false;
    }
    http.addHeader("User-Agent", "ptc-esp32");
    if (asset_url.startsWith("https://api.github.com/") && github_token_valid()) {
        http.addHeader("Authorization", String("token ") + secrets::kGithubToken);
        http.addHeader("Accept", "application/octet-stream");
    }
    int code = http.GET();
    if (code < 200 || code >= 300) {
        http.end();
        if ((code == 401 || code == 403 || code == 404) && !github_token_valid()) {
            g_last_error = "GitHub token required for private repo";
        } else {
            g_last_error = String("Asset download failed (") + code + ")";
        }
        return false;
    }

    int content_length = http.getSize();
    if (!Update.begin(content_length > 0 ? static_cast<size_t>(content_length) : UPDATE_SIZE_UNKNOWN)) {
        http.end();
        g_last_error = String("Update begin failed: ") + Update.errorString();
        return false;
    }

    WiFiClient* stream = http.getStreamPtr();
    size_t written = Update.writeStream(*stream);
    http.end();
    if (content_length > 0 && static_cast<int>(written) != content_length) {
        g_last_error = "Incomplete firmware download";
        Update.abort();
        return false;
    }
    if (!Update.end(true)) {
        g_last_error = String("Update end failed: ") + Update.errorString();
        return false;
    }

    return true;
}

void service_ota_init() {
    ArduinoOTA.onStart([]() {
        g_updating = true;
    });
    ArduinoOTA.onEnd([]() {
        g_updating = false;
    });
    ArduinoOTA.onError([](ota_error_t error) {
        g_last_error = String("OTA error ") + String(static_cast<int>(error));
        g_updating = false;
    });
}

void service_ota_tick(DeviceConfig& config, AppState& state) {
    (void)state;
    if (!service_wifi_is_connected()) {
        g_ready = false;
        return;
    }

    if (g_hostname.length() == 0 && config.device_id.length() > 0) {
        g_hostname = String("ptc-") + config.device_id;
        ArduinoOTA.setHostname(g_hostname.c_str());
    }

    if (g_requested && !g_ready) {
        ArduinoOTA.begin();
        g_ready = true;
    }

    if (g_ready) {
        ArduinoOTA.handle();
    }
}

void service_ota_request_start() {
    g_requested = true;
}

void service_ota_check_github() {
    g_github_status = "Checking...";
    g_last_error = "";
    String version;
    String asset_url;
    if (!github_get_latest_release(version, asset_url)) {
        g_github_status = String("Error: ") + g_last_error;
        return;
    }

    g_latest_version = version;
    g_latest_asset_url = asset_url;
    if (g_latest_version == kFirmwareVersion) {
        g_update_available = false;
        g_github_status = "Up to date";
    } else {
        g_update_available = true;
        g_github_status = String("Update available ") + g_latest_version;
    }
}

void service_ota_download_github() {
    if (!g_update_available) {
        g_github_status = "No update available";
        return;
    }
    if (g_latest_asset_url.length() == 0) {
        g_github_status = "Missing asset URL";
        return;
    }
    g_github_status = "Downloading...";
    g_last_error = "";
    if (!github_download_asset(g_latest_asset_url)) {
        g_github_status = String("Error: ") + g_last_error;
        return;
    }
    g_update_ready = true;
    g_reboot_required = true;
    g_github_status = "Installed. Reboot when ready";
}

void service_ota_install_latest_github() {
    g_github_status = "Checking...";
    g_last_error = "";
    String version;
    String asset_url;
    if (!github_get_latest_release(version, asset_url)) {
        g_github_status = String("Error: ") + g_last_error;
        return;
    }

    g_latest_version = version;
    g_latest_asset_url = asset_url;
    if (g_latest_version == kFirmwareVersion) {
        g_update_available = false;
        g_github_status = "Up to date";
        return;
    }

    g_update_available = true;
    g_github_status = String("Installing ") + g_latest_version + "...";
    if (!github_download_asset(g_latest_asset_url)) {
        g_github_status = String("Error: ") + g_last_error;
        return;
    }

    g_update_ready = true;
    g_reboot_required = true;
    g_github_status = String("Installed ") + g_latest_version + ". Reboot when ready";
}

void service_ota_apply_update() {
    if (g_reboot_required || g_update_ready) {
        ESP.restart();
    }
}

bool service_ota_update_available() {
    return g_update_available;
}

bool service_ota_update_ready() {
    return g_update_ready;
}

bool service_ota_reboot_required() {
    return g_reboot_required;
}

String service_ota_github_status() {
    return g_github_status;
}

bool service_ota_ready() {
    return g_ready;
}

bool service_ota_updating() {
    return g_updating;
}

String service_ota_last_error() {
    return g_last_error;
}

} // namespace ptc
