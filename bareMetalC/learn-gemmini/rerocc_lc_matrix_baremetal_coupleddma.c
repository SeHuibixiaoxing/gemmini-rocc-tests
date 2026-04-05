#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "encoding.h"
#include "util.h"
#include "include/gemmini.h"
#include "include/rerocc_coupleddma.h"
#include "include/rerocc_gemmini_spm_xlate.h"
#include "include/gemmini_testutils.h"
#include "rerocc-linux-tests/rerocc_control.h"

#ifndef NUM_CORES
#define NUM_CORES 1
#endif

#ifndef REROCC_MAX_CORES
#define REROCC_MAX_CORES 8
#endif

#ifndef REROCC_LOGICAL_CORES
#define REROCC_LOGICAL_CORES 0
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

#ifndef REROCC_DMA_BYTES
#define REROCC_DMA_BYTES (64 * 1024)
#endif

#define REROCC_MATRIX_FULL 0
#define REROCC_MATRIX_DIAGONAL 1
#define REROCC_MATRIX_SINGLE 2

#ifndef REROCC_MATRIX_MODE
#define REROCC_MATRIX_MODE REROCC_MATRIX_FULL
#endif

#ifndef REROCC_DEBUG_CHECKPOINTS
#define REROCC_DEBUG_CHECKPOINTS 1
#endif

#ifndef REROCC_DEBUG_CORE
#define REROCC_DEBUG_CORE 0
#endif

#ifndef REROCC_DEBUG_CONVREF
#define REROCC_DEBUG_CONVREF 0
#endif

#ifndef REROCC_ACQUIRE_MAX_RETRIES
#define REROCC_ACQUIRE_MAX_RETRIES 1000000UL
#endif

#define GEMMINI_CFG_ID 0
#define DMA_CFG_ID 1

#define SHARED_SPAD_GLOBAL_ADDR_BASE 0x40000000ULL
#define SHARED_SPAD_LOCAL_SIZE (1024 * 1024ULL)
#define SHARED_SPAD_LOCAL_ADDR_BASE(i) (SHARED_SPAD_GLOBAL_ADDR_BASE + SHARED_SPAD_LOCAL_SIZE * (uint64_t)(i))
#define SHARED_SPAD_ALIGN_BYTES 64ULL
#define REROCC_SPM_PAGE_SHIFT 10U
#define REROCC_SPM_PAGE_BYTES (1ULL << REROCC_SPM_PAGE_SHIFT)
#define SHARED_SPAD_XLATE_RANGE_BASE 0xC0000000ULL
#define SHARED_SPAD_XLATE_RANGE_SIZE ((uint64_t)REROCC_NUM_GEMMINI * SHARED_SPAD_LOCAL_SIZE)
#define SHARED_SPAD_XLATE_PTE_CAP ((REROCC_NUM_GEMMINI * (SHARED_SPAD_LOCAL_SIZE / REROCC_SPM_PAGE_BYTES)) + 128U)

#ifndef BATCH_SIZE
#define BATCH_SIZE 1
#endif

#ifndef IN_ROW_DIM
#define IN_ROW_DIM 8
#endif

#ifndef IN_COL_DIM
#define IN_COL_DIM 8
#endif

#ifndef IN_CHANNELS
#define IN_CHANNELS 4
#endif

#ifndef OUT_CHANNELS
#define OUT_CHANNELS 4
#endif

#ifndef KERNEL_DIM
#define KERNEL_DIM 3
#endif

#ifndef PADDING
#define PADDING 1
#endif

#ifndef STRIDE
#define STRIDE 1
#endif

#define OUT_ROW_DIM ((IN_ROW_DIM + 2 * PADDING - KERNEL_DIM) / STRIDE + 1)
#define OUT_COL_DIM ((IN_COL_DIM + 2 * PADDING - KERNEL_DIM) / STRIDE + 1)
#define PATCH_SIZE (KERNEL_DIM * KERNEL_DIM * IN_CHANNELS)
#define N_PATCHES (BATCH_SIZE * OUT_ROW_DIM * OUT_COL_DIM)

static volatile int dma_complete_flag[REROCC_MAX_CORES] __attribute__((aligned(64)));
static uint8_t dma_src[REROCC_MAX_CORES][REROCC_DMA_BYTES] __attribute__((aligned(64)));
static uint8_t dma_dst[REROCC_MAX_CORES][REROCC_DMA_BYTES] __attribute__((aligned(64)));
static elem_t resadd_a_global[REROCC_MAX_CORES][DIM][DIM] __attribute__((aligned(64)));
static elem_t resadd_b_global[REROCC_MAX_CORES][DIM][DIM] __attribute__((aligned(64)));
static elem_t resadd_out_global[REROCC_MAX_CORES][DIM][DIM] __attribute__((aligned(64)));
static elem_t resadd_gold_global[REROCC_MAX_CORES][DIM][DIM] __attribute__((aligned(64)));
static volatile uint8_t gemmini_stage_debug[REROCC_MAX_CORES][REROCC_NUM_GEMMINI];
static volatile int gemmini_conv_debug[REROCC_MAX_CORES][REROCC_NUM_GEMMINI];
static volatile int gemmini_resadd_debug[REROCC_MAX_CORES][REROCC_NUM_GEMMINI];
static volatile int gemmini_shared_mv_debug[REROCC_MAX_CORES][REROCC_NUM_GEMMINI];
static volatile int gemmini_ok_debug[REROCC_MAX_CORES][REROCC_NUM_GEMMINI];
static volatile int cpu_conv_ref_debug_once = REROCC_DEBUG_CHECKPOINTS ? 1 : 0;
static volatile int core_done[REROCC_MAX_CORES];
static volatile int core_gemmini_pass[REROCC_MAX_CORES];
static volatile int core_gemmini_fail[REROCC_MAX_CORES];
static volatile int core_dma_pass[REROCC_MAX_CORES];
static volatile int core_dma_fail[REROCC_MAX_CORES];

enum {
  GEMDBG_STAGE_IDLE = 0,
  GEMDBG_STAGE_PREP = 1,
  GEMDBG_STAGE_ACQUIRE = 2,
  GEMDBG_STAGE_CONV = 3,
  GEMDBG_STAGE_RESADD = 4,
  GEMDBG_STAGE_SHARED_MV = 5,
  GEMDBG_STAGE_FENCE = 6,
  GEMDBG_STAGE_CHECK = 7,
  GEMDBG_STAGE_RELEASE = 8,
  GEMDBG_STAGE_DONE = 9,
};

static inline bool debug_this_core(int cid) {
  return REROCC_DEBUG_CHECKPOINTS && (REROCC_DEBUG_CORE < 0 || cid == REROCC_DEBUG_CORE);
}

static inline uint64_t read_cycles_local(void) {
  return read_csr(mcycle);
}

static inline uint64_t align_up_u64(uint64_t value, uint64_t align) {
  return ((value + align - 1) / align) * align;
}

static inline uint64_t shared_spad_case_bytes(void) {
  const uint64_t gemmini_bytes = (uint64_t)(DIM * DIM * sizeof(elem_t));
  const uint64_t dma_bytes = (uint64_t)REROCC_DMA_BYTES;
  const uint64_t needed = gemmini_bytes > dma_bytes ? gemmini_bytes : dma_bytes;
  return align_up_u64(needed, SHARED_SPAD_ALIGN_BYTES);
}

static inline uint64_t shared_spad_core_stride_bytes(void) {
  return 2ULL * shared_spad_case_bytes();
}

static inline uint64_t shared_spad_case_addr(int local_gid, int cid, int case_idx) {
  return SHARED_SPAD_LOCAL_ADDR_BASE(local_gid) +
    shared_spad_core_stride_bytes() * (uint64_t)cid +
    shared_spad_case_bytes() * (uint64_t)case_idx;
}

static inline bool shared_spad_layout_fits(int logical_cores) {
  return logical_cores > 0 &&
    (uint64_t)logical_cores * shared_spad_core_stride_bytes() <= SHARED_SPAD_LOCAL_SIZE;
}

static uint64_t spm_xlate_pte[SHARED_SPAD_XLATE_PTE_CAP] __attribute__((aligned(64)));

static inline uint64_t shared_spad_case_vaddr(int local_gid, int cid, int case_idx) {
  return SHARED_SPAD_XLATE_RANGE_BASE +
    (shared_spad_case_addr(local_gid, cid, case_idx) - SHARED_SPAD_GLOBAL_ADDR_BASE);
}

static void spm_xlate_table_clear(void) {
  memset(spm_xlate_pte, 0, sizeof(spm_xlate_pte));
}

static void spm_xlate_map_page(uint64_t vaddr, uint64_t paddr) {
  uint64_t vpage = (vaddr - SHARED_SPAD_XLATE_RANGE_BASE) >> REROCC_SPM_PAGE_SHIFT;
  if (vpage < (uint64_t)SHARED_SPAD_XLATE_PTE_CAP) {
    spm_xlate_pte[vpage] = ((paddr >> REROCC_SPM_PAGE_SHIFT) << 1) | 1ULL;
  }
}

static void spm_xlate_map_range(uint64_t vaddr, uint64_t paddr, uint64_t bytes) {
  const uint64_t page_mask = REROCC_SPM_PAGE_BYTES - 1ULL;
  uint64_t cur_vaddr;
  uint64_t cur_paddr;
  uint64_t end_vaddr;

  if (bytes == 0) {
    return;
  }

  // Map every page touched by [vaddr, vaddr + bytes), even when the range is not page-aligned.
  cur_vaddr = vaddr & ~page_mask;
  cur_paddr = paddr & ~page_mask;
  end_vaddr = (vaddr + bytes - 1ULL) & ~page_mask;

  while (1) {
    spm_xlate_map_page(cur_vaddr, cur_paddr);
    if (cur_vaddr == end_vaddr) {
      break;
    }
    cur_vaddr += REROCC_SPM_PAGE_BYTES;
    cur_paddr += REROCC_SPM_PAGE_BYTES;
  }
}

static inline void spm_xlate_program(uint64_t range_base, uint64_t range_size, bool enable) {
  rerocc_gemmini_spm_xlate_cfg((uint64_t)(uintptr_t)spm_xlate_pte,
                               (uint32_t)SHARED_SPAD_XLATE_PTE_CAP,
                               REROCC_SPM_PAGE_SHIFT,
                               enable ? 1U : 0U);
  rerocc_gemmini_spm_xlate_range(range_base, range_size);
}

static inline void spm_xlate_reset(void) {
  rerocc_gemmini_spm_xlate_cfg(0ULL, 0U, REROCC_SPM_PAGE_SHIFT, 0U);
  rerocc_gemmini_spm_xlate_range(0ULL, 0ULL);
  rerocc_gemmini_spm_xlate_flush();
}

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

static inline void dma_fence_wait(volatile int *completion_flag) {
  uint64_t status = 0;
  asm volatile("fence");
  status = rerocc_coupleddma_wait();
  (void)status;
  if (*completion_flag != 0) {
    *completion_flag = 0;
  }
}

static const char *matrix_mode_name(int mode) {
  if (mode == REROCC_MATRIX_FULL) return "full";
  if (mode == REROCC_MATRIX_DIAGONAL) return "diagonal";
  if (mode == REROCC_MATRIX_SINGLE) return "single";
  return "unknown";
}

static bool should_run_pair(int mode, int cid, int mid, int n_mgrs) {
  if (mode == REROCC_MATRIX_FULL) {
    return true;
  }
  if (mode == REROCC_MATRIX_DIAGONAL) {
    return mid == (cid % n_mgrs);
  }
  if (mode == REROCC_MATRIX_SINGLE) {
    return cid == 0 && mid == 0;
  }
  return false;
}

static int selected_pairs_for_cpu(int mode, int cid, int n_mgrs) {
  if (mode == REROCC_MATRIX_FULL) {
    return n_mgrs;
  }
  if (mode == REROCC_MATRIX_DIAGONAL) {
    return n_mgrs > 0 ? 1 : 0;
  }
  if (mode == REROCC_MATRIX_SINGLE) {
    return cid == 0 ? 1 : 0;
  }
  return 0;
}

static int selected_pairs(int mode, int n_cores, int n_mgrs) {
  int total = 0;
  for (int cid = 0; cid < n_cores; cid++) {
    total += selected_pairs_for_cpu(mode, cid, n_mgrs);
  }
  return total;
}

static void init_random_elem(elem_t *buf, size_t n, uint32_t *state) {
  for (size_t i = 0; i < n; i++) {
    buf[i] = (elem_t)((int32_t)(lcg_next(state) % 5) - 2);
  }
}

static void init_random_acc(acc_t *buf, size_t n, uint32_t *state) {
  for (size_t i = 0; i < n; i++) {
    buf[i] = (acc_t)((int32_t)(lcg_next(state) % 5) - 2);
  }
}

static void flatten_weights(elem_t weights[OUT_CHANNELS][KERNEL_DIM][KERNEL_DIM][IN_CHANNELS],
                            elem_t weights_mat[PATCH_SIZE][OUT_CHANNELS]) {
  for (int outc = 0; outc < OUT_CHANNELS; outc++) {
    for (int krow = 0; krow < KERNEL_DIM; krow++) {
      for (int kcol = 0; kcol < KERNEL_DIM; kcol++) {
        for (int inc = 0; inc < IN_CHANNELS; inc++) {
          int row = krow * KERNEL_DIM * IN_CHANNELS + kcol * IN_CHANNELS + inc;
          weights_mat[row][outc] = weights[outc][krow][kcol][inc];
        }
      }
    }
  }
}

static void cpu_conv_reference(elem_t input[BATCH_SIZE][IN_ROW_DIM][IN_COL_DIM][IN_CHANNELS],
                               elem_t weights[OUT_CHANNELS][KERNEL_DIM][KERNEL_DIM][IN_CHANNELS],
                               acc_t bias[OUT_CHANNELS],
                               elem_t reference[BATCH_SIZE][OUT_ROW_DIM][OUT_COL_DIM][OUT_CHANNELS],
                               bool debug_once) {
  if (debug_once) {
    printf("CHK convref enter\n");
  }
  for (int b = 0; b < BATCH_SIZE; b++) {
    for (int orow = 0; orow < OUT_ROW_DIM; orow++) {
      if (debug_once && (orow == 0 || orow == (OUT_ROW_DIM / 2) || orow == (OUT_ROW_DIM - 1))) {
        printf("CHK convref orow=%d\n", orow);
      }
      for (int ocol = 0; ocol < OUT_COL_DIM; ocol++) {
        for (int och = 0; och < OUT_CHANNELS; och++) {
          acc_t acc = bias[och];
          for (int krow = 0; krow < KERNEL_DIM; krow++) {
            for (int kcol = 0; kcol < KERNEL_DIM; kcol++) {
              for (int ich = 0; ich < IN_CHANNELS; ich++) {
                int irow = orow * STRIDE + krow - PADDING;
                int icol = ocol * STRIDE + kcol - PADDING;
                elem_t px = (irow < 0 || irow >= IN_ROW_DIM || icol < 0 || icol >= IN_COL_DIM) ?
                  (elem_t)0 : input[b][irow][icol][ich];
                acc += (acc_t)weights[och][krow][kcol][ich] * (acc_t)px;
              }
            }
          }

          if (acc > elem_t_max) {
            acc = elem_t_max;
          } else if (acc < elem_t_min) {
            acc = elem_t_min;
          }
          reference[b][orow][ocol][och] = (elem_t)acc;
        }
      }
    }
  }
  if (debug_once) {
    printf("CHK convref done\n");
  }
}

static bool output_matches_reference(elem_t reference[BATCH_SIZE][OUT_ROW_DIM][OUT_COL_DIM][OUT_CHANNELS],
                                     elem_t output[N_PATCHES][OUT_CHANNELS]) {
  const elem_t *ref = &reference[0][0][0][0];
  const elem_t *out = &output[0][0];
  size_t n = (size_t)BATCH_SIZE * OUT_ROW_DIM * OUT_COL_DIM * OUT_CHANNELS;
  for (size_t i = 0; i < n; i++) {
    if (ref[i] != out[i]) {
      return false;
    }
  }
  return true;
}

static bool run_one_gemmini_case(int cid, int manager_id) {
  const bool debug = debug_this_core(cid);
  const int local_gid = manager_id - REROCC_GEMMINI_BASE_ID;
  elem_t input[BATCH_SIZE][IN_ROW_DIM][IN_COL_DIM][IN_CHANNELS] __attribute__((aligned(64)));
  elem_t weights[OUT_CHANNELS][KERNEL_DIM][KERNEL_DIM][IN_CHANNELS] __attribute__((aligned(64)));
  acc_t bias[OUT_CHANNELS] __attribute__((aligned(64)));
  elem_t reference[BATCH_SIZE][OUT_ROW_DIM][OUT_COL_DIM][OUT_CHANNELS] __attribute__((aligned(64)));
  elem_t weights_mat[PATCH_SIZE][OUT_CHANNELS] __attribute__((aligned(64)));
  elem_t output_mat[N_PATCHES][OUT_CHANNELS] __attribute__((aligned(64)));
  elem_t (*resadd_a)[DIM] = resadd_a_global[cid];
  elem_t (*resadd_b)[DIM] = resadd_b_global[cid];
  elem_t (*resadd_out)[DIM] = resadd_out_global[cid];
  elem_t (*resadd_gold)[DIM] = resadd_gold_global[cid];

  uint32_t state = (uint32_t)(0x1234567u ^ (cid * 131u) ^ (manager_id * 977u));

  gemmini_stage_debug[cid][local_gid] = GEMDBG_STAGE_PREP;
  gemmini_conv_debug[cid][local_gid] = -1;
  if (debug && local_gid == 0) {
    printf("CHK gemmini0 enter\n");
  }
  gemmini_resadd_debug[cid][local_gid] = -1;
  gemmini_shared_mv_debug[cid][local_gid] = -1;
  gemmini_ok_debug[cid][local_gid] = -1;

  if (debug) {
    printf("[dbg][gemmini] cpu=%d mgr=%d before cpu_conv_reference_prep\n", cid, manager_id);
  }
  init_random_elem(&input[0][0][0][0], sizeof(input) / sizeof(elem_t), &state);
  if (debug && local_gid == 0) {
    printf("CHK gemmini0 input_init\n");
  }
  init_random_elem(&weights[0][0][0][0], sizeof(weights) / sizeof(elem_t), &state);
  if (debug && local_gid == 0) {
    printf("CHK gemmini0 weights_init\n");
  }
  init_random_acc(&bias[0], sizeof(bias) / sizeof(acc_t), &state);
  if (debug && local_gid == 0) {
    printf("CHK gemmini0 bias_init\n");
  }
  flatten_weights(weights, weights_mat);
  const bool convref_debug =
    debug && local_gid == 0 && (REROCC_DEBUG_CONVREF != 0) && cpu_conv_ref_debug_once != 0;
  if (debug && local_gid == 0) {
    printf("CHK gemmini0 flatten_done\n");
    printf("CHK gemmini0 convref_call\n");
  }
  cpu_conv_reference(input, weights, bias, reference, convref_debug);
  if (convref_debug) {
    cpu_conv_ref_debug_once = 0;
  }
  if (debug && local_gid == 0) {
    printf("CHK gemmini0 prepared\n");
  }
  if (debug) {
    printf("[dbg][gemmini] cpu=%d mgr=%d after cpu_conv_reference_prep\n", cid, manager_id);
  }

  for (size_t i = 0; i < (sizeof(output_mat) / sizeof(elem_t)); i++) {
    (&output_mat[0][0])[i] = 0;
  }

  if (debug) {
    printf("[dbg][gemmini] cpu=%d mgr=%d before rr_acquire\n", cid, manager_id);
  }
  gemmini_stage_debug[cid][local_gid] = GEMDBG_STAGE_ACQUIRE;
  if (debug && local_gid == 0) {
    printf("CHK gemmini0 before_acquire\n");
  }
  if (!rr_acquire_cfg_with_retry(GEMMINI_CFG_ID, (uint64_t)manager_id)) {
    if (debug) {
      printf("[dbg][gemmini] cpu=%d mgr=%d rr_acquire FAIL\n", cid, manager_id);
    }
    return false;
  }
  if (debug && local_gid == 0) {
    printf("CHK gemmini0 after_acquire\n");
  }
  if (debug) {
    printf("[dbg][gemmini] cpu=%d mgr=%d after rr_acquire\n", cid, manager_id);
  }

  if (debug) {
    printf("[dbg][gemmini] cpu=%d mgr=%d before rr_set_opc\n", cid, manager_id);
  }
  rr_set_opc(3, GEMMINI_CFG_ID);
  if (debug) {
    printf("[dbg][gemmini] cpu=%d mgr=%d after rr_set_opc\n", cid, manager_id);
    printf("[dbg][gemmini] cpu=%d mgr=%d before gemmini_flush\n", cid, manager_id);
  }
  gemmini_flush(0);
  if (debug) {
    printf("[dbg][gemmini] cpu=%d mgr=%d after gemmini_flush\n", cid, manager_id);
  }
  if (debug) {
    printf("[dbg][gemmini] cpu=%d mgr=%d before tiled_conv_auto\n", cid, manager_id);
  }
  gemmini_stage_debug[cid][local_gid] = GEMDBG_STAGE_CONV;
  tiled_conv_auto(
    BATCH_SIZE, IN_ROW_DIM, IN_COL_DIM, IN_CHANNELS,
    OUT_CHANNELS, OUT_ROW_DIM, OUT_COL_DIM,
    STRIDE, 1, 1, PADDING, KERNEL_DIM,
    false, false, false, false, false,
    (elem_t *)input,
    (elem_t *)weights_mat,
    (acc_t *)bias,
    (elem_t *)output_mat,
    NO_ACTIVATION, ACC_SCALE_IDENTITY, 0, 0, 0,
    WS
  );
  if (debug) {
    printf("[dbg][gemmini] cpu=%d mgr=%d after tiled_conv_auto\n", cid, manager_id);
  }

  // Drain the acquired Gemmini manager before flushing/reusing accumulator rows.
  rr_fence(GEMMINI_CFG_ID);

  // resadd reuses accumulator rows; flush state left by conv to get deterministic checks.
  if (debug) {
    printf("[dbg][gemmini] cpu=%d mgr=%d before resadd_flush\n", cid, manager_id);
  }
  gemmini_flush(0);
  rr_fence(GEMMINI_CFG_ID);
  if (debug) {
    printf("[dbg][gemmini] cpu=%d mgr=%d after resadd_flush\n", cid, manager_id);
  }

  for (size_t i = 0; i < (size_t)(DIM * DIM); i++) {
    ((elem_t *)resadd_a)[i] = (elem_t)((int32_t)(lcg_next(&state) % 7) - 3);
    ((elem_t *)resadd_b)[i] = (elem_t)((int32_t)(lcg_next(&state) % 7) - 3);
    ((elem_t *)resadd_out)[i] = 0;
    ((elem_t *)resadd_gold)[i] = 0;
  }

  resadd_cpu(DIM, DIM, DIM, MVIN_SCALE_IDENTITY, MVIN_SCALE_IDENTITY, ACC_SCALE_IDENTITY,
    (elem_t *)resadd_a, (elem_t *)resadd_b, (elem_t *)resadd_gold, false);
  if (debug) {
    printf("[dbg][gemmini] cpu=%d mgr=%d before tiled_resadd_auto\n", cid, manager_id);
  }
  gemmini_stage_debug[cid][local_gid] = GEMDBG_STAGE_RESADD;
  tiled_resadd_auto(DIM, DIM, MVIN_SCALE_IDENTITY, MVIN_SCALE_IDENTITY, ACC_SCALE_IDENTITY,
    (elem_t *)resadd_a, (elem_t *)resadd_b, (elem_t *)resadd_out, false, WS);
  if (debug) {
    printf("[dbg][gemmini] cpu=%d mgr=%d after tiled_resadd_auto\n", cid, manager_id);
    printf("[dbg][gemmini] cpu=%d mgr=%d before resadd_fence\n", cid, manager_id);
  }
  rr_fence(GEMMINI_CFG_ID);
  if (debug) {
    printf("[dbg][gemmini] cpu=%d mgr=%d after resadd_fence\n", cid, manager_id);
  }

  bool resadd_match = true;
  size_t resadd_mismatch_idx = 0;
  for (size_t i = 0; i < (size_t)(DIM * DIM); i++) {
    if (((elem_t *)resadd_out)[i] != ((elem_t *)resadd_gold)[i]) {
      resadd_match = false;
      resadd_mismatch_idx = i;
      break;
    }
  }
  bool resadd_ok = resadd_match;
  if (debug && !resadd_ok) {
    printf("[dbg][gemmini] cpu=%d mgr=%d resadd mismatch idx=%lu out=%d gold=%d\n",
      cid, manager_id, (unsigned long)resadd_mismatch_idx,
      (int)((elem_t *)resadd_out)[resadd_mismatch_idx],
      (int)((elem_t *)resadd_gold)[resadd_mismatch_idx]);
  }

  uint64_t shared_src_addr = shared_spad_case_addr(local_gid, cid, 0);
  uint64_t shared_dst_addr = shared_spad_case_addr(local_gid, cid, 1);
  uint64_t shared_src_vaddr = shared_spad_case_vaddr(local_gid, cid, 0);
  uint64_t shared_dst_vaddr = shared_spad_case_vaddr(local_gid, cid, 1);
  elem_t *shared_src = (elem_t *)(uintptr_t)shared_src_addr;
  elem_t *shared_dst = (elem_t *)(uintptr_t)shared_dst_addr;
  elem_t *shared_src_va = (elem_t *)(uintptr_t)shared_src_vaddr;
  elem_t *shared_dst_va = (elem_t *)(uintptr_t)shared_dst_vaddr;
  for (size_t i = 0; i < (size_t)(DIM * DIM); i++) {
    shared_src[i] = (elem_t)((int32_t)(lcg_next(&state) % 9) - 4);
    shared_dst[i] = 0;
  }

  gemmini_config_ld(DIM * sizeof(elem_t));
  gemmini_config_st(DIM * sizeof(elem_t));
  gemmini_stage_debug[cid][local_gid] = GEMDBG_STAGE_SHARED_MV;

  spm_xlate_table_clear();
  spm_xlate_map_range(shared_src_vaddr, shared_src_addr, (uint64_t)(DIM * DIM * sizeof(elem_t)));
  spm_xlate_map_range(shared_dst_vaddr, shared_dst_addr, (uint64_t)(DIM * DIM * sizeof(elem_t)));
  spm_xlate_program(SHARED_SPAD_XLATE_RANGE_BASE, SHARED_SPAD_XLATE_RANGE_SIZE, true);
  gemmini_mvin(shared_src_va, 0);
  gemmini_mvout(shared_dst_va, 0);
  if (debug) {
    printf("[dbg][gemmini] cpu=%d mgr=%d before rr_fence shared_xlate\n", cid, manager_id);
  }
  gemmini_stage_debug[cid][local_gid] = GEMDBG_STAGE_FENCE;
  rr_fence(GEMMINI_CFG_ID);
  if (debug) {
    printf("[dbg][gemmini] cpu=%d mgr=%d after rr_fence shared_xlate\n", cid, manager_id);
  }

  bool shared_mv_xlate_ok = true;
  for (size_t i = 0; i < (size_t)(DIM * DIM); i++) {
    if (shared_src[i] != shared_dst[i]) {
      shared_mv_xlate_ok = false;
      break;
    }
  }

  memset(shared_dst, 0, (size_t)(DIM * DIM * sizeof(elem_t)));
  spm_xlate_program(SHARED_SPAD_GLOBAL_ADDR_BASE, SHARED_SPAD_XLATE_RANGE_SIZE, false);
  gemmini_mvin(shared_src, 0);
  gemmini_mvout(shared_dst, 0);
  if (debug) {
    printf("[dbg][gemmini] cpu=%d mgr=%d before rr_fence shared_passthrough\n", cid, manager_id);
  }
  rr_fence(GEMMINI_CFG_ID);
  if (debug) {
    printf("[dbg][gemmini] cpu=%d mgr=%d after rr_fence shared_passthrough\n", cid, manager_id);
  }

  bool shared_mv_passthrough_ok = true;
  for (size_t i = 0; i < (size_t)(DIM * DIM); i++) {
    if (shared_src[i] != shared_dst[i]) {
      shared_mv_passthrough_ok = false;
      break;
    }
  }
  spm_xlate_reset();

  bool shared_mv_ok = shared_mv_xlate_ok && shared_mv_passthrough_ok;
  bool conv_ok = output_matches_reference(reference, output_mat);
  bool ok = conv_ok && resadd_ok && shared_mv_ok;
  gemmini_stage_debug[cid][local_gid] = GEMDBG_STAGE_CHECK;
  gemmini_conv_debug[cid][local_gid] = conv_ok ? 1 : 0;
  gemmini_resadd_debug[cid][local_gid] = resadd_ok ? 1 : 0;
  gemmini_shared_mv_debug[cid][local_gid] = shared_mv_ok ? 1 : 0;
  gemmini_ok_debug[cid][local_gid] = ok ? 1 : 0;
  if (debug) {
    printf("[dbg][gemmini] cpu=%d mgr=%d conv=%d resadd=%d shared_mv=%d\n",
      cid, manager_id, conv_ok ? 1 : 0, resadd_ok ? 1 : 0, shared_mv_ok ? 1 : 0);
  }
  if (debug && !ok) {
    printf("[dbg][gemmini] cpu=%d mgr=%d check output=%d resadd_match=%d shared_mv=%d\n",
      cid, manager_id, conv_ok ? 1 : 0,
      resadd_match ? 1 : 0, shared_mv_ok ? 1 : 0);
  }
  if (debug) {
    printf("[dbg][gemmini] cpu=%d mgr=%d before rr_release\n", cid, manager_id);
  }
  gemmini_stage_debug[cid][local_gid] = GEMDBG_STAGE_RELEASE;
  rr_release(GEMMINI_CFG_ID);
  if (debug) {
    printf("[dbg][gemmini] cpu=%d mgr=%d after rr_release ok=%d\n", cid, manager_id, ok ? 1 : 0);
  }
  gemmini_stage_debug[cid][local_gid] = GEMDBG_STAGE_DONE;
  return ok;
}

static void fill_pattern(uint8_t *buf, size_t n, int cid, int manager_id, int round) {
  uint32_t state = (uint32_t)(0x9e3779b9u ^ (cid * 97u) ^ (manager_id * 313u) ^ (round * 131u));
  for (size_t i = 0; i < n; i++) {
    buf[i] = (uint8_t)lcg_next(&state);
  }
}

static void clear_buffer(uint8_t *buf, size_t n) {
  for (size_t i = 0; i < n; i++) {
    buf[i] = 0;
  }
}

static bool buffers_equal(const uint8_t *a, const uint8_t *b, size_t n) {
  for (size_t i = 0; i < n; i++) {
    if (a[i] != b[i]) {
      return false;
    }
  }
  return true;
}

static bool run_one_dma_case(int cid, int manager_id) {
  const bool debug = debug_this_core(cid);
  uint8_t *src = dma_src[cid];
  uint8_t *dst = dma_dst[cid];
  int local_gid = manager_id - REROCC_DMA_BASE_ID + REROCC_GEMMINI_BASE_ID;
  uint8_t *shared_a = (uint8_t *)(uintptr_t)shared_spad_case_addr(local_gid, cid, 0);
  uint8_t *shared_b = (uint8_t *)(uintptr_t)shared_spad_case_addr(local_gid, cid, 1);
  volatile int *completion_flag = &dma_complete_flag[cid];
  *completion_flag = 0;

  if (debug) {
    printf("[dbg][dma] cpu=%d mgr=%d before rr_acquire\n", cid, manager_id);
  }
  if (!rr_acquire_cfg_with_retry(DMA_CFG_ID, (uint64_t)manager_id)) {
    if (debug) {
      printf("[dbg][dma] cpu=%d mgr=%d rr_acquire FAIL\n", cid, manager_id);
    }
    return false;
  }
  if (debug) {
    printf("[dbg][dma] cpu=%d mgr=%d after rr_acquire\n", cid, manager_id);
  }

  if (debug) {
    printf("[dbg][dma] cpu=%d mgr=%d before rr_set_opc\n", cid, manager_id);
  }
  rr_set_opc(2, DMA_CFG_ID);
  if (debug) {
    printf("[dbg][dma] cpu=%d mgr=%d after rr_set_opc\n", cid, manager_id);
  }

  fill_pattern(src, REROCC_DMA_BYTES, cid, manager_id, 0);
  clear_buffer(shared_a, REROCC_DMA_BYTES);
  *completion_flag = 0;
  rerocc_coupleddma_set_dst((uint64_t)(uintptr_t)shared_a, (uint64_t)(uintptr_t)completion_flag);
  rerocc_coupleddma_set_src((uint64_t)(uintptr_t)src, (uint64_t)REROCC_DMA_BYTES);
  if (debug) {
    printf("[dbg][dma] cpu=%d mgr=%d after issue dram_to_shared\n", cid, manager_id);
  }
  dma_fence_wait(completion_flag);
  if (debug) {
    printf("[dbg][dma] cpu=%d mgr=%d after wait dram_to_shared\n", cid, manager_id);
  }
  rr_fence(DMA_CFG_ID);
  if (debug) {
    printf("[dbg][dma] cpu=%d mgr=%d after rr_fence dram_to_shared\n", cid, manager_id);
  }
  if (!buffers_equal(src, shared_a, REROCC_DMA_BYTES)) {
    if (debug) {
      printf("[dbg][dma] cpu=%d mgr=%d compare FAIL dram_to_shared\n", cid, manager_id);
    }
    rr_release(DMA_CFG_ID);
    return false;
  }
  if (debug) {
    printf("[dbg][dma] cpu=%d mgr=%d compare PASS dram_to_shared\n", cid, manager_id);
  }

  fill_pattern(shared_a, REROCC_DMA_BYTES, cid, manager_id, 1);
  clear_buffer(dst, REROCC_DMA_BYTES);
  *completion_flag = 0;
  rerocc_coupleddma_set_dst((uint64_t)(uintptr_t)dst, (uint64_t)(uintptr_t)completion_flag);
  rerocc_coupleddma_set_src((uint64_t)(uintptr_t)shared_a, (uint64_t)REROCC_DMA_BYTES);
  if (debug) {
    printf("[dbg][dma] cpu=%d mgr=%d after issue shared_to_dram\n", cid, manager_id);
  }
  dma_fence_wait(completion_flag);
  if (debug) {
    printf("[dbg][dma] cpu=%d mgr=%d after wait shared_to_dram\n", cid, manager_id);
  }
  rr_fence(DMA_CFG_ID);
  if (debug) {
    printf("[dbg][dma] cpu=%d mgr=%d after rr_fence shared_to_dram\n", cid, manager_id);
  }
  if (!buffers_equal(shared_a, dst, REROCC_DMA_BYTES)) {
    if (debug) {
      printf("[dbg][dma] cpu=%d mgr=%d compare FAIL shared_to_dram\n", cid, manager_id);
    }
    rr_release(DMA_CFG_ID);
    return false;
  }
  if (debug) {
    printf("[dbg][dma] cpu=%d mgr=%d compare PASS shared_to_dram\n", cid, manager_id);
  }

  fill_pattern(shared_a, REROCC_DMA_BYTES, cid, manager_id, 2);
  clear_buffer(shared_b, REROCC_DMA_BYTES);
  *completion_flag = 0;
  rerocc_coupleddma_set_dst((uint64_t)(uintptr_t)shared_b, (uint64_t)(uintptr_t)completion_flag);
  rerocc_coupleddma_set_src((uint64_t)(uintptr_t)shared_a, (uint64_t)REROCC_DMA_BYTES);
  if (debug) {
    printf("[dbg][dma] cpu=%d mgr=%d after issue shared_to_shared\n", cid, manager_id);
  }
  dma_fence_wait(completion_flag);
  if (debug) {
    printf("[dbg][dma] cpu=%d mgr=%d after wait shared_to_shared\n", cid, manager_id);
  }
  rr_fence(DMA_CFG_ID);
  if (debug) {
    printf("[dbg][dma] cpu=%d mgr=%d after rr_fence shared_to_shared\n", cid, manager_id);
  }
  if (!buffers_equal(shared_a, shared_b, REROCC_DMA_BYTES)) {
    if (debug) {
      printf("[dbg][dma] cpu=%d mgr=%d compare FAIL shared_to_shared\n", cid, manager_id);
    }
    rr_release(DMA_CFG_ID);
    return false;
  }
  if (debug) {
    printf("[dbg][dma] cpu=%d mgr=%d compare PASS shared_to_shared\n", cid, manager_id);
  }

  if (debug) {
    printf("[dbg][dma] cpu=%d mgr=%d before rr_release\n", cid, manager_id);
  }
  rr_release(DMA_CFG_ID);
  if (debug) {
    printf("[dbg][dma] cpu=%d mgr=%d after rr_release\n", cid, manager_id);
  }
  return true;
}

static void wait_for_logical_cores(int logical_cores) {
  while (1) {
    int done = 0;
    for (int i = 0; i < logical_cores; i++) {
      done += core_done[i];
    }
    if (done == logical_cores) {
      break;
    }
  }
  __sync_synchronize();
}

void thread_entry(int cid, int nc) {
  const int requested_logical_cores = REROCC_LOGICAL_CORES;
  const int logical_cores = requested_logical_cores > 0 ? requested_logical_cores : nc;

  if (cid == 0) {
    printf("[rerocc-baremetal] start mode=%s logical_cores=%d requested_logical_cores=%d runtime_nc=%d gemmini=%d dma=%d gemmini_base=%d dma_base=%d dma_bytes=%d\n",
      matrix_mode_name(REROCC_MATRIX_MODE), logical_cores, requested_logical_cores, nc, REROCC_NUM_GEMMINI, REROCC_NUM_DMA,
      REROCC_GEMMINI_BASE_ID, REROCC_DMA_BASE_ID, REROCC_DMA_BYTES);
    if (debug_this_core(cid)) {
      printf("CHK thread_entry after_start\n");
    }
  }

  if (logical_cores <= 0 || logical_cores > REROCC_MAX_CORES) {
    if (cid == 0) {
      printf("[rerocc-baremetal] FAIL: logical_cores=%d out of range (1..%d)\n",
        logical_cores, REROCC_MAX_CORES);
      exit(1);
    }
    while (1) {
      asm volatile("wfi");
    }
  }

  if (logical_cores > nc) {
    if (cid == 0) {
      printf("[rerocc-baremetal] FAIL: logical_cores=%d exceeds runtime_nc=%d\n", logical_cores, nc);
      exit(1);
    }
    while (1) {
      asm volatile("wfi");
    }
  }

  if (!shared_spad_layout_fits(logical_cores)) {
    if (cid == 0) {
      printf("[rerocc-baremetal] FAIL: shared_spad layout overflow logical_cores=%d case_bytes=%lu stride=%lu local_size=%lu\n",
        logical_cores,
        (unsigned long)shared_spad_case_bytes(),
        (unsigned long)shared_spad_core_stride_bytes(),
        (unsigned long)SHARED_SPAD_LOCAL_SIZE);
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

  int gemmini_pass_local = 0;
  int gemmini_fail_local = 0;
  int dma_pass_local = 0;
  int dma_fail_local = 0;

  for (int gid = 0; gid < REROCC_NUM_GEMMINI; gid++) {
    if (!should_run_pair(REROCC_MATRIX_MODE, cid, gid, REROCC_NUM_GEMMINI)) {
      continue;
    }
    int manager_id = REROCC_GEMMINI_BASE_ID + gid;
    uint64_t t0 = read_cycles_local();
    bool ok = run_one_gemmini_case(cid, manager_id);
    uint64_t t1 = read_cycles_local();
    if (ok) {
      gemmini_pass_local++;
    } else {
      gemmini_fail_local++;
    }
    if (debug_this_core(cid)) {
      printf("[gemmini] cpu=%d mgr=%d %s cycles=%lu\n", cid, manager_id, ok ? "PASS" : "FAIL",
        (unsigned long)(t1 - t0));
    }
  }

  for (int did = 0; did < REROCC_NUM_DMA; did++) {
    if (!should_run_pair(REROCC_MATRIX_MODE, cid, did, REROCC_NUM_DMA)) {
      continue;
    }
    int manager_id = REROCC_DMA_BASE_ID + did;
    uint64_t t0 = read_cycles_local();
    bool ok = run_one_dma_case(cid, manager_id);
    uint64_t t1 = read_cycles_local();
    if (ok) {
      dma_pass_local++;
    } else {
      dma_fail_local++;
    }
    if (debug_this_core(cid)) {
      printf("[dma] cpu=%d mgr=%d %s cycles=%lu\n", cid, manager_id, ok ? "PASS" : "FAIL",
        (unsigned long)(t1 - t0));
    }
  }

  core_gemmini_pass[cid] = gemmini_pass_local;
  core_gemmini_fail[cid] = gemmini_fail_local;
  core_dma_pass[cid] = dma_pass_local;
  core_dma_fail[cid] = dma_fail_local;
  __sync_synchronize();
  core_done[cid] = 1;
  __sync_synchronize();

  if (cid != 0) {
    while (1) {
      asm volatile("wfi");
    }
  }

  wait_for_logical_cores(logical_cores);

  rr_release_all(RR_MAX_CFGS);

  int gemmini_pass = 0;
  int gemmini_fail = 0;
  int dma_pass = 0;
  int dma_fail = 0;

  for (int i = 0; i < logical_cores; i++) {
    printf("CORE_RESULT cid=%d gemmini_pass=%d gemmini_fail=%d dma_pass=%d dma_fail=%d\n",
      i, core_gemmini_pass[i], core_gemmini_fail[i], core_dma_pass[i], core_dma_fail[i]);
    for (int gid = 0; gid < REROCC_NUM_GEMMINI; gid++) {
      if (!should_run_pair(REROCC_MATRIX_MODE, i, gid, REROCC_NUM_GEMMINI)) {
        continue;
      }
      printf("GEMMINI_DEBUG cid=%d mgr=%d stage=%d conv=%d resadd=%d shared_mv=%d ok=%d\n",
        i, REROCC_GEMMINI_BASE_ID + gid,
        gemmini_stage_debug[i][gid],
        gemmini_conv_debug[i][gid],
        gemmini_resadd_debug[i][gid],
        gemmini_shared_mv_debug[i][gid],
        gemmini_ok_debug[i][gid]);
    }
    gemmini_pass += core_gemmini_pass[i];
    gemmini_fail += core_gemmini_fail[i];
    dma_pass += core_dma_pass[i];
    dma_fail += core_dma_fail[i];
  }

  int gemmini_expected = selected_pairs(REROCC_MATRIX_MODE, logical_cores, REROCC_NUM_GEMMINI);
  int dma_expected = selected_pairs(REROCC_MATRIX_MODE, logical_cores, REROCC_NUM_DMA);

  printf("GEMMINI_MATRIX_RESULT mode=%s pass=%d fail=%d expected=%d\n",
    matrix_mode_name(REROCC_MATRIX_MODE), gemmini_pass, gemmini_fail, gemmini_expected);
  printf("DMA_MATRIX_RESULT mode=%s pass=%d fail=%d expected=%d bytes=%d\n",
    matrix_mode_name(REROCC_MATRIX_MODE), dma_pass, dma_fail, dma_expected, REROCC_DMA_BYTES);

  if (gemmini_fail == 0 && dma_fail == 0 &&
      gemmini_pass == gemmini_expected && dma_pass == dma_expected) {
    printf("ALL_TESTS_PASS\n");
    exit(0);
  }

  printf("ALL_TESTS_FAIL\n");
  exit(1);
}

int main(void) {
  return 1;
}
