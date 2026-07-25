#include <cassert>
#include <cstdint>
#include <cstring>

#include "components/zigbee_gateway/zigbee_metadata_cache.h"

using namespace esphome::zigbee_gateway;

int main() {
  PhysicalIdentityCache physical{};
  RunningImageCache image{};
  NetworkSnapshotCache network{};
  assert(!valid_physical_identity_cache(physical));
  assert(!valid_running_image_cache(image));
  assert(!valid_network_snapshot_cache(network));

  initialize_physical_identity_cache(&physical, 11);
  initialize_running_image_cache(&image, 22);
  initialize_network_snapshot_cache(&network, 33);
  assert(valid_physical_identity_cache(physical));
  assert(valid_running_image_cache(image));
  assert(valid_network_snapshot_cache(network));
  assert(physical.generation == 11);
  assert(image.generation == 22);
  assert(network.generation == 33);
  assert(image.awaiting_observation == 0);

  // Physical identity has no dirty/pending field. BSL invalidation belongs
  // exclusively to the running-image record.
  assert(copy_zigbee_cache_text(physical.hardware, "CC2652P7"));
  assert(copy_zigbee_cache_text(physical.factory_ieee, "00:11:22:33:44:55:66:77"));
  physical.known |= PHYSICAL_IDENTITY_HARDWARE | PHYSICAL_IDENTITY_FACTORY_IEEE;
  physical.flash_size_bytes = 704 * 1024;
  physical.known |= PHYSICAL_IDENTITY_FLASH_SIZE;
  assert(valid_physical_identity_cache(physical));

  image.awaiting_observation = 1;
  assert(valid_running_image_cache(image));
  image.awaiting_observation = 2;
  assert(!valid_running_image_cache(image));
  image.awaiting_observation = 0;
  assert(copy_zigbee_cache_text(image.firmware, "20260726"));
  assert(copy_zigbee_cache_text(image.stack, "3.30.0"));
  assert(copy_zigbee_cache_text(image.active_ieee, "00:11:22:33:44:55:66:77"));
  assert(copy_zigbee_cache_text(image.role, "Coordinator"));
  image.known |= RUNNING_IMAGE_FIRMWARE | RUNNING_IMAGE_STACK |
                 RUNNING_IMAGE_ACTIVE_IEEE | RUNNING_IMAGE_ROLE;
  assert(valid_running_image_cache(image));

  network.pan_id = 0x1A62;
  network.channel = 15;
  network.on_network = 1;
  assert(copy_zigbee_cache_text(network.parent_ieee, "00:00:00:00:00:00:00:00"));
  assert(copy_zigbee_cache_text(network.extended_pan_id, "1 2 3 4 5 6 7 8"));
  network.known = NETWORK_SNAPSHOT_PAN_ID | NETWORK_SNAPSHOT_CHANNEL |
                  NETWORK_SNAPSHOT_ON_NETWORK | NETWORK_SNAPSHOT_PARENT_IEEE |
                  NETWORK_SNAPSHOT_EXTENDED_PAN_ID;
  assert(valid_network_snapshot_cache(network));
  network.on_network = 2;
  assert(!valid_network_snapshot_cache(network));
  network.on_network = 1;

  const uint32_t physical_known = physical.known;
  physical.known |= 1u << 31;
  assert(!valid_physical_identity_cache(physical));
  physical.known = physical_known;

  const uint32_t image_known = image.known;
  image.known |= 1u << 31;
  assert(!valid_running_image_cache(image));
  image.known = image_known;

  const uint32_t network_known = network.known;
  network.known |= 1u << 31;
  assert(!valid_network_snapshot_cache(network));
  network.known = network_known;

  physical.schema++;
  image.schema++;
  network.schema++;
  assert(!valid_physical_identity_cache(physical));
  assert(!valid_running_image_cache(image));
  assert(!valid_network_snapshot_cache(network));
  physical.schema = ZIGBEE_CACHE_SCHEMA;
  image.schema = ZIGBEE_CACHE_SCHEMA;
  network.schema = ZIGBEE_CACHE_SCHEMA;

  std::memset(image.role, 'R', sizeof(image.role));
  assert(!valid_running_image_cache(image));
  image.role[sizeof(image.role) - 1] = '\0';
  assert(valid_running_image_cache(image));

  char too_long[sizeof(image.firmware) + 1];
  std::memset(too_long, '1', sizeof(too_long) - 1);
  too_long[sizeof(too_long) - 1] = '\0';
  assert(!copy_zigbee_cache_text(image.firmware, too_long));
  assert(image.firmware[0] == '\0');

  return 0;
}
