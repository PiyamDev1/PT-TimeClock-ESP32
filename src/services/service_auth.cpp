#include "service_auth.h"

#include <esp_system.h>
#include <mbedtls/base64.h>
#include <mbedtls/md.h>
#include <mbedtls/sha256.h>

#include <vector>

namespace ptc {

namespace {

// ISRG Root X1 anchors the default Let's Encrypt YR1 chain used by PT Portal.
constexpr char kIsrgRootX1[] = R"PEM(-----BEGIN CERTIFICATE-----
MIIFazCCA1OgAwIBAgIRAIIQz7DSQONZRGPgu2OCiwAwDQYJKoZIhvcNAQELBQAw
TzELMAkGA1UEBhMCVVMxKTAnBgNVBAoTIEludGVybmV0IFNlY3VyaXR5IFJlc2Vh
cmNoIEdyb3VwMRUwEwYDVQQDEwxJU1JHIFJvb3QgWDEwHhcNMTUwNjA0MTEwNDM4
WhcNMzUwNjA0MTEwNDM4WjBPMQswCQYDVQQGEwJVUzEpMCcGA1UEChMgSW50ZXJu
ZXQgU2VjdXJpdHkgUmVzZWFyY2ggR3JvdXAxFTATBgNVBAMTDElTUkcgUm9vdCBY
MTCCAiIwDQYJKoZIhvcNAQEBBQADggIPADCCAgoCggIBAK3oJHP0FDfzm54rVygc
h77ct984kIxuPOZXoHj3dcKi/vVqbvYATyjb3miGbESTtrFj/RQSa78f0uoxmyF+
0TM8ukj13Xnfs7j/EvEhmkvBioZxaUpmZmyPfjxwv60pIgbz5MDmgK7iS4+3mX6U
A5/TR5d8mUgjU+g4rk8Kb4Mu0UlXjIB0ttov0DiNewNwIRt18jA8+o+u3dpjq+sW
T8KOEUt+zwvo/7V3LvSye0rgTBIlDHCNAymg4VMk7BPZ7hm/ELNKjD+Jo2FR3qyH
B5T0Y3HsLuJvW5iB4YlcNHlsdu87kGJ55tukmi8mxdAQ4Q7e2RCOFvu396j3x+UC
B5iPNgiV5+I3lg02dZ77DnKxHZu8A/lJBdiB3QW0KtZB6awBdpUKD9jf1b0SHzUv
KBds0pjBqAlkd25HN7rOrFleaJ1/ctaJxQZBKT5ZPt0m9STJEadao0xAH0ahmbWn
OlFuhjuefXKnEgV4We0+UXgVCwOPjdAvBbI+e0ocS3MFEvzG6uBQE3xDk3SzynTn
jh8BCNAw1FtxNrQHusEwMFxIt4I7mKZ9YIqioymCzLq9gwQbooMDQaHWBfEbwrbw
qHyGO0aoSCqI3Haadr8faqU9GY/rOPNk3sgrDQoo//fb4hVC1CLQJ13hef4Y53CI
rU7m2Ys6xt0nUW7/vGT1M0NPAgMBAAGjQjBAMA4GA1UdDwEB/wQEAwIBBjAPBgNV
HRMBAf8EBTADAQH/MB0GA1UdDgQWBBR5tFnme7bl5AFzgAiIyBpY9umbbjANBgkq
hkiG9w0BAQsFAAOCAgEAVR9YqbyyqFDQDLHYGmkgJykIrGF1XIpu+ILlaS/V9lZL
ubhzEFnTIZd+50xx+7LSYK05qAvqFyFWhfFQDlnrzuBZ6brJFe+GnY+EgPbk6ZGQ
3BebYhtF8GaV0nxvwuo77x/Py9auJ/GpsMiu/X1+mvoiBOv/2X/qkSsisRcOj/KK
NFtY2PwByVS5uCbMiogziUwthDyC3+6WVwW6LLv3xLfHTjuCvjHIInNzktHCgKQ5
ORAzI4JMPJ+GslWYHb4phowim57iaztXOoJwTdwJx4nLCgdNbOhdjsnvzqvHu7Ur
TkXWStAmzOVyyghqpZXjFaH3pO3JLF+l+/+sKAIuvtd7u+Nxe5AW0wdeRlN8NwdC
jNPElpzVmbUq4JUagEiuTDkHzsxHpFKVK7q4+63SM1N95R1NbdWhscdCb+ZAJzVc
oyi3B43njTOQ5yOf+1CceWxG1bQVs5ZufpsMljq4Ui0/1lvh+wjChP4kqKOJ2qxq
4RgqsahDYVvTH9w7jXbyLeiNdd8XM2w9U/t7y0Ff/9yi0GE44Za4rF2LN9d11TPA
mRGunUHBcnWEvgJBQl9nJEiU0Zsnvgc/ubhPgXRR4Xq37Z0j4r7g1SgEEzwxA57d
emyPxgcYxn/eR44/KJ4EBs+lVDR3veyJm+kXQ99b21/+jh5Xos1AnX5iItreGCc=
-----END CERTIFICATE-----
)PEM";

} // namespace

String service_auth_base64url_encode(const uint8_t* data, size_t length) {
    size_t output_length = 0;
    std::vector<uint8_t> output(((length + 2) / 3) * 4 + 1);
    if (mbedtls_base64_encode(
            output.data(), output.size(), &output_length, data, length) != 0) {
        return "";
    }

    String encoded(reinterpret_cast<char*>(output.data()), output_length);
    encoded.replace("+", "-");
    encoded.replace("/", "_");
    encoded.replace("=", "");
    return encoded;
}

String service_auth_random_nonce(size_t byte_count) {
    std::vector<uint8_t> bytes(byte_count);
    esp_fill_random(bytes.data(), bytes.size());
    return service_auth_base64url_encode(bytes.data(), bytes.size());
}

String service_auth_sha256_hex(const String& value) {
    uint8_t digest[32] = {0};
    mbedtls_sha256_ret(
        reinterpret_cast<const uint8_t*>(value.c_str()), value.length(), digest, 0);

    static constexpr char kHex[] = "0123456789abcdef";
    char encoded[65] = {0};
    for (size_t i = 0; i < sizeof(digest); ++i) {
        encoded[i * 2] = kHex[digest[i] >> 4];
        encoded[i * 2 + 1] = kHex[digest[i] & 0x0F];
    }
    return String(encoded);
}

String service_auth_hmac_sha256_base64url(const String& secret, const String& material) {
    uint8_t digest[32] = {0};
    mbedtls_md_context_t context;
    mbedtls_md_init(&context);
    const mbedtls_md_info_t* info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    if (!info || mbedtls_md_setup(&context, info, 1) != 0) {
        mbedtls_md_free(&context);
        return "";
    }
    mbedtls_md_hmac_starts(
        &context,
        reinterpret_cast<const uint8_t*>(secret.c_str()),
        secret.length());
    mbedtls_md_hmac_update(
        &context,
        reinterpret_cast<const uint8_t*>(material.c_str()),
        material.length());
    mbedtls_md_hmac_finish(&context, digest);
    mbedtls_md_free(&context);
    return service_auth_base64url_encode(digest, sizeof(digest));
}

String service_auth_request_signature(
    const String& method,
    const String& path_and_query,
    const String& timestamp,
    const String& nonce,
    const String& body,
    const String& secret) {
    const String material = method + "\n" + path_and_query + "\n" + timestamp + "\n" +
        nonce + "\n" + service_auth_sha256_hex(body);
    return service_auth_hmac_sha256_base64url(secret, material);
}

const char* service_auth_portal_root_ca() {
    return kIsrgRootX1;
}

} // namespace ptc
