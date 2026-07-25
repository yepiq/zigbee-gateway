#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <initializer_list>
#include <vector>

#include "components/zigbee_gateway/zigbee_stream_pump.h"

using esphome::zigbee_gateway::ZigbeeStreamEndpoint;
using esphome::zigbee_gateway::ZigbeeStreamIoResult;
using esphome::zigbee_gateway::ZigbeeStreamPump;
using esphome::zigbee_gateway::ZigbeeStreamPumpResult;

class MockEndpoint : public ZigbeeStreamEndpoint {
 public:
  std::deque<uint8_t> input{};
  std::vector<uint8_t> output{};
  size_t write_limit{static_cast<size_t>(-1)};
  bool read_blocked{false};
  bool write_blocked{false};
  bool closed{false};
  bool failed{false};

  ZigbeeStreamIoResult read(uint8_t *data, size_t length) override {
    if (failed)
      return ZigbeeStreamIoResult::failed(1);
    if (closed && input.empty())
      return ZigbeeStreamIoResult::closed();
    if (read_blocked || input.empty())
      return ZigbeeStreamIoResult::would_block();
    const size_t count = std::min(length, input.size());
    for (size_t index = 0; index < count; index++) {
      data[index] = input.front();
      input.pop_front();
    }
    return ZigbeeStreamIoResult::progress(count);
  }

  ZigbeeStreamIoResult write(const uint8_t *data, size_t length) override {
    if (failed)
      return ZigbeeStreamIoResult::failed(1);
    if (closed)
      return ZigbeeStreamIoResult::closed();
    if (write_blocked)
      return ZigbeeStreamIoResult::would_block();
    const size_t count = std::min(length, write_limit);
    if (count == 0)
      return ZigbeeStreamIoResult::would_block();
    output.insert(output.end(), data, data + count);
    return ZigbeeStreamIoResult::progress(count);
  }
};

static void append(MockEndpoint &endpoint, std::initializer_list<uint8_t> bytes) {
  endpoint.input.insert(endpoint.input.end(), bytes.begin(), bytes.end());
}

static void test_bidirectional_forwarding() {
  MockEndpoint left;
  MockEndpoint right;
  append(left, {0x01, 0x02, 0x03});
  append(right, {0xA1, 0xA2});

  ZigbeeStreamPump pump;
  pump.set_left(&left);
  pump.set_right(&right);
  assert(pump.pump() == ZigbeeStreamPumpResult::ACTIVE);
  assert((right.output == std::vector<uint8_t>{0x01, 0x02, 0x03}));
  assert((left.output == std::vector<uint8_t>{0xA1, 0xA2}));
  assert(!pump.has_pending_data());
}

static void test_partial_write_is_retained() {
  MockEndpoint left;
  MockEndpoint right;
  append(right, {0x10, 0x11, 0x12, 0x13});
  left.write_limit = 2;

  ZigbeeStreamPump pump;
  pump.set_left(&left);
  pump.set_right(&right);
  assert(pump.pump() == ZigbeeStreamPumpResult::ACTIVE);
  assert((left.output == std::vector<uint8_t>{0x10, 0x11, 0x12, 0x13}));

  append(right, {0x20, 0x21, 0x22});
  left.write_blocked = true;
  assert(pump.pump() == ZigbeeStreamPumpResult::ACTIVE);
  assert(pump.has_pending_data());
  left.write_blocked = false;
  assert(pump.pump() == ZigbeeStreamPumpResult::ACTIVE);
  assert((left.output == std::vector<uint8_t>{
                             0x10, 0x11, 0x12, 0x13, 0x20, 0x21, 0x22}));
  assert(!pump.has_pending_data());
}

static void test_prebuffer_and_reset() {
  MockEndpoint left;
  MockEndpoint right;
  const uint8_t first[] = {0xFE, 0x00, 0x21, 0x01};

  ZigbeeStreamPump pump;
  pump.set_left(&left);
  pump.set_right(&right);
  right.write_blocked = true;
  assert(pump.queue_left_to_right(first, sizeof(first)));
  assert(!pump.queue_left_to_right(first, sizeof(first)));
  assert(pump.pump() == ZigbeeStreamPumpResult::ACTIVE);
  assert(pump.has_pending_data());
  pump.reset();
  assert(!pump.has_pending_data());

  right.write_blocked = false;
  assert(pump.queue_left_to_right(first, sizeof(first)));
  assert(pump.pump() == ZigbeeStreamPumpResult::ACTIVE);
  assert((right.output == std::vector<uint8_t>{0xFE, 0x00, 0x21, 0x01}));
}

static void test_closure_and_error_side() {
  MockEndpoint left;
  MockEndpoint right;
  ZigbeeStreamPump pump;
  pump.set_left(&left);
  pump.set_right(&right);

  left.closed = true;
  assert(pump.pump() == ZigbeeStreamPumpResult::LEFT_CLOSED);

  left.closed = false;
  left.failed = true;
  assert(pump.pump() == ZigbeeStreamPumpResult::LEFT_ERROR);

  left.failed = false;
  left.read_blocked = true;
  right.failed = true;
  assert(pump.pump() == ZigbeeStreamPumpResult::RIGHT_ERROR);
}

int main() {
  test_bidirectional_forwarding();
  test_partial_write_is_retained();
  test_prebuffer_and_reset();
  test_closure_and_error_side();
  return 0;
}
