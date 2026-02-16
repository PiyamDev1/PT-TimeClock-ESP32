# PT Timeclock (ESP32-S3)

Portrait LVGL timeclock with swipeable tabs, provisioning flow, signed QR codes, and OTA updates.

## Quick start

1) Update pins in include/pins.h for your panel and touch controller (ESP32-8048S050C is prefilled).
2) Set API base URL in include/secrets.h.
3) Build and flash using PlatformIO.

## OTA updates

OTA is manual-only. Tap "Enable OTA" in Settings to start the OTA listener. The hostname is set to `ptc-<device_id>`.

GitHub OTA (manual): tap "Check GitHub update" then "Download update" and finally "Reboot to apply". The device looks for a `firmware.bin` asset on the latest GitHub release.

Configure these in [include/secrets.h](include/secrets.h):

- `kGithubOwner`
- `kGithubRepo`
- `kGithubToken`

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
