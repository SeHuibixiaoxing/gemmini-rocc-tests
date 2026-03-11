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

#ifndef REROCC_DMA_BASE_ID
#define REROCC_DMA_BASE_ID REROCC_NUM_GEMMINI
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

#ifndef REROCC_DMA_CROSS_BYTES
#define REROCC_DMA_CROSS_BYTES 256U
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
#define IN_ROW_DIM 8
#define IN_COL_DIM 8
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

static uint8_t dma_dram_src[REROCC_DMA_BYTES] __attribute__((aligned(64)));
static uint8_t dma_dram_dst[REROCC_DMA_BYTES] __attribute__((aligned(64)));

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

static bool conv_output_matches(const elem_t *reference, const elem_t *output) {
  for (size_t i = 0; i < CONV_ELEM_COUNT; i++) {
    if (reference[i] != output[i]) {
      return false;
    }
  }
  return true;
}

static bool run_conv_case(const char *name, int gemmini_manager_id,
                          const elem_t *input, const elem_t *weights, elem_t *output,
                          const elem_t *reference) {
  if (!rr_acquire_cfg_with_retry(GEMMINI_CFG_ID, (uint64_t)gemmini_manager_id)) {
    printf("CASE_FAIL %s reason=acquire\n", name);
    return false;
  }

  rr_set_opc(3, GEMMINI_CFG_ID);
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
  rr_fence(GEMMINI_CFG_ID);
  rr_release(GEMMINI_CFG_ID);

  bool ok = conv_output_matches(reference, output);
  printf("CASE_RESULT %s %s\n", name, ok ? "PASS" : "FAIL");
  return ok;
}

static bool run_resadd_case(const char *name, int gemmini_manager_id,
                            const elem_t *a, const elem_t *b, elem_t *out, const elem_t *gold) {
  bool ok = true;
  if (!rr_acquire_cfg_with_retry(GEMMINI_CFG_ID, (uint64_t)gemmini_manager_id)) {
    printf("CASE_FAIL %s reason=acquire\n", name);
    return false;
  }

  rr_set_opc(3, GEMMINI_CFG_ID);
  gemmini_flush(0);

  tiled_resadd_auto(
      DIM, DIM, MVIN_SCALE_IDENTITY, MVIN_SCALE_IDENTITY, ACC_SCALE_IDENTITY,
      a, b, out, false, WS);

  gemmini_fence();
  rr_fence(GEMMINI_CFG_ID);
  rr_release(GEMMINI_CFG_ID);

  for (size_t i = 0; i < (size_t)(DIM * DIM); i++) {
    if (out[i] != gold[i]) {
      ok = false;
      break;
    }
  }
  printf("CASE_RESULT %s %s\n", name, ok ? "PASS" : "FAIL");
  return ok;
}

static bool run_spm_xlate_ctrl_case(const char *name, int gemmini_manager_id) {
  uint64_t fault_raw;
  bool ok;
  if (!rr_acquire_cfg_with_retry(GEMMINI_CFG_ID, (uint64_t)gemmini_manager_id)) {
    printf("CASE_FAIL %s reason=acquire\n", name);
    return false;
  }

  rr_set_opc(3, GEMMINI_CFG_ID);
  rerocc_gemmini_spm_xlate_cfg(0x90000000ULL, 128U, 10U, 1U);
  rerocc_gemmini_spm_xlate_range(0x80000000ULL, 0x00010000ULL);
  fault_raw = rerocc_gemmini_spm_xlate_fault();
  rerocc_gemmini_spm_xlate_flush();

  // Restore the controller defaults so later DRAM-based coverage cases are not
  // redirected into shared scratchpad by the control-path test's temporary setup.
  rerocc_gemmini_spm_xlate_cfg(0ULL, 0U, 10U, 0U);
  rerocc_gemmini_spm_xlate_range(0ULL, 0ULL);
  rerocc_gemmini_spm_xlate_flush();
  rr_fence(GEMMINI_CFG_ID);
  rr_release(GEMMINI_CFG_ID);

  ok = (fault_raw & 0xffULL) == 0ULL;
  printf("CASE_RESULT %s %s fault=0x%lx\n", name, ok ? "PASS" : "FAIL",
         (unsigned long)fault_raw);
  return ok;
}

static void fill_pattern(uint8_t *buf, size_t n, uint32_t *state) {
  for (size_t i = 0; i < n; i++) {
    buf[i] = (uint8_t)lcg_next(state);
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

static bool dma_copy_wait(uint64_t src, uint64_t dst, volatile int *completion, size_t nbytes) {
  *completion = 0;
  rerocc_coupleddma_set_dst(dst, (uint64_t)(uintptr_t)completion);
  rerocc_coupleddma_set_src(src, (uint64_t)nbytes);

  for (unsigned long spin = 0; spin < DMA_WAIT_SPINS; spin++) {
    if (*completion != 0) {
      asm volatile("fence");
      return true;
    }
    asm volatile("nop");
  }

  return false;
}

static bool run_dma_case(const char *name, int dma_manager_id,
                         const uint8_t *expected, uint8_t *src, uint8_t *dst, size_t nbytes) {
  if (!rr_acquire_cfg_with_retry(DMA_CFG_ID, (uint64_t)dma_manager_id)) {
    printf("CASE_FAIL %s reason=acquire\n", name);
    return false;
  }

  rr_set_opc(2, DMA_CFG_ID);
  bool completed = dma_copy_wait((uint64_t)(uintptr_t)src, (uint64_t)(uintptr_t)dst,
                                 &dma_complete_flag, nbytes);
  if (!completed) {
    printf("CASE_FAIL %s reason=timeout src=0x%lx dst=0x%lx bytes=%lu\n",
           name, (unsigned long)(uintptr_t)src, (unsigned long)(uintptr_t)dst, (unsigned long)nbytes);
  } else {
    rr_fence(DMA_CFG_ID);
  }
  bool data_ok = completed && buffers_equal(expected, dst, nbytes);
  rr_release(DMA_CFG_ID);

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

  const int gemmini_local_id = REROCC_TEST_LOCAL_GEMMINI_ID;
  const int gemmini_manager_id = REROCC_GEMMINI_BASE_ID + gemmini_local_id;
  const int dma_manager_id = REROCC_DMA_BASE_ID + gemmini_local_id;
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

  uint32_t rnd = 0x12345678u;
  init_random_elem(&input_dram[0][0][0][0], sizeof(input_dram) / sizeof(elem_t), &rnd);
  init_random_elem(&weights_4d[0][0][0][0], sizeof(weights_4d) / sizeof(elem_t), &rnd);
  init_random_acc(&bias_dram[0], sizeof(bias_dram) / sizeof(acc_t), &rnd);
  flatten_weights(weights_4d, weights_mat_dram);
  cpu_conv_reference(input_dram, weights_4d, bias_dram, reference_out);

  memset(output_case1_dram, 0, sizeof(output_case1_dram));
  memset(output_case2_dram, 0, sizeof(output_case2_dram));
  memset(shared_conv_output, 0, sizeof(output_case1_dram));

  for (size_t i = 0; i < (size_t)(DIM * DIM); i++) {
    ((elem_t *)resadd_a_dram)[i] = (elem_t)((int32_t)(lcg_next(&rnd) % 9) - 4);
    ((elem_t *)resadd_b_dram)[i] = (elem_t)((int32_t)(lcg_next(&rnd) % 9) - 4);
    ((elem_t *)resadd_out_dram)[i] = 0;
    ((elem_t *)resadd_gold_dram)[i] = 0;
  }
  resadd_cpu(DIM, DIM, DIM, MVIN_SCALE_IDENTITY, MVIN_SCALE_IDENTITY, ACC_SCALE_IDENTITY,
             (elem_t *)resadd_a_dram, (elem_t *)resadd_b_dram, (elem_t *)resadd_gold_dram, false);

  memcpy(shared_conv_input, input_dram, sizeof(input_dram));
  memcpy(shared_conv_weight, weights_mat_dram, sizeof(weights_mat_dram));
  memcpy(shared_resadd_a, resadd_a_dram, sizeof(resadd_a_dram));
  memcpy(shared_resadd_b, resadd_b_dram, sizeof(resadd_b_dram));
  memset(shared_resadd_out, 0, sizeof(resadd_out_dram));

  printf("[rerocc-coverage] gemmini_id=%d dma_id=%d dma_bytes=%d\n",
         gemmini_manager_id, dma_manager_id, REROCC_DMA_BYTES);

  bool case0 = run_spm_xlate_ctrl_case("spm_xlate_ctrl", gemmini_manager_id);

  bool case1 = run_conv_case(
      "conv_dram_input_dram_weight_to_dram_output",
      gemmini_manager_id,
      (const elem_t *)input_dram,
      (const elem_t *)weights_mat_dram,
      (elem_t *)output_case1_dram,
      (const elem_t *)&reference_out[0][0][0][0]);

  bool case2 = run_conv_case(
      "conv_dram_input_shared_weight_to_dram_output",
      gemmini_manager_id,
      (const elem_t *)input_dram,
      (const elem_t *)shared_conv_weight,
      (elem_t *)output_case2_dram,
      (const elem_t *)&reference_out[0][0][0][0]);

  bool case3 = run_conv_case(
      "conv_shared_input_shared_weight_to_shared_output",
      gemmini_manager_id,
      (const elem_t *)shared_conv_input,
      (const elem_t *)shared_conv_weight,
      (elem_t *)shared_conv_output,
      (const elem_t *)&reference_out[0][0][0][0]);

  bool case4 = run_resadd_case(
      "resadd_shared_input_to_shared_output",
      gemmini_manager_id,
      (const elem_t *)shared_resadd_a,
      (const elem_t *)shared_resadd_b,
      (elem_t *)shared_resadd_out,
      (const elem_t *)resadd_gold_dram);

  uint32_t dma_seed = 0x9e3779b9u;
  fill_pattern(shared_dma_a, REROCC_DMA_BYTES, &dma_seed);
  memset(shared_dma_b, 0, REROCC_DMA_BYTES);
  printf("CASE_START dma_shared_to_shared\n");
  bool case5 = run_dma_case(
      "dma_shared_to_shared",
      dma_manager_id,
      (const uint8_t *)shared_dma_a,
      shared_dma_a,
      shared_dma_b,
      REROCC_DMA_BYTES);

  fill_pattern(shared_dma_a, REROCC_DMA_BYTES, &dma_seed);
  memset(dma_dram_dst, 0, sizeof(dma_dram_dst));
  printf("CASE_START dma_shared_to_dram\n");
  bool case6 = run_dma_case(
      "dma_shared_to_dram",
      dma_manager_id,
      (const uint8_t *)shared_dma_a,
      shared_dma_a,
      dma_dram_dst,
      REROCC_DMA_BYTES);

  fill_pattern(dma_dram_src, REROCC_DMA_BYTES, &dma_seed);
  memset(shared_dma_b, 0, REROCC_DMA_BYTES);
  printf("CASE_START dma_dram_to_shared\n");
  bool case7 = run_dma_case(
      "dma_dram_to_shared",
      dma_manager_id,
      (const uint8_t *)dma_dram_src,
      dma_dram_src,
      shared_dma_b,
      REROCC_DMA_BYTES);

  fill_pattern(shared_dma_a_cross, REROCC_DMA_CROSS_BYTES, &dma_seed);
  memset(shared_dma_b_cross, 0, REROCC_DMA_CROSS_BYTES);
  printf("CASE_START dma_shared_to_shared_cross_page\n");
  bool case8 = run_dma_case(
      "dma_shared_to_shared_cross_page",
      dma_manager_id,
      (const uint8_t *)shared_dma_a_cross,
      shared_dma_a_cross,
      shared_dma_b_cross,
      REROCC_DMA_CROSS_BYTES);

  fill_pattern(shared_dma_a_cross, REROCC_DMA_CROSS_BYTES, &dma_seed);
  memset(dram_dma_dst_cross, 0, REROCC_DMA_CROSS_BYTES);
  printf("CASE_START dma_shared_to_dram_cross_page\n");
  bool case9 = run_dma_case(
      "dma_shared_to_dram_cross_page",
      dma_manager_id,
      (const uint8_t *)shared_dma_a_cross,
      shared_dma_a_cross,
      dram_dma_dst_cross,
      REROCC_DMA_CROSS_BYTES);

  fill_pattern(dram_dma_src_cross, REROCC_DMA_CROSS_BYTES, &dma_seed);
  memset(shared_dma_b_cross, 0, REROCC_DMA_CROSS_BYTES);
  printf("CASE_START dma_dram_to_shared_cross_page\n");
  bool case10 = run_dma_case(
      "dma_dram_to_shared_cross_page",
      dma_manager_id,
      (const uint8_t *)dram_dma_src_cross,
      dram_dma_src_cross,
      shared_dma_b_cross,
      REROCC_DMA_CROSS_BYTES);

  rr_release_all(RR_MAX_CFGS);

  const bool all_ok = case0 && case1 && case2 && case3 && case4 &&
                      case5 && case6 && case7 && case8 && case9 && case10;
  printf("COVERAGE_SUMMARY case0=%d case1=%d case2=%d case3=%d case4=%d case5=%d case6=%d case7=%d case8=%d case9=%d case10=%d\n",
         case0 ? 1 : 0, case1 ? 1 : 0, case2 ? 1 : 0, case3 ? 1 : 0,
         case4 ? 1 : 0, case5 ? 1 : 0, case6 ? 1 : 0, case7 ? 1 : 0,
         case8 ? 1 : 0, case9 ? 1 : 0, case10 ? 1 : 0);
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
