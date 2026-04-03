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

#ifndef REROCC_SEG3_LOG_LEVEL
#define REROCC_SEG3_LOG_LEVEL 2
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

#ifndef REROCC_STAGE0_SHARED_GID
#define REROCC_STAGE0_SHARED_GID (-1)
#endif

#define SEG3_LOG_FINAL(...) do { if (REROCC_SEG3_LOG_LEVEL >= 1) printf(__VA_ARGS__); } while (0)
#define SEG3_LOG_VERBOSE(...) do { if (REROCC_SEG3_LOG_LEVEL >= 2) printf(__VA_ARGS__); } while (0)

#define TEST_WORKER_CORES 2
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
#define STAGE0_LOCAL_ADDR 139264ULL
#define STAGE1_LOCAL_ADDR 590848ULL

static volatile int barrier_count = 0;
static volatile int barrier_epoch = 0;
static volatile int stage_status[TEST_WORKER_CORES];
static volatile uint32_t stage_progress[TEST_WORKER_CORES];

static uint8_t stage0_src[REROCC_EXPORT_STAGE0_BYTES] __attribute__((aligned(64)));
static uint8_t stage0_addr0[REROCC_EXPORT_STAGE0_BYTES] __attribute__((aligned(64)));
static uint8_t stage0_addr1[REROCC_EXPORT_STAGE0_BYTES] __attribute__((aligned(64)));
static uint8_t stage1_src[REROCC_EXPORT_STAGE1_BYTES] __attribute__((aligned(64)));
static uint8_t stage1_dst[REROCC_EXPORT_STAGE1_BYTES] __attribute__((aligned(64)));
static volatile int dma_completion_global[TEST_WORKER_CORES] __attribute__((aligned(64)));

static uint64_t rr_read_cfg_state(uint32_t cfg_id);

static uint32_t dma_cfg_id_for_stage(uint32_t stage_idx) {
  return ((stage_idx * 2U) + 0U) % RR_MAX_CFGS;
}

static void set_stage_progress(int cid, uint32_t progress) {
  if (cid < 0 || cid >= TEST_WORKER_CORES) return;
  stage_progress[cid] = progress;
  __sync_synchronize();
}

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

static bool rr_acquire_cfg_with_retry(uint32_t cfg_id, uint64_t manager_id,
                                      const char *tag, uint32_t stage_idx,
                                      bool emit_logs) {
  unsigned long retries = 0;
  if (emit_logs) {
    SEG3_LOG_VERBOSE("EXPORT_SEG3_ACQUIRE tag=%s stage=%u manager=%llu cfg=%u phase=begin\n",
                     tag ? tag : "unknown",
                     stage_idx,
                     (unsigned long long)manager_id,
                     cfg_id);
  }
  while (!rr_acquire_cfg(cfg_id, manager_id)) {
    retries += 1UL;
    if (emit_logs &&
        (retries == 1UL || retries == 1000UL || retries == 10000UL ||
         (retries % 100000UL) == 0UL)) {
      SEG3_LOG_VERBOSE("EXPORT_SEG3_ACQUIRE tag=%s stage=%u manager=%llu cfg=%u phase=wait retries=%lu cfg_state=0x%llx\n",
                       tag ? tag : "unknown",
                       stage_idx,
                       (unsigned long long)manager_id,
                       cfg_id,
                       retries,
                       (unsigned long long)rr_read_cfg_state(cfg_id));
    }
    if (REROCC_ACQUIRE_MAX_RETRIES != 0UL && retries >= REROCC_ACQUIRE_MAX_RETRIES) {
      if (emit_logs) {
        SEG3_LOG_VERBOSE("EXPORT_SEG3_ACQUIRE tag=%s stage=%u manager=%llu cfg=%u phase=timeout retries=%lu cfg_state=0x%llx\n",
                         tag ? tag : "unknown",
                         stage_idx,
                         (unsigned long long)manager_id,
                         cfg_id,
                         retries,
                         (unsigned long long)rr_read_cfg_state(cfg_id));
      }
      return false;
    }
    asm volatile("nop");
  }
  if (emit_logs) {
    SEG3_LOG_VERBOSE("EXPORT_SEG3_ACQUIRE tag=%s stage=%u manager=%llu cfg=%u phase=done retries=%lu cfg_state=0x%llx\n",
                     tag ? tag : "unknown",
                     stage_idx,
                     (unsigned long long)manager_id,
                     cfg_id,
                     retries,
                     (unsigned long long)rr_read_cfg_state(cfg_id));
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

static void report_stage_progress(const char *tag, int reporter_cid) {
  const uint32_t stage0_cfg = dma_cfg_id_for_stage(0U);
  const uint32_t stage1_cfg = dma_cfg_id_for_stage(1U);
  SEG3_LOG_VERBOSE("EXPORT_SEG3_STAGE_PROGRESS tag=%s reporter=%d stage0=%u stage1=%u status0=%d status1=%d barrier_count=%d barrier_epoch=%d cfg0=0x%llx cfg1=0x%llx\n",
                   tag ? tag : "progress",
                   reporter_cid,
                   (unsigned)stage_progress[0],
                   (unsigned)stage_progress[1],
                   stage_status[0],
                   stage_status[1],
                   barrier_count,
                   barrier_epoch,
                   (unsigned long long)rr_read_cfg_state(stage0_cfg),
                   (unsigned long long)rr_read_cfg_state(stage1_cfg));
}

static void log_export_rr_dma_snapshot(const char *tag, const char *phase,
                                       uint32_t manager_id, uint32_t cfg_id,
                                       uint32_t repeat_idx, uint32_t step_idx) {
  const uint64_t opc0 = rr_read_csr(CSR_RROPC0);
  const uint64_t opc1 = rr_read_csr(CSR_RROPC1);
  const uint64_t opc2 = rr_read_csr(CSR_RROPC2);
  const uint64_t opc3 = rr_read_csr(CSR_RROPC3);
  const uint64_t cfg_dma = rr_read_cfg_state(cfg_id);
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
  SEG3_LOG_VERBOSE("EXPORT_SEG3_SNAPSHOT tag=%s phase=%s manager=%u repeat=%u step=%u cfg=%u\n",
                   tag, phase ? phase : "snapshot", manager_id, repeat_idx, step_idx, cfg_id);
  SEG3_LOG_VERBOSE("EXPORT_SEG3_SNAPSHOT tag=%s phase=%s opc0=0x%llx opc1=0x%llx opc2=0x%llx opc3=0x%llx cfg_dma=0x%llx\n",
                   tag, phase ? phase : "snapshot",
                   (unsigned long long)opc0,
                   (unsigned long long)opc1,
                   (unsigned long long)opc2,
                   (unsigned long long)opc3,
                   (unsigned long long)cfg_dma);
  SEG3_LOG_VERBOSE("EXPORT_SEG3_SNAPSHOT tag=%s phase=%s mon_valid=%llu src_cmds=%llu dst_cmds=%llu req_bytes=%llu cycles=%llu effective_bytes=%llu eff_bw_x1000_bpc=%llu\n",
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
                                    size_t nbytes, const char *tag, const char *phase,
                                    uint32_t manager_id, uint32_t cfg_id,
                                    uint32_t repeat_idx,
                                    uint32_t step_idx) {
  uint64_t fence_status = 0;
  *flag = 0;
  asm volatile("fence rw, rw" ::: "memory");
  rerocc_coupleddma_set_dst(dst, (uint64_t)(uintptr_t)flag);
  rerocc_coupleddma_set_src(src, (uint64_t)nbytes);
  fence_status = rerocc_coupleddma_wait();
  asm volatile("fence rw, rw" ::: "memory");
  if (*flag != 0) {
    *flag = 0;
    asm volatile("fence" ::: "memory");
  }
  if (fence_status == 0ULL) {
    return true;
  }

  SEG3_LOG_VERBOSE("EXPORT_SEG3_TIMEOUT tag=%s phase=%s repeat=%u step=%u bytes=%llu src=0x%llx dst=0x%llx\n",
                   tag,
                   phase ? phase : "timeout",
                   repeat_idx,
                   step_idx,
                   (unsigned long long)nbytes,
                   (unsigned long long)src,
                   (unsigned long long)dst);
  log_export_rr_dma_snapshot(tag, phase, manager_id, cfg_id, repeat_idx, step_idx);
  return false;
}

static int dma_manager_to_shared_gid(int dma_manager_id) {
  return dma_manager_id - REROCC_DMA_BASE_ID + REROCC_GEMMINI_BASE_ID;
}

static bool resolve_stage0_shared_gid(uint32_t manager_id, int *gid_out) {
  int gid = REROCC_STAGE0_SHARED_GID;
  if (!gid_out) {
    return false;
  }
  if (gid < 0) {
    gid = dma_manager_to_shared_gid((int)manager_id);
  }
  if (gid < REROCC_GEMMINI_BASE_ID ||
      gid >= (REROCC_GEMMINI_BASE_ID + REROCC_NUM_GEMMINI)) {
    return false;
  }
  *gid_out = gid;
  return true;
}

static bool run_stage0_segment3_case(int cid, uint32_t manager_id) {
  volatile int *completion = &dma_completion_global[cid];
  const uint32_t cfg_id = dma_cfg_id_for_stage(0U);
  int gid = 0;
  uint64_t shared_base = 0ULL;
  const uint64_t addr0 = (uint64_t)(uintptr_t)stage0_addr0;
  const uint64_t addr1 = (uint64_t)(uintptr_t)stage0_addr1;

  if (!resolve_stage0_shared_gid(manager_id, &gid)) {
    set_stage_progress(cid, 0xE0U);
    return false;
  }
  shared_base = SHARED_SPAD_LOCAL_ADDR_BASE(gid) + STAGE0_LOCAL_ADDR;

  set_stage_progress(cid, 31U);
  set_stage_progress(cid, 32U);
  if (!rr_acquire_cfg_with_retry(cfg_id, manager_id, "stage0", 0U, false)) {
    set_stage_progress(cid, 0xE1U);
    return false;
  }

  set_stage_progress(cid, 33U);
  rr_set_opc(2, cfg_id);
  set_stage_progress(cid, 331U);
  set_stage_progress(cid, 332U);
  /* Stage1-post breadcrumbs are enough here; keep stage0 off the shared
   * baremetal printf path until we clear the seed-spm/export boundary. */
  set_stage_progress(cid, 34U);
  if (!dma_issue_copy_and_wait((uint64_t)(uintptr_t)stage0_src, shared_base, completion,
                               sizeof(stage0_src), "stage0", "seed-spm",
                               manager_id, cfg_id, 0U, 0U)) {
    set_stage_progress(cid, 0xE2U);
    rr_release(cfg_id);
    return false;
  }
  rr_fence(cfg_id);
  set_stage_progress(cid, 35U);

  for (uint32_t repeat_idx = 0; repeat_idx < REROCC_EXPORT_REPEAT_COUNT; ++repeat_idx) {
    memset(stage0_addr0, 0, sizeof(stage0_addr0));
    memset(stage0_addr1, 0, sizeof(stage0_addr1));

    set_stage_progress(cid, 40U + repeat_idx * 4U);
    if (!dma_issue_copy_and_wait(shared_base, addr0, completion, sizeof(stage0_src),
                                 "stage0", "export-sync-addr0",
                                 manager_id, cfg_id, repeat_idx, 0U)) {
      set_stage_progress(cid, 0xE3U);
      rr_release(cfg_id);
      return false;
    }
    rr_fence(cfg_id);

    set_stage_progress(cid, 41U + repeat_idx * 4U);
    if (!dma_issue_copy_and_wait(shared_base, addr1, completion, sizeof(stage0_src),
                                 "stage0", "export-sync-addr1",
                                 manager_id, cfg_id, repeat_idx, 1U)) {
      set_stage_progress(cid, 0xE4U);
      rr_release(cfg_id);
      return false;
    }
    rr_fence(cfg_id);

    set_stage_progress(cid, 42U + repeat_idx * 4U);
    if (!dma_issue_copy_and_wait(shared_base, addr0, completion, sizeof(stage0_src),
                                 "stage0", "c2-flush-slot0",
                                 manager_id, cfg_id, repeat_idx, 2U)) {
      set_stage_progress(cid, 0xE5U);
      rr_release(cfg_id);
      return false;
    }
    rr_fence(cfg_id);

    set_stage_progress(cid, 43U + repeat_idx * 4U);
    if (!buffers_equal(stage0_src, stage0_addr0, sizeof(stage0_src)) ||
        !buffers_equal(stage0_src, stage0_addr1, sizeof(stage0_src))) {
      set_stage_progress(cid, 0xE6U);
      rr_release(cfg_id);
      return false;
    }
  }

  set_stage_progress(cid, 80U);
  rr_release(cfg_id);
  return true;
}

static bool run_stage1_background_case(int cid, uint32_t manager_id) {
  volatile int *completion = &dma_completion_global[cid];
  const uint32_t cfg_id = dma_cfg_id_for_stage(1U);
  const int gid = dma_manager_to_shared_gid((int)manager_id);
  const uint64_t shared_base = SHARED_SPAD_LOCAL_ADDR_BASE(gid) + STAGE1_LOCAL_ADDR;
  const uint64_t dst = (uint64_t)(uintptr_t)stage1_dst;

  set_stage_progress(cid, 131U);
  if (!rr_acquire_cfg_with_retry(cfg_id, manager_id, "stage1", 1U, true)) {
    set_stage_progress(cid, 0xF1U);
    SEG3_LOG_VERBOSE("EXPORT_SEG3_FAIL tag=stage1 phase=acquire manager=%u cfg=%u\n",
                     manager_id, cfg_id);
    return false;
  }

  set_stage_progress(cid, 132U);
  rr_set_opc(2, cfg_id);
  log_export_rr_dma_snapshot("stage1", "begin", manager_id, cfg_id, 0U, 0U);
  SEG3_LOG_VERBOSE("EXPORT_SEG3_BEGIN tag=stage1 cid=%d manager=%u cfg=%u shared=0x%llx dst=0x%llx bytes=%llu repeats=%u\n",
                   cid,
                   manager_id,
                   cfg_id,
                   (unsigned long long)shared_base,
                   (unsigned long long)dst,
                   (unsigned long long)sizeof(stage1_src),
                   (unsigned)REROCC_EXPORT_REPEAT_COUNT);

  set_stage_progress(cid, 133U);
  if (!dma_issue_copy_and_wait((uint64_t)(uintptr_t)stage1_src, shared_base, completion,
                               sizeof(stage1_src), "stage1", "seed-spm",
                               manager_id, cfg_id, 0U, 0U)) {
    set_stage_progress(cid, 0xF2U);
    rr_release(cfg_id);
    SEG3_LOG_VERBOSE("EXPORT_SEG3_FAIL tag=stage1 phase=seed-spm manager=%u\n", manager_id);
    return false;
  }
  rr_fence(cfg_id);
  set_stage_progress(cid, 134U);

  for (uint32_t repeat_idx = 0; repeat_idx < REROCC_EXPORT_REPEAT_COUNT; ++repeat_idx) {
    memset(stage1_dst, 0, sizeof(stage1_dst));
    set_stage_progress(cid, 140U + repeat_idx);
    if (!dma_issue_copy_and_wait(shared_base, dst, completion, sizeof(stage1_src),
                                 "stage1", "background-export",
                                 manager_id, cfg_id, repeat_idx, 0U)) {
      set_stage_progress(cid, 0xF3U);
      rr_release(cfg_id);
      SEG3_LOG_VERBOSE("EXPORT_SEG3_FAIL tag=stage1 phase=background-export manager=%u repeat=%u\n",
                       manager_id, repeat_idx);
      return false;
    }
    rr_fence(cfg_id);
    if (!buffers_equal(stage1_src, stage1_dst, sizeof(stage1_src))) {
      set_stage_progress(cid, 0xF4U);
      rr_release(cfg_id);
      SEG3_LOG_VERBOSE("EXPORT_SEG3_FAIL tag=stage1 phase=verify manager=%u repeat=%u\n",
                       manager_id, repeat_idx);
      return false;
    }
    SEG3_LOG_VERBOSE("EXPORT_SEG3_PROGRESS tag=stage1 manager=%u repeat=%u steps=1 bytes=%llu\n",
                     manager_id,
                     repeat_idx,
                     (unsigned long long)sizeof(stage1_src));
  }

  set_stage_progress(cid, 180U);
  rr_release(cfg_id);
  SEG3_LOG_VERBOSE("EXPORT_SEG3_PASS tag=stage1 manager=%u cfg=%u bytes=%llu repeats=%u\n",
                   manager_id,
                   cfg_id,
                   (unsigned long long)sizeof(stage1_src),
                   (unsigned)REROCC_EXPORT_REPEAT_COUNT);
  return true;
}

void thread_entry(int cid, int nc) {
  const int logical_cores = TEST_WORKER_CORES;
  bool ok = true;

  if (cid < TEST_WORKER_CORES) {
    stage_status[cid] = -1;
    set_stage_progress(cid, 1U);
  }

  if (cid == 0) {
    SEG3_LOG_VERBOSE("[rerocc-export-seg3-repro] start runtime_nc=%d logical_cores=%d gemmini=%d dma=%d stage0_bytes=%u stage1_bytes=%u repeats=%u stage0_shared_gid=%d\n",
                     nc, logical_cores, REROCC_NUM_GEMMINI, REROCC_NUM_DMA,
                     (unsigned)REROCC_EXPORT_STAGE0_BYTES,
                     (unsigned)REROCC_EXPORT_STAGE1_BYTES,
                     (unsigned)REROCC_EXPORT_REPEAT_COUNT,
                     (int)REROCC_STAGE0_SHARED_GID);
  }

  if (nc < logical_cores || REROCC_NUM_DMA < 2 || REROCC_NUM_GEMMINI < 2) {
    if (cid == 0) {
      SEG3_LOG_VERBOSE("[rerocc-export-seg3-repro] FAIL requires >=2 cores, >=2 gemmini, >=2 dma\n");
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

  set_stage_progress(cid, 10U);
  barrier_wait(logical_cores);
  set_stage_progress(cid, 20U);

  if (cid == 0) {
    set_stage_progress(cid, 30U);
    ok = run_stage0_segment3_case(cid, (uint32_t)(REROCC_DMA_BASE_ID + 0));
    set_stage_progress(cid, ok ? 90U : 0xEEU);
  } else if (cid == 1) {
    set_stage_progress(cid, 130U);
    ok = run_stage1_background_case(cid, (uint32_t)(REROCC_DMA_BASE_ID + 1));
    set_stage_progress(cid, ok ? 190U : 0xFEU);
  }

  stage_status[cid] = ok ? 1 : 0;
  __sync_synchronize();
  if (cid == 1) {
    /* Stage1 is known-good; use it to report core0 progress without relying on core0 printf. */
    for (uint32_t sample = 0; sample < 6U && stage_status[0] < 0; ++sample) {
      report_stage_progress("stage1-post", cid);
      for (volatile unsigned long spin = 0; spin < 5000000UL; ++spin) {
        asm volatile("nop");
      }
    }
    report_stage_progress("pre-final-barrier", cid);
  }
  barrier_wait(logical_cores);

  if (cid != 0) {
    while (1) {
      asm volatile("wfi");
    }
  }

  rr_release_all(RR_MAX_CFGS);

  if (stage_status[0] && stage_status[1]) {
    SEG3_LOG_FINAL("EXPORT_SEG3_ALL_PASS\n");
    exit(0);
  }

  SEG3_LOG_FINAL("EXPORT_SEG3_ALL_FAIL stage0=%d stage1=%d\n",
                 stage_status[0], stage_status[1]);
  exit(1);
}

int main(void) {
  return 1;
}
