#pragma once

#include <Arduino.h>

namespace ptc {

String service_auth_base64url_encode(const uint8_t* data, size_t length);
String service_auth_random_nonce(size_t byte_count = 16);
String service_auth_sha256_hex(const String& value);
String service_auth_hmac_sha256_base64url(const String& secret, const String& material);
String service_auth_request_signature(
    const String& method,
    const String& path_and_query,
    const String& timestamp,
    const String& nonce,
    const String& body,
    const String& secret);
const char* service_auth_portal_root_ca();

} // namespace ptc
