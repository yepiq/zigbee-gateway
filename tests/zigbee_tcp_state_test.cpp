#include <cassert>
#include <string>

#include "components/zigbee_gateway/zigbee_tcp_state.h"

using esphome::zigbee_gateway::ZigbeeTcpAcceptAction;
using esphome::zigbee_gateway::ZigbeeTcpActiveState;
using esphome::zigbee_gateway::ZigbeeTcpBslAction;
using esphome::zigbee_gateway::ZigbeeTcpDisconnectAction;
using esphome::zigbee_gateway::ZigbeeTcpEvent;
using esphome::zigbee_gateway::ZigbeeTcpResetAction;
using esphome::zigbee_gateway::ZigbeeTcpState;
using esphome::zigbee_gateway::zigbee_tcp_active_state_name;
using esphome::zigbee_gateway::zigbee_tcp_event_name;

static void assert_valid(const ZigbeeTcpState &state) { assert(state.valid()); }

static void test_normal_and_pending_clients() {
  ZigbeeTcpState state;
  assert(state.last_event() == ZigbeeTcpEvent::INITIALIZED);
  assert(state.revision() == 0);
  assert(state.accept_client() == ZigbeeTcpAcceptAction::ACTIVATE_PROVISIONAL);
  assert(state.receive_active_payload());
  assert(state.active() == ZigbeeTcpActiveState::NORMAL);
  assert(state.last_event() == ZigbeeTcpEvent::NORMAL_SESSION_STARTED);
  assert(state.accept_client() == ZigbeeTcpAcceptAction::HOLD_PENDING);
  assert(state.pending());
  assert(state.accept_client() == ZigbeeTcpAcceptAction::REJECT);
  assert(state.counters().rejected_connections == 1);
  assert(state.last_event() == ZigbeeTcpEvent::CONNECTION_REJECTED);
  state.disconnect_pending();
  assert(state.last_event() == ZigbeeTcpEvent::PENDING_CLIENT_DISCONNECTED);
  assert(state.accept_client() == ZigbeeTcpAcceptAction::HOLD_PENDING);

  const auto disconnected = state.disconnect_active();
  assert(disconnected.action == ZigbeeTcpDisconnectAction::PROMOTE_PENDING);
  assert(!disconnected.recover_radio);
  assert(state.active() == ZigbeeTcpActiveState::PROVISIONAL);
  assert(!state.pending());
  assert(state.last_event() == ZigbeeTcpEvent::PENDING_CLIENT_PROMOTED);
  assert_valid(state);
}

static void test_silent_provisional_disconnect() {
  ZigbeeTcpState state;
  assert(state.accept_client() == ZigbeeTcpAcceptAction::ACTIVATE_PROVISIONAL);

  const auto disconnected = state.disconnect_active();
  assert(disconnected.action == ZigbeeTcpDisconnectAction::PROVISIONAL_ENDED);
  assert(!disconnected.recover_radio);
  assert(state.active() == ZigbeeTcpActiveState::IDLE);
  assert(state.last_event() == ZigbeeTcpEvent::PROVISIONAL_CLIENT_DISCONNECTED);
  assert(std::string(zigbee_tcp_event_name(state.last_event())) ==
         "Provisional Client Disconnected");
  assert_valid(state);
}

static void test_connection_first_bsl_takeover() {
  ZigbeeTcpState state;
  assert(state.accept_client() == ZigbeeTcpAcceptAction::ACTIVATE_PROVISIONAL);
  assert(state.receive_active_payload());
  assert(state.accept_client() == ZigbeeTcpAcceptAction::HOLD_PENDING);

  assert(state.request_bsl(true) == ZigbeeTcpBslAction::TAKE_OVER_WITH_PENDING);
  assert(state.active() == ZigbeeTcpActiveState::MAINTENANCE);
  assert(!state.pending());
  assert(state.parked());
  assert(state.bsl_entered());
  assert(state.counters().maintenance_sessions == 1);
  assert(state.last_event() == ZigbeeTcpEvent::MAINTENANCE_SESSION_STARTED);

  const auto disconnected = state.disconnect_active();
  assert(disconnected.action == ZigbeeTcpDisconnectAction::FINISH_MAINTENANCE);
  assert(disconnected.recover_radio);
  assert(state.active() == ZigbeeTcpActiveState::IDLE);
  assert(!state.parked());
  assert(state.last_event() == ZigbeeTcpEvent::MAINTENANCE_SESSION_FINISHED);
  state.record_recovery_reset();
  assert(state.counters().recovery_resets == 1);
  assert(state.last_event() == ZigbeeTcpEvent::RECOVERY_RESET);
  assert_valid(state);
}

static void test_legacy_command_first_bsl() {
  ZigbeeTcpState state;
  assert(state.request_bsl(false) == ZigbeeTcpBslAction::ENTER_BSL_AND_WAIT);
  assert(state.bsl_armed());
  assert(state.bsl_entered());
  assert(state.last_event() == ZigbeeTcpEvent::BSL_RENDEZVOUS_ARMED);
  assert(state.accept_client() == ZigbeeTcpAcceptAction::ACTIVATE_MAINTENANCE);
  assert(state.active() == ZigbeeTcpActiveState::MAINTENANCE);
  assert(!state.bsl_armed());
  assert(state.bsl_entered());
  assert(state.counters().maintenance_sessions == 1);
  assert_valid(state);
}

static void test_armed_takeover_preserves_normal_until_flashing_socket() {
  ZigbeeTcpState state;
  assert(state.accept_client() == ZigbeeTcpAcceptAction::ACTIVATE_PROVISIONAL);
  assert(state.receive_active_payload());
  assert(state.request_bsl(true) == ZigbeeTcpBslAction::ARM_AND_WAIT);
  assert(state.active() == ZigbeeTcpActiveState::NORMAL);
  assert(state.bsl_armed());
  assert(!state.bsl_entered());
  assert(state.last_event() == ZigbeeTcpEvent::BSL_RENDEZVOUS_ARMED);

  assert(state.accept_client() == ZigbeeTcpAcceptAction::TAKE_OVER_WITH_NEW_CLIENT);
  assert(state.active() == ZigbeeTcpActiveState::MAINTENANCE);
  assert(state.parked());
  assert(state.bsl_entered());
  assert(state.counters().maintenance_sessions == 1);
  assert_valid(state);
}

static void test_armed_owner_disconnect_enters_bsl_for_replacement() {
  ZigbeeTcpState state;
  assert(state.accept_client() == ZigbeeTcpAcceptAction::ACTIVATE_PROVISIONAL);
  assert(state.receive_active_payload());
  assert(state.request_bsl(true) == ZigbeeTcpBslAction::ARM_AND_WAIT);
  assert(state.disconnect_active().action == ZigbeeTcpDisconnectAction::NORMAL_ENDED);
  assert(state.active() == ZigbeeTcpActiveState::IDLE);
  assert(state.bsl_armed());
  assert(!state.bsl_entered());

  assert(state.accept_client() ==
         ZigbeeTcpAcceptAction::ACTIVATE_MAINTENANCE_AND_ENTER_BSL);
  assert(state.active() == ZigbeeTcpActiveState::MAINTENANCE);
  assert(state.bsl_entered());
  assert(state.counters().maintenance_sessions == 1);
  assert_valid(state);
}

static void test_reset_and_timeouts() {
  ZigbeeTcpState state;
  assert(state.accept_client() == ZigbeeTcpAcceptAction::ACTIVATE_PROVISIONAL);
  assert(state.request_reset(false) == ZigbeeTcpResetAction::APPLY_TO_ACTIVE);
  assert(state.active() == ZigbeeTcpActiveState::MAINTENANCE);
  assert(!state.bsl_entered());
  assert(state.counters().maintenance_sessions == 1);
  assert(state.disconnect_active().action == ZigbeeTcpDisconnectAction::FINISH_MAINTENANCE);

  assert(state.request_bsl(false) == ZigbeeTcpBslAction::ENTER_BSL_AND_WAIT);
  assert(state.expire_bsl_rendezvous());
  assert(state.last_event() == ZigbeeTcpEvent::BSL_RENDEZVOUS_TIMED_OUT);
  assert(!state.bsl_armed());
  assert(!state.bsl_entered());
  state.record_recovery_reset();
  assert(state.counters().recovery_resets == 1);
  assert(state.last_event() == ZigbeeTcpEvent::RECOVERY_RESET);
  assert_valid(state);
}

static void test_pending_timeout_and_names() {
  ZigbeeTcpState state;
  assert(state.accept_client() == ZigbeeTcpAcceptAction::ACTIVATE_PROVISIONAL);
  assert(state.receive_active_payload());
  assert(state.accept_client() == ZigbeeTcpAcceptAction::HOLD_PENDING);
  state.timeout_pending();
  assert(!state.pending());
  assert(state.counters().pending_timeouts == 1);
  assert(state.last_event() == ZigbeeTcpEvent::PENDING_CLIENT_TIMED_OUT);
  assert(std::string(zigbee_tcp_active_state_name(state.active())) == "normal");
  assert(std::string(zigbee_tcp_event_name(state.last_event())) ==
         "Pending Client Timed Out");
}

int main() {
  test_normal_and_pending_clients();
  test_silent_provisional_disconnect();
  test_connection_first_bsl_takeover();
  test_legacy_command_first_bsl();
  test_armed_takeover_preserves_normal_until_flashing_socket();
  test_armed_owner_disconnect_enters_bsl_for_replacement();
  test_reset_and_timeouts();
  test_pending_timeout_and_names();
  return 0;
}
