#pragma once

#include <cstdint>

namespace esphome {
namespace zigbee_gateway {

enum class ZigbeeTcpActiveState : uint8_t {
  IDLE = 0,
  PROVISIONAL = 1,
  NORMAL = 2,
  MAINTENANCE = 3,
};

enum class ZigbeeTcpAcceptAction : uint8_t {
  ACTIVATE_PROVISIONAL = 0,
  ACTIVATE_MAINTENANCE = 1,
  ACTIVATE_MAINTENANCE_AND_ENTER_BSL = 2,
  HOLD_PENDING = 3,
  TAKE_OVER_WITH_NEW_CLIENT = 4,
  REJECT = 5,
};

enum class ZigbeeTcpBslAction : uint8_t {
  APPLY_TO_ACTIVE = 0,
  TAKE_OVER_WITH_PENDING = 1,
  ARM_AND_WAIT = 2,
  ENTER_BSL_AND_WAIT = 3,
};

enum class ZigbeeTcpResetAction : uint8_t {
  APPLY_TO_ACTIVE = 0,
  TAKE_OVER_WITH_PENDING = 1,
  RESET_ONLY = 2,
};

enum class ZigbeeTcpDisconnectAction : uint8_t {
  NORMAL_ENDED = 0,
  PROMOTE_PENDING = 1,
  FINISH_MAINTENANCE = 2,
};

enum class ZigbeeTcpEvent : uint8_t {
  INITIALIZED = 0,
  PROVISIONAL_CLIENT_CONNECTED = 1,
  NORMAL_SESSION_STARTED = 2,
  PENDING_CLIENT_CONNECTED = 3,
  CONNECTION_REJECTED = 4,
  PENDING_CLIENT_DISCONNECTED = 5,
  PENDING_CLIENT_TIMED_OUT = 6,
  PENDING_CLIENT_PROMOTED = 7,
  BSL_RENDEZVOUS_ARMED = 8,
  MAINTENANCE_SESSION_STARTED = 9,
  RADIO_RESET_REQUESTED = 10,
  NORMAL_SESSION_ENDED = 11,
  MAINTENANCE_SESSION_FINISHED = 12,
  BSL_RENDEZVOUS_TIMED_OUT = 13,
  PARKED_CLIENT_DISCONNECTED = 14,
  PARKED_CLIENT_TIMED_OUT = 15,
  RECOVERY_RESET = 16,
  SERVER_SHUTDOWN = 17,
  BSL_ENTERED = 18,
};

struct ZigbeeTcpCounters {
  uint32_t rejected_connections{0};
  uint32_t pending_timeouts{0};
  uint32_t maintenance_sessions{0};
  uint32_t recovery_resets{0};
};

struct ZigbeeTcpDisconnectResult {
  ZigbeeTcpDisconnectAction action;
  bool recover_radio;
};

inline const char *zigbee_tcp_active_state_name(ZigbeeTcpActiveState state) {
  switch (state) {
    case ZigbeeTcpActiveState::IDLE:
      return "idle";
    case ZigbeeTcpActiveState::PROVISIONAL:
      return "provisional";
    case ZigbeeTcpActiveState::NORMAL:
      return "normal";
    case ZigbeeTcpActiveState::MAINTENANCE:
      return "maintenance";
  }
  return "unknown";
}

inline const char *zigbee_tcp_event_name(ZigbeeTcpEvent event) {
  switch (event) {
    case ZigbeeTcpEvent::INITIALIZED:
      return "Initialized";
    case ZigbeeTcpEvent::PROVISIONAL_CLIENT_CONNECTED:
      return "Provisional Client Connected";
    case ZigbeeTcpEvent::NORMAL_SESSION_STARTED:
      return "Normal Session Started";
    case ZigbeeTcpEvent::PENDING_CLIENT_CONNECTED:
      return "Pending Client Connected";
    case ZigbeeTcpEvent::CONNECTION_REJECTED:
      return "Connection Rejected";
    case ZigbeeTcpEvent::PENDING_CLIENT_DISCONNECTED:
      return "Pending Client Disconnected";
    case ZigbeeTcpEvent::PENDING_CLIENT_TIMED_OUT:
      return "Pending Client Timed Out";
    case ZigbeeTcpEvent::PENDING_CLIENT_PROMOTED:
      return "Pending Client Promoted";
    case ZigbeeTcpEvent::BSL_RENDEZVOUS_ARMED:
      return "BSL Rendezvous Armed";
    case ZigbeeTcpEvent::MAINTENANCE_SESSION_STARTED:
      return "Maintenance Session Started";
    case ZigbeeTcpEvent::RADIO_RESET_REQUESTED:
      return "Radio Reset Requested";
    case ZigbeeTcpEvent::NORMAL_SESSION_ENDED:
      return "Normal Session Ended";
    case ZigbeeTcpEvent::MAINTENANCE_SESSION_FINISHED:
      return "Maintenance Session Finished";
    case ZigbeeTcpEvent::BSL_RENDEZVOUS_TIMED_OUT:
      return "BSL Rendezvous Timed Out";
    case ZigbeeTcpEvent::PARKED_CLIENT_DISCONNECTED:
      return "Parked Client Disconnected";
    case ZigbeeTcpEvent::PARKED_CLIENT_TIMED_OUT:
      return "Parked Client Timed Out";
    case ZigbeeTcpEvent::RECOVERY_RESET:
      return "Recovery Reset";
    case ZigbeeTcpEvent::SERVER_SHUTDOWN:
      return "Server Shutdown";
    case ZigbeeTcpEvent::BSL_ENTERED:
      return "BSL Entered";
  }
  return "Unknown";
}

/// Socket-independent transport state and transition policy.
///
/// The TCP server owns the actual sockets and UART side effects. This class is
/// deliberately limited to deterministic role/topology decisions so every
/// supported ordering can be exercised by a small native host test.
class ZigbeeTcpState {
 public:
  ZigbeeTcpActiveState active() const { return this->active_; }
  bool pending() const { return this->pending_; }
  bool parked() const { return this->parked_; }
  bool bsl_armed() const { return this->bsl_armed_; }
  bool bsl_entered() const { return this->bsl_entered_; }
  ZigbeeTcpEvent last_event() const { return this->last_event_; }
  const ZigbeeTcpCounters &counters() const { return this->counters_; }
  uint32_t revision() const { return this->revision_; }

  ZigbeeTcpAcceptAction accept_client() {
    if (this->active_ == ZigbeeTcpActiveState::IDLE) {
      if (this->bsl_armed_) {
        const bool enter_bsl = !this->bsl_entered_;
        this->active_ = ZigbeeTcpActiveState::MAINTENANCE;
        this->bsl_armed_ = false;
        this->bsl_entered_ = true;
        this->record_maintenance_started_();
        return enter_bsl ? ZigbeeTcpAcceptAction::ACTIVATE_MAINTENANCE_AND_ENTER_BSL
                         : ZigbeeTcpAcceptAction::ACTIVATE_MAINTENANCE;
      }
      this->active_ = ZigbeeTcpActiveState::PROVISIONAL;
      this->record_event_(ZigbeeTcpEvent::PROVISIONAL_CLIENT_CONNECTED);
      return ZigbeeTcpAcceptAction::ACTIVATE_PROVISIONAL;
    }

    if ((this->active_ == ZigbeeTcpActiveState::NORMAL ||
         this->active_ == ZigbeeTcpActiveState::PROVISIONAL) &&
        !this->pending_) {
      if (this->bsl_armed_) {
        this->parked_ = true;
        this->active_ = ZigbeeTcpActiveState::MAINTENANCE;
        this->bsl_armed_ = false;
        this->bsl_entered_ = true;
        this->record_maintenance_started_();
        return ZigbeeTcpAcceptAction::TAKE_OVER_WITH_NEW_CLIENT;
      }
      this->pending_ = true;
      this->record_event_(ZigbeeTcpEvent::PENDING_CLIENT_CONNECTED);
      return ZigbeeTcpAcceptAction::HOLD_PENDING;
    }

    this->counters_.rejected_connections++;
    this->record_event_(ZigbeeTcpEvent::CONNECTION_REJECTED);
    return ZigbeeTcpAcceptAction::REJECT;
  }

  bool receive_active_payload() {
    if (this->active_ != ZigbeeTcpActiveState::PROVISIONAL)
      return false;
    this->active_ = ZigbeeTcpActiveState::NORMAL;
    this->record_event_(ZigbeeTcpEvent::NORMAL_SESSION_STARTED);
    return true;
  }

  ZigbeeTcpBslAction request_bsl(bool active_has_received_bytes) {
    if (this->active_ == ZigbeeTcpActiveState::MAINTENANCE) {
      this->bsl_armed_ = false;
      this->bsl_entered_ = true;
      this->record_event_(ZigbeeTcpEvent::BSL_ENTERED);
      return ZigbeeTcpBslAction::APPLY_TO_ACTIVE;
    }
    if (this->pending_) {
      this->pending_ = false;
      this->parked_ = this->active_ != ZigbeeTcpActiveState::IDLE;
      this->active_ = ZigbeeTcpActiveState::MAINTENANCE;
      this->bsl_armed_ = false;
      this->bsl_entered_ = true;
      this->record_maintenance_started_();
      return ZigbeeTcpBslAction::TAKE_OVER_WITH_PENDING;
    }
    if (this->active_ != ZigbeeTcpActiveState::IDLE && !active_has_received_bytes) {
      this->active_ = ZigbeeTcpActiveState::MAINTENANCE;
      this->bsl_armed_ = false;
      this->bsl_entered_ = true;
      this->record_maintenance_started_();
      return ZigbeeTcpBslAction::APPLY_TO_ACTIVE;
    }
    if (this->active_ != ZigbeeTcpActiveState::IDLE) {
      this->bsl_armed_ = true;
      this->record_event_(ZigbeeTcpEvent::BSL_RENDEZVOUS_ARMED);
      return ZigbeeTcpBslAction::ARM_AND_WAIT;
    }

    this->bsl_armed_ = true;
    this->bsl_entered_ = true;
    this->record_event_(ZigbeeTcpEvent::BSL_RENDEZVOUS_ARMED);
    return ZigbeeTcpBslAction::ENTER_BSL_AND_WAIT;
  }

  ZigbeeTcpResetAction request_reset(bool active_has_received_bytes) {
    if (this->pending_) {
      this->pending_ = false;
      this->parked_ = this->active_ != ZigbeeTcpActiveState::IDLE;
      this->active_ = ZigbeeTcpActiveState::MAINTENANCE;
      this->bsl_armed_ = false;
      this->bsl_entered_ = false;
      this->record_maintenance_started_();
      return ZigbeeTcpResetAction::TAKE_OVER_WITH_PENDING;
    }
    if (this->active_ == ZigbeeTcpActiveState::MAINTENANCE ||
        (this->active_ != ZigbeeTcpActiveState::IDLE && !active_has_received_bytes)) {
      const bool begin_maintenance = this->active_ != ZigbeeTcpActiveState::MAINTENANCE;
      this->active_ = ZigbeeTcpActiveState::MAINTENANCE;
      this->bsl_armed_ = false;
      this->bsl_entered_ = false;
      if (begin_maintenance)
        this->record_maintenance_started_();
      else
        this->record_event_(ZigbeeTcpEvent::RADIO_RESET_REQUESTED);
      return ZigbeeTcpResetAction::APPLY_TO_ACTIVE;
    }

    this->bsl_armed_ = false;
    this->bsl_entered_ = false;
    this->record_event_(ZigbeeTcpEvent::RADIO_RESET_REQUESTED);
    return ZigbeeTcpResetAction::RESET_ONLY;
  }

  ZigbeeTcpDisconnectResult disconnect_active() {
    if (this->active_ == ZigbeeTcpActiveState::MAINTENANCE) {
      const bool recover_radio = this->bsl_entered_;
      this->active_ = ZigbeeTcpActiveState::IDLE;
      this->parked_ = false;
      this->bsl_armed_ = false;
      this->bsl_entered_ = false;
      this->record_event_(ZigbeeTcpEvent::MAINTENANCE_SESSION_FINISHED);
      return {ZigbeeTcpDisconnectAction::FINISH_MAINTENANCE, recover_radio};
    }

    if (this->pending_) {
      this->active_ = ZigbeeTcpActiveState::PROVISIONAL;
      this->pending_ = false;
      this->record_event_(ZigbeeTcpEvent::PENDING_CLIENT_PROMOTED);
      return {ZigbeeTcpDisconnectAction::PROMOTE_PENDING, false};
    }

    this->active_ = ZigbeeTcpActiveState::IDLE;
    this->record_event_(ZigbeeTcpEvent::NORMAL_SESSION_ENDED);
    return {ZigbeeTcpDisconnectAction::NORMAL_ENDED, false};
  }

  void disconnect_pending() {
    this->pending_ = false;
    this->record_event_(ZigbeeTcpEvent::PENDING_CLIENT_DISCONNECTED);
  }

  void timeout_pending() {
    this->pending_ = false;
    this->counters_.pending_timeouts++;
    this->record_event_(ZigbeeTcpEvent::PENDING_CLIENT_TIMED_OUT);
  }

  void disconnect_parked() {
    this->parked_ = false;
    this->record_event_(ZigbeeTcpEvent::PARKED_CLIENT_DISCONNECTED);
  }

  void timeout_parked() {
    this->parked_ = false;
    this->record_event_(ZigbeeTcpEvent::PARKED_CLIENT_TIMED_OUT);
  }

  bool expire_bsl_rendezvous() {
    const bool recover_radio = this->bsl_entered_;
    this->bsl_armed_ = false;
    this->bsl_entered_ = false;
    this->record_event_(ZigbeeTcpEvent::BSL_RENDEZVOUS_TIMED_OUT);
    return recover_radio;
  }

  void record_recovery_reset() {
    this->counters_.recovery_resets++;
    this->record_event_(ZigbeeTcpEvent::RECOVERY_RESET);
  }

  void shutdown() {
    this->active_ = ZigbeeTcpActiveState::IDLE;
    this->pending_ = false;
    this->parked_ = false;
    this->bsl_armed_ = false;
    this->bsl_entered_ = false;
    this->record_event_(ZigbeeTcpEvent::SERVER_SHUTDOWN);
  }

  bool valid() const {
    if (this->pending_ &&
        this->active_ != ZigbeeTcpActiveState::PROVISIONAL &&
        this->active_ != ZigbeeTcpActiveState::NORMAL)
      return false;
    if (this->parked_ && this->active_ != ZigbeeTcpActiveState::MAINTENANCE)
      return false;
    if (this->bsl_armed_ && this->active_ == ZigbeeTcpActiveState::MAINTENANCE)
      return false;
    return true;
  }

 protected:
  void record_event_(ZigbeeTcpEvent event) {
    this->last_event_ = event;
    this->revision_++;
  }

  void record_maintenance_started_() {
    this->counters_.maintenance_sessions++;
    this->record_event_(ZigbeeTcpEvent::MAINTENANCE_SESSION_STARTED);
  }

  ZigbeeTcpActiveState active_{ZigbeeTcpActiveState::IDLE};
  bool pending_{false};
  bool parked_{false};
  bool bsl_armed_{false};
  bool bsl_entered_{false};
  ZigbeeTcpEvent last_event_{ZigbeeTcpEvent::INITIALIZED};
  ZigbeeTcpCounters counters_{};
  uint32_t revision_{0};
};

}  // namespace zigbee_gateway
}  // namespace esphome
