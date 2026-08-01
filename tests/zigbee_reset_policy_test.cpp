#include <cassert>

#include "components/zigbee_gateway/zigbee_reset_policy.h"

using esphome::zigbee_gateway::ZigbeeResetCompletionPolicy;
using esphome::zigbee_gateway::zigbee_reset_completion_policy;

int main() {
  assert(zigbee_reset_completion_policy("Router") ==
         ZigbeeResetCompletionPolicy::SETTLE_ONLY);
  assert(zigbee_reset_completion_policy("Coordinator") ==
         ZigbeeResetCompletionPolicy::WAIT_FOR_ZNP_RESET_IND);
  assert(zigbee_reset_completion_policy("Unknown") ==
         ZigbeeResetCompletionPolicy::WAIT_FOR_ZNP_RESET_IND);
  assert(zigbee_reset_completion_policy("") ==
         ZigbeeResetCompletionPolicy::WAIT_FOR_ZNP_RESET_IND);
  return 0;
}
