#include "usb_keyboard.h"

#include <pocketmage_sys/usb_role.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include <usb/usb_host.h>

#include <atomic>
#include <cstdlib>

namespace pm_keyboard {
namespace {
constexpr const char *TAG = "USBKeyboard";
constexpr uint32_t kShutdownTimeoutMs = 5000;
constexpr uint32_t kEventWaitMs = 50;
constexpr uint32_t kDrainWaitMs = 10;
constexpr size_t kEventQueueLength = 10;
constexpr size_t kTaskStackBytes = 4096;
constexpr size_t kReportBytes = 64;
constexpr unsigned kHostPriority = 2;
constexpr unsigned kDriverPriority = 5;

QueueHandle_t events = nullptr;
SemaphoreHandle_t connectionsDone = nullptr, libraryDone = nullptr;
TaskHandle_t connectionsTask = nullptr, libraryTask = nullptr;
std::atomic<bool> stopRequested{false}, stopLibrary{false};
std::atomic<size_t> openInterfaces{0};
bool libraryInstalled = false, driverInstalled = false, rootPowered = false;

void reportError(const char *operation, esp_err_t error) {
  ESP_LOGE(TAG, "%s: %s", operation, esp_err_to_name(error));
}

void acknowledge(SemaphoreHandle_t done) {
  if (xSemaphoreGive(done) != pdTRUE)
    std::abort();
  // No worker may touch session resources after publishing its acknowledgement.
  vTaskDelete(nullptr);
}

void libraryLoop(void *) {
  while (!stopLibrary.load()) {
    uint32_t flags = 0;
    const esp_err_t error = usb_host_lib_handle_events(pdMS_TO_TICKS(kEventWaitMs), &flags);
    if (error != ESP_OK && error != ESP_ERR_TIMEOUT)
      reportError("USB event processing", error);
    if (flags & USB_HOST_LIB_EVENT_FLAGS_NO_CLIENTS) {
      const esp_err_t freed = usb_host_device_free_all();
      if (freed != ESP_OK && freed != ESP_ERR_NOT_FINISHED)
        reportError("USB device release", freed);
    }
  }
  acknowledge(libraryDone);
}

void interfaceEvent(hid_host_device_handle_t device, hid_host_interface_event_t event, void *) {
  if (event == HID_HOST_INTERFACE_EVENT_DISCONNECTED) {
    const esp_err_t error = hid_host_device_close(device);
    if (error == ESP_OK)
      --openInterfaces;
    else
      reportError("HID disconnect", error);
    return;
  }
  if (event != HID_HOST_INTERFACE_EVENT_INPUT_REPORT || stopRequested.load())
    return;
  hid_host_dev_params_t params{};
  uint8_t data[kReportBytes];
  size_t length = 0;
  esp_err_t error = hid_host_device_get_params(device, &params);
  if (error == ESP_OK)
    error = hid_host_device_get_raw_input_report_data(device, data, sizeof(data), &length);
  if (error != ESP_OK) {
    reportError("HID input report", error);
    return;
  }
  if (length <= sizeof(data))
    handleReport(params, data, length);
}

void openDevice(hid_host_device_handle_t device) {
  hid_host_dev_params_t params{};
  esp_err_t error = hid_host_device_get_params(device, &params);
  if (error != ESP_OK) {
    reportError("HID device parameters", error);
    return;
  }
  const hid_host_device_config_t config = { .callback = interfaceEvent, .callback_arg = nullptr };
  ++openInterfaces;
  error = hid_host_device_open(device, &config);
  if (error != ESP_OK) {
    --openInterfaces;
    reportError("HID device open", error);
    return;
  }
  if (params.sub_class == HID_SUBCLASS_BOOT_INTERFACE) {
    error = hid_class_request_set_protocol(device, HID_REPORT_PROTOCOL_BOOT);
    if (error == ESP_OK && params.proto == HID_PROTOCOL_KEYBOARD)
      error = hid_class_request_set_idle(device, 0, 0);
  }
  if (error == ESP_OK && !stopRequested.load())
    error = hid_host_device_start(device);
  if (error != ESP_OK)
    reportError("HID device start", error);
  // The disconnect callback releases opened interfaces, including failed starts.
}

void connectionLoop(void *) {
  while (!stopRequested.load()) {
    hid_host_device_handle_t device;
    if (xQueueReceive(events, &device, pdMS_TO_TICKS(kEventWaitMs)) == pdTRUE &&
        !stopRequested.load())
      openDevice(device);
  }
  acknowledge(connectionsDone);
}

void deviceEvent(hid_host_device_handle_t device, hid_host_driver_event_t event, void *) {
  if (event == HID_HOST_DRIVER_EVENT_CONNECTED && !stopRequested.load() &&
      xQueueSend(events, &device, 0) != pdTRUE)
    ESP_LOGE(TAG, "HID connection queue full");
}

bool join(TaskHandle_t &task, SemaphoreHandle_t done) {
  if (!task)
    return true;
  if (xSemaphoreTake(done, pdMS_TO_TICKS(kShutdownTimeoutMs)) != pdTRUE)
    return false;
  task = nullptr;
  return true;
}

bool devicesGone() {
  const TickType_t start = xTaskGetTickCount();
  do {
    usb_host_lib_info_t info{};
    const esp_err_t error = usb_host_lib_info(&info);
    if (error != ESP_OK) {
      reportError("USB device drain", error);
      return false;
    }
    if (info.num_devices == 0 && openInterfaces.load() == 0)
      return true;
    vTaskDelay(pdMS_TO_TICKS(kDrainWaitMs));
  } while (static_cast<TickType_t>(xTaskGetTickCount() - start) <
           pdMS_TO_TICKS(kShutdownTimeoutMs));
  return false;
}

void freeSynchronization() {
  if (events) vQueueDelete(events);
  if (connectionsDone) vSemaphoreDelete(connectionsDone);
  if (libraryDone) vSemaphoreDelete(libraryDone);
  events = nullptr;
  connectionsDone = nullptr;
  libraryDone = nullptr;
}

bool startFailed(const char *operation, esp_err_t error) {
  reportError(operation, error);
  if (!stopUsb())
    ESP_LOGE(TAG, "HID startup cleanup incomplete; retaining connector");
  return false;
}
} // namespace

bool stopping() {
  return stopRequested.load();
}

bool beginUsb() {
  pm_usb::Lock lock;
  if (pm_usb::owner() == pm_usb::Owner::Keyboard)
    return driverInstalled && rootPowered && !stopRequested.load();
  if (!prepareInput() || !pm_usb::claim(pm_usb::Owner::Keyboard))
    return false;
  stopRequested = false;
  stopLibrary = false;
  resetInput();
  events = xQueueCreate(kEventQueueLength, sizeof(hid_host_device_handle_t));
  connectionsDone = xSemaphoreCreateBinary();
  libraryDone = xSemaphoreCreateBinary();
  if (!events || !connectionsDone || !libraryDone)
    return startFailed("HID synchronization allocation", ESP_ERR_NO_MEM);

  usb_host_config_t host{};
  host.intr_flags = ESP_INTR_FLAG_LEVEL1;
  // Publish queues and workers before allowing enumeration callbacks.
  host.root_port_unpowered = true;
  esp_err_t error = usb_host_install(&host);
  if (error != ESP_OK)
    return startFailed("USB host install", error);
  libraryInstalled = true;
  if (xTaskCreatePinnedToCore(libraryLoop, "usb_events", kTaskStackBytes, nullptr,
                              kHostPriority, &libraryTask, 1) != pdPASS)
    return startFailed("USB event task", ESP_ERR_NO_MEM);
  if (xTaskCreate(connectionLoop, "hid_connections", kTaskStackBytes, nullptr,
                   kHostPriority, &connectionsTask) != pdPASS)
    return startFailed("HID connection task", ESP_ERR_NO_MEM);

  hid_host_driver_config_t driver{};
  driver.create_background_task = true;
  driver.task_priority = kDriverPriority;
  driver.stack_size = kTaskStackBytes;
  driver.core_id = 0;
  driver.callback = deviceEvent;
  error = hid_host_install(&driver);
  if (error != ESP_OK)
    return startFailed("HID driver install", error);
  driverInstalled = true;
  error = usb_host_lib_set_root_port_power(true);
  if (error != ESP_OK)
    return startFailed("USB root port enable", error);
  rootPowered = true;
  return true;
}

bool stopUsb() {
  pm_usb::Lock lock;
  if (pm_usb::owner() != pm_usb::Owner::Keyboard)
    return true;
  stopRequested = true;
  // Finish any control request before a forced disconnect releases its device.
  if (!join(connectionsTask, connectionsDone))
    return false;
  if (rootPowered) {
    const esp_err_t error = usb_host_lib_set_root_port_power(false);
    if (error != ESP_OK) {
      reportError("USB root port disable", error);
      return false;
    }
    rootPowered = false;
  }
  // The HID driver must process DEV_GONE before uninstall. Keep its event loop
  // and queue alive until every interface has closed and the client deregisters.
  if (driverInstalled && !devicesGone())
    return false;
  if (driverInstalled) {
    const esp_err_t error = hid_host_uninstall();
    if (error != ESP_OK) {
      reportError("HID driver uninstall", error);
      return false;
    }
    driverInstalled = false;
  }
  stopLibrary = true;
  if (!join(libraryTask, libraryDone))
    return false;
  if (libraryInstalled) {
    // Only the control task handles events here; the library worker has joined.
    // A failed uninstall retains this phase for the next stop attempt.
    uint32_t flags = 0;
    esp_err_t error = usb_host_lib_handle_events(0, &flags);
    if (error != ESP_OK && error != ESP_ERR_TIMEOUT) {
      reportError("USB final event drain", error);
      return false;
    }
    error = usb_host_device_free_all();
    if (error != ESP_OK) {
      reportError("USB final device release", error);
      return false;
    }
    error = usb_host_uninstall();
    if (error != ESP_OK) {
      reportError("USB host uninstall", error);
      return false;
    }
    libraryInstalled = false;
  }
  freeSynchronization();
  resetInput();
  if (!pm_usb::release(pm_usb::Owner::Keyboard))
    return false;
  stopRequested = false;
  return true;
}

} // namespace pm_keyboard
