#pragma once

#include "esphome/components/binary_sensor/binary_sensor.h"
#include "esphome/components/button/button.h"
#include "esphome/components/select/select.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/text_sensor/text_sensor.h"
#include "esphome/core/component.h"
#include "esphome/core/preferences.h"

#ifdef USE_API
#include "esphome/components/api/api_server.h"
#endif

#include <atomic>
#include <array>
#include <map>
#include <string>
#include <vector>

#include <esp_err.h>
#include <esp_http_client.h>
#include <esp_partition.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <mbedtls/sha256.h>

namespace esphome::zigbee_gateway {

class ZigbeeFirmwareManager : public Component {
 public:
  void setup() override;
  void loop() override;
  void dump_config() override;
  bool can_proceed() override;
  float get_setup_priority() const override { return setup_priority::BEFORE_CONNECTION; }

  void set_manifest_url(const std::string &value) { this->manifest_url_ = value; }
  void set_chip(const std::string &value) { this->chip_ = value; }
  void set_preferred_role(const std::string &value) { this->preferred_role_ = value; }
  void set_startup_timeout(uint32_t value) { this->startup_timeout_ms_ = value; }
  void set_http_timeout(uint32_t value) { this->http_timeout_ms_ = value; }
  void set_max_manifest_size(size_t value) { this->max_manifest_size_ = value; }

  void set_role_select(select::Select *value) { this->role_select_ = value; }
  void set_firmware_select(select::Select *value) { this->firmware_select_ = value; }
  void set_status_text_sensor(text_sensor::TextSensor *value) { this->status_text_sensor_ = value; }
  void set_flash_status_text_sensor(text_sensor::TextSensor *value) {
    this->flash_status_text_sensor_ = value;
  }
  void set_flash_progress_sensor(sensor::Sensor *value) { this->flash_progress_sensor_ = value; }
  void set_catalog_check_failed_binary_sensor(binary_sensor::BinarySensor *value) {
    this->catalog_check_failed_binary_sensor_ = value;
  }

  void set_target_role(const std::string &display_role);
  void set_target_firmware(const std::string &display_version);
  void on_network_connected();
  void request_refresh(bool force);
  void start_flash_simulation();
  void invalidate_staged_firmware();

 protected:
  static constexpr uint32_t CACHE_MAGIC = 0x46574350UL;
  static constexpr uint16_t CACHE_SCHEMA = 2;
  static constexpr size_t CACHE_ETAG_SIZE = 160;
  static constexpr size_t CACHE_DATA_SIZE = 8192;
  static constexpr uint32_t CACHE_PREFERENCE_KEY = 0xF1C07A01UL;
  static constexpr uint32_t TARGET_PREFERENCE_KEY = 0xF1C07A02UL;
  static constexpr uint32_t TARGET_MAGIC = 0x46575450UL;
  static constexpr uint16_t TARGET_SCHEMA = 1;
  static constexpr const char *UNAVAILABLE_OPTION = "Catalog unavailable";
  static constexpr const char *SELECT_OPTION = "Select firmware...";
  static constexpr const char *STAGING_PARTITION_LABEL = "zigbee_fw";
  static constexpr uint8_t STAGING_PARTITION_TYPE = 0x40;
  static constexpr uint8_t STAGING_PARTITION_SUBTYPE = 0x00;
  static constexpr size_t STAGING_PARTITION_SIZE = 0xC0000;
  static constexpr size_t STAGING_HEADER_SIZE = 0x1000;
  static constexpr size_t MAX_FIRMWARE_IMAGE_SIZE = 0xB0000;
  static constexpr size_t STAGING_IO_BLOCK_SIZE = 4096;
  static constexpr size_t STAGING_ETAG_SIZE = 160;
  static constexpr uint32_t STAGING_MAGIC = 0x5A465753UL;
  static constexpr uint16_t STAGING_SCHEMA = 2;
  static_assert(STAGING_HEADER_SIZE + MAX_FIRMWARE_IMAGE_SIZE <= STAGING_PARTITION_SIZE);
  static_assert(STAGING_PARTITION_SIZE % STAGING_HEADER_SIZE == 0);
  static constexpr size_t ERASE_BLOCK_SIZE = 8192;
  static constexpr size_t WRITE_BLOCK_SIZE = 248;
  static constexpr uint32_t FLASH_STEP_INTERVAL_MS = 6;

  struct FirmwareEntry {
    std::string role;
    std::string filename;
    std::string version;
    std::string label;
    std::string url;
    std::string notes;
    uint32_t baud_rate{115200};
    uint32_t sort_key{0};
  };

  struct CatalogCacheBlob {
    uint32_t magic{0};
    uint16_t schema{0};
    uint16_t reserved{0};
    uint32_t length{0};
    uint32_t manifest_length{0};
    uint32_t digest{0};
    char etag[CACHE_ETAG_SIZE]{};
    char data[CACHE_DATA_SIZE]{};
  };

  struct TargetSelectionBlob {
    uint32_t magic{0};
    uint16_t schema{0};
    uint16_t reserved{0};
    uint32_t digest{0};
    char role[16]{};
    char version[64]{};
    char filename[128]{};
  };

  struct FetchResult {
    esp_err_t error{ESP_FAIL};
    int status_code{-1};
    uint32_t duration_ms{0};
    size_t declared_length{0};
    bool body_too_large{false};
    std::string body;
    std::string etag;
    std::string content_type;
  };

  struct FirmwareDownloadResult {
    esp_err_t error{ESP_FAIL};
    esp_err_t staging_error{ESP_OK};
    int status_code{-1};
    uint32_t duration_ms{0};
    size_t declared_length{0};
    size_t received_length{0};
    std::array<uint8_t, 32> digest{};
    bool body_too_large{false};
    bool hash_failed{false};
    std::string etag;
  };

  struct FirmwareProbeResult {
    esp_err_t error{ESP_FAIL};
    int status_code{-1};
    uint32_t duration_ms{0};
    std::string etag;
  };

  struct StagingHeader {
    uint32_t magic{0};
    uint16_t schema{0};
    uint8_t state{0};
    uint8_t reserved{0};
    uint32_t image_size{0};
    uint32_t baud_rate{0};
    std::array<uint8_t, 32> digest{};
    char role[16]{};
    char version[64]{};
    char filename[128]{};
    char etag[STAGING_ETAG_SIZE]{};
  };
  static_assert(sizeof(StagingHeader) <= STAGING_HEADER_SIZE);

  enum class FlashState : uint8_t {
    IDLE,
    CHECKING_FRESHNESS,
    PREPARING_DOWNLOAD,
    DOWNLOADING,
    CHECKING_DOWNLOAD,
    ERASING,
    WRITING,
    VERIFYING,
    COMPLETE,
    FAILED,
  };

  enum class DownloadPhase : uint8_t {
    IDLE,
    ERASING_STAGING,
    DOWNLOADING,
  };

  static void fetch_task_entry_(void *parameter);
  static esp_err_t http_event_handler_(esp_http_client_event_t *event);
  static void firmware_download_task_entry_(void *parameter);
  static esp_err_t firmware_http_event_handler_(esp_http_client_event_t *event);
  static void firmware_probe_task_entry_(void *parameter);
  static esp_err_t firmware_probe_http_event_handler_(esp_http_client_event_t *event);

  void fetch_task_();
  bool start_fetch_();
  void handle_fetch_result_();
  bool process_manifest_(const std::string &body, std::vector<FirmwareEntry> &entries);
  bool load_cache_();
  bool save_cache_();
  bool parse_normalized_catalog_(const uint8_t *data, size_t length, std::vector<FirmwareEntry> &entries);
  bool serialize_normalized_catalog_(const std::vector<FirmwareEntry> &entries);
  void apply_catalog_(std::vector<FirmwareEntry> entries, bool allow_api_reconnect, const char *source);
  void rebuild_role_options_();
  bool rebuild_firmware_options_();
  void publish_selected_details_(const std::string &label);
  void publish_status_(const std::string &status);
  void release_startup_gate_(const char *reason);
  void schedule_api_reconnect_(const char *reason);
  void clear_cache_blob_();
  void publish_catalog_check_failed_(bool failed);
  bool load_target_selection_();
  bool save_target_selection_(const std::string &role, const std::string &version,
                              const std::string &filename);
  void persist_current_target_selection_();
  void adopt_staged_target_selection_();
  const FirmwareEntry *find_saved_target_entry_() const;

  void firmware_probe_task_();
  bool start_firmware_probe_();
  void handle_firmware_probe_result_();
  void firmware_download_task_();
  bool start_firmware_download_();
  void handle_firmware_download_result_();
  void use_staged_firmware_(const char *reason);
  void begin_staged_image_check_();
  void advance_staged_image_check_();
  void handle_staged_image_check_failure_(const char *reason);
  void advance_flash_simulation_();
  void begin_flash_stage_(FlashState state, const char *status);
  void fail_flash_simulation_(const std::string &reason);
  void finish_flash_simulation_();
  void publish_flash_progress_(uint8_t progress);
  void update_download_progress_();
  bool write_staging_header_();
  bool load_staged_image_();
  bool staged_image_matches_(const FirmwareEntry &entry) const;
  bool invalidate_staged_firmware_(const char *reason, bool publish_status);
  void clear_staged_image_runtime_();
  bool read_staged_bytes_(size_t offset, uint8_t *data, size_t length);
  bool flash_active_() const;
  const FirmwareEntry *find_entry_(const std::string &role, const std::string &label) const;
  std::string role_key_from_display_(const std::string &display) const;
  std::string role_display_from_key_(const std::string &key) const;
  static bool is_supported_role_(const std::string &role);
  static std::string humanize_(const std::string &value);
  static uint32_t extract_sort_key_(const std::string &value);
  static uint32_t digest_(const char *data, size_t length);
  static uint32_t digest_byte_(uint32_t digest, uint8_t value);
  static uint32_t target_selection_digest_(const TargetSelectionBlob &target);
  static std::string sha256_to_string_(const std::array<uint8_t, 32> &digest);
  static bool same_options_(const std::vector<std::string> &left, const std::vector<std::string> &right);
  static void set_select_options_(select::Select *target, const std::vector<std::string> &options);

  std::string manifest_url_;
  std::string chip_;
  std::string preferred_role_;
  uint32_t startup_timeout_ms_{20000};
  uint32_t http_timeout_ms_{15000};
  size_t max_manifest_size_{65536};

  select::Select *role_select_{nullptr};
  select::Select *firmware_select_{nullptr};
  text_sensor::TextSensor *status_text_sensor_{nullptr};
  text_sensor::TextSensor *flash_status_text_sensor_{nullptr};
  sensor::Sensor *flash_progress_sensor_{nullptr};
  binary_sensor::BinarySensor *catalog_check_failed_binary_sensor_{nullptr};

  std::vector<FirmwareEntry> entries_;
  std::vector<std::string> role_keys_;
  std::vector<std::string> role_options_;
  std::vector<std::string> firmware_options_;
  std::map<std::string, std::string> remembered_firmware_;
  std::string active_role_;
  std::string selected_firmware_label_;
  std::string last_status_;

  ESPPreferenceObject preference_;
  CatalogCacheBlob cache_blob_{};
  bool cache_valid_{false};
  ESPPreferenceObject target_preference_;
  TargetSelectionBlob target_selection_{};
  bool target_selection_valid_{false};

  FetchResult fetch_result_{};
  std::atomic<bool> fetch_running_{false};
  std::atomic<bool> fetch_done_{false};
  std::atomic<uint32_t> fetch_completed_ms_{0};
  bool fetch_requested_{false};
  bool force_fetch_{false};
  bool startup_gate_released_{false};
  uint32_t setup_started_ms_{0};

  FirmwareEntry flash_entry_{};
  const esp_partition_t *staging_partition_{nullptr};
  StagingHeader staged_header_{};
  bool staged_image_valid_{false};
  bool finalize_staging_after_check_{false};
  FirmwareProbeResult firmware_probe_result_{};
  std::atomic<bool> firmware_probe_running_{false};
  std::atomic<bool> firmware_probe_done_{false};
  FirmwareDownloadResult firmware_download_result_{};
  mbedtls_sha256_context firmware_download_sha_context_{};
  mbedtls_sha256_context staged_image_sha_context_{};
  bool staged_image_sha_active_{false};
  std::atomic<bool> firmware_download_running_{false};
  std::atomic<bool> firmware_download_done_{false};
  std::atomic<DownloadPhase> firmware_download_phase_{DownloadPhase::IDLE};
  std::atomic<size_t> firmware_staging_erase_bytes_{0};
  std::atomic<size_t> firmware_download_bytes_{0};
  std::atomic<size_t> firmware_download_total_{0};
  FlashState flash_state_{FlashState::IDLE};
  uint32_t flash_started_ms_{0};
  uint32_t flash_stage_started_ms_{0};
  uint32_t next_flash_action_ms_{0};
  size_t flash_work_offset_{0};
  size_t flash_image_size_{0};
  std::array<uint8_t, 32> flash_image_digest_{};
  mbedtls_sha256_context simulated_write_sha_context_{};
  mbedtls_sha256_context simulated_verify_sha_context_{};
  std::array<uint8_t, 32> simulated_write_digest_{};
  std::array<uint8_t, 32> simulated_verify_digest_{};
  bool simulated_write_sha_active_{false};
  bool simulated_verify_sha_active_{false};
  uint8_t last_flash_progress_{255};
  uint8_t next_flash_progress_log_{10};
  uint8_t staging_scratch_[STAGING_IO_BLOCK_SIZE]{};
};

class FirmwareRoleSelect : public select::Select,
                           public Component,
                           public Parented<ZigbeeFirmwareManager> {
 public:
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::HARDWARE; }

 protected:
  void control(size_t index) override {
    this->parent_->set_target_role(this->option_at(index));
  }
};

class FirmwareVersionSelect : public select::Select,
                              public Component,
                              public Parented<ZigbeeFirmwareManager> {
 public:
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::HARDWARE; }

 protected:
  void control(size_t index) override {
    this->parent_->set_target_firmware(this->option_at(index));
  }
};

class FirmwareCatalogRefreshButton : public button::Button,
                                     public Parented<ZigbeeFirmwareManager> {
 protected:
  void press_action() override { this->parent_->request_refresh(false); }
};

class FirmwareUpdateSimulationButton : public button::Button,
                                       public Parented<ZigbeeFirmwareManager> {
 protected:
  void press_action() override { this->parent_->start_flash_simulation(); }
};

class StagedFirmwareInvalidateButton : public button::Button,
                                       public Parented<ZigbeeFirmwareManager> {
 protected:
  void press_action() override { this->parent_->invalidate_staged_firmware(); }
};

}  // namespace esphome::zigbee_gateway
