Import("env")

from pathlib import Path


def parse_local_env(path):
    values = {}
    if not path.exists():
        return values

    for raw_line in path.read_text(encoding="utf-8").splitlines():
        line = raw_line.strip()
        if not line or line.startswith("#"):
            continue
        if "=" not in line:
            continue

        key, value = line.split("=", 1)
        key = key.strip()
        value = value.strip()

        if len(value) >= 2 and value[0] == value[-1] and value[0] in ('"', "'"):
            value = value[1:-1]

        values[key] = value

    return values


project_dir = Path(env["PROJECT_DIR"])
local_env = project_dir / "local.env"
values = parse_local_env(local_env)

github_token = values.get("PTC_GITHUB_TOKEN", "")
if github_token:
    env.Append(CPPDEFINES=[("PTC_GITHUB_TOKEN", github_token)])

api_base_url = values.get("PTC_API_BASE_URL", "")
if api_base_url:
    env.Append(CPPDEFINES=[("PTC_API_BASE_URL", api_base_url)])

device_id = values.get("PTC_DEVICE_ID", "")
device_secret = values.get("PTC_DEVICE_SECRET", "")
device_location = values.get("PTC_DEVICE_LOCATION", "")
if device_id:
    env.Append(CPPDEFINES=[("PTC_DEVICE_ID", device_id)])
if device_secret:
    env.Append(CPPDEFINES=[("PTC_DEVICE_SECRET", device_secret)])
if device_location:
    env.Append(CPPDEFINES=[("PTC_DEVICE_LOCATION", device_location)])

default_wifi_ssid = values.get("PTC_DEFAULT_WIFI_SSID", "")
default_wifi_password = values.get("PTC_DEFAULT_WIFI_PASSWORD", "")
if default_wifi_ssid:
    env.Append(CPPDEFINES=[("PTC_DEFAULT_WIFI_SSID", default_wifi_ssid)])
if default_wifi_password:
    env.Append(CPPDEFINES=[("PTC_DEFAULT_WIFI_PASSWORD", default_wifi_password)])
