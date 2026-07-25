#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

#include "esphome/components/binary_sensor/binary_sensor.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/socket/socket.h"
#include "esphome/components/text_sensor/text_sensor.h"

#include "zigbee_serial.h"
#include "zigbee_tcp_state.h"

namespace esphome {
namespace zigbee_gateway {

class ZigbeeGatewayComponent;

/// Exclusive-owner TCP bridge for the Zigbee radio.
///
/// The bridge deliberately has fixed connection roles rather than a
/// multi-client broadcast model:
///
/// - one active client may own the UART;
/// - one first-arriving candidate may wait for /cmdZigBSL or /cmdZigRST;
/// - during maintenance the previous normal client may remain TCP-connected
///   but quarantined, with all of its traffic drained and discarded.
///
/// This preserves a running Zigbee2MQTT process during a several-minute flash
/// while ensuring that only the maintenance client can reach the UART.
class ZigbeeTcpServer {
 public:
  void set_parent(ZigbeeGatewayComponent *parent) { this->parent_ = parent; }
  void set_serial(ZigbeeSerialInterface *serial) { this->serial_ = serial; }
  void set_port(uint16_t port) { this->port_ = port; }
  void set_pending_timeout(uint32_t timeout_ms) { this->pending_timeout_ms_ = timeout_ms; }
  void set_park_timeout(uint32_t timeout_ms) { this->park_timeout_ms_ = timeout_ms; }
  void set_connected_sensor(binary_sensor::BinarySensor *sensor) { this->connected_sensor_ = sensor; }
  void set_connection_count_sensor(sensor::Sensor *sensor) { this->connection_count_sensor_ = sensor; }
  void set_transport_state_sensor(text_sensor::TextSensor *sensor) { this->transport_state_sensor_ = sensor; }
  void set_pending_socket_sensor(binary_sensor::BinarySensor *sensor) { this->pending_socket_sensor_ = sensor; }
  void set_parked_socket_sensor(binary_sensor::BinarySensor *sensor) { this->parked_socket_sensor_ = sensor; }
  void set_last_event_sensor(text_sensor::TextSensor *sensor) { this->last_event_sensor_ = sensor; }
  void set_rejected_connections_sensor(sensor::Sensor *sensor) { this->rejected_connections_sensor_ = sensor; }
  void set_pending_timeouts_sensor(sensor::Sensor *sensor) { this->pending_timeouts_sensor_ = sensor; }
  void set_maintenance_sessions_sensor(sensor::Sensor *sensor) { this->maintenance_sessions_sensor_ = sensor; }
  void set_recovery_resets_sensor(sensor::Sensor *sensor) { this->recovery_resets_sensor_ = sensor; }

  bool start();
  void loop();
  void shutdown();

  void request_bsl();
  void request_reset();

  bool has_any_client() const;
  bool is_started() const { return this->started_; }
  bool maintenance_active() const;
  size_t connection_count() const;

 protected:
  enum class MaintenanceCommand : uint8_t {
    BSL = 0,
    RESET = 1,
  };

  static constexpr size_t PREBUFFER_SIZE = 512;
  static constexpr size_t UART_BUFFER_SIZE = 1024;
  static constexpr size_t IO_CHUNK_SIZE = 256;
  static constexpr size_t LOOP_IO_BUDGET = 1024;

  struct Client {
    std::unique_ptr<socket::Socket> socket{};
    std::string identifier{};
    uint32_t connected_at{0};
    uint32_t bytes_received{0};
    uint32_t bytes_discarded{0};
    std::array<uint8_t, PREBUFFER_SIZE> prebuffer{};
    size_t prebuffer_length{0};

    bool connected() const { return this->socket != nullptr; }
  };

  void accept_clients_();
  Client make_client_(std::unique_ptr<socket::Socket> socket, const struct sockaddr *address,
                      socklen_t address_length);
  bool configure_client_(socket::Socket *socket);
  void reject_client_(Client &client, const char *reason);
  void close_client_(Client &client, bool abortive);

  bool collect_prebuffer_(Client &client);
  void classify_active_();
  void pump_active_();
  void pump_tcp_to_uart_();
  void pump_uart_to_tcp_();
  void drain_parked_();

  void start_bsl_rendezvous_timer_();
  void begin_maintenance_with_active_(MaintenanceCommand command);
  void begin_maintenance_with_pending_(MaintenanceCommand command);
  void apply_maintenance_command_(MaintenanceCommand command);
  void handle_active_disconnect_();
  void promote_pending_after_normal_disconnect_();
  void finish_maintenance_(bool recover_radio);
  void clear_uart_output_();
  void publish_sensors_();

  ZigbeeGatewayComponent *parent_{nullptr};
  ZigbeeSerialInterface *serial_{nullptr};
  uint16_t port_{6638};
  uint32_t pending_timeout_ms_{30000};
  uint32_t park_timeout_ms_{600000};
  uint32_t last_start_attempt_ms_{0};
  bool started_{false};

  std::unique_ptr<socket::ListenSocket> server_{};
  Client active_{};
  Client pending_{};
  Client parked_{};

  ZigbeeTcpState state_{};
  uint32_t bsl_armed_at_{0};
  uint32_t parked_at_{0};

  std::array<uint8_t, UART_BUFFER_SIZE> uart_buffer_{};
  size_t uart_buffer_offset_{0};
  size_t uart_buffer_length_{0};

  binary_sensor::BinarySensor *connected_sensor_{nullptr};
  sensor::Sensor *connection_count_sensor_{nullptr};
  text_sensor::TextSensor *transport_state_sensor_{nullptr};
  binary_sensor::BinarySensor *pending_socket_sensor_{nullptr};
  binary_sensor::BinarySensor *parked_socket_sensor_{nullptr};
  text_sensor::TextSensor *last_event_sensor_{nullptr};
  sensor::Sensor *rejected_connections_sensor_{nullptr};
  sensor::Sensor *pending_timeouts_sensor_{nullptr};
  sensor::Sensor *maintenance_sessions_sensor_{nullptr};
  sensor::Sensor *recovery_resets_sensor_{nullptr};
  size_t last_published_connection_count_{static_cast<size_t>(-1)};
  uint32_t last_published_state_revision_{static_cast<uint32_t>(-1)};
};

}  // namespace zigbee_gateway
}  // namespace esphome
