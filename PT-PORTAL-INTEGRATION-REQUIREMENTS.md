# PT Portal requirements for the ESP32 timeclock

This document describes the portal work needed for a complete and secure
physical timeclock integration. The existing QR scan route is the correct
starting point. The ESP32 generates a short-lived signed QR code; an
authenticated employee scans that code in PT Portal.

## 1. Fix the current database exposure first

`public.timeclock_devices` currently has row-level security disabled while
`anon` and `authenticated` have full table privileges. This exposes every
device HMAC secret and allows direct inserts, updates, deletes, and truncation
through Supabase.

The same review must be applied to `public.timeclock_manual_codes`.

Required actions:

1. Revoke all direct client-role access from both tables.
2. Enable RLS on both tables with no client policies unless a specific,
   reviewed use case requires one.
3. Keep all reads and writes behind server routes using the Supabase service
   role.
4. Remove the production diagnostics route, or require an authenticated
   maintenance/admin session. It currently performs database writes.
5. Rotate all existing device secrets after access has been closed. Treat the
   current secrets as compromised because they were readable by the anonymous
   role.

Suggested migration:

```sql
revoke all on table public.timeclock_devices from anon, authenticated;
revoke all on table public.timeclock_manual_codes from anon, authenticated;

alter table public.timeclock_devices enable row level security;
alter table public.timeclock_manual_codes enable row level security;
```

The existing timeclock server routes use the service-role client, which
bypasses RLS.

## 2. Preserve the QR scan contract

The ESP32 emits:

```text
ptc1:<base64url(JSON)>
```

Decoded JSON:

```json
{
  "v": 1,
  "device_id": "<timeclock_devices UUID>",
  "ts": 1784419200,
  "nonce": "<random value>",
  "sig": "<base64url HMAC-SHA256>"
}
```

The signature material must remain:

```text
<device_id>.<unix timestamp seconds>.<nonce>
```

The signing key is the matching `timeclock_devices.secret` value. Keep the
existing two-minute timestamp window, constant-time signature comparison, and
duplicate-scan protection in `POST /api/timeclock/scan`.

## 3. Add physical device management

Add an admin-only PT Portal page for physical timeclocks. It should support:

- List devices without returning the `secret` column.
- Create a physical device with a generated UUID and 32-byte random secret.
- Assign a portal location/branch.
- Activate or deactivate a device.
- Rotate a secret with an explicit confirmation.
- Show `last_seen_at`, firmware version, IP address, and online/offline state.
- Reveal a newly generated secret only once so it can be provisioned into the
  firmware. Never return it from normal list or detail responses.

The current physical record is:

- Name: `Luton Office ESP32 Timeclock`
- Location: `Luton Office - Dunstable Road`
- Device ID: `cb9008f8-0098-4b46-b77b-b82029aff3f2`

Do not put a device secret in source control, logs, browser storage, or API
responses after initial provisioning.

## 4. Authenticate device-to-portal requests

Device service routes must not trust a device UUID by itself. Use these
headers:

```text
X-PTC-Device-Id: <UUID>
X-PTC-Timestamp: <unix seconds>
X-PTC-Nonce: <random base64url value>
X-PTC-Signature: <base64url HMAC-SHA256>
```

Canonical signature material:

```text
<HTTP method>\n<pathname and query>\n<timestamp>\n<nonce>\n<SHA-256 hex of request body>
```

Validation requirements:

- Load the active device by UUID using the service-role client.
- Reject timestamps more than 120 seconds from server time.
- Compare signatures in constant time.
- Reject a reused nonce within the acceptance window.
- Return `401` for missing/invalid authentication and `403` for inactive
  devices.
- Never include the device secret in a response.

Implement this once as a shared server-only helper and use it on every route
below.

## 5. Add the device service routes

### GET /api/timeclock/devices/config

Query:

```text
?device_id=<UUID>
```

Response:

```json
{
  "device_id": "<UUID>",
  "location_id": "<location UUID or null>",
  "location_name": "Luton Office - Dunstable Road",
  "qr_interval_sec": 20,
  "is_active": true
}
```

The route must use the signed device headers. Add `location_id` and
`qr_interval_sec` to the device schema if they are not stored elsewhere.

### POST /api/timeclock/devices/heartbeat

Request:

```json
{
  "device_id": "<UUID>",
  "firmware_version": "0.1.0",
  "ip": "192.0.2.10",
  "wifi_rssi": -55,
  "free_heap": 176000,
  "uptime_sec": 3600
}
```

Store operational status in a separate `timeclock_device_status` table keyed
by `device_id`, or in dedicated columns on `timeclock_devices`. Upsert the
status and set `last_seen_at` from server time, not from a client timestamp.
Return:

```json
{ "ok": true, "server_time": 1784419200 }
```

### GET /api/timeclock/notices

Query:

```text
?device_id=<UUID>&since=<optional unix timestamp>
```

Return a JSON array:

```json
[
  {
    "id": "<notice UUID>",
    "title": "Office notice",
    "body": "Notice text",
    "created_at": "2026-07-19T00:00:00Z"
  }
]
```

Only return active notices visible to the device's assigned location. An empty
array is valid.

### POST /api/timeclock/devices/manual-code

The physical display cannot generate a valid manual code locally. The current
manual-entry submit route accepts only an 8-digit code already stored in
`timeclock_manual_codes`, so add a device-authenticated route that creates that
record.

Request:

```json
{
  "device_id": "<UUID>",
  "qr_payload": "ptc1:<current signed payload>"
}
```

Response:

```json
{
  "code": "12345678",
  "code_display": "1234-5678",
  "expires_at": "2026-07-21T12:00:30Z"
}
```

Requirements:

- Authenticate the request using the signed device headers.
- Generate a cryptographically random 8-digit code and retry on a uniqueness
  collision.
- Store the physical `device_id`, current QR payload, and a 30-second expiry.
- Allow `timeclock_manual_codes.user_id` to be null for a physical-device code;
  the employee is the authenticated user who later submits the code.
- Replace or expire any previous unused code for the same physical device.
- Return the secret in neither the response nor logs.
- Apply a per-device rate limit aligned with `qr_interval_sec`.

## 6. Registration policy

Do not add an unauthenticated endpoint that creates active devices and returns
secrets. Physical devices should be created by an administrator and provisioned
with the one-time secret.

If remote enrollment is needed later, use a short-lived, single-use enrollment
code generated by an administrator. The code must expire, be rate limited, and
be exchanged exactly once for the device UUID and secret.

## 7. OTA ownership

PT Portal does not need to serve firmware updates. The device supports:

- Arduino OTA on the local Wi-Fi network, enabled automatically after boot.
- GitHub release OTA using the latest release's `firmware.bin` asset.

The firmware repository/release process owns OTA. PT Portal only needs to show
the firmware version reported by heartbeat.

## Acceptance checks

- Anonymous Supabase REST requests cannot read or mutate either timeclock
  secret/code table.
- The diagnostics route is unavailable to an unauthenticated caller.
- A valid ESP32 QR scan creates one punch for the authenticated employee.
- An invalid signature, inactive device, expired timestamp, reused nonce, or
  duplicate scan is rejected.
- Signed config, heartbeat, and notices requests succeed for the physical
  device.
- A signed physical-device manual-code request returns an 8-digit code that can
  be submitted once by an authenticated employee before it expires.
- The admin device list never contains `secret`.
- Secret rotation invalidates old QR signatures, and the replacement secret is
  shown only once.
- Heartbeats update the physical device's online state and firmware version.

## Completion handoff to the firmware project

When the PT Portal work is complete, provide the following back to this project:

1. The pull request or commit containing the portal changes and database
   migration.
2. Confirmation that the migration has been applied to production and that an
   anonymous Supabase request cannot access either protected table.
3. The production API base URL and confirmation that the three device service
   routes above are deployed at that URL.
4. A newly rotated secret for device
   `cb9008f8-0098-4b46-b77b-b82029aff3f2`. Put this directly in this firmware
   project's ignored `local.env`; do not send it in chat, commit it, or add it
   to the portal repository.
5. One successful server-side test result for config, heartbeat, and notices,
   plus one rejected request for an invalid signature and one for a replayed
   nonce. Redact signatures, nonces, and secrets from logs.
6. The final `location_id` assigned to the device and the intended QR refresh
   interval.

After that handoff, the firmware can enable signed config, heartbeat, and notice
polling against PT Portal. Until those routes exist, the device deliberately
treats the configured PT Portal base URL as configured without repeatedly
calling missing endpoints.
