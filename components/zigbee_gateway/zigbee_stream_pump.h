#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>

namespace esphome {
namespace zigbee_gateway {

enum class ZigbeeStreamIoState : uint8_t {
  PROGRESS = 0,
  WOULD_BLOCK = 1,
  CLOSED = 2,
  ERROR = 3,
};

struct ZigbeeStreamIoResult {
  ZigbeeStreamIoState state{ZigbeeStreamIoState::WOULD_BLOCK};
  size_t count{0};
  int error{0};

  static ZigbeeStreamIoResult progress(size_t count) {
    return {ZigbeeStreamIoState::PROGRESS, count, 0};
  }
  static ZigbeeStreamIoResult would_block() {
    return {ZigbeeStreamIoState::WOULD_BLOCK, 0, 0};
  }
  static ZigbeeStreamIoResult closed() {
    return {ZigbeeStreamIoState::CLOSED, 0, 0};
  }
  static ZigbeeStreamIoResult failed(int error = 0) {
    return {ZigbeeStreamIoState::ERROR, 0, error};
  }
};

/// One non-blocking byte-stream endpoint.
///
/// The stream pump deliberately knows nothing about sockets or UARTs. A thin
/// endpoint adapter translates the native API into these four results:
/// progress, temporary back-pressure, permanent closure, or error.
class ZigbeeStreamEndpoint {
 public:
  virtual ~ZigbeeStreamEndpoint() = default;
  virtual ZigbeeStreamIoResult read(uint8_t *data, size_t length) = 0;
  virtual ZigbeeStreamIoResult write(const uint8_t *data, size_t length) = 0;
};

enum class ZigbeeStreamPumpResult : uint8_t {
  ACTIVE = 0,
  LEFT_CLOSED = 1,
  RIGHT_CLOSED = 2,
  LEFT_ERROR = 3,
  RIGHT_ERROR = 4,
};

/// Buffered full-duplex byte pump shared by TCP and USB-bridged transports.
///
/// Each direction has its own retained buffer. This is important for TCP,
/// where a non-blocking socket may accept only part of a radio response; it is
/// also safe for UART-to-UART forwarding and gives both transports identical
/// buffering semantics.
class ZigbeeStreamPump {
 public:
  static constexpr size_t BUFFER_SIZE = 1024;
  static constexpr size_t IO_CHUNK_SIZE = 256;
  static constexpr size_t LOOP_IO_BUDGET = 1024;

  void set_left(ZigbeeStreamEndpoint *endpoint) { this->left_ = endpoint; }
  void set_right(ZigbeeStreamEndpoint *endpoint) { this->right_ = endpoint; }

  void reset() {
    this->left_to_right_ = DirectionBuffer{};
    this->right_to_left_ = DirectionBuffer{};
  }

  bool queue_left_to_right(const uint8_t *data, size_t length) {
    if (data == nullptr || length == 0)
      return true;
    if (this->left_to_right_.length != 0 || length > this->left_to_right_.data.size())
      return false;
    std::copy_n(data, length, this->left_to_right_.data.begin());
    this->left_to_right_.length = length;
    return true;
  }

  bool has_pending_data() const {
    return this->left_to_right_.length != 0 || this->right_to_left_.length != 0;
  }

  ZigbeeStreamPumpResult pump() {
    if (this->left_ == nullptr || this->right_ == nullptr)
      return ZigbeeStreamPumpResult::RIGHT_ERROR;

    const auto left_result =
        this->pump_direction_(this->left_, this->right_, this->left_to_right_,
                              ZigbeeStreamPumpResult::LEFT_CLOSED,
                              ZigbeeStreamPumpResult::LEFT_ERROR,
                              ZigbeeStreamPumpResult::RIGHT_CLOSED,
                              ZigbeeStreamPumpResult::RIGHT_ERROR);
    if (left_result != ZigbeeStreamPumpResult::ACTIVE)
      return left_result;

    return this->pump_direction_(this->right_, this->left_, this->right_to_left_,
                                 ZigbeeStreamPumpResult::RIGHT_CLOSED,
                                 ZigbeeStreamPumpResult::RIGHT_ERROR,
                                 ZigbeeStreamPumpResult::LEFT_CLOSED,
                                 ZigbeeStreamPumpResult::LEFT_ERROR);
  }

 protected:
  struct DirectionBuffer {
    std::array<uint8_t, BUFFER_SIZE> data{};
    size_t offset{0};
    size_t length{0};
  };

  ZigbeeStreamPumpResult pump_direction_(
      ZigbeeStreamEndpoint *source, ZigbeeStreamEndpoint *target,
      DirectionBuffer &buffer, ZigbeeStreamPumpResult source_closed,
      ZigbeeStreamPumpResult source_error, ZigbeeStreamPumpResult target_closed,
      ZigbeeStreamPumpResult target_error) {
    size_t budget = LOOP_IO_BUDGET;
    while (budget > 0) {
      if (buffer.length != 0) {
        const auto result = target->write(
            buffer.data.data() + buffer.offset, std::min(buffer.length, budget));
        switch (result.state) {
          case ZigbeeStreamIoState::PROGRESS:
            if (result.count == 0 || result.count > buffer.length)
              return target_error;
            buffer.offset += result.count;
            buffer.length -= result.count;
            budget -= result.count;
            if (buffer.length == 0)
              buffer.offset = 0;
            continue;
          case ZigbeeStreamIoState::WOULD_BLOCK:
            return ZigbeeStreamPumpResult::ACTIVE;
          case ZigbeeStreamIoState::CLOSED:
            return target_closed;
          case ZigbeeStreamIoState::ERROR:
            return target_error;
        }
      }

      const size_t requested =
          std::min({IO_CHUNK_SIZE, buffer.data.size(), budget});
      const auto result = source->read(buffer.data.data(), requested);
      switch (result.state) {
        case ZigbeeStreamIoState::PROGRESS:
          if (result.count == 0 || result.count > requested)
            return source_error;
          buffer.offset = 0;
          buffer.length = result.count;
          continue;
        case ZigbeeStreamIoState::WOULD_BLOCK:
          return ZigbeeStreamPumpResult::ACTIVE;
        case ZigbeeStreamIoState::CLOSED:
          return source_closed;
        case ZigbeeStreamIoState::ERROR:
          return source_error;
      }
    }
    return ZigbeeStreamPumpResult::ACTIVE;
  }

  ZigbeeStreamEndpoint *left_{nullptr};
  ZigbeeStreamEndpoint *right_{nullptr};
  DirectionBuffer left_to_right_{};
  DirectionBuffer right_to_left_{};
};

}  // namespace zigbee_gateway
}  // namespace esphome
