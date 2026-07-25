#pragma once

#include <algorithm>
#include <cerrno>
#include <cstddef>
#include <cstdint>

#include "esphome/components/socket/socket.h"
#include "esphome/components/uart/uart.h"

#include "zigbee_serial.h"
#include "zigbee_stream_pump.h"

namespace esphome {
namespace zigbee_gateway {

class ZigbeeRadioStreamEndpoint : public ZigbeeStreamEndpoint {
 public:
  void set_serial(ZigbeeSerialInterface *serial) { this->serial_ = serial; }
  void set_owner(ZigbeeSerialInterface::Owner owner) { this->owner_ = owner; }

  ZigbeeStreamIoResult read(uint8_t *data, size_t length) override {
    if (this->serial_ == nullptr || this->serial_->owner() != this->owner_)
      return ZigbeeStreamIoResult::failed();
    const int available = this->serial_->stream_available(this->owner_);
    if (available <= 0)
      return ZigbeeStreamIoResult::would_block();
    const size_t count = std::min(length, static_cast<size_t>(available));
    if (!this->serial_->stream_read_array(this->owner_, data, count))
      return ZigbeeStreamIoResult::failed();
    return ZigbeeStreamIoResult::progress(count);
  }

  ZigbeeStreamIoResult write(const uint8_t *data, size_t length) override {
    if (this->serial_ == nullptr ||
        !this->serial_->stream_write_array(this->owner_, data, length))
      return ZigbeeStreamIoResult::failed();
    return ZigbeeStreamIoResult::progress(length);
  }

 protected:
  ZigbeeSerialInterface *serial_{nullptr};
  ZigbeeSerialInterface::Owner owner_{ZigbeeSerialInterface::Owner::NONE};
};

class ZigbeeUartStreamEndpoint : public ZigbeeStreamEndpoint {
 public:
  void set_uart(uart::UARTComponent *uart) { this->uart_ = uart; }

  void drain() {
    if (this->uart_ == nullptr)
      return;
    uint8_t byte = 0;
    while (this->uart_->available() && this->uart_->read_byte(&byte)) {
    }
  }

  ZigbeeStreamIoResult read(uint8_t *data, size_t length) override {
    if (this->uart_ == nullptr)
      return ZigbeeStreamIoResult::failed();
    const int available = this->uart_->available();
    if (available <= 0)
      return ZigbeeStreamIoResult::would_block();
    const size_t count = std::min(length, static_cast<size_t>(available));
    if (!this->uart_->read_array(data, count))
      return ZigbeeStreamIoResult::failed();
    return ZigbeeStreamIoResult::progress(count);
  }

  ZigbeeStreamIoResult write(const uint8_t *data, size_t length) override {
    if (this->uart_ == nullptr)
      return ZigbeeStreamIoResult::failed();
    this->uart_->write_array(data, length);
    return ZigbeeStreamIoResult::progress(length);
  }

 protected:
  uart::UARTComponent *uart_{nullptr};
};

class ZigbeeSocketStreamEndpoint : public ZigbeeStreamEndpoint {
 public:
  void set_socket(socket::Socket *socket) {
    this->socket_ = socket;
    this->last_error_ = 0;
  }
  int last_error() const { return this->last_error_; }

  ZigbeeStreamIoResult read(uint8_t *data, size_t length) override {
    if (this->socket_ == nullptr)
      return ZigbeeStreamIoResult::closed();
    const ssize_t count = this->socket_->read(data, length);
    if (count > 0)
      return ZigbeeStreamIoResult::progress(static_cast<size_t>(count));
    if (count == 0)
      return ZigbeeStreamIoResult::closed();
    if (errno == EWOULDBLOCK || errno == EAGAIN)
      return ZigbeeStreamIoResult::would_block();
    this->last_error_ = errno;
    return ZigbeeStreamIoResult::failed(errno);
  }

  ZigbeeStreamIoResult write(const uint8_t *data, size_t length) override {
    if (this->socket_ == nullptr)
      return ZigbeeStreamIoResult::closed();
    const ssize_t count = this->socket_->write(data, length);
    if (count > 0)
      return ZigbeeStreamIoResult::progress(static_cast<size_t>(count));
    if (count == 0)
      return ZigbeeStreamIoResult::closed();
    if (errno == EWOULDBLOCK || errno == EAGAIN)
      return ZigbeeStreamIoResult::would_block();
    this->last_error_ = errno;
    return ZigbeeStreamIoResult::failed(errno);
  }

 protected:
  socket::Socket *socket_{nullptr};
  int last_error_{0};
};

}  // namespace zigbee_gateway
}  // namespace esphome
