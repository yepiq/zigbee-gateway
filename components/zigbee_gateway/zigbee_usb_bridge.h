#pragma once

#include "esphome/components/uart/uart.h"

#include "zigbee_serial.h"
#include "zigbee_stream_adapters.h"
#include "zigbee_stream_pump.h"

namespace esphome {
namespace zigbee_gateway {

/// Software USB bridge using the same duplex pump as the TCP transport.
///
/// The external endpoint is ESP32 UART0 on the UZG-01 USB connector; the radio
/// endpoint remains behind ZigbeeSerialInterface so local diagnostics, TCP,
/// and the USB bridge can never consume the radio UART concurrently.
class ZigbeeUsbBridge {
 public:
  void set_serial(ZigbeeSerialInterface *serial) {
    this->serial_ = serial;
    this->radio_endpoint_.set_serial(serial);
    this->radio_endpoint_.set_owner(ZigbeeSerialInterface::Owner::USB_BRIDGE);
  }
  void set_usb_uart(uart::UARTComponent *uart) {
    this->usb_uart_ = uart;
    this->usb_endpoint_.set_uart(uart);
  }

  bool start();
  void loop();
  void stop();
  void reset_buffers();

  bool active() const { return this->active_; }

 protected:
  ZigbeeSerialInterface *serial_{nullptr};
  uart::UARTComponent *usb_uart_{nullptr};
  ZigbeeUartStreamEndpoint usb_endpoint_{};
  ZigbeeRadioStreamEndpoint radio_endpoint_{};
  ZigbeeStreamPump pump_{};
  bool active_{false};
};

}  // namespace zigbee_gateway
}  // namespace esphome
