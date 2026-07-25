#include <cassert>
#include <cstdint>
#include <cstring>

#include "components/zigbee_gateway/zigbee_metadata_cache.h"

using esphome::zigbee_gateway::RADIO_METADATA_CHANNEL;
using esphome::zigbee_gateway::RADIO_METADATA_HARDWARE;
using esphome::zigbee_gateway::RADIO_METADATA_ON_NETWORK;
using esphome::zigbee_gateway::RadioMetadataCache;
using esphome::zigbee_gateway::copy_radio_metadata_text;
using esphome::zigbee_gateway::initialize_radio_metadata_cache;
using esphome::zigbee_gateway::valid_radio_metadata_cache;

int main() {
  RadioMetadataCache cache{};
  assert(!valid_radio_metadata_cache(cache));

  initialize_radio_metadata_cache(&cache, 41);
  assert(valid_radio_metadata_cache(cache));
  assert(cache.generation == 41);
  assert(cache.dirty == 0);
  assert(cache.known == 0);

  assert(copy_radio_metadata_text(cache.hardware, "CC2652P7"));
  cache.known |= RADIO_METADATA_HARDWARE;
  cache.channel = 25;
  cache.known |= RADIO_METADATA_CHANNEL;
  assert(valid_radio_metadata_cache(cache));

  cache.dirty = 1;
  assert(valid_radio_metadata_cache(cache));
  cache.dirty = 2;
  assert(!valid_radio_metadata_cache(cache));
  cache.dirty = 0;

  cache.known |= RADIO_METADATA_ON_NETWORK;
  cache.on_network = 2;
  assert(!valid_radio_metadata_cache(cache));
  cache.on_network = 1;
  assert(valid_radio_metadata_cache(cache));

  const uint32_t known = cache.known;
  cache.known |= 1u << 31;
  assert(!valid_radio_metadata_cache(cache));
  cache.known = known;

  const uint16_t schema = cache.schema;
  cache.schema++;
  assert(!valid_radio_metadata_cache(cache));
  cache.schema = schema;

  std::memset(cache.role, 'R', sizeof(cache.role));
  assert(!valid_radio_metadata_cache(cache));
  cache.role[sizeof(cache.role) - 1] = '\0';
  assert(valid_radio_metadata_cache(cache));

  char too_long[sizeof(cache.firmware) + 1];
  std::memset(too_long, '1', sizeof(too_long) - 1);
  too_long[sizeof(too_long) - 1] = '\0';
  assert(!copy_radio_metadata_text(cache.firmware, too_long));
  assert(cache.firmware[0] == '\0');

  return 0;
}
