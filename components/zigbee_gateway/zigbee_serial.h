#pragma once

#include <cstddef>
#include <cstdint>

#include "esphome/components/uart/uart.h"

namespace esphome {
namespace zigbee_gateway {

/// Single ownership gate for the Zigbee radio UART.
///
/// ESPHome permits more than one UARTDevice to reference the same UART bus, but
/// it does not arbitrate reads. The gateway therefore exposes the physical UART
/// only through this interface. Local ZNP/BSL diagnostics and the TCP bridge
/// must explicitly own it before consuming or producing bytes.
class ZigbeeSerialInterface {
 public:
  enum class Owner : uint8_t {
    NONE = 0,
    LOCAL = 1,
    TCP_NORMAL = 2,
    TCP_MAINTENANCE = 3,
  };

  void set_uart(uart::UARTComponent *uart) { this->uart_ = uart; }

  Owner owner() const { return this->owner_; }
  bool is_local_owner() const { return this->owner_ == Owner::LOCAL; }
  bool is_tcp_owner() const {
    return this->owner_ == Owner::TCP_NORMAL || this->owner_ == Owner::TCP_MAINTENANCE;
  }

  bool claim(Owner owner) {
    if (owner == Owner::NONE)
      return false;
    if (this->owner_ != Owner::NONE && this->owner_ != owner)
      return false;
    this->owner_ = owner;
    return true;
  }

  void set_owner(Owner owner) { this->owner_ = owner; }

  void release(Owner owner) {
    if (this->owner_ == owner)
      this->owner_ = Owner::NONE;
  }

  /// Local protocol-facing operations. These deliberately mirror the small
  /// UARTComponent surface used by protocol.h.
  int available() const {
    if (!this->is_local_owner() || this->uart_ == nullptr)
      return 0;
    return this->uart_->available();
  }

  bool read_byte(uint8_t *data) {
    return this->is_local_owner() && this->uart_ != nullptr && this->uart_->read_byte(data);
  }

  bool read_array(uint8_t *data, size_t length) {
    return this->is_local_owner() && this->uart_ != nullptr && this->uart_->read_array(data, length);
  }

  void write_byte(uint8_t data) {
    if (this->is_local_owner() && this->uart_ != nullptr)
      this->uart_->write_byte(data);
  }

  void write_array(const uint8_t *data, size_t length) {
    if (this->is_local_owner() && this->uart_ != nullptr)
      this->uart_->write_array(data, length);
  }

  void flush() {
    if (this->is_local_owner() && this->uart_ != nullptr)
      this->uart_->flush();
  }

  /// TCP bridge operations use a separate surface so a local protocol routine
  /// cannot accidentally operate while a remote session owns the UART.
  int remote_available() const {
    if (!this->is_tcp_owner() || this->uart_ == nullptr)
      return 0;
    return this->uart_->available();
  }

  bool remote_read_array(uint8_t *data, size_t length) {
    return this->is_tcp_owner() && this->uart_ != nullptr && this->uart_->read_array(data, length);
  }

  void remote_write_array(const uint8_t *data, size_t length) {
    if (this->is_tcp_owner() && this->uart_ != nullptr)
      this->uart_->write_array(data, length);
  }

  void drain(Owner owner) {
    if (this->uart_ == nullptr || this->owner_ != owner)
      return;
    uint8_t byte;
    while (this->uart_->available() && this->uart_->read_byte(&byte)) {
    }
  }

 protected:
  uart::UARTComponent *uart_{nullptr};
  Owner owner_{Owner::NONE};
};

}  // namespace zigbee_gateway
}  // namespace esphome
