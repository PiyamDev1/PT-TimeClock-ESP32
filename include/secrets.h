#pragma once

// NOTE: Do not place real secrets in source control.

namespace secrets {

static constexpr const char* kApiBaseUrl = "https://example.com";
static constexpr const char* kGithubOwner = "OWNER";
static constexpr const char* kGithubRepo = "REPO";

#ifndef PTC_GITHUB_TOKEN
#define PTC_GITHUB_TOKEN ""
#endif

#define PTC_STRINGIFY_IMPL(x) #x
#define PTC_STRINGIFY(x) PTC_STRINGIFY_IMPL(x)

static constexpr const char* kGithubToken = PTC_STRINGIFY(PTC_GITHUB_TOKEN);

} // namespace secrets
