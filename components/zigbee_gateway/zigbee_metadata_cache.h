#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <type_traits>

namespace esphome {
namespace zigbee_gateway {

// Physical identity, running-image metadata, and network state have independent
// invalidation rules and persistence records.
static constexpr uint16_t ZIGBEE_CACHE_SCHEMA = 2;
static constexpr uint32_t PHYSICAL_IDENTITY_CACHE_MAGIC = 0x5A475048u;  // "ZGPH"
static constexpr uint32_t RUNNING_IMAGE_CACHE_MAGIC = 0x5A47494Du;     // "ZGIM"
static constexpr uint32_t NETWORK_SNAPSHOT_CACHE_MAGIC = 0x5A474E57u;  // "ZGNW"

enum PhysicalIdentityKnown : uint32_t {
  PHYSICAL_IDENTITY_HARDWARE = 1u << 0,
  PHYSICAL_IDENTITY_FLASH_SIZE = 1u << 1,
  PHYSICAL_IDENTITY_FACTORY_IEEE = 1u << 2,
};

static constexpr uint32_t PHYSICAL_IDENTITY_ALL_KNOWN =
    PHYSICAL_IDENTITY_HARDWARE | PHYSICAL_IDENTITY_FLASH_SIZE | PHYSICAL_IDENTITY_FACTORY_IEEE;

/// Lifetime properties of the soldered Zigbee radio.
///
/// These values do not become dirty when application firmware is replaced.
/// Only a missing/incompatible record causes another physical identification
/// pass. Flash-resident CCFG values intentionally live in RunningImageCache.
struct PhysicalIdentityCache {
  uint32_t magic;
  uint16_t schema;
  uint16_t size;
  uint32_t generation;
  uint32_t known;

  uint8_t chip_family;
  uint8_t protocols;
  uint8_t flash_pages;
  uint8_t reserved8;
  uint16_t chip_id_16;
  uint16_t reserved16;

  uint32_t chip_id_be;
  uint32_t wafer_id;
  uint32_t pg_rev;
  uint32_t flash_size_bytes;

  char hardware[80];
  char factory_ieee[24];
};

enum RunningImageKnown : uint32_t {
  RUNNING_IMAGE_FIRMWARE = 1u << 0,
  RUNNING_IMAGE_STACK = 1u << 1,
  RUNNING_IMAGE_ACTIVE_IEEE = 1u << 2,
  RUNNING_IMAGE_ROLE = 1u << 3,
  RUNNING_IMAGE_MODE_CFG = 1u << 4,
  RUNNING_IMAGE_BSL_CFG = 1u << 5,
};

static constexpr uint32_t RUNNING_IMAGE_ALL_KNOWN =
    RUNNING_IMAGE_FIRMWARE | RUNNING_IMAGE_STACK | RUNNING_IMAGE_ACTIVE_IEEE |
    RUNNING_IMAGE_ROLE | RUNNING_IMAGE_MODE_CFG | RUNNING_IMAGE_BSL_CFG;

/// Information owned by the currently installed application image.
///
/// `awaiting_observation` is set before transparent BSL access. The physical
/// identity record remains valid while these mutable fields wait for either a
/// local diagnostic refresh or passive observation of the new image.
struct RunningImageCache {
  uint32_t magic;
  uint16_t schema;
  uint16_t size;
  uint32_t generation;
  uint32_t known;

  uint8_t awaiting_observation;
  uint8_t reserved[3];
  uint32_t mode_cfg;
  uint32_t bsl_cfg;

  char firmware[16];
  char stack[16];
  char active_ieee[24];
  char role[16];
};

enum NetworkSnapshotKnown : uint32_t {
  NETWORK_SNAPSHOT_PAN_ID = 1u << 0,
  NETWORK_SNAPSHOT_CHANNEL = 1u << 1,
  NETWORK_SNAPSHOT_ON_NETWORK = 1u << 2,
  NETWORK_SNAPSHOT_PARENT_IEEE = 1u << 3,
  NETWORK_SNAPSHOT_EXTENDED_PAN_ID = 1u << 4,
};

static constexpr uint32_t NETWORK_SNAPSHOT_ALL_KNOWN =
    NETWORK_SNAPSHOT_PAN_ID | NETWORK_SNAPSHOT_CHANNEL | NETWORK_SNAPSHOT_ON_NETWORK |
    NETWORK_SNAPSHOT_PARENT_IEEE | NETWORK_SNAPSHOT_EXTENDED_PAN_ID;

/// Last confirmed Zigbee network information.
///
/// A restored record is explicitly published as `Cached`; it is not dirtied
/// by a firmware transfer. Local NV diagnostics or a later passive ZNP
/// observation can replace the snapshot and its runtime provenance.
struct NetworkSnapshotCache {
  uint32_t magic;
  uint16_t schema;
  uint16_t size;
  uint32_t generation;
  uint32_t known;

  uint16_t pan_id;
  uint8_t channel;
  uint8_t on_network;

  char parent_ieee[24];
  char extended_pan_id[40];
};

static_assert(std::is_trivially_copyable<PhysicalIdentityCache>::value,
              "Physical identity preference must remain trivially copyable");
static_assert(std::is_trivially_copyable<RunningImageCache>::value,
              "Running image preference must remain trivially copyable");
static_assert(std::is_trivially_copyable<NetworkSnapshotCache>::value,
              "Network snapshot preference must remain trivially copyable");

inline void initialize_physical_identity_cache(PhysicalIdentityCache *cache, uint32_t generation = 0) {
  std::memset(cache, 0, sizeof(*cache));
  cache->magic = PHYSICAL_IDENTITY_CACHE_MAGIC;
  cache->schema = ZIGBEE_CACHE_SCHEMA;
  cache->size = sizeof(*cache);
  cache->generation = generation;
}

inline void initialize_running_image_cache(RunningImageCache *cache, uint32_t generation = 0) {
  std::memset(cache, 0, sizeof(*cache));
  cache->magic = RUNNING_IMAGE_CACHE_MAGIC;
  cache->schema = ZIGBEE_CACHE_SCHEMA;
  cache->size = sizeof(*cache);
  cache->generation = generation;
}

inline void initialize_network_snapshot_cache(NetworkSnapshotCache *cache, uint32_t generation = 0) {
  std::memset(cache, 0, sizeof(*cache));
  cache->magic = NETWORK_SNAPSHOT_CACHE_MAGIC;
  cache->schema = ZIGBEE_CACHE_SCHEMA;
  cache->size = sizeof(*cache);
  cache->generation = generation;
}

template<size_t N> inline bool zigbee_cache_text_is_terminated(const char (&value)[N]) {
  return std::memchr(value, '\0', N) != nullptr;
}

template<size_t N> inline bool copy_zigbee_cache_text(char (&destination)[N], const char *source) {
  std::memset(destination, 0, N);
  if (source == nullptr)
    return false;
  const size_t length = std::strlen(source);
  if (length == 0 || length >= N)
    return false;
  std::memcpy(destination, source, length);
  return true;
}

inline bool record_local_firmware_install(RunningImageCache *cache,
                                          const char *firmware,
                                          const char *role) {
  if (cache == nullptr)
    return false;

  cache->known = 0;
  cache->awaiting_observation = 1;
  cache->mode_cfg = 0;
  cache->bsl_cfg = 0;
  std::memset(cache->stack, 0, sizeof(cache->stack));
  std::memset(cache->active_ieee, 0, sizeof(cache->active_ieee));

  const bool firmware_valid = copy_zigbee_cache_text(cache->firmware, firmware);
  const bool role_valid = copy_zigbee_cache_text(cache->role, role);
  if (!firmware_valid || !role_valid) {
    std::memset(cache->firmware, 0, sizeof(cache->firmware));
    std::memset(cache->role, 0, sizeof(cache->role));
    return false;
  }

  cache->known = RUNNING_IMAGE_FIRMWARE | RUNNING_IMAGE_ROLE;
  return true;
}

inline bool mark_network_connection_unknown(NetworkSnapshotCache *cache) {
  if (cache == nullptr)
    return false;
  cache->known &= ~NETWORK_SNAPSHOT_ON_NETWORK;
  cache->on_network = 0;
  return true;
}

inline bool record_local_radio_erase(NetworkSnapshotCache *cache) {
  if (cache == nullptr)
    return false;
  const uint32_t next_generation = cache->generation + 1;
  initialize_network_snapshot_cache(cache, next_generation);
  cache->known = NETWORK_SNAPSHOT_ON_NETWORK;
  cache->on_network = 0;
  return true;
}

inline bool valid_physical_identity_cache(const PhysicalIdentityCache &cache) {
  if (cache.magic != PHYSICAL_IDENTITY_CACHE_MAGIC || cache.schema != ZIGBEE_CACHE_SCHEMA ||
      cache.size != sizeof(cache) || cache.chip_family > 2 ||
      (cache.known & ~PHYSICAL_IDENTITY_ALL_KNOWN) != 0)
    return false;
  return zigbee_cache_text_is_terminated(cache.hardware) &&
         zigbee_cache_text_is_terminated(cache.factory_ieee);
}

inline bool valid_running_image_cache(const RunningImageCache &cache) {
  if (cache.magic != RUNNING_IMAGE_CACHE_MAGIC || cache.schema != ZIGBEE_CACHE_SCHEMA ||
      cache.size != sizeof(cache) || cache.awaiting_observation > 1 ||
      (cache.known & ~RUNNING_IMAGE_ALL_KNOWN) != 0)
    return false;
  return zigbee_cache_text_is_terminated(cache.firmware) &&
         zigbee_cache_text_is_terminated(cache.stack) &&
         zigbee_cache_text_is_terminated(cache.active_ieee) &&
         zigbee_cache_text_is_terminated(cache.role);
}

inline bool valid_network_snapshot_cache(const NetworkSnapshotCache &cache) {
  if (cache.magic != NETWORK_SNAPSHOT_CACHE_MAGIC || cache.schema != ZIGBEE_CACHE_SCHEMA ||
      cache.size != sizeof(cache) || (cache.known & ~NETWORK_SNAPSHOT_ALL_KNOWN) != 0)
    return false;
  if ((cache.known & NETWORK_SNAPSHOT_ON_NETWORK) != 0 && cache.on_network > 1)
    return false;
  return zigbee_cache_text_is_terminated(cache.parent_ieee) &&
         zigbee_cache_text_is_terminated(cache.extended_pan_id);
}

// A local information refresh is read-only. Build its candidate from the
// last-known image without changing the authoritative cache; a failed probe can
// then retain every value and its pending state exactly as it was.
inline bool copy_running_image_for_local_refresh(
    const RunningImageCache &current, RunningImageCache *candidate) {
  if (candidate == nullptr || !valid_running_image_cache(current))
    return false;
  *candidate = current;
  candidate->generation++;
  return true;
}

}  // namespace zigbee_gateway
}  // namespace esphome
