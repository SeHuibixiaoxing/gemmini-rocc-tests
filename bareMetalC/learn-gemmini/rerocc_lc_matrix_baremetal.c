#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "encoding.h"
#include "util.h"
#include "include/gemmini.h"
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

#ifndef REROCC_ACQUIRE_MAX_RETRIES
#define REROCC_ACQUIRE_MAX_RETRIES 1000000UL
#endif

#define GEMMINI_CFG_ID 0
#define DMA_CFG_ID 1
#define DMA_XCUSTOM 2

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

static volatile int dma_complete_flag[REROCC_MAX_CORES] __attribute__((aligned(64)));
static uint8_t dma_src[REROCC_MAX_CORES][REROCC_DMA_BYTES] __attribute__((aligned(64)));
static uint8_t dma_dst[REROCC_MAX_CORES][REROCC_DMA_BYTES] __attribute__((aligned(64)));
static volatile int core_done[REROCC_MAX_CORES];
static volatile int core_gemmini_pass[REROCC_MAX_CORES];
static volatile int core_gemmini_fail[REROCC_MAX_CORES];
static volatile int core_dma_pass[REROCC_MAX_CORES];
static volatile int core_dma_fail[REROCC_MAX_CORES];

static inline bool debug_this_core(int cid) {
  return REROCC_DEBUG_CHECKPOINTS && cid == 0;
}

static inline uint64_t read_cycles_local(void) {
  return read_csr(mcycle);
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

static inline void dma_set_dst(uint64_t addr, uint64_t completion_addr) {
  ROCC_INSTRUCTION_0_R_R(DMA_XCUSTOM, addr, completion_addr, 2);
}

static inline void dma_set_src(uint64_t addr, uint64_t len) {
  ROCC_INSTRUCTION_0_R_R(DMA_XCUSTOM, addr, len, 1);
}

static inline void dma_fence_wait(volatile int *completion_flag) {
  uint64_t status = 0;
  asm volatile("fence");
  ROCC_INSTRUCTION_R_R_R(DMA_XCUSTOM, status, 0, 0, 3);
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
  elem_t input[BATCH_SIZE][IN_ROW_DIM][IN_COL_DIM][IN_CHANNELS] __attribute__((aligned(64)));
  elem_t weights[OUT_CHANNELS][KERNEL_DIM][KERNEL_DIM][IN_CHANNELS] __attribute__((aligned(64)));
  acc_t bias[OUT_CHANNELS] __attribute__((aligned(64)));
  elem_t reference[BATCH_SIZE][OUT_ROW_DIM][OUT_COL_DIM][OUT_CHANNELS] __attribute__((aligned(64)));
  elem_t weights_mat[PATCH_SIZE][OUT_CHANNELS] __attribute__((aligned(64)));
  elem_t output_mat[N_PATCHES][OUT_CHANNELS] __attribute__((aligned(64)));

  uint32_t state = (uint32_t)(0x1234567u ^ (cid * 131u) ^ (manager_id * 977u));

  if (debug) {
    printf("[dbg][gemmini] cpu=%d mgr=%d before cpu_conv_reference_prep\n", cid, manager_id);
  }
  init_random_elem(&input[0][0][0][0], sizeof(input) / sizeof(elem_t), &state);
  init_random_elem(&weights[0][0][0][0], sizeof(weights) / sizeof(elem_t), &state);
  init_random_acc(&bias[0], sizeof(bias) / sizeof(acc_t), &state);
  flatten_weights(weights, weights_mat);
  cpu_conv_reference(input, weights, bias, reference);
  if (debug) {
    printf("[dbg][gemmini] cpu=%d mgr=%d after cpu_conv_reference_prep\n", cid, manager_id);
  }

  for (size_t i = 0; i < (sizeof(output_mat) / sizeof(elem_t)); i++) {
    (&output_mat[0][0])[i] = 0;
  }

  if (debug) {
    printf("[dbg][gemmini] cpu=%d mgr=%d before rr_acquire\n", cid, manager_id);
  }
  if (!rr_acquire_cfg_with_retry(GEMMINI_CFG_ID, (uint64_t)manager_id)) {
    if (debug) {
      printf("[dbg][gemmini] cpu=%d mgr=%d rr_acquire FAIL\n", cid, manager_id);
    }
    return false;
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
  if (debug) {
    printf("[dbg][gemmini] cpu=%d mgr=%d before rr_fence\n", cid, manager_id);
  }
  rr_fence(GEMMINI_CFG_ID);
  if (debug) {
    printf("[dbg][gemmini] cpu=%d mgr=%d after rr_fence\n", cid, manager_id);
  }

  bool ok = output_matches_reference(reference, output_mat);
  if (debug) {
    printf("[dbg][gemmini] cpu=%d mgr=%d before rr_release\n", cid, manager_id);
  }
  rr_release(GEMMINI_CFG_ID);
  if (debug) {
    printf("[dbg][gemmini] cpu=%d mgr=%d after rr_release ok=%d\n", cid, manager_id, ok ? 1 : 0);
  }
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

  for (int round = 0; round < 2; round++) {
    if (debug) {
      printf("[dbg][dma] cpu=%d mgr=%d round=%d before dma_set_dst/src\n", cid, manager_id, round);
    }
    fill_pattern(src, REROCC_DMA_BYTES, cid, manager_id, round);
    clear_buffer(dst, REROCC_DMA_BYTES);
    *completion_flag = 0;
    dma_set_dst((uint64_t)dst, (uint64_t)completion_flag);
    dma_set_src((uint64_t)src, (uint64_t)REROCC_DMA_BYTES);
    if (debug) {
      printf("[dbg][dma] cpu=%d mgr=%d round=%d before dma_fence_wait\n", cid, manager_id, round);
    }
    dma_fence_wait(completion_flag);
    if (debug) {
      printf("[dbg][dma] cpu=%d mgr=%d round=%d after dma_fence_wait\n", cid, manager_id, round);
      printf("[dbg][dma] cpu=%d mgr=%d round=%d before rr_fence\n", cid, manager_id, round);
    }
    rr_fence(DMA_CFG_ID);
    if (debug) {
      printf("[dbg][dma] cpu=%d mgr=%d round=%d after rr_fence\n", cid, manager_id, round);
    }
    if (!buffers_equal(src, dst, REROCC_DMA_BYTES)) {
      if (debug) {
        printf("[dbg][dma] cpu=%d mgr=%d round=%d buffers_equal FAIL\n", cid, manager_id, round);
      }
      rr_release(DMA_CFG_ID);
      return false;
    }
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
