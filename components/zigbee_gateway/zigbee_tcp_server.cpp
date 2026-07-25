#include "zigbee_tcp_server.h"

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <utility>

#include "esphome/components/network/util.h"
#include "esphome/core/application.h"
#include "esphome/core/log.h"

#include "zigbee_gateway.h"

namespace esphome {
namespace zigbee_gateway {

static const char *const TCP_TAG = "zigbee_gateway.tcp";

bool ZigbeeTcpServer::start() {
  if (this->started_)
    return true;
  if (!network::is_connected())
    return false;

  const uint32_t now = millis();
  if (this->last_start_attempt_ms_ != 0 && now - this->last_start_attempt_ms_ < 5000)
    return false;
  this->last_start_attempt_ms_ = now;

  this->server_ = socket::socket_ip_loop_monitored(SOCK_STREAM, 0);
  if (this->server_ == nullptr) {
    ESP_LOGE(TCP_TAG, "Failed to create TCP listener");
    return false;
  }

  int enabled = 1;
  if (this->server_->setsockopt(SOL_SOCKET, SO_REUSEADDR, &enabled, sizeof(enabled)) != 0)
    ESP_LOGW(TCP_TAG, "Failed to enable SO_REUSEADDR: errno=%d", errno);
  if (this->server_->setblocking(false) != 0) {
    ESP_LOGE(TCP_TAG, "Failed to make listener non-blocking: errno=%d", errno);
    this->server_.reset();
    return false;
  }

  struct sockaddr_storage bind_address {};
  const socklen_t bind_length =
      socket::set_sockaddr_any(reinterpret_cast<struct sockaddr *>(&bind_address), sizeof(bind_address), this->port_);
  if (bind_length == 0 ||
      this->server_->bind(reinterpret_cast<struct sockaddr *>(&bind_address), bind_length) != 0) {
    ESP_LOGE(TCP_TAG, "Failed to bind TCP port %u: errno=%d", static_cast<unsigned>(this->port_), errno);
    this->server_.reset();
    return false;
  }
  if (this->server_->listen(2) != 0) {
    ESP_LOGE(TCP_TAG, "Failed to listen on TCP port %u: errno=%d", static_cast<unsigned>(this->port_), errno);
    this->server_.reset();
    return false;
  }

  this->started_ = true;
  ESP_LOGI(TCP_TAG, "Listening on TCP port %u", static_cast<unsigned>(this->port_));
  this->publish_sensors_();
  return true;
}

void ZigbeeTcpServer::loop() {
  if (!this->started_)
    return;

  // Local chip identification, NV inspection, and explicit local operations
  // have priority over the transparent transport. Leave new connections in the
  // listener backlog and do not touch existing sockets until LOCAL releases the
  // UART; this prevents classification from stealing ownership mid-operation.
  if (this->serial_ != nullptr && this->serial_->owner() == ZigbeeSerialInterface::Owner::LOCAL)
    return;

  this->accept_clients_();

  if (this->pending_.connected() && !this->collect_prebuffer_(this->pending_)) {
    ESP_LOGD(TCP_TAG, "Pending client %s disconnected", this->pending_.identifier.c_str());
    this->close_client_(this->pending_, false);
    this->state_.disconnect_pending();
  }

  if (this->active_.connected() && this->state_.active() == ZigbeeTcpActiveState::PROVISIONAL) {
    if (!this->collect_prebuffer_(this->active_)) {
      this->handle_active_disconnect_();
    } else {
      this->classify_active_();
    }
  }

  if (this->active_.connected() &&
      (this->state_.active() == ZigbeeTcpActiveState::NORMAL ||
       this->state_.active() == ZigbeeTcpActiveState::MAINTENANCE))
    this->pump_active_();

  this->drain_parked_();

  const uint32_t now = millis();
  if (this->pending_.connected() && now - this->pending_.connected_at >= this->pending_timeout_ms_) {
    ESP_LOGW(TCP_TAG, "Pending client %s timed out after %u ms", this->pending_.identifier.c_str(),
             static_cast<unsigned>(this->pending_timeout_ms_));
    this->close_client_(this->pending_, true);
    this->state_.timeout_pending();
  }
  if (this->state_.bsl_armed() && now - this->bsl_armed_at_ >= this->pending_timeout_ms_) {
    ESP_LOGW(TCP_TAG, "BSL rendezvous timed out after %u ms", static_cast<unsigned>(this->pending_timeout_ms_));
    const bool radio_was_in_bsl = this->state_.expire_bsl_rendezvous();
    if (radio_was_in_bsl && this->parent_ != nullptr) {
      this->parent_->reset_for_remote_();
      this->state_.record_recovery_reset();
    }
    if (radio_was_in_bsl && this->parent_ != nullptr)
      this->parent_->on_tcp_maintenance_finished_();
  }
  if (this->parked_.connected() && now - this->parked_at_ >= this->park_timeout_ms_) {
    ESP_LOGW(TCP_TAG, "Parked client %s reached the %u ms safety limit; closing it",
             this->parked_.identifier.c_str(), static_cast<unsigned>(this->park_timeout_ms_));
    this->close_client_(this->parked_, true);
    this->state_.timeout_parked();
  }

  this->publish_sensors_();
}

void ZigbeeTcpServer::shutdown() {
  // The socket may already have observed EOF while the state machine still
  // owns the logical normal session. Use that authoritative state so a clean
  // shutdown still gets one best-effort LED-off transaction.
  const bool normal_was_active = this->state_.active() == ZigbeeTcpActiveState::NORMAL;
  this->close_client_(this->pending_, false);
  this->close_client_(this->active_, false);
  this->close_client_(this->parked_, false);
  this->server_.reset();
  this->clear_stream_buffers_();
  this->started_ = false;
  this->state_.shutdown();
  if (this->serial_ != nullptr)
    this->serial_->set_owner(ZigbeeSerialInterface::Owner::NONE);
  if (normal_was_active && this->parent_ != nullptr)
    this->parent_->on_tcp_normal_session_finished_();
  this->publish_sensors_();
}

bool ZigbeeTcpServer::has_any_client() const {
  return this->active_.connected() || this->pending_.connected() || this->parked_.connected();
}

bool ZigbeeTcpServer::maintenance_active() const {
  return this->active_.connected() && this->state_.active() == ZigbeeTcpActiveState::MAINTENANCE;
}

size_t ZigbeeTcpServer::connection_count() const {
  return static_cast<size_t>(this->active_.connected()) + static_cast<size_t>(this->pending_.connected()) +
         static_cast<size_t>(this->parked_.connected());
}

void ZigbeeTcpServer::accept_clients_() {
  for (uint8_t accepted = 0; accepted < 4; accepted++) {
    struct sockaddr_storage address {};
    socklen_t address_length = sizeof(address);
    auto socket =
        this->server_->accept(reinterpret_cast<struct sockaddr *>(&address), &address_length);
    if (socket == nullptr)
      return;

    Client client = this->make_client_(std::move(socket), reinterpret_cast<struct sockaddr *>(&address),
                                       address_length);
    if (!client.connected())
      continue;

    switch (this->state_.accept_client()) {
      case ZigbeeTcpAcceptAction::ACTIVATE_PROVISIONAL:
        this->active_ = std::move(client);
        ESP_LOGI(TCP_TAG, "Provisional client connected from %s", this->active_.identifier.c_str());
        break;
      case ZigbeeTcpAcceptAction::ACTIVATE_MAINTENANCE:
        this->active_ = std::move(client);
        this->serial_->set_owner(ZigbeeSerialInterface::Owner::TCP_MAINTENANCE);
        this->serial_->drain(ZigbeeSerialInterface::Owner::TCP_MAINTENANCE);
        ESP_LOGI(TCP_TAG, "Maintenance client %s connected after BSL command",
                 this->active_.identifier.c_str());
        break;
      case ZigbeeTcpAcceptAction::ACTIVATE_MAINTENANCE_AND_ENTER_BSL:
        this->active_ = std::move(client);
        this->serial_->set_owner(ZigbeeSerialInterface::Owner::TCP_MAINTENANCE);
        this->serial_->drain(ZigbeeSerialInterface::Owner::TCP_MAINTENANCE);
        if (this->parent_ != nullptr)
          this->parent_->enter_bsl_for_remote_();
        ESP_LOGI(TCP_TAG, "Maintenance client %s connected after the normal owner left; entered BSL now",
                 this->active_.identifier.c_str());
        break;
      case ZigbeeTcpAcceptAction::HOLD_PENDING:
        this->pending_ = std::move(client);
        ESP_LOGI(TCP_TAG, "Holding first pending client from %s for up to %u ms",
                 this->pending_.identifier.c_str(), static_cast<unsigned>(this->pending_timeout_ms_));
        break;
      case ZigbeeTcpAcceptAction::TAKE_OVER_WITH_NEW_CLIENT:
        this->pending_ = std::move(client);
        this->begin_maintenance_with_pending_(MaintenanceCommand::BSL);
        break;
      case ZigbeeTcpAcceptAction::REJECT:
        this->reject_client_(client, "another active or pending client already exists");
        break;
    }
  }
}

ZigbeeTcpServer::Client ZigbeeTcpServer::make_client_(std::unique_ptr<socket::Socket> socket,
                                                       const struct sockaddr *address,
                                                       socklen_t address_length) {
  Client client;
  if (!this->configure_client_(socket.get())) {
    socket.reset();
    return client;
  }

  std::array<char, socket::SOCKADDR_STR_LEN> identifier{};
  socket::format_sockaddr_to(address, address_length, identifier);
  client.identifier = identifier.data();
  client.socket = std::move(socket);
  client.connected_at = millis();
  return client;
}

bool ZigbeeTcpServer::configure_client_(socket::Socket *socket) {
  int enabled = 1;
  if (socket->setblocking(false) != 0) {
    ESP_LOGW(TCP_TAG, "Failed to make client non-blocking: errno=%d", errno);
    return false;
  }
  if (socket->setsockopt(IPPROTO_TCP, TCP_NODELAY, &enabled, sizeof(enabled)) != 0)
    ESP_LOGW(TCP_TAG, "Failed to enable TCP_NODELAY: errno=%d", errno);
  if (socket->setsockopt(SOL_SOCKET, SO_KEEPALIVE, &enabled, sizeof(enabled)) != 0)
    ESP_LOGW(TCP_TAG, "Failed to enable SO_KEEPALIVE: errno=%d", errno);
  return true;
}

void ZigbeeTcpServer::reject_client_(Client &client, const char *reason) {
  ESP_LOGW(TCP_TAG, "Rejecting client %s: %s", client.identifier.c_str(), reason);
  this->close_client_(client, true);
}

void ZigbeeTcpServer::close_client_(Client &client, bool abortive) {
  if (client.socket != nullptr && abortive) {
    struct linger linger_option {
      1, 0
    };
    client.socket->setsockopt(SOL_SOCKET, SO_LINGER, &linger_option, sizeof(linger_option));
  }
  client.socket.reset();
  client = Client{};
}

bool ZigbeeTcpServer::collect_prebuffer_(Client &client) {
  uint8_t chunk[IO_CHUNK_SIZE];
  size_t budget = LOOP_IO_BUDGET;
  while (budget > 0) {
    const size_t free = client.prebuffer.size() - client.prebuffer_length;
    if (free == 0) {
      ESP_LOGW(TCP_TAG, "Client %s sent more than %u bytes before obtaining UART ownership",
               client.identifier.c_str(), static_cast<unsigned>(client.prebuffer.size()));
      return false;
    }
    const size_t requested = std::min({sizeof(chunk), free, budget});
    const ssize_t count = client.socket->read(chunk, requested);
    if (count > 0) {
      std::copy_n(chunk, static_cast<size_t>(count), client.prebuffer.begin() + client.prebuffer_length);
      client.prebuffer_length += static_cast<size_t>(count);
      client.bytes_received += static_cast<uint32_t>(count);
      budget -= static_cast<size_t>(count);
      continue;
    }
    if (count == 0)
      return false;
    if (errno == EWOULDBLOCK || errno == EAGAIN)
      return true;
    ESP_LOGW(TCP_TAG, "Read failed for client %s: errno=%d", client.identifier.c_str(), errno);
    return false;
  }
  return true;
}

void ZigbeeTcpServer::classify_active_() {
  if (!this->active_.connected() || this->state_.active() != ZigbeeTcpActiveState::PROVISIONAL ||
      this->active_.prebuffer_length == 0)
    return;

  // Zigbee2MQTT writes its ZNP ping (or the legacy 0xEF bootloader-skip byte)
  // immediately after connecting. A connection-first flashing tool remains
  // silent until it has sent /cmdZigBSL, so any pre-command TCP payload makes
  // this the normal transparent owner.
  if (!this->state_.receive_active_payload())
    return;

  // Keep the client's first bytes quarantined while the gateway performs its
  // local LED1 transaction. Only after that transaction and its SRSP have
  // cleared the UART may the normal client receive transparent ownership.
  if (this->parent_ != nullptr)
    this->parent_->on_tcp_normal_session_started_();
  this->serial_->set_owner(ZigbeeSerialInterface::Owner::TCP_NORMAL);
  this->serial_->drain(ZigbeeSerialInterface::Owner::TCP_NORMAL);
  ESP_LOGI(TCP_TAG, "Client %s is the normal UART owner", this->active_.identifier.c_str());
}

void ZigbeeTcpServer::pump_active_() {
  if (this->active_.prebuffer_length > 0) {
    if (!this->stream_pump_.queue_left_to_right(
            this->active_.prebuffer.data(), this->active_.prebuffer_length)) {
      ESP_LOGE(TCP_TAG, "Could not queue the active client's quarantined bytes");
      this->active_.socket.reset();
      this->handle_active_disconnect_();
      return;
    }
    this->active_.prebuffer_length = 0;
  }

  const auto owner =
      this->state_.active() == ZigbeeTcpActiveState::MAINTENANCE
          ? ZigbeeSerialInterface::Owner::TCP_MAINTENANCE
          : ZigbeeSerialInterface::Owner::TCP_NORMAL;
  this->socket_endpoint_.set_socket(this->active_.socket.get());
  this->radio_endpoint_.set_owner(owner);
  this->stream_pump_.set_left(&this->socket_endpoint_);
  this->stream_pump_.set_right(&this->radio_endpoint_);

  const auto result = this->stream_pump_.pump();
  if (result == ZigbeeStreamPumpResult::ACTIVE)
    return;

  if (result == ZigbeeStreamPumpResult::LEFT_ERROR ||
      result == ZigbeeStreamPumpResult::RIGHT_ERROR) {
    ESP_LOGW(TCP_TAG, "Stream pump failed for active client %s: result=%u errno=%d",
             this->active_.identifier.c_str(), static_cast<unsigned>(result),
             this->socket_endpoint_.last_error());
  }
  this->active_.socket.reset();
  this->handle_active_disconnect_();
}

void ZigbeeTcpServer::drain_parked_() {
  if (!this->parked_.connected())
    return;

  uint8_t discarded[IO_CHUNK_SIZE];
  size_t budget = LOOP_IO_BUDGET;
  while (budget > 0) {
    const size_t requested = std::min(sizeof(discarded), budget);
    const ssize_t count = this->parked_.socket->read(discarded, requested);
    if (count > 0) {
      this->parked_.bytes_discarded += static_cast<uint32_t>(count);
      budget -= static_cast<size_t>(count);
      continue;
    }
    if (count == 0) {
      ESP_LOGI(TCP_TAG, "Parked client %s disconnected after %u discarded bytes",
               this->parked_.identifier.c_str(), static_cast<unsigned>(this->parked_.bytes_discarded));
      this->close_client_(this->parked_, false);
      this->state_.disconnect_parked();
      return;
    }
    if (errno == EWOULDBLOCK || errno == EAGAIN)
      return;
    ESP_LOGW(TCP_TAG, "Read failed for parked client %s: errno=%d", this->parked_.identifier.c_str(), errno);
    this->close_client_(this->parked_, false);
    this->state_.disconnect_parked();
    return;
  }
}

void ZigbeeTcpServer::request_bsl() {
  if (!this->started_) {
    ESP_LOGW(TCP_TAG, "BSL command received before TCP server startup");
    return;
  }

  const bool already_maintenance = this->maintenance_active();
  const auto action = this->state_.request_bsl(this->active_.bytes_received != 0);
  switch (action) {
    case ZigbeeTcpBslAction::APPLY_TO_ACTIVE:
      if (already_maintenance)
        this->apply_maintenance_command_(MaintenanceCommand::BSL);
      else
        this->begin_maintenance_with_active_(MaintenanceCommand::BSL);
      break;
    case ZigbeeTcpBslAction::TAKE_OVER_WITH_PENDING:
      this->begin_maintenance_with_pending_(MaintenanceCommand::BSL);
      break;
    case ZigbeeTcpBslAction::ARM_AND_WAIT:
      this->start_bsl_rendezvous_timer_();
      break;
    case ZigbeeTcpBslAction::ENTER_BSL_AND_WAIT:
      // Command-first tools send /cmdZigBSL, wait, and only then establish
      // TCP. With no normal client to preserve, enter BSL immediately.
      this->serial_->set_owner(ZigbeeSerialInterface::Owner::NONE);
      if (this->parent_ != nullptr)
        this->parent_->enter_bsl_for_remote_();
      this->start_bsl_rendezvous_timer_();
      break;
  }
}

void ZigbeeTcpServer::request_reset() {
  if (!this->started_) {
    if (this->parent_ != nullptr)
      this->parent_->reset_for_remote_();
    return;
  }

  const bool already_maintenance = this->maintenance_active();
  const auto action = this->state_.request_reset(this->active_.bytes_received != 0);
  switch (action) {
    case ZigbeeTcpResetAction::APPLY_TO_ACTIVE:
      if (already_maintenance)
        this->apply_maintenance_command_(MaintenanceCommand::RESET);
      else
        this->begin_maintenance_with_active_(MaintenanceCommand::RESET);
      break;
    case ZigbeeTcpResetAction::TAKE_OVER_WITH_PENDING:
      this->begin_maintenance_with_pending_(MaintenanceCommand::RESET);
      break;
    case ZigbeeTcpResetAction::RESET_ONLY:
      if (this->parent_ != nullptr)
        this->parent_->reset_for_remote_();
      break;
  }
}

void ZigbeeTcpServer::start_bsl_rendezvous_timer_() {
  this->bsl_armed_at_ = millis();
  ESP_LOGI(TCP_TAG, "BSL rendezvous armed for %u ms; the normal client remains active",
           static_cast<unsigned>(this->pending_timeout_ms_));
}

void ZigbeeTcpServer::begin_maintenance_with_active_(MaintenanceCommand command) {
  if (!this->active_.connected())
    return;
  this->clear_stream_buffers_();
  this->serial_->set_owner(ZigbeeSerialInterface::Owner::TCP_MAINTENANCE);
  this->serial_->drain(ZigbeeSerialInterface::Owner::TCP_MAINTENANCE);
  ESP_LOGI(TCP_TAG, "Client %s became the maintenance UART owner", this->active_.identifier.c_str());
  this->apply_maintenance_command_(command);
}

void ZigbeeTcpServer::begin_maintenance_with_pending_(MaintenanceCommand command) {
  if (!this->pending_.connected())
    return;

  this->clear_stream_buffers_();
  if (this->active_.connected()) {
    this->parked_ = std::move(this->active_);
    this->active_ = Client{};
    this->parked_at_ = millis();
    ESP_LOGI(TCP_TAG, "Parked normal client %s; its TCP traffic will be discarded",
             this->parked_.identifier.c_str());
  }

  this->active_ = std::move(this->pending_);
  this->pending_ = Client{};
  this->serial_->set_owner(ZigbeeSerialInterface::Owner::TCP_MAINTENANCE);
  this->serial_->drain(ZigbeeSerialInterface::Owner::TCP_MAINTENANCE);
  ESP_LOGI(TCP_TAG, "Client %s became the maintenance UART owner", this->active_.identifier.c_str());
  this->apply_maintenance_command_(command);
}

void ZigbeeTcpServer::apply_maintenance_command_(MaintenanceCommand command) {
  if (this->parent_ == nullptr)
    return;
  if (command == MaintenanceCommand::BSL) {
    this->parent_->enter_bsl_for_remote_();
  } else {
    this->parent_->reset_for_remote_();
  }
}

void ZigbeeTcpServer::handle_active_disconnect_() {
  const bool was_normal = this->state_.active() == ZigbeeTcpActiveState::NORMAL;
  const auto result = this->state_.disconnect_active();
  const bool was_maintenance = result.action == ZigbeeTcpDisconnectAction::FINISH_MAINTENANCE;
  const std::string identifier = this->active_.identifier;
  this->close_client_(this->active_, false);
  this->clear_stream_buffers_();
  this->serial_->set_owner(ZigbeeSerialInterface::Owner::NONE);
  ESP_LOGI(TCP_TAG, "%s client %s disconnected", was_maintenance ? "Maintenance" : "Normal",
           identifier.c_str());

  if (was_normal && this->parent_ != nullptr)
    this->parent_->on_tcp_normal_session_finished_();

  if (was_maintenance) {
    this->finish_maintenance_(result.recover_radio);
  } else if (result.action == ZigbeeTcpDisconnectAction::PROMOTE_PENDING) {
    this->promote_pending_after_normal_disconnect_();
  }
}

void ZigbeeTcpServer::promote_pending_after_normal_disconnect_() {
  if (!this->pending_.connected())
    return;
  this->active_ = std::move(this->pending_);
  this->pending_ = Client{};
  ESP_LOGI(TCP_TAG, "Pending client %s became provisional after the normal owner disconnected",
           this->active_.identifier.c_str());
}

void ZigbeeTcpServer::finish_maintenance_(bool recover_radio) {
  // A successful newer XZG-MT session normally issues /cmdZigRST before it
  // disconnects. If a client instead exits while the radio may still be in ROM
  // BSL (including an aborted flash), perform one recovery reset here. An
  // already-running image tolerates the same reset after legacy tools.
  if (recover_radio && this->parent_ != nullptr) {
    this->parent_->reset_for_remote_();
    this->state_.record_recovery_reset();
  }
  if (this->parked_.connected()) {
    ESP_LOGI(TCP_TAG, "Closing parked client %s so it can restart against the post-maintenance radio",
             this->parked_.identifier.c_str());
    this->close_client_(this->parked_, true);
  }
  if (this->parent_ != nullptr)
    this->parent_->on_tcp_maintenance_finished_();
}

void ZigbeeTcpServer::clear_stream_buffers_() {
  this->stream_pump_.reset();
}

void ZigbeeTcpServer::publish_sensors_() {
  const size_t count = this->connection_count();
  if (count != this->last_published_connection_count_) {
    this->last_published_connection_count_ = count;
    if (this->connected_sensor_ != nullptr)
      this->connected_sensor_->publish_state(count > 0);
    if (this->connection_count_sensor_ != nullptr)
      this->connection_count_sensor_->publish_state(static_cast<float>(count));
  }

  if (this->state_.revision() == this->last_published_state_revision_)
    return;
  this->last_published_state_revision_ = this->state_.revision();
  if (this->transport_state_sensor_ != nullptr)
    this->transport_state_sensor_->publish_state(zigbee_tcp_active_state_name(this->state_.active()));
  if (this->pending_socket_sensor_ != nullptr)
    this->pending_socket_sensor_->publish_state(this->state_.pending());
  if (this->parked_socket_sensor_ != nullptr)
    this->parked_socket_sensor_->publish_state(this->state_.parked());
  if (this->last_event_sensor_ != nullptr)
    this->last_event_sensor_->publish_state(zigbee_tcp_event_name(this->state_.last_event()));

  const auto &counters = this->state_.counters();
  if (this->rejected_connections_sensor_ != nullptr)
    this->rejected_connections_sensor_->publish_state(static_cast<float>(counters.rejected_connections));
  if (this->pending_timeouts_sensor_ != nullptr)
    this->pending_timeouts_sensor_->publish_state(static_cast<float>(counters.pending_timeouts));
  if (this->maintenance_sessions_sensor_ != nullptr)
    this->maintenance_sessions_sensor_->publish_state(static_cast<float>(counters.maintenance_sessions));
  if (this->recovery_resets_sensor_ != nullptr)
    this->recovery_resets_sensor_->publish_state(static_cast<float>(counters.recovery_resets));
}

}  // namespace zigbee_gateway
}  // namespace esphome
