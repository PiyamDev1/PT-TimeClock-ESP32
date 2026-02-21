# PT Timeclock (ESP32-S3)

Portrait LVGL timeclock with swipeable tabs, provisioning flow, signed QR codes, and OTA updates.

## Quick start

0) Bootstrap local tooling and dependencies:
	- `./scripts/setup_environment.sh`
1) Update pins in include/pins.h for your panel and touch controller (ESP32-8048S050C is prefilled).
2) Set API base URL in include/secrets.h.
3) Build and flash using PlatformIO.

If `pio` is not in your shell path, use `~/.local/bin/pio`.

Repository size notes:

- Heavy/generated assets are intentionally not tracked (`vendor/`, `release-artifacts/`, local build folders).
- PlatformIO will download libraries into local cache/workspace during setup/build.

USB flashing note:

- GitHub Codespaces cannot access your local USB serial device directly.
- Do the first flash from a local machine (local VS Code/devcontainer or native terminal) where the ESP32 is physically connected.
- After first flash, use OTA update flow from this project.

## Screensaver

- Display backlight dims after 60 seconds of no touch input.
- If still inactive while dimmed, display backlight turns off at 120 seconds.
- A tap wakes the display.
- The wake tap is consumed, so it won’t trigger the underlying button/tile press.

## OTA updates

OTA is manual-only. Tap "Enable OTA" in Settings to start the OTA listener. The hostname is set to `ptc-<device_id>`.

GitHub OTA: tap "Install latest GitHub update". The device fetches the latest release, downloads `firmware.bin`, and stages it for boot. Reboot is deferred until you tap "Reboot to finish update".

Configure these in [include/secrets.h](include/secrets.h):

- `kGithubOwner`
- `kGithubRepo`
- `kGithubToken` (provided via build env var for private repos)

Private repo token setup (recommended):

- Copy template: `cp local.env.example local.env`
- Set token in `local.env`: `PTC_GITHUB_TOKEN=ghp_your_token_here`
- Build: `pio run`

Notes:

- The token is injected at compile time from `local.env` via `scripts/load_local_env.py`.
- `local.env` is git-ignored; never commit real tokens.

Token sanity check:

- `set -a && . ./local.env && set +a && curl -sS -o /tmp/gh_repo.json -w "%{http_code}\n" -H "Authorization: Bearer $PTC_GITHUB_TOKEN" -H "Accept: application/vnd.github+json" https://api.github.com/repos/PiyamDev1/PT-TimeClock-ESP32`
- `set -a && . ./local.env && set +a && curl -sS -o /tmp/gh_rel.json -w "%{http_code}\n" -H "Authorization: Bearer $PTC_GITHUB_TOKEN" -H "Accept: application/vnd.github+json" https://api.github.com/repos/PiyamDev1/PT-TimeClock-ESP32/releases/latest`

Expected:

- Repo endpoint returns `200`.
- Latest release endpoint returns `200` when a release exists (or `404` if no release has been published yet).

GitHub release checklist (required for OTA):

1) Prepare artifact: `./scripts/prepare_release.sh --tag v0.1.1`
2) Create a GitHub Release with tag `v0.1.1`
3) Upload `release-artifacts/v0.1.1/firmware.bin` (name must stay exactly `firmware.bin`)
4) Publish the release
5) On device, tap `Install latest GitHub update`
6) After status shows installed, tap `Reboot to finish update`

Notes:

- OTA lookup uses `releases/latest`, so only the latest published release is used.
- Asset name must be exactly `firmware.bin`.
- Use `./scripts/prepare_release.sh --skip-build --tag v0.1.1` if you already built firmware.

One-command GitHub publish (optional):

- Authenticate GitHub CLI once: `gh auth login`
- Publish release + upload `firmware.bin`: `./scripts/publish_release.sh --tag v0.1.1`

Useful options:

- `./scripts/publish_release.sh --tag v0.1.1 --notes "Bug fixes"`
- `./scripts/publish_release.sh --tag v0.1.1 --skip-build`
- `./scripts/publish_release.sh --tag v0.1.1 --repo owner/repo`

## Easy first flash (Windows, COM5)

Generate a single flash image:

- `./scripts/make_fullflash.sh --tag firstflash`

This creates:

- `release-artifacts/firstflash/fullflash.bin`

Option A (easiest command line):

1) Open Windows terminal in your local project clone
2) Run: `python -m esptool --chip esp32s3 --port COM5 --baud 460800 write_flash 0x0 release-artifacts/firstflash/fullflash.bin`
3) Press reset on the board

Option B (Espressif Flash Download Tool GUI):

1) Open Flash Download Tool and select `ESP32-S3`
2) Add file: `release-artifacts/firstflash/fullflash.bin`
3) Set address to `0x0`
4) Select port `COM5`
5) Click Flash and wait for success
6) Press reset on the board

Notes:

- Use `./scripts/make_fullflash.sh --skip-build --tag firstflash` if already built.
- After first USB flash succeeds, future updates can use OTA (`firmware.bin` release asset).

Example:

- Build: `pio run`
- Upload OTA: `pio run -t upload --upload-port ptc-ESP32S3-XXXX`

If the OTA hostname is not discoverable, use the device IP and add it to the upload command:

- `pio run -t upload --upload-port 192.168.1.50`

## Structure

- src/main.cpp: boot, LVGL init, service tick loop
- src/ui: UI tabs and screen root
- src/services: Wi-Fi, time, QR, storage, log, HTTP, OTA
- src/drivers: RGB panel + GT911 touch
- include: config, pins, secrets

## Notes

- The display and touch drivers are wired for the ESP32-8048S050C (yellow board). Adjust timings in src/drivers/display_driver.cpp if you see tearing.
- OTA requires Wi-Fi to be connected. Check the Settings tab for OTA status.
