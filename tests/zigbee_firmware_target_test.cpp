#include <cassert>
#include <string>
#include <vector>

#include "components/zigbee_gateway/zigbee_firmware_target.h"

using esphome::zigbee_gateway::select_initial_firmware_role;

int main() {
  const std::vector<std::string> roles{"coordinator", "router"};

  // A saved explicit target, including an adopted staged target, wins.
  assert(select_initial_firmware_role(roles, "coordinator", "router",
                                      "router") == "coordinator");

  // With no saved or staged target, follow the current radio role without
  // implying that its exact installed firmware version is known.
  assert(select_initial_firmware_role(roles, "", "router", "coordinator") ==
         "router");

  // The configured preference is only a fallback for an unknown or unsupported
  // current role.
  assert(select_initial_firmware_role(roles, "", "", "coordinator") ==
         "coordinator");
  assert(select_initial_firmware_role(roles, "", "end_device",
                                      "coordinator") == "coordinator");

  assert(select_initial_firmware_role(roles, "", "", "") ==
         "coordinator");
  assert(select_initial_firmware_role({}, "coordinator", "router",
                                      "coordinator").empty());
  return 0;
}
