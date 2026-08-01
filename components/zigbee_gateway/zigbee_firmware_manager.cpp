#include "zigbee_firmware_manager.h"
#include "protocol.h"
#include "zigbee_crc32.h"
#include "zigbee_firmware_target.h"
#include "zigbee_gateway.h"
#include "zigbee_ti_image.h"

#include "esphome/components/json/json_util.h"
#include "esphome/components/network/util.h"
#include "esphome/core/log.h"

#include <ArduinoJson.h>
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <inttypes.h>
#include <iterator>
#include <strings.h>

#include <esp_crt_bundle.h>
#include <esp_system.h>

namespace esphome::zigbee_gateway {

static const char *const TAG = "zigbee_firmware";
static constexpr uint32_t RADIO_BSL_SYNC_ACK_TIMEOUT_MS = 1000;
static constexpr uint32_t RADIO_BSL_STATUS_ACK_TIMEOUT_MS = 250;
static constexpr uint32_t RADIO_BSL_HEADER_TIMEOUT_MS = 250;
static constexpr uint32_t RADIO_BSL_PAYLOAD_TIMEOUT_MS = 250;
static constexpr uint32_t RADIO_BSL_ERASE_ACK_TIMEOUT_MS = 15000;
static constexpr uint32_t RADIO_BSL_DOWNLOAD_ACK_TIMEOUT_MS = 2000;
static constexpr uint32_t RADIO_BSL_DATA_ACK_TIMEOUT_MS = 5000;
static constexpr uint32_t RADIO_BSL_CRC_ACK_TIMEOUT_MS = 5000;
static constexpr uint32_t RADIO_BSL_RESET_ACK_TIMEOUT_MS = 1000;
static constexpr uint32_t RADIO_FLASH_START_ADDRESS = 0;
static constexpr size_t RADIO_FLASH_TASK_STACK_SIZE = 8192;

void FirmwareRoleSelect::dump_config() {
  LOG_SELECT("", "Target Firmware Role", this);
}

void FirmwareVersionSelect::dump_config() {
  LOG_SELECT("", "Target Firmware Version", this);
}

void ZigbeeFirmwareManager::setup() {
  this->setup_started_ms_ = millis();
  this->preference_ =
      global_preferences->make_preference<CatalogCacheBlob>(CACHE_PREFERENCE_KEY, true);
  this->target_preference_ =
      global_preferences->make_preference<TargetSelectionBlob>(TARGET_PREFERENCE_KEY, true);
  const bool target_selection_loaded = this->load_target_selection_();

  ESP_LOGD(TAG, "Starting firmware catalog for %s", this->chip_.c_str());
  this->staging_partition_ = esp_partition_find_first(
      static_cast<esp_partition_type_t>(STAGING_PARTITION_TYPE),
      static_cast<esp_partition_subtype_t>(STAGING_PARTITION_SUBTYPE),
      STAGING_PARTITION_LABEL);
  if (this->staging_partition_ == nullptr) {
    ESP_LOGE(TAG, "Firmware staging partition '%s' was not found", STAGING_PARTITION_LABEL);
  } else if (this->staging_partition_->size < STAGING_PARTITION_SIZE) {
    ESP_LOGE(TAG, "Firmware staging partition is too small: %u bytes",
             static_cast<unsigned>(this->staging_partition_->size));
    this->staging_partition_ = nullptr;
  } else {
    ESP_LOGD(TAG, "Firmware staging partition ready: %u KiB",
             static_cast<unsigned>(this->staging_partition_->size / 1024));
    this->load_staged_image_();
    if (!target_selection_loaded && this->staged_image_valid_)
      this->adopt_staged_target_selection_();
  }

  if (this->load_cache_()) {
    this->publish_status_("Cached catalog loaded; checking upstream");
    this->apply_catalog_(this->entries_, false, "NVS cache");
  } else {
    this->entries_.clear();
    this->role_options_ = {UNAVAILABLE_OPTION};
    this->firmware_options_ = {UNAVAILABLE_OPTION};
    set_select_options_(this->role_select_, this->role_options_);
    if (this->role_select_ != nullptr)
      this->role_select_->publish_state(UNAVAILABLE_OPTION);
    set_select_options_(this->firmware_select_, this->firmware_options_);
    if (this->firmware_select_ != nullptr)
      this->firmware_select_->publish_state(UNAVAILABLE_OPTION);
    this->publish_status_(UNAVAILABLE_OPTION);
  }
  if (this->staged_image_valid_) {
    if (this->flash_status_text_sensor_ != nullptr)
      this->flash_status_text_sensor_->publish_state(
          std::string("Staged: ") + this->staged_header_.role + " " +
          this->staged_header_.version);
  } else if (this->flash_status_text_sensor_ != nullptr) {
    this->flash_status_text_sensor_->publish_state("Idle");
  }
  this->publish_flash_progress_(0);

  this->fetch_requested_ = true;
  this->force_fetch_ = false;
  ESP_LOGD(TAG, "Startup API gate is closed; HTTPS refresh is required after the network connects");
  if (network::is_connected()) {
    this->on_network_connected();
  } else {
    this->publish_status_("Waiting for network");
  }
}

void ZigbeeFirmwareManager::loop() {
  if (this->fetch_requested_ && network::is_connected() &&
      !this->fetch_running_.load(std::memory_order_acquire) &&
      !this->fetch_done_.load(std::memory_order_acquire))
    this->on_network_connected();

  if (this->fetch_done_.exchange(false, std::memory_order_acquire)) {
    this->handle_fetch_result_();
    return;
  }

  if (this->firmware_probe_done_.exchange(false, std::memory_order_acquire)) {
    this->handle_firmware_probe_result_();
    return;
  }

  if (this->firmware_download_done_.exchange(false, std::memory_order_acquire)) {
    this->handle_firmware_download_result_();
    return;
  }

  if (this->radio_flash_done_.exchange(false, std::memory_order_acquire)) {
    this->handle_radio_flash_result_();
    return;
  }

  if (this->flash_state_ == FlashState::DOWNLOADING) {
    this->update_download_progress_();
  } else if (this->flash_state_ == FlashState::PREPARING_DOWNLOAD) {
    this->update_download_progress_();
  } else if (this->flash_state_ == FlashState::CHECKING_DOWNLOAD) {
    this->advance_staged_image_check_();
  } else if (this->flash_state_ == FlashState::ENTERING_BSL) {
    if (this->radio_flash_running_.load(std::memory_order_acquire))
      this->update_radio_flash_status_();
    else if (this->gateway_ != nullptr && this->gateway_->local_firmware_update_ready())
      this->start_radio_flash_task_();
  } else if (this->flash_state_ == FlashState::ERASING ||
             this->flash_state_ == FlashState::WRITING ||
             this->flash_state_ == FlashState::VERIFYING) {
    this->update_radio_flash_status_();
  } else if (this->flash_state_ == FlashState::RESTARTING &&
             this->gateway_ != nullptr &&
             !this->gateway_->local_firmware_update_active()) {
    this->finish_firmware_update_();
  }

}

bool ZigbeeFirmwareManager::can_proceed() {
  if (this->startup_gate_released_)
    return true;

  if (millis() - this->setup_started_ms_ < this->startup_timeout_ms_)
    return false;

  ESP_LOGW(TAG, "Startup catalog timeout reached after %" PRIu32 " ms", this->startup_timeout_ms_);
  if (this->cache_valid_) {
    this->publish_status_("Startup refresh timed out; using cached catalog");
  } else {
    this->publish_status_(UNAVAILABLE_OPTION);
  }
  this->release_startup_gate_("startup timeout");
  return true;
}

void ZigbeeFirmwareManager::dump_config() {
  ESP_LOGCONFIG(TAG,
                "Zigbee Firmware Manager:\n"
                "  Manifest: %s\n"
                "  Chip: %s\n"
                "  Preferred role: %s\n"
                "  Startup timeout: %" PRIu32 " ms\n"
                "  HTTP timeout: %" PRIu32 " ms\n"
                "  Maximum manifest size: %u bytes\n"
                "  Bootloader backdoor: DIO %u, active %s\n"
                "  Firmware staging: %s\n"
                "  Maximum firmware size: %u bytes\n"
                "  Cache valid: %s\n"
                "  Cached payload: %" PRIu32 " bytes\n"
                "  Entries: %u",
                this->manifest_url_.c_str(), this->chip_.c_str(), this->preferred_role_.c_str(),
                this->startup_timeout_ms_, this->http_timeout_ms_,
                static_cast<unsigned>(this->max_manifest_size_),
                static_cast<unsigned>(this->bootloader_backdoor_dio_),
                this->bootloader_backdoor_active_high_ ? "high" : "low",
                this->staging_partition_ == nullptr ? "unavailable" : STAGING_PARTITION_LABEL,
                static_cast<unsigned>(MAX_FIRMWARE_IMAGE_SIZE),
                YESNO(this->cache_valid_), this->cache_blob_.length,
                static_cast<unsigned>(this->entries_.size()));
}

void ZigbeeFirmwareManager::set_current_radio_role(const std::string &role) {
  std::string key;
  if (strcasecmp(role.c_str(), "Coordinator") == 0)
    key = "coordinator";
  else if (strcasecmp(role.c_str(), "Router") == 0)
    key = "router";

  if (key == this->current_radio_role_)
    return;

  ESP_LOGD(TAG, "Current radio role changed: %s -> %s",
           this->current_radio_role_.empty() ? "(unknown)"
                                             : this->current_radio_role_.c_str(),
           key.empty() ? "(unknown)" : key.c_str());
  this->current_radio_role_ = key;

  // Saved user intent (including a migrated staged target) must not be
  // overwritten by later radio observations. Before setup, retaining the role
  // is enough; apply_catalog_ will select it once options are available.
  if (this->target_selection_valid_ || this->entries_.empty() || key.empty() ||
      std::find(this->role_keys_.begin(), this->role_keys_.end(), key) ==
          this->role_keys_.end() ||
      this->active_role_ == key)
    return;

  this->active_role_ = key;
  if (this->role_select_ != nullptr)
    this->role_select_->publish_state(this->role_display_from_key_(key));
  const bool options_changed = this->rebuild_firmware_options_();
  if (options_changed && this->firmware_select_ != nullptr)
    this->schedule_api_reconnect_("current radio role changed");
}

void ZigbeeFirmwareManager::set_target_role(const std::string &display_role) {
  if (this->flash_active_()) {
    ESP_LOGW(TAG, "Target role cannot change while a firmware update is active");
    if (this->role_select_ != nullptr)
      this->role_select_->publish_state(this->role_display_from_key_(this->active_role_));
    return;
  }
  const std::string role = this->role_key_from_display_(display_role);
  if (role.empty()) {
    ESP_LOGW(TAG, "Ignoring unknown target role: %s", display_role.c_str());
    return;
  }
  if (role == this->active_role_)
    return;

  if (!this->selected_firmware_label_.empty() &&
      this->selected_firmware_label_ != SELECT_OPTION &&
      this->selected_firmware_label_ != UNAVAILABLE_OPTION)
    this->remembered_firmware_[this->active_role_] = this->selected_firmware_label_;

  ESP_LOGD(TAG, "Target role changed: %s -> %s", this->active_role_.c_str(), role.c_str());
  this->active_role_ = role;
  if (this->role_select_ != nullptr)
    this->role_select_->publish_state(display_role);
  const bool options_changed = this->rebuild_firmware_options_();
  this->persist_current_target_selection_();
  if (options_changed && this->firmware_select_ != nullptr)
    this->schedule_api_reconnect_("target role changed");
}

void ZigbeeFirmwareManager::set_target_firmware(const std::string &display_version) {
  if (this->flash_active_()) {
    ESP_LOGW(TAG, "Target firmware cannot change while a firmware update is active");
    if (this->firmware_select_ != nullptr)
      this->firmware_select_->publish_state(this->selected_firmware_label_);
    return;
  }
  ESP_LOGD(TAG, "Target firmware selected for role '%s': %s", this->active_role_.c_str(),
           display_version.c_str());
  if (display_version == UNAVAILABLE_OPTION) {
    if (this->firmware_select_ != nullptr)
      this->firmware_select_->publish_state(this->selected_firmware_label_);
    return;
  }
  if (display_version == SELECT_OPTION) {
    this->remembered_firmware_.erase(this->active_role_);
    this->selected_firmware_label_ = SELECT_OPTION;
    this->save_target_selection_(this->active_role_, "", "");
  } else {
    const auto *entry = this->find_entry_(this->active_role_, display_version);
    if (entry == nullptr) {
      ESP_LOGW(TAG, "Cannot persist unknown target firmware: %s", display_version.c_str());
      if (this->firmware_select_ != nullptr)
        this->firmware_select_->publish_state(this->selected_firmware_label_);
      return;
    }
    this->remembered_firmware_[this->active_role_] = display_version;
    this->selected_firmware_label_ = display_version;
    this->save_target_selection_(entry->role, entry->version, entry->filename);
  }
  if (this->firmware_select_ != nullptr)
    this->firmware_select_->publish_state(this->selected_firmware_label_);
  this->publish_selected_details_(display_version);
}

void ZigbeeFirmwareManager::on_network_connected() {
  ESP_LOGD(TAG, "Network connected; evaluating pending catalog refresh");
  if (!this->fetch_requested_ || this->fetch_running_.load(std::memory_order_acquire) ||
      this->fetch_done_.load(std::memory_order_acquire))
    return;

  if (!this->start_fetch_()) {
    this->fetch_requested_ = false;
    this->publish_catalog_check_failed_(true);
    this->publish_status_(this->cache_valid_ ? "Refresh task failed; using cached catalog" : UNAVAILABLE_OPTION);
    this->release_startup_gate_("fetch task could not start");
  }
}

void ZigbeeFirmwareManager::request_refresh(bool force) {
  if (this->flash_active_()) {
    ESP_LOGW(TAG, "Catalog refresh ignored while a firmware update is active");
    return;
  }
  if (this->fetch_requested_ || this->fetch_running_.load(std::memory_order_acquire) ||
      this->fetch_done_.load(std::memory_order_acquire)) {
    ESP_LOGD(TAG, "Catalog refresh ignored because another refresh is active");
    return;
  }
  this->force_fetch_ = force;
  this->fetch_requested_ = true;
  ESP_LOGD(TAG, "%s catalog refresh requested", force ? "Full" : "Conditional");
  if (network::is_connected()) {
    this->on_network_connected();
  } else {
    this->publish_status_("Waiting for network");
  }
}

void ZigbeeFirmwareManager::start_firmware_update() {
  if (this->flash_active_()) {
    ESP_LOGW(TAG, "A Zigbee firmware update is already active");
    return;
  }
  if (this->staging_partition_ == nullptr) {
    this->fail_firmware_update_("Firmware staging unavailable");
    return;
  }
  if (this->fetch_requested_ || this->fetch_running_.load(std::memory_order_acquire) ||
      this->fetch_done_.load(std::memory_order_acquire)) {
    this->fail_firmware_update_("Catalog refresh is active");
    return;
  }
  const auto *selected =
      this->find_entry_(this->active_role_, this->selected_firmware_label_);
  if (selected == nullptr) {
    this->fail_firmware_update_("Select firmware first");
    return;
  }

  this->flash_entry_ = *selected;
  this->flash_started_ms_ = millis();
  this->flash_image_size_ = 0;
  this->flash_image_digest_.fill(0);
  this->last_flash_progress_ = 255;
  this->next_flash_progress_log_ = 10;
  this->publish_flash_progress_(0);
  ESP_LOGI(TAG, "Zigbee firmware update started: role=%s, version=%s, file=%s, baud=%" PRIu32,
           this->flash_entry_.role.c_str(), this->flash_entry_.version.c_str(),
           this->flash_entry_.filename.c_str(), this->flash_entry_.baud_rate);
  if (this->staged_image_matches_(this->flash_entry_)) {
    this->flash_image_size_ = this->staged_header_.image_size;
    this->flash_image_digest_ = this->staged_header_.digest;
    if (!network::is_connected()) {
      ESP_LOGW(TAG, "Firmware freshness check skipped because the network is unavailable");
      this->use_staged_firmware_("network unavailable");
      return;
    }
    if (!this->start_firmware_probe_()) {
      ESP_LOGW(TAG, "Firmware freshness task could not start; using staged image");
      this->use_staged_firmware_("freshness task unavailable");
    }
    return;
  }

  if (!network::is_connected()) {
    this->fail_firmware_update_("Network unavailable and selected firmware is not staged");
    return;
  }
  ESP_LOGD(TAG, "Selected firmware is not staged; downloading %s",
           this->flash_entry_.filename.c_str());
  if (!this->start_firmware_download_())
    this->fail_firmware_update_("Could not start download task");
}

void ZigbeeFirmwareManager::invalidate_staged_firmware() {
  if (this->flash_active_()) {
    ESP_LOGW(TAG, "Staged firmware cannot be invalidated while an update is active");
    return;
  }
  if (!this->invalidate_staged_firmware_("manual request", true)) {
    if (this->flash_status_text_sensor_ != nullptr)
      this->flash_status_text_sensor_->publish_state("Failed to invalidate staged firmware");
  } else {
    ESP_LOGI(TAG, "Staged firmware invalidated by user request");
  }
}

void ZigbeeFirmwareManager::fetch_task_entry_(void *parameter) {
  auto *self = static_cast<ZigbeeFirmwareManager *>(parameter);
  self->fetch_task_();
  vTaskDelete(nullptr);
}

esp_err_t ZigbeeFirmwareManager::http_event_handler_(esp_http_client_event_t *event) {
  auto *self = static_cast<ZigbeeFirmwareManager *>(event->user_data);
  if (self == nullptr)
    return ESP_FAIL;

  switch (event->event_id) {
    case HTTP_EVENT_ON_HEADER:
      if (event->header_key != nullptr && event->header_value != nullptr &&
          strcasecmp(event->header_key, "etag") == 0)
        self->fetch_result_.etag = event->header_value;
      if (event->header_key != nullptr && event->header_value != nullptr &&
          strcasecmp(event->header_key, "content-type") == 0)
        self->fetch_result_.content_type = event->header_value;
      break;
    case HTTP_EVENT_ON_DATA:
      if (event->data_len <= 0)
        break;
      if (self->fetch_result_.body.size() + static_cast<size_t>(event->data_len) >
          self->max_manifest_size_) {
        self->fetch_result_.body_too_large = true;
        ESP_LOGE(TAG, "Manifest exceeds configured limit of %u bytes",
                 static_cast<unsigned>(self->max_manifest_size_));
        return ESP_FAIL;
      }
      self->fetch_result_.body.append(static_cast<const char *>(event->data), event->data_len);
      break;
    default:
      break;
  }
  return ESP_OK;
}

void ZigbeeFirmwareManager::fetch_task_() {
  const uint32_t started = millis();
  ESP_LOGD(TAG, "Catalog refresh started; conditional=%s",
           YESNO(this->cache_valid_ && !this->force_fetch_));

  esp_http_client_config_t config{};
  config.url = this->manifest_url_.c_str();
  config.method = HTTP_METHOD_GET;
  config.timeout_ms = static_cast<int>(this->http_timeout_ms_);
  config.event_handler = &ZigbeeFirmwareManager::http_event_handler_;
  config.user_data = this;
  config.crt_bundle_attach = esp_crt_bundle_attach;
  config.user_agent = "YZG-Firmware-Catalog/1";
  config.disable_auto_redirect = false;
  config.max_redirection_count = 3;

  esp_http_client_handle_t client = esp_http_client_init(&config);
  if (client == nullptr) {
    this->fetch_result_.error = ESP_ERR_NO_MEM;
  } else {
    esp_http_client_set_header(client, "Accept", "application/json");
    if (this->cache_valid_ && !this->force_fetch_ && this->cache_blob_.etag[0] != '\0') {
      esp_http_client_set_header(client, "If-None-Match", this->cache_blob_.etag);
      ESP_LOGD(TAG, "Using cached ETag: %s", this->cache_blob_.etag);
    }

    this->fetch_result_.error = esp_http_client_perform(client);
    this->fetch_result_.status_code = esp_http_client_get_status_code(client);
    const int64_t content_length = esp_http_client_get_content_length(client);
    if (content_length > 0)
      this->fetch_result_.declared_length = static_cast<size_t>(content_length);
    esp_http_client_cleanup(client);
  }

  this->fetch_result_.duration_ms = millis() - started;
  ESP_LOGD(TAG,
           "Catalog download complete: error=%s, HTTP=%d, received=%u bytes, "
           "duration=%" PRIu32 " ms",
           esp_err_to_name(this->fetch_result_.error), this->fetch_result_.status_code,
           static_cast<unsigned>(this->fetch_result_.body.size()),
           this->fetch_result_.duration_ms);
  this->fetch_running_.store(false, std::memory_order_release);
  this->fetch_completed_ms_.store(millis(), std::memory_order_release);
  this->fetch_done_.store(true, std::memory_order_release);
}

bool ZigbeeFirmwareManager::start_fetch_() {
  this->fetch_result_ = {};
  this->fetch_requested_ = false;
  this->fetch_running_.store(true, std::memory_order_release);
  this->publish_status_(this->force_fetch_ ? "Downloading full manifest" : "Checking upstream catalog");

  BaseType_t created = xTaskCreate(&ZigbeeFirmwareManager::fetch_task_entry_, "catalog_https", 8192, this, 1, nullptr);
  if (created != pdPASS) {
    this->fetch_running_.store(false, std::memory_order_release);
    ESP_LOGE(TAG, "Could not create HTTPS task");
    return false;
  }
  return true;
}

void ZigbeeFirmwareManager::firmware_probe_task_entry_(void *parameter) {
  auto *self = static_cast<ZigbeeFirmwareManager *>(parameter);
  self->firmware_probe_task_();
  vTaskDelete(nullptr);
}

esp_err_t ZigbeeFirmwareManager::firmware_probe_http_event_handler_(
    esp_http_client_event_t *event) {
  auto *self = static_cast<ZigbeeFirmwareManager *>(event->user_data);
  if (self == nullptr)
    return ESP_FAIL;
  if (event->event_id == HTTP_EVENT_ON_HEADER && event->header_key != nullptr &&
      event->header_value != nullptr && strcasecmp(event->header_key, "etag") == 0)
    self->firmware_probe_result_.etag = event->header_value;
  return ESP_OK;
}

void ZigbeeFirmwareManager::firmware_probe_task_() {
  const uint32_t started = millis();

  esp_http_client_config_t config{};
  config.url = this->flash_entry_.url.c_str();
  config.method = HTTP_METHOD_HEAD;
  config.timeout_ms = static_cast<int>(this->http_timeout_ms_);
  config.event_handler = &ZigbeeFirmwareManager::firmware_probe_http_event_handler_;
  config.user_data = this;
  config.crt_bundle_attach = esp_crt_bundle_attach;
  config.user_agent = "YZG-Firmware-Updater/1";
  config.disable_auto_redirect = false;
  config.max_redirection_count = 3;

  esp_http_client_handle_t client = esp_http_client_init(&config);
  if (client == nullptr) {
    this->firmware_probe_result_.error = ESP_ERR_NO_MEM;
  } else {
    if (this->staged_header_.etag[0] != '\0')
      esp_http_client_set_header(client, "If-None-Match", this->staged_header_.etag);
    this->firmware_probe_result_.error = esp_http_client_perform(client);
    this->firmware_probe_result_.status_code = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);
  }

  this->firmware_probe_result_.duration_ms = millis() - started;
  this->firmware_probe_running_.store(false, std::memory_order_release);
  this->firmware_probe_done_.store(true, std::memory_order_release);
}

bool ZigbeeFirmwareManager::start_firmware_probe_() {
  this->firmware_probe_result_ = {};
  this->firmware_probe_done_.store(false, std::memory_order_release);
  this->firmware_probe_running_.store(true, std::memory_order_release);
  this->begin_flash_stage_(FlashState::CHECKING_FRESHNESS,
                           "Checking staged firmware freshness");

  BaseType_t created = xTaskCreate(&ZigbeeFirmwareManager::firmware_probe_task_entry_,
                                   "firmware_head", 6144, this, 1, nullptr);
  if (created != pdPASS) {
    this->firmware_probe_running_.store(false, std::memory_order_release);
    ESP_LOGW(TAG, "Could not create firmware freshness task");
    return false;
  }
  return true;
}

void ZigbeeFirmwareManager::handle_firmware_probe_result_() {
  const auto &result = this->firmware_probe_result_;
  if (result.error == ESP_OK && result.status_code == 200) {
    ESP_LOGI(TAG,
             "Staged firmware changed upstream: old ETag=%s, new ETag=%s; "
             "downloading replacement",
             this->staged_header_.etag[0] == '\0' ? "(none)" : this->staged_header_.etag,
             result.etag.empty() ? "(none)" : result.etag.c_str());
    if (!this->start_firmware_download_())
      this->fail_firmware_update_("Could not start replacement download");
    return;
  }

  if (result.error == ESP_OK && result.status_code == 304) {
    ESP_LOGD(TAG, "Staged firmware is current; conditional HEAD returned 304 in %" PRIu32
                  " ms",
             result.duration_ms);
    this->use_staged_firmware_("upstream unchanged");
    return;
  }

  ESP_LOGW(TAG,
           "Firmware freshness could not be confirmed; error=%s, HTTP=%d. "
           "Using the locally verified staged image",
           esp_err_to_name(result.error), result.status_code);
  this->use_staged_firmware_("freshness check failed");
}

void ZigbeeFirmwareManager::firmware_download_task_entry_(void *parameter) {
  auto *self = static_cast<ZigbeeFirmwareManager *>(parameter);
  self->firmware_download_task_();
  vTaskDelete(nullptr);
}

esp_err_t ZigbeeFirmwareManager::firmware_http_event_handler_(esp_http_client_event_t *event) {
  auto *self = static_cast<ZigbeeFirmwareManager *>(event->user_data);
  if (self == nullptr)
    return ESP_FAIL;

  if (event->event_id == HTTP_EVENT_ON_HEADER && event->header_key != nullptr &&
      event->header_value != nullptr && strcasecmp(event->header_key, "etag") == 0) {
    self->firmware_download_result_.etag = event->header_value;
    return ESP_OK;
  }

  if (event->event_id == HTTP_EVENT_ON_HEADER && event->header_key != nullptr &&
      event->header_value != nullptr && strcasecmp(event->header_key, "content-length") == 0) {
    char *end = nullptr;
    const unsigned long long parsed = std::strtoull(event->header_value, &end, 10);
    if (end != event->header_value && *end == '\0') {
      if (parsed > MAX_FIRMWARE_IMAGE_SIZE) {
        self->firmware_download_result_.body_too_large = true;
        ESP_LOGE(TAG, "Firmware image exceeds the CC2652P7 %u-byte limit",
                 static_cast<unsigned>(MAX_FIRMWARE_IMAGE_SIZE));
        return ESP_FAIL;
      }
      self->firmware_download_result_.declared_length = static_cast<size_t>(parsed);
      self->firmware_download_total_.store(static_cast<size_t>(parsed), std::memory_order_release);
    }
    return ESP_OK;
  }

  if (event->event_id != HTTP_EVENT_ON_DATA || event->data_len <= 0)
    return ESP_OK;

  const size_t incoming = static_cast<size_t>(event->data_len);
  if (self->firmware_download_result_.received_length + incoming >
      MAX_FIRMWARE_IMAGE_SIZE) {
    self->firmware_download_result_.body_too_large = true;
    ESP_LOGE(TAG, "Firmware image exceeds the CC2652P7 %u-byte limit",
             static_cast<unsigned>(MAX_FIRMWARE_IMAGE_SIZE));
    return ESP_FAIL;
  }

  const auto *data = static_cast<const uint8_t *>(event->data);
  const esp_err_t write_error = esp_partition_write(
      self->staging_partition_,
      STAGING_HEADER_SIZE + self->firmware_download_result_.received_length, data,
      incoming);
  if (write_error != ESP_OK) {
    self->firmware_download_result_.staging_error = write_error;
    ESP_LOGE(TAG, "Could not write firmware staging partition: %s",
             esp_err_to_name(write_error));
    return ESP_FAIL;
  }
  if (mbedtls_sha256_update(&self->firmware_download_sha_context_, data, incoming) != 0) {
    self->firmware_download_result_.hash_failed = true;
    ESP_LOGE(TAG, "Could not update firmware SHA-256");
    return ESP_FAIL;
  }
  self->firmware_download_result_.received_length += incoming;
  self->firmware_download_bytes_.store(
      self->firmware_download_result_.received_length, std::memory_order_release);
  return ESP_OK;
}

void ZigbeeFirmwareManager::firmware_download_task_() {
  const uint32_t started = millis();
  this->firmware_download_phase_.store(DownloadPhase::ERASING_STAGING,
                                       std::memory_order_release);
  this->firmware_staging_erase_bytes_.store(0, std::memory_order_release);

  for (size_t offset = STAGING_HEADER_SIZE; offset < this->staging_partition_->size;
       offset += STAGING_HEADER_SIZE) {
    const esp_err_t error =
        esp_partition_erase_range(this->staging_partition_, offset, STAGING_HEADER_SIZE);
    if (error != ESP_OK) {
      this->firmware_download_result_.error = error;
      this->firmware_download_result_.staging_error = error;
      ESP_LOGE(TAG, "Could not erase firmware staging partition at 0x%X: %s",
               static_cast<unsigned>(offset), esp_err_to_name(error));
      this->firmware_download_result_.duration_ms = millis() - started;
      this->firmware_download_running_.store(false, std::memory_order_release);
      this->firmware_download_done_.store(true, std::memory_order_release);
      return;
    }
    this->firmware_staging_erase_bytes_.store(offset,
                                               std::memory_order_release);
    vTaskDelay(1);
  }

  this->firmware_download_phase_.store(DownloadPhase::DOWNLOADING,
                                       std::memory_order_release);
  mbedtls_sha256_init(&this->firmware_download_sha_context_);
  if (mbedtls_sha256_starts(&this->firmware_download_sha_context_, 0) != 0) {
    this->firmware_download_result_.hash_failed = true;
    this->firmware_download_result_.error = ESP_FAIL;
    this->firmware_download_result_.duration_ms = millis() - started;
    mbedtls_sha256_free(&this->firmware_download_sha_context_);
    this->firmware_download_phase_.store(DownloadPhase::IDLE,
                                         std::memory_order_release);
    this->firmware_download_running_.store(false, std::memory_order_release);
    this->firmware_download_done_.store(true, std::memory_order_release);
    return;
  }

  esp_http_client_config_t config{};
  config.url = this->flash_entry_.url.c_str();
  config.method = HTTP_METHOD_GET;
  config.timeout_ms = static_cast<int>(this->http_timeout_ms_);
  config.event_handler = &ZigbeeFirmwareManager::firmware_http_event_handler_;
  config.user_data = this;
  config.crt_bundle_attach = esp_crt_bundle_attach;
  config.user_agent = "YZG-Firmware-Updater/1";
  config.disable_auto_redirect = false;
  config.max_redirection_count = 3;

  esp_http_client_handle_t client = esp_http_client_init(&config);
  if (client == nullptr) {
    this->firmware_download_result_.error = ESP_ERR_NO_MEM;
  } else {
    this->firmware_download_result_.error = esp_http_client_perform(client);
    this->firmware_download_result_.status_code = esp_http_client_get_status_code(client);
    const int64_t content_length = esp_http_client_get_content_length(client);
    if (content_length > 0) {
      this->firmware_download_result_.declared_length =
          static_cast<size_t>(content_length);
      this->firmware_download_total_.store(
          static_cast<size_t>(content_length), std::memory_order_release);
    }
    esp_http_client_cleanup(client);
  }

  if (!this->firmware_download_result_.hash_failed &&
      mbedtls_sha256_finish(&this->firmware_download_sha_context_,
                            this->firmware_download_result_.digest.data()) != 0) {
    this->firmware_download_result_.hash_failed = true;
    this->firmware_download_result_.error = ESP_FAIL;
  }
  mbedtls_sha256_free(&this->firmware_download_sha_context_);
  this->firmware_download_result_.duration_ms = millis() - started;
  this->firmware_download_phase_.store(DownloadPhase::IDLE, std::memory_order_release);
  this->firmware_download_running_.store(false, std::memory_order_release);
  this->firmware_download_done_.store(true, std::memory_order_release);
}

bool ZigbeeFirmwareManager::start_firmware_download_() {
  if (!this->invalidate_staged_firmware_("replacement download", false))
    return false;

  this->firmware_download_result_ = {};
  this->firmware_staging_erase_bytes_.store(0, std::memory_order_release);
  this->firmware_download_bytes_.store(0, std::memory_order_release);
  this->firmware_download_total_.store(0, std::memory_order_release);
  this->firmware_download_done_.store(false, std::memory_order_release);
  this->firmware_download_running_.store(true, std::memory_order_release);
  this->firmware_download_phase_.store(DownloadPhase::ERASING_STAGING,
                                       std::memory_order_release);
  this->finalize_staging_after_check_ = true;
  this->begin_flash_stage_(FlashState::PREPARING_DOWNLOAD, "Preparing firmware storage");

  BaseType_t created = xTaskCreate(
      &ZigbeeFirmwareManager::firmware_download_task_entry_, "firmware_https", 8192,
      this, 1, nullptr);
  if (created != pdPASS) {
    this->firmware_download_running_.store(false, std::memory_order_release);
    ESP_LOGE(TAG, "Could not create firmware download task");
    return false;
  }
  return true;
}

void ZigbeeFirmwareManager::handle_firmware_download_result_() {
  const auto &result = this->firmware_download_result_;
  if (result.staging_error != ESP_OK) {
    this->fail_firmware_update_(
        std::string("Firmware storage failed: ") + esp_err_to_name(result.staging_error));
    return;
  }
  if (result.hash_failed) {
    this->fail_firmware_update_("Firmware SHA-256 failed");
    return;
  }
  if (result.body_too_large) {
    this->fail_firmware_update_("Firmware exceeds the 704 KiB radio flash");
    return;
  }
  if (result.error != ESP_OK) {
    this->fail_firmware_update_(
        std::string("Download failed: ") + esp_err_to_name(result.error));
    return;
  }
  if (result.status_code != 200) {
    char reason[64];
    snprintf(reason, sizeof(reason), "Download failed: HTTP %d", result.status_code);
    this->fail_firmware_update_(reason);
    return;
  }
  if (result.received_length == 0) {
    this->fail_firmware_update_("Download failed: empty image");
    return;
  }
  if (result.declared_length != 0 &&
      result.declared_length != result.received_length) {
    char reason[96];
    snprintf(reason, sizeof(reason), "Download incomplete: expected %u, received %u",
             static_cast<unsigned>(result.declared_length),
             static_cast<unsigned>(result.received_length));
    this->fail_firmware_update_(reason);
    return;
  }

  this->flash_image_size_ = result.received_length;
  this->flash_image_digest_ = result.digest;
  ESP_LOGI(TAG, "Firmware downloaded: %u bytes in %" PRIu32 " ms, SHA-256=%s",
           static_cast<unsigned>(this->flash_image_size_), result.duration_ms,
           sha256_to_string_(this->flash_image_digest_).c_str());
  this->publish_flash_progress_(30);
  this->begin_staged_image_check_();
}

void ZigbeeFirmwareManager::use_staged_firmware_(const char *reason) {
  if (!this->staged_image_valid_) {
    this->fail_firmware_update_("No valid staged firmware is available");
    return;
  }
  this->flash_image_size_ = this->staged_header_.image_size;
  this->flash_image_digest_ = this->staged_header_.digest;
  this->finalize_staging_after_check_ = false;
  ESP_LOGD(TAG, "Using staged firmware: role=%s, version=%s, file=%s, reason=%s",
           this->staged_header_.role, this->staged_header_.version,
           this->staged_header_.filename, reason);
  this->publish_flash_progress_(30);
  this->begin_staged_image_check_();
}

void ZigbeeFirmwareManager::begin_staged_image_check_() {
  mbedtls_sha256_init(&this->staged_image_sha_context_);
  if (mbedtls_sha256_starts(&this->staged_image_sha_context_, 0) != 0) {
    mbedtls_sha256_free(&this->staged_image_sha_context_);
    this->fail_firmware_update_("Could not start staged image verification");
    return;
  }
  this->staged_image_sha_active_ = true;
  this->staged_image_crc_ = 0xFFFFFFFFUL;
  this->begin_flash_stage_(FlashState::CHECKING_DOWNLOAD, "Verifying staged firmware");
}

void ZigbeeFirmwareManager::advance_staged_image_check_() {
  if (!this->staged_image_sha_active_)
    return;

  if (this->flash_work_offset_ >= this->flash_image_size_) {
    std::array<uint8_t, 32> readback_digest{};
    if (mbedtls_sha256_finish(&this->staged_image_sha_context_, readback_digest.data()) != 0) {
      mbedtls_sha256_free(&this->staged_image_sha_context_);
      this->staged_image_sha_active_ = false;
      this->fail_firmware_update_("Could not finish staged image verification");
      return;
    }
    mbedtls_sha256_free(&this->staged_image_sha_context_);
    this->staged_image_sha_active_ = false;
    if (readback_digest != this->flash_image_digest_) {
      this->handle_staged_image_check_failure_("Staged firmware SHA-256 mismatch");
      return;
    }
    this->flash_image_crc_ = this->staged_image_crc_ ^ 0xFFFFFFFFUL;
    if (!this->validate_staged_image_size_())
      return;
    if (!this->validate_staged_image_ccfg_())
      return;
    if (this->finalize_staging_after_check_ && !this->write_staging_header_()) {
      this->fail_firmware_update_("Could not finalize staged firmware");
      return;
    }
    ESP_LOGD(TAG,
             "Staged firmware verified from flash: SHA-256=%s, CRC32=0x%08" PRIX32,
             sha256_to_string_(readback_digest).c_str(), this->flash_image_crc_);
    this->publish_flash_progress_(35);
    this->begin_radio_flash_();
    return;
  }

  const size_t length =
      std::min(STAGING_IO_BLOCK_SIZE, this->flash_image_size_ - this->flash_work_offset_);
  if (!this->read_staged_bytes_(this->flash_work_offset_, this->staging_scratch_, length)) {
    mbedtls_sha256_free(&this->staged_image_sha_context_);
    this->staged_image_sha_active_ = false;
    this->handle_staged_image_check_failure_("Could not read staged firmware");
    return;
  }
  if (mbedtls_sha256_update(&this->staged_image_sha_context_, this->staging_scratch_,
                            length) != 0) {
    mbedtls_sha256_free(&this->staged_image_sha_context_);
    this->staged_image_sha_active_ = false;
    this->fail_firmware_update_("Could not verify staged firmware");
    return;
  }
  this->staged_image_crc_ = zigbee_crc32_update(
      this->staged_image_crc_, this->staging_scratch_, length);
  this->flash_work_offset_ += length;
  this->publish_flash_progress_(
      static_cast<uint8_t>(30 + (this->flash_work_offset_ * 5) / this->flash_image_size_));
}

bool ZigbeeFirmwareManager::validate_staged_image_size_() {
  if (this->gateway_ == nullptr) {
    this->fail_firmware_update_("Zigbee gateway is unavailable");
    return false;
  }

  const uint32_t radio_size = this->gateway_->radio_flash_size_bytes();
  if (radio_size == 0) {
    this->fail_firmware_update_("Zigbee radio flash size is unavailable");
    return false;
  }
  if (zigbee_ti_image_size_is_compatible(this->flash_image_size_, radio_size))
    return true;

  ESP_LOGE(TAG, "Firmware image size %u does not match radio flash size %u",
           static_cast<unsigned>(this->flash_image_size_),
           static_cast<unsigned>(radio_size));
  if (this->staged_image_valid_)
    this->invalidate_staged_firmware_("incompatible image size", false);
  this->fail_firmware_update_("Firmware image does not match radio flash size");
  return false;
}

bool ZigbeeFirmwareManager::validate_staged_image_ccfg_() {
  uint8_t image_tail[ZIGBEE_TI_CCFG_SIZE]{};
  if (this->flash_image_size_ < sizeof(image_tail) ||
      !this->read_staged_bytes_(this->flash_image_size_ - sizeof(image_tail),
                                image_tail, sizeof(image_tail))) {
    this->handle_staged_image_check_failure_(
        "Could not read candidate TI CCFG");
    return false;
  }

  ZigbeeTiCcfg ccfg{};
  const ZigbeeTiCcfgError error = zigbee_ti_validate_ccfg(
      image_tail, sizeof(image_tail), this->bootloader_backdoor_dio_,
      this->bootloader_backdoor_active_high_, RADIO_FLASH_START_ADDRESS,
      &ccfg);
  ESP_LOGD(TAG,
           "Candidate TI CCFG: MODE_CONF=0x%08" PRIX32
           ", BL_CONFIG=0x%08" PRIX32 ", ERASE_CONF=0x%08" PRIX32
           ", IMAGE_VALID=0x%08" PRIX32,
           ccfg.mode_conf, ccfg.bl_config, ccfg.erase_conf, ccfg.image_valid);
  if (error == ZigbeeTiCcfgError::NONE)
    return true;

  ESP_LOGE(TAG,
           "Candidate firmware rejected before radio erase: %s "
           "(configured DIO=%u, active_%s)",
           zigbee_ti_ccfg_error_name(error),
           static_cast<unsigned>(this->bootloader_backdoor_dio_),
           this->bootloader_backdoor_active_high_ ? "high" : "low");
  if (this->staged_image_valid_)
    this->invalidate_staged_firmware_("unsafe TI CCFG", false);
  this->fail_firmware_update_(
      std::string("Unsafe Zigbee firmware: ") +
      zigbee_ti_ccfg_error_name(error));
  return false;
}

void ZigbeeFirmwareManager::handle_staged_image_check_failure_(const char *reason) {
  if (this->finalize_staging_after_check_) {
    this->fail_firmware_update_(reason);
    return;
  }

  ESP_LOGW(TAG, "%s; invalidating the cached image", reason);
  if (!network::is_connected()) {
    this->invalidate_staged_firmware_("local verification failed", false);
    this->fail_firmware_update_(
        "Staged firmware is corrupt and the network is unavailable");
    return;
  }
  if (!this->start_firmware_download_())
    this->fail_firmware_update_("Could not replace invalid staged firmware");
}

void ZigbeeFirmwareManager::begin_flash_stage_(FlashState state, const char *status) {
  this->flash_state_ = state;
  this->flash_work_offset_ = 0;
  ESP_LOGD(TAG, "Firmware update stage: %s", status);
  if (this->flash_status_text_sensor_ != nullptr)
    this->flash_status_text_sensor_->publish_state(status);
}

void ZigbeeFirmwareManager::begin_radio_flash_() {
  // Size and CCFG were validated before a newly downloaded image could be
  // finalized in staging. Recheck the immutable size here as a defensive
  // boundary immediately before UART ownership.
  if (!this->validate_staged_image_size_())
    return;
  if (!this->gateway_->begin_local_firmware_update()) {
    this->fail_firmware_update_("Could not claim the Zigbee radio");
    return;
  }
  this->begin_flash_stage_(FlashState::ENTERING_BSL,
                           "Entering radio bootloader");
}

bool ZigbeeFirmwareManager::start_radio_flash_task_() {
  if (this->radio_flash_running_.load(std::memory_order_acquire) ||
      this->radio_flash_done_.load(std::memory_order_acquire))
    return false;
  this->radio_flash_serial_ = this->gateway_->local_firmware_update_serial();
  if (this->radio_flash_serial_ == nullptr) {
    this->fail_firmware_update_("Zigbee bootloader UART is unavailable");
    this->gateway_->finish_local_firmware_update(false);
    return false;
  }

  this->radio_flash_result_ = {};
  this->radio_flash_stage_.store(RadioFlashStage::SYNCING,
                                 std::memory_order_release);
  this->radio_flash_progress_.store(35, std::memory_order_release);
  this->published_radio_flash_stage_ = RadioFlashStage::IDLE;
  this->radio_flash_done_.store(false, std::memory_order_release);
  this->radio_flash_running_.store(true, std::memory_order_release);
  BaseType_t created = xTaskCreate(
      &ZigbeeFirmwareManager::radio_flash_task_entry_, "zigbee_radio_flash",
      RADIO_FLASH_TASK_STACK_SIZE, this, 1, nullptr);
  if (created != pdPASS) {
    this->radio_flash_running_.store(false, std::memory_order_release);
    this->fail_firmware_update_("Could not create radio firmware task");
    this->gateway_->finish_local_firmware_update(false);
    return false;
  }
  this->update_radio_flash_status_();
  return true;
}

void ZigbeeFirmwareManager::radio_flash_task_entry_(void *parameter) {
  auto *self = static_cast<ZigbeeFirmwareManager *>(parameter);
  self->radio_flash_task_();
  vTaskDelete(nullptr);
}

void ZigbeeFirmwareManager::radio_flash_task_() {
  const uint32_t started = millis();
  auto finish = [this, started](RadioFlashError error, uint8_t rom_status,
                                uint32_t offset) {
    this->radio_flash_result_.error = error;
    this->radio_flash_result_.rom_status = rom_status;
    this->radio_flash_result_.offset = offset;
    this->radio_flash_result_.duration_ms = millis() - started;
    this->radio_flash_running_.store(false, std::memory_order_release);
    this->radio_flash_done_.store(true, std::memory_order_release);
  };

  auto *serial = this->radio_flash_serial_;
  if (serial == nullptr ||
      !bsl_sync(serial, RADIO_BSL_SYNC_ACK_TIMEOUT_MS)) {
    finish(RadioFlashError::BSL_SYNC_FAILED, 0, 0);
    return;
  }

  this->radio_flash_stage_.store(RadioFlashStage::ERASING,
                                 std::memory_order_release);
  this->radio_flash_progress_.store(36, std::memory_order_release);
  uint8_t rom_status = 0;
  // Bank erase also clears CCFG. Until the complete candidate is rewritten,
  // IMAGE_VALID is erased to 0xFFFFFFFF; TI Boot ROM treats that as an illegal
  // application vector and remains available through the serial bootloader.
  if (!bsl_bank_erase(serial, RADIO_BSL_ERASE_ACK_TIMEOUT_MS,
                      RADIO_BSL_STATUS_ACK_TIMEOUT_MS,
                      RADIO_BSL_HEADER_TIMEOUT_MS,
                      RADIO_BSL_PAYLOAD_TIMEOUT_MS, &rom_status)) {
    finish(RadioFlashError::BANK_ERASE_FAILED, rom_status, 0);
    return;
  }
  this->radio_flash_result_.bank_erased = true;
  this->radio_flash_progress_.store(45, std::memory_order_release);

  if (!bsl_download(serial, RADIO_FLASH_START_ADDRESS,
                    static_cast<uint32_t>(this->flash_image_size_),
                    RADIO_BSL_DOWNLOAD_ACK_TIMEOUT_MS,
                    RADIO_BSL_STATUS_ACK_TIMEOUT_MS,
                    RADIO_BSL_HEADER_TIMEOUT_MS,
                    RADIO_BSL_PAYLOAD_TIMEOUT_MS, &rom_status)) {
    finish(RadioFlashError::DOWNLOAD_FAILED, rom_status, 0);
    return;
  }

  this->radio_flash_stage_.store(RadioFlashStage::WRITING,
                                 std::memory_order_release);
  uint8_t buffer[WRITE_BLOCK_SIZE];
  uint32_t crc = 0xFFFFFFFFUL;
  size_t offset = 0;
  while (offset < this->flash_image_size_) {
    const size_t length =
        std::min(WRITE_BLOCK_SIZE, this->flash_image_size_ - offset);
    if (!this->read_staged_bytes_(offset, buffer, length)) {
      finish(RadioFlashError::STAGING_READ_FAILED, 0,
             static_cast<uint32_t>(offset));
      return;
    }
    if (!bsl_send_data(serial, buffer, static_cast<uint8_t>(length),
                       RADIO_BSL_DATA_ACK_TIMEOUT_MS,
                       RADIO_BSL_STATUS_ACK_TIMEOUT_MS,
                       RADIO_BSL_HEADER_TIMEOUT_MS,
                       RADIO_BSL_PAYLOAD_TIMEOUT_MS, &rom_status)) {
      finish(RadioFlashError::SEND_DATA_FAILED, rom_status,
             static_cast<uint32_t>(offset));
      return;
    }
    crc = zigbee_crc32_update(crc, buffer, length);
    offset += length;
    this->radio_flash_progress_.store(
        static_cast<uint8_t>(45 + (offset * 45) / this->flash_image_size_),
        std::memory_order_release);
    vTaskDelay(1);
  }
  this->radio_flash_result_.local_crc = crc ^ 0xFFFFFFFFUL;

  this->radio_flash_stage_.store(RadioFlashStage::VERIFYING,
                                 std::memory_order_release);
  this->radio_flash_progress_.store(95, std::memory_order_release);
  uint32_t radio_crc = 0;
  if (!bsl_crc32(serial, RADIO_FLASH_START_ADDRESS,
                 static_cast<uint32_t>(this->flash_image_size_),
                 RADIO_BSL_CRC_ACK_TIMEOUT_MS, RADIO_BSL_HEADER_TIMEOUT_MS,
                 RADIO_BSL_PAYLOAD_TIMEOUT_MS,
                 RADIO_BSL_STATUS_ACK_TIMEOUT_MS, &radio_crc, &rom_status)) {
    finish(RadioFlashError::CRC_COMMAND_FAILED, rom_status,
           static_cast<uint32_t>(this->flash_image_size_));
    return;
  }
  this->radio_flash_result_.radio_crc = radio_crc;
  if (radio_crc != this->radio_flash_result_.local_crc) {
    finish(RadioFlashError::CRC_MISMATCH, 0,
           static_cast<uint32_t>(this->flash_image_size_));
    return;
  }

  this->radio_flash_stage_.store(RadioFlashStage::RESTARTING,
                                 std::memory_order_release);
  this->radio_flash_progress_.store(99, std::memory_order_release);
  this->radio_flash_result_.bootloader_reset_acknowledged =
      bsl_reset(serial, RADIO_BSL_RESET_ACK_TIMEOUT_MS);
  this->radio_flash_progress_.store(100, std::memory_order_release);
  finish(RadioFlashError::NONE, 0,
         static_cast<uint32_t>(this->flash_image_size_));
}

void ZigbeeFirmwareManager::update_radio_flash_status_() {
  const RadioFlashStage stage =
      this->radio_flash_stage_.load(std::memory_order_acquire);
  if (stage != this->published_radio_flash_stage_) {
    this->published_radio_flash_stage_ = stage;
    const char *status = nullptr;
    switch (stage) {
      case RadioFlashStage::SYNCING:
        status = "Synchronizing radio bootloader";
        this->flash_state_ = FlashState::ENTERING_BSL;
        break;
      case RadioFlashStage::ERASING:
        status = "Erasing radio";
        this->flash_state_ = FlashState::ERASING;
        break;
      case RadioFlashStage::WRITING:
        status = "Writing radio";
        this->flash_state_ = FlashState::WRITING;
        break;
      case RadioFlashStage::VERIFYING:
        status = "Verifying radio";
        this->flash_state_ = FlashState::VERIFYING;
        break;
      case RadioFlashStage::RESTARTING:
        status = "Restarting radio";
        this->flash_state_ = FlashState::RESTARTING;
        break;
      case RadioFlashStage::IDLE:
        break;
    }
    if (status != nullptr) {
      ESP_LOGI(TAG, "Firmware update stage: %s", status);
      if (this->flash_status_text_sensor_ != nullptr)
        this->flash_status_text_sensor_->publish_state(status);
    }
  }
  this->publish_flash_progress_(
      this->radio_flash_progress_.load(std::memory_order_acquire));
}

const char *ZigbeeFirmwareManager::radio_flash_error_name_(
    RadioFlashError error) {
  switch (error) {
    case RadioFlashError::NONE:
      return "none";
    case RadioFlashError::INVALID_IMAGE_SIZE:
      return "invalid image size";
    case RadioFlashError::BSL_SYNC_FAILED:
      return "radio bootloader synchronization failed";
    case RadioFlashError::BANK_ERASE_FAILED:
      return "radio erase failed";
    case RadioFlashError::DOWNLOAD_FAILED:
      return "radio download initialization failed";
    case RadioFlashError::STAGING_READ_FAILED:
      return "staged firmware read failed";
    case RadioFlashError::SEND_DATA_FAILED:
      return "radio write failed";
    case RadioFlashError::CRC_COMMAND_FAILED:
      return "radio CRC verification command failed";
    case RadioFlashError::CRC_MISMATCH:
      return "radio CRC verification mismatch";
  }
  return "unknown radio firmware error";
}

void ZigbeeFirmwareManager::handle_radio_flash_result_() {
  this->update_radio_flash_status_();
  const auto &result = this->radio_flash_result_;
  if (result.bank_erased && this->gateway_ != nullptr &&
      !this->gateway_->record_local_radio_erase()) {
    ESP_LOGE(TAG, "Radio bank erase succeeded, but cleared network metadata was not persisted.");
  }
  if (result.error != RadioFlashError::NONE) {
    if (this->gateway_ != nullptr)
      this->gateway_->finish_local_firmware_update(false);
    ESP_LOGE(TAG,
             "Radio firmware update failed: %s, offset=%" PRIu32
             ", ROM status=0x%02X, elapsed=%" PRIu32 " ms",
             radio_flash_error_name_(result.error), result.offset,
             result.rom_status, result.duration_ms);
    this->fail_firmware_update_(radio_flash_error_name_(result.error));
    return;
  }

  ESP_LOGI(TAG,
           "Radio firmware verified: CRC32=0x%08" PRIX32
           ", bytes=%" PRIu32 ", elapsed=%" PRIu32 " ms",
           result.radio_crc, result.offset, result.duration_ms);
  if (this->gateway_ != nullptr &&
      !this->gateway_->record_local_firmware_install(
          this->flash_entry_.version,
          this->role_display_from_key_(this->flash_entry_.role))) {
    ESP_LOGE(TAG, "Radio update succeeded, but installed-image metadata was not persisted.");
  }
  if (result.bootloader_reset_acknowledged) {
    ESP_LOGI(TAG, "ROM bootloader reset acknowledged.");
  } else {
    ESP_LOGW(TAG,
             "ROM bootloader reset was not acknowledged; using the hardware reset fallback.");
  }
  if (this->gateway_ != nullptr) {
    this->gateway_->finish_local_firmware_update(
        result.bootloader_reset_acknowledged);
  } else {
    this->finish_firmware_update_();
  }
}

void ZigbeeFirmwareManager::update_download_progress_() {
  const DownloadPhase phase =
      this->firmware_download_phase_.load(std::memory_order_acquire);
  if (phase == DownloadPhase::ERASING_STAGING) {
    const size_t erased =
        this->firmware_staging_erase_bytes_.load(std::memory_order_acquire);
    const size_t payload_capacity = this->staging_partition_->size - STAGING_HEADER_SIZE;
    this->publish_flash_progress_(static_cast<uint8_t>(
        std::min<size_t>(4, (erased * 5) / payload_capacity)));
    return;
  }
  if (phase == DownloadPhase::DOWNLOADING &&
      this->flash_state_ == FlashState::PREPARING_DOWNLOAD) {
    this->begin_flash_stage_(FlashState::DOWNLOADING, "Downloading firmware");
  }
  const size_t total = this->firmware_download_total_.load(std::memory_order_acquire);
  const size_t received =
      this->firmware_download_bytes_.load(std::memory_order_acquire);
  if (total == 0)
    return;
  this->publish_flash_progress_(
      static_cast<uint8_t>(std::min<size_t>(29, 5 + (received * 25) / total)));
}

bool ZigbeeFirmwareManager::write_staging_header_() {
  StagingHeader header{};
  header.magic = STAGING_MAGIC;
  header.schema = STAGING_SCHEMA;
  header.state = 1;
  header.image_size = static_cast<uint32_t>(this->flash_image_size_);
  header.baud_rate = this->flash_entry_.baud_rate;
  header.digest = this->flash_image_digest_;
  strncpy(header.role, this->flash_entry_.role.c_str(), sizeof(header.role) - 1);
  strncpy(header.version, this->flash_entry_.version.c_str(), sizeof(header.version) - 1);
  strncpy(header.filename, this->flash_entry_.filename.c_str(), sizeof(header.filename) - 1);
  strncpy(header.etag, this->firmware_download_result_.etag.c_str(),
          sizeof(header.etag) - 1);

  const esp_err_t write_error =
      esp_partition_write(this->staging_partition_, 0, &header, sizeof(header));
  if (write_error != ESP_OK) {
    ESP_LOGE(TAG, "Could not write firmware staging header: %s",
             esp_err_to_name(write_error));
    return false;
  }

  StagingHeader readback{};
  const esp_err_t read_error =
      esp_partition_read(this->staging_partition_, 0, &readback, sizeof(readback));
  if (read_error != ESP_OK) {
    ESP_LOGE(TAG, "Could not read firmware staging header: %s",
             esp_err_to_name(read_error));
    return false;
  }
  if (memcmp(&header, &readback, sizeof(header)) != 0) {
    ESP_LOGE(TAG, "Firmware staging header readback mismatch");
    return false;
  }
  this->staged_header_ = readback;
  this->staged_image_valid_ = true;
  this->finalize_staging_after_check_ = false;
  return true;
}

bool ZigbeeFirmwareManager::load_staged_image_() {
  this->clear_staged_image_runtime_();
  StagingHeader header{};
  const esp_err_t error =
      esp_partition_read(this->staging_partition_, 0, &header, sizeof(header));
  if (error != ESP_OK) {
    ESP_LOGW(TAG, "Could not inspect firmware staging header: %s",
             esp_err_to_name(error));
    return false;
  }
  if (header.magic != STAGING_MAGIC || header.schema != STAGING_SCHEMA ||
      header.state != 1 || header.image_size == 0 ||
      header.image_size > MAX_FIRMWARE_IMAGE_SIZE ||
      memchr(header.role, '\0', sizeof(header.role)) == nullptr ||
      memchr(header.version, '\0', sizeof(header.version)) == nullptr ||
      memchr(header.filename, '\0', sizeof(header.filename)) == nullptr ||
      memchr(header.etag, '\0', sizeof(header.etag)) == nullptr ||
      header.role[0] == '\0' || header.version[0] == '\0' ||
      header.filename[0] == '\0') {
    ESP_LOGD(TAG, "No verified firmware image is staged");
    return false;
  }
  this->staged_header_ = header;
  this->staged_image_valid_ = true;
  ESP_LOGD(TAG,
           "Verified staged image: role=%s, version=%s, file=%s, size=%" PRIu32
           ", ETag=%s, SHA-256=%s",
           header.role, header.version, header.filename, header.image_size,
           header.etag[0] == '\0' ? "(none)" : header.etag,
           sha256_to_string_(header.digest).c_str());
  return true;
}

bool ZigbeeFirmwareManager::staged_image_matches_(const FirmwareEntry &entry) const {
  return this->staged_image_valid_ && entry.role == this->staged_header_.role &&
         entry.version == this->staged_header_.version &&
         entry.filename == this->staged_header_.filename;
}

bool ZigbeeFirmwareManager::invalidate_staged_firmware_(const char *reason,
                                                     bool publish_status) {
  if (this->staging_partition_ == nullptr)
    return false;
  const esp_err_t error =
      esp_partition_erase_range(this->staging_partition_, 0, STAGING_HEADER_SIZE);
  if (error != ESP_OK) {
    ESP_LOGE(TAG, "Could not invalidate staged firmware: %s", esp_err_to_name(error));
    return false;
  }
  this->clear_staged_image_runtime_();
  ESP_LOGD(TAG, "Staged firmware invalidated: %s", reason);
  if (publish_status) {
    if (this->flash_status_text_sensor_ != nullptr)
      this->flash_status_text_sensor_->publish_state("No staged firmware");
    this->publish_flash_progress_(0);
  }
  return true;
}

void ZigbeeFirmwareManager::clear_staged_image_runtime_() {
  this->staged_header_ = {};
  this->staged_image_valid_ = false;
}

bool ZigbeeFirmwareManager::read_staged_bytes_(size_t offset, uint8_t *data, size_t length) {
  if (this->staging_partition_ == nullptr ||
      offset + length > this->flash_image_size_ ||
      STAGING_HEADER_SIZE + offset + length > this->staging_partition_->size)
    return false;
  const esp_err_t error =
      esp_partition_read(this->staging_partition_, STAGING_HEADER_SIZE + offset, data, length);
  if (error != ESP_OK) {
    ESP_LOGE(TAG, "Could not read firmware staging partition at 0x%X: %s",
             static_cast<unsigned>(offset), esp_err_to_name(error));
    return false;
  }
  return true;
}

bool ZigbeeFirmwareManager::flash_active_() const {
  return this->flash_state_ == FlashState::CHECKING_FRESHNESS ||
         this->flash_state_ == FlashState::PREPARING_DOWNLOAD ||
         this->flash_state_ == FlashState::DOWNLOADING ||
         this->flash_state_ == FlashState::CHECKING_DOWNLOAD ||
         this->flash_state_ == FlashState::ENTERING_BSL ||
         this->flash_state_ == FlashState::ERASING ||
         this->flash_state_ == FlashState::WRITING ||
         this->flash_state_ == FlashState::VERIFYING ||
         this->flash_state_ == FlashState::RESTARTING;
}

void ZigbeeFirmwareManager::publish_flash_progress_(uint8_t progress) {
  progress = std::min<uint8_t>(progress, 100);
  if (progress == this->last_flash_progress_)
    return;
  this->last_flash_progress_ = progress;
  if (this->flash_progress_sensor_ != nullptr)
    this->flash_progress_sensor_->publish_state(progress);
  if (progress >= this->next_flash_progress_log_) {
    ESP_LOGD(TAG, "Firmware update progress: %u%%", progress);
    while (this->next_flash_progress_log_ <= progress &&
           this->next_flash_progress_log_ < 100)
      this->next_flash_progress_log_ += 10;
  }
}

void ZigbeeFirmwareManager::fail_firmware_update_(const std::string &reason) {
  if (this->staged_image_sha_active_) {
    mbedtls_sha256_free(&this->staged_image_sha_context_);
    this->staged_image_sha_active_ = false;
  }
  this->flash_state_ = FlashState::FAILED;
  if (this->flash_status_text_sensor_ != nullptr)
    this->flash_status_text_sensor_->publish_state(std::string("Failed: ") + reason);
  ESP_LOGE(TAG, "Zigbee firmware update failed: %s", reason.c_str());
}

void ZigbeeFirmwareManager::finish_firmware_update_() {
  this->flash_state_ = FlashState::COMPLETE;
  this->publish_flash_progress_(100);
  if (this->flash_status_text_sensor_ != nullptr)
    this->flash_status_text_sensor_->publish_state("Complete");
  ESP_LOGI(TAG, "Zigbee firmware update complete in %" PRIu32 " ms",
           millis() - this->flash_started_ms_);
}

void ZigbeeFirmwareManager::handle_fetch_result_() {
  const bool gate_was_released = this->startup_gate_released_;
  const size_t response_body_size = this->fetch_result_.body.size();
  const auto release_response_body = [this]() {
    std::string{}.swap(this->fetch_result_.body);
  };

  if (this->fetch_result_.error != ESP_OK || this->fetch_result_.body_too_large) {
    ESP_LOGE(TAG, "Catalog download failed: %s", esp_err_to_name(this->fetch_result_.error));
    release_response_body();
    this->publish_catalog_check_failed_(true);
    this->publish_status_(this->cache_valid_ ? "Refresh failed; using cached catalog" : UNAVAILABLE_OPTION);
    this->release_startup_gate_("HTTPS failure");
    this->force_fetch_ = false;
    return;
  }

  if (this->fetch_result_.status_code == 304) {
    release_response_body();
    if (!this->cache_valid_) {
      ESP_LOGE(TAG, "Server returned 304 but there is no valid local cache");
      this->publish_catalog_check_failed_(true);
      this->publish_status_(UNAVAILABLE_OPTION);
    } else {
      ESP_LOGD(TAG, "Catalog is unchanged; cached normalized catalog remains active");
      this->publish_catalog_check_failed_(false);
      this->publish_status_("Ready; upstream unchanged");
    }
    this->release_startup_gate_("conditional refresh complete");
    this->force_fetch_ = false;
    return;
  }

  if (this->fetch_result_.status_code != 200) {
    ESP_LOGE(TAG, "Unexpected HTTP status: %d", this->fetch_result_.status_code);
    release_response_body();
    this->publish_catalog_check_failed_(true);
    this->publish_status_(this->cache_valid_ ? "Refresh failed; using cached catalog" : UNAVAILABLE_OPTION);
    this->release_startup_gate_("unexpected HTTP status");
    this->force_fetch_ = false;
    return;
  }

  ESP_LOGD(TAG, "Manifest content type=%s, ETag=%s",
           this->fetch_result_.content_type.empty() ? "(not supplied)"
                                                    : this->fetch_result_.content_type.c_str(),
           this->fetch_result_.etag.empty() ? "(not supplied)" : this->fetch_result_.etag.c_str());
  std::vector<FirmwareEntry> fresh_entries;
  if (!this->process_manifest_(this->fetch_result_.body, fresh_entries)) {
    release_response_body();
    this->publish_catalog_check_failed_(true);
    this->publish_status_(this->cache_valid_ ? "Manifest invalid; using cached catalog" : UNAVAILABLE_OPTION);
    this->release_startup_gate_("manifest parse failure");
    this->force_fetch_ = false;
    return;
  }

  this->clear_cache_blob_();
  this->cache_blob_.manifest_length = response_body_size;
  if (!this->fetch_result_.etag.empty()) {
    strncpy(this->cache_blob_.etag, this->fetch_result_.etag.c_str(), sizeof(this->cache_blob_.etag) - 1);
  }
  release_response_body();
  this->apply_catalog_(std::move(fresh_entries), gate_was_released, "HTTPS manifest");
  this->cache_valid_ = this->save_cache_();
  this->publish_catalog_check_failed_(!this->cache_valid_);
  this->publish_status_(this->cache_valid_ ? "Ready" : "Ready; cache save failed");
  this->release_startup_gate_("catalog ready");
  this->force_fetch_ = false;
}

bool ZigbeeFirmwareManager::process_manifest_(const std::string &body, std::vector<FirmwareEntry> &entries) {
  ESP_LOGD(TAG, "Parsing %u manifest bytes", static_cast<unsigned>(body.size()));

  JsonDocument document = json::parse_json(reinterpret_cast<const uint8_t *>(body.data()), body.size());
  if (document.isNull() || !document.is<JsonObject>()) {
    ESP_LOGE(TAG, "Manifest root is not a JSON object");
    return false;
  }
  JsonObject root = document.as<JsonObject>();

  size_t rejected = 0;
  for (JsonPair role_pair : root) {
    const std::string role = role_pair.key().c_str();
    if (!is_supported_role_(role)) {
      ESP_LOGD(TAG, "Ignoring unsupported firmware role '%s'", role.c_str());
      continue;
    }
    JsonObject chips = role_pair.value().as<JsonObject>();
    if (chips.isNull()) {
      ESP_LOGW(TAG, "Role '%s' is not an object", role.c_str());
      continue;
    }

    JsonVariant selected_chip = chips[this->chip_.c_str()];
    if (selected_chip.isNull()) {
      ESP_LOGD(TAG, "Role '%s' has no '%s' catalog", role.c_str(), this->chip_.c_str());
      continue;
    }

    JsonObject builds = selected_chip.as<JsonObject>();
    for (JsonPair build_pair : builds) {
      JsonObject build = build_pair.value().as<JsonObject>();
      const char *version = build["ver"] | "";
      const char *url = build["link"] | "";
      const char *notes = build["notes"] | "";
      const char *baud = build["baud"] | "";
      const std::string filename = build_pair.key().c_str();
      if (filename.empty() || version[0] == '\0' || url[0] == '\0') {
        ESP_LOGW(TAG, "Rejected '%s/%s': required filename, ver, or link is missing", role.c_str(),
                 filename.c_str());
        rejected++;
        continue;
      }
      if (!filename.ends_with(".bin")) {
        ESP_LOGW(TAG, "Rejected '%s/%s': only raw .bin images are supported",
                 role.c_str(), filename.c_str());
        rejected++;
        continue;
      }

      FirmwareEntry entry;
      entry.role = role;
      entry.filename = filename;
      entry.version = version;
      entry.label = entry.version;
      entry.url = url;
      entry.notes = notes;
      if (baud[0] != '\0') {
        char *end = nullptr;
        const unsigned long parsed = std::strtoul(baud, &end, 10);
        if (end == baud || *end != '\0' || parsed == 0 || parsed > UINT32_MAX) {
          ESP_LOGW(TAG, "Rejected '%s/%s': invalid baud rate '%s'", role.c_str(),
                   filename.c_str(), baud);
          rejected++;
          continue;
        }
        entry.baud_rate = static_cast<uint32_t>(parsed);
      }
      entry.sort_key = extract_sort_key_(entry.version);
      entries.push_back(std::move(entry));
    }
  }

  if (entries.empty()) {
    ESP_LOGE(TAG, "No compatible entries found for chip '%s'", this->chip_.c_str());
    return false;
  }

  std::sort(entries.begin(), entries.end(), [](const FirmwareEntry &left, const FirmwareEntry &right) {
    if (left.role != right.role)
      return left.role < right.role;
    if (left.sort_key != right.sort_key)
      return left.sort_key > right.sort_key;
    return left.version > right.version;
  });

  for (size_t i = 0; i < entries.size(); i++) {
    size_t duplicates = 0;
    for (const auto &candidate : entries) {
      if (candidate.role == entries[i].role && candidate.version == entries[i].version)
        duplicates++;
    }
    if (duplicates > 1)
      entries[i].label = entries[i].version + " · " + entries[i].filename;
  }

  size_t coordinator = 0;
  size_t router = 0;
  for (const auto &entry : entries) {
    if (entry.role == "coordinator")
      coordinator++;
    else if (entry.role == "router")
      router++;
  }
  ESP_LOGD(TAG,
           "Catalog ready for %s: coordinator=%u, router=%u, rejected=%u",
           this->chip_.c_str(), static_cast<unsigned>(coordinator),
           static_cast<unsigned>(router), static_cast<unsigned>(rejected));
  return true;
}

bool ZigbeeFirmwareManager::load_cache_() {
  this->clear_cache_blob_();
  if (!this->preference_.load(&this->cache_blob_)) {
    ESP_LOGD(TAG, "No catalog cache record found in NVS");
    return false;
  }
  ESP_LOGD(TAG, "NVS catalog record loaded: %u bytes",
           static_cast<unsigned>(sizeof(this->cache_blob_)));
  if (this->cache_blob_.magic != CACHE_MAGIC || this->cache_blob_.schema != CACHE_SCHEMA ||
      this->cache_blob_.length == 0 || this->cache_blob_.length > sizeof(this->cache_blob_.data)) {
    ESP_LOGW(TAG,
             "Cache header invalid: magic=%08" PRIX32 ", schema=%u, length=%" PRIu32,
             this->cache_blob_.magic, this->cache_blob_.schema, this->cache_blob_.length);
    return false;
  }
  const uint32_t actual_digest = digest_(this->cache_blob_.data, this->cache_blob_.length);
  if (actual_digest != this->cache_blob_.digest) {
    ESP_LOGW(TAG, "Cache digest mismatch: stored=%08" PRIX32 ", actual=%08" PRIX32,
             this->cache_blob_.digest, actual_digest);
    return false;
  }

  std::vector<FirmwareEntry> cached_entries;
  if (!this->parse_normalized_catalog_(reinterpret_cast<const uint8_t *>(this->cache_blob_.data),
                                       this->cache_blob_.length, cached_entries)) {
    ESP_LOGW(TAG, "Normalized cache JSON is invalid");
    return false;
  }

  this->entries_ = std::move(cached_entries);
  this->cache_valid_ = true;
  ESP_LOGD(TAG, "Catalog cache restored: %u Zigbee entries",
           static_cast<unsigned>(this->entries_.size()));
  ESP_LOGD(TAG, "Catalog cache payload=%" PRIu32 " bytes, ETag=%s",
           this->cache_blob_.length,
           this->cache_blob_.etag[0] == '\0' ? "(none)" : this->cache_blob_.etag);
  return true;
}

bool ZigbeeFirmwareManager::save_cache_() {
  if (!this->serialize_normalized_catalog_(this->entries_)) {
    return false;
  }

  this->cache_blob_.magic = CACHE_MAGIC;
  this->cache_blob_.schema = CACHE_SCHEMA;
  this->cache_blob_.digest = digest_(this->cache_blob_.data, this->cache_blob_.length);
  ESP_LOGD(TAG, "Saving normalized catalog: payload=%" PRIu32 ", digest=%08" PRIX32
                ", record=%u, ETag=%s",
           this->cache_blob_.length, this->cache_blob_.digest,
           static_cast<unsigned>(sizeof(this->cache_blob_)),
           this->cache_blob_.etag[0] == '\0' ? "(none)" : this->cache_blob_.etag);

  const bool queued = this->preference_.save(&this->cache_blob_);
  const bool synced = queued && global_preferences->sync();
  ESP_LOGD(TAG, "NVS catalog save: queued=%s, synced=%s", YESNO(queued), YESNO(synced));
  if (!synced) {
    ESP_LOGW(TAG, "Could not save the normalized catalog");
    return false;
  }

  return true;
}

bool ZigbeeFirmwareManager::parse_normalized_catalog_(const uint8_t *data, size_t length,
                                                    std::vector<FirmwareEntry> &entries) {
  JsonDocument document = json::parse_json(data, length);
  if (document.isNull() || !document.is<JsonArray>())
    return false;

  for (JsonObject item : document.as<JsonArray>()) {
    const char *role = item["role"] | "";
    const char *filename = item["file"] | "";
    const char *version = item["version"] | "";
    const char *label = item["label"] | "";
    const char *url = item["url"] | "";
    const char *notes = item["notes"] | "";
    if (!is_supported_role_(role) || filename[0] == '\0' || version[0] == '\0' ||
        url[0] == '\0')
      return false;

    FirmwareEntry entry;
    entry.role = role;
    entry.filename = filename;
    entry.version = version;
    entry.label = label[0] == '\0' ? entry.version : label;
    entry.url = url;
    entry.notes = notes;
    entry.baud_rate = item["baud"] | 115200;
    if (entry.baud_rate == 0)
      return false;
    entry.sort_key = item["sort"] | extract_sort_key_(entry.version);
    entries.push_back(std::move(entry));
  }
  return !entries.empty();
}

bool ZigbeeFirmwareManager::serialize_normalized_catalog_(const std::vector<FirmwareEntry> &entries) {
  JsonDocument document;
  JsonArray array = document.to<JsonArray>();
  for (const auto &entry : entries) {
    JsonObject item = array.add<JsonObject>();
    item["role"] = entry.role;
    item["file"] = entry.filename;
    item["version"] = entry.version;
    item["label"] = entry.label;
    item["url"] = entry.url;
    item["notes"] = entry.notes;
    item["baud"] = entry.baud_rate;
    item["sort"] = entry.sort_key;
  }

  const size_t required = measureJson(document);
  ESP_LOGD(TAG, "Normalized catalog requires %u bytes", static_cast<unsigned>(required));
  if (required == 0 || required >= sizeof(this->cache_blob_.data)) {
    ESP_LOGE(TAG, "Normalized catalog does not fit the %u-byte cache",
             static_cast<unsigned>(sizeof(this->cache_blob_.data)));
    return false;
  }
  this->cache_blob_.length =
      serializeJson(document, this->cache_blob_.data, sizeof(this->cache_blob_.data));
  return this->cache_blob_.length == required;
}

void ZigbeeFirmwareManager::apply_catalog_(std::vector<FirmwareEntry> entries, bool allow_api_reconnect,
                                        const char *source) {
  const auto previous_roles = this->role_options_;
  const auto previous_firmwares = this->firmware_options_;
  this->entries_ = std::move(entries);
  this->rebuild_role_options_();

  set_select_options_(this->role_select_, this->role_options_);
  this->active_role_ = select_initial_firmware_role(
      this->role_keys_,
      this->target_selection_valid_ ? this->target_selection_.role : "",
      this->current_radio_role_, this->preferred_role_);
  if (this->role_select_ != nullptr)
    this->role_select_->publish_state(this->role_display_from_key_(this->active_role_));
  this->rebuild_firmware_options_();

  const bool visible_changed =
      (this->role_select_ != nullptr &&
       !same_options_(previous_roles, this->role_options_)) ||
      (this->firmware_select_ != nullptr &&
       !same_options_(previous_firmwares, this->firmware_options_));
  ESP_LOGD(TAG, "Applied catalog from %s; visible metadata changed=%s", source, YESNO(visible_changed));
  if (allow_api_reconnect && visible_changed)
    this->schedule_api_reconnect_("catalog options changed");
}

void ZigbeeFirmwareManager::rebuild_role_options_() {
  this->role_keys_.clear();
  for (const auto &entry : this->entries_) {
    if (std::find(this->role_keys_.begin(), this->role_keys_.end(), entry.role) == this->role_keys_.end())
      this->role_keys_.push_back(entry.role);
  }
  std::sort(this->role_keys_.begin(), this->role_keys_.end(), [](const std::string &left, const std::string &right) {
    auto priority = [](const std::string &role) {
      if (role == "coordinator")
        return 0;
      if (role == "router")
        return 1;
      return 10;
    };
    const int left_priority = priority(left);
    const int right_priority = priority(right);
    return left_priority == right_priority ? left < right : left_priority < right_priority;
  });

  this->role_options_.clear();
  for (const auto &role : this->role_keys_)
    this->role_options_.push_back(this->role_display_from_key_(role));
  if (this->role_options_.empty())
    this->role_options_.push_back(UNAVAILABLE_OPTION);
}

bool ZigbeeFirmwareManager::rebuild_firmware_options_() {
  const auto previous = this->firmware_options_;
  this->firmware_options_.clear();

  if (this->entries_.empty() || this->active_role_.empty()) {
    this->firmware_options_.push_back(UNAVAILABLE_OPTION);
    set_select_options_(this->firmware_select_, this->firmware_options_);
    this->selected_firmware_label_ = UNAVAILABLE_OPTION;
    if (this->firmware_select_ != nullptr)
      this->firmware_select_->publish_state(UNAVAILABLE_OPTION);
    this->publish_selected_details_(UNAVAILABLE_OPTION);
    return !same_options_(previous, this->firmware_options_);
  }

  this->firmware_options_.push_back(SELECT_OPTION);
  for (const auto &entry : this->entries_) {
    if (entry.role == this->active_role_)
      this->firmware_options_.push_back(entry.label);
  }
  if (this->firmware_options_.size() == 1)
    this->firmware_options_ = {UNAVAILABLE_OPTION};

  set_select_options_(this->firmware_select_, this->firmware_options_);
  std::string selected = SELECT_OPTION;
  if (this->target_selection_valid_ &&
      this->active_role_ == this->target_selection_.role) {
    const auto *saved = this->find_saved_target_entry_();
    if (saved != nullptr) {
      selected = saved->label;
      this->remembered_firmware_[this->active_role_] = selected;
    } else if (this->target_selection_.version[0] != '\0') {
      ESP_LOGD(TAG, "Saved target is absent from the current catalog: role=%s, "
                    "version=%s, file=%s",
               this->target_selection_.role, this->target_selection_.version,
               this->target_selection_.filename);
    }
  } else {
    auto remembered = this->remembered_firmware_.find(this->active_role_);
    if (remembered != this->remembered_firmware_.end() &&
        std::find(this->firmware_options_.begin(), this->firmware_options_.end(),
                  remembered->second) != this->firmware_options_.end()) {
      selected = remembered->second;
    }
  }
  this->selected_firmware_label_ = selected;
  if (this->firmware_select_ != nullptr)
    this->firmware_select_->publish_state(selected);
  this->publish_selected_details_(selected);

  ESP_LOGD(TAG, "Active role '%s' exposes %u firmware options", this->active_role_.c_str(),
           static_cast<unsigned>(this->firmware_options_.size() - 1));
  return !same_options_(previous, this->firmware_options_);
}

void ZigbeeFirmwareManager::publish_selected_details_(const std::string &label) {
  const auto *entry = this->find_entry_(this->active_role_, label);
  if (entry == nullptr)
    return;
  ESP_LOGD(TAG, "Selected firmware: role=%s, version=%s, file=%s, baud=%" PRIu32
                ", notes=%s",
           entry->role.c_str(), entry->version.c_str(), entry->filename.c_str(),
           entry->baud_rate, entry->notes.empty() ? "(none)" : entry->notes.c_str());
}

void ZigbeeFirmwareManager::publish_status_(const std::string &status) {
  if (status == this->last_status_)
    return;
  this->last_status_ = status;
  ESP_LOGD(TAG, "Catalog status: %s", status.c_str());
  if (this->status_text_sensor_ != nullptr)
    this->status_text_sensor_->publish_state(status);
}

void ZigbeeFirmwareManager::publish_catalog_check_failed_(bool failed) {
  if (this->catalog_check_failed_binary_sensor_ != nullptr)
    this->catalog_check_failed_binary_sensor_->publish_state(failed);
}

void ZigbeeFirmwareManager::release_startup_gate_(const char *reason) {
  if (this->startup_gate_released_)
    return;
  this->startup_gate_released_ = true;
  ESP_LOGI(TAG, "Startup API gate released: %s; elapsed=%" PRIu32 " ms", reason,
           millis() - this->setup_started_ms_);
}

void ZigbeeFirmwareManager::schedule_api_reconnect_(const char *reason) {
#ifdef USE_API
  ESP_LOGD(TAG, "Scheduling API reconnect in 500 ms: %s", reason);
  this->set_timeout("catalog_api_reconnect", 500, [reason]() {
    auto *server = api::global_api_server;
    if (server == nullptr) {
      ESP_LOGW(TAG, "API server is unavailable; reconnect not requested");
      return;
    }
    api::DisconnectRequest request;
    size_t disconnected = 0;
    for (const auto &client : server->active_clients()) {
      if (client->is_marked_for_removal())
        continue;
      if (!client->send_message(request))
        client->on_fatal_error();
      disconnected++;
    }
    ESP_LOGI(TAG, "Disconnect requested for %u API client(s)", static_cast<unsigned>(disconnected));
  });
#else
  ESP_LOGW(TAG, "API reconnect requested but native API is not compiled");
#endif
}

void ZigbeeFirmwareManager::clear_cache_blob_() {
  this->cache_blob_.magic = 0;
  this->cache_blob_.schema = 0;
  this->cache_blob_.reserved = 0;
  this->cache_blob_.length = 0;
  this->cache_blob_.manifest_length = 0;
  this->cache_blob_.digest = 0;
  std::fill(std::begin(this->cache_blob_.etag), std::end(this->cache_blob_.etag), '\0');
  std::fill(std::begin(this->cache_blob_.data), std::end(this->cache_blob_.data), '\0');
}

bool ZigbeeFirmwareManager::load_target_selection_() {
  this->target_selection_ = {};
  this->target_selection_valid_ = false;

  TargetSelectionBlob target{};
  if (!this->target_preference_.load(&target)) {
    ESP_LOGD(TAG, "No saved target firmware selection");
    return false;
  }

  const bool strings_valid =
      memchr(target.role, '\0', sizeof(target.role)) != nullptr &&
      memchr(target.version, '\0', sizeof(target.version)) != nullptr &&
      memchr(target.filename, '\0', sizeof(target.filename)) != nullptr;
  const bool firmware_identity_valid =
      (target.version[0] == '\0') == (target.filename[0] == '\0');
  if (target.magic != TARGET_MAGIC || target.schema != TARGET_SCHEMA ||
      !strings_valid || !is_supported_role_(target.role) ||
      !firmware_identity_valid) {
    ESP_LOGW(TAG, "Saved target firmware selection is invalid");
    return false;
  }

  const uint32_t actual_digest = target_selection_digest_(target);
  if (target.digest != actual_digest) {
    ESP_LOGW(TAG,
             "Saved target firmware digest mismatch: stored=%08" PRIX32
             ", actual=%08" PRIX32,
             target.digest, actual_digest);
    return false;
  }

  this->target_selection_ = target;
  this->target_selection_valid_ = true;
  this->active_role_ = target.role;
  ESP_LOGD(TAG, "Restored target firmware selection: role=%s, version=%s, file=%s",
           target.role, target.version[0] == '\0' ? "(not selected)" : target.version,
           target.filename[0] == '\0' ? "(not selected)" : target.filename);
  return true;
}

bool ZigbeeFirmwareManager::save_target_selection_(const std::string &role,
                                                const std::string &version,
                                                const std::string &filename) {
  if (!is_supported_role_(role) || (version.empty() != filename.empty()) ||
      role.size() >= sizeof(this->target_selection_.role) ||
      version.size() >= sizeof(this->target_selection_.version) ||
      filename.size() >= sizeof(this->target_selection_.filename)) {
    ESP_LOGW(TAG, "Target firmware selection cannot be persisted");
    return false;
  }

  TargetSelectionBlob target{};
  target.magic = TARGET_MAGIC;
  target.schema = TARGET_SCHEMA;
  strncpy(target.role, role.c_str(), sizeof(target.role) - 1);
  strncpy(target.version, version.c_str(), sizeof(target.version) - 1);
  strncpy(target.filename, filename.c_str(), sizeof(target.filename) - 1);
  target.digest = target_selection_digest_(target);

  if (this->target_selection_valid_ &&
      memcmp(&target, &this->target_selection_, sizeof(target)) == 0)
    return true;

  this->target_selection_ = target;
  this->target_selection_valid_ = true;
  const bool queued = this->target_preference_.save(&this->target_selection_);
  const bool synced = queued && global_preferences->sync();
  if (!synced) {
    ESP_LOGW(TAG, "Could not save target firmware selection");
    return false;
  }

  ESP_LOGD(TAG, "Saved target firmware selection: role=%s, version=%s, file=%s",
           target.role, target.version[0] == '\0' ? "(not selected)" : target.version,
           target.filename[0] == '\0' ? "(not selected)" : target.filename);
  return true;
}

void ZigbeeFirmwareManager::persist_current_target_selection_() {
  if (!is_supported_role_(this->active_role_))
    return;

  const auto *entry =
      this->find_entry_(this->active_role_, this->selected_firmware_label_);
  if (entry != nullptr) {
    this->save_target_selection_(entry->role, entry->version, entry->filename);
    return;
  }
  this->save_target_selection_(this->active_role_, "", "");
}

void ZigbeeFirmwareManager::adopt_staged_target_selection_() {
  if (this->target_selection_valid_ || !this->staged_image_valid_ ||
      !is_supported_role_(this->staged_header_.role))
    return;

  this->active_role_ = this->staged_header_.role;
  this->save_target_selection_(this->staged_header_.role,
                               this->staged_header_.version,
                               this->staged_header_.filename);
  ESP_LOGI(TAG, "Adopted staged image as the initial target firmware selection");
}

const ZigbeeFirmwareManager::FirmwareEntry *
ZigbeeFirmwareManager::find_saved_target_entry_() const {
  if (!this->target_selection_valid_ ||
      this->target_selection_.version[0] == '\0')
    return nullptr;

  for (const auto &entry : this->entries_) {
    if (entry.role == this->target_selection_.role &&
        entry.version == this->target_selection_.version &&
        entry.filename == this->target_selection_.filename)
      return &entry;
  }
  return nullptr;
}

const ZigbeeFirmwareManager::FirmwareEntry *ZigbeeFirmwareManager::find_entry_(
    const std::string &role, const std::string &label) const {
  for (const auto &entry : this->entries_) {
    if (entry.role == role && entry.label == label)
      return &entry;
  }
  return nullptr;
}

std::string ZigbeeFirmwareManager::role_key_from_display_(const std::string &display) const {
  for (const auto &role : this->role_keys_) {
    if (this->role_display_from_key_(role) == display)
      return role;
  }
  return {};
}

std::string ZigbeeFirmwareManager::role_display_from_key_(const std::string &key) const {
  return humanize_(key);
}

bool ZigbeeFirmwareManager::is_supported_role_(const std::string &role) {
  return role == "coordinator" || role == "router";
}

std::string ZigbeeFirmwareManager::humanize_(const std::string &value) {
  std::string result;
  result.reserve(value.size());
  bool capitalize = true;
  for (char character : value) {
    if (character == '_' || character == '-') {
      result.push_back(' ');
      capitalize = true;
      continue;
    }
    result.push_back(capitalize ? static_cast<char>(std::toupper(static_cast<unsigned char>(character))) : character);
    capitalize = false;
  }
  return result;
}

uint32_t ZigbeeFirmwareManager::extract_sort_key_(const std::string &value) {
  uint32_t result = 0;
  for (size_t index = 0; index + 8 <= value.size(); index++) {
    bool digits = true;
    for (size_t offset = 0; offset < 8; offset++) {
      if (!std::isdigit(static_cast<unsigned char>(value[index + offset]))) {
        digits = false;
        break;
      }
    }
    if (!digits)
      continue;
    uint32_t candidate = 0;
    for (size_t offset = 0; offset < 8; offset++)
      candidate = candidate * 10 + static_cast<uint32_t>(value[index + offset] - '0');
    result = candidate;
  }
  return result;
}

uint32_t ZigbeeFirmwareManager::digest_(const char *data, size_t length) {
  uint32_t digest = 2166136261UL;
  for (size_t index = 0; index < length; index++)
    digest = digest_byte_(digest, static_cast<uint8_t>(data[index]));
  return digest;
}

uint32_t ZigbeeFirmwareManager::digest_byte_(uint32_t digest, uint8_t value) {
  digest ^= value;
  digest *= 16777619UL;
  return digest;
}

uint32_t ZigbeeFirmwareManager::target_selection_digest_(
    const TargetSelectionBlob &target) {
  uint32_t digest = 2166136261UL;
  for (const char value : target.role)
    digest = digest_byte_(digest, static_cast<uint8_t>(value));
  for (const char value : target.version)
    digest = digest_byte_(digest, static_cast<uint8_t>(value));
  for (const char value : target.filename)
    digest = digest_byte_(digest, static_cast<uint8_t>(value));
  return digest;
}

std::string ZigbeeFirmwareManager::sha256_to_string_(
    const std::array<uint8_t, 32> &digest) {
  static constexpr char HEX[] = "0123456789abcdef";
  std::string result(64, '0');
  for (size_t index = 0; index < digest.size(); index++) {
    result[index * 2] = HEX[digest[index] >> 4];
    result[index * 2 + 1] = HEX[digest[index] & 0x0F];
  }
  return result;
}

bool ZigbeeFirmwareManager::same_options_(const std::vector<std::string> &left,
                                       const std::vector<std::string> &right) {
  return left == right;
}

void ZigbeeFirmwareManager::set_select_options_(select::Select *target,
                                             const std::vector<std::string> &options) {
  if (target == nullptr)
    return;
  FixedVector<const char *> pointers;
  pointers.init(options.size());
  for (const auto &option : options)
    pointers.push_back(option.c_str());
  target->traits.set_options(pointers);
}

}  // namespace esphome::zigbee_gateway
