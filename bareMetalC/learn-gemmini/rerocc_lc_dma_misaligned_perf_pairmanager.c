#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "encoding.h"
#include "util.h"
#include "include/gemmini_testutils.h"
#include "include/rerocc_coupleddma.h"
#include "rerocc-linux-tests/rerocc_control.h"

extern void printstr(const char *s);

#ifndef NUM_CORES
#define NUM_CORES 1
#endif

#ifndef REROCC_GEMMINI_BASE_ID
#define REROCC_GEMMINI_BASE_ID 0
#endif

#ifndef REROCC_DMA_BASE_ID
#define REROCC_DMA_BASE_ID REROCC_GEMMINI_BASE_ID
#endif

#ifndef REROCC_PAIR_MANAGER_MODE
#define REROCC_PAIR_MANAGER_MODE 1
#endif

#ifndef REROCC_CFG_ID
#define REROCC_CFG_ID 16U
#endif

#ifndef REROCC_DMA_BYTES
#define REROCC_DMA_BYTES 1024U
#endif

#ifndef REROCC_DMA_OFFSET_MAX
#define REROCC_DMA_OFFSET_MAX 64U
#endif

#ifndef REROCC_ACQUIRE_MAX_RETRIES
#define REROCC_ACQUIRE_MAX_RETRIES 1000000UL
#endif

#ifndef REROCC_DMA_WAIT_SPINS
#define REROCC_DMA_WAIT_SPINS 20000UL
#endif

#ifndef REROCC_MANAGER0_ID
#define REROCC_MANAGER0_ID 0
#endif

#ifndef REROCC_MANAGER1_ID
#define REROCC_MANAGER1_ID 1
#endif

#ifndef REROCC_DMA_CASE_FIRST
#define REROCC_DMA_CASE_FIRST 0U
#endif

#ifndef REROCC_DMA_CASE_COUNT
#define REROCC_DMA_CASE_COUNT 0U
#endif

#ifndef REROCC_DMA_VERBOSE
#define REROCC_DMA_VERBOSE 0U
#endif

#ifndef REROCC_DMA_STAGE_TRACE
#define REROCC_DMA_STAGE_TRACE 0U
#endif

#ifndef REROCC_DMA_RELEASE_LAST_CASE
#define REROCC_DMA_RELEASE_LAST_CASE 0U
#endif

#ifndef REROCC_DMA_RELEASE_ALL_ON_EXIT
#define REROCC_DMA_RELEASE_ALL_ON_EXIT 0U
#endif

#ifndef REROCC_DMA_USE_FENCE
#define REROCC_DMA_USE_FENCE 0U
#endif

#ifndef REROCC_DMA_VERIFY_DATA
#define REROCC_DMA_VERIFY_DATA 1U
#endif

#define DMA_MON_VALID 0ULL
#define DMA_MON_SRC_CMDS 1ULL
#define DMA_MON_DST_CMDS 2ULL
#define DMA_MON_REQ_COPY_BYTES 3ULL
#define DMA_MON_CYCLES 4ULL
#define DMA_MON_EFFECTIVE_BYTES 5ULL
#define DMA_MON_EFF_BW_X1000_BPC 6ULL

#define SHARED_SPAD_GLOBAL_ADDR_BASE 0x40000000ULL
#define SHARED_SPAD_LOCAL_SIZE (1024ULL * 1024ULL)
#define SHARED_SPAD_LOCAL_ADDR_BASE(i) \
  (SHARED_SPAD_GLOBAL_ADDR_BASE + SHARED_SPAD_LOCAL_SIZE * (uint64_t)(i))
#define SHARED_DMA_A_OFFSET 0x10000ULL
#define SHARED_DMA_B_OFFSET 0x20000ULL

typedef struct {
  const char *name;
  int manager_id;
  bool src_shared;
  bool dst_shared;
  size_t src_offset;
  size_t dst_offset;
} dma_case_t;

typedef struct {
  uint64_t src_cmds;
  uint64_t dst_cmds;
  uint64_t req_bytes;
  uint64_t cycles;
  uint64_t effective_bytes;
} dma_monitor_snapshot_t;

typedef struct {
  size_t case_index;
  uint64_t src_mod64;
  uint64_t dst_mod64;
  uint64_t core_cycles;
  uint64_t dma_cycles;
  uint64_t req_bytes;
  uint64_t effective_bytes;
  uint64_t src_cmds;
  uint64_t dst_cmds;
  bool ok;
} dma_case_result_t;

#define LOGF(...) do { \
  printf(__VA_ARGS__); \
} while (0)

#define VLOGF(...) do { \
  if (REROCC_DMA_VERBOSE) { \
    printf(__VA_ARGS__); \
  } \
} while (0)

#define TRACE_STAGE(msg) do { \
  if (REROCC_DMA_STAGE_TRACE) { \
    printstr(msg); \
  } \
} while (0)

static uint8_t dram_src_buf[REROCC_DMA_BYTES + REROCC_DMA_OFFSET_MAX] __attribute__((aligned(4096)));
static uint8_t dram_dst_buf[REROCC_DMA_BYTES + REROCC_DMA_OFFSET_MAX] __attribute__((aligned(4096)));
static uint8_t gold_buf[REROCC_DMA_BYTES] __attribute__((aligned(64)));
static volatile uint32_t dma_completion __attribute__((aligned(64)));

#define DRAM_TO_SHARED_SRC_CASE(off) \
  { "dram_to_shared_src" #off "_dst0", REROCC_MANAGER0_ID, false, true, off, 0U }

static const dma_case_t g_cases[] = {
  DRAM_TO_SHARED_SRC_CASE(0U),
  DRAM_TO_SHARED_SRC_CASE(1U),
  DRAM_TO_SHARED_SRC_CASE(2U),
  DRAM_TO_SHARED_SRC_CASE(4U),
  DRAM_TO_SHARED_SRC_CASE(8U),
  DRAM_TO_SHARED_SRC_CASE(15U),
  DRAM_TO_SHARED_SRC_CASE(16U),
  DRAM_TO_SHARED_SRC_CASE(17U),
  DRAM_TO_SHARED_SRC_CASE(31U),
  DRAM_TO_SHARED_SRC_CASE(32U),
  DRAM_TO_SHARED_SRC_CASE(47U),
  DRAM_TO_SHARED_SRC_CASE(48U),
  DRAM_TO_SHARED_SRC_CASE(63U),
  { "shared_to_dram_aligned_src0_dst0", REROCC_MANAGER1_ID, true, false, 0U, 0U },
  { "shared_to_dram_misalign_src0_dst16", REROCC_MANAGER1_ID, true, false, 0U, 16U },
  { "shared_to_dram_misalign_src16_dst48", REROCC_MANAGER1_ID, true, false, 16U, 48U },
};

static bool rr_acquire_cfg_with_retry(uint32_t cfg_id, uint64_t manager_id) {
  unsigned long retries = 0;
  while (!rr_acquire_cfg(cfg_id, manager_id)) {
    retries++;
    if (REROCC_ACQUIRE_MAX_RETRIES != 0UL && retries >= REROCC_ACQUIRE_MAX_RETRIES) {
      return false;
    }
    asm volatile("nop");
  }
  return true;
}

static void init_gold_pattern(uint8_t *buf, size_t n) {
  for (size_t i = 0; i < n; i++) {
    buf[i] = (uint8_t)(i * 13U + 7U);
  }
}

static bool buffers_equal(const uint8_t *lhs, const uint8_t *rhs, size_t n) {
  size_t i = 0;
  const uintptr_t lhs_addr = (uintptr_t)lhs;
  const uintptr_t rhs_addr = (uintptr_t)rhs;

  if ((lhs_addr & (sizeof(uint64_t) - 1U)) == (rhs_addr & (sizeof(uint64_t) - 1U))) {
    while (i < n && (((lhs_addr + i) & (sizeof(uint64_t) - 1U)) != 0U)) {
      if (lhs[i] != rhs[i]) {
        return false;
      }
      i++;
    }

    for (; i + sizeof(uint64_t) <= n; i += sizeof(uint64_t)) {
      const uint64_t lhs64 = *(const uint64_t *)(const void *)(lhs + i);
      const uint64_t rhs64 = *(const uint64_t *)(const void *)(rhs + i);
      if (lhs64 != rhs64) {
        return false;
      }
    }
  }

  for (; i < n; i++) {
    if (lhs[i] != rhs[i]) {
      return false;
    }
  }
  return true;
}

static void shared_byte_copy(volatile uint8_t *dst, const uint8_t *src, size_t n) {
  size_t i = 0;
  const uintptr_t dst_addr = (uintptr_t)dst;
  const uintptr_t src_addr = (uintptr_t)src;

  if ((dst_addr & (sizeof(uint64_t) - 1U)) == (src_addr & (sizeof(uint64_t) - 1U))) {
    while (i < n && (((dst_addr + i) & (sizeof(uint64_t) - 1U)) != 0U)) {
      dst[i] = src[i];
      i++;
    }

    for (; i + sizeof(uint64_t) <= n; i += sizeof(uint64_t)) {
      *(volatile uint64_t *)(volatile void *)(dst + i) =
        *(const uint64_t *)(const void *)(src + i);
    }
  }

  for (; i < n; i++) {
    dst[i] = src[i];
  }
}

static void shared_zero_region(volatile uint8_t *dst, size_t n) {
  volatile uint64_t *dst64 = (volatile uint64_t *)(uintptr_t)dst;
  size_t n64 = n / sizeof(uint64_t);

  for (size_t i = 0; i < n64; i++) {
    dst64[i] = 0ULL;
  }

  for (size_t i = n64 * sizeof(uint64_t); i < n; i++) {
    dst[i] = 0;
  }
}

static inline int dma_manager_to_shared_gid(int dma_manager_id) {
#if REROCC_PAIR_MANAGER_MODE
  return dma_manager_id - REROCC_GEMMINI_BASE_ID;
#else
  return dma_manager_id - REROCC_DMA_BASE_ID + REROCC_GEMMINI_BASE_ID;
#endif
}

static inline void bind_dma_pair_cfg(uint32_t cfg_id) {
  rr_set_opc(2, cfg_id);
#if REROCC_PAIR_MANAGER_MODE
  rr_set_opc(3, cfg_id);
#endif
}

static inline dma_monitor_snapshot_t read_dma_monitor_snapshot(void) {
  dma_monitor_snapshot_t snap;
  snap.src_cmds = rerocc_coupleddma_read_monitor(DMA_MON_SRC_CMDS);
  snap.dst_cmds = rerocc_coupleddma_read_monitor(DMA_MON_DST_CMDS);
  snap.req_bytes = rerocc_coupleddma_read_monitor(DMA_MON_REQ_COPY_BYTES);
  snap.cycles = rerocc_coupleddma_read_monitor(DMA_MON_CYCLES);
  snap.effective_bytes = rerocc_coupleddma_read_monitor(DMA_MON_EFFECTIVE_BYTES);
  return snap;
}

static char *append_cstr(char *dst, const char *src) {
  while (*src != '\0') {
    *dst++ = *src++;
  }
  return dst;
}

static char *append_hex_u64(char *dst, uint64_t value) {
  static const char hex_digits[] = "0123456789abcdef";
  bool started = false;

  *dst++ = '0';
  *dst++ = 'x';
  for (int shift = 60; shift >= 0; shift -= 4) {
    const uint8_t digit = (value >> shift) & 0xfU;
    if (digit != 0U || started || shift == 0) {
      *dst++ = hex_digits[digit];
      started = true;
    }
  }
  return dst;
}

static void emit_case_result(const dma_case_result_t *result) {
  char buf[160];
  char *p = buf;
  p = append_cstr(p, "M i=");
  p = append_hex_u64(p, result->case_index);
  p = append_cstr(p, " s=");
  p = append_hex_u64(p, result->src_mod64);
  p = append_cstr(p, " d=");
  p = append_hex_u64(p, result->dst_mod64);
  p = append_cstr(p, " c=");
  p = append_hex_u64(p, result->core_cycles);
  p = append_cstr(p, " m=");
  p = append_hex_u64(p, result->dma_cycles);
  p = append_cstr(p, " sc=");
  p = append_hex_u64(p, result->src_cmds);
  p = append_cstr(p, " dc=");
  p = append_hex_u64(p, result->dst_cmds);
  p = append_cstr(p, " ok=");
  p = append_cstr(p, result->ok ? "1\n" : "0\n");
  *p = '\0';
  printstr(buf);
}

static bool dma_issue_copy_and_wait(uint64_t src, uint64_t dst, size_t nbytes) {
  dma_completion = 0U;
  asm volatile("fence rw, rw" ::: "memory");
  rerocc_coupleddma_set_dst(dst, (uint64_t)(uintptr_t)&dma_completion);
  rerocc_coupleddma_set_src(src, (uint64_t)nbytes);

  for (unsigned long spin = 0; spin < REROCC_DMA_WAIT_SPINS; spin++) {
    asm volatile("fence r, rw" ::: "memory");
    if (dma_completion != 0U) {
      dma_completion = 0U;
      asm volatile("fence" ::: "memory");
      return true;
    }
    asm volatile("nop");
  }

  return false;
}

static bool run_dma_case(size_t case_index, const dma_case_t *tc) {
  const uint32_t cfg_id = REROCC_CFG_ID;
  const int gid = dma_manager_to_shared_gid(tc->manager_id);
  volatile uint8_t *shared_src_base =
    (volatile uint8_t *)(uintptr_t)(SHARED_SPAD_LOCAL_ADDR_BASE(gid) + SHARED_DMA_A_OFFSET);
  volatile uint8_t *shared_dst_base =
    (volatile uint8_t *)(uintptr_t)(SHARED_SPAD_LOCAL_ADDR_BASE(gid) + SHARED_DMA_B_OFFSET);
  uint8_t *dram_src = dram_src_buf + tc->src_offset;
  uint8_t *dram_dst = dram_dst_buf + tc->dst_offset;
  volatile uint8_t *shared_src = shared_src_base + tc->src_offset;
  volatile uint8_t *shared_dst = shared_dst_base + tc->dst_offset;
  uint64_t src_addr = tc->src_shared ? (uint64_t)(uintptr_t)shared_src : (uint64_t)(uintptr_t)dram_src;
  uint64_t dst_addr = tc->dst_shared ? (uint64_t)(uintptr_t)shared_dst : (uint64_t)(uintptr_t)dram_dst;
  const uint8_t *compare_ptr = tc->dst_shared ? (const uint8_t *)shared_dst : (const uint8_t *)dram_dst;
  dma_monitor_snapshot_t mon_before;
  dma_monitor_snapshot_t mon_after;
  uint64_t cycle_before;
  uint64_t cycle_after;
  bool completed;
  bool data_ok = false;
  bool case_ok = false;
  dma_case_result_t result;

  TRACE_STAGE("TRACE case_begin\n");
  VLOGF("CASE_BEGIN i=%u name=%s manager=%d src_shared=%u dst_shared=%u src_mod64=%llu dst_mod64=%llu bytes=%u wait_spins=%lu\n",
        (unsigned)case_index,
        tc->name,
        tc->manager_id,
        tc->src_shared ? 1U : 0U,
        tc->dst_shared ? 1U : 0U,
        (unsigned long long)(src_addr & 63ULL),
        (unsigned long long)(dst_addr & 63ULL),
        (unsigned)REROCC_DMA_BYTES,
        (unsigned long)REROCC_DMA_WAIT_SPINS);

  if (tc->src_shared) {
    shared_byte_copy(shared_src, gold_buf, REROCC_DMA_BYTES);
  } else {
    memcpy(dram_src, gold_buf, REROCC_DMA_BYTES);
  }

  VLOGF("CASE_STAGE %s prepared\n", tc->name);

  mon_before = read_dma_monitor_snapshot();
  cycle_before = read_cycles();
  VLOGF("CASE_STAGE %s issued\n", tc->name);
  TRACE_STAGE("TRACE dma_issue\n");
  completed = dma_issue_copy_and_wait(src_addr, dst_addr, REROCC_DMA_BYTES);
  TRACE_STAGE("TRACE dma_wait_done\n");
  if (completed && REROCC_DMA_USE_FENCE) {
    rr_fence(cfg_id);
    TRACE_STAGE("TRACE dma_fence_done\n");
  }
  cycle_after = read_cycles();
  mon_after = read_dma_monitor_snapshot();

  if (!completed) {
    LOGF("CASE_FAIL %s reason=timeout manager=%d cfg=%u src=0x%llx dst=0x%llx bytes=%u\n",
           tc->name,
           tc->manager_id,
           cfg_id,
           (unsigned long long)src_addr,
           (unsigned long long)dst_addr,
           (unsigned)REROCC_DMA_BYTES);
  } else {
    if (REROCC_DMA_VERIFY_DATA) {
      data_ok = buffers_equal(gold_buf, compare_ptr, REROCC_DMA_BYTES);
      if (!data_ok) {
        LOGF("CASE_FAIL %s reason=data_mismatch manager=%d cfg=%u src=0x%llx dst=0x%llx bytes=%u\n",
               tc->name,
               tc->manager_id,
               cfg_id,
               (unsigned long long)src_addr,
               (unsigned long long)dst_addr,
               (unsigned)REROCC_DMA_BYTES);
      }
    } else {
      data_ok = true;
    }
  }
  TRACE_STAGE("TRACE data_checked\n");

  result.case_index = case_index;
  result.src_mod64 = src_addr & 63ULL;
  result.dst_mod64 = dst_addr & 63ULL;
  result.core_cycles = cycle_after - cycle_before;
  result.dma_cycles = mon_after.cycles - mon_before.cycles;
  result.req_bytes = mon_after.req_bytes - mon_before.req_bytes;
  result.effective_bytes = mon_after.effective_bytes - mon_before.effective_bytes;
  result.src_cmds = mon_after.src_cmds - mon_before.src_cmds;
  result.dst_cmds = mon_after.dst_cmds - mon_before.dst_cmds;
  case_ok = completed && data_ok;
  result.ok = case_ok;
  emit_case_result(&result);
  return case_ok;
}

void thread_entry(int cid, int nc) {
  (void)nc;

  if (cid != 0) {
    while (1) {
      asm volatile("wfi");
    }
  }

  const size_t total_cases = sizeof(g_cases) / sizeof(g_cases[0]);
  const size_t first_case = REROCC_DMA_CASE_FIRST;
  const size_t requested_count = REROCC_DMA_CASE_COUNT;
  const size_t end_case =
    requested_count == 0U ? total_cases :
      ((first_case + requested_count) < total_cases ? (first_case + requested_count) : total_cases);
  bool all_ok = true;
  int active_manager_id = -1;
  rr_release(REROCC_CFG_ID);

  VLOGF("PERF_TEST_START cfg=%u total_cases=%u case_first=%u case_end=%u bytes=%u wait_spins=%u\n",
        (unsigned)REROCC_CFG_ID,
        (unsigned)total_cases,
        (unsigned)first_case,
        (unsigned)end_case,
        (unsigned)REROCC_DMA_BYTES,
        (unsigned)REROCC_DMA_WAIT_SPINS);
  init_gold_pattern(gold_buf, REROCC_DMA_BYTES);
  for (size_t i = first_case; i < end_case; i++) {
    const dma_case_t *tc = &g_cases[i];
    const bool manager_changed = active_manager_id != tc->manager_id;

    if (manager_changed && active_manager_id >= 0) {
      rr_release(REROCC_CFG_ID);
    }

    if (manager_changed) {
      TRACE_STAGE("TRACE acquire_begin\n");
      if (!rr_acquire_cfg_with_retry(REROCC_CFG_ID, (uint64_t)tc->manager_id)) {
        LOGF("CASE_FAIL %s reason=acquire manager=%d cfg=%u\n", tc->name, tc->manager_id, REROCC_CFG_ID);
        emit_case_result(&(dma_case_result_t){
          .case_index = i,
          .src_mod64 = tc->src_offset & 63U,
          .dst_mod64 = tc->dst_offset & 63U,
          .core_cycles = 0,
          .dma_cycles = 0,
          .req_bytes = 0,
          .effective_bytes = 0,
          .src_cmds = 0,
          .dst_cmds = 0,
          .ok = false,
        });
        all_ok = false;
        active_manager_id = -1;
        continue;
      }
      TRACE_STAGE("TRACE acquire_done\n");
      bind_dma_pair_cfg(REROCC_CFG_ID);
      active_manager_id = tc->manager_id;
    }

    all_ok = run_dma_case(i, tc) && all_ok;
  }

  if (active_manager_id >= 0 && REROCC_DMA_RELEASE_LAST_CASE) {
    rr_release(REROCC_CFG_ID);
  }

  if (REROCC_DMA_RELEASE_ALL_ON_EXIT) {
    rr_release_all(RR_MAX_CFGS);
  }
  if (all_ok) {
    printstr("DONE ok=1\n");
    exit(0);
  }

  printstr("DONE ok=0\n");
  exit(1);
}

int main(void) {
  return 1;
}
