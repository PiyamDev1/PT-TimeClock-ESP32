#pragma once

// NOTE: Do not place real secrets in source control.

namespace secrets {

#define PTC_STRINGIFY_IMPL(x) #x
#define PTC_STRINGIFY(x) PTC_STRINGIFY_IMPL(x)

#ifndef PTC_API_BASE_URL_STRING
#define PTC_API_BASE_URL_STRING "https://ims.piyamtravel.com"
#endif

#ifndef PTC_DEVICE_ID
#define PTC_DEVICE_ID
#endif

#ifndef PTC_DEVICE_SECRET
#define PTC_DEVICE_SECRET
#endif

#ifndef PTC_DEVICE_LOCATION
#define PTC_DEVICE_LOCATION
#endif

#ifndef PTC_DEFAULT_WIFI_SSID
#define PTC_DEFAULT_WIFI_SSID
#endif

#ifndef PTC_DEFAULT_WIFI_PASSWORD
#define PTC_DEFAULT_WIFI_PASSWORD
#endif

static constexpr const char* kApiBaseUrl = PTC_API_BASE_URL_STRING;
static constexpr const char* kDefaultDeviceId = PTC_STRINGIFY(PTC_DEVICE_ID);
static constexpr const char* kDefaultDeviceSecret = PTC_STRINGIFY(PTC_DEVICE_SECRET);
static constexpr const char* kDefaultDeviceLocation = PTC_STRINGIFY(PTC_DEVICE_LOCATION);
static constexpr const char* kDefaultWifiSsid = PTC_STRINGIFY(PTC_DEFAULT_WIFI_SSID);
static constexpr const char* kDefaultWifiPassword = PTC_STRINGIFY(PTC_DEFAULT_WIFI_PASSWORD);
static constexpr const char* kGithubOwner = "OWNER";
static constexpr const char* kGithubRepo = "REPO";

#ifndef PTC_GITHUB_TOKEN
#define PTC_GITHUB_TOKEN ""
#endif

static constexpr const char* kGithubToken = PTC_STRINGIFY(PTC_GITHUB_TOKEN);

} // namespace secrets
