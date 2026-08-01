#pragma once

#include <string>

namespace esphome::zigbee_gateway {

enum class ZigbeeResetCompletionPolicy {
  WAIT_FOR_ZNP_RESET_IND,
  SETTLE_ONLY,
};

inline ZigbeeResetCompletionPolicy zigbee_reset_completion_policy(
    const std::string &role) {
  return role == "Router"
             ? ZigbeeResetCompletionPolicy::SETTLE_ONLY
             : ZigbeeResetCompletionPolicy::WAIT_FOR_ZNP_RESET_IND;
}

}  // namespace esphome::zigbee_gateway
