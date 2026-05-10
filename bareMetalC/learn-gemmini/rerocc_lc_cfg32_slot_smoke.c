#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "encoding.h"
#include "util.h"
#include "rocc-software/src/xcustom.h"
#include "include/gemmini_testutils.h"
#include "include/rerocc_coupleddma.h"
#include "rerocc-linux-tests/rerocc_control.h"

#ifndef NUM_CORES
#define NUM_CORES 1
#endif

#ifndef REROCC_PAIR_MANAGER_MODE
#define REROCC_PAIR_MANAGER_MODE 1
#endif

#ifndef CFG32_SLOT_MANAGER_ID
#define CFG32_SLOT_MANAGER_ID 0U
#endif

#ifndef CFG32_SLOT_DMA_BYTES
#define CFG32_SLOT_DMA_BYTES 64U
#endif

#ifndef CFG32_SLOT_WAIT_SPINS
#define CFG32_SLOT_WAIT_SPINS 20000000UL
#endif

#ifndef CFG32_SLOT_BOUNDARY_SWEEP
#define CFG32_SLOT_BOUNDARY_SWEEP 0
#endif

#ifndef CFG32_SLOT_TRACERV_MARKERS
#define CFG32_SLOT_TRACERV_MARKERS 1
#endif

#ifndef CFG32_SLOT_ACQUIRE_MAX_RETRIES
#define CFG32_SLOT_ACQUIRE_MAX_RETRIES 1000000UL
#endif

#ifndef CFG32_SLOT_SKIP_GEMMINI_DATA_CHECK
#define CFG32_SLOT_SKIP_GEMMINI_DATA_CHECK 0
#endif

#define CFG32_SLOT_ACTIVE_CORE 0
#define CFG32_SLOT_TOTAL_CFGS 32U

#ifndef CFG32_SLOT_QUICK_CFG_ID
#define CFG32_SLOT_QUICK_CFG_ID 16U
#endif

#if !defined(RR_MAX_CFGS) || RR_MAX_CFGS < 32
#ifndef CSR_RRCFG16
#define CSR_RRCFG16 0x820
#define CSR_RRCFG17 0x821
#define CSR_RRCFG18 0x822
#define CSR_RRCFG19 0x823
#define CSR_RRCFG20 0x824
#define CSR_RRCFG21 0x825
#define CSR_RRCFG22 0x826
#define CSR_RRCFG23 0x827
#define CSR_RRCFG24 0x828
#define CSR_RRCFG25 0x829
#define CSR_RRCFG26 0x82a
#define CSR_RRCFG27 0x82b
#define CSR_RRCFG28 0x82c
#define CSR_RRCFG29 0x82d
#define CSR_RRCFG30 0x82e
#define CSR_RRCFG31 0x82f
#endif

#define RR32_EXTRA_CSR_LIST \
  F(CSR_RRCFG16) \
  F(CSR_RRCFG17) \
  F(CSR_RRCFG18) \
  F(CSR_RRCFG19) \
  F(CSR_RRCFG20) \
  F(CSR_RRCFG21) \
  F(CSR_RRCFG22) \
  F(CSR_RRCFG23) \
  F(CSR_RRCFG24) \
  F(CSR_RRCFG25) \
  F(CSR_RRCFG26) \
  F(CSR_RRCFG27) \
  F(CSR_RRCFG28) \
  F(CSR_RRCFG29) \
  F(CSR_RRCFG30) \
  F(CSR_RRCFG31)
#else
#define RR32_EXTRA_CSR_LIST
#endif

#define RR32_CSR_LIST \
  RR_CSR_LIST \
  RR32_EXTRA_CSR_LIST

static uint8_t dma_src[CFG32_SLOT_DMA_BYTES] __attribute__((aligned(64)));
static uint8_t dma_dst[CFG32_SLOT_DMA_BYTES] __attribute__((aligned(64)));
static volatile uint32_t dma_completion __attribute__((aligned(64)));
static elem_t gemmini_in[DIM][DIM] __attribute__((aligned(64)));
static elem_t gemmini_out[DIM][DIM] __attribute__((aligned(64)));

#if defined(__riscv) && CFG32_SLOT_TRACERV_MARKERS
static inline void cfg32_tracerv_start_marker(void) {
  asm volatile(".word 0x00008013" ::: "memory");
}

static inline void cfg32_tracerv_end_marker(void) {
  asm volatile(".word 0x00010013" ::: "memory");
}
#else
static inline void cfg32_tracerv_start_marker(void) {}
static inline void cfg32_tracerv_end_marker(void) {}
#endif

#if defined(__riscv)
static inline void cfg32_case_ok_marker(uint32_t cfg_id) {
  switch (cfg_id) {
    case 15U:
      asm volatile(".word 0x00f00013" ::: "memory");
      break;
    case 16U:
      asm volatile(".word 0x01000013" ::: "memory");
      break;
    case 31U:
      asm volatile(".word 0x01f00013" ::: "memory");
      break;
    default:
      break;
  }
}

static inline void cfg32_fail_marker(void) {
  asm volatile(".word 0x0ee00013" ::: "memory");
}
#else
static inline void cfg32_case_ok_marker(uint32_t cfg_id) {
  (void)cfg_id;
}

static inline void cfg32_fail_marker(void) {}
#endif

static inline uint64_t cfg32_swap_csr(uint64_t csr_id, uint64_t wdata) {
  uint64_t ret = 0;
  switch (csr_id) {
#define F(c) case c: { ret = swap_csr(c, wdata); break; }
    RR32_CSR_LIST
#undef F
    default:
      cfg32_fail_marker();
      abort();
  }
  return ret;
}

static inline uint64_t cfg32_read_csr(uint64_t csr_id) {
  uint64_t ret = 0;
  switch (csr_id) {
#define F(c) case c: { ret = read_csr(c); break; }
    RR32_CSR_LIST
#undef F
    default:
      cfg32_fail_marker();
      abort();
  }
  return ret;
}

static inline void cfg32_write_csr(uint64_t csr_id, uint64_t wdata) {
  (void)cfg32_swap_csr(csr_id, wdata);
}

static inline uint32_t cfg32_cfg_csr_id(uint32_t cfg_id) {
  return (uint32_t)(CSR_RRCFG0 + cfg_id);
}

static bool cfg32_acquire_cfg(uint32_t cfg_id, uint64_t manager_id) {
  const uint64_t wdata = RR_CFG_ACQ_MASK | (manager_id & RR_CFG_MGR_MASK);
  const uint32_t csr_id = cfg32_cfg_csr_id(cfg_id);
  cfg32_write_csr(csr_id, wdata);
  return (cfg32_read_csr(csr_id) & RR_CFG_ACQ_MASK) != 0U;
}

static bool cfg32_acquire_cfg_with_retry(uint32_t cfg_id, uint64_t manager_id) {
  unsigned long retries = 0;
  while (!cfg32_acquire_cfg(cfg_id, manager_id)) {
    retries += 1UL;
    if (CFG32_SLOT_ACQUIRE_MAX_RETRIES != 0UL &&
        retries >= CFG32_SLOT_ACQUIRE_MAX_RETRIES) {
      return false;
    }
    asm volatile("nop");
  }
  return true;
}

static inline void cfg32_release_cfg(uint32_t cfg_id) {
  cfg32_write_csr(cfg32_cfg_csr_id(cfg_id), 0U);
}

static void cfg32_release_all(void) {
  for (uint32_t cfg_id = 0; cfg_id < CFG32_SLOT_TOTAL_CFGS; ++cfg_id) {
    cfg32_release_cfg(cfg_id);
  }
}

static inline uint32_t lcg_next(uint32_t *state) {
  *state = (*state) * 1664525u + 1013904223u;
  return *state;
}

static void cfg32_fill_dma_pattern(uint8_t *buf, size_t n, uint32_t seed) {
  for (size_t idx = 0; idx < n; ++idx) {
    seed = lcg_next(&seed);
    buf[idx] = (uint8_t)seed;
  }
}

static bool cfg32_buffers_equal(const uint8_t *lhs, const uint8_t *rhs, size_t n) {
  for (size_t idx = 0; idx < n; ++idx) {
    if (lhs[idx] != rhs[idx]) {
      return false;
    }
  }
  return true;
}

static void cfg32_prepare_gemmini_inputs(void) {
  for (size_t row = 0; row < DIM; ++row) {
    for (size_t col = 0; col < DIM; ++col) {
      gemmini_in[row][col] = (elem_t)(((row * 13U) + (col * 7U) + 3U) % 97U);
      gemmini_out[row][col] = 0;
    }
  }
}

static void cfg32_issue_dma_copy(void) {
  dma_completion = 0U;
  asm volatile("fence rw, rw");
  rerocc_coupleddma_set_dst((uint64_t)(uintptr_t)dma_dst,
                            (uint64_t)(uintptr_t)&dma_completion);
  rerocc_coupleddma_set_src((uint64_t)(uintptr_t)dma_src,
                            (uint64_t)CFG32_SLOT_DMA_BYTES);
}

static bool cfg32_wait_dma_completion(void) {
  for (unsigned long spin = 0; spin < CFG32_SLOT_WAIT_SPINS; ++spin) {
    asm volatile("fence r, rw");
    if (dma_completion != 0U) {
      dma_completion = 0U;
      asm volatile("fence");
      return true;
    }
    asm volatile("nop");
  }
  return false;
}

static bool cfg32_run_dma_subtest(void) {
  memset(dma_dst, 0, sizeof(dma_dst));
  cfg32_fill_dma_pattern(dma_src, sizeof(dma_src), 0x13572468u);
  cfg32_issue_dma_copy();
  if (!cfg32_wait_dma_completion()) {
    return false;
  }
  if (!cfg32_buffers_equal(dma_src, dma_dst, sizeof(dma_src))) {
    return false;
  }
  return true;
}

static inline void cfg32_gemmini_wait_shared_mv_style(uint32_t cfg_id) {
  gemmini_fence();
  rr_fence(cfg_id);
}

static bool cfg32_run_gemmini_subtest(uint32_t cfg_id) {
  cfg32_prepare_gemmini_inputs();

  gemmini_flush(0);
  gemmini_config_ld(DIM * sizeof(elem_t));
  gemmini_config_st(DIM * sizeof(elem_t));
  gemmini_mvin(gemmini_in, 0);
  gemmini_mvout(gemmini_out, 0);
  cfg32_gemmini_wait_shared_mv_style(cfg_id);

#if CFG32_SLOT_SKIP_GEMMINI_DATA_CHECK
  return true;
#else
  if (!is_equal(gemmini_in, gemmini_out)) {
    return false;
  }
  return true;
#endif
}

static bool cfg32_run_one_case_dynamic(uint32_t cfg_id, uint64_t manager_id) {
  bool ok = false;
  bool acquired = false;
  uint64_t prev_opc2 = rr_read_csr(CSR_RROPC2);
  uint64_t prev_opc3 = rr_read_csr(CSR_RROPC3);

  cfg32_tracerv_start_marker();

  if (!cfg32_acquire_cfg_with_retry(cfg_id, manager_id)) {
    goto cleanup;
  }
  acquired = true;

  rr_set_opc(2, cfg_id);
  rr_set_opc(3, cfg_id);

  if (!cfg32_run_dma_subtest()) {
    goto cleanup;
  }

  if (!cfg32_run_gemmini_subtest(cfg_id)) {
    goto cleanup;
  }

  ok = true;

cleanup:
  if (acquired) {
    cfg32_release_cfg(cfg_id);
    (void)cfg32_read_csr(cfg32_cfg_csr_id(cfg_id));
  }
  rr_write_csr(CSR_RROPC2, prev_opc2);
  rr_write_csr(CSR_RROPC3, prev_opc3);
  if (ok) {
    cfg32_case_ok_marker(cfg_id);
  }
  cfg32_tracerv_end_marker();
  return ok;
}

#define CFG32_DEFINE_CONST_CASE_RUNNER(fn_name, cfg_literal)       \
static bool fn_name(uint64_t manager_id) {                         \
  const uint32_t cfg_id = (cfg_literal);                           \
  bool ok = false;                                                 \
  bool acquired = false;                                           \
  uint64_t prev_opc2 = rr_read_csr(CSR_RROPC2);                    \
  uint64_t prev_opc3 = rr_read_csr(CSR_RROPC3);                    \
                                                                    \
  cfg32_tracerv_start_marker();                                    \
                                                                    \
  if (!cfg32_acquire_cfg_with_retry(cfg_id, manager_id)) {         \
    goto cleanup;                                                  \
  }                                                                 \
  acquired = true;                                                 \
                                                                    \
  rr_set_opc(2, cfg_id);                                           \
  rr_set_opc(3, cfg_id);                                           \
                                                                    \
  if (!cfg32_run_dma_subtest()) {                                  \
    goto cleanup;                                                  \
  }                                                                 \
                                                                    \
  if (!cfg32_run_gemmini_subtest(cfg_id)) {                        \
    goto cleanup;                                                  \
  }                                                                 \
                                                                    \
  ok = true;                                                       \
                                                                    \
cleanup:                                                            \
  if (acquired) {                                                  \
    cfg32_release_cfg(cfg_id);                                     \
    (void)cfg32_read_csr(cfg32_cfg_csr_id(cfg_id));                \
  }                                                                 \
  rr_write_csr(CSR_RROPC2, prev_opc2);                             \
  rr_write_csr(CSR_RROPC3, prev_opc3);                             \
  if (ok) {                                                        \
    cfg32_case_ok_marker(cfg_id);                                  \
  }                                                                 \
  cfg32_tracerv_end_marker();                                      \
  return ok;                                                       \
}

CFG32_DEFINE_CONST_CASE_RUNNER(cfg32_run_case_15, 15U)
CFG32_DEFINE_CONST_CASE_RUNNER(cfg32_run_case_16, 16U)
CFG32_DEFINE_CONST_CASE_RUNNER(cfg32_run_case_31, 31U)

#undef CFG32_DEFINE_CONST_CASE_RUNNER

static bool cfg32_run_boundary_sweep(uint64_t manager_id) {
  if (!cfg32_run_case_15(manager_id)) {
    return false;
  }
  if (!cfg32_run_case_16(manager_id)) {
    return false;
  }
  if (!cfg32_run_case_31(manager_id)) {
    return false;
  }
  return true;
}

void thread_entry(int cid, int nc) {
  bool ok = true;

  if (cid != CFG32_SLOT_ACTIVE_CORE) {
    while (1) {
      asm volatile("wfi");
    }
  }

  if (!REROCC_PAIR_MANAGER_MODE) {
    cfg32_fail_marker();
    exit(1);
  }

  cfg32_release_all();

  if (CFG32_SLOT_BOUNDARY_SWEEP) {
    ok = cfg32_run_boundary_sweep((uint64_t)CFG32_SLOT_MANAGER_ID);
  } else {
    ok = cfg32_run_one_case_dynamic((uint64_t)CFG32_SLOT_QUICK_CFG_ID,
                                    (uint64_t)CFG32_SLOT_MANAGER_ID);
  }

  cfg32_release_all();

  if (ok) {
    exit(0);
  }

  cfg32_fail_marker();
  exit(1);
}

int main(void) {
  return 1;
}
