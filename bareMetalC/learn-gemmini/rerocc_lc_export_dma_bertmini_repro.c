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
#define REROCC_NUM_GEMMINI 2
#endif

#ifndef REROCC_NUM_DMA
#define REROCC_NUM_DMA 2
#endif

#ifndef REROCC_GEMMINI_BASE_ID
#define REROCC_GEMMINI_BASE_ID 0
#endif

#ifndef REROCC_DMA_BASE_ID
#define REROCC_DMA_BASE_ID REROCC_NUM_GEMMINI
#endif

#ifndef REROCC_ACQUIRE_MAX_RETRIES
#define REROCC_ACQUIRE_MAX_RETRIES 1000000UL
#endif

#ifndef DMA_WAIT_SPINS
#define DMA_WAIT_SPINS 20000000UL
#endif

#ifndef REROCC_EXPORT_REPEAT_COUNT
#define REROCC_EXPORT_REPEAT_COUNT 8U
#endif

#ifndef REROCC_EXPORT_STAGE0_BYTES
#define REROCC_EXPORT_STAGE0_BYTES (512U * 1024U)
#endif

#ifndef REROCC_EXPORT_STAGE1_BYTES
#define REROCC_EXPORT_STAGE1_BYTES (64U * 1024U)
#endif

#define TEST_WORKER_CORES 2
#define DMA_CFG_ID 1U
#define DMA_MON_VALID 0ULL
#define DMA_MON_SRC_CMDS 1ULL
#define DMA_MON_DST_CMDS 2ULL
#define DMA_MON_REQ_COPY_BYTES 3ULL
#define DMA_MON_CYCLES 4ULL
#define DMA_MON_EFFECTIVE_BYTES 5ULL
#define DMA_MON_EFF_BW_X1000_BPC 6ULL
#define SHARED_SPAD_GLOBAL_ADDR_BASE 0x40000000ULL
#define SHARED_SPAD_LOCAL_SIZE (1024ULL * 1024ULL)
#define SHARED_SPAD_LOCAL_ADDR_BASE(i) (SHARED_SPAD_GLOBAL_ADDR_BASE + SHARED_SPAD_LOCAL_SIZE * (uint64_t)(i))
#define STAGE0_LOCAL_ADDR 139264ULL
#define STAGE1_LOCAL_ADDR 590848ULL

static volatile int barrier_count = 0;
static volatile int barrier_epoch = 0;
static volatile int stage_status[TEST_WORKER_CORES];

static uint8_t stage0_src[REROCC_EXPORT_STAGE0_BYTES] __attribute__((aligned(64)));
static uint8_t stage0_alias_a[REROCC_EXPORT_STAGE0_BYTES] __attribute__((aligned(64)));
static uint8_t stage0_alias_b[REROCC_EXPORT_STAGE0_BYTES] __attribute__((aligned(64)));
static uint8_t stage1_src[REROCC_EXPORT_STAGE1_BYTES] __attribute__((aligned(64)));
static uint8_t stage1_alias[REROCC_EXPORT_STAGE1_BYTES] __attribute__((aligned(64)));
static volatile int dma_completion_global[TEST_WORKER_CORES] __attribute__((aligned(64)));

static void barrier_wait(int ncores) {
  int epoch = barrier_epoch;
  __sync_synchronize();
  if (__sync_add_and_fetch(&barrier_count, 1) == ncores) {
    barrier_count = 0;
    __sync_synchronize();
    barrier_epoch = epoch + 1;
  } else {
    while (barrier_epoch == epoch) {
      asm volatile("nop");
    }
  }
  __sync_synchronize();
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

static inline uint32_t lcg_next(uint32_t *state) {
  *state = (*state) * 1664525u + 1013904223u;
  return *state;
}

static void fill_pattern(uint8_t *buf, size_t n, uint32_t seed) {
  for (size_t i = 0; i < n; ++i) {
    seed = lcg_next(&seed);
    buf[i] = (uint8_t)seed;
  }
}

static bool buffers_equal(const uint8_t *lhs, const uint8_t *rhs, size_t n) {
  for (size_t i = 0; i < n; ++i) {
    if (lhs[i] != rhs[i]) return false;
  }
  return true;
}

static uint64_t rr_read_cfg_state(uint32_t cfg_id) {
  if (cfg_id >= RR_MAX_CFGS) return 0;
  return rr_read_csr(CSR_RRCFG0 + cfg_id);
}

static void log_export_rr_dma_snapshot(const char *tag, const char *phase,
                                       uint32_t manager_id, uint32_t repeat_idx,
                                       uint32_t alias_idx) {
  const uint64_t opc0 = rr_read_csr(CSR_RROPC0);
  const uint64_t opc1 = rr_read_csr(CSR_RROPC1);
  const uint64_t opc2 = rr_read_csr(CSR_RROPC2);
  const uint64_t opc3 = rr_read_csr(CSR_RROPC3);
  const uint64_t cfg_dma = rr_read_cfg_state(DMA_CFG_ID);
  const uint64_t valid = rerocc_coupleddma_read_monitor(DMA_MON_VALID);
  uint64_t src_cmds = 0;
  uint64_t dst_cmds = 0;
  uint64_t req_copy_bytes = 0;
  uint64_t cycles = 0;
  uint64_t effective_bytes = 0;
  uint64_t eff_bw_x1000_bpc = 0;
  if (valid != 0ULL) {
    src_cmds = rerocc_coupleddma_read_monitor(DMA_MON_SRC_CMDS);
    dst_cmds = rerocc_coupleddma_read_monitor(DMA_MON_DST_CMDS);
    req_copy_bytes = rerocc_coupleddma_read_monitor(DMA_MON_REQ_COPY_BYTES);
    cycles = rerocc_coupleddma_read_monitor(DMA_MON_CYCLES);
    effective_bytes = rerocc_coupleddma_read_monitor(DMA_MON_EFFECTIVE_BYTES);
    eff_bw_x1000_bpc = rerocc_coupleddma_read_monitor(DMA_MON_EFF_BW_X1000_BPC);
  }
  printf("EXPORT_BAREMETAL_SNAPSHOT tag=%s phase=%s manager=%u repeat=%u alias=%u cfg=%u\n",
         tag, phase ? phase : "snapshot", manager_id, repeat_idx, alias_idx, DMA_CFG_ID);
  printf("EXPORT_BAREMETAL_SNAPSHOT tag=%s phase=%s opc0=0x%llx opc1=0x%llx opc2=0x%llx opc3=0x%llx cfg_dma=0x%llx\n",
         tag, phase ? phase : "snapshot",
         (unsigned long long)opc0,
         (unsigned long long)opc1,
         (unsigned long long)opc2,
         (unsigned long long)opc3,
         (unsigned long long)cfg_dma);
  printf("EXPORT_BAREMETAL_SNAPSHOT tag=%s phase=%s mon_valid=%llu src_cmds=%llu dst_cmds=%llu req_bytes=%llu cycles=%llu effective_bytes=%llu eff_bw_x1000_bpc=%llu\n",
         tag, phase ? phase : "snapshot",
         (unsigned long long)valid,
         (unsigned long long)src_cmds,
         (unsigned long long)dst_cmds,
         (unsigned long long)req_copy_bytes,
         (unsigned long long)cycles,
         (unsigned long long)effective_bytes,
         (unsigned long long)eff_bw_x1000_bpc);
}

static bool dma_issue_copy_and_wait(uint64_t src, uint64_t dst, volatile int *flag,
                                    size_t nbytes, const char *tag,
                                    uint32_t manager_id, uint32_t repeat_idx,
                                    uint32_t alias_idx) {
  *flag = 0;
  asm volatile("fence rw, rw" ::: "memory");
  rerocc_coupleddma_set_dst(dst, (uint64_t)(uintptr_t)flag);
  rerocc_coupleddma_set_src(src, (uint64_t)nbytes);

  for (unsigned long spin = 0; spin < DMA_WAIT_SPINS; ++spin) {
    asm volatile("fence r, rw" ::: "memory");
    if (*flag != 0) {
      *flag = 0;
      asm volatile("fence" ::: "memory");
      return true;
    }
  }

  printf("EXPORT_BAREMETAL_TIMEOUT tag=%s repeat=%u alias=%u bytes=%llu src=0x%llx dst=0x%llx\n",
         tag,
         repeat_idx,
         alias_idx,
         (unsigned long long)nbytes,
         (unsigned long long)src,
         (unsigned long long)dst);
  log_export_rr_dma_snapshot(tag, "timeout", manager_id, repeat_idx, alias_idx);
  return false;
}

static int dma_manager_to_shared_gid(int dma_manager_id) {
  return dma_manager_id - REROCC_DMA_BASE_ID + REROCC_GEMMINI_BASE_ID;
}

static bool run_stage_export_case(int cid, uint32_t manager_id, const char *tag,
                                  uint64_t local_addr, uint8_t *src, size_t bytes,
                                  uint8_t *alias0, uint8_t *alias1, uint32_t alias_count) {
  volatile int *completion = &dma_completion_global[cid];
  const int gid = dma_manager_to_shared_gid((int)manager_id);
  const uint64_t shared_base = SHARED_SPAD_LOCAL_ADDR_BASE(gid) + local_addr;

  if (!rr_acquire_cfg_with_retry(DMA_CFG_ID, manager_id)) {
    printf("EXPORT_BAREMETAL_FAIL tag=%s phase=acquire manager=%u\n", tag, manager_id);
    return false;
  }
  rr_set_opc(2, DMA_CFG_ID);
  log_export_rr_dma_snapshot(tag, "begin", manager_id, 0U, 0U);

  printf("EXPORT_BAREMETAL_BEGIN tag=%s cid=%d manager=%u shared=0x%llx bytes=%llu repeats=%u alias_count=%u\n",
         tag,
         cid,
         manager_id,
         (unsigned long long)shared_base,
         (unsigned long long)bytes,
         (unsigned)REROCC_EXPORT_REPEAT_COUNT,
         alias_count);

  if (!dma_issue_copy_and_wait((uint64_t)(uintptr_t)src, shared_base, completion, bytes,
                               tag, manager_id, 0U, 0U)) {
    rr_release(DMA_CFG_ID);
    printf("EXPORT_BAREMETAL_FAIL tag=%s phase=seed manager=%u\n", tag, manager_id);
    return false;
  }
  rr_fence(DMA_CFG_ID);
  printf("EXPORT_BAREMETAL_SEED_OK tag=%s manager=%u bytes=%llu\n",
         tag, manager_id, (unsigned long long)bytes);

  for (uint32_t repeat_idx = 0; repeat_idx < REROCC_EXPORT_REPEAT_COUNT; ++repeat_idx) {
    for (uint32_t alias_idx = 0; alias_idx < alias_count; ++alias_idx) {
      uint8_t *dst = alias_idx == 0U ? alias0 : alias1;
      memset(dst, 0, bytes);
      if (!dma_issue_copy_and_wait(shared_base, (uint64_t)(uintptr_t)dst, completion, bytes,
                                   tag, manager_id, repeat_idx, alias_idx)) {
        rr_release(DMA_CFG_ID);
        printf("EXPORT_BAREMETAL_FAIL tag=%s phase=export manager=%u repeat=%u alias=%u\n",
               tag, manager_id, repeat_idx, alias_idx);
        return false;
      }
      rr_fence(DMA_CFG_ID);
      if (!buffers_equal(src, dst, bytes)) {
        rr_release(DMA_CFG_ID);
        printf("EXPORT_BAREMETAL_FAIL tag=%s phase=verify manager=%u repeat=%u alias=%u\n",
               tag, manager_id, repeat_idx, alias_idx);
        return false;
      }
      printf("EXPORT_BAREMETAL_PROGRESS tag=%s manager=%u repeat=%u alias=%u bytes=%llu\n",
             tag,
             manager_id,
             repeat_idx,
             alias_idx,
             (unsigned long long)bytes);
    }
  }

  rr_release(DMA_CFG_ID);
  printf("EXPORT_BAREMETAL_PASS tag=%s manager=%u bytes=%llu repeats=%u\n",
         tag, manager_id, (unsigned long long)bytes, (unsigned)REROCC_EXPORT_REPEAT_COUNT);
  return true;
}

void thread_entry(int cid, int nc) {
  const int logical_cores = TEST_WORKER_CORES;
  bool ok = true;

  if (cid == 0) {
    printf("[rerocc-export-repro] start runtime_nc=%d logical_cores=%d gemmini=%d dma=%d stage0_bytes=%u stage1_bytes=%u repeats=%u\n",
           nc, logical_cores, REROCC_NUM_GEMMINI, REROCC_NUM_DMA,
           (unsigned)REROCC_EXPORT_STAGE0_BYTES,
           (unsigned)REROCC_EXPORT_STAGE1_BYTES,
           (unsigned)REROCC_EXPORT_REPEAT_COUNT);
  }

  if (nc < logical_cores || REROCC_NUM_DMA < 2 || REROCC_NUM_GEMMINI < 2) {
    if (cid == 0) {
      printf("[rerocc-export-repro] FAIL requires >=2 cores, >=2 gemmini, >=2 dma\n");
      exit(1);
    }
    while (1) {
      asm volatile("wfi");
    }
  }

  if (cid >= logical_cores) {
    while (1) {
      asm volatile("wfi");
    }
  }

  if (cid == 0) {
    fill_pattern(stage0_src, sizeof(stage0_src), 0x13572468u);
    fill_pattern(stage1_src, sizeof(stage1_src), 0x24681357u);
  }

  barrier_wait(logical_cores);

  if (cid == 0) {
    ok = run_stage_export_case(cid,
                               (uint32_t)(REROCC_DMA_BASE_ID + 0),
                               "stage0_export512k",
                               STAGE0_LOCAL_ADDR,
                               stage0_src,
                               sizeof(stage0_src),
                               stage0_alias_a,
                               stage0_alias_b,
                               2U);
  } else if (cid == 1) {
    ok = run_stage_export_case(cid,
                               (uint32_t)(REROCC_DMA_BASE_ID + 1),
                               "stage1_export64k",
                               STAGE1_LOCAL_ADDR,
                               stage1_src,
                               sizeof(stage1_src),
                               stage1_alias,
                               stage1_alias,
                               1U);
  }

  stage_status[cid] = ok ? 1 : 0;
  __sync_synchronize();
  barrier_wait(logical_cores);

  if (cid != 0) {
    while (1) {
      asm volatile("wfi");
    }
  }

  rr_release_all(RR_MAX_CFGS);

  if (stage_status[0] && stage_status[1]) {
    printf("EXPORT_BAREMETAL_ALL_PASS\n");
    exit(0);
  }

  printf("EXPORT_BAREMETAL_ALL_FAIL stage0=%d stage1=%d\n",
         stage_status[0], stage_status[1]);
  exit(1);
}

int main(void) {
  return 1;
}
