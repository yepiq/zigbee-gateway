#include "zigbee_usb_bridge.h"

#include "esphome/core/log.h"

namespace esphome {
namespace zigbee_gateway {

static const char *const USB_BRIDGE_TAG = "zigbee_gateway.usb";

bool ZigbeeUsbBridge::start() {
  if (this->active_)
    return true;
  if (this->serial_ == nullptr || this->usb_uart_ == nullptr) {
    ESP_LOGE(USB_BRIDGE_TAG, "Cannot start USB bridge without both UART endpoints");
    return false;
  }
  if (!this->serial_->claim(ZigbeeSerialInterface::Owner::USB_BRIDGE)) {
    ESP_LOGW(USB_BRIDGE_TAG, "Cannot start USB bridge while the radio UART has another owner");
    return false;
  }

  this->pump_.set_left(&this->usb_endpoint_);
  this->pump_.set_right(&this->radio_endpoint_);
  this->pump_.reset();
  this->usb_endpoint_.drain();
  this->serial_->drain(ZigbeeSerialInterface::Owner::USB_BRIDGE);
  this->active_ = true;
  ESP_LOGI(USB_BRIDGE_TAG, "USB software bridge started");
  return true;
}

void ZigbeeUsbBridge::loop() {
  if (!this->active_)
    return;
  if (this->pump_.pump() != ZigbeeStreamPumpResult::ACTIVE)
    ESP_LOGE(USB_BRIDGE_TAG, "USB bridge endpoint failed; byte forwarding paused");
}

void ZigbeeUsbBridge::stop() {
  if (!this->active_)
    return;
  this->active_ = false;
  this->pump_.reset();
  this->usb_endpoint_.drain();
  if (this->serial_ != nullptr) {
    this->serial_->drain(ZigbeeSerialInterface::Owner::USB_BRIDGE);
    this->serial_->release(ZigbeeSerialInterface::Owner::USB_BRIDGE);
  }
  ESP_LOGI(USB_BRIDGE_TAG, "USB software bridge stopped");
}

void ZigbeeUsbBridge::reset_buffers() {
  this->pump_.reset();
  this->usb_endpoint_.drain();
  if (this->serial_ != nullptr)
    this->serial_->drain(ZigbeeSerialInterface::Owner::USB_BRIDGE);
}

}  // namespace zigbee_gateway
}  // namespace esphome
