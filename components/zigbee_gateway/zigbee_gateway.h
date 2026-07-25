#pragma once

#include <array>
#include <cstdint>
#include <string>

#include "esphome/core/component.h"
#include "esphome/core/hal.h"
#include "esphome/components/binary_sensor/binary_sensor.h"
#include "esphome/components/button/button.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/text_sensor/text_sensor.h"
#include "esphome/components/uart/uart.h"
#include "esphome/core/preferences.h"

#include "protocol.h"
#include "zigbee_metadata_cache.h"
#include "zigbee_tcp_server.h"
#include "zigbee_znp_observer.h"

namespace esphome {
namespace zigbee_gateway {

class ZigbeeGatewayComponent : public Component, public uart::UARTDevice {
 public:
  void set_reset_pin(GPIOPin *pin) { this->reset_pin_ = pin; }
  void set_bsl_pin(GPIOPin *pin) { this->bsl_pin_ = pin; }
  void set_socket_connected_binary_sensor(binary_sensor::BinarySensor *sensor) {
    this->socket_connected_binary_sensor_ = sensor;
    this->tcp_server_.set_connected_sensor(sensor);
  }
  void set_connection_count_sensor(sensor::Sensor *sensor) {
    this->connection_count_sensor_ = sensor;
    this->tcp_server_.set_connection_count_sensor(sensor);
  }
  void set_transport_state_text_sensor(text_sensor::TextSensor *sensor) {
    this->tcp_server_.set_transport_state_sensor(sensor);
  }
  void set_pending_socket_binary_sensor(binary_sensor::BinarySensor *sensor) {
    this->tcp_server_.set_pending_socket_sensor(sensor);
  }
  void set_parked_socket_binary_sensor(binary_sensor::BinarySensor *sensor) {
    this->tcp_server_.set_parked_socket_sensor(sensor);
  }
  void set_last_transport_event_text_sensor(text_sensor::TextSensor *sensor) {
    this->tcp_server_.set_last_event_sensor(sensor);
  }
  void set_rejected_connections_sensor(sensor::Sensor *sensor) {
    this->tcp_server_.set_rejected_connections_sensor(sensor);
  }
  void set_pending_timeouts_sensor(sensor::Sensor *sensor) {
    this->tcp_server_.set_pending_timeouts_sensor(sensor);
  }
  void set_maintenance_sessions_sensor(sensor::Sensor *sensor) {
    this->tcp_server_.set_maintenance_sessions_sensor(sensor);
  }
  void set_recovery_resets_sensor(sensor::Sensor *sensor) {
    this->tcp_server_.set_recovery_resets_sensor(sensor);
  }
  void set_ip_address_text_sensor(text_sensor::TextSensor *sensor) { this->ip_address_text_sensor_ = sensor; }

  void set_flash_size_sensor(sensor::Sensor *sensor) { this->flash_size_sensor_ = sensor; }
  void set_tx_power_sensor(sensor::Sensor *sensor) { this->tx_power_sensor_ = sensor; }
  void set_pan_id_sensor(sensor::Sensor *sensor) { this->pan_id_sensor_ = sensor; }
  void set_channel_sensor(sensor::Sensor *sensor) { this->channel_sensor_ = sensor; }
  void set_on_network_binary_sensor(binary_sensor::BinarySensor *sensor) {
    this->on_network_binary_sensor_ = sensor;
  }
  void set_firmware_text_sensor(text_sensor::TextSensor *sensor) { this->firmware_text_sensor_ = sensor; }
  void set_stack_text_sensor(text_sensor::TextSensor *sensor) { this->stack_text_sensor_ = sensor; }
  void set_factory_ieee_text_sensor(text_sensor::TextSensor *sensor) { this->factory_ieee_text_sensor_ = sensor; }
  void set_self_ieee_text_sensor(text_sensor::TextSensor *sensor) { this->self_ieee_text_sensor_ = sensor; }
  void set_parent_ieee_text_sensor(text_sensor::TextSensor *sensor) { this->parent_ieee_text_sensor_ = sensor; }
  void set_role_text_sensor(text_sensor::TextSensor *sensor) { this->role_text_sensor_ = sensor; }
  void set_ext_pan_id_text_sensor(text_sensor::TextSensor *sensor) { this->ext_pan_id_text_sensor_ = sensor; }
  void set_hardware_text_sensor(text_sensor::TextSensor *sensor) { this->hardware_text_sensor_ = sensor; }
  void set_metadata_status_text_sensor(text_sensor::TextSensor *sensor) {
    this->metadata_status_text_sensor_ = sensor;
  }
  void set_network_information_status_text_sensor(text_sensor::TextSensor *sensor) {
    this->network_information_status_text_sensor_ = sensor;
  }

  void set_tcp_port(uint16_t port) {
    this->tcp_port_ = port;
    this->tcp_server_.set_port(port);
  }
  void set_pending_socket_timeout(uint32_t value) {
    this->pending_socket_timeout_ms_ = value;
    this->tcp_server_.set_pending_timeout(value);
  }
  void set_parked_socket_timeout(uint32_t value) {
    this->parked_socket_timeout_ms_ = value;
    this->tcp_server_.set_park_timeout(value);
  }
  void set_reset_timeout(uint32_t value) { this->reset_timeout_ms_ = value; }
  void set_znp_start_timeout(uint32_t value) { this->znp_start_timeout_ms_ = value; }
  void set_znp_byte_timeout(uint32_t value) { this->znp_byte_timeout_ms_ = value; }
  void set_znp_overall_timeout(uint32_t value) { this->znp_overall_timeout_ms_ = value; }
  void set_znp_post_send_delay(uint32_t value) { this->znp_post_send_delay_ms_ = value; }
  void set_znp_retries(uint8_t value) { this->znp_retries_ = value; }
  void set_bsl_ack_timeout(uint32_t value) { this->bsl_ack_timeout_ms_ = value; }
  void set_bsl_header_timeout(uint32_t value) { this->bsl_header_timeout_ms_ = value; }
  void set_bsl_payload_timeout(uint32_t value) { this->bsl_payload_timeout_ms_ = value; }
  void set_bsl_sync_gap(uint32_t value) { this->bsl_sync_gap_ms_ = value; }
  void set_nv_cc26x2(uint32_t base, uint32_t size) {
    this->nv_base_cc26x2_ = base;
    this->nv_size_cc26x2_ = size;
  }
  void set_nv_cc26x2x7(uint32_t base, uint32_t size) {
    this->nv_base_cc26x2x7_ = base;
    this->nv_size_cc26x2x7_ = size;
  }

  void setup() override;
  void loop() override;
  void dump_config() override;
  void on_shutdown() override;
  float get_setup_priority() const override { return setup_priority::DATA; }

  /// User-facing control entry points. These defer work to the component loop
  /// so buttons and HTTP handlers return immediately.
  void request_restart();
  void request_bsl();
  void request_router_rejoin();
  void request_metadata_refresh();

 protected:
  enum class ChipFamily : uint8_t {
    UNKNOWN = 0,
    CC13X2_CC26X2 = 1,
    CC13X2X7_CC26X2X7 = 2,
  };

  struct ChipInfo {
    ChipFamily family{ChipFamily::UNKNOWN};
    uint32_t chip_id_be{0};
    uint16_t chip_id_16{0};
    uint32_t wafer_id{0};
    uint32_t pg_rev{0};
    uint8_t protocols{0};
    uint8_t flash_pages{0};
    std::string hardware;
    uint32_t flash_size_bytes{0};
    uint32_t mode_cfg{0};
    uint32_t bsl_cfg{0};
  };

#ifdef USE_UART_DEBUGGER
  struct ZnpSnifferState {
    uint8_t state{0};
    uint8_t length{0};
    uint8_t cmd0{0};
    uint8_t cmd1{0};
    uint8_t index{0};
    std::array<uint8_t, 256> payload{};
  };
#endif

  enum class ResetParserState : uint8_t {
    SEEK_SOF,
    LENGTH,
    CMD0,
    CMD1,
    BODY,
  };

  bool startup_probe_();
  void enter_bsl_blocking_();
  void restart_blocking_();
  bool wait_for_reset_ind_blocking_();
  void request_restart_();
  void finish_async_restart_(bool reset_ind_seen);
  void process_async_reset_();
  void request_bsl_();
  void request_router_rejoin_();
  void request_metadata_refresh_();
  void enter_bsl_for_remote_();
  void reset_for_remote_();
  void on_tcp_normal_session_started_();
  void on_tcp_normal_session_finished_();
  void on_tcp_maintenance_finished_();
  bool set_radio_connection_led_(bool on);

  void setup_metadata_cache_();
  bool refresh_metadata_();
  bool mark_running_image_pending_();
  bool save_physical_identity_cache_();
  bool save_running_image_cache_();
  bool save_network_snapshot_cache_();
  void capture_chip_info_(const ChipInfo &chip);
  void publish_physical_identity_(const PhysicalIdentityCache &cache);
  void publish_running_image_(const RunningImageCache &cache);
  void publish_network_snapshot_(const NetworkSnapshotCache &cache);
  void publish_metadata_status_(const char *status);
  void publish_network_information_status_(const char *status);

  bool detect_chip_info_(ChipInfo *chip);
  bool read_memory_word_(uint32_t address, const char *name, uint8_t out[4]);
  void scan_nv_(ChipFamily family);
  void get_device_info_();
  void get_firmware_version_();
  void run_post_reset_diagnostics_();
  bool socket_connected_() const;
  void publish_hardware_(const char *hardware);
  void publish_flash_size_(uint32_t flash_size_bytes);
  void publish_firmware_(const char *firmware);
  void publish_stack_(const char *stack);
  void publish_factory_ieee_(const char *ieee);
  void publish_self_ieee_(const char *ieee);
  void publish_role_(const char *role);
  void publish_pan_id_(uint16_t pan_id);
  void publish_channel_(uint8_t channel);
  void publish_on_network_(bool on_network);
  void publish_parent_ieee_(const char *ieee);
  void publish_extended_pan_id_(const char *extended_pan_id);

#ifdef USE_UART_DEBUGGER
  void sniff_byte_(uart::UARTDirection direction, uint8_t byte);
  void reset_sniffer_(ZnpSnifferState &state);
  void queue_znp_observation_(const ZnpObservation &observation);
  void process_znp_observations_();
  void apply_znp_observation_(const ZnpObservation &observation);
#endif

  void register_web_handlers_();

  friend class ZigbeeTcpServer;

  GPIOPin *reset_pin_{nullptr};
  GPIOPin *bsl_pin_{nullptr};
  binary_sensor::BinarySensor *socket_connected_binary_sensor_{nullptr};
  sensor::Sensor *connection_count_sensor_{nullptr};
  text_sensor::TextSensor *ip_address_text_sensor_{nullptr};

  sensor::Sensor *flash_size_sensor_{nullptr};
  sensor::Sensor *tx_power_sensor_{nullptr};
  sensor::Sensor *pan_id_sensor_{nullptr};
  sensor::Sensor *channel_sensor_{nullptr};
  binary_sensor::BinarySensor *on_network_binary_sensor_{nullptr};
  text_sensor::TextSensor *firmware_text_sensor_{nullptr};
  text_sensor::TextSensor *stack_text_sensor_{nullptr};
  text_sensor::TextSensor *factory_ieee_text_sensor_{nullptr};
  text_sensor::TextSensor *self_ieee_text_sensor_{nullptr};
  text_sensor::TextSensor *parent_ieee_text_sensor_{nullptr};
  text_sensor::TextSensor *role_text_sensor_{nullptr};
  text_sensor::TextSensor *ext_pan_id_text_sensor_{nullptr};
  text_sensor::TextSensor *hardware_text_sensor_{nullptr};
  text_sensor::TextSensor *metadata_status_text_sensor_{nullptr};
  text_sensor::TextSensor *network_information_status_text_sensor_{nullptr};

  uint16_t tcp_port_{6638};
  uint32_t pending_socket_timeout_ms_{30000};
  uint32_t parked_socket_timeout_ms_{600000};
  uint32_t reset_timeout_ms_{5000};
  uint32_t znp_start_timeout_ms_{100};
  uint32_t znp_byte_timeout_ms_{10};
  uint32_t znp_overall_timeout_ms_{500};
  uint32_t znp_post_send_delay_ms_{10};
  uint8_t znp_retries_{2};
  uint32_t bsl_ack_timeout_ms_{50};
  uint32_t bsl_header_timeout_ms_{50};
  uint32_t bsl_payload_timeout_ms_{50};
  uint32_t bsl_sync_gap_ms_{5};
  uint32_t nv_base_cc26x2_{0x00050000};
  uint32_t nv_size_cc26x2_{0x00006000};
  uint32_t nv_base_cc26x2x7_{0x000A6000};
  uint32_t nv_size_cc26x2x7_{0x00008000};

  std::string role_{"Unknown"};
  bool sniffer_enabled_{true};
  bool operation_active_{false};
  bool async_reset_active_{false};
  uint32_t async_reset_started_ms_{0};
  ResetParserState reset_parser_state_{ResetParserState::SEEK_SOF};
  uint8_t reset_parser_length_{0};
  uint8_t reset_parser_cmd0_{0};
  uint8_t reset_parser_cmd1_{0};
  uint16_t reset_parser_remaining_{0};
  bool web_handlers_registered_{false};
  bool physical_identity_available_{false};
  bool running_image_available_{false};
  bool network_snapshot_available_{false};
  bool metadata_capture_active_{false};
  bool network_observed_this_boot_{false};

  ESPPreferenceObject physical_identity_preference_{};
  ESPPreferenceObject running_image_preference_{};
  ESPPreferenceObject network_snapshot_preference_{};
  PhysicalIdentityCache physical_identity_{};
  PhysicalIdentityCache physical_identity_candidate_{};
  RunningImageCache running_image_{};
  RunningImageCache running_image_candidate_{};
  NetworkSnapshotCache network_snapshot_{};
  NetworkSnapshotCache network_snapshot_candidate_{};

  ZigbeeSerialInterface serial_{};
  ZigbeeTcpServer tcp_server_{};

#ifdef USE_UART_DEBUGGER
  ZnpSnifferState tx_sniffer_{};
  ZnpSnifferState rx_sniffer_{};
  std::array<ZnpObservation, 8> pending_znp_observations_{};
  size_t pending_znp_observation_count_{0};
#endif
};

class RadioRestartButton : public button::Button, public Parented<ZigbeeGatewayComponent> {
 protected:
  void press_action() override { this->parent_->request_restart(); }
};

class RadioBslButton : public button::Button, public Parented<ZigbeeGatewayComponent> {
 protected:
  void press_action() override { this->parent_->request_bsl(); }
};

class RouterRejoinButton : public button::Button, public Parented<ZigbeeGatewayComponent> {
 protected:
  void press_action() override { this->parent_->request_router_rejoin(); }
};

class RadioMetadataRefreshButton : public button::Button, public Parented<ZigbeeGatewayComponent> {
 protected:
  void press_action() override { this->parent_->request_metadata_refresh(); }
};

}  // namespace zigbee_gateway
}  // namespace esphome
