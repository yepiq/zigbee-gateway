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

#include "protocol.h"

namespace esphome {
namespace zigbee_gateway {

class ZigbeeGatewayComponent : public Component, public uart::UARTDevice {
 public:
  void set_reset_pin(GPIOPin *pin) { this->reset_pin_ = pin; }
  void set_bsl_pin(GPIOPin *pin) { this->bsl_pin_ = pin; }
  void set_socket_connected_binary_sensor(binary_sensor::BinarySensor *sensor) {
    this->socket_connected_binary_sensor_ = sensor;
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
  void set_self_ieee_text_sensor(text_sensor::TextSensor *sensor) { this->self_ieee_text_sensor_ = sensor; }
  void set_parent_ieee_text_sensor(text_sensor::TextSensor *sensor) { this->parent_ieee_text_sensor_ = sensor; }
  void set_role_text_sensor(text_sensor::TextSensor *sensor) { this->role_text_sensor_ = sensor; }
  void set_ext_pan_id_text_sensor(text_sensor::TextSensor *sensor) { this->ext_pan_id_text_sensor_ = sensor; }
  void set_hardware_text_sensor(text_sensor::TextSensor *sensor) { this->hardware_text_sensor_ = sensor; }

  void set_tcp_port(uint16_t port) { this->tcp_port_ = port; }
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
  float get_setup_priority() const override { return setup_priority::DATA; }

  /// User-facing control entry points. These defer work to the component loop
  /// so buttons and HTTP handlers return immediately.
  void request_restart();
  void request_bsl();
  void request_router_rejoin();

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

  void startup_probe_();
  void enter_bsl_blocking_();
  void restart_blocking_();
  bool wait_for_reset_ind_blocking_();
  void request_restart_();
  void finish_async_restart_(bool reset_ind_seen);
  void process_async_reset_();
  void request_bsl_();
  void request_router_rejoin_();

  bool detect_chip_info_(ChipInfo *chip);
  bool read_memory_word_(uint32_t address, const char *name, uint8_t out[4]);
  void scan_nv_(ChipFamily family);
  void get_device_info_();
  void get_firmware_version_();
  void run_post_reset_diagnostics_();
  bool socket_connected_() const;
  void publish_role_(const char *role);

#ifdef USE_UART_DEBUGGER
  void sniff_byte_(uart::UARTDirection direction, uint8_t byte);
  void reset_sniffer_(ZnpSnifferState &state);
#endif

  void register_web_handlers_();

  GPIOPin *reset_pin_{nullptr};
  GPIOPin *bsl_pin_{nullptr};
  binary_sensor::BinarySensor *socket_connected_binary_sensor_{nullptr};
  text_sensor::TextSensor *ip_address_text_sensor_{nullptr};

  sensor::Sensor *flash_size_sensor_{nullptr};
  sensor::Sensor *tx_power_sensor_{nullptr};
  sensor::Sensor *pan_id_sensor_{nullptr};
  sensor::Sensor *channel_sensor_{nullptr};
  binary_sensor::BinarySensor *on_network_binary_sensor_{nullptr};
  text_sensor::TextSensor *firmware_text_sensor_{nullptr};
  text_sensor::TextSensor *stack_text_sensor_{nullptr};
  text_sensor::TextSensor *self_ieee_text_sensor_{nullptr};
  text_sensor::TextSensor *parent_ieee_text_sensor_{nullptr};
  text_sensor::TextSensor *role_text_sensor_{nullptr};
  text_sensor::TextSensor *ext_pan_id_text_sensor_{nullptr};
  text_sensor::TextSensor *hardware_text_sensor_{nullptr};

  uint16_t tcp_port_{6638};
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

#ifdef USE_UART_DEBUGGER
  ZnpSnifferState tx_sniffer_{};
  ZnpSnifferState rx_sniffer_{};
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

}  // namespace zigbee_gateway
}  // namespace esphome
