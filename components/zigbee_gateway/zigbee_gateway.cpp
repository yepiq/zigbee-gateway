#include "zigbee_gateway.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>
#include <utility>

#include "esphome/core/application.h"
#include "esphome/core/log.h"

#if defined(USE_WEBSERVER) && defined(USE_NETWORK)
#include "esphome/components/web_server_base/web_server_base.h"
#include "esphome/components/web_server_idf/web_server_idf.h"
#endif

namespace esphome {
namespace zigbee_gateway {

static const char *const TAG = "zigbee_gateway";

#if defined(USE_WEBSERVER) && defined(USE_NETWORK)
class GatewayCommandHandler : public web_server_idf::AsyncWebHandler {
 public:
  GatewayCommandHandler(std::string uri, std::function<void()> action)
      : uri_(std::move(uri)), action_(std::move(action)) {}

  bool canHandle(web_server_idf::AsyncWebServerRequest *request) const override {
    char buffer[web_server_idf::AsyncWebServerRequest::URL_BUF_SIZE];
    return std::string(request->url_to(buffer)) == this->uri_;
  }

  void handleRequest(web_server_idf::AsyncWebServerRequest *request) override {
    if (this->action_)
      this->action_();
    request->send(200, "text/plain", "OK\n");
  }

 protected:
  std::string uri_;
  std::function<void()> action_;
};
#endif

void ZigbeeGatewayComponent::setup() {
  ESP_LOGCONFIG(TAG, "Setting up Zigbee Gateway...");

  this->serial_.set_uart(this->parent_);
  this->serial_.set_owner(ZigbeeSerialInterface::Owner::LOCAL);
  this->tcp_server_.set_parent(this);
  this->tcp_server_.set_serial(&this->serial_);
  this->tcp_server_.set_port(this->tcp_port_);
  this->tcp_server_.set_pending_timeout(this->pending_socket_timeout_ms_);
  this->tcp_server_.set_park_timeout(this->parked_socket_timeout_ms_);
  this->tcp_server_.set_connected_sensor(this->socket_connected_binary_sensor_);
  this->tcp_server_.set_connection_count_sensor(this->connection_count_sensor_);

  this->reset_pin_->setup();
  this->bsl_pin_->setup();
  // Both pins are configured inverted for UZG-01. Writing false means the
  // logical control is released, independent of the physical pin polarity.
  this->reset_pin_->digital_write(false);
  this->bsl_pin_->digital_write(false);

#ifdef USE_UART_DEBUGGER
  // Passive byte tap only: this callback observes TX/RX but never consumes RX.
  // All consuming reads and writes still pass through ZigbeeSerialInterface.
  this->parent_->add_debug_callback(
      [this](uart::UARTDirection direction, uint8_t byte) { this->sniff_byte_(direction, byte); });
#endif

  // Restore a clean, previously verified snapshot without touching the radio.
  // First boot, an incompatible record, or a dirty record left by flashing
  // takes the slower local BSL/NV/ZNP identification path before TCP starts.
  this->setup_metadata_cache_();
  this->serial_.release(ZigbeeSerialInterface::Owner::LOCAL);
}

void ZigbeeGatewayComponent::loop() {
  this->register_web_handlers_();
  if (!this->tcp_server_.is_started())
    this->tcp_server_.start();
  this->tcp_server_.loop();
  if (this->async_reset_active_)
    this->process_async_reset_();
}

void ZigbeeGatewayComponent::dump_config() {
  ESP_LOGCONFIG(TAG, "Zigbee Gateway:");
  LOG_PIN("  Reset Pin: ", this->reset_pin_);
  LOG_PIN("  BSL Pin: ", this->bsl_pin_);
  ESP_LOGCONFIG(TAG, "  TCP compatibility port: %u", (unsigned) this->tcp_port_);
  ESP_LOGCONFIG(TAG, "  Pending socket timeout: %u ms", (unsigned) this->pending_socket_timeout_ms_);
  ESP_LOGCONFIG(TAG, "  Parked socket timeout: %u ms", (unsigned) this->parked_socket_timeout_ms_);
  ESP_LOGCONFIG(TAG, "  NV CC13x2/CC26x2: base=0x%08X size=0x%08X", (unsigned) this->nv_base_cc26x2_,
                (unsigned) this->nv_size_cc26x2_);
  ESP_LOGCONFIG(TAG, "  NV CC13x2x7/CC26x2x7: base=0x%08X size=0x%08X",
                (unsigned) this->nv_base_cc26x2x7_, (unsigned) this->nv_size_cc26x2x7_);
}

void ZigbeeGatewayComponent::on_shutdown() { this->tcp_server_.shutdown(); }

bool ZigbeeGatewayComponent::socket_connected_() const {
  return this->tcp_server_.has_any_client();
}

void ZigbeeGatewayComponent::publish_hardware_(const char *hardware) {
  if (this->hardware_text_sensor_ != nullptr)
    this->hardware_text_sensor_->publish_state(hardware);
  if (!this->metadata_capture_active_)
    return;
  if (copy_radio_metadata_text(this->metadata_candidate_.hardware, hardware))
    this->metadata_candidate_.known |= RADIO_METADATA_HARDWARE;
  else
    this->metadata_candidate_.known &= ~RADIO_METADATA_HARDWARE;
}

void ZigbeeGatewayComponent::publish_flash_size_(uint32_t flash_size_bytes) {
  if (this->flash_size_sensor_ != nullptr)
    this->flash_size_sensor_->publish_state(static_cast<float>(flash_size_bytes));
  if (this->metadata_capture_active_) {
    this->metadata_candidate_.flash_size_bytes = flash_size_bytes;
    this->metadata_candidate_.known |= RADIO_METADATA_FLASH_SIZE;
  }
}

void ZigbeeGatewayComponent::publish_firmware_(const char *firmware) {
  if (this->firmware_text_sensor_ != nullptr)
    this->firmware_text_sensor_->publish_state(firmware);
  if (!this->metadata_capture_active_)
    return;
  if (std::strcmp(firmware, "Unknown") != 0 &&
      copy_radio_metadata_text(this->metadata_candidate_.firmware, firmware))
    this->metadata_candidate_.known |= RADIO_METADATA_FIRMWARE;
  else
    this->metadata_candidate_.known &= ~RADIO_METADATA_FIRMWARE;
}

void ZigbeeGatewayComponent::publish_stack_(const char *stack) {
  if (this->stack_text_sensor_ != nullptr)
    this->stack_text_sensor_->publish_state(stack);
  if (!this->metadata_capture_active_)
    return;
  if (std::strcmp(stack, "Unknown") != 0 &&
      copy_radio_metadata_text(this->metadata_candidate_.stack, stack))
    this->metadata_candidate_.known |= RADIO_METADATA_STACK;
  else
    this->metadata_candidate_.known &= ~RADIO_METADATA_STACK;
}

void ZigbeeGatewayComponent::publish_self_ieee_(const char *ieee) {
  if (this->self_ieee_text_sensor_ != nullptr)
    this->self_ieee_text_sensor_->publish_state(ieee);
  if (!this->metadata_capture_active_)
    return;
  if (std::strcmp(ieee, "Unknown") != 0 &&
      copy_radio_metadata_text(this->metadata_candidate_.self_ieee, ieee))
    this->metadata_candidate_.known |= RADIO_METADATA_SELF_IEEE;
  else
    this->metadata_candidate_.known &= ~RADIO_METADATA_SELF_IEEE;
}

void ZigbeeGatewayComponent::publish_role_(const char *role) {
  this->role_ = role;
  if (this->role_text_sensor_ != nullptr)
    this->role_text_sensor_->publish_state(role);
  if (!this->metadata_capture_active_)
    return;
  if (std::strcmp(role, "Unknown") != 0 &&
      copy_radio_metadata_text(this->metadata_candidate_.role, role))
    this->metadata_candidate_.known |= RADIO_METADATA_ROLE;
  else
    this->metadata_candidate_.known &= ~RADIO_METADATA_ROLE;
}

void ZigbeeGatewayComponent::publish_pan_id_(uint16_t pan_id) {
  if (this->pan_id_sensor_ != nullptr)
    this->pan_id_sensor_->publish_state(static_cast<float>(pan_id));
  if (this->metadata_capture_active_) {
    this->metadata_candidate_.pan_id = pan_id;
    this->metadata_candidate_.known |= RADIO_METADATA_PAN_ID;
  }
}

void ZigbeeGatewayComponent::publish_channel_(uint8_t channel) {
  if (this->channel_sensor_ != nullptr)
    this->channel_sensor_->publish_state(static_cast<float>(channel));
  if (this->metadata_capture_active_) {
    this->metadata_candidate_.channel = channel;
    this->metadata_candidate_.known |= RADIO_METADATA_CHANNEL;
  }
}

void ZigbeeGatewayComponent::publish_on_network_(bool on_network) {
  if (this->on_network_binary_sensor_ != nullptr)
    this->on_network_binary_sensor_->publish_state(on_network);
  if (this->metadata_capture_active_) {
    this->metadata_candidate_.on_network = on_network;
    this->metadata_candidate_.known |= RADIO_METADATA_ON_NETWORK;
  }
}

void ZigbeeGatewayComponent::publish_parent_ieee_(const char *ieee) {
  if (this->parent_ieee_text_sensor_ != nullptr)
    this->parent_ieee_text_sensor_->publish_state(ieee);
  if (!this->metadata_capture_active_)
    return;
  if (std::strcmp(ieee, "Unknown") != 0 &&
      copy_radio_metadata_text(this->metadata_candidate_.parent_ieee, ieee))
    this->metadata_candidate_.known |= RADIO_METADATA_PARENT_IEEE;
  else
    this->metadata_candidate_.known &= ~RADIO_METADATA_PARENT_IEEE;
}

void ZigbeeGatewayComponent::publish_extended_pan_id_(const char *extended_pan_id) {
  if (this->ext_pan_id_text_sensor_ != nullptr)
    this->ext_pan_id_text_sensor_->publish_state(extended_pan_id);
  if (!this->metadata_capture_active_)
    return;
  if (std::strcmp(extended_pan_id, "Unknown") != 0 &&
      copy_radio_metadata_text(this->metadata_candidate_.extended_pan_id, extended_pan_id))
    this->metadata_candidate_.known |= RADIO_METADATA_EXTENDED_PAN_ID;
  else
    this->metadata_candidate_.known &= ~RADIO_METADATA_EXTENDED_PAN_ID;
}

void ZigbeeGatewayComponent::publish_metadata_status_(const char *status) {
  if (this->metadata_status_text_sensor_ != nullptr)
    this->metadata_status_text_sensor_->publish_state(status);
}

void ZigbeeGatewayComponent::publish_metadata_record_(const RadioMetadataCache &cache) {
  this->publish_hardware_((cache.known & RADIO_METADATA_HARDWARE) != 0 ? cache.hardware : "Unknown");
  if (this->flash_size_sensor_ != nullptr) {
    this->flash_size_sensor_->publish_state(
        (cache.known & RADIO_METADATA_FLASH_SIZE) != 0 ? static_cast<float>(cache.flash_size_bytes) : NAN);
  }
  this->publish_firmware_((cache.known & RADIO_METADATA_FIRMWARE) != 0 ? cache.firmware : "Unknown");
  this->publish_stack_((cache.known & RADIO_METADATA_STACK) != 0 ? cache.stack : "Unknown");
  this->publish_self_ieee_((cache.known & RADIO_METADATA_SELF_IEEE) != 0 ? cache.self_ieee : "Unknown");
  this->publish_role_((cache.known & RADIO_METADATA_ROLE) != 0 ? cache.role : "Unknown");
  if (this->pan_id_sensor_ != nullptr) {
    this->pan_id_sensor_->publish_state(
        (cache.known & RADIO_METADATA_PAN_ID) != 0 ? static_cast<float>(cache.pan_id) : NAN);
  }
  if (this->channel_sensor_ != nullptr) {
    this->channel_sensor_->publish_state(
        (cache.known & RADIO_METADATA_CHANNEL) != 0 ? static_cast<float>(cache.channel) : NAN);
  }
  if (this->on_network_binary_sensor_ != nullptr) {
    if ((cache.known & RADIO_METADATA_ON_NETWORK) != 0)
      this->on_network_binary_sensor_->publish_state(cache.on_network != 0);
    else
      this->on_network_binary_sensor_->invalidate_state();
  }
  this->publish_parent_ieee_(
      (cache.known & RADIO_METADATA_PARENT_IEEE) != 0 ? cache.parent_ieee : "Unknown");
  this->publish_extended_pan_id_(
      (cache.known & RADIO_METADATA_EXTENDED_PAN_ID) != 0 ? cache.extended_pan_id : "Unknown");
}

bool ZigbeeGatewayComponent::save_metadata_cache_() {
  if (!this->metadata_preference_.save(&this->metadata_cache_)) {
    ESP_LOGE(TAG, "Failed to queue Zigbee metadata preference.");
    return false;
  }
  if (global_preferences == nullptr || !global_preferences->sync()) {
    ESP_LOGE(TAG, "Failed to commit Zigbee metadata preference.");
    return false;
  }
  return true;
}

bool ZigbeeGatewayComponent::mark_metadata_dirty_() {
  if (!this->metadata_cache_available_) {
    initialize_radio_metadata_cache(&this->metadata_cache_);
    this->metadata_cache_available_ = true;
  }
  this->metadata_cache_.dirty = 1;
  const bool saved = this->save_metadata_cache_();
  this->publish_metadata_status_(this->metadata_cache_.known != 0 ? "Stale" : "Unavailable");
  if (!saved)
    ESP_LOGE(TAG, "Could not persist the pre-BSL dirty marker; cached metadata is unsafe after reboot.");
  return saved;
}

void ZigbeeGatewayComponent::capture_chip_info_(const ChipInfo &chip) {
  if (!this->metadata_capture_active_)
    return;
  this->metadata_candidate_.chip_family = static_cast<uint8_t>(chip.family);
  this->metadata_candidate_.chip_id_be = chip.chip_id_be;
  this->metadata_candidate_.chip_id_16 = chip.chip_id_16;
  this->metadata_candidate_.wafer_id = chip.wafer_id;
  this->metadata_candidate_.pg_rev = chip.pg_rev;
  this->metadata_candidate_.protocols = chip.protocols;
  this->metadata_candidate_.flash_pages = chip.flash_pages;
  this->metadata_candidate_.mode_cfg = chip.mode_cfg;
  this->metadata_candidate_.bsl_cfg = chip.bsl_cfg;
}

void ZigbeeGatewayComponent::setup_metadata_cache_() {
  this->metadata_preference_ = global_preferences->make_preference<RadioMetadataCache>(
      fnv1_hash("zigbee_gateway.radio_metadata"), true);

  RadioMetadataCache loaded{};
  if (this->metadata_preference_.load(&loaded) && valid_radio_metadata_cache(loaded)) {
    this->metadata_cache_ = loaded;
    this->metadata_cache_available_ = true;
    this->publish_metadata_record_(this->metadata_cache_);
    if (this->metadata_cache_.dirty == 0) {
      ESP_LOGI(TAG, "Restored Zigbee metadata cache generation %u; startup radio probe skipped.",
               (unsigned) this->metadata_cache_.generation);
      this->publish_metadata_status_("Restored");
      return;
    }
    ESP_LOGW(TAG, "Zigbee metadata cache generation %u is dirty; refreshing before TCP startup.",
             (unsigned) this->metadata_cache_.generation);
    this->publish_metadata_status_("Stale");
  } else {
    initialize_radio_metadata_cache(&this->metadata_cache_);
    this->metadata_cache_available_ = false;
    this->publish_metadata_record_(this->metadata_cache_);
    this->publish_metadata_status_("Unavailable");
    ESP_LOGI(TAG, "No compatible Zigbee metadata cache; running initial identification.");
  }

  this->refresh_metadata_();
}

bool ZigbeeGatewayComponent::refresh_metadata_() {
  if (this->socket_connected_()) {
    ESP_LOGW(TAG, "TCP client connected; metadata refresh skipped to preserve UART ownership.");
    return false;
  }

  this->mark_metadata_dirty_();
  const uint32_t next_generation = this->metadata_cache_.generation + 1;
  initialize_radio_metadata_cache(&this->metadata_candidate_, next_generation);
  this->metadata_candidate_.dirty = 1;
  this->metadata_capture_active_ = true;
  this->role_ = "Unknown";
  this->publish_metadata_status_("Refreshing");

  const bool identified = this->startup_probe_();
  this->metadata_capture_active_ = false;

  if (!identified) {
    ESP_LOGW(TAG, "Zigbee metadata refresh failed; retaining the last known snapshot as stale.");
    this->publish_metadata_record_(this->metadata_cache_);
    this->publish_metadata_status_(this->metadata_cache_.known != 0 ? "Stale" : "Unavailable");
    return false;
  }

  this->metadata_candidate_.dirty = 0;
  this->metadata_cache_ = this->metadata_candidate_;
  this->metadata_cache_available_ = true;
  this->publish_metadata_record_(this->metadata_cache_);
  if (this->save_metadata_cache_()) {
    ESP_LOGI(TAG, "Saved verified Zigbee metadata cache generation %u.",
             (unsigned) this->metadata_cache_.generation);
  } else {
    ESP_LOGE(TAG, "Radio metadata is verified for this boot but could not be saved.");
  }
  this->publish_metadata_status_("Verified");
  return true;
}

bool ZigbeeGatewayComponent::startup_probe_() {
  ESP_LOGI(TAG, "Get Zigbee Chip Info");

  bool identified = false;
  if (this->socket_connected_()) {
    ESP_LOGW(TAG, "Socket client connected; skipping BSL chip probe to avoid UART contention.");
  } else {
    this->enter_bsl_blocking_();
    if (!bsl_sync(&this->serial_, this->bsl_ack_timeout_ms_, this->bsl_sync_gap_ms_)) {
      ESP_LOGW(TAG, "Failed to SYNC with BSL");
    } else {
      ChipInfo chip;
      if (this->detect_chip_info_(&chip)) {
        identified = true;
        this->scan_nv_(chip.family);
      }
    }
  }

  this->restart_blocking_();
  return identified;
}

void ZigbeeGatewayComponent::enter_bsl_blocking_() {
  ESP_LOGI(TAG, "Put Zigbee in BSL mode.");
  this->sniffer_enabled_ = false;

  this->bsl_pin_->digital_write(true);
  this->reset_pin_->digital_write(true);
  delay(50);

  this->reset_pin_->digital_write(false);
  delay(250);

  this->bsl_pin_->digital_write(false);
  delay(100);

  if (this->ip_address_text_sensor_ != nullptr && this->ip_address_text_sensor_->has_state()) {
    ESP_LOGI(TAG,
             "Zigbee is in BSL mode. To update firmware, run: "
             "cc2538-bsl.py -p socket://%s:%u -evw firmware.hex",
             this->ip_address_text_sensor_->state.c_str(), (unsigned) this->tcp_port_);
  } else {
    ESP_LOGI(TAG, "Zigbee is in BSL mode; raw flashing transport is TCP port %u", (unsigned) this->tcp_port_);
  }
}

void ZigbeeGatewayComponent::restart_blocking_() {
  ESP_LOGI(TAG, "Resetting Zigbee module.");
  this->reset_pin_->digital_write(true);
  delay(15);
  this->reset_pin_->digital_write(false);

  const bool reset_ind_seen = this->wait_for_reset_ind_blocking_();
  if (reset_ind_seen)
    ESP_LOGD(TAG, "SYS_RESET_IND received");
  else
    ESP_LOGW(TAG, "SYS_RESET_IND was not received before the %u ms timeout",
             (unsigned) this->reset_timeout_ms_);

  ESP_LOGI(TAG, "Zigbee module has been reset.");
  this->run_post_reset_diagnostics_();
}

bool ZigbeeGatewayComponent::wait_for_reset_ind_blocking_() {
  const uint32_t started = millis();
  uint8_t cmd0 = 0;
  uint8_t cmd1 = 0;
  uint8_t length = 0;
  uint8_t payload[256] = {0};

  while (millis() - started < this->reset_timeout_ms_) {
    if (znp_recv_once(&this->serial_, &cmd0, &cmd1, payload, sizeof(payload), &length,
                      std::min<uint32_t>(this->znp_start_timeout_ms_, 20), this->znp_byte_timeout_ms_)) {
      if (cmd0 == 0x41 && cmd1 == 0x80)
        return true;
    }
    App.feed_wdt();
    delay(1);
  }
  return false;
}

void ZigbeeGatewayComponent::run_post_reset_diagnostics_() {
  if (this->role_ == "Router") {
    ESP_LOGI(TAG, "Zigbee role is Router; skipping ZNP routines.");
    this->sniffer_enabled_ = false;
    return;
  }

  ESP_LOGI(TAG, "Re-run all ZNP routines.");
  this->sniffer_enabled_ = true;
  this->get_device_info_();
  this->get_firmware_version_();
}

void ZigbeeGatewayComponent::request_restart() {
  this->defer("zigbee_restart", [this]() { this->request_restart_(); });
}

void ZigbeeGatewayComponent::request_bsl() {
  this->defer("zigbee_bsl", [this]() { this->request_bsl_(); });
}

void ZigbeeGatewayComponent::request_router_rejoin() {
  this->defer("zigbee_router_rejoin", [this]() { this->request_router_rejoin_(); });
}

void ZigbeeGatewayComponent::request_metadata_refresh() {
  this->defer("zigbee_metadata_refresh", [this]() { this->request_metadata_refresh_(); });
}

void ZigbeeGatewayComponent::request_restart_() {
  if (this->operation_active_) {
    ESP_LOGW(TAG, "Another Zigbee operation is active; restart request ignored.");
    return;
  }

  if (this->tcp_server_.has_any_client()) {
    // Preserve the current TCP owner and forward the radio's reset indication
    // to it. Local diagnostics must not race that client for the same bytes.
    this->tcp_server_.request_reset();
    return;
  }
  if (!this->serial_.claim(ZigbeeSerialInterface::Owner::LOCAL)) {
    ESP_LOGW(TAG, "UART is owned by another operation; restart request ignored.");
    return;
  }

  ESP_LOGI(TAG, "Resetting Zigbee module.");
  this->operation_active_ = true;
  this->reset_pin_->digital_write(true);
  this->set_timeout("zigbee_reset_release", 15, [this]() {
    this->reset_pin_->digital_write(false);
    this->async_reset_active_ = true;
    this->async_reset_started_ms_ = millis();
    this->reset_parser_state_ = ResetParserState::SEEK_SOF;
    this->reset_parser_remaining_ = 0;
  });
}

void ZigbeeGatewayComponent::process_async_reset_() {
  // Stateful, bounded parser: consume at most 64 bytes per loop pass and never
  // spin waiting for a partial ZNP frame. This replaces the unbounded YAML
  // loops from the working baseline while preserving SYS_RESET_IND detection.
  uint8_t byte = 0;
  uint8_t consumed = 0;
  while (consumed < 64 && this->serial_.available() && this->serial_.read_byte(&byte)) {
    consumed++;
    switch (this->reset_parser_state_) {
      case ResetParserState::SEEK_SOF:
        if (byte == 0xFE)
          this->reset_parser_state_ = ResetParserState::LENGTH;
        break;
      case ResetParserState::LENGTH:
        this->reset_parser_length_ = byte;
        this->reset_parser_state_ = ResetParserState::CMD0;
        break;
      case ResetParserState::CMD0:
        this->reset_parser_cmd0_ = byte;
        this->reset_parser_state_ = ResetParserState::CMD1;
        break;
      case ResetParserState::CMD1:
        this->reset_parser_cmd1_ = byte;
        this->reset_parser_remaining_ = static_cast<uint16_t>(this->reset_parser_length_) + 1;
        this->reset_parser_state_ = ResetParserState::BODY;
        break;
      case ResetParserState::BODY:
        if (this->reset_parser_remaining_ > 0)
          this->reset_parser_remaining_--;
        if (this->reset_parser_remaining_ == 0) {
          const bool is_reset_ind = this->reset_parser_cmd0_ == 0x41 && this->reset_parser_cmd1_ == 0x80;
          this->reset_parser_state_ = ResetParserState::SEEK_SOF;
          if (is_reset_ind) {
            ESP_LOGD(TAG, "SYS_RESET_IND received");
            this->finish_async_restart_(true);
            return;
          }
        }
        break;
    }
  }

  if (millis() - this->async_reset_started_ms_ >= this->reset_timeout_ms_)
    this->finish_async_restart_(false);
}

void ZigbeeGatewayComponent::finish_async_restart_(bool reset_ind_seen) {
  this->async_reset_active_ = false;
  if (!reset_ind_seen)
    ESP_LOGW(TAG, "SYS_RESET_IND was not received before the %u ms timeout",
             (unsigned) this->reset_timeout_ms_);
  ESP_LOGI(TAG, "Zigbee module has been reset.");
  this->run_post_reset_diagnostics_();
  this->operation_active_ = false;
  this->serial_.release(ZigbeeSerialInterface::Owner::LOCAL);
}

void ZigbeeGatewayComponent::request_bsl_() {
  if (this->operation_active_) {
    ESP_LOGW(TAG, "Another Zigbee operation is active; BSL request ignored.");
    return;
  }

  if (this->tcp_server_.is_started()) {
    this->tcp_server_.request_bsl();
    return;
  }
  if (!this->serial_.claim(ZigbeeSerialInterface::Owner::LOCAL)) {
    ESP_LOGW(TAG, "UART is owned by another operation; BSL request ignored.");
    return;
  }

  ESP_LOGI(TAG, "Put Zigbee in BSL mode.");
  this->operation_active_ = true;
  this->sniffer_enabled_ = false;
  this->mark_metadata_dirty_();
  this->bsl_pin_->digital_write(true);
  this->reset_pin_->digital_write(true);

  this->set_timeout("zigbee_bsl_release_reset", 50, [this]() {
    this->reset_pin_->digital_write(false);
    this->set_timeout("zigbee_bsl_release_pin", 250, [this]() {
      this->bsl_pin_->digital_write(false);
      this->set_timeout("zigbee_bsl_settle", 100, [this]() {
        this->operation_active_ = false;
        this->serial_.release(ZigbeeSerialInterface::Owner::LOCAL);
        if (this->ip_address_text_sensor_ != nullptr && this->ip_address_text_sensor_->has_state()) {
          ESP_LOGI(TAG,
                   "Zigbee is in BSL mode. To update firmware, run: "
                   "cc2538-bsl.py -p socket://%s:%u -evw firmware.hex",
                   this->ip_address_text_sensor_->state.c_str(), (unsigned) this->tcp_port_);
        } else {
          ESP_LOGI(TAG, "Zigbee is in BSL mode; raw flashing transport is TCP port %u",
                   (unsigned) this->tcp_port_);
        }
      });
    });
  });
}

void ZigbeeGatewayComponent::request_router_rejoin_() {
  if (this->operation_active_) {
    ESP_LOGW(TAG, "Another Zigbee operation is active; router rejoin request ignored.");
    return;
  }
  if (this->role_ != "Router" && this->role_ != "Unknown") {
    ESP_LOGI(TAG, "Router rejoin is only applicable in Router role; skipping.");
    return;
  }

  ESP_LOGI(TAG, "Put Zigbee in router rejoin mode.");
  this->operation_active_ = true;
  this->bsl_pin_->digital_write(true);
  this->set_timeout("zigbee_rejoin_release", 250, [this]() {
    this->bsl_pin_->digital_write(false);
    this->set_timeout("zigbee_rejoin_settle", 500, [this]() {
      this->operation_active_ = false;
      ESP_LOGI(TAG, "Zigbee router rejoin pulse complete.");
    });
  });
}

void ZigbeeGatewayComponent::request_metadata_refresh_() {
  if (this->operation_active_) {
    ESP_LOGW(TAG, "Another Zigbee operation is active; metadata refresh request ignored.");
    return;
  }
  if (this->tcp_server_.has_any_client()) {
    ESP_LOGW(TAG, "TCP client connected; stop it before manually refreshing Zigbee information.");
    return;
  }
  if (!this->serial_.claim(ZigbeeSerialInterface::Owner::LOCAL)) {
    ESP_LOGW(TAG, "UART is owned by another operation; metadata refresh request ignored.");
    return;
  }

  this->operation_active_ = true;
  this->refresh_metadata_();
  this->operation_active_ = false;
  this->serial_.release(ZigbeeSerialInterface::Owner::LOCAL);
}

void ZigbeeGatewayComponent::enter_bsl_for_remote_() {
  // The TCP server has already selected the exclusive maintenance owner and
  // quarantined any normal client. Keep the raw stream opaque: only manipulate
  // the radio pins here; the external tool owns every following BSL byte.
  this->mark_metadata_dirty_();
  this->enter_bsl_blocking_();
}

void ZigbeeGatewayComponent::reset_for_remote_() {
  // Compatibility reset used by /cmdZigRST. Do not consume SYS_RESET_IND or
  // run local ZNP diagnostics: the active TCP client must receive all UART
  // bytes, and may continue with application-mode requests on the same socket.
  ESP_LOGI(TAG, "Resetting Zigbee module for remote TCP owner.");
  this->reset_pin_->digital_write(true);
  delay(15);
  this->reset_pin_->digital_write(false);
}

void ZigbeeGatewayComponent::on_tcp_normal_session_started_() {
  this->sniffer_enabled_ = true;
}

void ZigbeeGatewayComponent::on_tcp_maintenance_finished_() {
  // Transparent BSL access can replace coordinator firmware with router
  // firmware (or vice versa) without the ESP32 observing the image. A BSL
  // session dirtied the snapshot before pin takeover; identify the resulting
  // image locally before admitting the next normal TCP owner.
  this->sniffer_enabled_ = true;
  if (!this->metadata_cache_available_ || this->metadata_cache_.dirty == 0) {
    ESP_LOGI(TAG, "Maintenance transport ended without a BSL metadata change.");
    return;
  }
  if (!this->serial_.claim(ZigbeeSerialInterface::Owner::LOCAL)) {
    ESP_LOGE(TAG, "Could not claim UART for post-maintenance metadata refresh.");
    this->publish_metadata_status_("Stale");
    return;
  }

  ESP_LOGI(TAG, "Maintenance transport ended; identifying the resulting Zigbee firmware.");
  this->operation_active_ = true;
  this->refresh_metadata_();
  this->operation_active_ = false;
  this->serial_.release(ZigbeeSerialInterface::Owner::LOCAL);
}

void ZigbeeGatewayComponent::get_device_info_() {
  ESP_LOGI(TAG, "Get Zigbee IEEE Address");
  if (this->socket_connected_()) {
    ESP_LOGW(TAG, "Socket client connected; skipping UTIL_GET_DEVICE_INFO to avoid UART contention.");
    return;
  }

  uint8_t buffer[16] = {0};
  const bool ok = znp_exec(
      &this->serial_,
      /* SREQ */ 0x27, 0x00,
      /* expected SRSP */ 0x67, 0x00,
      [this](const uint8_t *data, uint8_t length) {
        if (length != 14)
          return;

        // UTIL_GET_DEVICE_INFO returns devType followed by the local IEEE
        // address in least-significant-byte-first order.
        char ieee[24];
        snprintf(ieee, sizeof(ieee), "%02X:%02X:%02X:%02X:%02X:%02X:%02X:%02X", data[8], data[7], data[6],
                 data[5], data[4], data[3], data[2], data[1]);
        this->publish_self_ieee_(ieee);

        const char *role = "Unknown";
        switch (data[0]) {
          case 0x00:
            role = "Coordinator";
            break;
          case 0x01:
            role = "Router";
            break;
          case 0x02:
            role = "End Device";
            break;
        }
        this->publish_role_(role);
        ESP_LOGI(TAG, "UTIL_GET_DEVICE_INFO -> IEEE: %s, Role: %s", ieee, role);
      },
      buffer, sizeof(buffer), this->znp_start_timeout_ms_, this->znp_byte_timeout_ms_,
      this->znp_overall_timeout_ms_, this->znp_retries_, this->znp_post_send_delay_ms_);

  if (!ok) {
    // Do not overwrite an IEEE obtained through BSL.
    ESP_LOGW(TAG, "UTIL_GET_DEVICE_INFO timed out; preserving the current IEEE address.");
    this->publish_role_("Unknown");
  }
}

void ZigbeeGatewayComponent::get_firmware_version_() {
  ESP_LOGI(TAG, "Get Zigbee Firmware Version");
  if (this->socket_connected_()) {
    ESP_LOGW(TAG, "Socket client connected; skipping SYS_VERSION to avoid UART contention.");
    return;
  }

  uint8_t buffer[16] = {0};
  const bool ok = znp_exec(
      &this->serial_,
      /* SREQ */ 0x21, 0x02,
      /* expected SRSP */ 0x61, 0x02,
      [this](const uint8_t *data, uint8_t length) {
        if (length != 10)
          return;

        const uint8_t major = data[2];
        const uint8_t minor = data[3];
        const uint8_t maintenance = data[4];
        const uint32_t build = static_cast<uint32_t>(data[5]) | (static_cast<uint32_t>(data[6]) << 8) |
                               (static_cast<uint32_t>(data[7]) << 16) |
                               (static_cast<uint32_t>(data[8]) << 24);
        const std::string firmware = std::to_string(build);
        this->publish_firmware_(firmware.c_str());
        char stack[16];
        snprintf(stack, sizeof(stack), "%u.%u.%u", major, minor, maintenance);
        this->publish_stack_(stack);
        ESP_LOGI(TAG, "SYS_VERSION -> build=%u, stack=%s", (unsigned) build, stack);
      },
      buffer, sizeof(buffer), this->znp_start_timeout_ms_, this->znp_byte_timeout_ms_,
      this->znp_overall_timeout_ms_, this->znp_retries_, this->znp_post_send_delay_ms_);

  if (!ok) {
    ESP_LOGW(TAG, "SYS_VERSION timed out.");
    this->publish_firmware_("Unknown");
    this->publish_stack_("Unknown");
  }
}

bool ZigbeeGatewayComponent::read_memory_word_(uint32_t address, const char *name, uint8_t out[4]) {
  uint8_t length = 0;
  const bool ok = bsl_mem_read(&this->serial_, address, /* width */ 1, /* count */ 1, out, 4, &length,
                               this->bsl_ack_timeout_ms_, this->bsl_header_timeout_ms_,
                               this->bsl_payload_timeout_ms_, 1);
  if (!ok || length < 4) {
    ESP_LOGW(TAG, "MEM_READ %s addr=0x%08X FAILED (len=%u)", name, (unsigned) address,
             (unsigned) length);
    return false;
  }
  ESP_LOGV(TAG, "MEM_READ %s @0x%08X -> %02X %02X %02X %02X", name, (unsigned) address, out[0],
           out[1], out[2], out[3]);
  return true;
}

bool ZigbeeGatewayComponent::detect_chip_info_(ChipInfo *chip) {
  // This is a functional replica of XZG CCTools::detectChipInfo. The
  // register addresses and interpretation are kept beside the implementation
  // because they describe the supported TI chip/storage layouts.
  static constexpr uint32_t ICEPICK_DEVICE_ID = 0x50001318u;
  static constexpr uint32_t FCFG_USER_ID = 0x50001294u;
  static constexpr uint32_t FLASH_SIZE_REG = 0x4003002Cu;
  static constexpr uint32_t ADDR_IEEE_PRIMARY = 0x500012F0u;
  static constexpr uint8_t PROTO_MASK_IEEE = 0x04u;

  // GET_CHIP_ID (0x28): CCTools reconstructs a big-endian 32-bit value,
  // then treats the first two response bytes as the 16-bit chip identifier.
  const uint8_t get_chip_id[] = {0x03, 0x28, 0x28};
  uint8_t response[16] = {0};
  const bool chip_id_ok = bsl_exec(
      &this->serial_, get_chip_id, sizeof(get_chip_id),
      [chip](const uint8_t *payload, uint8_t length) {
        if (length < 4)
          return;
        chip->chip_id_be = (static_cast<uint32_t>(payload[0]) << 24) |
                           (static_cast<uint32_t>(payload[1]) << 16) |
                           (static_cast<uint32_t>(payload[2]) << 8) | static_cast<uint32_t>(payload[3]);
        chip->chip_id_16 = (static_cast<uint16_t>(payload[0]) << 8) | payload[1];
      },
      response, sizeof(response), this->bsl_ack_timeout_ms_, this->bsl_header_timeout_ms_,
      this->bsl_payload_timeout_ms_, 1);
  if (!chip_id_ok) {
    ESP_LOGW(TAG, "GET_CHIP_ID timed out.");
    return false;
  }
  ESP_LOGD(TAG, "CHIP_ID(be)=0x%08X chip_id_16=0x%04X", (unsigned) chip->chip_id_be,
           (unsigned) chip->chip_id_16);

  // ICEPICK_DEVICE_ID contains the wafer identifier and PG revision.
  uint8_t device_id[4] = {0};
  if (!this->read_memory_word_(ICEPICK_DEVICE_ID, "ICEPICK_DEVICE_ID", device_id))
    return false;
  chip->wafer_id = ((((static_cast<uint32_t>(device_id[3]) & 0x0F) << 16) |
                     (static_cast<uint32_t>(device_id[2]) << 8) |
                     (static_cast<uint32_t>(device_id[1]) & 0xF0)) >>
                    4);
  chip->pg_rev = (static_cast<uint32_t>(device_id[3]) & 0xF0) >> 4;
  ESP_LOGD(TAG, "wafer_id=0x%05X pg_rev=0x%X", (unsigned) chip->wafer_id, (unsigned) chip->pg_rev);

  // FCFG_USER_ID also describes storage/radio features:
  //   bit 25: 0=no 20 dBm PA support, 1=20 dBm PA supported
  //   bit 23: 0=CC26xx device type, 1=CC13xx device type
  // The high nibble used here matches CCTools' protocol capability check.
  uint8_t user_id[4] = {0};
  if (!this->read_memory_word_(FCFG_USER_ID, "FCFG_USER_ID", user_id))
    return false;
  chip->protocols = user_id[1] >> 4;
  ESP_LOGD(TAG, "protocols=0x%02X (FCFG_USER_ID=%02X %02X %02X %02X)", chip->protocols, user_id[0],
           user_id[1], user_id[2], user_id[3]);

  // FLASH_SIZE[0] reports the number of physical flash pages. The byte count
  // depends on the family-specific page size determined below.
  uint8_t flash_size_reg[4] = {0};
  if (!this->read_memory_word_(FLASH_SIZE_REG, "FLASH_SIZE", flash_size_reg))
    return false;
  chip->flash_pages = flash_size_reg[0];

  // Chip/storage-family detection
  // -----------------------------
  // Compare the CCFG BL_CONFIG mirror at 0x50004FD8 with the absolute
  // BL_CONFIG location at the end of each supported flash layout:
  //
  //   CC13x2/CC26x2     -> 0x00057FD8, 4 KiB erase pages
  //   CC13x2x7/CC26x2x7 -> 0x000AFFD8, 8 KiB erase pages
  //
  // If the mirror is unreadable or erased, fall back to whichever absolute
  // address contains a non-erased word. Ambiguous results stay UNKNOWN.
  uint8_t bl_mirror[4] = {0};
  uint8_t bl_x2[4] = {0};
  uint8_t bl_x7[4] = {0};
  const bool mirror_ok = this->read_memory_word_(0x50004FD8u, "BL_CONFIG_MIRROR", bl_mirror);
  const bool x2_ok = this->read_memory_word_(0x00057FD8u, "BL_CONFIG_X2", bl_x2);
  const bool x7_ok = this->read_memory_word_(0x000AFFD8u, "BL_CONFIG_X7", bl_x7);

  const auto equal4 = [](const uint8_t a[4], const uint8_t b[4]) {
    return std::equal(a, a + 4, b);
  };
  const auto erased4 = [](const uint8_t value[4]) {
    return value[0] == 0xFF && value[1] == 0xFF && value[2] == 0xFF && value[3] == 0xFF;
  };

  const bool mirror_is_x2 = mirror_ok && x2_ok && !erased4(bl_mirror) && !erased4(bl_x2) &&
                            equal4(bl_mirror, bl_x2);
  const bool mirror_is_x7 = mirror_ok && x7_ok && !erased4(bl_mirror) && !erased4(bl_x7) &&
                            equal4(bl_mirror, bl_x7);
  if (mirror_is_x2 != mirror_is_x7) {
    chip->family = mirror_is_x2 ? ChipFamily::CC13X2_CC26X2 : ChipFamily::CC13X2X7_CC26X2X7;
  } else {
    const bool x2_present = x2_ok && !erased4(bl_x2);
    const bool x7_present = x7_ok && !erased4(bl_x7);
    if (x2_present != x7_present)
      chip->family = x2_present ? ChipFamily::CC13X2_CC26X2 : ChipFamily::CC13X2X7_CC26X2X7;
  }

  const char *family_name = chip->family == ChipFamily::CC13X2_CC26X2
                                ? "cc13x2_cc26x2"
                                : (chip->family == ChipFamily::CC13X2X7_CC26X2X7
                                       ? "cc13x2x7_cc26x2x7"
                                       : "unknown");
  ESP_LOGI(TAG, "Detected chip family: %s", family_name);

  // XZG probes 0x00057FB4 on x2 and uses byte 1 to refine the human-readable
  // CC2652P2 label. On x2x7 that address is application space, so do not read
  // or interpret it there.
  uint8_t mode_probe[4] = {0};
  uint8_t mode_byte = 0;
  if (chip->family == ChipFamily::CC13X2_CC26X2) {
    (void) this->read_memory_word_(0x00057FB4u, "MODE_BYTE_PROBE", mode_probe);
    mode_byte = mode_probe[1];
  }

  if (chip->chip_id_16 == 0xB964 || chip->chip_id_16 == 0xB965) {
    chip->hardware = "CC2538";
  } else if (chip->chip_id_16 == 0x1202 && chip->wafer_id == 0xBB77 && chip->pg_rev == 0x1) {
    chip->hardware = "CC2652P7";
  } else if (chip->chip_id_16 == 0x3202 && chip->wafer_id == 0xBB41 && chip->pg_rev == 0x3 &&
             mode_byte == 0xC1) {
    chip->hardware = "CC2652P2_launchpad";
  } else if (chip->chip_id_16 == 0x3202 && chip->wafer_id == 0xBB41 && chip->pg_rev == 0x3 &&
             mode_byte == 0xFA) {
    chip->hardware = "CC2652P2_other";
  } else if (chip->chip_id_16 == 0x3202 && chip->wafer_id == 0xBB41 && chip->pg_rev == 0x3) {
    chip->hardware = "CC2652P2_";
  } else if (chip->chip_id_16 == 0x3102 && chip->wafer_id == 0xBB41 && chip->pg_rev == 0x3) {
    chip->hardware = "CC2652RB";
  } else {
    char description[80];
    snprintf(description, sizeof(description), "Unknown (C: %X, W: %X, P: %X, M: %X)",
             (unsigned) chip->chip_id_16, (unsigned) chip->wafer_id, (unsigned) chip->pg_rev,
             (unsigned) mode_byte);
    chip->hardware = description;
  }
  ESP_LOGI(TAG, "Hardware: %s (mode byte=0x%02X)", chip->hardware.c_str(), mode_byte);
  this->publish_hardware_(chip->hardware.c_str());

  // x2x7 devices use 8 KiB flash pages; x2 devices use 4 KiB pages. Preserve
  // the working baseline's 4 KiB fallback for UNKNOWN, but make it visible.
  const uint32_t page_size = chip->family == ChipFamily::CC13X2X7_CC26X2X7 ? 8192 : 4096;
  if (chip->family == ChipFamily::UNKNOWN)
    ESP_LOGW(TAG, "Unknown chip family; retaining the legacy 4 KiB page-size fallback.");
  chip->flash_size_bytes = static_cast<uint32_t>(chip->flash_pages) * page_size;
  ESP_LOGI(TAG, "FLASH pages=%u page_size=%u -> flash_size=%u bytes (~%u KiB)",
           (unsigned) chip->flash_pages, (unsigned) page_size, (unsigned) chip->flash_size_bytes,
           (unsigned) (chip->flash_size_bytes / 1024));
  this->publish_flash_size_(chip->flash_size_bytes);

  // CCTools derives MODE_CFG and BSL_CFG from the final 88-byte CCFG area:
  //   MODE_CFG = flash_end - 88 + 0x0C
  //   BSL_CFG  = flash_end - 88 + 0x30
  const uint32_t mode_cfg_address = chip->flash_size_bytes - 88 + 0x0C;
  const uint32_t bsl_cfg_address = chip->flash_size_bytes - 88 + 0x30;
  uint8_t mode_raw[4] = {0};
  if (this->read_memory_word_(mode_cfg_address, "MODE_CFG", mode_raw)) {
    chip->mode_cfg = decode_u32_be(mode_raw);
    ESP_LOGI(TAG, "modeCfg=0x%08X", (unsigned) chip->mode_cfg);
  }

  uint8_t bsl_raw[4] = {0};
  if (this->read_memory_word_(bsl_cfg_address, "BSL_CFG", bsl_raw)) {
    chip->bsl_cfg = decode_u32_be(bsl_raw);
    ESP_LOGI(TAG, "bslCfg=0x%08X", (unsigned) chip->bsl_cfg);

    // If the factory configuration reports IEEE support, read the primary
    // EUI-64 as two words and format it exactly like XZG/CCTools.
    if ((chip->protocols & PROTO_MASK_IEEE) == PROTO_MASK_IEEE) {
      uint8_t ieee_msw[4] = {0};
      uint8_t ieee_lsw[4] = {0};
      if (this->read_memory_word_(ADDR_IEEE_PRIMARY + 4, "IEEE_MSW", ieee_msw) &&
          this->read_memory_word_(ADDR_IEEE_PRIMARY, "IEEE_LSW", ieee_lsw)) {
        char ieee[24];
        snprintf(ieee, sizeof(ieee), "%02X:%02X:%02X:%02X:%02X:%02X:%02X:%02X", ieee_msw[3],
                 ieee_msw[2], ieee_msw[1], ieee_msw[0], ieee_lsw[3], ieee_lsw[2], ieee_lsw[1],
                 ieee_lsw[0]);
        ESP_LOGI(TAG, "IEEE (BSL): %s", ieee);
        this->publish_self_ieee_(ieee);
      } else {
        ESP_LOGW(TAG, "IEEE primary-address read failed.");
      }
    } else {
      ESP_LOGD(TAG, "Skipping IEEE read: protocols=0x%02X mask=0x%02X", chip->protocols, PROTO_MASK_IEEE);
    }
  }

  this->capture_chip_info_(*chip);
  return true;
}

void ZigbeeGatewayComponent::scan_nv_(ChipFamily family) {
  // NVOCMP region selection
  // -----------------------
  // Koenkk's patched Z-Stack layouts used by the working configuration:
  //   CC13x2/CC26x2     base 0x00050000, size 0x6000
  //   CC13x2x7/CC26x2x7 base 0x000A6000, size 0x8000
  //
  // Preserve the existing x2 fallback for UNKNOWN until the component/UART
  // design discussion establishes explicit unsupported-family behavior.
  const bool x7 = family == ChipFamily::CC13X2X7_CC26X2X7;
  const uint32_t nv_base = x7 ? this->nv_base_cc26x2x7_ : this->nv_base_cc26x2_;
  const uint32_t nv_size = x7 ? this->nv_size_cc26x2x7_ : this->nv_size_cc26x2_;
  if (family == ChipFamily::UNKNOWN)
    ESP_LOGW(TAG, "Unknown chip family; retaining the legacy CC13x2/CC26x2 NV-region fallback.");
  ESP_LOGI(TAG, "NV region: base=0x%06X size=0x%04X (%u KiB)", (unsigned) nv_base,
           (unsigned) nv_size, (unsigned) (nv_size / 1024));

  NzgNvScanConfig config{
      .nv_base = nv_base,
      .nv_size = nv_size,
      .ack_timeout_ms = this->bsl_ack_timeout_ms_,
      .header_timeout_ms = this->bsl_header_timeout_ms_,
      .payload_timeout_ms = this->bsl_payload_timeout_ms_,
  };

  enum WantedIndex : int {
    LOGICAL_TYPE = 0,
    BDB_ON_NETWORK,
    NIB,
    WANTED_COUNT,
  };
  NzgNvWanted wanted[WANTED_COUNT] = {
      {1, 0x0000, 0x0087, "ZCD_NV_LOGICAL_TYPE", false},
      {1, 0x0000, 0x0055, "ZCD_NV_BDBNODEISONANETWORK", false},
      {1, 0x0000, 0x0021, "ZCD_NV_NIB", false},
  };

  const auto handle_logical_type = [this](const uint8_t *payload, uint16_t used) {
    // Logical node type: 0=Coordinator, 1=Router, 2=End Device.
    if (used < 1) {
      ESP_LOGW(TAG, "ZCD_NV_LOGICAL_TYPE payload too short (got=%u, expected>=1)", (unsigned) used);
      return;
    }
    const char *role = "Unknown";
    switch (payload[0]) {
      case 0x00:
        role = "Coordinator";
        break;
      case 0x01:
        role = "Router";
        break;
      case 0x02:
        role = "End Device";
        break;
    }
    ESP_LOGI(TAG, "ZCD_NV_LOGICAL_TYPE: logical_type=0x%02X -> %s", payload[0], role);
    this->publish_role_(role);
  };

  const auto handle_bdb_on_network = [this](const uint8_t *payload, uint16_t used) {
    // BDBNODEISONANETWORK: 0=not on a network, nonzero=on a network.
    if (used < 1) {
      ESP_LOGW(TAG, "ZCD_NV_BDBNODEISONANETWORK payload too short (got=%u, expected>=1)",
               (unsigned) used);
      return;
    }
    const bool on_network = payload[0] != 0;
    ESP_LOGI(TAG, "ZCD_NV_BDBNODEISONANETWORK: on_network=%s", YESNO(on_network));
    this->publish_on_network_(on_network);
  };

  const auto handle_nib = [this](const uint8_t *payload, uint16_t used) {
    // ZCD_NV_NIB field map
    // --------------------
    // The NIB is a flat NV snapshot of most of Z-Stack 3.x nwkIB_t. Koenkk's
    // CC2652 NVOCMP build stores a 116-byte payload. The map below documents
    // fields used by diagnostics; '*' marks offsets cross-checked against
    // other NV items or live network values.
    //
    // Offs  Size  Name / behavior
    // ----  ----  -----------------------------------------------------------
    // 0x00  1     nwkSequenceNum
    // 0x01  1     passiveAckTimeout
    // 0x02  1     maxBroadcastRetries
    // 0x03  1  *  MaxChildren
    // 0x04  1  *  MaxDepth
    // 0x05  1  *  MaxRouters
    // 0x06  1     dummyNeighborTable pointer placeholder
    // 0x07  1     broadcastDeliveryTime
    // 0x08  1     reportConstantCost
    // 0x09  1     routeDiscRetries
    // 0x0A  1     dummyRoutingTable pointer placeholder
    // 0x0B  1     secureAllFrames
    // 0x0C  1     securityLevel (normally 5 for Zigbee Pro)
    // 0x0D  1     symLink
    // 0x0E  1     MAC capabilityInfo
    // 0x0F  2     transactionPersistenceTime, little-endian
    // 0x11  1     nwkProtocolVersion
    // 0x12  1     routeDiscoveryTime
    // 0x13  1     routeExpiryTime
    // 0x14  2  *  nwkDevAddress, little-endian
    // 0x16  1     implementation-specific
    // 0x17  1     implementation-specific
    // 0x18  1  *  nwkLogicalChannel (IEEE 802.15.4 channel)
    // 0x19  1     implementation-specific
    // 0x1A  2  *  nwkCoordAddress, little-endian
    // 0x1C  8  *  nwkCoordExtAddress, least-significant byte first
    // 0x24  2  *  nwkPanId, little-endian
    // 0x26  1     nwkState (0x00 and 0x08 observed)
    // 0x27  1     padding / implementation-specific
    // 0x28  4  *  primary channel bitmask
    // 0x2C  4     beacon/superframe/scan/battery-life settings
    // 0x30  4     allocatedRouterAddresses tracking
    // 0x34  4     allocatedEndDeviceAddresses tracking
    // 0x38  1     likely nodeDepth
    // 0x39  8  *  extendedPANID, most-significant byte first on flash
    // 0x41..0x73   security/concentrator/manager/statistics fields, opaque

    if (used >= 0x27) {
      const uint16_t device_address = payload[0x14] | (static_cast<uint16_t>(payload[0x15]) << 8);
      const uint8_t channel = payload[0x18];
      const uint8_t network_state = payload[0x26];
      const uint8_t max_children = payload[0x03];
      const uint8_t max_depth = payload[0x04];
      const uint8_t max_routers = payload[0x05];
      ESP_LOGI(TAG, "ZCD_NV_NIB: nwkDevAddress=0x%04X (%u) channel=%u nwkState=0x%02X",
               (unsigned) device_address, (unsigned) device_address, (unsigned) channel,
               (unsigned) network_state);
      ESP_LOGI(TAG, "ZCD_NV_NIB: MaxChildren=%u MaxRouters=%u MaxDepth=%u",
               (unsigned) max_children, (unsigned) max_routers, (unsigned) max_depth);
      this->publish_channel_(channel);
    }

    if (used >= 0x26) {
      const uint16_t pan_id = payload[0x24] | (static_cast<uint16_t>(payload[0x25]) << 8);
      ESP_LOGI(TAG, "ZCD_NV_NIB: nwkPanId=0x%04X (%u)", (unsigned) pan_id, (unsigned) pan_id);
      this->publish_pan_id_(pan_id);
    }

    if (used >= 0x39 + 8) {
      const uint8_t *coordinator = &payload[0x1C];
      char coordinator_string[24];
      snprintf(coordinator_string, sizeof(coordinator_string), "%02X:%02X:%02X:%02X:%02X:%02X:%02X:%02X",
               coordinator[7], coordinator[6], coordinator[5], coordinator[4], coordinator[3], coordinator[2],
               coordinator[1], coordinator[0]);
      ESP_LOGI(TAG, "ZCD_NV_NIB: nwkCoordExtAddress=%s", coordinator_string);
      this->publish_parent_ieee_(coordinator_string);

      const uint8_t *extended_pan = &payload[0x39];
      ESP_LOGI(TAG, "ZCD_NV_NIB: extendedPANID(msb)=%02X %02X %02X %02X %02X %02X %02X %02X",
               extended_pan[0], extended_pan[1], extended_pan[2], extended_pan[3], extended_pan[4],
               extended_pan[5], extended_pan[6], extended_pan[7]);

      // Publish decimal bytes in LSB-first order to match Zigbee2MQTT's
      // ext_pan_id presentation. Forty bytes safely cover eight "255" values,
      // seven separators, and the terminating NUL.
      char extended_pan_decimal[40] = {0};
      size_t position = 0;
      for (int index = 7; index >= 0; index--) {
        const int written =
            snprintf(extended_pan_decimal + position, sizeof(extended_pan_decimal) - position,
                     index > 0 ? "%u " : "%u", static_cast<unsigned>(extended_pan[index]));
        if (written < 0 || static_cast<size_t>(written) >= sizeof(extended_pan_decimal) - position)
          break;
        position += static_cast<size_t>(written);
      }
      this->publish_extended_pan_id_(extended_pan_decimal);
    }
  };

  const auto frontend = [&](int index, const NzgNvItemHeader &, const uint8_t *payload, uint16_t used) {
    switch (index) {
      case LOGICAL_TYPE:
        handle_logical_type(payload, used);
        break;
      case BDB_ON_NETWORK:
        handle_bdb_on_network(payload, used);
        break;
      case NIB:
        handle_nib(payload, used);
        break;
    }
  };

  // protocol.h owns the detailed NVOCMP page/header/item layout, active-item
  // filtering, CRC-8 validation, and BSL windowed reads. This frontend only
  // selects and decodes the three diagnostic items above.
  nzg_nv_scan_and_dispatch(&this->serial_, config, wanted, WANTED_COUNT, frontend);
}

#ifdef USE_UART_DEBUGGER
void ZigbeeGatewayComponent::reset_sniffer_(ZnpSnifferState &state) {
  state.state = 0;
  state.length = 0;
  state.cmd0 = 0;
  state.cmd1 = 0;
  state.index = 0;
}

void ZigbeeGatewayComponent::sniff_byte_(uart::UARTDirection direction, uint8_t byte) {
  if (!this->sniffer_enabled_) {
    this->reset_sniffer_(this->tx_sniffer_);
    this->reset_sniffer_(this->rx_sniffer_);
    return;
  }

  ZnpSnifferState &state = direction == uart::UART_DIRECTION_TX ? this->tx_sniffer_ : this->rx_sniffer_;
  switch (state.state) {
    case 0:  // SEEK_SOF
      if (byte == 0xFE) {
        state.state = 1;
        state.index = 0;
      }
      return;
    case 1:  // LEN
      state.length = byte;
      state.state = 2;
      return;
    case 2:  // CMD0
      state.cmd0 = byte;
      state.state = 3;
      return;
    case 3:  // CMD1
      state.cmd1 = byte;
      state.state = state.length == 0 ? 5 : 4;
      return;
    case 4:  // PAYLOAD
      if (state.index < state.payload.size())
        state.payload[state.index++] = byte;
      if (state.index >= state.length)
        state.state = 5;
      return;
    case 5: {  // FCS
      uint8_t expected_fcs = state.length ^ state.cmd0 ^ state.cmd1;
      for (uint16_t index = 0; index < state.length; index++)
        expected_fcs ^= state.payload[index];
      const bool fcs_ok = expected_fcs == byte;

      char payload_hex[128] = {0};
      size_t position = 0;
      for (uint16_t index = 0; index < state.length && position + 3 < sizeof(payload_hex); index++)
        position += snprintf(payload_hex + position, sizeof(payload_hex) - position, "%02X", state.payload[index]);

      const uint8_t command_type = (state.cmd0 >> 5) & 0x07;
      const char *type_name =
          command_type == 1 ? "SREQ" : (command_type == 2 ? "AREQ" : (command_type == 3 ? "SRSP" : "UNK"));
      ESP_LOGV("znp-sniff", "[%s] %s sub=0x%02X cmd1=0x%02X len=%u fcs=%s payload=%s",
               direction == uart::UART_DIRECTION_TX ? "TX" : "RX", type_name, state.cmd0 & 0x1F, state.cmd1,
               state.length, fcs_ok ? "OK" : "BAD", payload_hex);

      if (fcs_ok && command_type == 1 && (state.cmd0 & 0x1F) == 0x01) {
        // Genuine Yepiq diagnostic: passively observe both supported ZNP
        // commands that change radio TX power.
        int8_t dbm = 0;
        bool matched = false;
        if (state.cmd1 == 0x14 && state.length == 1) {
          // SYS.SetTxPower: payload[0] is signed dBm.
          dbm = static_cast<int8_t>(state.payload[0]);
          matched = true;
          ESP_LOGI("znp-sniff", "Detected SYS.SetTxPower -> %d dBm", dbm);
        } else if (state.cmd1 == 0x0F && state.length >= 2 && state.payload[0] == 0x00) {
          // SYS.StackTune operation 0x00 (TX_POWER): payload[1] is signed dBm.
          dbm = static_cast<int8_t>(state.payload[1]);
          matched = true;
          ESP_LOGI("znp-sniff", "Detected SYS.StackTune(TX_POWER) -> %d dBm", dbm);
        }
        if (matched && this->tx_power_sensor_ != nullptr)
          this->tx_power_sensor_->publish_state(static_cast<float>(dbm));
      }

      this->reset_sniffer_(state);
      return;
    }
    default:
      this->reset_sniffer_(state);
      return;
  }
}
#endif

void ZigbeeGatewayComponent::register_web_handlers_() {
  if (this->web_handlers_registered_)
    return;

#if defined(USE_WEBSERVER) && defined(USE_NETWORK)
  auto *server = web_server_base::global_web_server_base;
  if (server == nullptr)
    return;

  // Compatibility endpoints retained from the working gateway/XZG behavior.
  // The command is coordinated with the TCP session manager before pins move;
  // remote BSL payload remains opaque and owned by the external flash tool.
  server->add_handler(new GatewayCommandHandler("/cmdZigBSL", [this]() { this->request_bsl(); }));
  server->add_handler(new GatewayCommandHandler("/cmdZigRST", [this]() { this->request_restart(); }));
  ESP_LOGI(TAG, "Registered web endpoints: /cmdZigBSL, /cmdZigRST");
#endif

  this->web_handlers_registered_ = true;
}

}  // namespace zigbee_gateway
}  // namespace esphome
