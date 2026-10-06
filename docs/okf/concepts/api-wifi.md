---
type: api
title: "WiFi (pocketmage_wifi)"
description: "The station radio service: enabled/scan/connect lifecycle, status queries, and saved credentials."
source: "https://talismandesign.github.io/PocketMage_SDK/docs/api/wifi/"
path: /api/wifi/
updated: 2026-10-06
okf:
  generated_by: "@docmd/plugin-okf"
  generated_at: "2026-10-06T07:52:17.225Z"
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
