#include <os>
#include <kprint>
#include <util/crc32.hpp>

#define SOFT_RESET_MAGIC    0xFEE1DEAD
#define SOFT_RESET_LOCATION 0x7000

namespace x86 {
  extern uint32_t apic_timer_get_ticks() noexcept;
  extern void     apic_timer_set_ticks(uint32_t) noexcept;
}

struct softreset_t
{
  uint32_t  checksum;
  uint64_t  liveupdate_loc;
  uint64_t  high_mem;
  MHz       cpu_freq;
  uint32_t  apic_ticks;
  uint64_t  extra;
  uint32_t  extra_len;
};

bool OS::is_softreset_magic(uint32_t value)
{
  return value == SOFT_RESET_MAGIC;
}

__attribute__((weak))
void softreset_service_handler(const void*, size_t) {}

void OS::resume_softreset(intptr_t addr)
{
  auto* data = (softreset_t*) addr;

  /// validate soft-reset data
  const uint32_t csum_copy = data->checksum;
  data->checksum = 0;
  uint32_t crc = crc32_fast(data, sizeof(softreset_t));
  if (crc != csum_copy) {
    kprintf("[!] Failed to verify CRC of softreset data: %08x vs %08x\n",
            crc, csum_copy);
    return;
  }
  data->checksum = csum_copy;

  /// restore known values
  OS::liveupdate_loc_ = data->liveupdate_loc;
  OS::memory_end_     = data->high_mem;
  OS::cpu_mhz_        = data->cpu_freq;
  x86::apic_timer_set_ticks(data->apic_ticks);
  OS::m_is_live_updated = true;

  /// call service-specific softreset handler
  softreset_service_handler((void*) data->extra, data->extra_len);
}

extern "C"
void* __os_store_soft_reset(void* extra, size_t extra_len)
{
  // store softreset data in low memory
  auto* data = (softreset_t*) SOFT_RESET_LOCATION;
  data->checksum    = 0;
  data->liveupdate_loc = (uintptr_t) OS::liveupdate_storage_area();
  data->high_mem    = OS::memory_end();
  data->cpu_freq    = OS::cpu_freq();
  data->apic_ticks  = x86::apic_timer_get_ticks();
  data->extra       = (uint64_t) extra;
  data->extra_len   = extra_len;

  uint32_t csum = crc32_fast(data, sizeof(softreset_t));
  data->checksum = csum;
  return data;
}
