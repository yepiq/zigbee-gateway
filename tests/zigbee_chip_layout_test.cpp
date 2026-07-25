#include <cassert>
#include <cstdint>

#include "components/zigbee_gateway/zigbee_chip_layout.h"

using namespace esphome::zigbee_gateway;

int main() {
  const ZigbeeChipLayout *x2 =
      chip_layout_for_family(ChipFamily::CC13X2_CC26X2);
  assert(x2 != nullptr);
  assert(x2->flash_size_unit_bytes == 0x1000);
  assert(x2->nv_base == 0x00050000);
  assert(x2->nv_size == 0x00006000);
  assert(x2->nv_page_size == 0x00002000);
  assert(nv_page_count(*x2) == 3);
  assert(88 * x2->flash_size_unit_bytes == 0x00058000);

  const ZigbeeChipLayout *x2x7 =
      chip_layout_for_family(ChipFamily::CC13X2X7_CC26X2X7);
  assert(x2x7 != nullptr);
  assert(x2x7->flash_size_unit_bytes == 0x2000);
  assert(x2x7->nv_base == 0x000A6000);
  assert(x2x7->nv_size == 0x00008000);
  assert(x2x7->nv_page_size == 0x00002000);
  assert(nv_page_count(*x2x7) == 4);
  assert(88 * x2x7->flash_size_unit_bytes == 0x000B0000);

  assert(chip_layout_for_family(ChipFamily::UNKNOWN) == nullptr);
  return 0;
}
