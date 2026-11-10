#include "usb_role.h"

#include <pocketmage.h>
#include <esp_private/periph_ctrl.h>
#include <esp_private/usb_phy.h>
#include <soc/rtc_cntl_reg.h>
#include <soc/usb_serial_jtag_reg.h>

#include <cstdlib>

namespace pm_usb {
namespace {
constexpr const char *TAG = "USBRole";
Owner current = Owner::DebugSerial;

SemaphoreHandle_t roleMutex() {
  static SemaphoreHandle_t mutex = xSemaphoreCreateRecursiveMutex();
  if (!mutex) {
    ESP_LOGE(TAG, "Cannot create USB role mutex");
    std::abort();
  }
  return mutex;
}

bool phyFree() {
  usb_phy_status_t status;
  esp_err_t err = usb_phy_get_phy_status(USB_PHY_TARGET_INT, &status);
  // INVALID_STATE means the PHY driver itself is uninstalled.
  return err == ESP_ERR_INVALID_STATE || (err == ESP_OK && status == USB_PHY_STATUS_FREE);
}

void restoreSerial() {
  // usb_del_phy() disables the pads but does not select USB Serial/JTAG again.
  // The caller has stopped the host, or is immediately resetting the device.
  // https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-guides/usb-serial-jtag-console.html
  periph_module_reset(PERIPH_USB_MODULE);
  periph_module_disable(PERIPH_USB_MODULE);
  CLEAR_PERI_REG_MASK(RTC_CNTL_USB_CONF_REG, RTC_CNTL_SW_HW_USB_PHY_SEL |
                      RTC_CNTL_SW_USB_PHY_SEL | RTC_CNTL_USB_PAD_ENABLE);
  CLEAR_PERI_REG_MASK(USB_SERIAL_JTAG_CONF0_REG, USB_SERIAL_JTAG_PHY_SEL |
                      USB_SERIAL_JTAG_PAD_PULL_OVERRIDE | USB_SERIAL_JTAG_DP_PULLUP |
                      USB_SERIAL_JTAG_DM_PULLUP | USB_SERIAL_JTAG_DP_PULLDOWN |
                      USB_SERIAL_JTAG_DM_PULLDOWN);
  SET_PERI_REG_MASK(USB_SERIAL_JTAG_CONF0_REG, USB_SERIAL_JTAG_USB_PAD_ENABLE);
  PowerSystem.setUSBControlESP();
}
} // namespace

Lock::Lock() {
  if (xSemaphoreTakeRecursive(roleMutex(), portMAX_DELAY) != pdTRUE)
    std::abort();
}

Lock::~Lock() {
  if (xSemaphoreGiveRecursive(roleMutex()) != pdTRUE)
    std::abort();
}

Owner owner() {
  Lock lock;
  return current;
}

bool updatePower(bool hostAttached) {
  Lock lock;
  bool boostOn;
  if (!PowerSystem.getBoostState(boostOn))
    return false;
  if (boostOn != hostAttached) {
    if (!PowerSystem.setBoost(hostAttached))
      return false;
    if (!hostAttached) {
      detachInterrupt(digitalPinToInterrupt(PWR_BTN));
      attachInterrupt(digitalPinToInterrupt(PWR_BTN), pocketmage::PWR_BTN_irq, FALLING);
    }
  }
  sinkEnabled = hostAttached;
  PowerSystem.setUSBControlESP();
  return true;
}

bool claim(Owner requested) {
  Lock lock;
  if (requested == Owner::DebugSerial || current != Owner::DebugSerial || !phyFree())
    return false;
  bool needBoost = false;
  if (requested != Owner::Storage && !PowerSystem.getOTGNeed(needBoost))
    return false;
  if (!updatePower(needBoost))
    return false;
  current = requested;
  return true;
}

bool release(Owner requested) {
  Lock lock;
  if (requested == Owner::DebugSerial || current != requested || !phyFree())
    return false;
  if (!updatePower(false))
    return false;
  restoreSerial();
  current = Owner::DebugSerial;
  return true;
}

void restoreSerialForRestart() {
  Lock lock;
  restoreSerial();
}
} // namespace pm_usb
