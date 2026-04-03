#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define PRT_ENABLE_PROGRESS_RAW_LOG 1
#define PIPELINE_RUNTIME_GEMMINI_PHASE_LOG 1

#include "encoding.h"
#include "util.h"
#include "include/gemmini.h"
#include "include/gemmini_nn.h"
#include "include/rerocc_gemmini_spm_xlate.h"
#include "rerocc-linux-tests/rerocc_control.h"

#ifndef NUM_CORES
#define NUM_CORES 1
#endif

#ifndef REROCC_NUM_GEMMINI
#define REROCC_NUM_GEMMINI 2
#endif

#ifndef REROCC_GEMMINI_BASE_ID
#define REROCC_GEMMINI_BASE_ID 0
#endif

#ifndef REROCC_TEST_LOCAL_GEMMINI_ID
#define REROCC_TEST_LOCAL_GEMMINI_ID 0
#endif

#ifndef REROCC_STALL_DIAG_ONLY
#define REROCC_STALL_DIAG_ONLY 0
#endif

#ifndef REROCC_RUNTIME_STYLE_ONLY
#define REROCC_RUNTIME_STYLE_ONLY 0
#endif

#ifndef REROCC_FOCUSED_POINTWISE_INTERLEAVED
#define REROCC_FOCUSED_POINTWISE_INTERLEAVED 0
#endif

#ifndef REROCC_FOCUSED_BIAS_MVIN3_ALIAS
#define REROCC_FOCUSED_BIAS_MVIN3_ALIAS 0
#endif

#ifndef REROCC_FOCUSED_BIAS_MVIN3_ALIAS_VPAGE0
#define REROCC_FOCUSED_BIAS_MVIN3_ALIAS_VPAGE0 0
#endif

#ifndef REROCC_FOCUSED_POINTWISE_OS_INNER_ALIAS
#define REROCC_FOCUSED_POINTWISE_OS_INNER_ALIAS 0
#endif

#ifndef REROCC_FOCUSED_POINTWISE_EXPLICIT_TILES_ALIAS
#define REROCC_FOCUSED_POINTWISE_EXPLICIT_TILES_ALIAS 0
#endif

#ifndef REROCC_FOCUSED_POINTWISE_GROUPED_ALIAS
#define REROCC_FOCUSED_POINTWISE_GROUPED_ALIAS 0
#endif

#ifndef REROCC_ACQUIRE_MAX_RETRIES
#define REROCC_ACQUIRE_MAX_RETRIES 1000000UL
#endif

#ifndef REROCC_XLATE_PROGRAM_SUBPHASE_LOG
#define REROCC_XLATE_PROGRAM_SUBPHASE_LOG 0
#endif

#ifndef RR_MAX_CFGS
#define RR_MAX_CFGS 16U
#endif

#define GEMMINI_STAGE_ID 0U
#define GEMMINI_OPCODE_ID 3U

static inline uint32_t rr_cfg_id_for_stage_opcode(uint32_t stage_id, uint32_t opcode_id) {
  const uint32_t lane = opcode_id == 2U ? 0U : 1U;
  return ((stage_id * 2U) + lane) % RR_MAX_CFGS;
}

#define GEMMINI_CFG_ID rr_cfg_id_for_stage_opcode(GEMMINI_STAGE_ID, GEMMINI_OPCODE_ID)

#define SHARED_SPAD_GLOBAL_ADDR_BASE 0x40000000ULL
#define SHARED_SPAD_LOCAL_SIZE (1024 * 1024ULL)
#define SHARED_SPAD_LOCAL_ADDR_BASE(i) \
  (SHARED_SPAD_GLOBAL_ADDR_BASE + SHARED_SPAD_LOCAL_SIZE * (uint64_t)(i))

#define REROCC_SPM_PAGE_SHIFT 10U
#define REROCC_SPM_PAGE_BYTES (1ULL << REROCC_SPM_PAGE_SHIFT)
#ifndef REROCC_SHARED_SPAD_XLATE_RANGE_BASE
#define REROCC_SHARED_SPAD_XLATE_RANGE_BASE 0xC0000000ULL
#endif

#define SHARED_SPAD_XLATE_RANGE_BASE REROCC_SHARED_SPAD_XLATE_RANGE_BASE
#define SHARED_SPAD_XLATE_RANGE_SIZE \
  ((uint64_t)REROCC_NUM_GEMMINI * SHARED_SPAD_LOCAL_SIZE)
#define SHARED_SPAD_XLATE_PTE_CAP \
  ((REROCC_NUM_GEMMINI * (SHARED_SPAD_LOCAL_SIZE / REROCC_SPM_PAGE_BYTES)) + 128U)

#define RESADD_I 24
#define RESADD_J 64
#define RESADD_STRIDE RESADD_J
#define RESADD_ELEM_COUNT ((size_t)RESADD_I * (size_t)RESADD_STRIDE)
#define RESADD_BYTES (RESADD_ELEM_COUNT * sizeof(elem_t))
#ifndef ALIAS_REGION_MAX_PAGES
#define ALIAS_REGION_MAX_PAGES 128U
#endif
#define RESADD_VADDR_PAGE_OFFSET 768ULL
#define CLEAN_ACC_ROW_OFFSET 256U

#define CONTIG_A_VPAGE 0x010U
#define CONTIG_B_VPAGE 0x018U
#define CONTIG_C_VPAGE 0x020U
#define INTERLEAVED_A_VPAGE 0x030U
#define INTERLEAVED_B_VPAGE 0x038U
#define INTERLEAVED_C_VPAGE 0x040U

#define CONTIG_A_LOCAL_PAGE 64U
#define CONTIG_B_LOCAL_PAGE 68U
#define CONTIG_C_LOCAL_PAGE 72U
#define INTERLEAVED_A_LOCAL_PAGE 96U
#define INTERLEAVED_B_LOCAL_PAGE 104U
#define INTERLEAVED_C_LOCAL_PAGE 112U

#ifndef PW_I
#define PW_I 256
#endif
#ifndef PW_J
#define PW_J 64
#endif
#ifndef PW_K
#define PW_K 256
#endif
#ifndef PW_CHUNK_TILE_J
#define PW_CHUNK_TILE_J 128
#endif
#ifndef PW_CHUNK_OC
#define PW_CHUNK_OC 64
#endif
#ifndef PW_FOCUS_OC_BEG
#define PW_FOCUS_OC_BEG 0
#endif
#ifndef PW_FOCUS_OC_TILE
#define PW_FOCUS_OC_TILE PW_CHUNK_OC
#endif
#ifndef PW_A_STRIDE
#define PW_A_STRIDE 256
#endif
#ifndef PW_B_STRIDE
#define PW_B_STRIDE 256
#endif
#ifndef PW_C_STRIDE
#define PW_C_STRIDE 256
#endif
#ifndef PW_DIAG_I
#define PW_DIAG_I 8
#endif
#ifndef PW_DIAG_J
#define PW_DIAG_J PW_CHUNK_TILE_J
#endif
#ifndef PW_DIAG_K
#define PW_DIAG_K 8
#endif
#ifndef PW_LINUX_TILE_I
#define PW_LINUX_TILE_I 32
#endif
#ifndef PW_LINUX_TILE_J
#define PW_LINUX_TILE_J 8
#endif
#ifndef PW_LINUX_TILE_K
#define PW_LINUX_TILE_K 32
#endif
#ifndef PW_GROUP_COUNT
#define PW_GROUP_COUNT 8U
#endif
#define PW_LINUX_TILE_BLOCKS 2U
#define PW_LINUX_TILE_COLS (PW_LINUX_TILE_BLOCKS * DIM)
#define PW_LINUX_TILE_ROWS DIM
#define PW_LINUX_TILE_D_SP_ADDR (1U << (ADDR_LEN - 1))
#define PW_OS_SAFE_TILE_I 16U
#define PW_OS_SAFE_TILE_J MAX_BLOCK_LEN
#define PW_OS_SAFE_TILE_K (PW_K / DIM)
#define PW_GROUP_TOTAL_K ((size_t)PW_GROUP_COUNT * (size_t)PW_K)
#define PW_GROUP_TOTAL_OC ((size_t)PW_GROUP_COUNT * (size_t)PW_J)
#define PW_ELEM_COUNT_A ((size_t)PW_I * (size_t)PW_A_STRIDE)
#define PW_ELEM_COUNT_B ((size_t)PW_K * (size_t)PW_B_STRIDE)
#define PW_ELEM_COUNT_C ((size_t)PW_I * (size_t)PW_C_STRIDE)
#define PW_BYTES_A (PW_ELEM_COUNT_A * sizeof(elem_t))
#define PW_BYTES_B (PW_ELEM_COUNT_B * sizeof(elem_t))
#define PW_BYTES_C (PW_ELEM_COUNT_C * sizeof(elem_t))
#define PW_BYTES_BIAS ((size_t)PW_J * sizeof(acc_t))
#define PW_GROUP_BYTES_B (PW_GROUP_TOTAL_K * (size_t)PW_B_STRIDE * sizeof(elem_t))
#define PW_GROUP_BYTES_BIAS (PW_GROUP_TOTAL_OC * sizeof(acc_t))
#define PW_CHUNK_BYTES_BIAS ((size_t)PW_CHUNK_TILE_J * sizeof(acc_t))
#define PW_DIAG_BYTES_A ((size_t)PW_DIAG_I * (size_t)PW_A_STRIDE * sizeof(elem_t))
#define PW_DIAG_BYTES_B ((size_t)PW_DIAG_K * (size_t)PW_B_STRIDE * sizeof(elem_t))
#define PW_DIAG_BYTES_C ((size_t)PW_DIAG_I * (size_t)PW_C_STRIDE * sizeof(elem_t))
#define PW_DIAG_BYTES_BIAS ((size_t)PW_DIAG_J * sizeof(acc_t))
#define PW_DIAG_ELEM_COUNT_C ((size_t)PW_DIAG_I * (size_t)PW_C_STRIDE)
#ifndef PW_VADDR_PAGE_OFFSET
#define PW_VADDR_PAGE_OFFSET 768ULL
#endif

#ifndef PW_A_VPAGE
#define PW_A_VPAGE 0x080U
#endif
#ifndef PW_B_VPAGE
#define PW_B_VPAGE 0x0d0U
#endif
#ifndef PW_BIAS_VPAGE
#define PW_BIAS_VPAGE 0x120U
#endif
#ifndef PW_C_VPAGE
#define PW_C_VPAGE 0x130U
#endif
#ifndef PW_CHUNK_BIAS_VPAGE
#define PW_CHUNK_BIAS_VPAGE 0x171U
#endif
#ifndef PW_CHUNK_BIAS_CONTIG_VPAGE
#define PW_CHUNK_BIAS_CONTIG_VPAGE 0x174U
#endif

#ifndef PW_CONTIG_A_LOCAL_PAGE
#define PW_CONTIG_A_LOCAL_PAGE 160U
#endif
#ifndef PW_CONTIG_B_LOCAL_PAGE
#define PW_CONTIG_B_LOCAL_PAGE 240U
#endif
#ifndef PW_CONTIG_BIAS_LOCAL_PAGE
#define PW_CONTIG_BIAS_LOCAL_PAGE 320U
#endif
#ifndef PW_CONTIG_C_LOCAL_PAGE
#define PW_CONTIG_C_LOCAL_PAGE 336U
#endif
#ifndef PW_CHUNK_CONTIG_BIAS_LOCAL_PAGE
#define PW_CHUNK_CONTIG_BIAS_LOCAL_PAGE 416U
#endif

#ifndef PW_INTERLEAVED_A_LOCAL_PAGE
#define PW_INTERLEAVED_A_LOCAL_PAGE 480U
#endif
#ifndef PW_INTERLEAVED_B_LOCAL_PAGE
#define PW_INTERLEAVED_B_LOCAL_PAGE 560U
#endif
#ifndef PW_INTERLEAVED_BIAS_LOCAL_PAGE
#define PW_INTERLEAVED_BIAS_LOCAL_PAGE 640U
#endif
#ifndef PW_INTERLEAVED_C_LOCAL_PAGE
#define PW_INTERLEAVED_C_LOCAL_PAGE 656U
#endif
#ifndef PW_CHUNK_INTERLEAVED_BIAS_LOCAL_PAGE
#define PW_CHUNK_INTERLEAVED_BIAS_LOCAL_PAGE 704U
#endif

#ifndef PW_FOCUSED_STAGE0_A_LOCAL_PAGE
#define PW_FOCUSED_STAGE0_A_LOCAL_PAGE PW_INTERLEAVED_A_LOCAL_PAGE
#endif
#ifndef PW_FOCUSED_STAGE0_B_LOCAL_PAGE
#define PW_FOCUSED_STAGE0_B_LOCAL_PAGE PW_INTERLEAVED_B_LOCAL_PAGE
#endif
#ifndef PW_FOCUSED_STAGE0_BIAS_LOCAL_PAGE
#define PW_FOCUSED_STAGE0_BIAS_LOCAL_PAGE PW_CHUNK_INTERLEAVED_BIAS_LOCAL_PAGE
#endif
#ifndef PW_FOCUSED_STAGE0_C_LOCAL_PAGE
#define PW_FOCUSED_STAGE0_C_LOCAL_PAGE PW_INTERLEAVED_C_LOCAL_PAGE
#endif

#ifndef PW_FOCUSED_STAGE0_A_SLOT_OFFSET
#define PW_FOCUSED_STAGE0_A_SLOT_OFFSET 0U
#endif
#ifndef PW_FOCUSED_STAGE0_B_SLOT_OFFSET
#define PW_FOCUSED_STAGE0_B_SLOT_OFFSET 1U
#endif
#ifndef PW_FOCUSED_STAGE0_BIAS_SLOT_OFFSET
#define PW_FOCUSED_STAGE0_BIAS_SLOT_OFFSET 4U
#endif
#ifndef PW_FOCUSED_STAGE0_C_SLOT_OFFSET
#define PW_FOCUSED_STAGE0_C_SLOT_OFFSET 3U
#endif

#ifndef REROCC_FOCUSED_POINTWISE_CONTIGUOUS_LAYOUT
#define REROCC_FOCUSED_POINTWISE_CONTIGUOUS_LAYOUT 0
#endif

#ifndef PW_FOCUSED_STAGE0_A_LOCAL_TILE
#define PW_FOCUSED_STAGE0_A_LOCAL_TILE REROCC_TEST_LOCAL_GEMMINI_ID
#endif
#ifndef PW_FOCUSED_STAGE0_B_LOCAL_TILE
#define PW_FOCUSED_STAGE0_B_LOCAL_TILE REROCC_TEST_LOCAL_GEMMINI_ID
#endif
#ifndef PW_FOCUSED_STAGE0_BIAS_LOCAL_TILE
#define PW_FOCUSED_STAGE0_BIAS_LOCAL_TILE REROCC_TEST_LOCAL_GEMMINI_ID
#endif
#ifndef PW_FOCUSED_STAGE0_C_LOCAL_TILE
#define PW_FOCUSED_STAGE0_C_LOCAL_TILE REROCC_TEST_LOCAL_GEMMINI_ID
#endif

#ifndef REROCC_FOCUSED_POINTWISE_TARGET
#define REROCC_FOCUSED_POINTWISE_TARGET "linux-seg0-stage0-pointwise"
#endif
#ifndef REROCC_FOCUSED_POINTWISE_EXPLICIT_TARGET
#define REROCC_FOCUSED_POINTWISE_EXPLICIT_TARGET "linux-seg0-stage0-pointwise-explicit-tiles"
#endif

#define PW_DIAG_A_VPAGE 0x180U
#define PW_DIAG_B_VPAGE 0x188U
#define PW_DIAG_BIAS_VPAGE 0x190U
#define PW_DIAG_C_VPAGE 0x198U

#define PW_DIAG_CONTIG_A_LOCAL_PAGE 800U
#define PW_DIAG_CONTIG_B_LOCAL_PAGE 804U
#define PW_DIAG_CONTIG_BIAS_LOCAL_PAGE 808U
#define PW_DIAG_CONTIG_C_LOCAL_PAGE 812U

#define PW_DIAG_INTERLEAVED_A_LOCAL_PAGE 840U
#define PW_DIAG_INTERLEAVED_B_LOCAL_PAGE 844U
#define PW_DIAG_INTERLEAVED_BIAS_LOCAL_PAGE 848U
#define PW_DIAG_INTERLEAVED_C_LOCAL_PAGE 852U

typedef struct {
  uint64_t vaddr;
  uint64_t page_paddrs[ALIAS_REGION_MAX_PAGES];
  size_t page_count;
  size_t bytes;
} spm_alias_region_t;

typedef enum {
  RESADD_MODE_EXPLICIT_NO_FENCE = 0,
  RESADD_MODE_EXPLICIT_SERIALIZED = 1,
  RESADD_MODE_STANDARD_WS = 2,
} resadd_mode_t;

static elem_t resadd_a_dram[RESADD_I][RESADD_STRIDE] __attribute__((aligned(64)));
static elem_t resadd_b_dram[RESADD_I][RESADD_STRIDE] __attribute__((aligned(64)));
static elem_t resadd_gold_dram[RESADD_I][RESADD_STRIDE] __attribute__((aligned(64)));
static elem_t resadd_out_shadow_dram[RESADD_I][RESADD_STRIDE] __attribute__((aligned(64)));
static elem_t pw_a_dram[PW_I][PW_A_STRIDE] __attribute__((aligned(64)));
static elem_t pw_b_dram[PW_K][PW_B_STRIDE] __attribute__((aligned(64)));
static elem_t pw_group_b_dram[PW_GROUP_COUNT * PW_K][PW_B_STRIDE] __attribute__((aligned(64)));
static acc_t pw_bias_dram[PW_J] __attribute__((aligned(64)));
static acc_t pw_chunk_bias_dram[PW_CHUNK_TILE_J] __attribute__((aligned(64)));
static acc_t pw_group_bias_dram[PW_GROUP_COUNT * PW_J] __attribute__((aligned(64)));
static elem_t pw_init_c_dram[PW_I][PW_C_STRIDE] __attribute__((aligned(64)));
static elem_t pw_gold_dram[PW_I][PW_C_STRIDE] __attribute__((aligned(64)));
static elem_t pw_j128_gold_dram[PW_I][PW_C_STRIDE] __attribute__((aligned(64)));
static elem_t pw_chunk_gold_dram[PW_I][PW_C_STRIDE] __attribute__((aligned(64)));
static elem_t pw_group_gold_dram[PW_I][PW_C_STRIDE] __attribute__((aligned(64)));
static elem_t pw_diag_a_dram[PW_DIAG_I][PW_A_STRIDE] __attribute__((aligned(64)));
static elem_t pw_diag_b_dram[PW_DIAG_K][PW_B_STRIDE] __attribute__((aligned(64)));
static acc_t pw_diag_bias_dram[PW_DIAG_J] __attribute__((aligned(64)));
static elem_t pw_diag_init_c_dram[PW_DIAG_I][PW_C_STRIDE] __attribute__((aligned(64)));
static elem_t pw_diag_gold_dram[PW_DIAG_I][PW_C_STRIDE] __attribute__((aligned(64)));
static elem_t pw_out_shadow_dram[PW_I][PW_C_STRIDE] __attribute__((aligned(64)));
static uint64_t spm_xlate_pte[SHARED_SPAD_XLATE_PTE_CAP] __attribute__((aligned(64)));

static bool rr_acquire_cfg_with_retry(uint32_t cfg_id, uint64_t manager_id) {
  unsigned long retries = 0;
  while (!rr_acquire_cfg(cfg_id, manager_id)) {
    retries++;
    if (REROCC_ACQUIRE_MAX_RETRIES != 0 && retries >= REROCC_ACQUIRE_MAX_RETRIES) {
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

static inline unsigned long init_cycle(void) {
  return (unsigned long)read_csr(mcycle);
}

static inline size_t init_progress_step(size_t total, size_t buckets) {
  if (total == 0 || buckets == 0) {
    return 1;
  }
  size_t step = total / buckets;
  return step == 0 ? 1 : step;
}

static inline bool init_should_log_progress(size_t idx, size_t total, size_t step) {
  return idx == 0 || idx + 1 == total || ((idx + 1) % step) == 0;
}

static void init_phase_log(const char *phase, uint32_t seed) {
  printf("INIT_PHASE phase=%s cycle=%lu seed=0x%x\n",
         phase, init_cycle(), seed);
}

static void case_phase_log(const char *name, const char *phase) {
  printf("CASE_PROGRESS %s phase=%s cycle=%lu\n", name, phase, init_cycle());
}

static inline const char *focused_layout_name(void) {
#if defined(REROCC_FOCUSED_POINTWISE_CONTIGUOUS_LAYOUT) && REROCC_FOCUSED_POINTWISE_CONTIGUOUS_LAYOUT
  return "contiguous";
#else
  return "interleaved";
#endif
}

static inline bool stall_diag_only(void) {
  return REROCC_STALL_DIAG_ONLY != 0;
}

static inline bool runtime_style_only(void) {
  return REROCC_RUNTIME_STYLE_ONLY != 0;
}

static inline bool compare_disabled(void) {
  return stall_diag_only() || runtime_style_only();
}

static bool elem_buffer_matches(const elem_t *expected, const elem_t *actual, size_t n,
                                const char *case_name);

static bool finish_elem_case_result(const char *name,
                                    const elem_t *expected,
                                    const elem_t *actual,
                                    size_t n) {
  if (compare_disabled()) {
    printf("CASE_RESULT %s PASS_STALL_DIAG\n", name);
    return true;
  }

  const bool ok = elem_buffer_matches(expected, actual, n, name);
  printf("CASE_RESULT %s %s\n", name, ok ? "PASS" : "FAIL");
  return ok;
}

static void init_random_elem(elem_t *buf, size_t n, uint32_t *state) {
  const unsigned long start_cycle = init_cycle();
  printf("INIT_FN_START fn=init_random_elem buf=0x%lx n=%lu seed_in=0x%x cycle=%lu\n",
         (unsigned long)buf, (unsigned long)n, *state, start_cycle);
  for (size_t i = 0; i < n; i++) {
    buf[i] = (elem_t)((int32_t)(lcg_next(state) % 31u) - 15);
  }
  const unsigned long end_cycle = init_cycle();
  printf("INIT_FN_END fn=init_random_elem buf=0x%lx n=%lu seed_out=0x%x first=%d last=%d cycle=%lu delta=%lu\n",
         (unsigned long)buf, (unsigned long)n, *state,
         n == 0 ? 0 : (int)buf[0], n == 0 ? 0 : (int)buf[n - 1],
         end_cycle, end_cycle - start_cycle);
}

static void init_random_acc(acc_t *buf, size_t n, uint32_t *state) {
  const unsigned long start_cycle = init_cycle();
  printf("INIT_FN_START fn=init_random_acc buf=0x%lx n=%lu seed_in=0x%x cycle=%lu\n",
         (unsigned long)buf, (unsigned long)n, *state, start_cycle);
  for (size_t i = 0; i < n; i++) {
    buf[i] = (acc_t)((int32_t)(lcg_next(state) % 61u) - 30);
  }
  const unsigned long end_cycle = init_cycle();
  printf("INIT_FN_END fn=init_random_acc buf=0x%lx n=%lu seed_out=0x%x first=%ld last=%ld cycle=%lu delta=%lu\n",
         (unsigned long)buf, (unsigned long)n, *state,
         n == 0 ? 0L : (long)buf[0], n == 0 ? 0L : (long)buf[n - 1],
         end_cycle, end_cycle - start_cycle);
}

static void resadd_reference(const elem_t *a, const elem_t *b, elem_t *out, size_t n) {
  const unsigned long start_cycle = init_cycle();
  printf("INIT_FN_START fn=resadd_reference a=0x%lx b=0x%lx out=0x%lx n=%lu cycle=%lu\n",
         (unsigned long)a, (unsigned long)b, (unsigned long)out, (unsigned long)n,
         start_cycle);
  for (size_t i = 0; i < n; i++) {
    acc_t sum = (acc_t)a[i] + (acc_t)b[i];
    if (sum > elem_t_max) {
      sum = elem_t_max;
    } else if (sum < elem_t_min) {
      sum = elem_t_min;
    }
    out[i] = (elem_t)sum;
  }
  const unsigned long end_cycle = init_cycle();
  printf("INIT_FN_END fn=resadd_reference out=0x%lx n=%lu first=%d last=%d cycle=%lu delta=%lu\n",
         (unsigned long)out, (unsigned long)n,
         n == 0 ? 0 : (int)out[0], n == 0 ? 0 : (int)out[n - 1],
         end_cycle, end_cycle - start_cycle);
}

static void shared_byte_copy(volatile uint8_t *dst, const uint8_t *src, size_t n) {
  for (size_t i = 0; i < n; i++) {
    dst[i] = src[i];
  }
}

static void shared_byte_read(const volatile uint8_t *src, uint8_t *dst, size_t n) {
  for (size_t i = 0; i < n; i++) {
    dst[i] = src[i];
  }
}

static void shared_byte_zero(volatile uint8_t *dst, size_t n) {
  for (size_t i = 0; i < n; i++) {
    dst[i] = 0;
  }
}

static inline uint64_t shared_spad_vaddr(uint64_t paddr) {
  return SHARED_SPAD_XLATE_RANGE_BASE + (paddr - SHARED_SPAD_GLOBAL_ADDR_BASE);
}

static void spm_xlate_table_clear(void) {
  memset(spm_xlate_pte, 0, sizeof(spm_xlate_pte));
}

static void spm_xlate_map_page(uint64_t vaddr, uint64_t paddr) {
  const uint64_t vpage = (vaddr - SHARED_SPAD_XLATE_RANGE_BASE) >> REROCC_SPM_PAGE_SHIFT;
  if (vpage < (uint64_t)SHARED_SPAD_XLATE_PTE_CAP) {
    spm_xlate_pte[vpage] = ((paddr >> REROCC_SPM_PAGE_SHIFT) << 1) | 1ULL;
  }
}

static inline void spm_xlate_publish_table(void) {
  asm volatile("fence rw, rw" ::: "memory");
}

static inline void spm_xlate_program(bool enable) {
  if (enable) {
    if (REROCC_XLATE_PROGRAM_SUBPHASE_LOG) {
      printf("XLATE_TRACE phase=publish_begin enable=%u cycle=%lu\n",
             enable ? 1U : 0U, init_cycle());
    }
    spm_xlate_publish_table();
    if (REROCC_XLATE_PROGRAM_SUBPHASE_LOG) {
      printf("XLATE_TRACE phase=publish_end enable=%u cycle=%lu\n",
             enable ? 1U : 0U, init_cycle());
    }
  }
  if (REROCC_XLATE_PROGRAM_SUBPHASE_LOG) {
    printf("XLATE_TRACE phase=cfg_begin enable=%u cycle=%lu\n",
           enable ? 1U : 0U, init_cycle());
  }
  rerocc_gemmini_spm_xlate_cfg((uint64_t)(uintptr_t)spm_xlate_pte,
                               (uint32_t)SHARED_SPAD_XLATE_PTE_CAP,
                               REROCC_SPM_PAGE_SHIFT,
                               enable ? 1U : 0U);
  if (REROCC_XLATE_PROGRAM_SUBPHASE_LOG) {
    printf("XLATE_TRACE phase=cfg_end enable=%u cycle=%lu\n",
           enable ? 1U : 0U, init_cycle());
  }
  if (REROCC_XLATE_PROGRAM_SUBPHASE_LOG) {
    printf("XLATE_TRACE phase=range_begin enable=%u cycle=%lu\n",
           enable ? 1U : 0U, init_cycle());
  }
  rerocc_gemmini_spm_xlate_range(enable ? SHARED_SPAD_XLATE_RANGE_BASE : 0ULL,
                                 enable ? SHARED_SPAD_XLATE_RANGE_SIZE : 0ULL);
  if (REROCC_XLATE_PROGRAM_SUBPHASE_LOG) {
    printf("XLATE_TRACE phase=range_end enable=%u cycle=%lu\n",
           enable ? 1U : 0U, init_cycle());
  }
  if (REROCC_XLATE_PROGRAM_SUBPHASE_LOG) {
    printf("XLATE_TRACE phase=flush_begin enable=%u cycle=%lu\n",
           enable ? 1U : 0U, init_cycle());
  }
  rerocc_gemmini_spm_xlate_flush();
  if (REROCC_XLATE_PROGRAM_SUBPHASE_LOG) {
    printf("XLATE_TRACE phase=flush_end enable=%u cycle=%lu\n",
           enable ? 1U : 0U, init_cycle());
  }
}

static inline void spm_xlate_reset(void) {
  rerocc_gemmini_spm_xlate_cfg(0ULL, 0U, REROCC_SPM_PAGE_SHIFT, 0U);
  rerocc_gemmini_spm_xlate_range(0ULL, 0ULL);
  rerocc_gemmini_spm_xlate_flush();
}

static inline void gemmini_wait_managed(uint32_t cfg_id) {
  rr_fence(cfg_id);
  gemmini_flush(0);
  rr_fence(cfg_id);
}

static inline void gemmini_wait_managed_runtime_style(uint32_t cfg_id) {
  rr_fence(cfg_id);
  gemmini_fence();
  gemmini_flush(0);
  rr_fence(cfg_id);
  gemmini_fence();
}

static inline void gemmini_wait_managed_runtime_style_logged(const char *name,
                                                             uint32_t cfg_id) {
  case_phase_log(name, "wait_rr_fence0_begin");
  rr_fence(cfg_id);
  case_phase_log(name, "wait_rr_fence0_end");
  case_phase_log(name, "wait_cpu_fence0_begin");
  gemmini_fence();
  case_phase_log(name, "wait_cpu_fence0_end");
  case_phase_log(name, "wait_flush_begin");
  gemmini_flush(0);
  case_phase_log(name, "wait_flush_end");
  case_phase_log(name, "wait_rr_fence1_begin");
  rr_fence(cfg_id);
  case_phase_log(name, "wait_rr_fence1_end");
  case_phase_log(name, "wait_cpu_fence1_begin");
  gemmini_fence();
  case_phase_log(name, "wait_cpu_fence1_end");
}

static inline void gemmini_chunk_drain_runtime_style(uint32_t cfg_id,
                                                     const char *name,
                                                     size_t oc_beg,
                                                     size_t oc_tile) {
  printf("CASE_PROGRESS %s phase=chunk_drain_begin oc_beg=%lu oc_tile=%lu cycle=%lu\n",
         name, (unsigned long)oc_beg, (unsigned long)oc_tile, init_cycle());
  gemmini_flush(0);
  rr_fence(cfg_id);
  gemmini_fence();
  printf("CASE_PROGRESS %s phase=chunk_drain_end oc_beg=%lu oc_tile=%lu cycle=%lu\n",
         name, (unsigned long)oc_beg, (unsigned long)oc_tile, init_cycle());
}

static inline void gemmini_wait_for_resadd_acc_reuse(uint32_t cfg_id) {
  rr_fence(cfg_id);
}

static size_t region_page_count(uint64_t vaddr, size_t bytes) {
  const uint64_t first_page = vaddr >> REROCC_SPM_PAGE_SHIFT;
  const uint64_t last_page = (vaddr + bytes - 1ULL) >> REROCC_SPM_PAGE_SHIFT;
  return bytes == 0 ? 0 : (size_t)(last_page - first_page + 1ULL);
}

static bool alias_region_init(spm_alias_region_t *region, uint32_t vpage_base,
                              uint64_t page_offset, size_t bytes) {
  if (region == NULL) {
    return false;
  }

  region->vaddr = SHARED_SPAD_XLATE_RANGE_BASE +
                  (uint64_t)vpage_base * REROCC_SPM_PAGE_BYTES + page_offset;
  region->page_count = region_page_count(region->vaddr, bytes);
  region->bytes = bytes;
  return region->page_count <= ALIAS_REGION_MAX_PAGES;
}

static bool alias_region_build_contiguous(spm_alias_region_t *region, uint32_t vpage_base,
                                          uint64_t page_offset, size_t bytes,
                                          uint32_t local_tile, uint32_t local_page_base) {
  if (!alias_region_init(region, vpage_base, page_offset, bytes)) {
    return false;
  }

  for (size_t i = 0; i < region->page_count; i++) {
    region->page_paddrs[i] = SHARED_SPAD_LOCAL_ADDR_BASE(local_tile) +
                             (uint64_t)(local_page_base + (uint32_t)i) * REROCC_SPM_PAGE_BYTES;
  }
  return true;
}

static bool alias_region_build_interleaved(spm_alias_region_t *region, uint32_t vpage_base,
                                           uint64_t page_offset, size_t bytes,
                                           uint32_t local_page_base, uint32_t slot_base) {
  if (!alias_region_init(region, vpage_base, page_offset, bytes)) {
    return false;
  }

  for (size_t i = 0; i < region->page_count; i++) {
    const uint32_t slot = slot_base + (uint32_t)i;
    const uint32_t tile = slot % REROCC_NUM_GEMMINI;
    const uint32_t local_page = local_page_base + slot / REROCC_NUM_GEMMINI;
    region->page_paddrs[i] = SHARED_SPAD_LOCAL_ADDR_BASE(tile) +
                             (uint64_t)local_page * REROCC_SPM_PAGE_BYTES;
  }
  return true;
}

static bool alias_region_build_focus_layout(spm_alias_region_t *region, uint32_t vpage_base,
                                            uint64_t page_offset, size_t bytes,
                                            uint32_t local_page_base, uint32_t local_tile,
                                            uint32_t slot_base) {
  if (REROCC_FOCUSED_POINTWISE_CONTIGUOUS_LAYOUT) {
    return alias_region_build_contiguous(region, vpage_base, page_offset, bytes,
                                         local_tile, local_page_base);
  }
  return alias_region_build_interleaved(region, vpage_base, page_offset, bytes,
                                        local_page_base, slot_base);
}

static void alias_region_write(const spm_alias_region_t *region, const uint8_t *src, size_t bytes) {
  size_t copied = 0;
  uint64_t page_offset = region->vaddr & (REROCC_SPM_PAGE_BYTES - 1ULL);

  for (size_t i = 0; i < region->page_count && copied < bytes; i++) {
    size_t chunk = (size_t)(REROCC_SPM_PAGE_BYTES - page_offset);
    if (chunk > bytes - copied) {
      chunk = bytes - copied;
    }
    shared_byte_copy((volatile uint8_t *)(uintptr_t)(region->page_paddrs[i] + page_offset),
                     src + copied, chunk);
    copied += chunk;
    page_offset = 0;
  }
}

static void alias_region_read(const spm_alias_region_t *region, uint8_t *dst, size_t bytes) {
  size_t copied = 0;
  uint64_t page_offset = region->vaddr & (REROCC_SPM_PAGE_BYTES - 1ULL);

  for (size_t i = 0; i < region->page_count && copied < bytes; i++) {
    size_t chunk = (size_t)(REROCC_SPM_PAGE_BYTES - page_offset);
    if (chunk > bytes - copied) {
      chunk = bytes - copied;
    }
    shared_byte_read((const volatile uint8_t *)(uintptr_t)(region->page_paddrs[i] + page_offset),
                     dst + copied, chunk);
    copied += chunk;
    page_offset = 0;
  }
}

static void alias_region_zero(const spm_alias_region_t *region, size_t bytes) {
  size_t cleared = 0;
  uint64_t page_offset = region->vaddr & (REROCC_SPM_PAGE_BYTES - 1ULL);

  for (size_t i = 0; i < region->page_count && cleared < bytes; i++) {
    size_t chunk = (size_t)(REROCC_SPM_PAGE_BYTES - page_offset);
    if (chunk > bytes - cleared) {
      chunk = bytes - cleared;
    }
    shared_byte_zero((volatile uint8_t *)(uintptr_t)(region->page_paddrs[i] + page_offset), chunk);
    cleared += chunk;
    page_offset = 0;
  }
}

static void spm_xlate_map_region(const spm_alias_region_t *region) {
  const uint64_t vpage_base = region->vaddr & ~(REROCC_SPM_PAGE_BYTES - 1ULL);
  for (size_t i = 0; i < region->page_count; i++) {
    spm_xlate_map_page(vpage_base + i * REROCC_SPM_PAGE_BYTES, region->page_paddrs[i]);
  }
}

static void print_region(const char *case_name, const char *region_name,
                         const spm_alias_region_t *region) {
  printf("CASE_MAP %s region=%s vaddr=0x%lx pages=%lu",
         case_name, region_name,
         (unsigned long)region->vaddr,
         (unsigned long)region->page_count);
  for (size_t i = 0; i < region->page_count; i++) {
    printf(" p%lu=0x%lx", (unsigned long)i, (unsigned long)region->page_paddrs[i]);
  }
  printf("\n");
}

static void print_region_summary(const char *case_name, const char *region_name,
                                 const spm_alias_region_t *region,
                                 const char *layout_name) {
  const uint64_t first = region->page_count > 0 ? region->page_paddrs[0] : 0ULL;
  const uint64_t second = region->page_count > 1 ? region->page_paddrs[1] : first;
  const uint64_t last =
      region->page_count > 0 ? region->page_paddrs[region->page_count - 1] : 0ULL;
  printf("CASE_MAP_SUMMARY %s region=%s layout=%s vaddr=0x%lx pages=%lu first=0x%lx second=0x%lx last=0x%lx\n",
         case_name, region_name, layout_name,
         (unsigned long)region->vaddr,
         (unsigned long)region->page_count,
         (unsigned long)first,
         (unsigned long)second,
         (unsigned long)last);
}

static void print_region_xlate(const char *case_name, const char *region_name,
                               const spm_alias_region_t *region) {
  const uint64_t vpage_base = region->vaddr & ~(REROCC_SPM_PAGE_BYTES - 1ULL);

  for (size_t i = 0; i < region->page_count; i++) {
    const uint64_t vaddr = vpage_base + i * REROCC_SPM_PAGE_BYTES;
    const uint64_t vpage = (vaddr - SHARED_SPAD_XLATE_RANGE_BASE) >> REROCC_SPM_PAGE_SHIFT;
    const uint64_t pte = vpage < SHARED_SPAD_XLATE_PTE_CAP ? spm_xlate_pte[vpage] : 0ULL;
    printf("CASE_XLATE %s region=%s page=%lu vaddr=0x%lx pte=0x%lx paddr=0x%lx\n",
           case_name, region_name,
           (unsigned long)i,
           (unsigned long)vaddr,
           (unsigned long)pte,
           (unsigned long)region->page_paddrs[i]);
  }
}

static void print_acc_prefix(const char *case_name, const char *tag,
                             const acc_t *buf, size_t count) {
  printf("CASE_TRACE %s %s", case_name, tag);
  for (size_t i = 0; i < count; i++) {
    printf(" v%lu=%ld", (unsigned long)i, (long)buf[i]);
  }
  printf("\n");
}

static bool alias_regions_overlap(const spm_alias_region_t *lhs,
                                  const spm_alias_region_t *rhs) {
  size_t lhs_copied = 0;
  uint64_t lhs_page_offset = lhs->vaddr & (REROCC_SPM_PAGE_BYTES - 1ULL);

  for (size_t i = 0; i < lhs->page_count && lhs_copied < lhs->bytes; i++) {
    const uint64_t lhs_base = lhs->page_paddrs[i];
    const uint64_t lhs_begin = lhs_base + lhs_page_offset;
    size_t lhs_chunk = (size_t)(REROCC_SPM_PAGE_BYTES - lhs_page_offset);
    if (lhs_chunk > lhs->bytes - lhs_copied) {
      lhs_chunk = lhs->bytes - lhs_copied;
    }
    const uint64_t lhs_end = lhs_begin + lhs_chunk;
    size_t rhs_copied = 0;
    uint64_t rhs_page_offset = rhs->vaddr & (REROCC_SPM_PAGE_BYTES - 1ULL);

    for (size_t j = 0; j < rhs->page_count && rhs_copied < rhs->bytes; j++) {
      const uint64_t rhs_base = rhs->page_paddrs[j];
      const uint64_t rhs_begin = rhs_base + rhs_page_offset;
      size_t rhs_chunk = (size_t)(REROCC_SPM_PAGE_BYTES - rhs_page_offset);
      if (rhs_chunk > rhs->bytes - rhs_copied) {
        rhs_chunk = rhs->bytes - rhs_copied;
      }
      const uint64_t rhs_end = rhs_begin + rhs_chunk;

      if (!(lhs_end <= rhs_begin || rhs_end <= lhs_begin)) {
        return true;
      }

      rhs_copied += rhs_chunk;
      rhs_page_offset = 0;
    }

    lhs_copied += lhs_chunk;
    lhs_page_offset = 0;
  }

  return false;
}

static bool alias_regions_virtual_page_overlap(const spm_alias_region_t *lhs,
                                               const spm_alias_region_t *rhs) {
  const uint64_t lhs_begin = lhs->vaddr & ~(REROCC_SPM_PAGE_BYTES - 1ULL);
  const uint64_t rhs_begin = rhs->vaddr & ~(REROCC_SPM_PAGE_BYTES - 1ULL);
  const uint64_t lhs_end = lhs_begin + lhs->page_count * REROCC_SPM_PAGE_BYTES;
  const uint64_t rhs_end = rhs_begin + rhs->page_count * REROCC_SPM_PAGE_BYTES;

  return !(lhs_end <= rhs_begin || rhs_end <= lhs_begin);
}

static bool validate_alias_layout(const char **names, const spm_alias_region_t **regions,
                                  size_t count) {
  for (size_t i = 0; i < count; i++) {
    for (size_t j = i + 1; j < count; j++) {
      if (alias_regions_overlap(regions[i], regions[j])) {
        printf("ALL_TESTS_FAIL reason=region_overlap lhs=%s rhs=%s\n",
               names[i], names[j]);
        return false;
      }
    }
  }
  return true;
}

static bool validate_case_xlate_layout(const char *case_name, const char **names,
                                       const spm_alias_region_t **regions, size_t count) {
  for (size_t i = 0; i < count; i++) {
    for (size_t j = i + 1; j < count; j++) {
      if (alias_regions_virtual_page_overlap(regions[i], regions[j])) {
        printf("CASE_FAIL %s reason=region_vpage_overlap lhs=%s rhs=%s\n",
               case_name, names[i], names[j]);
        return false;
      }
    }
  }

  return true;
}

static bool elem_buffer_matches(const elem_t *expected, const elem_t *actual, size_t n,
                                const char *case_name) {
  for (size_t i = 0; i < n; i++) {
    if (expected[i] != actual[i]) {
      printf("CASE_MISMATCH %s idx=%lu exp=%d act=%d\n",
             case_name, (unsigned long)i, (int)expected[i], (int)actual[i]);
      return false;
    }
  }
  return true;
}

static bool copy_explicit_issue_no_fence(const elem_t *src, elem_t *dst,
                                         size_t I, size_t J, size_t stride,
                                         bool use_mvin2,
                                         uint32_t acc_row_offset) {
  size_t tile_I = I;
  size_t tile_J = J;
  size_t total_acc_rows;

  if (!src || !dst || I == 0 || J == 0 || stride == 0) {
    return false;
  }

  total_acc_rows = (tile_I / DIM + (tile_I % DIM != 0)) * DIM *
                   (tile_J / DIM + (tile_J % DIM != 0));

  while (total_acc_rows > ACC_ROWS / 2) {
    if (tile_I >= tile_J || tile_J <= DIM) {
      tile_I /= 2;
    } else {
      tile_J -= DIM;
    }
    if (tile_I == 0 || tile_J == 0) {
      return false;
    }
    total_acc_rows = (tile_I / DIM + (tile_I % DIM != 0)) * DIM *
                     (tile_J / DIM + (tile_J % DIM != 0));
  }

  gemmini_extended_config_st(stride * sizeof(elem_t),
                             NO_ACTIVATION,
                             ACC_SCALE_IDENTITY);
  gemmini_config_ex(WS, 0, 0);
  gemmini_extended4_config_ld(stride * sizeof(elem_t), MVIN_SCALE_IDENTITY, true, DIM, 0);
  gemmini_extended4_config_ld(stride * sizeof(elem_t), MVIN_SCALE_IDENTITY, true, DIM, 1);

  for (size_t i = 0; i < I; i += tile_I) {
    for (size_t j = 0; j < J; j += tile_J) {
      const size_t I_tile = i + tile_I <= I ? tile_I : I - i;
      const size_t J_tile = j + tile_J <= J ? tile_J : J - j;
      const elem_t *tile_src = src + i * stride + j;
      elem_t *tile_dst = dst + i * stride + j;
      const elem_t *tile_zero = (const elem_t *)resadd_out_shadow_dram + i * stride + j;
      const size_t rounded_up_J = (J_tile / DIM + (J_tile % DIM != 0)) * DIM;
      size_t blocks = rounded_up_J / DIM;
      const uint32_t acc_write_addr_start = 1U << (ADDR_LEN - 1);
      const uint32_t acc_accum_addr_start = 3U << (ADDR_LEN - 2);

      if (blocks == 0) {
        continue;
      }
      if (blocks > MAX_BLOCK_LEN) {
        blocks = MAX_BLOCK_LEN;
      }

      for (size_t ii = 0; ii < I_tile; ii += DIM) {
        for (size_t jj = 0; jj < J_tile; jj += blocks * DIM) {
          const size_t cols = jj + blocks * DIM <= J_tile ? blocks * DIM : J_tile - jj;
          const size_t rows = ii + DIM <= I_tile ? DIM : I_tile - ii;
          const elem_t *src_dram_addr = tile_src + ii * stride + jj;
          const elem_t *zero_dram_addr = tile_zero + ii * stride + jj;
          elem_t *dst_dram_addr = tile_dst + ii * stride + jj;
          const uint32_t acc_row_base = (uint32_t)(ii * (rounded_up_J / DIM) + jj);
          const uint32_t acc_write_addr = acc_write_addr_start + acc_row_offset + acc_row_base;
          const uint32_t acc_accum_addr = acc_accum_addr_start + acc_row_offset + acc_row_base;

          if (use_mvin2) {
            // mvin2 uses accumulator accumulate-on-write semantics, so the
            // backing accumulator rows must first be explicitly initialized.
            gemmini_extended_mvin(zero_dram_addr, acc_write_addr, cols, rows);
            gemmini_wait_for_resadd_acc_reuse(GEMMINI_CFG_ID);
            gemmini_extended_mvin2(src_dram_addr, acc_accum_addr, cols, rows);
            gemmini_wait_for_resadd_acc_reuse(GEMMINI_CFG_ID);
          } else {
            gemmini_extended_mvin(src_dram_addr, acc_write_addr, cols, rows);
          }
          gemmini_extended_mvout(dst_dram_addr, acc_write_addr, cols, rows);
        }
      }

      if (j + tile_J < J || i + tile_I < I) {
        gemmini_wait_for_resadd_acc_reuse(GEMMINI_CFG_ID);
      }
    }
  }

  return true;
}

static bool resadd_explicit_issue_no_fence(const elem_t *a, const elem_t *b, elem_t *out,
                                           size_t I, size_t J, size_t stride, bool relu) {
  size_t tile_I = I;
  size_t tile_J = J;
  size_t total_acc_rows;

  if (!a || !b || !out || I == 0 || J == 0 || stride == 0) {
    return false;
  }

  total_acc_rows = (tile_I / DIM + (tile_I % DIM != 0)) * DIM *
                   (tile_J / DIM + (tile_J % DIM != 0));

  while (total_acc_rows > ACC_ROWS / 2) {
    if (tile_I >= tile_J || tile_J <= DIM) {
      tile_I /= 2;
    } else {
      tile_J -= DIM;
    }
    if (tile_I == 0 || tile_J == 0) {
      return false;
    }
    total_acc_rows = (tile_I / DIM + (tile_I % DIM != 0)) * DIM *
                     (tile_J / DIM + (tile_J % DIM != 0));
  }

  gemmini_extended_config_st(stride * sizeof(elem_t),
                             relu ? RELU : NO_ACTIVATION,
                             ACC_SCALE_IDENTITY);
  gemmini_config_ex(WS, 0, 0);
  gemmini_extended4_config_ld(stride * sizeof(elem_t), MVIN_SCALE_IDENTITY, true, DIM, 0);
  gemmini_extended4_config_ld(stride * sizeof(elem_t), MVIN_SCALE_IDENTITY, true, DIM, 1);

  for (size_t i = 0; i < I; i += tile_I) {
    for (size_t j = 0; j < J; j += tile_J) {
      const size_t I_tile = i + tile_I <= I ? tile_I : I - i;
      const size_t J_tile = j + tile_J <= J ? tile_J : J - j;
      const elem_t *tile_a = a + i * stride + j;
      const elem_t *tile_b = b + i * stride + j;
      elem_t *tile_out = out + i * stride + j;
      const size_t rounded_up_J = (J_tile / DIM + (J_tile % DIM != 0)) * DIM;
      size_t blocks = rounded_up_J / DIM;
      const uint32_t A_acc_addr_start = 1U << (ADDR_LEN - 1);
      const uint32_t B_acc_addr_start = 3U << (ADDR_LEN - 2);

      if (blocks == 0) {
        continue;
      }
      if (blocks > MAX_BLOCK_LEN) {
        blocks = MAX_BLOCK_LEN;
      }

      for (size_t ii = 0; ii < I_tile; ii += DIM) {
        for (size_t jj = 0; jj < J_tile; jj += blocks * DIM) {
          const size_t cols = jj + blocks * DIM <= J_tile ? blocks * DIM : J_tile - jj;
          const size_t rows = ii + DIM <= I_tile ? DIM : I_tile - ii;
          const elem_t *A_dram_addr = tile_a + ii * stride + jj;
          const elem_t *B_dram_addr = tile_b + ii * stride + jj;
          elem_t *C_dram_addr = tile_out + ii * stride + jj;
          const uint32_t acc_row_base = (uint32_t)(ii * (rounded_up_J / DIM) + jj);
          const uint32_t A_acc_addr = A_acc_addr_start + acc_row_base;
          const uint32_t B_acc_addr = B_acc_addr_start + acc_row_base;

          gemmini_extended_mvin(A_dram_addr, A_acc_addr, cols, rows);
          // In ReRoCC manager mode the dependent mvin2 must not issue until
          // the accumulator rows have been updated with A.
          gemmini_wait_for_resadd_acc_reuse(GEMMINI_CFG_ID);
          gemmini_extended_mvin2(B_dram_addr, B_acc_addr, cols, rows);
          // The explicit resadd path then reads the same accumulator rows back
          // out through the non-accumulate address view.
          gemmini_wait_for_resadd_acc_reuse(GEMMINI_CFG_ID);
          gemmini_extended_mvout(C_dram_addr, A_acc_addr, cols, rows);
        }
      }

      if (j + tile_J < J || i + tile_I < I) {
        gemmini_wait_for_resadd_acc_reuse(GEMMINI_CFG_ID);
      }
    }
  }

  return true;
}

static bool resadd_explicit_issue_serialized(const elem_t *a, const elem_t *b, elem_t *out,
                                             size_t I, size_t J, size_t stride, bool relu) {
  size_t tile_I = I;
  size_t tile_J = J;
  size_t total_acc_rows;

  if (!a || !b || !out || I == 0 || J == 0 || stride == 0) {
    return false;
  }

  total_acc_rows = (tile_I / DIM + (tile_I % DIM != 0)) * DIM *
                   (tile_J / DIM + (tile_J % DIM != 0));

  while (total_acc_rows > ACC_ROWS / 2) {
    if (tile_I >= tile_J || tile_J <= DIM) {
      tile_I /= 2;
    } else {
      tile_J -= DIM;
    }
    if (tile_I == 0 || tile_J == 0) {
      return false;
    }
    total_acc_rows = (tile_I / DIM + (tile_I % DIM != 0)) * DIM *
                     (tile_J / DIM + (tile_J % DIM != 0));
  }

  gemmini_extended_config_st(stride * sizeof(elem_t),
                             relu ? RELU : NO_ACTIVATION,
                             ACC_SCALE_IDENTITY);
  gemmini_config_ex(WS, 0, 0);
  gemmini_extended4_config_ld(stride * sizeof(elem_t), MVIN_SCALE_IDENTITY, true, DIM, 0);
  gemmini_extended4_config_ld(stride * sizeof(elem_t), MVIN_SCALE_IDENTITY, true, DIM, 1);

  for (size_t i = 0; i < I; i += tile_I) {
    for (size_t j = 0; j < J; j += tile_J) {
      const size_t I_tile = i + tile_I <= I ? tile_I : I - i;
      const size_t J_tile = j + tile_J <= J ? tile_J : J - j;
      const elem_t *tile_a = a + i * stride + j;
      const elem_t *tile_b = b + i * stride + j;
      elem_t *tile_out = out + i * stride + j;
      const size_t rounded_up_J = (J_tile / DIM + (J_tile % DIM != 0)) * DIM;
      size_t blocks = rounded_up_J / DIM;
      const uint32_t A_acc_addr_start = 1U << (ADDR_LEN - 1);
      const uint32_t B_acc_addr_start = 3U << (ADDR_LEN - 2);

      if (blocks == 0) {
        continue;
      }
      if (blocks > MAX_BLOCK_LEN) {
        blocks = MAX_BLOCK_LEN;
      }

      for (size_t ii = 0; ii < I_tile; ii += DIM) {
        for (size_t jj = 0; jj < J_tile; jj += blocks * DIM) {
          const size_t cols = jj + blocks * DIM <= J_tile ? blocks * DIM : J_tile - jj;
          const size_t rows = ii + DIM <= I_tile ? DIM : I_tile - ii;
          const elem_t *A_dram_addr = tile_a + ii * stride + jj;
          const elem_t *B_dram_addr = tile_b + ii * stride + jj;
          elem_t *C_dram_addr = tile_out + ii * stride + jj;
          const uint32_t acc_row_base = (uint32_t)(ii * (rounded_up_J / DIM) + jj);
          const uint32_t A_acc_addr = A_acc_addr_start + acc_row_base;
          const uint32_t B_acc_addr = B_acc_addr_start + acc_row_base;

          gemmini_extended_mvin(A_dram_addr, A_acc_addr, cols, rows);
          gemmini_wait_for_resadd_acc_reuse(GEMMINI_CFG_ID);
          gemmini_extended_mvin2(B_dram_addr, B_acc_addr, cols, rows);
          gemmini_wait_for_resadd_acc_reuse(GEMMINI_CFG_ID);
          gemmini_extended_mvout(C_dram_addr, A_acc_addr, cols, rows);
          gemmini_wait_for_resadd_acc_reuse(GEMMINI_CFG_ID);
        }
      }
    }
  }

  return true;
}

static bool resadd_standard_issue(const elem_t *a, const elem_t *b, elem_t *out,
                                  size_t I, size_t J, size_t stride, bool relu) {
  if (!a || !b || !out || I == 0 || J == 0 || stride == 0) {
    return false;
  }

  tiled_resadd_stride_auto(I, J,
                           MVIN_SCALE_IDENTITY,
                           MVIN_SCALE_IDENTITY,
                           ACC_SCALE_IDENTITY,
                           stride,
                           a, b, out, relu, WS);
  return true;
}

static elem_t sat_i8_from_acc(acc_t value) {
  if (value > elem_t_max) value = elem_t_max;
  if (value < elem_t_min) value = elem_t_min;
  return (elem_t)value;
}

static void pointwise_matmul_reference(const elem_t *a, const elem_t *b, const acc_t *bias,
                                       elem_t *out) {
  const unsigned long start_cycle = init_cycle();
  const size_t row_step = init_progress_step(PW_I, 4);
  printf("INIT_FN_START fn=pointwise_matmul_reference a=0x%lx b=0x%lx bias=0x%lx out=0x%lx I=%u J=%u K=%u cycle=%lu\n",
         (unsigned long)a, (unsigned long)b, (unsigned long)bias, (unsigned long)out,
         PW_I, PW_J, PW_K, start_cycle);
  for (size_t i = 0; i < PW_I; ++i) {
    for (size_t j = 0; j < PW_J; ++j) {
      acc_t acc = bias ? bias[j] : 0;
      for (size_t k = 0; k < PW_K; ++k) {
        acc += (acc_t)a[i * PW_A_STRIDE + k] * (acc_t)b[k * PW_B_STRIDE + j];
      }
      out[i * PW_C_STRIDE + j] = sat_i8_from_acc(acc);
    }
    if (init_should_log_progress(i, PW_I, row_step)) {
      printf("INIT_FN_PROGRESS fn=pointwise_matmul_reference row=%lu total_rows=%u out_row0=%d out_row_last=%d cycle=%lu\n",
             (unsigned long)i, PW_I,
             (int)out[i * PW_C_STRIDE],
             (int)out[i * PW_C_STRIDE + (PW_J - 1)],
             init_cycle());
    }
  }
  const unsigned long end_cycle = init_cycle();
  printf("INIT_FN_END fn=pointwise_matmul_reference out=0x%lx last=%d cycle=%lu delta=%lu\n",
         (unsigned long)out, (int)out[(PW_I - 1) * PW_C_STRIDE + (PW_J - 1)],
         end_cycle, end_cycle - start_cycle);
}

static void pointwise_matmul_reference_dim_j(const elem_t *a, const elem_t *b,
                                             const acc_t *bias, const elem_t *init_c,
                                             elem_t *out, size_t dim_j) {
  const unsigned long start_cycle = init_cycle();
  printf("INIT_FN_START fn=pointwise_matmul_reference_dim_j a=0x%lx b=0x%lx bias=0x%lx init_c=0x%lx out=0x%lx I=%u J=%lu K=%u cycle=%lu\n",
         (unsigned long)a, (unsigned long)b, (unsigned long)bias,
         (unsigned long)init_c, (unsigned long)out, PW_I, (unsigned long)dim_j, PW_K,
         start_cycle);
  memcpy(out, init_c, PW_BYTES_C);

  for (size_t i = 0; i < PW_I; ++i) {
    for (size_t j = 0; j < dim_j; ++j) {
      acc_t acc = bias ? bias[j] : 0;
      for (size_t k = 0; k < PW_K; ++k) {
        acc += (acc_t)a[i * PW_A_STRIDE + k] * (acc_t)b[k * PW_B_STRIDE + j];
      }
      out[i * PW_C_STRIDE + j] = sat_i8_from_acc(acc);
    }
  }
  const unsigned long end_cycle = init_cycle();
  printf("INIT_FN_END fn=pointwise_matmul_reference_dim_j out=0x%lx last=%d cycle=%lu delta=%lu\n",
         (unsigned long)out, (int)out[(PW_I - 1) * PW_C_STRIDE + (dim_j - 1)],
         end_cycle, end_cycle - start_cycle);
}

static void pointwise_matmul_reference_shape(const elem_t *a, const elem_t *b,
                                             const acc_t *bias, const elem_t *init_c,
                                             elem_t *out,
                                             size_t dim_i, size_t dim_j, size_t dim_k,
                                             size_t a_stride, size_t b_stride,
                                             size_t c_stride) {
  const size_t c_bytes = dim_i * c_stride * sizeof(elem_t);
  const unsigned long start_cycle = init_cycle();
  printf("INIT_FN_START fn=pointwise_matmul_reference_shape a=0x%lx b=0x%lx bias=0x%lx init_c=0x%lx out=0x%lx I=%lu J=%lu K=%lu a_stride=%lu b_stride=%lu c_stride=%lu cycle=%lu\n",
         (unsigned long)a, (unsigned long)b, (unsigned long)bias,
         (unsigned long)init_c, (unsigned long)out,
         (unsigned long)dim_i, (unsigned long)dim_j, (unsigned long)dim_k,
         (unsigned long)a_stride, (unsigned long)b_stride, (unsigned long)c_stride,
         start_cycle);
  memcpy(out, init_c, c_bytes);

  for (size_t i = 0; i < dim_i; ++i) {
    for (size_t j = 0; j < dim_j; ++j) {
      acc_t acc = bias ? bias[j] : 0;
      for (size_t k = 0; k < dim_k; ++k) {
        acc += (acc_t)a[i * a_stride + k] * (acc_t)b[k * b_stride + j];
      }
      out[i * c_stride + j] = sat_i8_from_acc(acc);
    }
  }
  const unsigned long end_cycle = init_cycle();
  printf("INIT_FN_END fn=pointwise_matmul_reference_shape out=0x%lx last=%d cycle=%lu delta=%lu\n",
         (unsigned long)out, (int)out[(dim_i - 1) * c_stride + (dim_j - 1)],
         end_cycle, end_cycle - start_cycle);
}

static void pointwise_matmul_reference_grouped_runtime_style(const elem_t *a,
                                                             const elem_t *b,
                                                             const acc_t *bias,
                                                             const elem_t *init_c,
                                                             elem_t *out) {
  const unsigned long start_cycle = init_cycle();
  printf("INIT_FN_START fn=pointwise_matmul_reference_grouped_runtime_style a=0x%lx b=0x%lx bias=0x%lx init_c=0x%lx out=0x%lx I=%u J=%u K=%u groups=%u cycle=%lu\n",
         (unsigned long)a, (unsigned long)b, (unsigned long)bias,
         (unsigned long)init_c, (unsigned long)out,
         PW_I, PW_J, PW_K, PW_GROUP_COUNT, start_cycle);
  memcpy(out, init_c, PW_BYTES_C);

  for (size_t group_idx = 0; group_idx < PW_GROUP_COUNT; ++group_idx) {
    const elem_t *group_a = a + group_idx * (size_t)PW_K;
    const elem_t *group_b = b + group_idx * (size_t)PW_K * (size_t)PW_B_STRIDE;
    const acc_t *group_bias = bias ? (bias + group_idx * (size_t)PW_J) : NULL;
    printf("INIT_FN_PROGRESS fn=pointwise_matmul_reference_grouped_runtime_style phase=group_start group=%lu/%u cycle=%lu\n",
           (unsigned long)(group_idx + 1U), PW_GROUP_COUNT, init_cycle());
    for (size_t i = 0; i < PW_I; ++i) {
      for (size_t j = 0; j < PW_J; ++j) {
        acc_t acc = group_bias ? group_bias[j] : 0;
        for (size_t k = 0; k < PW_K; ++k) {
          acc += (acc_t)group_a[i * PW_A_STRIDE + k] * (acc_t)group_b[k * PW_B_STRIDE + j];
        }
        out[i * PW_C_STRIDE + group_idx * (size_t)PW_J + j] = sat_i8_from_acc(acc);
      }
    }
    printf("INIT_FN_PROGRESS fn=pointwise_matmul_reference_grouped_runtime_style phase=group_end group=%lu/%u cycle=%lu\n",
           (unsigned long)(group_idx + 1U), PW_GROUP_COUNT, init_cycle());
  }

  {
    const unsigned long end_cycle = init_cycle();
    printf("INIT_FN_END fn=pointwise_matmul_reference_grouped_runtime_style out=0x%lx last=%d cycle=%lu delta=%lu\n",
           (unsigned long)out, (int)out[(PW_I - 1U) * PW_C_STRIDE + (PW_C_STRIDE - 1U)],
           end_cycle, end_cycle - start_cycle);
  }
}

static void pointwise_matmul_chunked_reference(const elem_t *a, const elem_t *b,
                                               const acc_t *bias, const elem_t *init_c,
                                               elem_t *out) {
  const unsigned long start_cycle = init_cycle();
  printf("INIT_FN_START fn=pointwise_matmul_chunked_reference a=0x%lx b=0x%lx bias=0x%lx init_c=0x%lx out=0x%lx I=%u J=%u K=%u oc=%u cycle=%lu\n",
         (unsigned long)a, (unsigned long)b, (unsigned long)bias,
         (unsigned long)init_c, (unsigned long)out,
         PW_I, PW_CHUNK_TILE_J, PW_K, PW_CHUNK_OC, start_cycle);
  memcpy(out, init_c, PW_BYTES_C);

  for (size_t oc_beg = 0; oc_beg < PW_CHUNK_TILE_J; oc_beg += PW_CHUNK_OC) {
    const size_t oc_tile =
      oc_beg + PW_CHUNK_OC <= PW_CHUNK_TILE_J ? PW_CHUNK_OC : (PW_CHUNK_TILE_J - oc_beg);
    printf("INIT_FN_PROGRESS fn=pointwise_matmul_chunked_reference phase=chunk_start oc_beg=%lu oc_tile=%lu cycle=%lu\n",
           (unsigned long)oc_beg, (unsigned long)oc_tile, init_cycle());

    for (size_t i = 0; i < PW_I; ++i) {
      for (size_t j = 0; j < oc_tile; ++j) {
        acc_t acc = bias ? bias[oc_beg + j] : 0;
        for (size_t k = 0; k < PW_K; ++k) {
          acc += (acc_t)a[i * PW_A_STRIDE + k] *
                 (acc_t)b[k * PW_B_STRIDE + oc_beg + j];
        }
        out[i * PW_C_STRIDE + oc_beg + j] = sat_i8_from_acc(acc);
      }
    }
    printf("INIT_FN_PROGRESS fn=pointwise_matmul_chunked_reference phase=chunk_end oc_beg=%lu oc_tile=%lu cycle=%lu\n",
           (unsigned long)oc_beg, (unsigned long)oc_tile, init_cycle());
  }
  const unsigned long end_cycle = init_cycle();
  printf("INIT_FN_END fn=pointwise_matmul_chunked_reference out=0x%lx last=%d cycle=%lu delta=%lu\n",
         (unsigned long)out,
         (int)out[(PW_I - 1) * PW_C_STRIDE + (PW_CHUNK_TILE_J - 1)], end_cycle,
         end_cycle - start_cycle);
}

static bool pointwise_matmul_issue(const elem_t *a, const elem_t *b, const acc_t *bias,
                                   elem_t *out, enum tiled_matmul_type_t tiled_type) {
  if (!a || !b || !out) return false;
  tiled_matmul_nn_stride_auto(PW_I, PW_J, PW_K,
                              PW_A_STRIDE, PW_B_STRIDE, PW_C_STRIDE,
                              a, b, bias, out,
                              NO_ACTIVATION, ACC_SCALE_IDENTITY, true,
                              tiled_type);
  return true;
}

static bool pointwise_matmul_issue_dim_j(const elem_t *a, const elem_t *b, const acc_t *bias,
                                         elem_t *out, size_t dim_j,
                                         enum tiled_matmul_type_t tiled_type) {
  if (!a || !b || !out || dim_j == 0) return false;
  tiled_matmul_nn_stride_auto(PW_I, dim_j, PW_K,
                              PW_A_STRIDE, PW_B_STRIDE, PW_C_STRIDE,
                              a, b, bias, out,
                              NO_ACTIVATION, ACC_SCALE_IDENTITY, true,
                              tiled_type);
  return true;
}

static bool pointwise_matmul_issue_shape(const elem_t *a, const elem_t *b, const acc_t *bias,
                                         elem_t *out,
                                         size_t dim_i, size_t dim_j, size_t dim_k,
                                         size_t a_stride, size_t b_stride, size_t c_stride,
                                         enum tiled_matmul_type_t tiled_type) {
  if (!a || !b || !out || dim_i == 0 || dim_j == 0 || dim_k == 0 ||
      a_stride == 0 || b_stride == 0 || c_stride == 0) {
    return false;
  }
  tiled_matmul_nn_stride_auto(dim_i, dim_j, dim_k,
                              a_stride, b_stride, c_stride,
                              a, b, bias, out,
                              NO_ACTIVATION, ACC_SCALE_IDENTITY, true,
                              tiled_type);
  return true;
}

static bool run_bias_mvin_linux_first_tile_case(const char *name, int gemmini_manager_id,
                                                const spm_alias_region_t *bias_region,
                                                const acc_t *bias_src, size_t bias_bytes) {
  const acc_t *bias_req = (const acc_t *)(uintptr_t)bias_region->vaddr;
  const uint64_t rs1 = (uint64_t)(uintptr_t)bias_req;
  const uint64_t rs2 = prt_gemmini_pack_mvin_rs2(PW_LINUX_TILE_D_SP_ADDR,
                                                 PW_LINUX_TILE_COLS,
                                                 PW_LINUX_TILE_ROWS);
  acc_t bias_alias_head[PW_LINUX_TILE_COLS];

  printf("CASE_START %s bytes_bias=%lu page_bytes=%lu page_offset=%lu outer_I=%u outer_J=%u outer_K=%u inner_I=%u inner_J=%u inner_K=%u act=%d repeating_bias=1 D_stride=0 sp=0x%x cols=%u rows=%u rs1=0x%lx rs2=0x%lx\n",
         name,
         (unsigned long)bias_bytes,
         (unsigned long)REROCC_SPM_PAGE_BYTES,
         (unsigned long)PW_VADDR_PAGE_OFFSET,
         PW_I, PW_J, PW_K,
         PW_LINUX_TILE_I, PW_LINUX_TILE_J, PW_LINUX_TILE_K,
         RELU,
         PW_LINUX_TILE_D_SP_ADDR,
         PW_LINUX_TILE_COLS,
         PW_LINUX_TILE_ROWS,
         (unsigned long)rs1,
         (unsigned long)rs2);
  print_region(name, "BIAS", bias_region);

  {
    const char *region_names[] = {"BIAS"};
    const spm_alias_region_t *regions[] = {bias_region};
    if (!validate_case_xlate_layout(name, region_names, regions,
                                    sizeof(regions) / sizeof(regions[0]))) {
      return false;
    }
  }

  alias_region_zero(bias_region, bias_region->bytes);
  alias_region_write(bias_region, (const uint8_t *)bias_src, bias_bytes);
  memset(bias_alias_head, 0, sizeof(bias_alias_head));
  alias_region_read(bias_region, (uint8_t *)bias_alias_head, sizeof(bias_alias_head));
  print_acc_prefix(name, "bias-src-head", bias_src, PW_LINUX_TILE_COLS);
  print_acc_prefix(name, "bias-alias-head", bias_alias_head, PW_LINUX_TILE_COLS);

  if (!rr_acquire_cfg_with_retry(GEMMINI_CFG_ID, (uint64_t)gemmini_manager_id)) {
    printf("CASE_FAIL %s reason=acquire\n", name);
    return false;
  }

  rr_set_opc(3, GEMMINI_CFG_ID);
  gemmini_flush(0);
  spm_xlate_table_clear();
  spm_xlate_map_region(bias_region);
  spm_xlate_program(true);
  print_region_xlate(name, "BIAS", bias_region);

  printf("CASE_TRACE %s config-ex dataflow=%d act=%d A_stride=1 A_transpose=0 B_transpose=0\n",
         name, OUTPUT_STATIONARY, RELU & 3);
  gemmini_extended_config_ex(OUTPUT_STATIONARY, RELU & 3, 0, 1, false, false);
  printf("CASE_TRACE %s config-st stride_bytes=%lu act=%d scale=identity\n",
         name, (unsigned long)(PW_C_STRIDE * sizeof(elem_t)), RELU & 3);
  gemmini_extended_config_st(PW_C_STRIDE * sizeof(elem_t), RELU & 3, ACC_SCALE_IDENTITY);
  printf("CASE_TRACE %s config-ld-a stride_bytes=%lu id=0\n",
         name, (unsigned long)(PW_A_STRIDE * sizeof(elem_t)));
  gemmini_extended3_config_ld(PW_A_STRIDE * sizeof(elem_t), MVIN_SCALE_IDENTITY, false, 0);
  printf("CASE_TRACE %s config-ld-b stride_bytes=%lu id=1\n",
         name, (unsigned long)(PW_B_STRIDE * sizeof(elem_t)));
  gemmini_extended3_config_ld(PW_B_STRIDE * sizeof(elem_t), MVIN_SCALE_IDENTITY, false, 1);
  printf("CASE_TRACE %s config-ld-d-outer stride_bytes=0 id=2\n", name);
  gemmini_extended3_config_ld(0, ACC_SCALE_IDENTITY, false, 2);
  printf("CASE_TRACE %s config-ld-d-inner stride_bytes=0 default-id\n", name);
  gemmini_extended_config_ld(0, ACC_SCALE_IDENTITY);

  printf("CASE_TRACE %s pre-issue dram=0x%lx sp=0x%x cols=%u rows=%u\n",
         name,
         (unsigned long)rs1,
         PW_LINUX_TILE_D_SP_ADDR,
         PW_LINUX_TILE_COLS,
         PW_LINUX_TILE_ROWS);
  prt_gemmini_issue_bias_mvin0_debug(bias_req, PW_LINUX_TILE_D_SP_ADDR,
                                     PW_LINUX_TILE_COLS, PW_LINUX_TILE_ROWS);
  printf("CASE_TRACE %s post-issue\n", name);

  gemmini_wait_managed(GEMMINI_CFG_ID);
  printf("CASE_TRACE %s post-wait\n", name);
  spm_xlate_reset();
  rr_fence(GEMMINI_CFG_ID);
  rr_release(GEMMINI_CFG_ID);

  printf("CASE_RESULT %s PASS\n", name);
  return true;
}

static bool run_bias_mvin3_runtime_focus_case(const char *name, int gemmini_manager_id,
                                              const spm_alias_region_t *bias_region,
                                              const acc_t *bias_src, size_t bias_bytes) {
  const acc_t *bias_req = (const acc_t *)(uintptr_t)bias_region->vaddr;
  const uint64_t rs1 = (uint64_t)(uintptr_t)bias_req;
  const uint64_t rs2 = prt_gemmini_pack_mvin_rs2(PW_LINUX_TILE_D_SP_ADDR,
                                                 PW_LINUX_TILE_COLS,
                                                 PW_LINUX_TILE_ROWS);
  acc_t bias_alias_head[PW_LINUX_TILE_COLS];
  uint64_t xlate_fault_pre = 0;

  printf("CASE_START %s target=focus-bias-mvin3-alias bytes_bias=%lu page_bytes=%lu page_offset=%lu cfg=%u opcode=%u manager=%d outer_I=%u outer_J=%u outer_K=%u inner_I=%u inner_J=%u inner_K=%u act=%d repeating_bias=1 D_stride=0 sp=0x%x cols=%u rows=%u rs1=0x%lx rs2=0x%lx\n",
         name,
         (unsigned long)bias_bytes,
         (unsigned long)REROCC_SPM_PAGE_BYTES,
         (unsigned long)PW_VADDR_PAGE_OFFSET,
         GEMMINI_CFG_ID,
         GEMMINI_OPCODE_ID,
         gemmini_manager_id,
         PW_I, PW_J, PW_K,
         PW_LINUX_TILE_I, PW_LINUX_TILE_J, PW_LINUX_TILE_K,
         RELU,
         PW_LINUX_TILE_D_SP_ADDR,
         PW_LINUX_TILE_COLS,
         PW_LINUX_TILE_ROWS,
         (unsigned long)rs1,
         (unsigned long)rs2);
  print_region_summary(name, "BIAS", bias_region, "interleaved");

  {
    const char *region_names[] = {"BIAS"};
    const spm_alias_region_t *regions[] = {bias_region};
    if (!validate_case_xlate_layout(name, region_names, regions,
                                    sizeof(regions) / sizeof(regions[0]))) {
      return false;
    }
  }

  alias_region_zero(bias_region, bias_region->bytes);
  alias_region_write(bias_region, (const uint8_t *)bias_src, bias_bytes);
  memset(bias_alias_head, 0, sizeof(bias_alias_head));
  alias_region_read(bias_region, (uint8_t *)bias_alias_head, sizeof(bias_alias_head));
  print_acc_prefix(name, "bias-src-head", bias_src, PW_LINUX_TILE_COLS);
  print_acc_prefix(name, "bias-alias-head", bias_alias_head, PW_LINUX_TILE_COLS);

  if (!rr_acquire_cfg_with_retry(GEMMINI_CFG_ID, (uint64_t)gemmini_manager_id)) {
    printf("CASE_FAIL %s reason=acquire\n", name);
    return false;
  }

  rr_set_opc(GEMMINI_OPCODE_ID, GEMMINI_CFG_ID);
  spm_xlate_table_clear();
  spm_xlate_map_region(bias_region);
  spm_xlate_program(true);
  print_region_xlate(name, "BIAS", bias_region);
  xlate_fault_pre = rerocc_gemmini_spm_xlate_fault();
  printf("CASE_TRACE %s xlate-fault-pre raw=0x%lx cycle=%lu\n",
         name, (unsigned long)xlate_fault_pre, init_cycle());

  printf("CASE_TRACE %s config-ex dataflow=%d act=%d A_stride=1 A_transpose=0 B_transpose=0\n",
         name, OUTPUT_STATIONARY, RELU & 3);
  gemmini_extended_config_ex(OUTPUT_STATIONARY, RELU & 3, 0, 1, false, false);
  printf("CASE_TRACE %s config-st stride_bytes=%lu act=%d scale=identity\n",
         name, (unsigned long)(PW_C_STRIDE * sizeof(elem_t)), RELU & 3);
  gemmini_extended_config_st(PW_C_STRIDE * sizeof(elem_t), RELU & 3, ACC_SCALE_IDENTITY);
  printf("CASE_TRACE %s config-ld-a stride_bytes=%lu id=0\n",
         name, (unsigned long)(PW_A_STRIDE * sizeof(elem_t)));
  gemmini_extended3_config_ld(PW_A_STRIDE * sizeof(elem_t), MVIN_SCALE_IDENTITY, false, 0);
  printf("CASE_TRACE %s config-ld-b stride_bytes=%lu id=1\n",
         name, (unsigned long)(PW_B_STRIDE * sizeof(elem_t)));
  gemmini_extended3_config_ld(PW_B_STRIDE * sizeof(elem_t), MVIN_SCALE_IDENTITY, false, 1);
  printf("CASE_TRACE %s config-ld-d stride_bytes=0 id=2 runtime_skip_preflush=1\n", name);
  gemmini_extended3_config_ld(0, ACC_SCALE_IDENTITY, false, 2);

  printf("CASE_TRACE %s pre-issue-mvin3 dram=0x%lx sp=0x%x cols=%u rows=%u cycle=%lu\n",
         name,
         (unsigned long)rs1,
         PW_LINUX_TILE_D_SP_ADDR,
         PW_LINUX_TILE_COLS,
         PW_LINUX_TILE_ROWS,
         init_cycle());
  prt_gemmini_issue_bias_mvin3_debug(bias_req, PW_LINUX_TILE_D_SP_ADDR,
                                     PW_LINUX_TILE_COLS, PW_LINUX_TILE_ROWS);
  printf("CASE_TRACE %s post-issue-mvin3 cycle=%lu\n", name, init_cycle());

  gemmini_wait_managed_runtime_style(GEMMINI_CFG_ID);
  printf("CASE_TRACE %s post-wait cycle=%lu\n", name, init_cycle());
  spm_xlate_reset();
  rr_fence(GEMMINI_CFG_ID);
  rr_release(GEMMINI_CFG_ID);

  printf("CASE_RESULT %s PASS\n", name);
  return true;
}

static bool run_pointwise_os_inner_runtime_focus_case(const char *name,
                                                      int gemmini_manager_id,
                                                      const spm_alias_region_t *a_region,
                                                      const spm_alias_region_t *b_region,
                                                      const spm_alias_region_t *bias_region,
                                                      const spm_alias_region_t *c_region) {
  bool ok;
  const elem_t *a_req = (const elem_t *)(uintptr_t)a_region->vaddr;
  const elem_t *b_req = (const elem_t *)(uintptr_t)b_region->vaddr;
  const acc_t *bias_req = (const acc_t *)(uintptr_t)bias_region->vaddr;
  elem_t *c_req = (elem_t *)(uintptr_t)c_region->vaddr;
  const char *region_names[] = {"A", "B", "BIAS", "C"};
  const spm_alias_region_t *regions[] = {a_region, b_region, bias_region, c_region};
  const size_t inner_I = PW_I / DIM;
  const size_t inner_J = PW_FOCUS_OC_TILE / DIM;
  const size_t inner_K = PW_K / DIM;

  printf("CASE_START %s target=sp-tiled-matmul-os-single-inner stage=%u opcode=%u cfg=%u manager=%d I=%u J=%u K=%u inner_I=%lu inner_J=%lu inner_K=%lu page_bytes=%lu page_offset=%lu act=%d bias=1 repeating_bias=1 mode=runtime_style_manual_inner\n",
         name,
         GEMMINI_STAGE_ID, GEMMINI_OPCODE_ID, GEMMINI_CFG_ID, gemmini_manager_id,
         PW_I, PW_FOCUS_OC_TILE, PW_K,
         (unsigned long)inner_I, (unsigned long)inner_J, (unsigned long)inner_K,
         (unsigned long)REROCC_SPM_PAGE_BYTES,
         (unsigned long)PW_VADDR_PAGE_OFFSET,
         RELU);
  print_region_summary(name, "A", a_region, "interleaved");
  print_region_summary(name, "B", b_region, "interleaved");
  print_region_summary(name, "BIAS", bias_region, "interleaved");
  print_region_summary(name, "C", c_region, "interleaved");

  if (!validate_case_xlate_layout(name, region_names, regions,
                                  sizeof(regions) / sizeof(regions[0]))) {
    return false;
  }

  alias_region_write(a_region, (const uint8_t *)pw_a_dram, PW_BYTES_A);
  alias_region_write(b_region, (const uint8_t *)pw_b_dram, PW_BYTES_B);
  alias_region_write(bias_region, (const uint8_t *)pw_chunk_bias_dram, PW_CHUNK_BYTES_BIAS);
  alias_region_write(c_region, (const uint8_t *)pw_init_c_dram, PW_BYTES_C);
  memset(pw_out_shadow_dram, 0, sizeof(pw_out_shadow_dram));

  if (!compare_disabled()) {
    memcpy(pw_j128_gold_dram, pw_init_c_dram, sizeof(pw_j128_gold_dram));
    pointwise_matmul_reference_dim_j((const elem_t *)pw_a_dram,
                                     (const elem_t *)pw_b_dram,
                                     (const acc_t *)pw_chunk_bias_dram,
                                     (const elem_t *)pw_init_c_dram,
                                     (elem_t *)pw_j128_gold_dram,
                                     PW_FOCUS_OC_TILE);
  }

  if (!rr_acquire_cfg_with_retry(GEMMINI_CFG_ID, (uint64_t)gemmini_manager_id)) {
    printf("CASE_FAIL %s reason=acquire\n", name);
    return false;
  }

  rr_set_opc(GEMMINI_OPCODE_ID, GEMMINI_CFG_ID);
  spm_xlate_table_clear();
  spm_xlate_map_region(a_region);
  spm_xlate_map_region(b_region);
  spm_xlate_map_region(bias_region);
  spm_xlate_map_region(c_region);
  spm_xlate_program(true);

  printf("CASE_TRACE %s config-ex dataflow=%d act=%d A_stride=1 A_transpose=0 B_transpose=0 cycle=%lu\n",
         name, OUTPUT_STATIONARY, RELU & 3, init_cycle());
  gemmini_extended_config_ex(OUTPUT_STATIONARY, RELU & 3, 0, 1, false, false);
  printf("CASE_TRACE %s config-st stride_bytes=%lu act=%d scale=identity cycle=%lu\n",
         name, (unsigned long)(PW_C_STRIDE * sizeof(elem_t)), RELU & 3, init_cycle());
  gemmini_extended_config_st(PW_C_STRIDE * sizeof(elem_t), RELU & 3, ACC_SCALE_IDENTITY);
  printf("CASE_TRACE %s config-ld-a stride_bytes=%lu id=0 cycle=%lu\n",
         name, (unsigned long)(PW_A_STRIDE * sizeof(elem_t)), init_cycle());
  gemmini_extended3_config_ld(PW_A_STRIDE * sizeof(elem_t), MVIN_SCALE_IDENTITY, false, 0);
  printf("CASE_TRACE %s config-ld-b stride_bytes=%lu id=1 cycle=%lu\n",
         name, (unsigned long)(PW_B_STRIDE * sizeof(elem_t)), init_cycle());
  gemmini_extended3_config_ld(PW_B_STRIDE * sizeof(elem_t), MVIN_SCALE_IDENTITY, false, 1);
  printf("CASE_TRACE %s config-ld-d stride_bytes=0 id=2 runtime_skip_preflush=1 cycle=%lu\n",
         name, init_cycle());
  gemmini_extended3_config_ld(0, MVIN_SCALE_IDENTITY, false, 2);

  printf("CASE_TRACE %s pre-inner-call a=0x%lx b=0x%lx bias=0x%lx c=0x%lx cycle=%lu\n",
         name,
         (unsigned long)(uintptr_t)a_req,
         (unsigned long)(uintptr_t)(b_req + PW_FOCUS_OC_BEG),
         (unsigned long)(uintptr_t)(bias_req + PW_FOCUS_OC_BEG),
         (unsigned long)(uintptr_t)(c_req + PW_FOCUS_OC_BEG),
         init_cycle());
  printf("CASE_TRACE %s inner-shape I=%lu J=%lu K=%lu pad_I=0 pad_J=0 pad_K=0 stride_A=%u stride_B=%u stride_D=%u stride_C=%u act=%d repeat_bias=1 a_spad_id=0 b_spad_id=0 cycle=%lu\n",
         name,
         (unsigned long)inner_I, (unsigned long)inner_J, (unsigned long)inner_K,
         PW_A_STRIDE, PW_B_STRIDE, PW_C_STRIDE, PW_C_STRIDE,
         RELU, init_cycle());
  PRT_GEMMINI_RAW_LINE("[graw] focus-inner-pre-call");
  sp_tiled_matmul_os(a_req,
                     b_req + PW_FOCUS_OC_BEG,
                     bias_req ? (bias_req + PW_FOCUS_OC_BEG) : NULL,
                     c_req + PW_FOCUS_OC_BEG,
                     MVIN_SCALE_IDENTITY, MVIN_SCALE_IDENTITY, MVIN_SCALE_IDENTITY,
                     inner_I, inner_J, inner_K,
                     0, 0, 0,
                     PW_A_STRIDE, PW_B_STRIDE, PW_C_STRIDE, PW_C_STRIDE,
                     false, false,
                     false, false,
                     false, true,
                     RELU,
                     0, 0);
  PRT_GEMMINI_RAW_LINE("[graw] focus-inner-post-call");
  printf("CASE_TRACE %s post-inner-call cycle=%lu\n", name, init_cycle());

  gemmini_fence();
  printf("CASE_TRACE %s post-fence cycle=%lu\n", name, init_cycle());

  gemmini_wait_managed_runtime_style(GEMMINI_CFG_ID);
  printf("CASE_TRACE %s post-wait cycle=%lu\n", name, init_cycle());
  spm_xlate_reset();
  rr_fence(GEMMINI_CFG_ID);
  rr_release(GEMMINI_CFG_ID);

  alias_region_read(c_region, (uint8_t *)pw_out_shadow_dram, PW_BYTES_C);
  ok = finish_elem_case_result(name,
                               (const elem_t *)pw_j128_gold_dram,
                               (const elem_t *)pw_out_shadow_dram,
                               PW_ELEM_COUNT_C);
  return ok;
}

static bool run_pointwise_os_first_pair_runtime_focus_case(const char *name,
                                                           int gemmini_manager_id,
                                                           const spm_alias_region_t *a_region,
                                                           const spm_alias_region_t *b_region,
                                                           const spm_alias_region_t *bias_region,
                                                           const spm_alias_region_t *c_region) {
  const elem_t *a_req = (const elem_t *)(uintptr_t)a_region->vaddr;
  const elem_t *b_req = (const elem_t *)(uintptr_t)b_region->vaddr;
  const acc_t *bias_req = (const acc_t *)(uintptr_t)bias_region->vaddr;
  const char *region_names[] = {"A", "B", "BIAS", "C"};
  const spm_alias_region_t *regions[] = {a_region, b_region, bias_region, c_region};
  const size_t inner_I = PW_I / DIM;
  const size_t inner_J = PW_FOCUS_OC_TILE / DIM;
  const size_t inner_K = PW_K / DIM;
  const uint32_t A_sp_addr = 0;
  const uint32_t B_sp_addr_start = BANK_NUM * BANK_ROWS - inner_K * inner_J * DIM;
  const uint32_t B_sp_addr = B_sp_addr_start;
  const uint32_t D_sp_addr = 1U << (ADDR_LEN - 1);
  const uint32_t C_sp_addr = 3U << (ADDR_LEN - 2);
  const size_t A_cols = DIM;
  const size_t A_rows = DIM;
  const size_t B_cols = DIM;
  const size_t B_rows = DIM;
  const size_t C_cols = DIM;
  const size_t C_rows = DIM;
  const uint64_t bias_rs1 = (uint64_t)(uintptr_t)(bias_req + PW_FOCUS_OC_BEG);
  const uint64_t bias_rs2 = prt_gemmini_pack_mvin_rs2(D_sp_addr, C_cols, C_rows);
  const uint64_t b_rs1 = (uint64_t)(uintptr_t)(b_req + PW_FOCUS_OC_BEG);
  const uint64_t b_rs2 = prt_gemmini_pack_mvin_rs2(B_sp_addr, B_cols, B_rows);
  const uint64_t a_rs1 = (uint64_t)(uintptr_t)a_req;
  const uint64_t a_rs2 = prt_gemmini_pack_mvin_rs2(A_sp_addr, A_cols, A_rows);

  printf("CASE_START %s target=pointwise-os-first-pair stage=%u opcode=%u cfg=%u manager=%d I=%u J=%u K=%u inner_I=%lu inner_J=%lu inner_K=%lu page_bytes=%lu page_offset=%lu act=%d bias=1 repeating_bias=1 mode=runtime_style_first_pair_only\n",
         name,
         GEMMINI_STAGE_ID, GEMMINI_OPCODE_ID, GEMMINI_CFG_ID, gemmini_manager_id,
         PW_I, PW_FOCUS_OC_TILE, PW_K,
         (unsigned long)inner_I, (unsigned long)inner_J, (unsigned long)inner_K,
         (unsigned long)REROCC_SPM_PAGE_BYTES,
         (unsigned long)PW_VADDR_PAGE_OFFSET,
         RELU);
  print_region_summary(name, "A", a_region, "interleaved");
  print_region_summary(name, "B", b_region, "interleaved");
  print_region_summary(name, "BIAS", bias_region, "interleaved");
  print_region_summary(name, "C", c_region, "interleaved");

  if (!validate_case_xlate_layout(name, region_names, regions,
                                  sizeof(regions) / sizeof(regions[0]))) {
    return false;
  }

  alias_region_write(a_region, (const uint8_t *)pw_a_dram, PW_BYTES_A);
  alias_region_write(b_region, (const uint8_t *)pw_b_dram, PW_BYTES_B);
  alias_region_write(bias_region, (const uint8_t *)pw_chunk_bias_dram, PW_CHUNK_BYTES_BIAS);
  alias_region_write(c_region, (const uint8_t *)pw_init_c_dram, PW_BYTES_C);

  if (!rr_acquire_cfg_with_retry(GEMMINI_CFG_ID, (uint64_t)gemmini_manager_id)) {
    printf("CASE_FAIL %s reason=acquire\n", name);
    return false;
  }

  rr_set_opc(GEMMINI_OPCODE_ID, GEMMINI_CFG_ID);
  spm_xlate_table_clear();
  spm_xlate_map_region(a_region);
  spm_xlate_map_region(b_region);
  spm_xlate_map_region(bias_region);
  spm_xlate_map_region(c_region);
  spm_xlate_program(true);

  printf("CASE_TRACE %s config-ex dataflow=%d act=%d A_stride=1 A_transpose=0 B_transpose=0 cycle=%lu\n",
         name, OUTPUT_STATIONARY, RELU & 3, init_cycle());
  gemmini_extended_config_ex(OUTPUT_STATIONARY, RELU & 3, 0, 1, false, false);
  printf("CASE_TRACE %s config-st stride_bytes=%lu act=%d scale=identity cycle=%lu\n",
         name, (unsigned long)(PW_C_STRIDE * sizeof(elem_t)), RELU & 3, init_cycle());
  gemmini_extended_config_st(PW_C_STRIDE * sizeof(elem_t), RELU & 3, ACC_SCALE_IDENTITY);
  printf("CASE_TRACE %s config-ld-a stride_bytes=%lu id=0 cycle=%lu\n",
         name, (unsigned long)(PW_A_STRIDE * sizeof(elem_t)), init_cycle());
  gemmini_extended3_config_ld(PW_A_STRIDE * sizeof(elem_t), MVIN_SCALE_IDENTITY, false, 0);
  printf("CASE_TRACE %s config-ld-b stride_bytes=%lu id=1 cycle=%lu\n",
         name, (unsigned long)(PW_B_STRIDE * sizeof(elem_t)), init_cycle());
  gemmini_extended3_config_ld(PW_B_STRIDE * sizeof(elem_t), MVIN_SCALE_IDENTITY, false, 1);
  printf("CASE_TRACE %s config-ld-d stride_bytes=0 id=2 runtime_skip_preflush=1 cycle=%lu\n",
         name, init_cycle());
  gemmini_extended3_config_ld(0, MVIN_SCALE_IDENTITY, false, 2);

  printf("CASE_TRACE %s pre-bias-mvin3 dram=0x%lx sp=0x%x cols=%lu rows=%lu rs1=0x%lx rs2=0x%lx cycle=%lu\n",
         name, (unsigned long)bias_rs1, D_sp_addr,
         (unsigned long)C_cols, (unsigned long)C_rows,
         (unsigned long)bias_rs1, (unsigned long)bias_rs2, init_cycle());
  prt_gemmini_issue_bias_mvin3_debug(bias_req + PW_FOCUS_OC_BEG, D_sp_addr, C_cols, C_rows);
  printf("CASE_TRACE %s post-bias-mvin3 cycle=%lu\n", name, init_cycle());

  printf("CASE_TRACE %s pre-b-mvin2 dram=0x%lx sp=0x%x cols=%lu rows=%lu rs1=0x%lx rs2=0x%lx cycle=%lu\n",
         name, (unsigned long)b_rs1, B_sp_addr,
         (unsigned long)B_cols, (unsigned long)B_rows,
         (unsigned long)b_rs1, (unsigned long)b_rs2, init_cycle());
  gemmini_extended_mvin2(b_req + PW_FOCUS_OC_BEG, B_sp_addr, B_cols, B_rows);
  printf("CASE_TRACE %s post-b-mvin2 cycle=%lu\n", name, init_cycle());

  printf("CASE_TRACE %s pre-a-mvin0 dram=0x%lx sp=0x%x cols=%lu rows=%lu rs1=0x%lx rs2=0x%lx cycle=%lu\n",
         name, (unsigned long)a_rs1, A_sp_addr,
         (unsigned long)A_cols, (unsigned long)A_rows,
         (unsigned long)a_rs1, (unsigned long)a_rs2, init_cycle());
  gemmini_extended_mvin(a_req, A_sp_addr, A_cols, A_rows);
  printf("CASE_TRACE %s post-a-mvin0 cycle=%lu\n", name, init_cycle());

  printf("CASE_TRACE %s pre-preload0 bd=0x%x c=0x%x bd_cols=%lu bd_rows=%lu c_cols=%lu c_rows=%lu cycle=%lu\n",
         name, GARBAGE_ADDR, C_sp_addr,
         (unsigned long)DIM, (unsigned long)DIM,
         (unsigned long)C_cols, (unsigned long)C_rows,
         init_cycle());
  prt_gemmini_issue_preload_debug(GARBAGE_ADDR, C_sp_addr, DIM, DIM, C_cols, C_rows);
  printf("CASE_TRACE %s post-preload0 cycle=%lu\n", name, init_cycle());

  printf("CASE_TRACE %s pre-compute-preloaded0 a=0x%x b=0x%x a_cols=%lu a_rows=%lu b_cols=%lu b_rows=%lu cycle=%lu\n",
         name, A_sp_addr, B_sp_addr,
         (unsigned long)A_cols, (unsigned long)A_rows,
         (unsigned long)B_cols, (unsigned long)B_rows,
         init_cycle());
  prt_gemmini_issue_compute_preloaded_debug(A_sp_addr, B_sp_addr, A_cols, A_rows, B_cols, B_rows);
  printf("CASE_TRACE %s post-compute-preloaded0 cycle=%lu\n", name, init_cycle());

  gemmini_wait_managed_runtime_style(GEMMINI_CFG_ID);
  printf("CASE_TRACE %s post-wait cycle=%lu\n", name, init_cycle());
  spm_xlate_reset();
  rr_fence(GEMMINI_CFG_ID);
  rr_release(GEMMINI_CFG_ID);

  printf("CASE_RESULT %s PASS\n", name);
  return true;
}

static bool pointwise_matmul_issue_dim_j_explicit_tiles(const elem_t *a, const elem_t *b,
                                                        const acc_t *bias, elem_t *out,
                                                        size_t dim_j,
                                                        size_t tile_i, size_t tile_j,
                                                        size_t tile_k,
                                                        enum tiled_matmul_type_t tiled_type) {
  if (!a || !b || !out || dim_j == 0 || tile_i == 0 || tile_j == 0 || tile_k == 0) return false;
  tiled_matmul(PW_I, dim_j, PW_K,
               a, b, bias, out,
               PW_A_STRIDE, PW_B_STRIDE, PW_C_STRIDE, PW_C_STRIDE,
               MVIN_SCALE_IDENTITY, MVIN_SCALE_IDENTITY, MVIN_SCALE_IDENTITY,
               NO_ACTIVATION, ACC_SCALE_IDENTITY, 0, true,
               tile_i, tile_j, tile_k,
               false, false,
               false, false,
               0,
               tiled_type);
  return true;
}

static bool pointwise_matmul_issue_chunked(const elem_t *a, const elem_t *b, const acc_t *bias,
                                           elem_t *out, enum tiled_matmul_type_t tiled_type) {
  if (!a || !b || !out) return false;

  for (size_t oc_beg = 0; oc_beg < PW_CHUNK_TILE_J; oc_beg += PW_CHUNK_OC) {
    const size_t oc_tile =
      oc_beg + PW_CHUNK_OC <= PW_CHUNK_TILE_J ? PW_CHUNK_OC : (PW_CHUNK_TILE_J - oc_beg);
    tiled_matmul_nn_stride_auto(PW_I, oc_tile, PW_K,
                                PW_A_STRIDE, PW_B_STRIDE, PW_C_STRIDE,
                                a, b + oc_beg, bias ? (bias + oc_beg) : NULL, out + oc_beg,
                                NO_ACTIVATION, ACC_SCALE_IDENTITY, true,
                                tiled_type);
  }

  return true;
}

static bool pointwise_matmul_issue_chunked_runtime_style(const char *name,
                                                         uint32_t cfg_id,
                                                         const elem_t *a,
                                                         const elem_t *b,
                                                         const acc_t *bias,
                                                         elem_t *out,
                                                         enum tiled_matmul_type_t tiled_type) {
  if (!name || !a || !b || !out) return false;

  for (size_t oc_beg = 0; oc_beg < PW_CHUNK_TILE_J; oc_beg += PW_CHUNK_OC) {
    const size_t oc_tile =
      oc_beg + PW_CHUNK_OC <= PW_CHUNK_TILE_J ? PW_CHUNK_OC : (PW_CHUNK_TILE_J - oc_beg);
    printf("CASE_PROGRESS %s phase=chunk_issue_begin oc_beg=%lu oc_tile=%lu cycle=%lu\n",
           name, (unsigned long)oc_beg, (unsigned long)oc_tile, init_cycle());
    tiled_matmul_nn_stride_auto(PW_I, oc_tile, PW_K,
                                PW_A_STRIDE, PW_B_STRIDE, PW_C_STRIDE,
                                a, b + oc_beg, bias ? (bias + oc_beg) : NULL, out + oc_beg,
                                RELU, ACC_SCALE_IDENTITY, true,
                                tiled_type);
    printf("CASE_PROGRESS %s phase=chunk_issue_end oc_beg=%lu oc_tile=%lu cycle=%lu\n",
           name, (unsigned long)oc_beg, (unsigned long)oc_tile, init_cycle());
    if (oc_beg + PW_CHUNK_OC < PW_CHUNK_TILE_J) {
      gemmini_chunk_drain_runtime_style(cfg_id, name, oc_beg, oc_tile);
    }
  }

  return true;
}

static bool gemmini_runtime_style_group_fence_logged(const char *name,
                                                     const char *phase,
                                                     uint32_t cfg_id,
                                                     int gemmini_manager_id,
                                                     size_t group_idx,
                                                     size_t groups) {
  if (!name || !phase) return false;

  printf("CASE_PROGRESS %s phase=%s_acquire_begin group=%lu/%lu cfg=%u manager=%d cycle=%lu\n",
         name, phase,
         (unsigned long)(group_idx + 1U), (unsigned long)groups,
         cfg_id, gemmini_manager_id, init_cycle());
  if (!rr_acquire_cfg_with_retry(cfg_id, (uint64_t)gemmini_manager_id)) {
    printf("CASE_FAIL %s reason=%s_acquire group=%lu/%lu\n",
           name, phase,
           (unsigned long)(group_idx + 1U), (unsigned long)groups);
    return false;
  }
  rr_set_opc(GEMMINI_OPCODE_ID, cfg_id);
  printf("CASE_PROGRESS %s phase=%s_acquire_end group=%lu/%lu cfg=%u manager=%d cycle=%lu\n",
         name, phase,
         (unsigned long)(group_idx + 1U), (unsigned long)groups,
         cfg_id, gemmini_manager_id, init_cycle());

  printf("CASE_PROGRESS %s phase=%s_rr_fence0_begin group=%lu/%lu cycle=%lu\n",
         name, phase, (unsigned long)(group_idx + 1U), (unsigned long)groups, init_cycle());
  rr_fence(cfg_id);
  printf("CASE_PROGRESS %s phase=%s_rr_fence0_end group=%lu/%lu cycle=%lu\n",
         name, phase, (unsigned long)(group_idx + 1U), (unsigned long)groups, init_cycle());
  printf("CASE_PROGRESS %s phase=%s_cpu_fence0_begin group=%lu/%lu cycle=%lu\n",
         name, phase, (unsigned long)(group_idx + 1U), (unsigned long)groups, init_cycle());
  gemmini_fence();
  printf("CASE_PROGRESS %s phase=%s_cpu_fence0_end group=%lu/%lu cycle=%lu\n",
         name, phase, (unsigned long)(group_idx + 1U), (unsigned long)groups, init_cycle());
  printf("CASE_PROGRESS %s phase=%s_flush_begin group=%lu/%lu cycle=%lu\n",
         name, phase, (unsigned long)(group_idx + 1U), (unsigned long)groups, init_cycle());
  gemmini_flush(0);
  printf("CASE_PROGRESS %s phase=%s_flush_end group=%lu/%lu cycle=%lu\n",
         name, phase, (unsigned long)(group_idx + 1U), (unsigned long)groups, init_cycle());
  printf("CASE_PROGRESS %s phase=%s_rr_fence1_begin group=%lu/%lu cycle=%lu\n",
         name, phase, (unsigned long)(group_idx + 1U), (unsigned long)groups, init_cycle());
  rr_fence(cfg_id);
  printf("CASE_PROGRESS %s phase=%s_rr_fence1_end group=%lu/%lu cycle=%lu\n",
         name, phase, (unsigned long)(group_idx + 1U), (unsigned long)groups, init_cycle());
  printf("CASE_PROGRESS %s phase=%s_cpu_fence1_begin group=%lu/%lu cycle=%lu\n",
         name, phase, (unsigned long)(group_idx + 1U), (unsigned long)groups, init_cycle());
  gemmini_fence();
  printf("CASE_PROGRESS %s phase=%s_cpu_fence1_end group=%lu/%lu cycle=%lu\n",
         name, phase, (unsigned long)(group_idx + 1U), (unsigned long)groups, init_cycle());

  printf("CASE_PROGRESS %s phase=%s_release_begin group=%lu/%lu cycle=%lu\n",
         name, phase, (unsigned long)(group_idx + 1U), (unsigned long)groups, init_cycle());
  rr_release(cfg_id);
  printf("CASE_PROGRESS %s phase=%s_release_end group=%lu/%lu cycle=%lu\n",
         name, phase, (unsigned long)(group_idx + 1U), (unsigned long)groups, init_cycle());
  return true;
}

static bool pointwise_matmul_issue_grouped_runtime_style(const char *name,
                                                         uint32_t cfg_id,
                                                         int gemmini_manager_id,
                                                         const elem_t *a,
                                                         const elem_t *b,
                                                         const acc_t *bias,
                                                         elem_t *out,
                                                         enum tiled_matmul_type_t tiled_type) {
  if (!name || !a || !b || !out) return false;

  for (size_t group_idx = 0; group_idx < PW_GROUP_COUNT; ++group_idx) {
    const elem_t *group_a = a + group_idx * (size_t)PW_K;
    const elem_t *group_b = b + group_idx * (size_t)PW_K * (size_t)PW_B_STRIDE;
    const acc_t *group_bias = bias ? (bias + group_idx * (size_t)PW_J) : NULL;
    elem_t *group_out = out + group_idx * (size_t)PW_J;
    const size_t a_off = group_idx * (size_t)PW_K;
    const size_t b_off = group_idx * (size_t)PW_K * (size_t)PW_B_STRIDE;
    const size_t bias_off = group_idx * (size_t)PW_J;
    const size_t c_off = group_idx * (size_t)PW_J;

    printf("CASE_PROGRESS %s phase=group_begin group=%lu/%u cycle=%lu\n",
           name, (unsigned long)(group_idx + 1U), PW_GROUP_COUNT, init_cycle());
    printf("CASE_PROGRESS %s phase=group_issue_acquire_begin group=%lu/%u cfg=%u manager=%d cycle=%lu\n",
           name, (unsigned long)(group_idx + 1U), PW_GROUP_COUNT,
           cfg_id, gemmini_manager_id, init_cycle());
    if (!rr_acquire_cfg_with_retry(cfg_id, (uint64_t)gemmini_manager_id)) {
      printf("CASE_FAIL %s reason=group_issue_acquire group=%lu/%u\n",
             name, (unsigned long)(group_idx + 1U), PW_GROUP_COUNT);
      return false;
    }
    rr_set_opc(GEMMINI_OPCODE_ID, cfg_id);
    printf("CASE_PROGRESS %s phase=group_issue_acquire_end group=%lu/%u cfg=%u manager=%d cycle=%lu\n",
           name, (unsigned long)(group_idx + 1U), PW_GROUP_COUNT,
           cfg_id, gemmini_manager_id, init_cycle());
    printf("CASE_PROGRESS %s phase=subcall_begin group=%lu/%u a_off=%lu b_off=%lu bias_off=%lu c_off=%lu cycle=%lu\n",
           name, (unsigned long)(group_idx + 1U), PW_GROUP_COUNT,
           (unsigned long)a_off, (unsigned long)b_off,
           (unsigned long)bias_off, (unsigned long)c_off,
           init_cycle());
    if (!pointwise_matmul_issue_chunked_runtime_style(name, cfg_id,
                                                      group_a, group_b, group_bias, group_out,
                                                      tiled_type)) {
      printf("CASE_PROGRESS %s phase=group_issue_release_begin group=%lu/%u cycle=%lu\n",
             name, (unsigned long)(group_idx + 1U), PW_GROUP_COUNT, init_cycle());
      rr_release(cfg_id);
      printf("CASE_PROGRESS %s phase=group_issue_release_end group=%lu/%u cycle=%lu\n",
             name, (unsigned long)(group_idx + 1U), PW_GROUP_COUNT, init_cycle());
      printf("CASE_FAIL %s reason=group_issue group=%lu/%u\n",
             name, (unsigned long)(group_idx + 1U), PW_GROUP_COUNT);
      return false;
    }
    printf("CASE_PROGRESS %s phase=subcall_end group=%lu/%u cycle=%lu\n",
           name, (unsigned long)(group_idx + 1U), PW_GROUP_COUNT, init_cycle());
    printf("CASE_PROGRESS %s phase=group_issue_release_begin group=%lu/%u cycle=%lu\n",
           name, (unsigned long)(group_idx + 1U), PW_GROUP_COUNT, init_cycle());
    rr_release(cfg_id);
    printf("CASE_PROGRESS %s phase=group_issue_release_end group=%lu/%u cycle=%lu\n",
           name, (unsigned long)(group_idx + 1U), PW_GROUP_COUNT, init_cycle());
    printf("CASE_PROGRESS %s phase=group_end group=%lu/%u cycle=%lu\n",
           name, (unsigned long)(group_idx + 1U), PW_GROUP_COUNT, init_cycle());

    if (group_idx + 1U < PW_GROUP_COUNT) {
      if (!gemmini_runtime_style_group_fence_logged(name, "inter_group_fence",
                                                    cfg_id, gemmini_manager_id,
                                                    group_idx, PW_GROUP_COUNT)) {
        return false;
      }
    }
  }

  return true;
}

static bool run_pointwise_matmul_case_dim_j(const char *name, int gemmini_manager_id,
                                            const spm_alias_region_t *a_region,
                                            const spm_alias_region_t *b_region,
                                            const spm_alias_region_t *bias_region,
                                            const spm_alias_region_t *c_region,
                                            const acc_t *bias_src, size_t bias_bytes,
                                            const elem_t *expected, size_t dim_j,
                                            enum tiled_matmul_type_t tiled_type) {
  bool ok;
  const elem_t *a_req = (const elem_t *)(uintptr_t)a_region->vaddr;
  const elem_t *b_req = (const elem_t *)(uintptr_t)b_region->vaddr;
  const acc_t *bias_req = (const acc_t *)(uintptr_t)bias_region->vaddr;
  elem_t *c_req = (elem_t *)(uintptr_t)c_region->vaddr;
  const char *type_name = tiled_type == OS ? "OS" :
                          tiled_type == WS ? "WS" : "CPU";

  printf("CASE_START %s bytes_a=%lu bytes_b=%lu bytes_bias=%lu bytes_c=%lu page_bytes=%lu page_offset=%lu type=%s\n",
         name,
         (unsigned long)PW_BYTES_A,
         (unsigned long)PW_BYTES_B,
         (unsigned long)bias_bytes,
         (unsigned long)PW_BYTES_C,
         (unsigned long)REROCC_SPM_PAGE_BYTES,
         (unsigned long)PW_VADDR_PAGE_OFFSET,
         type_name);
  print_region(name, "A", a_region);
  print_region(name, "B", b_region);
  print_region(name, "BIAS", bias_region);
  print_region(name, "C", c_region);

  {
    const char *region_names[] = {"A", "B", "BIAS", "C"};
    const spm_alias_region_t *regions[] = {a_region, b_region, bias_region, c_region};
    if (!validate_case_xlate_layout(name, region_names, regions,
                                    sizeof(regions) / sizeof(regions[0]))) {
      return false;
    }
  }

  alias_region_write(a_region, (const uint8_t *)pw_a_dram, PW_BYTES_A);
  alias_region_write(b_region, (const uint8_t *)pw_b_dram, PW_BYTES_B);
  alias_region_write(bias_region, (const uint8_t *)bias_src, bias_bytes);
  alias_region_write(c_region, (const uint8_t *)pw_init_c_dram, PW_BYTES_C);
  memset(pw_out_shadow_dram, 0, sizeof(pw_out_shadow_dram));

  if (!rr_acquire_cfg_with_retry(GEMMINI_CFG_ID, (uint64_t)gemmini_manager_id)) {
    printf("CASE_FAIL %s reason=acquire\n", name);
    return false;
  }

  rr_set_opc(3, GEMMINI_CFG_ID);
  gemmini_flush(0);
  spm_xlate_table_clear();
  spm_xlate_map_region(a_region);
  spm_xlate_map_region(b_region);
  spm_xlate_map_region(bias_region);
  spm_xlate_map_region(c_region);
  spm_xlate_program(true);

  if (!pointwise_matmul_issue_dim_j(a_req, b_req, bias_req, c_req, dim_j, tiled_type)) {
    spm_xlate_reset();
    rr_fence(GEMMINI_CFG_ID);
    rr_release(GEMMINI_CFG_ID);
    printf("CASE_FAIL %s reason=issue\n", name);
    return false;
  }

  gemmini_wait_managed(GEMMINI_CFG_ID);
  spm_xlate_reset();
  rr_fence(GEMMINI_CFG_ID);
  rr_release(GEMMINI_CFG_ID);

  alias_region_read(c_region, (uint8_t *)pw_out_shadow_dram, PW_BYTES_C);
  ok = finish_elem_case_result(name,
                               expected,
                               (const elem_t *)pw_out_shadow_dram,
                               PW_ELEM_COUNT_C);
  return ok;
}

static bool run_pointwise_matmul_case(const char *name, int gemmini_manager_id,
                                      const spm_alias_region_t *a_region,
                                      const spm_alias_region_t *b_region,
                                      const spm_alias_region_t *bias_region,
                                      const spm_alias_region_t *c_region,
                                      enum tiled_matmul_type_t tiled_type) {
  return run_pointwise_matmul_case_dim_j(name, gemmini_manager_id,
                                         a_region, b_region, bias_region, c_region,
                                         (const acc_t *)pw_bias_dram, PW_BYTES_BIAS,
                                         (const elem_t *)pw_gold_dram, PW_J,
                                         tiled_type);
}

static bool run_pointwise_matmul_case_shape(const char *name, int gemmini_manager_id,
                                            const spm_alias_region_t *a_region,
                                            const spm_alias_region_t *b_region,
                                            const spm_alias_region_t *bias_region,
                                            const spm_alias_region_t *c_region,
                                            const elem_t *a_src, size_t a_bytes,
                                            const elem_t *b_src, size_t b_bytes,
                                            const acc_t *bias_src, size_t bias_bytes,
                                            const elem_t *c_init, size_t c_bytes,
                                            const elem_t *expected, size_t expected_elems,
                                            size_t dim_i, size_t dim_j, size_t dim_k,
                                            size_t a_stride, size_t b_stride, size_t c_stride,
                                            enum tiled_matmul_type_t tiled_type) {
  bool ok;
  const elem_t *a_req = (const elem_t *)(uintptr_t)a_region->vaddr;
  const elem_t *b_req = (const elem_t *)(uintptr_t)b_region->vaddr;
  const acc_t *bias_req = (const acc_t *)(uintptr_t)bias_region->vaddr;
  elem_t *c_req = (elem_t *)(uintptr_t)c_region->vaddr;
  const char *type_name = tiled_type == OS ? "OS" :
                          tiled_type == WS ? "WS" : "CPU";

  printf("CASE_START %s bytes_a=%lu bytes_b=%lu bytes_bias=%lu bytes_c=%lu page_bytes=%lu page_offset=%lu type=%s I=%lu J=%lu K=%lu a_stride=%lu b_stride=%lu c_stride=%lu\n",
         name,
         (unsigned long)a_bytes,
         (unsigned long)b_bytes,
         (unsigned long)bias_bytes,
         (unsigned long)c_bytes,
         (unsigned long)REROCC_SPM_PAGE_BYTES,
         (unsigned long)PW_VADDR_PAGE_OFFSET,
         type_name,
         (unsigned long)dim_i,
         (unsigned long)dim_j,
         (unsigned long)dim_k,
         (unsigned long)a_stride,
         (unsigned long)b_stride,
         (unsigned long)c_stride);
  print_region(name, "A", a_region);
  print_region(name, "B", b_region);
  print_region(name, "BIAS", bias_region);
  print_region(name, "C", c_region);

  {
    const char *region_names[] = {"A", "B", "BIAS", "C"};
    const spm_alias_region_t *regions[] = {a_region, b_region, bias_region, c_region};
    if (!validate_case_xlate_layout(name, region_names, regions,
                                    sizeof(regions) / sizeof(regions[0]))) {
      return false;
    }
  }

  alias_region_write(a_region, (const uint8_t *)a_src, a_bytes);
  alias_region_write(b_region, (const uint8_t *)b_src, b_bytes);
  alias_region_write(bias_region, (const uint8_t *)bias_src, bias_bytes);
  alias_region_write(c_region, (const uint8_t *)c_init, c_bytes);
  memset(pw_out_shadow_dram, 0, sizeof(pw_out_shadow_dram));

  if (!rr_acquire_cfg_with_retry(GEMMINI_CFG_ID, (uint64_t)gemmini_manager_id)) {
    printf("CASE_FAIL %s reason=acquire\n", name);
    return false;
  }

  rr_set_opc(3, GEMMINI_CFG_ID);
  gemmini_flush(0);
  spm_xlate_table_clear();
  spm_xlate_map_region(a_region);
  spm_xlate_map_region(b_region);
  spm_xlate_map_region(bias_region);
  spm_xlate_map_region(c_region);
  spm_xlate_program(true);

  if (!pointwise_matmul_issue_shape(a_req, b_req, bias_req, c_req,
                                    dim_i, dim_j, dim_k,
                                    a_stride, b_stride, c_stride,
                                    tiled_type)) {
    spm_xlate_reset();
    rr_fence(GEMMINI_CFG_ID);
    rr_release(GEMMINI_CFG_ID);
    printf("CASE_FAIL %s reason=issue\n", name);
    return false;
  }

  gemmini_wait_managed(GEMMINI_CFG_ID);
  spm_xlate_reset();
  rr_fence(GEMMINI_CFG_ID);
  rr_release(GEMMINI_CFG_ID);

  alias_region_read(c_region, (uint8_t *)pw_out_shadow_dram, c_bytes);
  ok = finish_elem_case_result(name,
                               expected,
                               (const elem_t *)pw_out_shadow_dram,
                               expected_elems);
  return ok;
}

static bool run_pointwise_matmul_case_dim_j_explicit_tiles(const char *name,
                                                           int gemmini_manager_id,
                                                           const spm_alias_region_t *a_region,
                                                           const spm_alias_region_t *b_region,
                                                           const spm_alias_region_t *bias_region,
                                                           const spm_alias_region_t *c_region,
                                                           const acc_t *bias_src,
                                                           size_t bias_bytes,
                                                           const elem_t *expected,
                                                           size_t dim_j,
                                                           size_t tile_i,
                                                           size_t tile_j,
                                                           size_t tile_k,
                                                           enum tiled_matmul_type_t tiled_type) {
  bool ok;
  const elem_t *a_req = (const elem_t *)(uintptr_t)a_region->vaddr;
  const elem_t *b_req = (const elem_t *)(uintptr_t)b_region->vaddr;
  const acc_t *bias_req = (const acc_t *)(uintptr_t)bias_region->vaddr;
  elem_t *c_req = (elem_t *)(uintptr_t)c_region->vaddr;
  const char *type_name = tiled_type == OS ? "OS" :
                          tiled_type == WS ? "WS" : "CPU";

  printf("CASE_START %s bytes_a=%lu bytes_b=%lu bytes_bias=%lu bytes_c=%lu page_bytes=%lu page_offset=%lu type=%s tile_i=%lu tile_j=%lu tile_k=%lu\n",
         name,
         (unsigned long)PW_BYTES_A,
         (unsigned long)PW_BYTES_B,
         (unsigned long)bias_bytes,
         (unsigned long)PW_BYTES_C,
         (unsigned long)REROCC_SPM_PAGE_BYTES,
         (unsigned long)PW_VADDR_PAGE_OFFSET,
         type_name,
         (unsigned long)tile_i,
         (unsigned long)tile_j,
         (unsigned long)tile_k);
  print_region(name, "A", a_region);
  print_region(name, "B", b_region);
  print_region(name, "BIAS", bias_region);
  print_region(name, "C", c_region);

  {
    const char *region_names[] = {"A", "B", "BIAS", "C"};
    const spm_alias_region_t *regions[] = {a_region, b_region, bias_region, c_region};
    if (!validate_case_xlate_layout(name, region_names, regions,
                                    sizeof(regions) / sizeof(regions[0]))) {
      return false;
    }
  }

  alias_region_write(a_region, (const uint8_t *)pw_a_dram, PW_BYTES_A);
  alias_region_write(b_region, (const uint8_t *)pw_b_dram, PW_BYTES_B);
  alias_region_write(bias_region, (const uint8_t *)bias_src, bias_bytes);
  alias_region_write(c_region, (const uint8_t *)pw_init_c_dram, PW_BYTES_C);
  memset(pw_out_shadow_dram, 0, sizeof(pw_out_shadow_dram));

  if (!rr_acquire_cfg_with_retry(GEMMINI_CFG_ID, (uint64_t)gemmini_manager_id)) {
    printf("CASE_FAIL %s reason=acquire\n", name);
    return false;
  }

  rr_set_opc(3, GEMMINI_CFG_ID);
  gemmini_flush(0);
  spm_xlate_table_clear();
  spm_xlate_map_region(a_region);
  spm_xlate_map_region(b_region);
  spm_xlate_map_region(bias_region);
  spm_xlate_map_region(c_region);
  spm_xlate_program(true);

  if (!pointwise_matmul_issue_dim_j_explicit_tiles(a_req, b_req, bias_req, c_req, dim_j,
                                                   tile_i, tile_j, tile_k, tiled_type)) {
    spm_xlate_reset();
    rr_fence(GEMMINI_CFG_ID);
    rr_release(GEMMINI_CFG_ID);
    printf("CASE_FAIL %s reason=issue\n", name);
    return false;
  }

  gemmini_wait_managed(GEMMINI_CFG_ID);
  spm_xlate_reset();
  rr_fence(GEMMINI_CFG_ID);
  rr_release(GEMMINI_CFG_ID);

  alias_region_read(c_region, (uint8_t *)pw_out_shadow_dram, PW_BYTES_C);
  ok = finish_elem_case_result(name,
                               expected,
                               (const elem_t *)pw_out_shadow_dram,
                               PW_ELEM_COUNT_C);
  return ok;
}

static bool run_pointwise_matmul_chunked_case(const char *name, int gemmini_manager_id,
                                              const spm_alias_region_t *a_region,
                                              const spm_alias_region_t *b_region,
                                              const spm_alias_region_t *bias_region,
                                              const spm_alias_region_t *c_region,
                                              enum tiled_matmul_type_t tiled_type) {
  bool ok;
  const elem_t *a_req = (const elem_t *)(uintptr_t)a_region->vaddr;
  const elem_t *b_req = (const elem_t *)(uintptr_t)b_region->vaddr;
  const acc_t *bias_req = (const acc_t *)(uintptr_t)bias_region->vaddr;
  elem_t *c_req = (elem_t *)(uintptr_t)c_region->vaddr;
  const char *type_name = tiled_type == OS ? "OS" :
                          tiled_type == WS ? "WS" : "CPU";

  printf("CASE_START %s bytes_a=%lu bytes_b=%lu bytes_bias=%lu bytes_c=%lu page_bytes=%lu page_offset=%lu type=%s tile_j=%u oc_chunk=%u\n",
         name,
         (unsigned long)PW_BYTES_A,
         (unsigned long)PW_BYTES_B,
         (unsigned long)PW_CHUNK_BYTES_BIAS,
         (unsigned long)PW_BYTES_C,
         (unsigned long)REROCC_SPM_PAGE_BYTES,
         (unsigned long)PW_VADDR_PAGE_OFFSET,
         type_name,
         (unsigned)PW_CHUNK_TILE_J,
         (unsigned)PW_CHUNK_OC);
  print_region(name, "A", a_region);
  print_region(name, "B", b_region);
  print_region(name, "BIAS", bias_region);
  print_region(name, "C", c_region);

  {
    const char *region_names[] = {"A", "B", "BIAS", "C"};
    const spm_alias_region_t *regions[] = {a_region, b_region, bias_region, c_region};
    if (!validate_case_xlate_layout(name, region_names, regions,
                                    sizeof(regions) / sizeof(regions[0]))) {
      return false;
    }
  }

  alias_region_write(a_region, (const uint8_t *)pw_a_dram, PW_BYTES_A);
  alias_region_write(b_region, (const uint8_t *)pw_b_dram, PW_BYTES_B);
  alias_region_write(bias_region, (const uint8_t *)pw_chunk_bias_dram, PW_CHUNK_BYTES_BIAS);
  alias_region_write(c_region, (const uint8_t *)pw_init_c_dram, PW_BYTES_C);
  memset(pw_out_shadow_dram, 0, sizeof(pw_out_shadow_dram));

  if (!rr_acquire_cfg_with_retry(GEMMINI_CFG_ID, (uint64_t)gemmini_manager_id)) {
    printf("CASE_FAIL %s reason=acquire\n", name);
    return false;
  }

  rr_set_opc(3, GEMMINI_CFG_ID);
  gemmini_flush(0);
  spm_xlate_table_clear();
  spm_xlate_map_region(a_region);
  spm_xlate_map_region(b_region);
  spm_xlate_map_region(bias_region);
  spm_xlate_map_region(c_region);
  spm_xlate_program(true);

  if (!pointwise_matmul_issue_chunked(a_req, b_req, bias_req, c_req, tiled_type)) {
    spm_xlate_reset();
    rr_fence(GEMMINI_CFG_ID);
    rr_release(GEMMINI_CFG_ID);
    printf("CASE_FAIL %s reason=issue\n", name);
    return false;
  }

  gemmini_wait_managed(GEMMINI_CFG_ID);
  spm_xlate_reset();
  rr_fence(GEMMINI_CFG_ID);
  rr_release(GEMMINI_CFG_ID);

  alias_region_read(c_region, (uint8_t *)pw_out_shadow_dram, PW_BYTES_C);
  ok = finish_elem_case_result(name,
                               (const elem_t *)pw_chunk_gold_dram,
                               (const elem_t *)pw_out_shadow_dram,
                               PW_ELEM_COUNT_C);
  return ok;
}

static bool run_pointwise_matmul_chunked_runtime_style_case(const char *name,
                                                            int gemmini_manager_id,
                                                            const spm_alias_region_t *a_region,
                                                            const spm_alias_region_t *b_region,
                                                            const spm_alias_region_t *bias_region,
                                                            const spm_alias_region_t *c_region,
                                                            enum tiled_matmul_type_t tiled_type) {
  bool ok;
  const elem_t *a_req = (const elem_t *)(uintptr_t)a_region->vaddr;
  const elem_t *b_req = (const elem_t *)(uintptr_t)b_region->vaddr;
  const acc_t *bias_req = (const acc_t *)(uintptr_t)bias_region->vaddr;
  elem_t *c_req = (elem_t *)(uintptr_t)c_region->vaddr;
  const char *type_name = tiled_type == OS ? "OS" :
                          tiled_type == WS ? "WS" : "CPU";

  printf("CASE_START %s bytes_a=%lu bytes_b=%lu bytes_bias=%lu bytes_c=%lu page_bytes=%lu page_offset=%lu type=%s tile_j=%u oc_chunk=%u mode=runtime_style_drain act=%d\n",
         name,
         (unsigned long)PW_BYTES_A,
         (unsigned long)PW_BYTES_B,
         (unsigned long)PW_CHUNK_BYTES_BIAS,
         (unsigned long)PW_BYTES_C,
         (unsigned long)REROCC_SPM_PAGE_BYTES,
         (unsigned long)PW_VADDR_PAGE_OFFSET,
         type_name,
         (unsigned)PW_CHUNK_TILE_J,
         (unsigned)PW_CHUNK_OC,
         RELU);
  print_region(name, "A", a_region);
  print_region(name, "B", b_region);
  print_region(name, "BIAS", bias_region);
  print_region(name, "C", c_region);

  {
    const char *region_names[] = {"A", "B", "BIAS", "C"};
    const spm_alias_region_t *regions[] = {a_region, b_region, bias_region, c_region};
    if (!validate_case_xlate_layout(name, region_names, regions,
                                    sizeof(regions) / sizeof(regions[0]))) {
      return false;
    }
  }

  alias_region_write(a_region, (const uint8_t *)pw_a_dram, PW_BYTES_A);
  alias_region_write(b_region, (const uint8_t *)pw_b_dram, PW_BYTES_B);
  alias_region_write(bias_region, (const uint8_t *)pw_chunk_bias_dram, PW_CHUNK_BYTES_BIAS);
  alias_region_write(c_region, (const uint8_t *)pw_init_c_dram, PW_BYTES_C);
  memset(pw_out_shadow_dram, 0, sizeof(pw_out_shadow_dram));

  if (!rr_acquire_cfg_with_retry(GEMMINI_CFG_ID, (uint64_t)gemmini_manager_id)) {
    printf("CASE_FAIL %s reason=acquire\n", name);
    return false;
  }

  rr_set_opc(3, GEMMINI_CFG_ID);
  spm_xlate_table_clear();
  spm_xlate_map_region(a_region);
  spm_xlate_map_region(b_region);
  spm_xlate_map_region(bias_region);
  spm_xlate_map_region(c_region);
  spm_xlate_program(true);
  printf("CASE_TRACE %s runtime_style_skip_preflush=1 act=%d cycle=%lu\n",
         name, RELU, init_cycle());

  if (!pointwise_matmul_issue_chunked_runtime_style(name, GEMMINI_CFG_ID,
                                                    a_req, b_req, bias_req, c_req,
                                                    tiled_type)) {
    spm_xlate_reset();
    rr_fence(GEMMINI_CFG_ID);
    rr_release(GEMMINI_CFG_ID);
    printf("CASE_FAIL %s reason=issue\n", name);
    return false;
  }

  gemmini_wait_managed_runtime_style_logged(name, GEMMINI_CFG_ID);
  spm_xlate_reset();
  rr_fence(GEMMINI_CFG_ID);
  rr_release(GEMMINI_CFG_ID);

  alias_region_read(c_region, (uint8_t *)pw_out_shadow_dram, PW_BYTES_C);
  ok = finish_elem_case_result(name,
                               (const elem_t *)pw_chunk_gold_dram,
                               (const elem_t *)pw_out_shadow_dram,
                               PW_ELEM_COUNT_C);
  return ok;
}

static bool run_pointwise_matmul_runtime_style_focus_case(const char *name,
                                                          int gemmini_manager_id,
                                                          const spm_alias_region_t *a_region,
                                                          const spm_alias_region_t *b_region,
                                                          const spm_alias_region_t *bias_region,
                                                          const spm_alias_region_t *c_region) {
  bool ok;
  const elem_t *a_req = (const elem_t *)(uintptr_t)a_region->vaddr;
  const elem_t *b_req = (const elem_t *)(uintptr_t)b_region->vaddr;
  const acc_t *bias_req = (const acc_t *)(uintptr_t)bias_region->vaddr;
  elem_t *c_req = (elem_t *)(uintptr_t)c_region->vaddr;
  const char *region_names[] = {"A", "B", "BIAS", "C"};
  const spm_alias_region_t *regions[] = {a_region, b_region, bias_region, c_region};

  printf("CASE_START %s target=%s deepest=matmul-os-biascfg-state stage=%u opcode=%u cfg=%u manager=%d I=%u J=%u K=%u focus_oc_beg=%u focus_oc_tile=%u page_bytes=%lu page_offset=%lu xlate_base=0x%lx vpage_a=%u vpage_b=%u vpage_bias=%u vpage_c=%u fallback=OS act=%d bias=1 mode=runtime_style_single_chunk\n",
         name,
         REROCC_FOCUSED_POINTWISE_TARGET,
         GEMMINI_STAGE_ID, GEMMINI_OPCODE_ID, GEMMINI_CFG_ID, gemmini_manager_id,
         PW_I, PW_CHUNK_TILE_J, PW_K,
         PW_FOCUS_OC_BEG, PW_FOCUS_OC_TILE,
         (unsigned long)REROCC_SPM_PAGE_BYTES,
         (unsigned long)PW_VADDR_PAGE_OFFSET,
         (unsigned long)SHARED_SPAD_XLATE_RANGE_BASE,
         PW_A_VPAGE, PW_B_VPAGE, PW_CHUNK_BIAS_VPAGE, PW_C_VPAGE,
         RELU);
  print_region_summary(name, "A", a_region, focused_layout_name());
  print_region_summary(name, "B", b_region, focused_layout_name());
  print_region_summary(name, "BIAS", bias_region, focused_layout_name());
  print_region_summary(name, "C", c_region, focused_layout_name());

  if (!validate_case_xlate_layout(name, region_names, regions,
                                  sizeof(regions) / sizeof(regions[0]))) {
    return false;
  }

  alias_region_write(a_region, (const uint8_t *)pw_a_dram, PW_BYTES_A);
  alias_region_write(b_region, (const uint8_t *)pw_b_dram, PW_BYTES_B);
  alias_region_write(bias_region, (const uint8_t *)pw_chunk_bias_dram, PW_CHUNK_BYTES_BIAS);
  alias_region_write(c_region, (const uint8_t *)pw_init_c_dram, PW_BYTES_C);
  memset(pw_out_shadow_dram, 0, sizeof(pw_out_shadow_dram));

  if (!compare_disabled()) {
    memcpy(pw_j128_gold_dram, pw_init_c_dram, sizeof(pw_j128_gold_dram));
    pointwise_matmul_reference_dim_j((const elem_t *)pw_a_dram,
                                     (const elem_t *)pw_b_dram,
                                     (const acc_t *)pw_chunk_bias_dram,
                                     (const elem_t *)pw_init_c_dram,
                                     (elem_t *)pw_j128_gold_dram,
                                     PW_FOCUS_OC_TILE);
  }

  if (!rr_acquire_cfg_with_retry(GEMMINI_CFG_ID, (uint64_t)gemmini_manager_id)) {
    printf("CASE_FAIL %s reason=acquire\n", name);
    return false;
  }

  case_phase_log(name, "acquire_end");
  rr_set_opc(GEMMINI_OPCODE_ID, GEMMINI_CFG_ID);
  case_phase_log(name, "xlate_map_begin");
  spm_xlate_table_clear();
  spm_xlate_map_region(a_region);
  spm_xlate_map_region(b_region);
  spm_xlate_map_region(bias_region);
  spm_xlate_map_region(c_region);
  case_phase_log(name, "xlate_map_end");
  case_phase_log(name, "xlate_prog_begin");
  spm_xlate_program(true);
  case_phase_log(name, "xlate_prog_end");
  printf("CASE_TRACE %s runtime_style_skip_preflush=1 stage=%u opcode=%u cfg=%u cycle=%lu\n",
         name, GEMMINI_STAGE_ID, GEMMINI_OPCODE_ID, GEMMINI_CFG_ID, init_cycle());
  printf("CASE_PROGRESS %s phase=focused_chunk_issue_begin oc_beg=%u oc_tile=%u cycle=%lu\n",
         name, PW_FOCUS_OC_BEG, PW_FOCUS_OC_TILE, init_cycle());
  tiled_matmul_nn_stride_auto(PW_I, PW_FOCUS_OC_TILE, PW_K,
                              PW_A_STRIDE, PW_B_STRIDE, PW_C_STRIDE,
                              a_req,
                              b_req + PW_FOCUS_OC_BEG,
                              bias_req ? (bias_req + PW_FOCUS_OC_BEG) : NULL,
                              c_req + PW_FOCUS_OC_BEG,
                              RELU, ACC_SCALE_IDENTITY, true, OS);
  printf("CASE_PROGRESS %s phase=focused_chunk_issue_end oc_beg=%u oc_tile=%u cycle=%lu\n",
         name, PW_FOCUS_OC_BEG, PW_FOCUS_OC_TILE, init_cycle());

  case_phase_log(name, "focused_wait_begin");
  gemmini_wait_managed_runtime_style_logged(name, GEMMINI_CFG_ID);
  case_phase_log(name, "focused_wait_end");
  case_phase_log(name, "xlate_reset_begin");
  spm_xlate_reset();
  case_phase_log(name, "xlate_reset_end");
  case_phase_log(name, "rr_fence_begin");
  rr_fence(GEMMINI_CFG_ID);
  case_phase_log(name, "rr_fence_end");
  case_phase_log(name, "rr_release_begin");
  rr_release(GEMMINI_CFG_ID);
  case_phase_log(name, "rr_release_end");

  case_phase_log(name, "shadow_read_begin");
  alias_region_read(c_region, (uint8_t *)pw_out_shadow_dram, PW_BYTES_C);
  case_phase_log(name, "shadow_read_end");
  ok = finish_elem_case_result(name,
                               (const elem_t *)pw_j128_gold_dram,
                               (const elem_t *)pw_out_shadow_dram,
                               PW_ELEM_COUNT_C);
  return ok;
}

static bool run_pointwise_matmul_runtime_style_explicit_tiles_focus_case(
    const char *name,
    int gemmini_manager_id,
    const spm_alias_region_t *a_region,
    const spm_alias_region_t *b_region,
    const spm_alias_region_t *bias_region,
    const spm_alias_region_t *c_region) {
  bool ok;
  const elem_t *a_req = (const elem_t *)(uintptr_t)a_region->vaddr;
  const elem_t *b_req = (const elem_t *)(uintptr_t)b_region->vaddr;
  const acc_t *bias_req = (const acc_t *)(uintptr_t)bias_region->vaddr;
  elem_t *c_req = (elem_t *)(uintptr_t)c_region->vaddr;
  const char *region_names[] = {"A", "B", "BIAS", "C"};
  const spm_alias_region_t *regions[] = {a_region, b_region, bias_region, c_region};

  printf("CASE_START %s target=%s stage=%u opcode=%u cfg=%u manager=%d I=%u J=%u K=%u focus_oc_beg=%u focus_oc_tile=%u tile_i=%u tile_j=%u tile_k=%u page_bytes=%lu page_offset=%lu xlate_base=0x%lx vpage_a=%u vpage_b=%u vpage_bias=%u vpage_c=%u fallback=OS act=%d bias=1 mode=runtime_style_explicit_tiles\n",
         name,
         REROCC_FOCUSED_POINTWISE_EXPLICIT_TARGET,
         GEMMINI_STAGE_ID, GEMMINI_OPCODE_ID, GEMMINI_CFG_ID, gemmini_manager_id,
         PW_I, PW_CHUNK_TILE_J, PW_K,
         PW_FOCUS_OC_BEG, PW_FOCUS_OC_TILE,
         PW_LINUX_TILE_I, PW_LINUX_TILE_J, PW_LINUX_TILE_K,
         (unsigned long)REROCC_SPM_PAGE_BYTES,
         (unsigned long)PW_VADDR_PAGE_OFFSET,
         (unsigned long)SHARED_SPAD_XLATE_RANGE_BASE,
         PW_A_VPAGE, PW_B_VPAGE, PW_CHUNK_BIAS_VPAGE, PW_C_VPAGE,
         RELU);
  print_region_summary(name, "A", a_region, focused_layout_name());
  print_region_summary(name, "B", b_region, focused_layout_name());
  print_region_summary(name, "BIAS", bias_region, focused_layout_name());
  print_region_summary(name, "C", c_region, focused_layout_name());

  if (!validate_case_xlate_layout(name, region_names, regions,
                                  sizeof(regions) / sizeof(regions[0]))) {
    return false;
  }

  alias_region_write(a_region, (const uint8_t *)pw_a_dram, PW_BYTES_A);
  alias_region_write(b_region, (const uint8_t *)pw_b_dram, PW_BYTES_B);
  alias_region_write(bias_region, (const uint8_t *)pw_chunk_bias_dram, PW_CHUNK_BYTES_BIAS);
  alias_region_write(c_region, (const uint8_t *)pw_init_c_dram, PW_BYTES_C);
  memset(pw_out_shadow_dram, 0, sizeof(pw_out_shadow_dram));

  if (!compare_disabled()) {
    memcpy(pw_j128_gold_dram, pw_init_c_dram, sizeof(pw_j128_gold_dram));
    pointwise_matmul_reference_dim_j((const elem_t *)pw_a_dram,
                                     (const elem_t *)pw_b_dram,
                                     (const acc_t *)pw_chunk_bias_dram,
                                     (const elem_t *)pw_init_c_dram,
                                     (elem_t *)pw_j128_gold_dram,
                                     PW_FOCUS_OC_TILE);
  }

  if (!rr_acquire_cfg_with_retry(GEMMINI_CFG_ID, (uint64_t)gemmini_manager_id)) {
    printf("CASE_FAIL %s reason=acquire\n", name);
    return false;
  }

  case_phase_log(name, "acquire_end");
  rr_set_opc(GEMMINI_OPCODE_ID, GEMMINI_CFG_ID);
  case_phase_log(name, "xlate_map_begin");
  spm_xlate_table_clear();
  spm_xlate_map_region(a_region);
  spm_xlate_map_region(b_region);
  spm_xlate_map_region(bias_region);
  spm_xlate_map_region(c_region);
  case_phase_log(name, "xlate_map_end");
  case_phase_log(name, "xlate_prog_begin");
  spm_xlate_program(true);
  case_phase_log(name, "xlate_prog_end");
  printf("CASE_TRACE %s runtime_style_skip_preflush=1 stage=%u opcode=%u cfg=%u cycle=%lu\n",
         name, GEMMINI_STAGE_ID, GEMMINI_OPCODE_ID, GEMMINI_CFG_ID, init_cycle());
  printf("CASE_PROGRESS %s phase=focused_explicit_issue_begin oc_beg=%u oc_tile=%u tile_i=%u tile_j=%u tile_k=%u cycle=%lu\n",
         name, PW_FOCUS_OC_BEG, PW_FOCUS_OC_TILE,
         PW_LINUX_TILE_I, PW_LINUX_TILE_J, PW_LINUX_TILE_K,
         init_cycle());
  tiled_matmul(PW_I, PW_FOCUS_OC_TILE, PW_K,
               a_req,
               b_req + PW_FOCUS_OC_BEG,
               bias_req ? (bias_req + PW_FOCUS_OC_BEG) : NULL,
               c_req + PW_FOCUS_OC_BEG,
               PW_A_STRIDE, PW_B_STRIDE, PW_C_STRIDE, PW_C_STRIDE,
               MVIN_SCALE_IDENTITY, MVIN_SCALE_IDENTITY, MVIN_SCALE_IDENTITY,
               RELU, ACC_SCALE_IDENTITY, 0, true,
               PW_LINUX_TILE_I, PW_LINUX_TILE_J, PW_LINUX_TILE_K,
               false, false,
               false, false,
               0,
               OS);
  printf("CASE_PROGRESS %s phase=focused_explicit_issue_end oc_beg=%u oc_tile=%u tile_i=%u tile_j=%u tile_k=%u cycle=%lu\n",
         name, PW_FOCUS_OC_BEG, PW_FOCUS_OC_TILE,
         PW_LINUX_TILE_I, PW_LINUX_TILE_J, PW_LINUX_TILE_K,
         init_cycle());

  case_phase_log(name, "focused_wait_begin");
  gemmini_wait_managed_runtime_style_logged(name, GEMMINI_CFG_ID);
  case_phase_log(name, "focused_wait_end");
  case_phase_log(name, "xlate_reset_begin");
  spm_xlate_reset();
  case_phase_log(name, "xlate_reset_end");
  case_phase_log(name, "rr_fence_begin");
  rr_fence(GEMMINI_CFG_ID);
  case_phase_log(name, "rr_fence_end");
  case_phase_log(name, "rr_release_begin");
  rr_release(GEMMINI_CFG_ID);
  case_phase_log(name, "rr_release_end");

  case_phase_log(name, "shadow_read_begin");
  alias_region_read(c_region, (uint8_t *)pw_out_shadow_dram, PW_BYTES_C);
  case_phase_log(name, "shadow_read_end");
  ok = finish_elem_case_result(name,
                               (const elem_t *)pw_j128_gold_dram,
                               (const elem_t *)pw_out_shadow_dram,
                               PW_ELEM_COUNT_C);
  return ok;
}

static bool run_pointwise_matmul_runtime_style_grouped_focus_case(
    const char *name,
    int gemmini_manager_id,
    const spm_alias_region_t *a_region,
    const spm_alias_region_t *b_region,
    const spm_alias_region_t *bias_region,
    const spm_alias_region_t *c_region) {
  bool ok;
  const elem_t *a_req = (const elem_t *)(uintptr_t)a_region->vaddr;
  const elem_t *b_req = (const elem_t *)(uintptr_t)b_region->vaddr;
  const acc_t *bias_req = (const acc_t *)(uintptr_t)bias_region->vaddr;
  elem_t *c_req = (elem_t *)(uintptr_t)c_region->vaddr;
  const char *region_names[] = {"A", "B", "BIAS", "C"};
  const spm_alias_region_t *regions[] = {a_region, b_region, bias_region, c_region};

  printf("CASE_START %s target=%s stage=%u opcode=%u cfg=%u manager=%d I=%u J=%u K=%u groups=%u page_bytes=%lu page_offset=%lu xlate_base=0x%lx vpage_a=%u vpage_b=%u vpage_bias=%u vpage_c=%u fallback=OS act=%d bias=1 mode=runtime_style_grouped_full\n",
         name,
#ifndef REROCC_FOCUSED_POINTWISE_GROUPED_TARGET
#define REROCC_FOCUSED_POINTWISE_GROUPED_TARGET "linux-seg3-stage0-full-grouped-pointwise"
#endif
         REROCC_FOCUSED_POINTWISE_GROUPED_TARGET,
         GEMMINI_STAGE_ID, GEMMINI_OPCODE_ID, GEMMINI_CFG_ID, gemmini_manager_id,
         PW_I, PW_J, PW_K, PW_GROUP_COUNT,
         (unsigned long)REROCC_SPM_PAGE_BYTES,
         (unsigned long)PW_VADDR_PAGE_OFFSET,
         (unsigned long)SHARED_SPAD_XLATE_RANGE_BASE,
         PW_A_VPAGE, PW_B_VPAGE, PW_BIAS_VPAGE, PW_C_VPAGE,
         RELU);
  print_region_summary(name, "A", a_region, focused_layout_name());
  print_region_summary(name, "B", b_region, focused_layout_name());
  print_region_summary(name, "BIAS", bias_region, focused_layout_name());
  print_region_summary(name, "C", c_region, focused_layout_name());

  if (!validate_case_xlate_layout(name, region_names, regions,
                                  sizeof(regions) / sizeof(regions[0]))) {
    return false;
  }

  alias_region_write(a_region, (const uint8_t *)pw_a_dram, PW_BYTES_A);
  alias_region_write(b_region, (const uint8_t *)pw_group_b_dram, PW_GROUP_BYTES_B);
  alias_region_write(bias_region, (const uint8_t *)pw_group_bias_dram, PW_GROUP_BYTES_BIAS);
  alias_region_write(c_region, (const uint8_t *)pw_init_c_dram, PW_BYTES_C);
  memset(pw_out_shadow_dram, 0, sizeof(pw_out_shadow_dram));

  if (!compare_disabled()) {
    pointwise_matmul_reference_grouped_runtime_style((const elem_t *)pw_a_dram,
                                                     (const elem_t *)pw_group_b_dram,
                                                     (const acc_t *)pw_group_bias_dram,
                                                     (const elem_t *)pw_init_c_dram,
                                                     (elem_t *)pw_group_gold_dram);
  }

  case_phase_log(name, "xlate_map_begin");
  spm_xlate_table_clear();
  spm_xlate_map_region(a_region);
  spm_xlate_map_region(b_region);
  spm_xlate_map_region(bias_region);
  spm_xlate_map_region(c_region);
  case_phase_log(name, "xlate_map_end");
  case_phase_log(name, "xlate_prog_acquire_begin");
  if (!rr_acquire_cfg_with_retry(GEMMINI_CFG_ID, (uint64_t)gemmini_manager_id)) {
    printf("CASE_FAIL %s reason=xlate_prog_acquire\n", name);
    return false;
  }
  rr_set_opc(GEMMINI_OPCODE_ID, GEMMINI_CFG_ID);
  case_phase_log(name, "xlate_prog_acquire_end");
  case_phase_log(name, "xlate_prog_begin");
  spm_xlate_program(true);
  case_phase_log(name, "xlate_prog_end");
  case_phase_log(name, "xlate_prog_release_begin");
  rr_release(GEMMINI_CFG_ID);
  case_phase_log(name, "xlate_prog_release_end");
  printf("CASE_TRACE %s runtime_style_skip_preflush=1 groups=%u stage=%u opcode=%u cfg=%u cycle=%lu\n",
         name, PW_GROUP_COUNT, GEMMINI_STAGE_ID, GEMMINI_OPCODE_ID, GEMMINI_CFG_ID, init_cycle());

  if (!pointwise_matmul_issue_grouped_runtime_style(name, GEMMINI_CFG_ID,
                                                    gemmini_manager_id,
                                                    a_req, b_req, bias_req, c_req,
                                                    OS)) {
    spm_xlate_reset();
    rr_release_all(RR_MAX_CFGS);
    return false;
  }

  if (!gemmini_runtime_style_group_fence_logged(name, "final_fence",
                                                GEMMINI_CFG_ID, gemmini_manager_id,
                                                PW_GROUP_COUNT - 1U, PW_GROUP_COUNT)) {
    spm_xlate_reset();
    rr_release_all(RR_MAX_CFGS);
    return false;
  }

  case_phase_log(name, "xlate_reset_begin");
  if (!rr_acquire_cfg_with_retry(GEMMINI_CFG_ID, (uint64_t)gemmini_manager_id)) {
    printf("CASE_FAIL %s reason=xlate_reset_acquire\n", name);
    rr_release_all(RR_MAX_CFGS);
    return false;
  }
  rr_set_opc(GEMMINI_OPCODE_ID, GEMMINI_CFG_ID);
  spm_xlate_reset();
  rr_release(GEMMINI_CFG_ID);
  case_phase_log(name, "xlate_reset_end");

  case_phase_log(name, "shadow_read_begin");
  alias_region_read(c_region, (uint8_t *)pw_out_shadow_dram, PW_BYTES_C);
  case_phase_log(name, "shadow_read_end");
  ok = finish_elem_case_result(name,
                               (const elem_t *)pw_group_gold_dram,
                               (const elem_t *)pw_out_shadow_dram,
                               PW_ELEM_COUNT_C);
  return ok;
}

static bool run_copy_case(const char *name, int gemmini_manager_id,
                          const spm_alias_region_t *src_region,
                          const spm_alias_region_t *dst_region,
                          const elem_t *expected,
                          bool use_mvin2,
                          uint32_t acc_row_offset) {
  bool ok;
  const elem_t *src_req = (const elem_t *)(uintptr_t)src_region->vaddr;
  elem_t *dst_req = (elem_t *)(uintptr_t)dst_region->vaddr;

  printf("CASE_START %s bytes=%lu page_bytes=%lu page_offset=%lu mode=%s\n",
         name,
         (unsigned long)RESADD_BYTES,
         (unsigned long)REROCC_SPM_PAGE_BYTES,
         (unsigned long)RESADD_VADDR_PAGE_OFFSET,
         use_mvin2 ? "mvin2" : "mvin");
  print_region(name, "SRC", src_region);
  print_region(name, "DST", dst_region);

  alias_region_write(src_region, (const uint8_t *)expected, RESADD_BYTES);
  alias_region_zero(dst_region, RESADD_BYTES);
  memset(resadd_out_shadow_dram, 0, sizeof(resadd_out_shadow_dram));

  if (!rr_acquire_cfg_with_retry(GEMMINI_CFG_ID, (uint64_t)gemmini_manager_id)) {
    printf("CASE_FAIL %s reason=acquire\n", name);
    return false;
  }

  rr_set_opc(3, GEMMINI_CFG_ID);
  gemmini_flush(0);
  spm_xlate_table_clear();
  spm_xlate_map_region(src_region);
  spm_xlate_map_region(dst_region);
  spm_xlate_program(true);

  if (!copy_explicit_issue_no_fence(src_req, dst_req,
                                    RESADD_I, RESADD_J, RESADD_STRIDE,
                                    use_mvin2, acc_row_offset)) {
    spm_xlate_reset();
    rr_fence(GEMMINI_CFG_ID);
    rr_release(GEMMINI_CFG_ID);
    printf("CASE_FAIL %s reason=issue\n", name);
    return false;
  }

  gemmini_wait_managed(GEMMINI_CFG_ID);
  spm_xlate_reset();
  rr_fence(GEMMINI_CFG_ID);
  rr_release(GEMMINI_CFG_ID);

  alias_region_read(dst_region, (uint8_t *)resadd_out_shadow_dram, RESADD_BYTES);
  ok = finish_elem_case_result(name,
                               expected,
                               (const elem_t *)resadd_out_shadow_dram,
                               RESADD_ELEM_COUNT);
  return ok;
}

static bool run_resadd_case(const char *name, int gemmini_manager_id,
                            const spm_alias_region_t *a_region,
                            const spm_alias_region_t *b_region,
                            const spm_alias_region_t *c_region,
                            resadd_mode_t mode) {
  bool ok;
  const elem_t *a_req = (const elem_t *)(uintptr_t)a_region->vaddr;
  const elem_t *b_req = (const elem_t *)(uintptr_t)b_region->vaddr;
  elem_t *c_req = (elem_t *)(uintptr_t)c_region->vaddr;
  const char *mode_name = "explicit";

  switch (mode) {
    case RESADD_MODE_EXPLICIT_NO_FENCE:
      mode_name = "explicit";
      break;
    case RESADD_MODE_EXPLICIT_SERIALIZED:
      mode_name = "explicit_serialized";
      break;
    case RESADD_MODE_STANDARD_WS:
      mode_name = "standard_ws";
      break;
    default:
      mode_name = "unknown";
      break;
  }

  printf("CASE_START %s bytes=%lu page_bytes=%lu page_offset=%lu mode=%s\n",
         name,
         (unsigned long)RESADD_BYTES,
         (unsigned long)REROCC_SPM_PAGE_BYTES,
         (unsigned long)RESADD_VADDR_PAGE_OFFSET,
         mode_name);
  print_region(name, "A", a_region);
  print_region(name, "B", b_region);
  print_region(name, "C", c_region);

  alias_region_write(a_region, (const uint8_t *)resadd_a_dram, RESADD_BYTES);
  alias_region_write(b_region, (const uint8_t *)resadd_b_dram, RESADD_BYTES);
  alias_region_zero(c_region, RESADD_BYTES);
  memset(resadd_out_shadow_dram, 0, sizeof(resadd_out_shadow_dram));

  if (!rr_acquire_cfg_with_retry(GEMMINI_CFG_ID, (uint64_t)gemmini_manager_id)) {
    printf("CASE_FAIL %s reason=acquire\n", name);
    return false;
  }

  rr_set_opc(3, GEMMINI_CFG_ID);
  gemmini_flush(0);
  spm_xlate_table_clear();
  spm_xlate_map_region(a_region);
  spm_xlate_map_region(b_region);
  spm_xlate_map_region(c_region);
  spm_xlate_program(true);

  bool issued = false;
  switch (mode) {
    case RESADD_MODE_EXPLICIT_NO_FENCE:
      issued = resadd_explicit_issue_no_fence(a_req, b_req, c_req,
                                              RESADD_I, RESADD_J, RESADD_STRIDE, false);
      break;
    case RESADD_MODE_EXPLICIT_SERIALIZED:
      issued = resadd_explicit_issue_serialized(a_req, b_req, c_req,
                                                RESADD_I, RESADD_J, RESADD_STRIDE, false);
      break;
    case RESADD_MODE_STANDARD_WS:
      issued = resadd_standard_issue(a_req, b_req, c_req,
                                     RESADD_I, RESADD_J, RESADD_STRIDE, false);
      break;
    default:
      issued = false;
      break;
  }

  if (!issued) {
    spm_xlate_reset();
    rr_fence(GEMMINI_CFG_ID);
    rr_release(GEMMINI_CFG_ID);
    printf("CASE_FAIL %s reason=issue\n", name);
    return false;
  }

  gemmini_wait_managed(GEMMINI_CFG_ID);
  spm_xlate_reset();
  rr_fence(GEMMINI_CFG_ID);
  rr_release(GEMMINI_CFG_ID);

  alias_region_read(c_region, (uint8_t *)resadd_out_shadow_dram, RESADD_BYTES);
  ok = finish_elem_case_result(name,
                               (const elem_t *)resadd_gold_dram,
                               (const elem_t *)resadd_out_shadow_dram,
                               RESADD_ELEM_COUNT);
  return ok;
}

void thread_entry(int cid, int nc) {
  (void)nc;
  if (cid != 0) {
    while (1) {
      asm volatile("wfi");
    }
  }

  const uint32_t local_gemmini_id = REROCC_TEST_LOCAL_GEMMINI_ID;
  const int gemmini_manager_id = REROCC_GEMMINI_BASE_ID + (int)local_gemmini_id;
#if REROCC_FOCUSED_BIAS_MVIN3_ALIAS
  {
    const char *name = "bias_mvin3_runtime_alias_focus";
    spm_alias_region_t pw_chunk_interleaved_bias;
    uint32_t seed = 0x12345678u;
    bool ok = false;

    printf("[bias-mvin3-runtime-alias-focus] gemmini_mgr=%d local_gemmini=%lu num_gemmini=%d cfg=%u opcode=%u\n",
           gemmini_manager_id, (unsigned long)local_gemmini_id, REROCC_NUM_GEMMINI,
           GEMMINI_CFG_ID, GEMMINI_OPCODE_ID);
    init_phase_log("focused_before_init_pw_chunk_bias", seed);
    init_random_acc((acc_t *)pw_chunk_bias_dram, PW_CHUNK_TILE_J, &seed);
    init_phase_log("focused_after_init_pw_chunk_bias", seed);

    if (!alias_region_build_interleaved(&pw_chunk_interleaved_bias, PW_CHUNK_BIAS_VPAGE,
                                        PW_VADDR_PAGE_OFFSET, PW_CHUNK_BYTES_BIAS,
                                        PW_CHUNK_INTERLEAVED_BIAS_LOCAL_PAGE,
                                        local_gemmini_id + 4U)) {
      printf("ALL_TESTS_FAIL reason=region_build\n");
      exit(1);
    }

    ok = run_bias_mvin3_runtime_focus_case(name, gemmini_manager_id,
                                           &pw_chunk_interleaved_bias,
                                           (const acc_t *)pw_chunk_bias_dram,
                                           PW_CHUNK_BYTES_BIAS);
    rr_release_all(RR_MAX_CFGS);
    if (ok) {
      printf("ALL_TESTS_PASS\n");
      exit(0);
    }
    printf("ALL_TESTS_FAIL\n");
    exit(1);
  }
#endif
#if REROCC_FOCUSED_BIAS_MVIN3_ALIAS_VPAGE0
  {
    const char *name = "bias_mvin3_runtime_alias_vpage0_focus";
    spm_alias_region_t pw_chunk_interleaved_bias;
    uint32_t seed = 0x12345678u;
    bool ok = false;

    printf("[bias-mvin3-runtime-alias-vpage0-focus] gemmini_mgr=%d local_gemmini=%lu num_gemmini=%d cfg=%u opcode=%u\n",
           gemmini_manager_id, (unsigned long)local_gemmini_id, REROCC_NUM_GEMMINI,
           GEMMINI_CFG_ID, GEMMINI_OPCODE_ID);
    init_phase_log("focused_vpage0_before_init_pw_chunk_bias", seed);
    init_random_acc((acc_t *)pw_chunk_bias_dram, PW_CHUNK_TILE_J, &seed);
    init_phase_log("focused_vpage0_after_init_pw_chunk_bias", seed);

    if (!alias_region_build_interleaved(&pw_chunk_interleaved_bias, 0U,
                                        0ULL, PW_CHUNK_BYTES_BIAS,
                                        PW_CHUNK_INTERLEAVED_BIAS_LOCAL_PAGE,
                                        local_gemmini_id + 4U)) {
      printf("ALL_TESTS_FAIL reason=region_build\n");
      exit(1);
    }

    ok = run_bias_mvin3_runtime_focus_case(name, gemmini_manager_id,
                                           &pw_chunk_interleaved_bias,
                                           (const acc_t *)pw_chunk_bias_dram,
                                           PW_CHUNK_BYTES_BIAS);
    rr_release_all(RR_MAX_CFGS);
    if (ok) {
      printf("ALL_TESTS_PASS\n");
      exit(0);
    }
    printf("ALL_TESTS_FAIL\n");
    exit(1);
  }
#endif
#if REROCC_FOCUSED_POINTWISE_OS_FIRST_PAIR_ALIAS
  {
    const char *name = "pointwise_os_first_pair_runtime_alias_focus";
    spm_alias_region_t pw_interleaved_a;
    spm_alias_region_t pw_interleaved_b;
    spm_alias_region_t pw_chunk_interleaved_bias;
    spm_alias_region_t pw_interleaved_c;
    uint32_t seed = 0x12345678u;
    bool ok = false;

    printf("[pointwise-os-first-pair-runtime-alias-focus] gemmini_mgr=%d local_gemmini=%lu num_gemmini=%d cfg=%u opcode=%u\n",
           gemmini_manager_id, (unsigned long)local_gemmini_id, REROCC_NUM_GEMMINI,
           GEMMINI_CFG_ID, GEMMINI_OPCODE_ID);
    init_phase_log("focused_first_pair_before_init_pw_a", seed);
    init_random_elem((elem_t *)pw_a_dram, PW_ELEM_COUNT_A, &seed);
    init_phase_log("focused_first_pair_after_init_pw_a", seed);
    init_phase_log("focused_first_pair_before_init_pw_b", seed);
    init_random_elem((elem_t *)pw_b_dram, PW_ELEM_COUNT_B, &seed);
    init_phase_log("focused_first_pair_after_init_pw_b", seed);
    init_phase_log("focused_first_pair_before_init_pw_chunk_bias", seed);
    init_random_acc((acc_t *)pw_chunk_bias_dram, PW_CHUNK_TILE_J, &seed);
    init_phase_log("focused_first_pair_after_init_pw_chunk_bias", seed);

    if (!alias_region_build_interleaved(&pw_interleaved_a, PW_A_VPAGE,
                                        PW_VADDR_PAGE_OFFSET, PW_BYTES_A,
                                        PW_INTERLEAVED_A_LOCAL_PAGE, local_gemmini_id) ||
        !alias_region_build_interleaved(&pw_interleaved_b, PW_B_VPAGE,
                                        PW_VADDR_PAGE_OFFSET, PW_BYTES_B,
                                        PW_INTERLEAVED_B_LOCAL_PAGE, local_gemmini_id + 1U) ||
        !alias_region_build_interleaved(&pw_chunk_interleaved_bias, PW_CHUNK_BIAS_VPAGE,
                                        PW_VADDR_PAGE_OFFSET, PW_CHUNK_BYTES_BIAS,
                                        PW_CHUNK_INTERLEAVED_BIAS_LOCAL_PAGE,
                                        local_gemmini_id + 4U) ||
        !alias_region_build_interleaved(&pw_interleaved_c, PW_C_VPAGE,
                                        PW_VADDR_PAGE_OFFSET, PW_BYTES_C,
                                        PW_INTERLEAVED_C_LOCAL_PAGE,
                                        local_gemmini_id + 5U)) {
      printf("ALL_TESTS_FAIL reason=region_build\n");
      exit(1);
    }

    ok = run_pointwise_os_first_pair_runtime_focus_case(name, gemmini_manager_id,
                                                        &pw_interleaved_a,
                                                        &pw_interleaved_b,
                                                        &pw_chunk_interleaved_bias,
                                                        &pw_interleaved_c);
    rr_release_all(RR_MAX_CFGS);
    if (ok) {
      printf("ALL_TESTS_PASS\n");
      exit(0);
    }
    printf("ALL_TESTS_FAIL\n");
    exit(1);
  }
#endif
#if REROCC_FOCUSED_POINTWISE_OS_INNER_ALIAS
  {
    const char *name = "pointwise_os_inner_runtime_alias_focus";
    spm_alias_region_t pw_interleaved_a;
    spm_alias_region_t pw_interleaved_b;
    spm_alias_region_t pw_chunk_interleaved_bias;
    spm_alias_region_t pw_interleaved_c;
    const char *region_names[] = {"pw_interleaved_a", "pw_interleaved_b",
                                  "pw_chunk_interleaved_bias", "pw_interleaved_c"};
    const spm_alias_region_t *regions[] = {
      &pw_interleaved_a, &pw_interleaved_b, &pw_chunk_interleaved_bias, &pw_interleaved_c,
    };
    uint32_t seed = 0x12345678u;
    bool ok = false;

    printf("[pointwise-os-inner-runtime-alias-focus] gemmini_mgr=%d local_gemmini=%lu num_gemmini=%d cfg=%u opcode=%u\n",
           gemmini_manager_id, (unsigned long)local_gemmini_id, REROCC_NUM_GEMMINI,
           GEMMINI_CFG_ID, GEMMINI_OPCODE_ID);
    init_phase_log("focused_before_init_pw_a", seed);
    init_random_elem((elem_t *)pw_a_dram, PW_ELEM_COUNT_A, &seed);
    init_phase_log("focused_after_init_pw_a", seed);
    init_phase_log("focused_before_init_pw_b", seed);
    init_random_elem((elem_t *)pw_b_dram, PW_ELEM_COUNT_B, &seed);
    init_phase_log("focused_after_init_pw_b", seed);
    init_phase_log("focused_before_init_pw_chunk_bias", seed);
    init_random_acc((acc_t *)pw_chunk_bias_dram, PW_CHUNK_TILE_J, &seed);
    init_phase_log("focused_after_init_pw_chunk_bias", seed);

    if (!alias_region_build_interleaved(&pw_interleaved_a, PW_A_VPAGE,
                                        PW_VADDR_PAGE_OFFSET, PW_BYTES_A,
                                        PW_INTERLEAVED_A_LOCAL_PAGE, local_gemmini_id) ||
        !alias_region_build_interleaved(&pw_interleaved_b, PW_B_VPAGE,
                                        PW_VADDR_PAGE_OFFSET, PW_BYTES_B,
                                        PW_INTERLEAVED_B_LOCAL_PAGE, local_gemmini_id + 1U) ||
        !alias_region_build_interleaved(&pw_chunk_interleaved_bias, PW_CHUNK_BIAS_VPAGE,
                                        PW_VADDR_PAGE_OFFSET, PW_CHUNK_BYTES_BIAS,
                                        PW_INTERLEAVED_BIAS_LOCAL_PAGE, local_gemmini_id + 2U) ||
        !alias_region_build_interleaved(&pw_interleaved_c, PW_C_VPAGE,
                                        PW_VADDR_PAGE_OFFSET, PW_BYTES_C,
                                        PW_INTERLEAVED_C_LOCAL_PAGE, local_gemmini_id + 3U)) {
      printf("ALL_TESTS_FAIL reason=region_build\n");
      exit(1);
    }

    if (!validate_alias_layout(region_names, regions,
                               sizeof(regions) / sizeof(regions[0]))) {
      printf("ALL_TESTS_FAIL reason=layout\n");
      exit(1);
    }

    ok = run_pointwise_os_inner_runtime_focus_case(name, gemmini_manager_id,
                                                   &pw_interleaved_a,
                                                   &pw_interleaved_b,
                                                   &pw_chunk_interleaved_bias,
                                                   &pw_interleaved_c);
    rr_release_all(RR_MAX_CFGS);
    if (ok) {
      printf("ALL_TESTS_PASS\n");
      exit(0);
    }
    printf("ALL_TESTS_FAIL\n");
    exit(1);
  }
#endif
#if REROCC_FOCUSED_POINTWISE_EXPLICIT_TILES_ALIAS
  {
    const char *name = "pointwise_stage0_runtime_explicit_tiles_focus";
    spm_alias_region_t pw_interleaved_a;
    spm_alias_region_t pw_interleaved_b;
    spm_alias_region_t pw_chunk_interleaved_bias;
    spm_alias_region_t pw_interleaved_c;
    const char *region_names[] = {"pw_interleaved_a", "pw_interleaved_b",
                                  "pw_chunk_interleaved_bias", "pw_interleaved_c"};
    const spm_alias_region_t *regions[] = {
      &pw_interleaved_a, &pw_interleaved_b, &pw_chunk_interleaved_bias, &pw_interleaved_c,
    };
    uint32_t seed = 0x12345678u;
    bool ok = false;

    printf("[pointwise-stage0-runtime-explicit-tiles-focus] gemmini_mgr=%d local_gemmini=%lu num_gemmini=%d cfg=%u\n",
           gemmini_manager_id, (unsigned long)local_gemmini_id, REROCC_NUM_GEMMINI,
           GEMMINI_CFG_ID);
    init_phase_log("focused_before_init_pw_a", seed);
    init_random_elem((elem_t *)pw_a_dram, PW_ELEM_COUNT_A, &seed);
    init_phase_log("focused_after_init_pw_a", seed);
    init_phase_log("focused_before_init_pw_b", seed);
    init_random_elem((elem_t *)pw_b_dram, PW_ELEM_COUNT_B, &seed);
    init_phase_log("focused_after_init_pw_b", seed);
    init_phase_log("focused_before_init_pw_chunk_bias", seed);
    init_random_acc((acc_t *)pw_chunk_bias_dram, PW_CHUNK_TILE_J, &seed);
    init_phase_log("focused_after_init_pw_chunk_bias", seed);
    memset(pw_init_c_dram, 0xa5, sizeof(pw_init_c_dram));

    if (!alias_region_build_focus_layout(&pw_interleaved_a, PW_A_VPAGE,
                                         PW_VADDR_PAGE_OFFSET, PW_BYTES_A,
                                         PW_FOCUSED_STAGE0_A_LOCAL_PAGE,
                                         PW_FOCUSED_STAGE0_A_LOCAL_TILE,
                                         local_gemmini_id + PW_FOCUSED_STAGE0_A_SLOT_OFFSET) ||
        !alias_region_build_focus_layout(&pw_interleaved_b, PW_B_VPAGE,
                                         PW_VADDR_PAGE_OFFSET, PW_BYTES_B,
                                         PW_FOCUSED_STAGE0_B_LOCAL_PAGE,
                                         PW_FOCUSED_STAGE0_B_LOCAL_TILE,
                                         local_gemmini_id + PW_FOCUSED_STAGE0_B_SLOT_OFFSET) ||
        !alias_region_build_focus_layout(&pw_chunk_interleaved_bias, PW_CHUNK_BIAS_VPAGE,
                                         PW_VADDR_PAGE_OFFSET, PW_CHUNK_BYTES_BIAS,
                                         PW_FOCUSED_STAGE0_BIAS_LOCAL_PAGE,
                                         PW_FOCUSED_STAGE0_BIAS_LOCAL_TILE,
                                         local_gemmini_id + PW_FOCUSED_STAGE0_BIAS_SLOT_OFFSET) ||
        !alias_region_build_focus_layout(&pw_interleaved_c, PW_C_VPAGE,
                                         PW_VADDR_PAGE_OFFSET, PW_BYTES_C,
                                         PW_FOCUSED_STAGE0_C_LOCAL_PAGE,
                                         PW_FOCUSED_STAGE0_C_LOCAL_TILE,
                                         local_gemmini_id + PW_FOCUSED_STAGE0_C_SLOT_OFFSET)) {
      printf("ALL_TESTS_FAIL reason=region_build\n");
      exit(1);
    }

    if (!validate_alias_layout(region_names, regions, sizeof(regions) / sizeof(regions[0]))) {
      exit(1);
    }

    ok = run_pointwise_matmul_runtime_style_explicit_tiles_focus_case(
        name, gemmini_manager_id,
        &pw_interleaved_a, &pw_interleaved_b,
        &pw_chunk_interleaved_bias,
        &pw_interleaved_c);
    rr_release_all(RR_MAX_CFGS);
    if (ok) {
      printf("ALL_TESTS_PASS\n");
      exit(0);
    }
    printf("ALL_TESTS_FAIL\n");
    exit(1);
  }
#endif
#if REROCC_FOCUSED_POINTWISE_GROUPED_ALIAS
  {
#ifndef REROCC_FOCUSED_POINTWISE_GROUPED_NAME
#define REROCC_FOCUSED_POINTWISE_GROUPED_NAME "pointwise_stage0_runtime_grouped_focus"
#endif
    const char *name = REROCC_FOCUSED_POINTWISE_GROUPED_NAME;
    spm_alias_region_t pw_interleaved_a;
    spm_alias_region_t pw_interleaved_b;
    spm_alias_region_t pw_interleaved_bias;
    spm_alias_region_t pw_interleaved_c;
    const char *region_names[] = {"pw_interleaved_a", "pw_interleaved_b",
                                  "pw_interleaved_bias", "pw_interleaved_c"};
    const spm_alias_region_t *regions[] = {
      &pw_interleaved_a, &pw_interleaved_b, &pw_interleaved_bias, &pw_interleaved_c,
    };
    uint32_t seed = 0x12345678u;
    bool ok = false;

    printf("[%s] gemmini_mgr=%d local_gemmini=%lu num_gemmini=%d cfg=%u groups=%u\n",
           name, gemmini_manager_id, (unsigned long)local_gemmini_id, REROCC_NUM_GEMMINI,
           GEMMINI_CFG_ID, PW_GROUP_COUNT);
    init_phase_log("grouped_before_init_pw_a", seed);
    init_random_elem((elem_t *)pw_a_dram, PW_ELEM_COUNT_A, &seed);
    init_phase_log("grouped_after_init_pw_a", seed);
    init_phase_log("grouped_before_init_pw_group_b", seed);
    init_random_elem((elem_t *)pw_group_b_dram, PW_GROUP_TOTAL_K * (size_t)PW_B_STRIDE, &seed);
    init_phase_log("grouped_after_init_pw_group_b", seed);
    init_phase_log("grouped_before_init_pw_group_bias", seed);
    init_random_acc((acc_t *)pw_group_bias_dram, PW_GROUP_TOTAL_OC, &seed);
    init_phase_log("grouped_after_init_pw_group_bias", seed);
    memset(pw_init_c_dram, 0xa5, sizeof(pw_init_c_dram));

    if (!alias_region_build_focus_layout(&pw_interleaved_a, PW_A_VPAGE,
                                         PW_VADDR_PAGE_OFFSET, PW_BYTES_A,
                                         PW_FOCUSED_STAGE0_A_LOCAL_PAGE,
                                         PW_FOCUSED_STAGE0_A_LOCAL_TILE,
                                         local_gemmini_id + PW_FOCUSED_STAGE0_A_SLOT_OFFSET) ||
        !alias_region_build_focus_layout(&pw_interleaved_b, PW_B_VPAGE,
                                         PW_VADDR_PAGE_OFFSET, PW_GROUP_BYTES_B,
                                         PW_FOCUSED_STAGE0_B_LOCAL_PAGE,
                                         PW_FOCUSED_STAGE0_B_LOCAL_TILE,
                                         local_gemmini_id + PW_FOCUSED_STAGE0_B_SLOT_OFFSET) ||
        !alias_region_build_focus_layout(&pw_interleaved_bias, PW_BIAS_VPAGE,
                                         PW_VADDR_PAGE_OFFSET, PW_GROUP_BYTES_BIAS,
                                         PW_FOCUSED_STAGE0_BIAS_LOCAL_PAGE,
                                         PW_FOCUSED_STAGE0_BIAS_LOCAL_TILE,
                                         local_gemmini_id + PW_FOCUSED_STAGE0_BIAS_SLOT_OFFSET) ||
        !alias_region_build_focus_layout(&pw_interleaved_c, PW_C_VPAGE,
                                         PW_VADDR_PAGE_OFFSET, PW_BYTES_C,
                                         PW_FOCUSED_STAGE0_C_LOCAL_PAGE,
                                         PW_FOCUSED_STAGE0_C_LOCAL_TILE,
                                         local_gemmini_id + PW_FOCUSED_STAGE0_C_SLOT_OFFSET)) {
      printf("ALL_TESTS_FAIL reason=region_build\n");
      exit(1);
    }

    if (!validate_alias_layout(region_names, regions, sizeof(regions) / sizeof(regions[0]))) {
      exit(1);
    }

    ok = run_pointwise_matmul_runtime_style_grouped_focus_case(
        name, gemmini_manager_id,
        &pw_interleaved_a, &pw_interleaved_b,
        &pw_interleaved_bias,
        &pw_interleaved_c);
    rr_release_all(RR_MAX_CFGS);
    if (ok) {
      printf("ALL_TESTS_PASS\n");
      exit(0);
    }
    printf("ALL_TESTS_FAIL\n");
    exit(1);
  }
#endif
#if REROCC_FOCUSED_POINTWISE_INTERLEAVED
  {
#ifndef REROCC_FOCUSED_POINTWISE_INTERLEAVED_NAME
#define REROCC_FOCUSED_POINTWISE_INTERLEAVED_NAME "pointwise_stage0_runtime_interleaved_focus"
#endif
    const char *name = REROCC_FOCUSED_POINTWISE_INTERLEAVED_NAME;
    spm_alias_region_t pw_interleaved_a;
    spm_alias_region_t pw_interleaved_b;
    spm_alias_region_t pw_chunk_interleaved_bias;
    spm_alias_region_t pw_interleaved_c;
    const char *region_names[] = {"pw_interleaved_a", "pw_interleaved_b",
                                  "pw_chunk_interleaved_bias", "pw_interleaved_c"};
    const spm_alias_region_t *regions[] = {
      &pw_interleaved_a, &pw_interleaved_b, &pw_chunk_interleaved_bias, &pw_interleaved_c,
    };
    uint32_t seed = 0x12345678u;
    bool ok = false;

    printf("[pointwise-stage0-runtime-interleaved-focus] gemmini_mgr=%d local_gemmini=%lu num_gemmini=%d cfg=%u\n",
           gemmini_manager_id, (unsigned long)local_gemmini_id, REROCC_NUM_GEMMINI, GEMMINI_CFG_ID);
    init_phase_log("focused_before_init_pw_a", seed);
    init_random_elem((elem_t *)pw_a_dram, PW_ELEM_COUNT_A, &seed);
    init_phase_log("focused_after_init_pw_a", seed);
    init_phase_log("focused_before_init_pw_b", seed);
    init_random_elem((elem_t *)pw_b_dram, PW_ELEM_COUNT_B, &seed);
    init_phase_log("focused_after_init_pw_b", seed);
    init_phase_log("focused_before_init_pw_chunk_bias", seed);
    init_random_acc((acc_t *)pw_chunk_bias_dram, PW_CHUNK_TILE_J, &seed);
    init_phase_log("focused_after_init_pw_chunk_bias", seed);
    memset(pw_init_c_dram, 0xa5, sizeof(pw_init_c_dram));

    if (!alias_region_build_focus_layout(&pw_interleaved_a, PW_A_VPAGE,
                                         PW_VADDR_PAGE_OFFSET, PW_BYTES_A,
                                         PW_FOCUSED_STAGE0_A_LOCAL_PAGE,
                                         PW_FOCUSED_STAGE0_A_LOCAL_TILE,
                                         local_gemmini_id + PW_FOCUSED_STAGE0_A_SLOT_OFFSET) ||
        !alias_region_build_focus_layout(&pw_interleaved_b, PW_B_VPAGE,
                                         PW_VADDR_PAGE_OFFSET, PW_BYTES_B,
                                         PW_FOCUSED_STAGE0_B_LOCAL_PAGE,
                                         PW_FOCUSED_STAGE0_B_LOCAL_TILE,
                                         local_gemmini_id + PW_FOCUSED_STAGE0_B_SLOT_OFFSET) ||
        !alias_region_build_focus_layout(&pw_chunk_interleaved_bias, PW_CHUNK_BIAS_VPAGE,
                                         PW_VADDR_PAGE_OFFSET, PW_CHUNK_BYTES_BIAS,
                                         PW_FOCUSED_STAGE0_BIAS_LOCAL_PAGE,
                                         PW_FOCUSED_STAGE0_BIAS_LOCAL_TILE,
                                         local_gemmini_id + PW_FOCUSED_STAGE0_BIAS_SLOT_OFFSET) ||
        !alias_region_build_focus_layout(&pw_interleaved_c, PW_C_VPAGE,
                                         PW_VADDR_PAGE_OFFSET, PW_BYTES_C,
                                         PW_FOCUSED_STAGE0_C_LOCAL_PAGE,
                                         PW_FOCUSED_STAGE0_C_LOCAL_TILE,
                                         local_gemmini_id + PW_FOCUSED_STAGE0_C_SLOT_OFFSET)) {
      printf("ALL_TESTS_FAIL reason=region_build\n");
      exit(1);
    }

    if (!validate_alias_layout(region_names, regions, sizeof(regions) / sizeof(regions[0]))) {
      exit(1);
    }

    ok = run_pointwise_matmul_runtime_style_focus_case(name, gemmini_manager_id,
                                                       &pw_interleaved_a, &pw_interleaved_b,
                                                       &pw_chunk_interleaved_bias,
                                                       &pw_interleaved_c);
    rr_release_all(RR_MAX_CFGS);
    if (ok) {
      printf("ALL_TESTS_PASS\n");
      exit(0);
    }
    printf("ALL_TESTS_FAIL\n");
    exit(1);
  }
#endif
  spm_alias_region_t contig_a;
  spm_alias_region_t contig_b;
  spm_alias_region_t contig_c;
  spm_alias_region_t interleaved_a;
  spm_alias_region_t interleaved_b;
  spm_alias_region_t interleaved_c;
  spm_alias_region_t pw_contig_a;
  spm_alias_region_t pw_contig_b;
  spm_alias_region_t pw_contig_bias;
  spm_alias_region_t pw_contig_c;
  spm_alias_region_t pw_chunk_contig_bias;
  spm_alias_region_t pw_interleaved_a;
  spm_alias_region_t pw_interleaved_b;
  spm_alias_region_t pw_interleaved_bias;
  spm_alias_region_t pw_chunk_interleaved_bias;
  spm_alias_region_t pw_interleaved_c;
  spm_alias_region_t pw_diag_contig_a;
  spm_alias_region_t pw_diag_contig_b;
  spm_alias_region_t pw_diag_contig_bias;
  spm_alias_region_t pw_diag_contig_c;
  spm_alias_region_t pw_diag_interleaved_a;
  spm_alias_region_t pw_diag_interleaved_b;
  spm_alias_region_t pw_diag_interleaved_bias;
  spm_alias_region_t pw_diag_interleaved_c;
  uint32_t seed = 0x12345678u;
  bool case_copy_a_mvin;
  bool case_copy_a_interleaved_src_contig_dst_mvin;
  bool case_copy_a_contig_src_interleaved_dst_mvin;
  bool case_copy_b_mvin;
  bool case_copy_b_mvin2;
  bool case_copy_b_mvin2_clean;
  bool case_contig_explicit;
  bool case_interleaved_explicit;
  bool case_interleaved_explicit_serialized;
  bool case_contig_standard_ws;
  bool case_interleaved_standard_ws;
  bool case_pw_contig_os;
  bool case_pw_contig_ws;
  bool case_pw_contig_os_j128;
  bool case_pw_contig_os_j128_tjcap;
  bool case_pw_interleaved_os_j128;
  bool case_pw_interleaved_os_j128_tjcap;
  bool case_pw_interleaved_os;
  bool case_pw_interleaved_ws;
  bool case_pw_contig_os_chunked;
  bool case_pw_interleaved_os_chunked;
  bool case_pw_contig_os_chunked_runtime_style;
  bool case_pw_interleaved_os_chunked_runtime_style;
  bool case_pw_diag_contig_os_bias_only;
  bool case_pw_diag_interleaved_os_bias_only;
  bool case_bias_first_mvin_contig;
  bool case_bias_first_mvin_interleaved;

  printf("[resadd-explicit-baremetal] gemmini_mgr=%d local_gemmini=%lu num_gemmini=%d\n",
         gemmini_manager_id, (unsigned long)local_gemmini_id, REROCC_NUM_GEMMINI);
  init_phase_log("thread_entry_after_banner", seed);
  printf("STALL_DIAG mode=%d runtime_style_only=%d\n",
         stall_diag_only() ? 1 : 0,
         runtime_style_only() ? 1 : 0);

  init_phase_log("before_init_resadd_a", seed);
  init_random_elem((elem_t *)resadd_a_dram, RESADD_ELEM_COUNT, &seed);
  init_phase_log("after_init_resadd_a", seed);
  init_phase_log("before_init_resadd_b", seed);
  init_random_elem((elem_t *)resadd_b_dram, RESADD_ELEM_COUNT, &seed);
  init_phase_log("after_init_resadd_b", seed);
  init_phase_log("before_resadd_reference", seed);
  resadd_reference((const elem_t *)resadd_a_dram,
                   (const elem_t *)resadd_b_dram,
                   (elem_t *)resadd_gold_dram,
                   RESADD_ELEM_COUNT);
  init_phase_log("after_resadd_reference", seed);
  init_phase_log("before_init_pw_a", seed);
  init_random_elem((elem_t *)pw_a_dram, PW_ELEM_COUNT_A, &seed);
  init_phase_log("after_init_pw_a", seed);
  init_phase_log("before_init_pw_b", seed);
  init_random_elem((elem_t *)pw_b_dram, PW_ELEM_COUNT_B, &seed);
  init_phase_log("after_init_pw_b", seed);
  init_phase_log("before_init_pw_bias", seed);
  init_random_acc((acc_t *)pw_bias_dram, PW_J, &seed);
  init_phase_log("after_init_pw_bias", seed);
  init_phase_log("before_init_pw_chunk_bias", seed);
  init_random_acc((acc_t *)pw_chunk_bias_dram, PW_CHUNK_TILE_J, &seed);
  init_phase_log("after_init_pw_chunk_bias", seed);
  init_phase_log("before_memset_pw_init_c", seed);
  memset(pw_init_c_dram, 0xa5, sizeof(pw_init_c_dram));
  init_phase_log("after_memset_pw_init_c", seed);
  if (compare_disabled()) {
    init_phase_log("stall_diag_skip_pointwise_goldens", seed);
  } else {
    init_phase_log("before_copy_pw_golds", seed);
    memcpy(pw_gold_dram, pw_init_c_dram, sizeof(pw_gold_dram));
    memcpy(pw_j128_gold_dram, pw_init_c_dram, sizeof(pw_j128_gold_dram));
    memcpy(pw_chunk_gold_dram, pw_init_c_dram, sizeof(pw_chunk_gold_dram));
    init_phase_log("after_copy_pw_golds", seed);
    init_phase_log("before_pointwise_reference_full", seed);
    pointwise_matmul_reference((const elem_t *)pw_a_dram,
                               (const elem_t *)pw_b_dram,
                               (const acc_t *)pw_bias_dram,
                               (elem_t *)pw_gold_dram);
    init_phase_log("after_pointwise_reference_full", seed);
    init_phase_log("before_pointwise_reference_dim_j", seed);
    pointwise_matmul_reference_dim_j((const elem_t *)pw_a_dram,
                                     (const elem_t *)pw_b_dram,
                                     (const acc_t *)pw_chunk_bias_dram,
                                     (const elem_t *)pw_init_c_dram,
                                     (elem_t *)pw_j128_gold_dram,
                                     PW_CHUNK_TILE_J);
    init_phase_log("after_pointwise_reference_dim_j", seed);
    init_phase_log("before_pointwise_reference_chunked", seed);
    pointwise_matmul_chunked_reference((const elem_t *)pw_a_dram,
                                       (const elem_t *)pw_b_dram,
                                       (const acc_t *)pw_chunk_bias_dram,
                                       (const elem_t *)pw_init_c_dram,
                                       (elem_t *)pw_chunk_gold_dram);
    init_phase_log("after_pointwise_reference_chunked", seed);
  }
  init_phase_log("before_diag_memset_ab", seed);
  memset(pw_diag_a_dram, 0, sizeof(pw_diag_a_dram));
  memset(pw_diag_b_dram, 0, sizeof(pw_diag_b_dram));
  init_phase_log("after_diag_memset_ab", seed);
  init_phase_log("before_init_diag_bias", seed);
  init_random_acc((acc_t *)pw_diag_bias_dram, PW_DIAG_J, &seed);
  init_phase_log("after_init_diag_bias", seed);
  init_phase_log("before_memset_diag_init_c", seed);
  memset(pw_diag_init_c_dram, 0xa5, sizeof(pw_diag_init_c_dram));
  init_phase_log("after_memset_diag_init_c", seed);
  if (compare_disabled()) {
    init_phase_log("stall_diag_skip_diag_goldens", seed);
  } else {
    init_phase_log("before_pointwise_reference_shape", seed);
    pointwise_matmul_reference_shape((const elem_t *)pw_diag_a_dram,
                                     (const elem_t *)pw_diag_b_dram,
                                     (const acc_t *)pw_diag_bias_dram,
                                     (const elem_t *)pw_diag_init_c_dram,
                                     (elem_t *)pw_diag_gold_dram,
                                     PW_DIAG_I, PW_DIAG_J, PW_DIAG_K,
                                     PW_A_STRIDE, PW_B_STRIDE, PW_C_STRIDE);
    init_phase_log("after_pointwise_reference_shape", seed);
  }

  init_phase_log("before_alias_region_build", seed);
  if (!alias_region_build_contiguous(&contig_a, CONTIG_A_VPAGE,
                                     RESADD_VADDR_PAGE_OFFSET, RESADD_BYTES,
                                     local_gemmini_id, CONTIG_A_LOCAL_PAGE) ||
      !alias_region_build_contiguous(&contig_b, CONTIG_B_VPAGE,
                                     RESADD_VADDR_PAGE_OFFSET, RESADD_BYTES,
                                     local_gemmini_id, CONTIG_B_LOCAL_PAGE) ||
      !alias_region_build_contiguous(&contig_c, CONTIG_C_VPAGE,
                                     RESADD_VADDR_PAGE_OFFSET, RESADD_BYTES,
                                     local_gemmini_id, CONTIG_C_LOCAL_PAGE) ||
      !alias_region_build_interleaved(&interleaved_a, INTERLEAVED_A_VPAGE,
                                      RESADD_VADDR_PAGE_OFFSET, RESADD_BYTES,
                                      INTERLEAVED_A_LOCAL_PAGE, local_gemmini_id) ||
      !alias_region_build_interleaved(&interleaved_b, INTERLEAVED_B_VPAGE,
                                      RESADD_VADDR_PAGE_OFFSET, RESADD_BYTES,
                                      INTERLEAVED_B_LOCAL_PAGE, local_gemmini_id + 1U) ||
      !alias_region_build_interleaved(&interleaved_c, INTERLEAVED_C_VPAGE,
                                      RESADD_VADDR_PAGE_OFFSET, RESADD_BYTES,
                                      INTERLEAVED_C_LOCAL_PAGE, local_gemmini_id + 2U) ||
      !alias_region_build_contiguous(&pw_contig_a, PW_A_VPAGE,
                                     PW_VADDR_PAGE_OFFSET, PW_BYTES_A,
                                     local_gemmini_id, PW_CONTIG_A_LOCAL_PAGE) ||
      !alias_region_build_contiguous(&pw_contig_b, PW_B_VPAGE,
                                     PW_VADDR_PAGE_OFFSET, PW_BYTES_B,
                                     local_gemmini_id, PW_CONTIG_B_LOCAL_PAGE) ||
      !alias_region_build_contiguous(&pw_contig_bias, PW_BIAS_VPAGE,
                                     PW_VADDR_PAGE_OFFSET, PW_BYTES_BIAS,
                                     local_gemmini_id, PW_CONTIG_BIAS_LOCAL_PAGE) ||
      !alias_region_build_contiguous(&pw_chunk_contig_bias, PW_CHUNK_BIAS_CONTIG_VPAGE,
                                     PW_VADDR_PAGE_OFFSET, PW_CHUNK_BYTES_BIAS,
                                     local_gemmini_id, PW_CHUNK_CONTIG_BIAS_LOCAL_PAGE) ||
      !alias_region_build_contiguous(&pw_contig_c, PW_C_VPAGE,
                                     PW_VADDR_PAGE_OFFSET, PW_BYTES_C,
                                     local_gemmini_id, PW_CONTIG_C_LOCAL_PAGE) ||
      !alias_region_build_interleaved(&pw_interleaved_a, PW_A_VPAGE,
                                      PW_VADDR_PAGE_OFFSET, PW_BYTES_A,
                                      PW_INTERLEAVED_A_LOCAL_PAGE, local_gemmini_id) ||
      !alias_region_build_interleaved(&pw_interleaved_b, PW_B_VPAGE,
                                      PW_VADDR_PAGE_OFFSET, PW_BYTES_B,
                                      PW_INTERLEAVED_B_LOCAL_PAGE, local_gemmini_id + 1U) ||
      !alias_region_build_interleaved(&pw_interleaved_bias, PW_BIAS_VPAGE,
                                      PW_VADDR_PAGE_OFFSET, PW_BYTES_BIAS,
                                      PW_INTERLEAVED_BIAS_LOCAL_PAGE, local_gemmini_id + 2U) ||
      !alias_region_build_interleaved(&pw_chunk_interleaved_bias, PW_CHUNK_BIAS_VPAGE,
                                      PW_VADDR_PAGE_OFFSET, PW_CHUNK_BYTES_BIAS,
                                      PW_CHUNK_INTERLEAVED_BIAS_LOCAL_PAGE, local_gemmini_id + 4U) ||
      !alias_region_build_interleaved(&pw_interleaved_c, PW_C_VPAGE,
                                      PW_VADDR_PAGE_OFFSET, PW_BYTES_C,
                                      PW_INTERLEAVED_C_LOCAL_PAGE, local_gemmini_id + 3U) ||
      !alias_region_build_contiguous(&pw_diag_contig_a, PW_DIAG_A_VPAGE,
                                     PW_VADDR_PAGE_OFFSET, PW_DIAG_BYTES_A,
                                     local_gemmini_id, PW_DIAG_CONTIG_A_LOCAL_PAGE) ||
      !alias_region_build_contiguous(&pw_diag_contig_b, PW_DIAG_B_VPAGE,
                                     PW_VADDR_PAGE_OFFSET, PW_DIAG_BYTES_B,
                                     local_gemmini_id, PW_DIAG_CONTIG_B_LOCAL_PAGE) ||
      !alias_region_build_contiguous(&pw_diag_contig_bias, PW_DIAG_BIAS_VPAGE,
                                     PW_VADDR_PAGE_OFFSET, PW_DIAG_BYTES_BIAS,
                                     local_gemmini_id, PW_DIAG_CONTIG_BIAS_LOCAL_PAGE) ||
      !alias_region_build_contiguous(&pw_diag_contig_c, PW_DIAG_C_VPAGE,
                                     PW_VADDR_PAGE_OFFSET, PW_DIAG_BYTES_C,
                                     local_gemmini_id, PW_DIAG_CONTIG_C_LOCAL_PAGE) ||
      !alias_region_build_interleaved(&pw_diag_interleaved_a, PW_DIAG_A_VPAGE,
                                      PW_VADDR_PAGE_OFFSET, PW_DIAG_BYTES_A,
                                      PW_DIAG_INTERLEAVED_A_LOCAL_PAGE, local_gemmini_id) ||
      !alias_region_build_interleaved(&pw_diag_interleaved_b, PW_DIAG_B_VPAGE,
                                      PW_VADDR_PAGE_OFFSET, PW_DIAG_BYTES_B,
                                      PW_DIAG_INTERLEAVED_B_LOCAL_PAGE, local_gemmini_id + 1U) ||
      !alias_region_build_interleaved(&pw_diag_interleaved_bias, PW_DIAG_BIAS_VPAGE,
                                      PW_VADDR_PAGE_OFFSET, PW_DIAG_BYTES_BIAS,
                                      PW_DIAG_INTERLEAVED_BIAS_LOCAL_PAGE, local_gemmini_id + 2U) ||
      !alias_region_build_interleaved(&pw_diag_interleaved_c, PW_DIAG_C_VPAGE,
                                      PW_VADDR_PAGE_OFFSET, PW_DIAG_BYTES_C,
                                      PW_DIAG_INTERLEAVED_C_LOCAL_PAGE, local_gemmini_id + 3U)) {
    printf("ALL_TESTS_FAIL reason=region_build\n");
    exit(1);
  }
  init_phase_log("after_alias_region_build", seed);

  init_phase_log("before_validate_alias_layout", seed);
  {
    const char *region_names[] = {
      "contig_a", "contig_b", "contig_c",
      "interleaved_a", "interleaved_b", "interleaved_c",
      "pw_contig_a", "pw_contig_b", "pw_contig_bias",
      "pw_chunk_contig_bias", "pw_contig_c",
      "pw_interleaved_a", "pw_interleaved_b", "pw_interleaved_bias",
      "pw_chunk_interleaved_bias", "pw_interleaved_c",
      "pw_diag_contig_a", "pw_diag_contig_b", "pw_diag_contig_bias", "pw_diag_contig_c",
      "pw_diag_interleaved_a", "pw_diag_interleaved_b",
      "pw_diag_interleaved_bias", "pw_diag_interleaved_c",
    };
    const spm_alias_region_t *regions[] = {
      &contig_a, &contig_b, &contig_c,
      &interleaved_a, &interleaved_b, &interleaved_c,
      &pw_contig_a, &pw_contig_b, &pw_contig_bias,
      &pw_chunk_contig_bias, &pw_contig_c,
      &pw_interleaved_a, &pw_interleaved_b, &pw_interleaved_bias,
      &pw_chunk_interleaved_bias, &pw_interleaved_c,
      &pw_diag_contig_a, &pw_diag_contig_b, &pw_diag_contig_bias, &pw_diag_contig_c,
      &pw_diag_interleaved_a, &pw_diag_interleaved_b,
      &pw_diag_interleaved_bias, &pw_diag_interleaved_c,
    };
    if (!validate_alias_layout(region_names, regions,
                               sizeof(regions) / sizeof(regions[0]))) {
      exit(1);
    }
  }
  init_phase_log("after_validate_alias_layout", seed);
  if (runtime_style_only()) {
    init_phase_log("before_runtime_style_only_cases", seed);
    case_contig_explicit = true;
    case_contig_standard_ws = true;
    case_copy_a_mvin = true;
    case_copy_a_interleaved_src_contig_dst_mvin = true;
    case_copy_a_contig_src_interleaved_dst_mvin = true;
    case_copy_b_mvin = true;
    case_copy_b_mvin2 = true;
    case_copy_b_mvin2_clean = true;
    case_interleaved_explicit = true;
    case_interleaved_explicit_serialized = true;
    case_interleaved_standard_ws = true;
    case_pw_diag_contig_os_bias_only = true;
    case_pw_diag_interleaved_os_bias_only = true;
    case_pw_contig_os = true;
    case_pw_contig_ws = true;
    case_pw_contig_os_j128 = true;
    case_pw_contig_os_j128_tjcap = true;
    case_pw_interleaved_os = true;
    case_pw_interleaved_os_j128 = true;
    case_pw_interleaved_os_j128_tjcap = true;
    case_pw_interleaved_ws = true;
    case_pw_contig_os_chunked = true;
    case_pw_interleaved_os_chunked = true;
    case_bias_first_mvin_contig = true;
    case_bias_first_mvin_interleaved = true;
    case_pw_contig_os_chunked_runtime_style =
        run_pointwise_matmul_chunked_runtime_style_case(
            "pointwise_matmul_os_chunked_128_to_64_runtime_style_cross_1kb_contiguous",
            gemmini_manager_id,
            &pw_contig_a, &pw_contig_b,
            &pw_chunk_contig_bias, &pw_contig_c,
            OS);
    case_pw_interleaved_os_chunked_runtime_style =
        run_pointwise_matmul_chunked_runtime_style_case(
            "pointwise_matmul_os_chunked_128_to_64_runtime_style_cross_1kb_interleaved",
            gemmini_manager_id,
            &pw_interleaved_a, &pw_interleaved_b,
            &pw_chunk_interleaved_bias, &pw_interleaved_c,
            OS);
  } else if (stall_diag_only()) {
    init_phase_log("before_first_case_bias_first_mvin", seed);
    case_contig_explicit = true;
    case_contig_standard_ws = true;
    case_copy_a_mvin = true;
    case_copy_a_interleaved_src_contig_dst_mvin = true;
    case_copy_a_contig_src_interleaved_dst_mvin = true;
    case_copy_b_mvin = true;
    case_copy_b_mvin2 = true;
    case_copy_b_mvin2_clean = true;
    case_interleaved_explicit = true;
    case_interleaved_explicit_serialized = true;
    case_interleaved_standard_ws = true;
    case_pw_diag_contig_os_bias_only = true;
    case_pw_diag_interleaved_os_bias_only = true;
    case_pw_contig_os = true;
    case_pw_contig_ws = true;
    case_pw_contig_os_j128 = true;
    case_pw_contig_os_j128_tjcap = true;
    case_pw_interleaved_os = true;
    case_pw_interleaved_os_j128 = true;
    case_pw_interleaved_os_j128_tjcap = true;
    case_pw_interleaved_ws = true;
    case_pw_contig_os_chunked = true;
    case_pw_interleaved_os_chunked = true;
    case_pw_contig_os_chunked_runtime_style = true;
    case_pw_interleaved_os_chunked_runtime_style = true;
    case_bias_first_mvin_contig =
        run_bias_mvin_linux_first_tile_case("bias_first_mvin_linux_tile_cross_1kb_contiguous",
                                            gemmini_manager_id,
                                            &pw_diag_contig_bias,
                                            (const acc_t *)pw_diag_bias_dram,
                                            PW_DIAG_BYTES_BIAS);
    case_bias_first_mvin_interleaved =
        run_bias_mvin_linux_first_tile_case("bias_first_mvin_linux_tile_cross_1kb_interleaved",
                                            gemmini_manager_id,
                                            &pw_diag_interleaved_bias,
                                            (const acc_t *)pw_diag_bias_dram,
                                            PW_DIAG_BYTES_BIAS);
  } else {
    init_phase_log("before_first_case_resadd_explicit", seed);

    case_contig_explicit = run_resadd_case("resadd_explicit_cross_1kb_contiguous",
                                           gemmini_manager_id,
                                           &contig_a, &contig_b, &contig_c,
                                           RESADD_MODE_EXPLICIT_NO_FENCE);
    case_contig_standard_ws = run_resadd_case("resadd_standard_ws_cross_1kb_contiguous",
                                              gemmini_manager_id,
                                              &contig_a, &contig_b, &contig_c,
                                              RESADD_MODE_STANDARD_WS);
    case_copy_a_mvin = run_copy_case("copy_explicit_cross_1kb_interleaved_a_mvin",
                                     gemmini_manager_id,
                                     &interleaved_a, &interleaved_c,
                                     (const elem_t *)resadd_a_dram,
                                     false, 0U);
    case_copy_a_interleaved_src_contig_dst_mvin =
        run_copy_case("copy_explicit_cross_1kb_interleaved_src_contig_dst_a_mvin",
                      gemmini_manager_id,
                      &interleaved_a, &contig_c,
                      (const elem_t *)resadd_a_dram,
                      false, 0U);
    case_copy_a_contig_src_interleaved_dst_mvin =
        run_copy_case("copy_explicit_cross_1kb_contig_src_interleaved_dst_a_mvin",
                      gemmini_manager_id,
                      &contig_a, &interleaved_c,
                      (const elem_t *)resadd_a_dram,
                      false, 0U);
    case_copy_b_mvin = run_copy_case("copy_explicit_cross_1kb_interleaved_b_mvin",
                                     gemmini_manager_id,
                                     &interleaved_b, &interleaved_c,
                                     (const elem_t *)resadd_b_dram,
                                     false, 0U);
    case_copy_b_mvin2 = run_copy_case("copy_explicit_cross_1kb_interleaved_b_mvin2",
                                      gemmini_manager_id,
                                      &interleaved_b, &interleaved_c,
                                      (const elem_t *)resadd_b_dram,
                                      true, 0U);
    case_copy_b_mvin2_clean = run_copy_case("copy_explicit_cross_1kb_interleaved_b_mvin2_clean",
                                            gemmini_manager_id,
                                            &interleaved_b, &interleaved_c,
                                            (const elem_t *)resadd_b_dram,
                                            true, CLEAN_ACC_ROW_OFFSET);
    case_interleaved_explicit = run_resadd_case("resadd_explicit_cross_1kb_interleaved",
                                                gemmini_manager_id,
                                                &interleaved_a, &interleaved_b, &interleaved_c,
                                                RESADD_MODE_EXPLICIT_NO_FENCE);
    case_interleaved_explicit_serialized =
        run_resadd_case("resadd_explicit_serialized_cross_1kb_interleaved",
                        gemmini_manager_id,
                        &interleaved_a, &interleaved_b, &interleaved_c,
                        RESADD_MODE_EXPLICIT_SERIALIZED);
    case_interleaved_standard_ws =
        run_resadd_case("resadd_standard_ws_cross_1kb_interleaved",
                        gemmini_manager_id,
                        &interleaved_a, &interleaved_b, &interleaved_c,
                        RESADD_MODE_STANDARD_WS);
    case_bias_first_mvin_contig =
        run_bias_mvin_linux_first_tile_case("bias_first_mvin_linux_tile_cross_1kb_contiguous",
                                            gemmini_manager_id,
                                            &pw_diag_contig_bias,
                                            (const acc_t *)pw_diag_bias_dram,
                                            PW_DIAG_BYTES_BIAS);
    case_bias_first_mvin_interleaved =
        run_bias_mvin_linux_first_tile_case("bias_first_mvin_linux_tile_cross_1kb_interleaved",
                                            gemmini_manager_id,
                                            &pw_diag_interleaved_bias,
                                            (const acc_t *)pw_diag_bias_dram,
                                            PW_DIAG_BYTES_BIAS);
    case_pw_diag_contig_os_bias_only =
        run_pointwise_matmul_case_shape("pointwise_matmul_os_biasonly_smallik8_128_cross_1kb_contiguous",
                                        gemmini_manager_id,
                                        &pw_diag_contig_a, &pw_diag_contig_b,
                                        &pw_diag_contig_bias, &pw_diag_contig_c,
                                        (const elem_t *)pw_diag_a_dram, PW_DIAG_BYTES_A,
                                        (const elem_t *)pw_diag_b_dram, PW_DIAG_BYTES_B,
                                        (const acc_t *)pw_diag_bias_dram, PW_DIAG_BYTES_BIAS,
                                        (const elem_t *)pw_diag_init_c_dram, PW_DIAG_BYTES_C,
                                        (const elem_t *)pw_diag_gold_dram, PW_DIAG_ELEM_COUNT_C,
                                        PW_DIAG_I, PW_DIAG_J, PW_DIAG_K,
                                        PW_A_STRIDE, PW_B_STRIDE, PW_C_STRIDE,
                                        OS);
    case_pw_diag_interleaved_os_bias_only =
        run_pointwise_matmul_case_shape("pointwise_matmul_os_biasonly_smallik8_128_cross_1kb_interleaved",
                                        gemmini_manager_id,
                                        &pw_diag_interleaved_a, &pw_diag_interleaved_b,
                                        &pw_diag_interleaved_bias, &pw_diag_interleaved_c,
                                        (const elem_t *)pw_diag_a_dram, PW_DIAG_BYTES_A,
                                        (const elem_t *)pw_diag_b_dram, PW_DIAG_BYTES_B,
                                        (const acc_t *)pw_diag_bias_dram, PW_DIAG_BYTES_BIAS,
                                        (const elem_t *)pw_diag_init_c_dram, PW_DIAG_BYTES_C,
                                        (const elem_t *)pw_diag_gold_dram, PW_DIAG_ELEM_COUNT_C,
                                        PW_DIAG_I, PW_DIAG_J, PW_DIAG_K,
                                        PW_A_STRIDE, PW_B_STRIDE, PW_C_STRIDE,
                                        OS);
    case_pw_contig_os =
        run_pointwise_matmul_case("pointwise_matmul_os_cross_1kb_contiguous",
                                  gemmini_manager_id,
                                  &pw_contig_a, &pw_contig_b, &pw_contig_bias, &pw_contig_c,
                                  OS);
    case_pw_contig_ws =
        run_pointwise_matmul_case("pointwise_matmul_ws_cross_1kb_contiguous",
                                  gemmini_manager_id,
                                  &pw_contig_a, &pw_contig_b, &pw_contig_bias, &pw_contig_c,
                                  WS);
    case_pw_interleaved_os =
        run_pointwise_matmul_case("pointwise_matmul_os_cross_1kb_interleaved",
                                  gemmini_manager_id,
                                  &pw_interleaved_a, &pw_interleaved_b,
                                  &pw_interleaved_bias, &pw_interleaved_c,
                                  OS);
    case_pw_contig_os_j128 =
        run_pointwise_matmul_case_dim_j("pointwise_matmul_os_128_cross_1kb_contiguous",
                                        gemmini_manager_id,
                                        &pw_contig_a, &pw_contig_b,
                                        &pw_chunk_contig_bias, &pw_contig_c,
                                        (const acc_t *)pw_chunk_bias_dram, PW_CHUNK_BYTES_BIAS,
                                        (const elem_t *)pw_j128_gold_dram, PW_CHUNK_TILE_J,
                                        OS);
    case_pw_interleaved_os_j128 =
        run_pointwise_matmul_case_dim_j("pointwise_matmul_os_128_cross_1kb_interleaved",
                                        gemmini_manager_id,
                                        &pw_interleaved_a, &pw_interleaved_b,
                                        &pw_chunk_interleaved_bias, &pw_interleaved_c,
                                        (const acc_t *)pw_chunk_bias_dram, PW_CHUNK_BYTES_BIAS,
                                        (const elem_t *)pw_j128_gold_dram, PW_CHUNK_TILE_J,
                                        OS);
    case_pw_contig_os_j128_tjcap =
        run_pointwise_matmul_case_dim_j_explicit_tiles(
            "pointwise_matmul_os_128_tjcap_cross_1kb_contiguous",
            gemmini_manager_id,
            &pw_contig_a, &pw_contig_b,
            &pw_chunk_contig_bias, &pw_contig_c,
            (const acc_t *)pw_chunk_bias_dram, PW_CHUNK_BYTES_BIAS,
            (const elem_t *)pw_j128_gold_dram, PW_CHUNK_TILE_J,
            PW_OS_SAFE_TILE_I, PW_OS_SAFE_TILE_J, PW_OS_SAFE_TILE_K,
            OS);
    case_pw_interleaved_os_j128_tjcap =
        run_pointwise_matmul_case_dim_j_explicit_tiles(
            "pointwise_matmul_os_128_tjcap_cross_1kb_interleaved",
            gemmini_manager_id,
            &pw_interleaved_a, &pw_interleaved_b,
            &pw_chunk_interleaved_bias, &pw_interleaved_c,
            (const acc_t *)pw_chunk_bias_dram, PW_CHUNK_BYTES_BIAS,
            (const elem_t *)pw_j128_gold_dram, PW_CHUNK_TILE_J,
            PW_OS_SAFE_TILE_I, PW_OS_SAFE_TILE_J, PW_OS_SAFE_TILE_K,
            OS);
    case_pw_interleaved_ws =
        run_pointwise_matmul_case("pointwise_matmul_ws_cross_1kb_interleaved",
                                  gemmini_manager_id,
                                  &pw_interleaved_a, &pw_interleaved_b,
                                  &pw_interleaved_bias, &pw_interleaved_c,
                                  WS);
    case_pw_contig_os_chunked =
        run_pointwise_matmul_chunked_case("pointwise_matmul_os_chunked_128_to_64_cross_1kb_contiguous",
                                          gemmini_manager_id,
                                          &pw_contig_a, &pw_contig_b,
                                          &pw_chunk_contig_bias, &pw_contig_c,
                                          OS);
    case_pw_interleaved_os_chunked =
        run_pointwise_matmul_chunked_case("pointwise_matmul_os_chunked_128_to_64_cross_1kb_interleaved",
                                          gemmini_manager_id,
                                          &pw_interleaved_a, &pw_interleaved_b,
                                          &pw_chunk_interleaved_bias, &pw_interleaved_c,
                                          OS);
    case_pw_contig_os_chunked_runtime_style =
        run_pointwise_matmul_chunked_runtime_style_case(
            "pointwise_matmul_os_chunked_128_to_64_runtime_style_cross_1kb_contiguous",
            gemmini_manager_id,
            &pw_contig_a, &pw_contig_b,
            &pw_chunk_contig_bias, &pw_contig_c,
            OS);
    case_pw_interleaved_os_chunked_runtime_style =
        run_pointwise_matmul_chunked_runtime_style_case(
            "pointwise_matmul_os_chunked_128_to_64_runtime_style_cross_1kb_interleaved",
            gemmini_manager_id,
            &pw_interleaved_a, &pw_interleaved_b,
            &pw_chunk_interleaved_bias, &pw_interleaved_c,
            OS);
  }

  rr_release_all(RR_MAX_CFGS);

  if (case_contig_explicit && case_contig_standard_ws &&
      case_copy_a_mvin &&
      case_copy_a_interleaved_src_contig_dst_mvin &&
      case_copy_a_contig_src_interleaved_dst_mvin &&
      case_copy_b_mvin &&
      case_copy_b_mvin2 &&
      case_copy_b_mvin2_clean &&
      case_interleaved_explicit &&
      case_interleaved_explicit_serialized &&
      case_interleaved_standard_ws &&
      case_bias_first_mvin_contig &&
      case_bias_first_mvin_interleaved &&
      case_pw_diag_contig_os_bias_only &&
      case_pw_diag_interleaved_os_bias_only &&
      case_pw_contig_os &&
      case_pw_contig_ws &&
      case_pw_contig_os_j128 &&
      case_pw_contig_os_j128_tjcap &&
      case_pw_interleaved_os &&
      case_pw_interleaved_os_j128 &&
      case_pw_interleaved_os_j128_tjcap &&
      case_pw_interleaved_ws &&
      case_pw_contig_os_chunked &&
      case_pw_interleaved_os_chunked &&
      case_pw_contig_os_chunked_runtime_style &&
      case_pw_interleaved_os_chunked_runtime_style) {
    printf("ALL_TESTS_PASS\n");
    exit(0);
  }

  printf("ALL_TESTS_FAIL\n");
  exit(1);
}

int main(void) {
  return 1;
}
