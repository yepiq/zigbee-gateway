#include <cassert>

#include "components/zigbee_gateway/zigbee_tcp_state.h"

using esphome::zigbee_gateway::ZigbeeTcpAcceptAction;
using esphome::zigbee_gateway::ZigbeeTcpActiveState;
using esphome::zigbee_gateway::ZigbeeTcpBslAction;
using esphome::zigbee_gateway::ZigbeeTcpDisconnectAction;
using esphome::zigbee_gateway::ZigbeeTcpResetAction;
using esphome::zigbee_gateway::ZigbeeTcpState;

static void assert_valid(const ZigbeeTcpState &state) { assert(state.valid()); }

static void test_normal_and_pending_clients() {
  ZigbeeTcpState state;
  assert(state.accept_client() == ZigbeeTcpAcceptAction::ACTIVATE_PROVISIONAL);
  assert(state.receive_active_payload());
  assert(state.active() == ZigbeeTcpActiveState::NORMAL);
  assert(state.accept_client() == ZigbeeTcpAcceptAction::HOLD_PENDING);
  assert(state.pending());
  assert(state.accept_client() == ZigbeeTcpAcceptAction::REJECT);
  state.close_pending();
  assert(state.accept_client() == ZigbeeTcpAcceptAction::HOLD_PENDING);

  const auto disconnected = state.disconnect_active();
  assert(disconnected.action == ZigbeeTcpDisconnectAction::PROMOTE_PENDING);
  assert(!disconnected.recover_radio);
  assert(state.active() == ZigbeeTcpActiveState::PROVISIONAL);
  assert(!state.pending());
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

  const auto disconnected = state.disconnect_active();
  assert(disconnected.action == ZigbeeTcpDisconnectAction::FINISH_MAINTENANCE);
  assert(disconnected.recover_radio);
  assert(state.active() == ZigbeeTcpActiveState::IDLE);
  assert(!state.parked());
  assert_valid(state);
}

static void test_legacy_command_first_bsl() {
  ZigbeeTcpState state;
  assert(state.request_bsl(false) == ZigbeeTcpBslAction::ENTER_BSL_AND_WAIT);
  assert(state.bsl_armed());
  assert(state.bsl_entered());
  assert(state.accept_client() == ZigbeeTcpAcceptAction::ACTIVATE_MAINTENANCE);
  assert(state.active() == ZigbeeTcpActiveState::MAINTENANCE);
  assert(!state.bsl_armed());
  assert(state.bsl_entered());
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

  assert(state.accept_client() == ZigbeeTcpAcceptAction::TAKE_OVER_WITH_NEW_CLIENT);
  assert(state.active() == ZigbeeTcpActiveState::MAINTENANCE);
  assert(state.parked());
  assert(state.bsl_entered());
  assert_valid(state);
}

static void test_armed_owner_disconnect_current_behavior() {
  ZigbeeTcpState state;
  assert(state.accept_client() == ZigbeeTcpAcceptAction::ACTIVATE_PROVISIONAL);
  assert(state.receive_active_payload());
  assert(state.request_bsl(true) == ZigbeeTcpBslAction::ARM_AND_WAIT);
  assert(state.disconnect_active().action == ZigbeeTcpDisconnectAction::NORMAL_ENDED);
  assert(state.active() == ZigbeeTcpActiveState::IDLE);
  assert(state.bsl_armed());
  assert(!state.bsl_entered());

  // Behavior-preserving extraction: the old server classified this next
  // client as maintenance without entering BSL. A separate corrective commit
  // changes this transition and its expected assertions.
  assert(state.accept_client() == ZigbeeTcpAcceptAction::ACTIVATE_MAINTENANCE);
  assert(state.active() == ZigbeeTcpActiveState::MAINTENANCE);
  assert(!state.bsl_entered());
  assert_valid(state);
}

static void test_reset_and_timeouts() {
  ZigbeeTcpState state;
  assert(state.accept_client() == ZigbeeTcpAcceptAction::ACTIVATE_PROVISIONAL);
  assert(state.request_reset(false) == ZigbeeTcpResetAction::APPLY_TO_ACTIVE);
  assert(state.active() == ZigbeeTcpActiveState::MAINTENANCE);
  assert(!state.bsl_entered());
  assert(state.disconnect_active().action == ZigbeeTcpDisconnectAction::FINISH_MAINTENANCE);

  assert(state.request_bsl(false) == ZigbeeTcpBslAction::ENTER_BSL_AND_WAIT);
  assert(state.expire_bsl_rendezvous());
  assert(!state.bsl_armed());
  assert(!state.bsl_entered());
  assert_valid(state);
}

int main() {
  test_normal_and_pending_clients();
  test_connection_first_bsl_takeover();
  test_legacy_command_first_bsl();
  test_armed_takeover_preserves_normal_until_flashing_socket();
  test_armed_owner_disconnect_current_behavior();
  test_reset_and_timeouts();
  return 0;
}
