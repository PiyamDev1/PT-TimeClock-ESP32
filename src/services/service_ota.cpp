#include "service_ota.h"

#include <ArduinoJson.h>
#include <ArduinoOTA.h>
#include <HTTPClient.h>
#include <SD.h>
#include <Update.h>
#include <WiFiClientSecure.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include <mbedtls/sha256.h>

#include "config.h"
#include "secrets.h"
#include "service_storage.h"
#include "service_wifi.h"

namespace ptc {

namespace {

constexpr const char* kFirmwareAssetName = "firmware.bin";
constexpr const char* kUpdateDirectory = "/ptc/update";
constexpr const char* kFirmwarePath = "/ptc/update/firmware.bin";
constexpr const char* kPartialFirmwarePath = "/ptc/update/firmware.bin.part";
constexpr const char* kMetadataPath = "/ptc/update/metadata.json";
constexpr size_t kMinimumFirmwareBytes = 64U * 1024U;
constexpr uint32_t kRebootDelayMs = 1500;
constexpr uint32_t kInitialAutoCheckDelayMs = 20000;
constexpr uint32_t kPeriodicAutoCheckMs = 6U * 60U * 60U * 1000U;
constexpr uint32_t kFailedAutoCheckRetryMs = 15U * 60U * 1000U;

enum class OtaCommandType : uint8_t {
    kCheck,
    kDownload,
    kInstall,
};

struct OtaCommand {
    OtaCommandType type = OtaCommandType::kCheck;
    String version;
    String asset_url;
    String digest;
    size_t expected_size = 0;
};

struct OtaResult {
    OtaCommandType type = OtaCommandType::kCheck;
    bool success = false;
    String error;
    String version;
    String asset_url;
    String digest;
    size_t asset_size = 0;
};

bool g_ready = false;
bool g_updating = false;
bool g_requested = true;
String g_last_error;
String g_hostname;
String g_latest_version;
String g_latest_asset_url;
String g_latest_digest;
size_t g_latest_asset_size = 0;
String g_github_status = "Idle";
OtaState g_state = OtaState::kIdle;
volatile uint8_t g_progress = 0;
uint32_t g_reboot_started_ms = 0;
uint32_t g_wifi_connected_since_ms = 0;
uint32_t g_last_release_check_ms = 0;
QueueHandle_t g_command_queue = nullptr;
QueueHandle_t g_result_queue = nullptr;
TaskHandle_t g_worker_task = nullptr;

String github_api_url() {
    return String("https://api.github.com/repos/") + secrets::kGithubOwner + "/" +
        secrets::kGithubRepo + "/releases/latest";
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

size_t version_numbers(const String& version, uint32_t* parts, size_t max_parts) {
    size_t count = 0;
    size_t index = 0;
    while (index < version.length() && count < max_parts) {
        while (index < version.length() &&
            (version[index] < '0' || version[index] > '9')) {
            ++index;
        }
        if (index >= version.length()) {
            break;
        }

        uint32_t value = 0;
        while (index < version.length() &&
            version[index] >= '0' && version[index] <= '9') {
            value = value * 10U + static_cast<uint32_t>(version[index] - '0');
            ++index;
        }
        parts[count++] = value;
    }
    return count;
}

bool is_version_newer(const String& candidate, const String& current) {
    constexpr size_t kMaxVersionParts = 6;
    uint32_t candidate_parts[kMaxVersionParts] = {0};
    uint32_t current_parts[kMaxVersionParts] = {0};
    const size_t candidate_count =
        version_numbers(candidate, candidate_parts, kMaxVersionParts);
    const size_t current_count =
        version_numbers(current, current_parts, kMaxVersionParts);
    const size_t compare_count = max(candidate_count, current_count);

    for (size_t i = 0; i < compare_count; ++i) {
        if (candidate_parts[i] != current_parts[i]) {
            return candidate_parts[i] > current_parts[i];
        }
    }
    return false;
}

String normalize_digest(const String& digest) {
    if (digest.startsWith("sha256:")) {
        return digest.substring(7);
    }
    return digest;
}

String digest_to_hex(const uint8_t digest[32]) {
    static constexpr char kHex[] = "0123456789abcdef";
    String value;
    value.reserve(64);
    for (size_t i = 0; i < 32; ++i) {
        value += kHex[(digest[i] >> 4) & 0x0f];
        value += kHex[digest[i] & 0x0f];
    }
    return value;
}

bool write_staged_metadata(
    const String& version,
    size_t size,
    const String& digest,
    String& error) {
    SD.mkdir(kUpdateDirectory);
    SD.remove(kMetadataPath);
    File file = SD.open(kMetadataPath, FILE_WRITE);
    if (!file) {
        error = "Cannot save update metadata";
        return false;
    }

    StaticJsonDocument<256> doc;
    doc["version"] = version;
    doc["size"] = static_cast<uint32_t>(size);
    doc["sha256"] = digest;
    const bool ok = serializeJson(doc, file) > 0;
    file.close();
    if (!ok) {
        SD.remove(kMetadataPath);
        error = "Cannot write update metadata";
    }
    return ok;
}

bool load_staged_metadata(String& version, size_t& size, String& digest) {
    if (!service_storage_sd_ready() || !SD.exists(kFirmwarePath) || !SD.exists(kMetadataPath)) {
        return false;
    }

    File metadata = SD.open(kMetadataPath, FILE_READ);
    if (!metadata) {
        return false;
    }
    StaticJsonDocument<256> doc;
    const DeserializationError parse_error = deserializeJson(doc, metadata);
    metadata.close();
    if (parse_error != DeserializationError::Ok) {
        return false;
    }

    File firmware = SD.open(kFirmwarePath, FILE_READ);
    if (!firmware) {
        return false;
    }
    const size_t actual_size = firmware.size();
    const int first_byte = firmware.read();
    firmware.close();

    version = String(doc["version"] | "");
    size = static_cast<size_t>(doc["size"] | 0U);
    digest = String(doc["sha256"] | "");
    return version.length() > 0 &&
        size >= kMinimumFirmwareBytes &&
        actual_size == size &&
        first_byte == 0xE9;
}

void remove_staged_update() {
    if (!service_storage_sd_ready()) {
        return;
    }
    SD.remove(kPartialFirmwarePath);
    SD.remove(kFirmwarePath);
    SD.remove(kMetadataPath);
}

bool fetch_latest_release(OtaResult& result) {
    if (!github_config_valid()) {
        result.error = "GitHub repo not set";
        return false;
    }
    if (!service_wifi_is_connected()) {
        result.error = "Wi-Fi offline";
        return false;
    }

    WiFiClientSecure client;
    client.setInsecure();
    HTTPClient http;
    if (!http.begin(client, github_api_url())) {
        result.error = "GitHub HTTP begin failed";
        return false;
    }
    http.setTimeout(15000);
    http.addHeader("User-Agent", "ptc-esp32");
    http.addHeader("Accept", "application/vnd.github+json");
    if (github_token_valid()) {
        http.addHeader("Authorization", String("token ") + secrets::kGithubToken);
    }

    const int code = http.GET();
    String response;
    if (code > 0) {
        response = http.getString();
    }
    http.end();
    if (code < 200 || code >= 300) {
        result.error = String("GitHub API failed (") + code + ")";
        return false;
    }

    DynamicJsonDocument doc(6144);
    if (deserializeJson(doc, response) != DeserializationError::Ok) {
        result.error = "GitHub JSON parse failed";
        return false;
    }

    result.version = String(doc["tag_name"] | "");
    for (JsonObject asset : doc["assets"].as<JsonArray>()) {
        if (String(asset["name"] | "") != kFirmwareAssetName) {
            continue;
        }
        result.asset_url = github_token_valid()
            ? String(asset["url"] | "")
            : String(asset["browser_download_url"] | "");
        result.asset_size = static_cast<size_t>(asset["size"] | 0U);
        result.digest = String(asset["digest"] | "");
        break;
    }

    if (result.version.length() == 0) {
        result.error = "No release tag";
        return false;
    }
    if (result.asset_url.length() == 0 || result.asset_size < kMinimumFirmwareBytes) {
        result.error = "Missing or invalid firmware.bin";
        return false;
    }
    return true;
}

bool download_firmware(const OtaCommand& command, OtaResult& result) {
    if (!service_wifi_is_connected()) {
        result.error = "Wi-Fi offline";
        return false;
    }
    if (!service_storage_sd_ready()) {
        result.error = "Memory card not mounted";
        return false;
    }

    StorageStatus storage;
    if (!service_storage_get_status(storage) ||
        storage.free_bytes < command.expected_size + (1024U * 1024U)) {
        result.error = "Not enough memory card space";
        return false;
    }

    SD.mkdir(kUpdateDirectory);
    SD.remove(kPartialFirmwarePath);
    File target = SD.open(kPartialFirmwarePath, FILE_WRITE);
    if (!target) {
        result.error = "Cannot create firmware.bin";
        return false;
    }

    WiFiClientSecure client;
    client.setInsecure();
    HTTPClient http;
    if (!http.begin(client, command.asset_url)) {
        target.close();
        SD.remove(kPartialFirmwarePath);
        result.error = "Asset HTTP begin failed";
        return false;
    }
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    http.setTimeout(20000);
    http.addHeader("User-Agent", "ptc-esp32");
    if (command.asset_url.startsWith("https://api.github.com/") && github_token_valid()) {
        http.addHeader("Authorization", String("token ") + secrets::kGithubToken);
        http.addHeader("Accept", "application/octet-stream");
    }

    const int code = http.GET();
    if (code < 200 || code >= 300) {
        http.end();
        target.close();
        SD.remove(kPartialFirmwarePath);
        result.error = String("Asset download failed (") + code + ")";
        return false;
    }

    const int response_size = http.getSize();
    if (response_size > 0 && static_cast<size_t>(response_size) != command.expected_size) {
        http.end();
        target.close();
        SD.remove(kPartialFirmwarePath);
        result.error = "Firmware size changed";
        return false;
    }

    mbedtls_sha256_context sha;
    mbedtls_sha256_init(&sha);
    mbedtls_sha256_starts_ret(&sha, 0);

    WiFiClient* stream = http.getStreamPtr();
    uint8_t buffer[4096];
    size_t written = 0;
    bool write_ok = true;
    uint32_t last_data_ms = millis();
    while ((http.connected() || stream->available()) && written < command.expected_size) {
        const size_t available = stream->available();
        if (available == 0) {
            if (millis() - last_data_ms > 20000) {
                write_ok = false;
                result.error = "Firmware download timed out";
                break;
            }
            delay(2);
            continue;
        }

        const size_t requested = min(
            min(available, sizeof(buffer)),
            command.expected_size - written);
        const size_t received = stream->readBytes(buffer, requested);
        if (received == 0) {
            continue;
        }
        last_data_ms = millis();
        if (target.write(buffer, received) != received) {
            write_ok = false;
            result.error = "Memory card write failed";
            break;
        }
        mbedtls_sha256_update_ret(&sha, buffer, received);
        written += received;
        g_progress = static_cast<uint8_t>((written * 100U) / command.expected_size);
        delay(1);
    }

    uint8_t digest_bytes[32];
    mbedtls_sha256_finish_ret(&sha, digest_bytes);
    mbedtls_sha256_free(&sha);
    target.flush();
    target.close();
    http.end();

    if (!write_ok || written != command.expected_size) {
        SD.remove(kPartialFirmwarePath);
        if (result.error.length() == 0) {
            result.error = "Incomplete firmware download";
        }
        return false;
    }

    File validation = SD.open(kPartialFirmwarePath, FILE_READ);
    const bool valid_image = validation && validation.size() == command.expected_size &&
        validation.read() == 0xE9;
    if (validation) {
        validation.close();
    }
    if (!valid_image) {
        SD.remove(kPartialFirmwarePath);
        result.error = "Invalid firmware image";
        return false;
    }

    const String actual_digest = digest_to_hex(digest_bytes);
    const String expected_digest = normalize_digest(command.digest);
    if (expected_digest.length() > 0 && !actual_digest.equalsIgnoreCase(expected_digest)) {
        SD.remove(kPartialFirmwarePath);
        result.error = "Firmware checksum failed";
        return false;
    }

    SD.remove(kFirmwarePath);
    if (!SD.rename(kPartialFirmwarePath, kFirmwarePath)) {
        SD.remove(kPartialFirmwarePath);
        result.error = "Cannot finalize firmware.bin";
        return false;
    }
    if (!write_staged_metadata(
            command.version,
            command.expected_size,
            actual_digest,
            result.error)) {
        SD.remove(kFirmwarePath);
        return false;
    }

    result.version = command.version;
    result.asset_size = command.expected_size;
    result.digest = actual_digest;
    g_progress = 100;
    return true;
}

bool install_staged_firmware(const OtaCommand& command, OtaResult& result) {
    if (!service_storage_sd_ready()) {
        result.error = "Memory card not mounted";
        return false;
    }

    File firmware = SD.open(kFirmwarePath, FILE_READ);
    if (!firmware) {
        result.error = "firmware.bin not found";
        return false;
    }

    const size_t firmware_size = firmware.size();
    if (firmware_size < kMinimumFirmwareBytes || firmware.read() != 0xE9) {
        firmware.close();
        result.error = "Invalid firmware.bin";
        return false;
    }
    firmware.seek(0);

    if (!Update.begin(firmware_size, U_FLASH)) {
        firmware.close();
        result.error = String("Update begin failed: ") + Update.errorString();
        return false;
    }

    uint8_t buffer[4096];
    size_t written = 0;
    while (written < firmware_size) {
        const size_t requested = min(sizeof(buffer), firmware_size - written);
        const size_t received = firmware.read(buffer, requested);
        if (received == 0 || Update.write(buffer, received) != received) {
            Update.abort();
            firmware.close();
            result.error = String("Firmware install failed: ") + Update.errorString();
            return false;
        }
        written += received;
        g_progress = static_cast<uint8_t>((written * 100U) / firmware_size);
        delay(1);
    }
    firmware.close();

    if (!Update.end(true)) {
        result.error = String("Update end failed: ") + Update.errorString();
        return false;
    }

    result.version = command.version;
    result.asset_size = firmware_size;
    g_progress = 100;
    return true;
}

void ota_worker(void*) {
    for (;;) {
        OtaCommand* command = nullptr;
        if (xQueueReceive(g_command_queue, &command, portMAX_DELAY) != pdTRUE || !command) {
            continue;
        }

        OtaResult* result = new OtaResult();
        result->type = command->type;
        switch (command->type) {
            case OtaCommandType::kCheck:
                result->success = fetch_latest_release(*result);
                break;
            case OtaCommandType::kDownload:
                result->success = download_firmware(*command, *result);
                break;
            case OtaCommandType::kInstall:
                result->success = install_staged_firmware(*command, *result);
                break;
        }
        delete command;
        xQueueSend(g_result_queue, &result, portMAX_DELAY);
    }
}

bool enqueue_command(OtaCommand* command) {
    if (!command || !g_command_queue) {
        delete command;
        return false;
    }
    if (xQueueSend(g_command_queue, &command, 0) != pdTRUE) {
        delete command;
        return false;
    }
    return true;
}

void set_error(const String& error) {
    g_state = OtaState::kError;
    g_progress = 0;
    g_last_error = error;
    g_github_status = String("Error: ") + error;
    Serial.printf("[OTA] %s\n", error.c_str());
}

void consume_result(OtaResult* result) {
    if (!result) {
        return;
    }
    if (!result->success) {
        set_error(result->error);
        delete result;
        return;
    }

    switch (result->type) {
        case OtaCommandType::kCheck:
            g_latest_version = result->version;
            g_latest_asset_url = result->asset_url;
            g_latest_asset_size = result->asset_size;
            g_latest_digest = result->digest;
            g_progress = 0;
            if (!is_version_newer(g_latest_version, kFirmwareVersion)) {
                g_state = OtaState::kUpToDate;
                g_github_status = String("Up to date (") + kFirmwareVersion + ")";
                remove_staged_update();
            } else {
                g_state = OtaState::kAvailable;
                g_github_status = String("Update available: ") + g_latest_version;
                Serial.printf("[OTA] update available version=%s bytes=%u\n",
                    g_latest_version.c_str(),
                    static_cast<unsigned>(g_latest_asset_size));
            }
            break;
        case OtaCommandType::kDownload:
            g_state = OtaState::kDownloaded;
            g_progress = 100;
            g_github_status = String("Downloaded ") + result->version + " to memory card";
            Serial.printf("[OTA] staged version=%s bytes=%u\n",
                result->version.c_str(),
                static_cast<unsigned>(result->asset_size));
            break;
        case OtaCommandType::kInstall:
            g_state = OtaState::kRebooting;
            g_progress = 100;
            g_github_status = String("Installed ") + result->version + ". Rebooting";
            g_reboot_started_ms = millis();
            Serial.printf("[OTA] installed version=%s; rebooting\n", result->version.c_str());
            break;
    }
    delete result;
}

} // namespace

void service_ota_init() {
    g_requested = true;
    g_ready = false;
    g_updating = false;
    g_last_error = "";
    g_state = OtaState::kIdle;
    g_progress = 0;
    g_wifi_connected_since_ms = 0;
    g_last_release_check_ms = 0;

    g_command_queue = xQueueCreate(1, sizeof(OtaCommand*));
    g_result_queue = xQueueCreate(1, sizeof(OtaResult*));
    if (!g_command_queue || !g_result_queue ||
        xTaskCreatePinnedToCore(
            ota_worker,
            "ptc_ota",
            16384,
            nullptr,
            1,
            &g_worker_task,
            0) != pdPASS) {
        set_error("Cannot start update worker");
    }

    String staged_version;
    String staged_digest;
    size_t staged_size = 0;
    if (load_staged_metadata(staged_version, staged_size, staged_digest)) {
        if (!is_version_newer(staged_version, kFirmwareVersion)) {
            remove_staged_update();
        } else {
            g_latest_version = staged_version;
            g_latest_asset_size = staged_size;
            g_latest_digest = staged_digest;
            g_state = OtaState::kDownloaded;
            g_progress = 100;
            g_github_status = String("Ready to install ") + staged_version;
            Serial.printf("[OTA] staged update restored version=%s\n", staged_version.c_str());
        }
    }

    ArduinoOTA.onStart([]() {
        g_updating = true;
        Serial.println("[OTA] network update started");
    });
    ArduinoOTA.onEnd([]() {
        g_updating = false;
        Serial.println("[OTA] network update complete");
    });
    ArduinoOTA.onError([](ota_error_t error) {
        g_last_error = String("OTA error ") + String(static_cast<int>(error));
        g_updating = false;
        Serial.printf("[OTA] network error=%u\n", static_cast<unsigned>(error));
    });
    Serial.println("[OTA] enabled; waiting for Wi-Fi");
}

void service_ota_tick(DeviceConfig& config, AppState& state) {
    OtaResult* result = nullptr;
    if (g_result_queue && xQueueReceive(g_result_queue, &result, 0) == pdTRUE) {
        consume_result(result);
    }

    if (g_state == OtaState::kRebooting &&
        millis() - g_reboot_started_ms >= kRebootDelayMs) {
        ESP.restart();
        return;
    }

    if (service_ota_exclusive()) {
        return;
    }
    if (!service_wifi_is_connected()) {
        g_ready = false;
        g_wifi_connected_since_ms = 0;
        return;
    }
    if (g_wifi_connected_since_ms == 0) {
        g_wifi_connected_since_ms = millis();
    }

    if (g_hostname.length() == 0 && config.device_id.length() > 0) {
        g_hostname = String("ptc-") + config.device_id;
        ArduinoOTA.setHostname(g_hostname.c_str());
    }
    if (g_requested && !g_ready) {
        ArduinoOTA.begin();
        g_ready = true;
        Serial.printf("[OTA] ready hostname=%s ip=%s\n",
            g_hostname.c_str(),
            WiFi.localIP().toString().c_str());
    }
    if (g_ready) {
        ArduinoOTA.handle();
    }

    const bool checkable_state =
        g_state == OtaState::kIdle ||
        g_state == OtaState::kUpToDate ||
        g_state == OtaState::kError;
    if (!state.provisioning_complete || !checkable_state) {
        return;
    }

    const uint32_t now = millis();
    const bool first_check_due =
        g_last_release_check_ms == 0 &&
        now - g_wifi_connected_since_ms >= kInitialAutoCheckDelayMs;
    const uint32_t check_interval =
        g_state == OtaState::kError ? kFailedAutoCheckRetryMs : kPeriodicAutoCheckMs;
    const bool periodic_check_due =
        g_last_release_check_ms != 0 &&
        now - g_last_release_check_ms >= check_interval;
    if (first_check_due || periodic_check_due) {
        Serial.println("[OTA] automatic release check");
        service_ota_check_github();
    }
}

void service_ota_request_start() {
    g_requested = true;
}

void service_ota_check_github() {
    if (service_ota_exclusive()) {
        return;
    }
    auto* command = new OtaCommand();
    command->type = OtaCommandType::kCheck;
    if (!enqueue_command(command)) {
        set_error("Update worker busy");
        return;
    }
    g_state = OtaState::kChecking;
    g_last_release_check_ms = millis();
    g_progress = 0;
    g_last_error = "";
    g_github_status = "Checking for updates";
    Serial.println("[OTA] check queued");
}

void service_ota_download_github() {
    if (g_state != OtaState::kAvailable || g_latest_asset_url.length() == 0) {
        return;
    }
    auto* command = new OtaCommand();
    command->type = OtaCommandType::kDownload;
    command->version = g_latest_version;
    command->asset_url = g_latest_asset_url;
    command->digest = g_latest_digest;
    command->expected_size = g_latest_asset_size;
    if (!enqueue_command(command)) {
        set_error("Update worker busy");
        return;
    }
    g_state = OtaState::kDownloading;
    g_progress = 0;
    g_last_error = "";
    g_github_status = String("Downloading ") + g_latest_version;
    Serial.println("[OTA] download queued");
}

void service_ota_install_latest_github() {
    if (g_state == OtaState::kAvailable) {
        service_ota_download_github();
    } else if (g_state == OtaState::kDownloaded) {
        service_ota_apply_update();
    } else {
        service_ota_check_github();
    }
}

void service_ota_apply_update() {
    if (g_state != OtaState::kDownloaded) {
        return;
    }
    auto* command = new OtaCommand();
    command->type = OtaCommandType::kInstall;
    command->version = g_latest_version;
    command->expected_size = g_latest_asset_size;
    if (!enqueue_command(command)) {
        set_error("Update worker busy");
        return;
    }
    g_state = OtaState::kInstalling;
    g_progress = 0;
    g_last_error = "";
    g_github_status = String("Installing ") + g_latest_version;
    Serial.println("[OTA] install queued");
}

bool service_ota_update_available() {
    return g_state == OtaState::kAvailable;
}

bool service_ota_update_ready() {
    return g_state == OtaState::kDownloaded;
}

bool service_ota_reboot_required() {
    return g_state == OtaState::kRebooting;
}

String service_ota_github_status() {
    return g_github_status;
}

String service_ota_latest_version() {
    return g_latest_version;
}

uint8_t service_ota_progress() {
    return g_progress;
}

OtaState service_ota_state() {
    return g_state;
}

bool service_ota_exclusive() {
    return g_updating ||
        g_state == OtaState::kChecking ||
        g_state == OtaState::kDownloading ||
        g_state == OtaState::kInstalling ||
        g_state == OtaState::kRebooting;
}

bool service_ota_enabled() {
    return g_requested;
}

bool service_ota_ready() {
    return g_ready;
}

bool service_ota_updating() {
    return g_updating ||
        g_state == OtaState::kDownloading ||
        g_state == OtaState::kInstalling;
}

String service_ota_last_error() {
    return g_last_error;
}

} // namespace ptc
