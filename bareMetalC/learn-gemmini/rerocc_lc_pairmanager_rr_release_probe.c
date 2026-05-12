#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "encoding.h"
#include "util.h"
#include "rocc-software/src/xcustom.h"
#include "include/rerocc_coupleddma.h"
#include "rerocc-linux-tests/rerocc_control.h"

#ifndef NUM_CORES
#define NUM_CORES 1
#endif

#ifndef REROCC_NUM_GEMMINI
#define REROCC_NUM_GEMMINI 12
#endif

#ifndef REROCC_NUM_DMA
#define REROCC_NUM_DMA 12
#endif

#ifndef REROCC_GEMMINI_BASE_ID
#define REROCC_GEMMINI_BASE_ID 0
#endif

#ifndef REROCC_DMA_BASE_ID
#define REROCC_DMA_BASE_ID 0
#endif

#ifndef REROCC_PAIR_MANAGER_MODE
#define REROCC_PAIR_MANAGER_MODE 1
#endif

#ifndef REROCC_ACQUIRE_MAX_RETRIES
#define REROCC_ACQUIRE_MAX_RETRIES 1000000UL
#endif

#ifndef DMA_WAIT_SPINS
#define DMA_WAIT_SPINS 20000000UL
#endif

#ifndef REROCC_RR_PROBE_CFG_ID
#define REROCC_RR_PROBE_CFG_ID 0U
#endif

#ifndef REROCC_RR_PROBE_MANAGER_ID
#define REROCC_RR_PROBE_MANAGER_ID 0U
#endif

#ifndef REROCC_RR_PROBE_BYTES
#define REROCC_RR_PROBE_BYTES 4096U
#endif

#ifndef REROCC_RR_PROBE_MODE
#define REROCC_RR_PROBE_MODE 2
#endif

#ifndef REROCC_RR_PROBE_TRACERV_MARKERS
#define REROCC_RR_PROBE_TRACERV_MARKERS 1
#endif

#define PROBE_ACTIVE_CORE 0

typedef enum {
  MODE_BLOCKING_RELEASE = 0,
  MODE_RAW_RELEASE_READBACK = 1,
  MODE_RAW_RELEASE_RESTORE_OPC3 = 2,
  MODE_FENCE_RELEASE_RESTORE_OPC3 = 3,
} probe_mode_t;

static uint8_t probe_src[REROCC_RR_PROBE_BYTES] __attribute__((aligned(64)));
static uint8_t probe_dst[REROCC_RR_PROBE_BYTES] __attribute__((aligned(64)));
static volatile uint32_t probe_completion __attribute__((aligned(64)));

#if defined(__riscv) && REROCC_RR_PROBE_TRACERV_MARKERS
static inline void rr_probe_tracerv_start_marker(void) {
  asm volatile(".word 0x00008013" ::: "memory");
}

static inline void rr_probe_tracerv_end_marker(void) {
  asm volatile(".word 0x00010013" ::: "memory");
}
#else
static inline void rr_probe_tracerv_start_marker(void) {}
static inline void rr_probe_tracerv_end_marker(void) {}
#endif

static void probe_log_step(const char *step, uint64_t a, uint64_t b) {
  printf(
      "RRPROBE step=%s mode=%u cfg=%u manager=%u bytes=%u a=0x%llx b=0x%llx\n",
      step,
      (unsigned)REROCC_RR_PROBE_MODE,
      (unsigned)REROCC_RR_PROBE_CFG_ID,
      (unsigned)REROCC_RR_PROBE_MANAGER_ID,
      (unsigned)REROCC_RR_PROBE_BYTES,
      (unsigned long long)a,
      (unsigned long long)b);
}

static inline uint32_t lcg_next(uint32_t *state) {
  *state = (*state) * 1664525u + 1013904223u;
  return *state;
}

static void fill_pattern(uint8_t *buf, size_t n, uint32_t seed) {
  for (size_t index = 0; index < n; ++index) {
    seed = lcg_next(&seed);
    buf[index] = (uint8_t)seed;
  }
}

static bool buffers_equal(const uint8_t *lhs, const uint8_t *rhs, size_t n) {
  for (size_t index = 0; index < n; ++index) {
    if (lhs[index] != rhs[index]) {
      printf(
          "RRPROBE mismatch idx=%u lhs=0x%02x rhs=0x%02x\n",
          (unsigned)index,
          (unsigned)lhs[index],
          (unsigned)rhs[index]);
      return false;
    }
  }
  return true;
}

static bool rr_acquire_cfg_with_retry(uint32_t cfg_id, uint64_t manager_id) {
  unsigned long retries = 0;
  while (!rr_acquire_cfg(cfg_id, manager_id)) {
    retries += 1UL;
    if (REROCC_ACQUIRE_MAX_RETRIES != 0UL && retries >= REROCC_ACQUIRE_MAX_RETRIES) {
      return false;
    }
    asm volatile("nop");
  }
  return true;
}

static void issue_dma_copy(void) {
  probe_completion = 0;
  asm volatile("fence rw, rw");
  rerocc_coupleddma_set_dst((uint64_t)(uintptr_t)probe_dst, (uint64_t)(uintptr_t)&probe_completion);
  rerocc_coupleddma_set_src((uint64_t)(uintptr_t)probe_src, (uint64_t)REROCC_RR_PROBE_BYTES);
}

static bool wait_dma_completion(unsigned long max_spins) {
  for (unsigned long spin = 0; spin < max_spins; ++spin) {
    asm volatile("fence r, rw");
    if (probe_completion != 0U) {
      probe_completion = 0U;
      asm volatile("fence");
      return true;
    }
    asm volatile("nop");
  }
  return false;
}

static bool run_probe_sequence(void) {
  const uint32_t cfg_id = (uint32_t)REROCC_RR_PROBE_CFG_ID;
  const uint64_t manager_id = (uint64_t)REROCC_RR_PROBE_MANAGER_ID;
  uint64_t prev_opc2 = 0;
  uint64_t prev_opc3 = 0;
  uint64_t cfg_readback = 0;

  probe_log_step("probe-begin", (uint64_t)(uintptr_t)probe_src, (uint64_t)(uintptr_t)probe_dst);

  if (!rr_acquire_cfg_with_retry(cfg_id, manager_id)) {
    printf("RRPROBE acquire failed cfg=%u manager=%llu\n",
           (unsigned)cfg_id,
           (unsigned long long)manager_id);
    return false;
  }
  probe_log_step("after-acquire", cfg_id, manager_id);

  prev_opc2 = rr_read_csr(CSR_RROPC2);
  prev_opc3 = rr_read_csr(CSR_RROPC3);
  probe_log_step("saved-prev-opc", prev_opc2, prev_opc3);

  rr_set_opc(3, cfg_id);
  rr_set_opc(2, cfg_id);
  probe_log_step("after-bind-pair-opc", cfg_id, manager_id);

  issue_dma_copy();
  probe_log_step("after-dma-issue", (uint64_t)(uintptr_t)&probe_completion, REROCC_RR_PROBE_BYTES);

  switch ((probe_mode_t)REROCC_RR_PROBE_MODE) {
    case MODE_BLOCKING_RELEASE:
      probe_log_step("before-dma-wait", DMA_WAIT_SPINS, 0);
      if (!wait_dma_completion(DMA_WAIT_SPINS)) {
        printf("RRPROBE dma wait timeout mode=0\n");
        return false;
      }
      probe_log_step("after-dma-wait", 0, 0);
      probe_log_step("before-fence", cfg_id, 0);
      rr_fence(cfg_id);
      probe_log_step("after-fence", cfg_id, 0);
      rr_probe_tracerv_start_marker();
      probe_log_step("before-raw-release", cfg_id, 0);
      rr_release(cfg_id);
      probe_log_step("after-raw-release", cfg_id, 0);
      probe_log_step("before-readback", (uint64_t)(CSR_RRCFG0 + cfg_id), 0);
      cfg_readback = rr_read_csr(CSR_RRCFG0 + cfg_id);
      probe_log_step("after-readback", cfg_readback, 0);
      rr_probe_tracerv_end_marker();
      break;

    case MODE_RAW_RELEASE_READBACK:
      rr_probe_tracerv_start_marker();
      probe_log_step("before-raw-release", cfg_id, 0);
      rr_release(cfg_id);
      probe_log_step("after-raw-release", cfg_id, 0);
      probe_log_step("before-readback", (uint64_t)(CSR_RRCFG0 + cfg_id), 0);
      cfg_readback = rr_read_csr(CSR_RRCFG0 + cfg_id);
      probe_log_step("after-readback", cfg_readback, 0);
      rr_probe_tracerv_end_marker();
      break;

    case MODE_RAW_RELEASE_RESTORE_OPC3:
      rr_probe_tracerv_start_marker();
      probe_log_step("before-raw-release", cfg_id, 0);
      rr_release(cfg_id);
      probe_log_step("after-raw-release", cfg_id, 0);
      probe_log_step("before-restore-opc3", prev_opc3, 0);
      rr_write_csr(CSR_RROPC3, prev_opc3);
      probe_log_step("after-restore-opc3", prev_opc3, 0);
      rr_probe_tracerv_end_marker();
      break;

    case MODE_FENCE_RELEASE_RESTORE_OPC3:
      probe_log_step("before-dma-wait", DMA_WAIT_SPINS, 0);
      if (!wait_dma_completion(DMA_WAIT_SPINS)) {
        printf("RRPROBE dma wait timeout mode=3\n");
        return false;
      }
      probe_log_step("after-dma-wait", 0, 0);
      probe_log_step("before-fence", cfg_id, 0);
      rr_fence(cfg_id);
      probe_log_step("after-fence", cfg_id, 0);
      rr_probe_tracerv_start_marker();
      probe_log_step("before-raw-release", cfg_id, 0);
      rr_release(cfg_id);
      probe_log_step("after-raw-release", cfg_id, 0);
      probe_log_step("before-restore-opc3", prev_opc3, 0);
      rr_write_csr(CSR_RROPC3, prev_opc3);
      probe_log_step("after-restore-opc3", prev_opc3, 0);
      rr_probe_tracerv_end_marker();
      break;

    default:
      printf("RRPROBE unsupported mode=%u\n", (unsigned)REROCC_RR_PROBE_MODE);
      return false;
  }

  if (((probe_mode_t)REROCC_RR_PROBE_MODE == MODE_RAW_RELEASE_READBACK ||
       (probe_mode_t)REROCC_RR_PROBE_MODE == MODE_RAW_RELEASE_RESTORE_OPC3) &&
      !wait_dma_completion(DMA_WAIT_SPINS)) {
    printf("RRPROBE dma wait timeout post-critical-window mode=%u\n",
           (unsigned)REROCC_RR_PROBE_MODE);
    return false;
  }

  if (!buffers_equal(probe_src, probe_dst, REROCC_RR_PROBE_BYTES)) {
    printf("RRPROBE copy verification failed\n");
    return false;
  }

  probe_log_step("probe-pass", (uint64_t)(uintptr_t)probe_src, (uint64_t)(uintptr_t)probe_dst);
  return true;
}

void thread_entry(int cid, int nc) {
  bool ok = false;

  if (cid != PROBE_ACTIVE_CORE) {
    while (1) {
      asm volatile("wfi");
    }
  }

  if (!REROCC_PAIR_MANAGER_MODE) {
    printf("RRPROBE FAIL requires REROCC_PAIR_MANAGER_MODE=1\n");
    exit(1);
  }
  if (nc < 1) {
    printf("RRPROBE FAIL runtime cores=%d\n", nc);
    exit(1);
  }

  printf(
      "RRPROBE config cores=%d runtime_nc=%d gemmini=%d dma=%d gemmini_base=%d dma_base=%d pair_mode=%d mode=%u cfg=%u manager=%u bytes=%u wait_spins=%u tracerv_markers=%u\n",
      NUM_CORES,
      nc,
      REROCC_NUM_GEMMINI,
      REROCC_NUM_DMA,
      REROCC_GEMMINI_BASE_ID,
      REROCC_DMA_BASE_ID,
      REROCC_PAIR_MANAGER_MODE,
      (unsigned)REROCC_RR_PROBE_MODE,
      (unsigned)REROCC_RR_PROBE_CFG_ID,
      (unsigned)REROCC_RR_PROBE_MANAGER_ID,
      (unsigned)REROCC_RR_PROBE_BYTES,
      (unsigned)DMA_WAIT_SPINS,
      (unsigned)REROCC_RR_PROBE_TRACERV_MARKERS);

  rr_release_all(RR_MAX_CFGS);
  memset(probe_dst, 0, sizeof(probe_dst));
  probe_completion = 0U;
  fill_pattern(probe_src, sizeof(probe_src), 0x13572468u);

  ok = run_probe_sequence();
  rr_release_all(RR_MAX_CFGS);

  if (ok) {
    printf("RRPROBE result=PASS\n");
    exit(0);
  }

  printf("RRPROBE result=FAIL\n");
  exit(1);
}

int main(void) {
  return 1;
}
