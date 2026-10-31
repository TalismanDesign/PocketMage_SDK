---
type: api
title: "WiFi (pocketmage_wifi)"
description: "The station radio service: enabled/scan/connect lifecycle, status queries, and saved credentials."
source: "https://talismandesign.github.io/PocketMage_SDK/docs/api/wifi/"
path: /api/wifi/
updated: 2026-10-31
okf:
  generated_by: "@docmd/plugin-okf"
  generated_at: "2026-10-31T22:45:59.833Z"
---
---
title: "WiFi (pocketmage_wifi)"
description: "The station radio service: enabled/scan/connect lifecycle, status queries, and saved credentials."
---

# WiFi API

`PocketMageWifi` is the ESP32-S3 station radio as a non-blocking service. All control calls dispatch to a dedicated task; status reads are thread-safe. On the S3 the radio stalls below 240 MHz, so the service holds the CPU at `WIFI_CPU_FREQ_MHZ` (240) whenever the radio is not `Off`.

## Access

```cpp
#include <pocketmage.h>

PocketMageWifi& wifi = P_WIFI;
```

`P_WIFI` is a global reference to the singleton (`PocketMageWifi::getInstance()`). The host calls `P_WIFI.begin()` during `PocketMage_INIT()`; the radio stays off until something calls `enable()`.

## State model

```cpp
enum class WifiRadioState {
  Off, TurningOn, On, Scanning, Connecting, Connected, TurningOff
};
```

## Class reference

### Lifecycle

```cpp
void begin();   // one-time service init (event loop, netif, task)
void stop();    // clean service shutdown
```

### Control (non-blocking, dispatched to the task)

```cpp
void enable();                          // turn the radio on
void disable();                         // turn the radio off
void scan();                            // start a scan
void connect(const char* ssid, const char* password, bool save = true);
void disconnect();
void reconnect();                       // retry with saved creds
```

`connect(..., save)` persists the credentials under the `pmwifi` NVS namespace when `save` is true. `reconnect()` and auto-connect use the saved networks on boot.

### Status (thread-safe)

```cpp
WifiRadioState getState() const;
bool isConnected() const;
bool isScanning() const;
String getStatusMessage() const;
String getConnectedSSID() const;
String getIpAddress() const;
int getRssi() const;
String getLastError() const;
```

`getStatusMessage()` is a human-readable line for status displays. `getLastError()` carries the connection-failure reason text.

### Scan results

```cpp
uint16_t getScanResultCount() const;
bool getScanResult(uint16_t index, WifiApInfo& out) const;
```

`WifiApInfo` is a simplified AP record (`ssid`, `rssi`, `channel`, `authmode`). Results are capped at `MAX_SCAN_RESULTS` (20).

### Saved credentials

```cpp
bool hasSavedCredentials(const char* ssid) const;
bool loadSavedCredentials(const char* ssid, char* password, size_t maxLen) const;
void clearSavedCredentials(const char* ssid);
```

### Events

```cpp
typedef std::function<void(void)> WifiEventCallback;
void setEventCallback(WifiEventCallback cb);
void dispatchEvents();
```

Set a callback with `setEventCallback` to be notified of state changes (the WiFi command screen uses it to trigger a redraw); call `dispatchEvents()` from the app loop to process pending notifications. The service handles backoff automatically (up to `MAX_RETRIES` with `RETRY_BASE_DELAY_MS` growing to `RETRY_MAX_DELAY_MS`).

## Threading

Control methods only enqueue a command and return. Status getters lock the service mutex. The service task holds the CPU at 240 MHz for the whole session; `pocketmage::setCpuSpeed` refuses any drop while the radio is not `Off` (see [sys](sys.md)).

## Example

```cpp
#include <pocketmage.h>

void connectHome() {
  P_WIFI.enable();
  delay(200);
  P_WIFI.connect("home-ssid", "hunter2");
}

void drawWifiStatus() {
  // draw a status line from the thread-safe getters
}
```

## C ABI (`pm_*`)

External ELF apps call these C wrappers rather than the C++ class above; they are generated from it by `gen_app_api.py` and declared in `pm_sdk_app.h` (pulled in via `pm_app_api.h`). See the [function index](functions.md) and the [projection model](index.md#api-reference-the-c-abi-pm) for the general rules.

```c
void pm_wifi_begin();
void pm_wifi_stop();
void pm_wifi_enable();
void pm_wifi_disable();
void pm_wifi_scan();
void pm_wifi_connect(const char* a0, const char* a1, bool a2);
void pm_wifi_disconnect();
void pm_wifi_reconnect();
int pm_wifi_get_state();
bool pm_wifi_is_connected();
bool pm_wifi_is_scanning();
const char* pm_wifi_get_status_message();
const char* pm_wifi_get_connected_ssid();
const char* pm_wifi_get_ip_address();
int pm_wifi_get_rssi();
const char* pm_wifi_get_last_error();
uint16_t pm_wifi_get_scan_result_count();
bool pm_wifi_has_saved_credentials(const char* a0);
bool pm_wifi_load_saved_credentials(const char* a0, char* a1, size_t a2);
void pm_wifi_clear_saved_credentials(const char* a0);
void pm_wifi_dispatch_events();
```

All operate on the `P_WIFI` singleton. Every `const char*` return points into the rotating string pool; copy it before the next SDK call if it must survive four further string-returning calls.

- `pm_wifi_begin()` / `pm_wifi_stop()` are `begin()` / `stop()`.
- `pm_wifi_enable()` / `pm_wifi_disable()` are `enable()` / `disable()`.
- `pm_wifi_scan()` is `scan()`.
- `pm_wifi_connect(a0, a1, a2)` is `connect(ssid, password, save)`; `a0` is the SSID, `a1` is the password, `a2` persists the credentials when true.
- `pm_wifi_disconnect()` / `pm_wifi_reconnect()` are `disconnect()` / `reconnect()`.
- `pm_wifi_get_state()` is `getState()`. Returns the `WifiRadioState` value as int.
- `pm_wifi_is_connected()` / `pm_wifi_is_scanning()` are `isConnected()` / `isScanning()`.
- `pm_wifi_get_status_message()` is `getStatusMessage()`; a human-readable status line.
- `pm_wifi_get_connected_ssid()` is `getConnectedSSID()`.
- `pm_wifi_get_ip_address()` is `getIpAddress()`.
- `pm_wifi_get_rssi()` is `getRssi()`.
- `pm_wifi_get_last_error()` is `getLastError()`; the connection-failure reason text.
- `pm_wifi_get_scan_result_count()` is `getScanResultCount()`. Returns the AP count, capped at `MAX_SCAN_RESULTS`. The per-result `getScanResult()` is not projected to the C ABI.
- `pm_wifi_has_saved_credentials(a0)` is `hasSavedCredentials(ssid)`.
- `pm_wifi_load_saved_credentials(a0, a1, a2)` is `loadSavedCredentials(ssid, password, maxLen)`; `a0` is the SSID, `a1` is the caller buffer the password is written into, `a2` is its size. Returns true when a credential was found and copied.
- `pm_wifi_clear_saved_credentials(a0)` is `clearSavedCredentials(ssid)`.
- `pm_wifi_dispatch_events()` is `dispatchEvents()`; call it from the app loop to process pending notifications. The `setEventCallback()` method is not projected to the C ABI.
