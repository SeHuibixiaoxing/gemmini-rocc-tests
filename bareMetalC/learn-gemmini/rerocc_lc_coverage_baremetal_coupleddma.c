#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "encoding.h"
#include "util.h"
#include "include/gemmini.h"
#include "include/gemmini_testutils.h"
#include "include/rerocc_coupleddma.h"
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

#ifndef REROCC_PAIR_MANAGER_MODE
#define REROCC_PAIR_MANAGER_MODE 0
#endif

#ifndef REROCC_DMA_BASE_ID
#if REROCC_PAIR_MANAGER_MODE
#define REROCC_DMA_BASE_ID REROCC_GEMMINI_BASE_ID
#else
#define REROCC_DMA_BASE_ID REROCC_NUM_GEMMINI
#endif
#endif

#ifndef REROCC_TEST_LOCAL_GEMMINI_ID
#define REROCC_TEST_LOCAL_GEMMINI_ID 0
#endif

#ifndef REROCC_DMA_BYTES
#define REROCC_DMA_BYTES 1024
#endif

#ifndef REROCC_SPM_PAGE_BYTES
#define REROCC_SPM_PAGE_BYTES 1024U
#endif

#define REROCC_SPM_PAGE_SHIFT 10U
#define SHARED_SPAD_XLATE_RANGE_BASE 0xC0000000ULL
#define SHARED_SPAD_XLATE_BYTES_NEEDED \
  (SHARED_CONV_OUTPUT_OFFSET + ((uint64_t)DIM * (uint64_t)DIM * sizeof(elem_t)))
#define SHARED_SPAD_XLATE_RANGE_SIZE \
  (((SHARED_SPAD_XLATE_BYTES_NEEDED + REROCC_SPM_PAGE_BYTES - 1ULL) / REROCC_SPM_PAGE_BYTES) * \
   REROCC_SPM_PAGE_BYTES)
#define SHARED_SPAD_XLATE_PTE_CAP ((SHARED_SPAD_XLATE_RANGE_SIZE / REROCC_SPM_PAGE_BYTES) + 8U)

#ifndef REROCC_DMA_CROSS_BYTES
#define REROCC_DMA_CROSS_BYTES 256U
#endif

#ifndef REROCC_DMA_OFFSET_MAX
#define REROCC_DMA_OFFSET_MAX 64U
#endif

#ifndef REROCC_DMA_MISALIGNED_ENABLE
#define REROCC_DMA_MISALIGNED_ENABLE 0
#endif

#ifndef REROCC_DMA_MISALIGNED_DRAM_SRC16_OFFSET
#define REROCC_DMA_MISALIGNED_DRAM_SRC16_OFFSET 16U
#endif

#ifndef REROCC_DMA_MISALIGNED_DRAM_SRC1_OFFSET
#define REROCC_DMA_MISALIGNED_DRAM_SRC1_OFFSET 1U
#endif

#ifndef REROCC_DMA_MISALIGNED_DRAM_DST16_OFFSET
#define REROCC_DMA_MISALIGNED_DRAM_DST16_OFFSET 16U
#endif

#ifndef REROCC_DMA_MISALIGNED_SHARED_SRC16_OFFSET
#define REROCC_DMA_MISALIGNED_SHARED_SRC16_OFFSET 16U
#endif

#ifndef REROCC_DMA_MISALIGNED_SHARED_DST48_OFFSET
#define REROCC_DMA_MISALIGNED_SHARED_DST48_OFFSET 48U
#endif

#ifndef REROCC_COVERAGE_SPM_XLATE_FAULT_READBACK
#if REROCC_PAIR_MANAGER_MODE
#define REROCC_COVERAGE_SPM_XLATE_FAULT_READBACK 0
#else
#define REROCC_COVERAGE_SPM_XLATE_FAULT_READBACK 1
#endif
#endif

#ifndef REROCC_COVERAGE_SIMPLE_CONV_FIXTURE
#define REROCC_COVERAGE_SIMPLE_CONV_FIXTURE 1
#endif

#ifndef REROCC_COVERAGE_SIMPLE_RESADD_FIXTURE
#define REROCC_COVERAGE_SIMPLE_RESADD_FIXTURE 1
#endif

#ifndef REROCC_COVERAGE_HEAVY_GEMMINI_CASES
#define REROCC_COVERAGE_HEAVY_GEMMINI_CASES 0
#endif

#ifndef REROCC_COVERAGE_BASELINE_CASES
#define REROCC_COVERAGE_BASELINE_CASES 0
#endif

#ifndef REROCC_COVERAGE_ALIGNED_DMA_VARIANTS
#define REROCC_COVERAGE_ALIGNED_DMA_VARIANTS 0
#endif

#ifndef REROCC_COVERAGE_SHARED_MV_CASES
#define REROCC_COVERAGE_SHARED_MV_CASES 0
#endif

#ifndef REROCC_COVERAGE_VERBOSE
#define REROCC_COVERAGE_VERBOSE 1
#endif

#if !REROCC_COVERAGE_VERBOSE
#define printf(...) ((int)0)
#endif

#ifndef REROCC_ACQUIRE_MAX_RETRIES
#define REROCC_ACQUIRE_MAX_RETRIES 1000000UL
#endif

#ifndef DMA_WAIT_SPINS
#define DMA_WAIT_SPINS 500000UL
#endif

#define GEMMINI_CFG_ID 0
#define DMA_CFG_ID 1

#define SHARED_SPAD_GLOBAL_ADDR_BASE 0x40000000ULL
#define SHARED_SPAD_LOCAL_SIZE (1024 * 1024ULL)
#define SHARED_SPAD_LOCAL_ADDR_BASE(i) (SHARED_SPAD_GLOBAL_ADDR_BASE + SHARED_SPAD_LOCAL_SIZE * (uint64_t)(i))

#define SHARED_CONV_INPUT_OFFSET 0x00380ULL
#define SHARED_CONV_WEIGHT_OFFSET 0x00780ULL
#define SHARED_CONV_OUTPUT_OFFSET 0x00B80ULL
#define SHARED_RESADD_A_OFFSET 0x01F40ULL
#define SHARED_RESADD_B_OFFSET 0x02340ULL
#define SHARED_RESADD_OUT_OFFSET 0x02740ULL
#define SHARED_DMA_A_OFFSET 0x10000ULL
#define SHARED_DMA_B_OFFSET 0x20000ULL

#define SHARED_DMA_CROSS_OFFSET (REROCC_SPM_PAGE_BYTES - 64U)
#define DRAM_DMA_CROSS_OFFSET 64U

#define BATCH_SIZE 1
#define IN_ROW_DIM 4
#define IN_COL_DIM 4
#define IN_CHANNELS 4
#define OUT_CHANNELS 4
#define KERNEL_DIM 3
#define PADDING 1
#define STRIDE 1

#define OUT_ROW_DIM ((IN_ROW_DIM + 2 * PADDING - KERNEL_DIM) / STRIDE + 1)
#define OUT_COL_DIM ((IN_COL_DIM + 2 * PADDING - KERNEL_DIM) / STRIDE + 1)
#define PATCH_SIZE (KERNEL_DIM * KERNEL_DIM * IN_CHANNELS)
#define N_PATCHES (BATCH_SIZE * OUT_ROW_DIM * OUT_COL_DIM)

#define CONV_ELEM_COUNT ((size_t)BATCH_SIZE * OUT_ROW_DIM * OUT_COL_DIM * OUT_CHANNELS)

#if (REROCC_DMA_BYTES < (DRAM_DMA_CROSS_OFFSET + REROCC_DMA_CROSS_BYTES))
#error "REROCC_DMA_BYTES is too small for cross-page DMA validation"
#endif

static volatile int dma_complete_flag __attribute__((aligned(64)));

static elem_t input_dram[BATCH_SIZE][IN_ROW_DIM][IN_COL_DIM][IN_CHANNELS] __attribute__((aligned(64)));
static elem_t weights_4d[OUT_CHANNELS][KERNEL_DIM][KERNEL_DIM][IN_CHANNELS] __attribute__((aligned(64)));
static elem_t weights_mat_dram[PATCH_SIZE][OUT_CHANNELS] __attribute__((aligned(64)));
static acc_t bias_dram[OUT_CHANNELS] __attribute__((aligned(64)));
static elem_t reference_out[BATCH_SIZE][OUT_ROW_DIM][OUT_COL_DIM][OUT_CHANNELS] __attribute__((aligned(64)));

static elem_t output_case1_dram[N_PATCHES][OUT_CHANNELS] __attribute__((aligned(64)));
static elem_t output_case2_dram[N_PATCHES][OUT_CHANNELS] __attribute__((aligned(64)));
static elem_t resadd_a_dram[DIM][DIM] __attribute__((aligned(64)));
static elem_t resadd_b_dram[DIM][DIM] __attribute__((aligned(64)));
static elem_t resadd_out_dram[DIM][DIM] __attribute__((aligned(64)));
static elem_t resadd_gold_dram[DIM][DIM] __attribute__((aligned(64)));

static uint8_t dma_dram_src[REROCC_DMA_BYTES + REROCC_DMA_OFFSET_MAX] __attribute__((aligned(64)));
static uint8_t dma_dram_dst[REROCC_DMA_BYTES + REROCC_DMA_OFFSET_MAX] __attribute__((aligned(64)));
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

static void shared_byte_zero(volatile uint8_t *dst, size_t n) {
  size_t i = 0;
  uintptr_t dst_addr = (uintptr_t)dst;

  while (i < n && (((dst_addr + i) & (sizeof(uint64_t) - 1U)) != 0U)) {
    dst[i] = 0;
    i++;
  }

  for (; i + sizeof(uint64_t) <= n; i += sizeof(uint64_t)) {
    *(volatile uint64_t *)(volatile void *)(dst + i) = 0ULL;
  }

  for (; i < n; i++) {
    dst[i] = 0;
  }
}

static void flatten_weights(
    elem_t weights[OUT_CHANNELS][KERNEL_DIM][KERNEL_DIM][IN_CHANNELS],
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

static void cpu_conv_reference(
    elem_t input[BATCH_SIZE][IN_ROW_DIM][IN_COL_DIM][IN_CHANNELS],
    elem_t weights[OUT_CHANNELS][KERNEL_DIM][KERNEL_DIM][IN_CHANNELS],
    acc_t bias[OUT_CHANNELS],
    elem_t reference[BATCH_SIZE][OUT_ROW_DIM][OUT_COL_DIM][OUT_CHANNELS]) {
  for (int b = 0; b < BATCH_SIZE; b++) {
    for (int orow = 0; orow < OUT_ROW_DIM; orow++) {
      for (int ocol = 0; ocol < OUT_COL_DIM; ocol++) {
        for (int och = 0; och < OUT_CHANNELS; och++) {
          acc_t acc = bias[och];
          for (int krow = 0; krow < KERNEL_DIM; krow++) {
            for (int kcol = 0; kcol < KERNEL_DIM; kcol++) {
              for (int ich = 0; ich < IN_CHANNELS; ich++) {
                int irow = orow * STRIDE + krow - PADDING;
                int icol = ocol * STRIDE + kcol - PADDING;
                elem_t px = (irow < 0 || irow >= IN_ROW_DIM || icol < 0 || icol >= IN_COL_DIM)
                    ? (elem_t)0
                    : input[b][irow][icol][ich];
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
}

static void init_simple_conv_fixture(void) {
  const int center = KERNEL_DIM / 2;
  const int center_row = center * KERNEL_DIM * IN_CHANNELS + center * IN_CHANNELS;

  // These globals start in zeroed BSS, so only touch a few non-zero points.
  input_dram[0][0][0][0] = (elem_t)1;
  reference_out[0][0][0][0] = (elem_t)1;
  input_dram[0][1][2][1] = (elem_t)-1;
  reference_out[0][1][2][1] = (elem_t)-1;
  input_dram[0][2][1][2] = (elem_t)2;
  reference_out[0][2][1][2] = (elem_t)2;
  input_dram[0][3][3][3] = (elem_t)-2;
  reference_out[0][3][3][3] = (elem_t)-2;

  for (int ch = 0; ch < OUT_CHANNELS && ch < IN_CHANNELS; ch++) {
    weights_4d[ch][center][center][ch] = (elem_t)1;
    weights_mat_dram[center_row + ch][ch] = (elem_t)1;
  }
}

static void init_simple_resadd_fixture(void) {
  ((elem_t *)resadd_a_dram)[0] = (elem_t)1;
  ((elem_t *)resadd_b_dram)[0] = (elem_t)2;
  ((elem_t *)resadd_gold_dram)[0] = (elem_t)3;

  ((elem_t *)resadd_a_dram)[5] = (elem_t)-2;
  ((elem_t *)resadd_b_dram)[5] = (elem_t)1;
  ((elem_t *)resadd_gold_dram)[5] = (elem_t)-1;

  ((elem_t *)resadd_a_dram)[10] = (elem_t)3;
  ((elem_t *)resadd_b_dram)[10] = (elem_t)-1;
  ((elem_t *)resadd_gold_dram)[10] = (elem_t)2;
}

static inline void set_stage_tag(uint64_t tag) {
  write_csr(mscratch, tag);
}

static bool conv_output_matches(const elem_t *reference, const elem_t *output) {
  for (size_t i = 0; i < CONV_ELEM_COUNT; i++) {
    if (reference[i] != output[i]) {
      return false;
    }
  }
  return true;
}

static inline uint64_t shared_spad_vaddr(uint64_t paddr) {
  return SHARED_SPAD_XLATE_RANGE_BASE + (paddr - SHARED_SPAD_GLOBAL_ADDR_BASE);
}

static void spm_xlate_table_clear(void) {
  memset(spm_xlate_pte, 0, sizeof(spm_xlate_pte));
}

static void spm_xlate_map_page(uint64_t vaddr, uint64_t paddr) {
  uint64_t vpage = (vaddr - SHARED_SPAD_XLATE_RANGE_BASE) >> REROCC_SPM_PAGE_SHIFT;
  if (vpage < SHARED_SPAD_XLATE_PTE_CAP) {
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

static inline uint32_t gemmini_cfg_id(void) {
  return GEMMINI_CFG_ID;
}

static inline uint32_t dma_cfg_id(void) {
#if REROCC_PAIR_MANAGER_MODE
  return GEMMINI_CFG_ID;
#else
  return DMA_CFG_ID;
#endif
}

static inline void bind_gemmini_opcode(uint32_t cfg_id) {
  rr_set_opc(3, cfg_id);
#if REROCC_PAIR_MANAGER_MODE
  rr_set_opc(2, cfg_id);
#endif
}

static inline void bind_dma_opcode(uint32_t cfg_id) {
  rr_set_opc(2, cfg_id);
#if REROCC_PAIR_MANAGER_MODE
  rr_set_opc(3, cfg_id);
#endif
}

static bool run_conv_case(const char *name, int gemmini_manager_id,
                          const elem_t *input, const elem_t *weights, elem_t *output,
                          const elem_t *reference) {
  const uint32_t cfg_id = gemmini_cfg_id();
  if (!rr_acquire_cfg_with_retry(cfg_id, (uint64_t)gemmini_manager_id)) {
    printf("CASE_FAIL %s reason=acquire\n", name);
    return false;
  }

  bind_gemmini_opcode(cfg_id);
  gemmini_flush(0);

  tiled_conv_auto(
      BATCH_SIZE, IN_ROW_DIM, IN_COL_DIM, IN_CHANNELS,
      OUT_CHANNELS, OUT_ROW_DIM, OUT_COL_DIM,
      STRIDE, 1, 1, PADDING, KERNEL_DIM,
      false, false, false, false, false,
      input,
      weights,
      bias_dram,
      output,
      NO_ACTIVATION, ACC_SCALE_IDENTITY, 0, 0, 0,
      WS);

  gemmini_fence();
  rr_fence(cfg_id);
  rr_release(cfg_id);

  bool ok = conv_output_matches(reference, output);
  printf("CASE_RESULT %s %s\n", name, ok ? "PASS" : "FAIL");
  return ok;
}

static bool run_resadd_case(const char *name, int gemmini_manager_id,
                            const elem_t *a, const elem_t *b, elem_t *out, const elem_t *gold) {
  bool ok = true;
  const uint32_t cfg_id = gemmini_cfg_id();
  if (!rr_acquire_cfg_with_retry(cfg_id, (uint64_t)gemmini_manager_id)) {
    printf("CASE_FAIL %s reason=acquire\n", name);
    return false;
  }

  bind_gemmini_opcode(cfg_id);
  gemmini_flush(0);

  tiled_resadd_auto(
      DIM, DIM, MVIN_SCALE_IDENTITY, MVIN_SCALE_IDENTITY, ACC_SCALE_IDENTITY,
      a, b, out, false, WS);

  gemmini_fence();
  rr_fence(cfg_id);
  rr_release(cfg_id);

  for (size_t i = 0; i < (size_t)(DIM * DIM); i++) {
    if (out[i] != gold[i]) {
      ok = false;
      break;
    }
  }
  printf("CASE_RESULT %s %s\n", name, ok ? "PASS" : "FAIL");
  return ok;
}

static bool run_shared_mv_case(const char *name, int gemmini_manager_id,
                               elem_t *src_phys, elem_t *dst_phys,
                               uint64_t src_vaddr, uint64_t dst_vaddr,
                               bool use_xlate) {
  const size_t bytes = (size_t)(DIM * DIM * sizeof(elem_t));
  const elem_t *src_req = src_phys;
  elem_t *dst_req = dst_phys;
  bool ok = true;
  const uint32_t cfg_id = gemmini_cfg_id();
  if (!rr_acquire_cfg_with_retry(cfg_id, (uint64_t)gemmini_manager_id)) {
    printf("CASE_FAIL %s reason=acquire\n", name);
    return false;
  }

  bind_gemmini_opcode(cfg_id);
  gemmini_flush(0);
  shared_byte_zero((volatile uint8_t *)dst_phys, bytes);

  if (use_xlate) {
    spm_xlate_table_clear();
    spm_xlate_map_range(src_vaddr, (uint64_t)(uintptr_t)src_phys, bytes);
    spm_xlate_map_range(dst_vaddr, (uint64_t)(uintptr_t)dst_phys, bytes);
    spm_xlate_program(SHARED_SPAD_XLATE_RANGE_BASE, SHARED_SPAD_XLATE_RANGE_SIZE, true);
    src_req = (const elem_t *)(uintptr_t)src_vaddr;
    dst_req = (elem_t *)(uintptr_t)dst_vaddr;
  } else {
    spm_xlate_program(SHARED_SPAD_GLOBAL_ADDR_BASE, SHARED_SPAD_XLATE_RANGE_SIZE, false);
  }

  gemmini_config_ld(DIM * sizeof(elem_t));
  gemmini_config_st(DIM * sizeof(elem_t));
  gemmini_mvin(src_req, 0);
  gemmini_mvout(dst_req, 0);
  gemmini_fence();
  rr_fence(cfg_id);
  spm_xlate_reset();
  rr_release(cfg_id);

  for (size_t i = 0; i < (size_t)(DIM * DIM); ++i) {
    if (src_phys[i] != dst_phys[i]) {
      ok = false;
      break;
    }
  }
  printf("CASE_RESULT %s %s\n", name, ok ? "PASS" : "FAIL");
  return ok;
}

static bool run_spm_xlate_ctrl_case(const char *name, int gemmini_manager_id) {
  uint64_t fault_raw = 0;
  bool ok;
  const uint32_t cfg_id = gemmini_cfg_id();
  if (!rr_acquire_cfg_with_retry(cfg_id, (uint64_t)gemmini_manager_id)) {
    printf("CASE_FAIL %s reason=acquire\n", name);
    return false;
  }

  bind_gemmini_opcode(cfg_id);
  spm_xlate_table_clear();
  rerocc_gemmini_spm_xlate_cfg((uint64_t)(uintptr_t)spm_xlate_pte,
                               (uint32_t)SHARED_SPAD_XLATE_PTE_CAP,
                               REROCC_SPM_PAGE_SHIFT,
                               1U);
  rerocc_gemmini_spm_xlate_range(SHARED_SPAD_XLATE_RANGE_BASE, SHARED_SPAD_XLATE_RANGE_SIZE);
#if REROCC_COVERAGE_SPM_XLATE_FAULT_READBACK
  fault_raw = rerocc_gemmini_spm_xlate_fault();
#endif
  rerocc_gemmini_spm_xlate_flush();

  // Restore the controller defaults so later DRAM-based coverage cases are not
  // redirected into shared scratchpad by the control-path test's temporary setup.
  rerocc_gemmini_spm_xlate_cfg(0ULL, 0U, REROCC_SPM_PAGE_SHIFT, 0U);
  rerocc_gemmini_spm_xlate_range(0ULL, 0ULL);
  rerocc_gemmini_spm_xlate_flush();
  rr_fence(cfg_id);
  rr_release(cfg_id);

  ok = (fault_raw & 0xffULL) == 0ULL;
  printf("CASE_RESULT %s %s f=0x%lx\n", name, ok ? "PASS" : "FAIL",
         (unsigned long)fault_raw);
  return ok;
}

static void fill_pattern(uint8_t *buf, size_t n, uint32_t *state) {
  size_t i = 0;
  const uintptr_t addr = (uintptr_t)buf;

  while (i < n && (((addr + i) & (sizeof(uint64_t) - 1U)) != 0U)) {
    buf[i] = (uint8_t)lcg_next(state);
    i++;
  }

  for (; i + sizeof(uint64_t) <= n; i += sizeof(uint64_t)) {
    uint64_t word = 0;
    for (size_t b = 0; b < sizeof(uint64_t); b++) {
      word |= ((uint64_t)(uint8_t)lcg_next(state)) << (8U * b);
    }
    *(uint64_t *)(void *)(buf + i) = word;
  }

  for (; i < n; i++) {
    buf[i] = (uint8_t)lcg_next(state);
  }
}

static bool buffers_equal(const uint8_t *a, const uint8_t *b, size_t n) {
  size_t i = 0;
  const uintptr_t a_addr = (uintptr_t)a;
  const uintptr_t b_addr = (uintptr_t)b;

  if ((a_addr & (sizeof(uint64_t) - 1U)) == (b_addr & (sizeof(uint64_t) - 1U))) {
    while (i < n && (((a_addr + i) & (sizeof(uint64_t) - 1U)) != 0U)) {
      if (a[i] != b[i]) {
        return false;
      }
      i++;
    }

    for (; i + sizeof(uint64_t) <= n; i += sizeof(uint64_t)) {
      const uint64_t a64 = *(const uint64_t *)(const void *)(a + i);
      const uint64_t b64 = *(const uint64_t *)(const void *)(b + i);
      if (a64 != b64) {
        return false;
      }
    }
  }

  for (; i < n; i++) {
    if (a[i] != b[i]) {
      return false;
    }
  }
  return true;
}

static bool dma_copy_wait(uint64_t src, uint64_t dst, volatile int *completion, size_t nbytes) {
  *completion = 0;
  asm volatile("fence rw, rw" ::: "memory");
  rerocc_coupleddma_set_dst(dst, (uint64_t)(uintptr_t)completion);
  rerocc_coupleddma_set_src(src, (uint64_t)nbytes);

  for (unsigned long spin = 0; spin < DMA_WAIT_SPINS; spin++) {
    asm volatile("fence r, rw" ::: "memory");
    if (*completion != 0) {
      *completion = 0;
      asm volatile("fence" ::: "memory");
      return true;
    }
    asm volatile("nop");
  }

  return false;
}

static bool run_dma_case(const char *name, int dma_manager_id,
                         const uint8_t *expected, uint8_t *src, uint8_t *dst, size_t nbytes) {
  const uint32_t cfg_id = dma_cfg_id();
  if (!rr_acquire_cfg_with_retry(cfg_id, (uint64_t)dma_manager_id)) {
    printf("CASE_FAIL %s reason=acquire\n", name);
    return false;
  }

  bind_dma_opcode(cfg_id);
  bool completed = dma_copy_wait((uint64_t)(uintptr_t)src, (uint64_t)(uintptr_t)dst,
                                 &dma_complete_flag, nbytes);
  if (!completed) {
    printf("CASE_FAIL %s reason=timeout src=0x%lx dst=0x%lx bytes=%lu\n",
           name, (unsigned long)(uintptr_t)src, (unsigned long)(uintptr_t)dst, (unsigned long)nbytes);
  } else {
    rr_fence(cfg_id);
  }
  bool data_ok = completed && buffers_equal(expected, dst, nbytes);
  if (completed && !data_ok) {
    printf("CASE_FAIL %s reason=data_mismatch src=0x%lx dst=0x%lx bytes=%lu\n",
           name, (unsigned long)(uintptr_t)src, (unsigned long)(uintptr_t)dst, (unsigned long)nbytes);
  }
  rr_release(cfg_id);

  printf("CASE_RESULT %s %s\n", name, data_ok ? "PASS" : "FAIL");
  return data_ok;
}

void thread_entry(int cid, int nc) {
  (void)nc;
  if (cid != 0) {
    while (1) {
      asm volatile("wfi");
    }
  }

  set_stage_tag(0x01);

  const int gemmini_local_id = REROCC_TEST_LOCAL_GEMMINI_ID;
  const int gemmini_manager_id = REROCC_GEMMINI_BASE_ID + gemmini_local_id;
#if REROCC_PAIR_MANAGER_MODE
  const int dma_manager_id = REROCC_GEMMINI_BASE_ID + gemmini_local_id;
#else
  const int dma_manager_id = REROCC_DMA_BASE_ID + gemmini_local_id;
#endif
  const uint64_t shared_base = SHARED_SPAD_LOCAL_ADDR_BASE(gemmini_local_id);

  elem_t *shared_conv_input = (elem_t *)(uintptr_t)(shared_base + SHARED_CONV_INPUT_OFFSET);
  elem_t *shared_conv_weight = (elem_t *)(uintptr_t)(shared_base + SHARED_CONV_WEIGHT_OFFSET);
  elem_t *shared_conv_output = (elem_t *)(uintptr_t)(shared_base + SHARED_CONV_OUTPUT_OFFSET);
  elem_t *shared_resadd_a = (elem_t *)(uintptr_t)(shared_base + SHARED_RESADD_A_OFFSET);
  elem_t *shared_resadd_b = (elem_t *)(uintptr_t)(shared_base + SHARED_RESADD_B_OFFSET);
  elem_t *shared_resadd_out = (elem_t *)(uintptr_t)(shared_base + SHARED_RESADD_OUT_OFFSET);
  uint8_t *shared_dma_a = (uint8_t *)(uintptr_t)(shared_base + SHARED_DMA_A_OFFSET);
  uint8_t *shared_dma_b = (uint8_t *)(uintptr_t)(shared_base + SHARED_DMA_B_OFFSET);
  uint8_t *shared_dma_a_cross = shared_dma_a + SHARED_DMA_CROSS_OFFSET;
  uint8_t *shared_dma_b_cross = shared_dma_b + SHARED_DMA_CROSS_OFFSET;
  uint8_t *dram_dma_src_cross = dma_dram_src + DRAM_DMA_CROSS_OFFSET;
  uint8_t *dram_dma_dst_cross = dma_dram_dst + DRAM_DMA_CROSS_OFFSET;
  uint8_t *dram_dma_src_misaligned16 = dma_dram_src + REROCC_DMA_MISALIGNED_DRAM_SRC16_OFFSET;
  uint8_t *dram_dma_src_misaligned1 = dma_dram_src + REROCC_DMA_MISALIGNED_DRAM_SRC1_OFFSET;
  uint8_t *dram_dma_dst_misaligned16 = dma_dram_dst + REROCC_DMA_MISALIGNED_DRAM_DST16_OFFSET;
  uint8_t *shared_dma_a_misaligned16 = shared_dma_a + REROCC_DMA_MISALIGNED_SHARED_SRC16_OFFSET;
  uint8_t *shared_dma_b_misaligned48 = shared_dma_b + REROCC_DMA_MISALIGNED_SHARED_DST48_OFFSET;

  set_stage_tag(0x02);

#if REROCC_COVERAGE_BASELINE_CASES || REROCC_COVERAGE_HEAVY_GEMMINI_CASES || REROCC_COVERAGE_SHARED_MV_CASES
  #if REROCC_COVERAGE_SIMPLE_CONV_FIXTURE
    init_simple_conv_fixture();
  #else
    uint32_t rnd = 0x12345678u;
    init_random_elem(&input_dram[0][0][0][0], sizeof(input_dram) / sizeof(elem_t), &rnd);
    init_random_elem(&weights_4d[0][0][0][0], sizeof(weights_4d) / sizeof(elem_t), &rnd);
    init_random_acc(&bias_dram[0], sizeof(bias_dram) / sizeof(acc_t), &rnd);
    flatten_weights(weights_4d, weights_mat_dram);
    cpu_conv_reference(input_dram, weights_4d, bias_dram, reference_out);
  #endif
#else
  for (size_t i = 0; i < (size_t)(DIM * DIM); i++) {
    ((elem_t *)input_dram)[i] = (elem_t)((int)i - 3);
  }
#endif
  set_stage_tag(0x03);
  set_stage_tag(0x10);

#if REROCC_COVERAGE_BASELINE_CASES || REROCC_COVERAGE_HEAVY_GEMMINI_CASES
  memset(output_case1_dram, 0, sizeof(output_case1_dram));
  memset(output_case2_dram, 0, sizeof(output_case2_dram));
#endif
#if REROCC_COVERAGE_SHARED_MV_CASES
  memset(shared_conv_output, 0, (size_t)(DIM * DIM * sizeof(elem_t)));
#endif
  set_stage_tag(0x04);

#if REROCC_COVERAGE_HEAVY_GEMMINI_CASES
  #if REROCC_COVERAGE_SIMPLE_RESADD_FIXTURE
    init_simple_resadd_fixture();
  #else
    uint32_t rnd = 0x12345678u;
    for (size_t i = 0; i < (size_t)(DIM * DIM); i++) {
      ((elem_t *)resadd_a_dram)[i] = (elem_t)((int32_t)(lcg_next(&rnd) % 9) - 4);
      ((elem_t *)resadd_b_dram)[i] = (elem_t)((int32_t)(lcg_next(&rnd) % 9) - 4);
      ((elem_t *)resadd_out_dram)[i] = 0;
      ((elem_t *)resadd_gold_dram)[i] = 0;
    }
    resadd_cpu(DIM, DIM, DIM, MVIN_SCALE_IDENTITY, MVIN_SCALE_IDENTITY, ACC_SCALE_IDENTITY,
               (elem_t *)resadd_a_dram, (elem_t *)resadd_b_dram, (elem_t *)resadd_gold_dram, false);
  #endif
#endif
  set_stage_tag(0x05);
  set_stage_tag(0x20);

#if REROCC_COVERAGE_SHARED_MV_CASES
  shared_byte_copy((volatile uint8_t *)shared_conv_input, (const uint8_t *)input_dram, sizeof(input_dram));
#endif
#if REROCC_COVERAGE_HEAVY_GEMMINI_CASES
  shared_byte_copy((volatile uint8_t *)shared_conv_weight, (const uint8_t *)weights_mat_dram, sizeof(weights_mat_dram));
  shared_byte_copy((volatile uint8_t *)shared_resadd_a, (const uint8_t *)resadd_a_dram, sizeof(resadd_a_dram));
  shared_byte_copy((volatile uint8_t *)shared_resadd_b, (const uint8_t *)resadd_b_dram, sizeof(resadd_b_dram));
  shared_byte_zero((volatile uint8_t *)shared_resadd_out, sizeof(resadd_out_dram));
#endif
  set_stage_tag(0x06);
  set_stage_tag(0x30);
  set_stage_tag(0x40);
#if REROCC_COVERAGE_BASELINE_CASES
  printf("CASE_BEGIN c0\n");
  bool case0 = run_spm_xlate_ctrl_case("c0", gemmini_manager_id);
#else
  bool case0 = true;
  printf("CASE_SKIP c0 baseline=0\n");
#endif

  set_stage_tag(0x41);
#if REROCC_COVERAGE_BASELINE_CASES
  printf("CASE_BEGIN c1\n");
  bool case1 = run_conv_case(
      "c1",
      gemmini_manager_id,
      (const elem_t *)input_dram,
      (const elem_t *)weights_mat_dram,
      (elem_t *)output_case1_dram,
      (const elem_t *)&reference_out[0][0][0][0]);
#else
  bool case1 = true;
  printf("CASE_SKIP c1 baseline=0\n");
#endif

#if REROCC_COVERAGE_HEAVY_GEMMINI_CASES
  set_stage_tag(0x42);
  printf("CASE_BEGIN c2\n");
  bool case2 = run_conv_case(
      "c2",
      gemmini_manager_id,
      (const elem_t *)input_dram,
      (const elem_t *)shared_conv_weight,
      (elem_t *)output_case2_dram,
      (const elem_t *)&reference_out[0][0][0][0]);

  set_stage_tag(0x43);
  printf("CASE_BEGIN c3\n");
  bool case3 = run_conv_case(
      "c3",
      gemmini_manager_id,
      (const elem_t *)shared_conv_input,
      (const elem_t *)shared_conv_weight,
      (elem_t *)shared_conv_output,
      (const elem_t *)&reference_out[0][0][0][0]);

  set_stage_tag(0x44);
  printf("CASE_BEGIN c4\n");
  bool case4 = run_resadd_case(
      "c4",
      gemmini_manager_id,
      (const elem_t *)shared_resadd_a,
      (const elem_t *)shared_resadd_b,
      (elem_t *)shared_resadd_out,
      (const elem_t *)resadd_gold_dram);
#else
  bool case2 = true;
  bool case3 = true;
  bool case4 = true;
  printf("CASE_SKIP c2 heavy_gemmini=0\n");
  printf("CASE_SKIP c3 heavy_gemmini=0\n");
  printf("CASE_SKIP c4 heavy_gemmini=0\n");
#endif

#if REROCC_COVERAGE_SHARED_MV_CASES
  set_stage_tag(0x45);
  printf("CASE_BEGIN c4b\n");
  bool case4b = run_shared_mv_case(
      "c4b",
      gemmini_manager_id,
      shared_conv_input,
      shared_conv_output,
      shared_spad_vaddr((uint64_t)(uintptr_t)shared_conv_input),
      shared_spad_vaddr((uint64_t)(uintptr_t)shared_conv_output),
      true);

  set_stage_tag(0x46);
  printf("CASE_BEGIN c4c\n");
  bool case4c = run_shared_mv_case(
      "c4c",
      gemmini_manager_id,
      shared_conv_input,
      shared_conv_output,
      shared_spad_vaddr((uint64_t)(uintptr_t)shared_conv_input),
      shared_spad_vaddr((uint64_t)(uintptr_t)shared_conv_output),
      false);
#else
  bool case4b = true;
  bool case4c = true;
  printf("CASE_SKIP c4b shared_mv=0\n");
  printf("CASE_SKIP c4c shared_mv=0\n");
#endif

  uint32_t dma_seed = 0x9e3779b9u;
  fill_pattern(shared_dma_a, REROCC_DMA_BYTES, &dma_seed);
  memset(shared_dma_b, 0, REROCC_DMA_BYTES);
  set_stage_tag(0x47);
  printf("CASE_BEGIN c5\n");
  bool case5 = run_dma_case(
      "c5",
      dma_manager_id,
      (const uint8_t *)shared_dma_a,
      shared_dma_a,
      shared_dma_b,
      REROCC_DMA_BYTES);

#if REROCC_COVERAGE_ALIGNED_DMA_VARIANTS
  fill_pattern(shared_dma_a, REROCC_DMA_BYTES, &dma_seed);
  memset(dma_dram_dst, 0, sizeof(dma_dram_dst));
  set_stage_tag(0x48);
  printf("CASE_BEGIN c6\n");
  bool case6 = run_dma_case(
      "c6",
      dma_manager_id,
      (const uint8_t *)shared_dma_a,
      shared_dma_a,
      dma_dram_dst,
      REROCC_DMA_BYTES);

  fill_pattern(dma_dram_src, REROCC_DMA_BYTES, &dma_seed);
  memset(shared_dma_b, 0, REROCC_DMA_BYTES);
  set_stage_tag(0x49);
  printf("CASE_BEGIN c7\n");
  bool case7 = run_dma_case(
      "c7",
      dma_manager_id,
      (const uint8_t *)dma_dram_src,
      dma_dram_src,
      shared_dma_b,
      REROCC_DMA_BYTES);

  fill_pattern(shared_dma_a_cross, REROCC_DMA_CROSS_BYTES, &dma_seed);
  memset(shared_dma_b_cross, 0, REROCC_DMA_CROSS_BYTES);
  set_stage_tag(0x4a);
  printf("CASE_BEGIN c8\n");
  bool case8 = run_dma_case(
      "c8",
      dma_manager_id,
      (const uint8_t *)shared_dma_a_cross,
      shared_dma_a_cross,
      shared_dma_b_cross,
      REROCC_DMA_CROSS_BYTES);

  fill_pattern(shared_dma_a_cross, REROCC_DMA_CROSS_BYTES, &dma_seed);
  memset(dram_dma_dst_cross, 0, REROCC_DMA_CROSS_BYTES);
  set_stage_tag(0x4b);
  printf("CASE_BEGIN c9\n");
  bool case9 = run_dma_case(
      "c9",
      dma_manager_id,
      (const uint8_t *)shared_dma_a_cross,
      shared_dma_a_cross,
      dram_dma_dst_cross,
      REROCC_DMA_CROSS_BYTES);

  fill_pattern(dram_dma_src_cross, REROCC_DMA_CROSS_BYTES, &dma_seed);
  memset(shared_dma_b_cross, 0, REROCC_DMA_CROSS_BYTES);
  set_stage_tag(0x4c);
  printf("CASE_BEGIN c10\n");
  bool case10 = run_dma_case(
      "c10",
      dma_manager_id,
      (const uint8_t *)dram_dma_src_cross,
      dram_dma_src_cross,
      shared_dma_b_cross,
      REROCC_DMA_CROSS_BYTES);
#else
  bool case6 = true;
  bool case7 = true;
  bool case8 = true;
  bool case9 = true;
  bool case10 = true;
  printf("CASE_SKIP c6 aligned_dma_variants=0\n");
  printf("CASE_SKIP c7 aligned_dma_variants=0\n");
  printf("CASE_SKIP c8 aligned_dma_variants=0\n");
  printf("CASE_SKIP c9 aligned_dma_variants=0\n");
  printf("CASE_SKIP c10 aligned_dma_variants=0\n");
#endif

#if REROCC_DMA_MISALIGNED_ENABLE
  fill_pattern(dram_dma_src_misaligned16, REROCC_DMA_BYTES, &dma_seed);
  memset(shared_dma_b, 0, REROCC_DMA_BYTES + REROCC_DMA_MISALIGNED_SHARED_DST48_OFFSET);
  set_stage_tag(0x4d);
  printf("CASE_BEGIN c11\n");
  bool case11 = run_dma_case(
      "c11",
      dma_manager_id,
      (const uint8_t *)dram_dma_src_misaligned16,
      dram_dma_src_misaligned16,
      shared_dma_b,
      REROCC_DMA_BYTES);

  fill_pattern(shared_dma_a, REROCC_DMA_BYTES + REROCC_DMA_MISALIGNED_SHARED_SRC16_OFFSET, &dma_seed);
  memset(dma_dram_dst, 0, sizeof(dma_dram_dst));
  set_stage_tag(0x4e);
  printf("CASE_BEGIN c12\n");
  bool case12 = run_dma_case(
      "c12",
      dma_manager_id,
      (const uint8_t *)shared_dma_a,
      shared_dma_a,
      dram_dma_dst_misaligned16,
      REROCC_DMA_BYTES);

  fill_pattern(shared_dma_a_misaligned16, REROCC_DMA_BYTES, &dma_seed);
  memset(shared_dma_b, 0, REROCC_DMA_BYTES + REROCC_DMA_MISALIGNED_SHARED_DST48_OFFSET);
  set_stage_tag(0x4f);
  printf("CASE_BEGIN c13\n");
  bool case13 = run_dma_case(
      "c13",
      dma_manager_id,
      (const uint8_t *)shared_dma_a_misaligned16,
      shared_dma_a_misaligned16,
      shared_dma_b_misaligned48,
      REROCC_DMA_BYTES);

  fill_pattern(dram_dma_src_misaligned1, REROCC_DMA_BYTES, &dma_seed);
  memset(shared_dma_b, 0, REROCC_DMA_BYTES);
  set_stage_tag(0x50);
  printf("CASE_BEGIN c14\n");
  bool case14 = run_dma_case(
      "c14",
      dma_manager_id,
      (const uint8_t *)dram_dma_src_misaligned1,
      dram_dma_src_misaligned1,
      shared_dma_b,
      REROCC_DMA_BYTES);
#endif

  rr_release_all(RR_MAX_CFGS);
  set_stage_tag(0x7f);

#if REROCC_DMA_MISALIGNED_ENABLE
  const bool all_ok = case0 && case1 && case2 && case3 && case4 && case4b && case4c &&
                      case5 && case6 && case7 && case8 && case9 && case10 &&
                      case11 && case12 && case13 && case14;
  printf("COV_SUM ok=%d n=17\n", all_ok ? 1 : 0);
#else
  const bool all_ok = case0 && case1 && case2 && case3 && case4 && case4b && case4c &&
                      case5 && case6 && case7 && case8 && case9 && case10;
  printf("COV_SUM ok=%d n=13\n", all_ok ? 1 : 0);
#endif
  if (all_ok) {
    printf("ALL_TESTS_PASS\n");
    exit(0);
  }

  printf("ALL_TESTS_FAIL\n");
  exit(1);
}

int main(void) {
  return 1;
}
