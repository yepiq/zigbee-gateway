#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <type_traits>

namespace esphome {
namespace zigbee_gateway {

static constexpr uint32_t RADIO_METADATA_CACHE_MAGIC = 0x5A474D44u;
static constexpr uint16_t RADIO_METADATA_CACHE_SCHEMA = 1;

enum RadioMetadataKnown : uint32_t {
  RADIO_METADATA_HARDWARE = 1u << 0,
  RADIO_METADATA_FLASH_SIZE = 1u << 1,
  RADIO_METADATA_FIRMWARE = 1u << 2,
  RADIO_METADATA_STACK = 1u << 3,
  RADIO_METADATA_SELF_IEEE = 1u << 4,
  RADIO_METADATA_ROLE = 1u << 5,
  RADIO_METADATA_PAN_ID = 1u << 6,
  RADIO_METADATA_CHANNEL = 1u << 7,
  RADIO_METADATA_ON_NETWORK = 1u << 8,
  RADIO_METADATA_PARENT_IEEE = 1u << 9,
  RADIO_METADATA_EXTENDED_PAN_ID = 1u << 10,
};

static constexpr uint32_t RADIO_METADATA_ALL_KNOWN =
    RADIO_METADATA_HARDWARE | RADIO_METADATA_FLASH_SIZE | RADIO_METADATA_FIRMWARE |
    RADIO_METADATA_STACK | RADIO_METADATA_SELF_IEEE | RADIO_METADATA_ROLE | RADIO_METADATA_PAN_ID |
    RADIO_METADATA_CHANNEL | RADIO_METADATA_ON_NETWORK | RADIO_METADATA_PARENT_IEEE |
    RADIO_METADATA_EXTENDED_PAN_ID;

/// One atomically stored snapshot of locally verified radio information.
///
/// Only fixed-size, trivially copyable fields belong here because ESPHome
/// preferences persist the complete object as a binary blob. The magic,
/// schema, and exact struct size deliberately invalidate incompatible records.
struct RadioMetadataCache {
  uint32_t magic;
  uint16_t schema;
  uint16_t size;
  uint32_t generation;
  uint32_t known;

  uint8_t dirty;
  uint8_t chip_family;
  uint8_t protocols;
  uint8_t flash_pages;
  uint16_t chip_id_16;
  uint16_t reserved;

  uint32_t chip_id_be;
  uint32_t wafer_id;
  uint32_t pg_rev;
  uint32_t flash_size_bytes;
  uint32_t mode_cfg;
  uint32_t bsl_cfg;
  uint16_t pan_id;
  uint8_t channel;
  uint8_t on_network;

  char hardware[80];
  char firmware[16];
  char stack[16];
  char self_ieee[24];
  char role[16];
  char parent_ieee[24];
  char extended_pan_id[40];
};

static_assert(std::is_trivially_copyable<RadioMetadataCache>::value,
              "Radio metadata preference must remain trivially copyable");

inline void initialize_radio_metadata_cache(RadioMetadataCache *cache, uint32_t generation = 0) {
  std::memset(cache, 0, sizeof(*cache));
  cache->magic = RADIO_METADATA_CACHE_MAGIC;
  cache->schema = RADIO_METADATA_CACHE_SCHEMA;
  cache->size = sizeof(*cache);
  cache->generation = generation;
}

template<size_t N> inline bool radio_metadata_text_is_terminated(const char (&value)[N]) {
  return std::memchr(value, '\0', N) != nullptr;
}

template<size_t N> inline bool copy_radio_metadata_text(char (&destination)[N], const char *source) {
  std::memset(destination, 0, N);
  if (source == nullptr)
    return false;
  const size_t length = std::strlen(source);
  if (length == 0 || length >= N)
    return false;
  std::memcpy(destination, source, length);
  return true;
}

inline bool valid_radio_metadata_cache(const RadioMetadataCache &cache) {
  if (cache.magic != RADIO_METADATA_CACHE_MAGIC || cache.schema != RADIO_METADATA_CACHE_SCHEMA ||
      cache.size != sizeof(cache) || cache.dirty > 1 || cache.chip_family > 2 ||
      (cache.known & ~RADIO_METADATA_ALL_KNOWN) != 0)
    return false;
  if ((cache.known & RADIO_METADATA_ON_NETWORK) != 0 && cache.on_network > 1)
    return false;
  return radio_metadata_text_is_terminated(cache.hardware) &&
         radio_metadata_text_is_terminated(cache.firmware) &&
         radio_metadata_text_is_terminated(cache.stack) &&
         radio_metadata_text_is_terminated(cache.self_ieee) &&
         radio_metadata_text_is_terminated(cache.role) &&
         radio_metadata_text_is_terminated(cache.parent_ieee) &&
         radio_metadata_text_is_terminated(cache.extended_pan_id);
}

}  // namespace zigbee_gateway
}  // namespace esphome
