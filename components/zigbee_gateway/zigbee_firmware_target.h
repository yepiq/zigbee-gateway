#pragma once

#include <algorithm>
#include <string>
#include <vector>

namespace esphome::zigbee_gateway {

inline std::string select_initial_firmware_role(
    const std::vector<std::string> &available_roles,
    const std::string &saved_target_role,
    const std::string &current_radio_role,
    const std::string &preferred_role) {
  const auto available = [&available_roles](const std::string &role) {
    return !role.empty() &&
           std::find(available_roles.begin(), available_roles.end(), role) !=
               available_roles.end();
  };

  if (available(saved_target_role))
    return saved_target_role;
  if (available(current_radio_role))
    return current_radio_role;
  if (available(preferred_role))
    return preferred_role;
  return available_roles.empty() ? std::string{} : available_roles.front();
}

}  // namespace esphome::zigbee_gateway
