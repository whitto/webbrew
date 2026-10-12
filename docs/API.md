# Local HTTP API

All endpoints require HTTP Basic authentication: username `brew`, device-generated or user-configured password. GET `/api/status` returns a per-boot `csrf` token. Send it as `X-WebBrew-Token` on every POST, with `Content-Type: application/json`. No cross-origin access is enabled. Do not expose this plain HTTP API outside a trusted local network.

| Endpoint | Method | Purpose |
|---|---|---|
| `/` | GET | Embedded dashboard |
| `/api/status` | GET | Current state, probe reading, target, output, timing, gains, network status, token and saved configuration |
| `/api/config` | POST | Replace validated kettle configuration and stage schedule; only while stopped and fault-free |
| `/api/command` | POST | Start, stop, pause, resume, reset fault or select mode |
| `/api/network` | POST | Save local Wi-Fi SSID/password; only while stopped and fault-free |
| `/api/password` | POST | Change dashboard password; only while stopped and fault-free |

Config payload example (the complete object is required):

```json
{
  "volumeL": 25,
  "powerW": 3000,
  "boilC": 100,
  "maxC": 105,
  "maxRunSeconds": 14400,
  "ssrPin": 25,
  "probePin": 4,
  "activeHigh": true,
  "fahrenheit": false,
  "learning": true,
  "stages": [
    {"name":"Warm-up & hold","targetC":78,"holdSeconds":600,"boil":false,"boilPower":70},
    {"name":"Boil","targetC":100,"holdSeconds":3600,"boil":true,"boilPower":70}
  ]
}
```

`activeHigh: false` selects active-low output with internal pull-up. All temperatures in the API are Celsius regardless of display unit. GPIO assignments must be distinct and match the classic ESP32 allowlist. Stage names are at most 48 UTF-8 bytes. Empty stages, nonfinite numbers, invalid ranges, incompatible boil targets and session limits shorter than the hold-time total are rejected. POST bodies are limited to 8 KiB after the embedded server receives them.

Commands:

```json
{"action":"start"}
{"action":"stop"}
{"action":"pause"}
{"action":"resume"}
{"action":"mode","manual":true,"power":40}
{"action":"mode","manual":false,"power":0}
{"action":"reset"}
```

Start requires a fresh valid probe, valid configuration and no latched fault. Manual is available only within an active session and starts at zero when selected by the UI. It freezes stage advancement/hold accumulation. A paused session never heats. Stop does not clear a fault. Fault reset requires a fresh probe at least 2 °C below the temperature cutoff; reset does not start heating.

Responses use appropriate HTTP status codes and a JSON `message` field, except `/api/status`, which returns the status object. Missing probe readings are `null`, not a fabricated temperature. Settings and API responses never return Wi-Fi passwords or the dashboard password.

