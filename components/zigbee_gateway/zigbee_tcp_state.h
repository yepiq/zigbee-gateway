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

struct ZigbeeTcpDisconnectResult {
  ZigbeeTcpDisconnectAction action;
  bool recover_radio;
};

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

  ZigbeeTcpAcceptAction accept_client() {
    if (this->active_ == ZigbeeTcpActiveState::IDLE) {
      if (this->bsl_armed_) {
        const bool enter_bsl = !this->bsl_entered_;
        this->active_ = ZigbeeTcpActiveState::MAINTENANCE;
        this->bsl_armed_ = false;
        this->bsl_entered_ = true;
        return enter_bsl ? ZigbeeTcpAcceptAction::ACTIVATE_MAINTENANCE_AND_ENTER_BSL
                         : ZigbeeTcpAcceptAction::ACTIVATE_MAINTENANCE;
      }
      this->active_ = ZigbeeTcpActiveState::PROVISIONAL;
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
        return ZigbeeTcpAcceptAction::TAKE_OVER_WITH_NEW_CLIENT;
      }
      this->pending_ = true;
      return ZigbeeTcpAcceptAction::HOLD_PENDING;
    }

    return ZigbeeTcpAcceptAction::REJECT;
  }

  bool receive_active_payload() {
    if (this->active_ != ZigbeeTcpActiveState::PROVISIONAL)
      return false;
    this->active_ = ZigbeeTcpActiveState::NORMAL;
    return true;
  }

  ZigbeeTcpBslAction request_bsl(bool active_has_received_bytes) {
    if (this->active_ == ZigbeeTcpActiveState::MAINTENANCE) {
      this->bsl_armed_ = false;
      this->bsl_entered_ = true;
      return ZigbeeTcpBslAction::APPLY_TO_ACTIVE;
    }
    if (this->pending_) {
      this->pending_ = false;
      this->parked_ = this->active_ != ZigbeeTcpActiveState::IDLE;
      this->active_ = ZigbeeTcpActiveState::MAINTENANCE;
      this->bsl_armed_ = false;
      this->bsl_entered_ = true;
      return ZigbeeTcpBslAction::TAKE_OVER_WITH_PENDING;
    }
    if (this->active_ != ZigbeeTcpActiveState::IDLE && !active_has_received_bytes) {
      this->active_ = ZigbeeTcpActiveState::MAINTENANCE;
      this->bsl_armed_ = false;
      this->bsl_entered_ = true;
      return ZigbeeTcpBslAction::APPLY_TO_ACTIVE;
    }
    if (this->active_ != ZigbeeTcpActiveState::IDLE) {
      this->bsl_armed_ = true;
      return ZigbeeTcpBslAction::ARM_AND_WAIT;
    }

    this->bsl_armed_ = true;
    this->bsl_entered_ = true;
    return ZigbeeTcpBslAction::ENTER_BSL_AND_WAIT;
  }

  ZigbeeTcpResetAction request_reset(bool active_has_received_bytes) {
    if (this->pending_) {
      this->pending_ = false;
      this->parked_ = this->active_ != ZigbeeTcpActiveState::IDLE;
      this->active_ = ZigbeeTcpActiveState::MAINTENANCE;
      this->bsl_armed_ = false;
      this->bsl_entered_ = false;
      return ZigbeeTcpResetAction::TAKE_OVER_WITH_PENDING;
    }
    if (this->active_ == ZigbeeTcpActiveState::MAINTENANCE ||
        (this->active_ != ZigbeeTcpActiveState::IDLE && !active_has_received_bytes)) {
      this->active_ = ZigbeeTcpActiveState::MAINTENANCE;
      this->bsl_armed_ = false;
      this->bsl_entered_ = false;
      return ZigbeeTcpResetAction::APPLY_TO_ACTIVE;
    }

    this->bsl_armed_ = false;
    this->bsl_entered_ = false;
    return ZigbeeTcpResetAction::RESET_ONLY;
  }

  ZigbeeTcpDisconnectResult disconnect_active() {
    if (this->active_ == ZigbeeTcpActiveState::MAINTENANCE) {
      const bool recover_radio = this->bsl_entered_;
      this->active_ = ZigbeeTcpActiveState::IDLE;
      this->parked_ = false;
      this->bsl_armed_ = false;
      this->bsl_entered_ = false;
      return {ZigbeeTcpDisconnectAction::FINISH_MAINTENANCE, recover_radio};
    }

    if (this->pending_) {
      this->active_ = ZigbeeTcpActiveState::PROVISIONAL;
      this->pending_ = false;
      return {ZigbeeTcpDisconnectAction::PROMOTE_PENDING, false};
    }

    this->active_ = ZigbeeTcpActiveState::IDLE;
    return {ZigbeeTcpDisconnectAction::NORMAL_ENDED, false};
  }

  void close_pending() { this->pending_ = false; }
  void close_parked() { this->parked_ = false; }

  bool expire_bsl_rendezvous() {
    const bool recover_radio = this->bsl_entered_;
    this->bsl_armed_ = false;
    this->bsl_entered_ = false;
    return recover_radio;
  }

  void shutdown() {
    this->active_ = ZigbeeTcpActiveState::IDLE;
    this->pending_ = false;
    this->parked_ = false;
    this->bsl_armed_ = false;
    this->bsl_entered_ = false;
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
  ZigbeeTcpActiveState active_{ZigbeeTcpActiveState::IDLE};
  bool pending_{false};
  bool parked_{false};
  bool bsl_armed_{false};
  bool bsl_entered_{false};
};

}  // namespace zigbee_gateway
}  // namespace esphome
