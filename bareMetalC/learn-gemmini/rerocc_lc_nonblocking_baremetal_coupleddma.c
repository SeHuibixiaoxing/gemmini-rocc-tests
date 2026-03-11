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

#ifndef REROCC_DMA_BYTES
#define REROCC_DMA_BYTES 512
#endif

#ifndef REROCC_ACQUIRE_MAX_RETRIES
#define REROCC_ACQUIRE_MAX_RETRIES 1000000UL
#endif

#ifndef DMA_WAIT_SPINS
#define DMA_WAIT_SPINS 2000000UL
#endif

#ifndef REROCC_LONG_CONV_ITERS
#define REROCC_LONG_CONV_ITERS 4
#endif

#ifndef REROCC_SHORT_CONV_ITERS
#define REROCC_SHORT_CONV_ITERS 1
#endif

#ifndef REROCC_LONG_RESADD_ITERS
#define REROCC_LONG_RESADD_ITERS 32
#endif

#ifndef REROCC_LONG_DMA_ITERS
#define REROCC_LONG_DMA_ITERS 4
#endif

#ifndef REROCC_SHORT_DMA_ITERS
#define REROCC_SHORT_DMA_ITERS 1
#endif

#ifndef REROCC_CORE1_DELAY_SPINS
#define REROCC_CORE1_DELAY_SPINS 2000UL
#endif

#define TEST_WORKER_CORES 2

#ifndef REROCC_DEBUG_FORCE_LOGICAL_CORES
#define REROCC_DEBUG_FORCE_LOGICAL_CORES 0
#endif

#define GEMMINI_CFG_ID 0
#define DMA_CFG_ID 1

#define SHARED_SPAD_GLOBAL_ADDR_BASE 0x40000000ULL
#define SHARED_SPAD_LOCAL_SIZE (1024 * 1024ULL)
#define SHARED_SPAD_LOCAL_ADDR_BASE(i) (SHARED_SPAD_GLOBAL_ADDR_BASE + SHARED_SPAD_LOCAL_SIZE * (uint64_t)(i))
#define SHARED_DMA_A_OFFSET 0x10000ULL
#define SHARED_DMA_B_OFFSET 0x20000ULL
#define SHARED_DMA_CORE_STRIDE 0x4000ULL

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

enum test_job {
  JOB_IDLE = 0,
  JOB_CONV_G0 = 1,
  JOB_CONV_G1 = 2,
  JOB_RESADD_G0 = 3,
  JOB_DMA_D0 = 4,
  JOB_DMA_D1 = 5,
};

static const char *job_name(enum test_job job) {
  switch (job) {
    case JOB_IDLE: return "idle";
    case JOB_CONV_G0: return "conv_g0";
    case JOB_CONV_G1: return "conv_g1";
    case JOB_RESADD_G0: return "resadd_g0";
    case JOB_DMA_D0: return "dma_d0";
    case JOB_DMA_D1: return "dma_d1";
    default: return "unknown";
  }
}

static volatile int barrier_count = 0;
static volatile int barrier_epoch = 0;
static volatile uint64_t stage_start_cycle = 0;
static volatile uint64_t stage_end_cycle = 0;
static volatile uint64_t stage_cycles[TEST_WORKER_CORES];
static volatile int stage_ok[TEST_WORKER_CORES];

typedef struct {
  bool valid;
  elem_t input[BATCH_SIZE][IN_ROW_DIM][IN_COL_DIM][IN_CHANNELS];
  elem_t weights_mat[PATCH_SIZE][OUT_CHANNELS];
  acc_t bias[OUT_CHANNELS];
  elem_t reference[BATCH_SIZE][OUT_ROW_DIM][OUT_COL_DIM][OUT_CHANNELS];
} conv_fixture_t;

static conv_fixture_t conv_fixture_global[TEST_WORKER_CORES][REROCC_NUM_GEMMINI] __attribute__((aligned(64)));

static elem_t resadd_a_global[TEST_WORKER_CORES][DIM][DIM] __attribute__((aligned(64)));
static elem_t resadd_b_global[TEST_WORKER_CORES][DIM][DIM] __attribute__((aligned(64)));
static elem_t resadd_out_global[TEST_WORKER_CORES][DIM][DIM] __attribute__((aligned(64)));
static elem_t resadd_gold_global[TEST_WORKER_CORES][DIM][DIM] __attribute__((aligned(64)));

static uint8_t dma_src_global[TEST_WORKER_CORES][REROCC_DMA_BYTES] __attribute__((aligned(64)));
static uint8_t dma_dst_global[TEST_WORKER_CORES][REROCC_DMA_BYTES] __attribute__((aligned(64)));
static volatile int dma_completion_global[TEST_WORKER_CORES] __attribute__((aligned(64)));

static inline uint64_t read_cycles_local(void) {
  return read_csr(mcycle);
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

static bool conv_output_matches(const elem_t *reference, const elem_t *output, size_t n) {
  for (size_t i = 0; i < n; i++) {
    if (reference[i] != output[i]) {
      return false;
    }
  }
  return true;
}

static conv_fixture_t *prepare_conv_fixture(int cid, int manager_id) {
  int fixture_id = manager_id - REROCC_GEMMINI_BASE_ID;
  if (fixture_id < 0 || fixture_id >= REROCC_NUM_GEMMINI) {
    return NULL;
  }

  conv_fixture_t *fixture = &conv_fixture_global[cid][fixture_id];
  if (fixture->valid) {
    return fixture;
  }

  elem_t weights4d[OUT_CHANNELS][KERNEL_DIM][KERNEL_DIM][IN_CHANNELS] __attribute__((aligned(64)));
  uint32_t seed = (uint32_t)(0x13572468u ^ (cid * 131u) ^ (manager_id * 977u));

  init_random_elem(&fixture->input[0][0][0][0], sizeof(fixture->input) / sizeof(elem_t), &seed);
  init_random_elem(&weights4d[0][0][0][0], sizeof(weights4d) / sizeof(elem_t), &seed);
  init_random_acc(fixture->bias, OUT_CHANNELS, &seed);

  memset(fixture->weights_mat, 0, sizeof(fixture->weights_mat));
  flatten_weights(weights4d, fixture->weights_mat);
  cpu_conv_reference(fixture->input, weights4d, fixture->bias, fixture->reference);
  fixture->valid = true;
  return fixture;
}

static bool warm_conv_fixtures(int cid) {
  for (int i = 0; i < REROCC_NUM_GEMMINI; i++) {
    if (prepare_conv_fixture(cid, REROCC_GEMMINI_BASE_ID + i) == NULL) {
      return false;
    }
  }
  return true;
}

static bool run_conv_workload(int cid, int manager_id, int iters, uint64_t *cycles_out) {
  conv_fixture_t *fixture = prepare_conv_fixture(cid, manager_id);
  elem_t output[N_PATCHES][OUT_CHANNELS] __attribute__((aligned(64)));
  if (fixture == NULL) {
    *cycles_out = 0;
    return false;
  }

  memset(output, 0, sizeof(output));

  if (!rr_acquire_cfg_with_retry(GEMMINI_CFG_ID, (uint64_t)manager_id)) {
    *cycles_out = 0;
    return false;
  }

  rr_set_opc(3, GEMMINI_CFG_ID);
  gemmini_flush(0);

  uint64_t t0 = read_cycles_local();
  for (int i = 0; i < iters; i++) {
    tiled_conv_auto(
      BATCH_SIZE, IN_ROW_DIM, IN_COL_DIM, IN_CHANNELS,
      OUT_CHANNELS, OUT_ROW_DIM, OUT_COL_DIM,
      STRIDE, 1, 1, PADDING, KERNEL_DIM,
      false, false, false, false, false,
      (elem_t *)fixture->input,
      (elem_t *)fixture->weights_mat,
      fixture->bias,
      (elem_t *)output,
      NO_ACTIVATION, ACC_SCALE_IDENTITY, 0, 0, 0,
      WS
    );
  }
  rr_fence(GEMMINI_CFG_ID);
  uint64_t t1 = read_cycles_local();
  rr_release(GEMMINI_CFG_ID);

  *cycles_out = t1 - t0;
  return conv_output_matches(&fixture->reference[0][0][0][0], &output[0][0],
    (size_t)BATCH_SIZE * OUT_ROW_DIM * OUT_COL_DIM * OUT_CHANNELS);
}

static bool run_resadd_workload(int cid, int manager_id, int iters, uint64_t *cycles_out) {
  elem_t (*a)[DIM] = resadd_a_global[cid];
  elem_t (*b)[DIM] = resadd_b_global[cid];
  elem_t (*out)[DIM] = resadd_out_global[cid];
  elem_t (*gold)[DIM] = resadd_gold_global[cid];

  uint32_t seed = (uint32_t)(0x89abcdefu ^ (cid * 193u) ^ (manager_id * 389u));
  for (size_t i = 0; i < (size_t)(DIM * DIM); i++) {
    ((elem_t *)a)[i] = (elem_t)((int32_t)(lcg_next(&seed) % 7) - 3);
    ((elem_t *)b)[i] = (elem_t)((int32_t)(lcg_next(&seed) % 7) - 3);
    ((elem_t *)out)[i] = 0;
    ((elem_t *)gold)[i] = 0;
  }

  resadd_cpu(DIM, DIM, DIM, MVIN_SCALE_IDENTITY, MVIN_SCALE_IDENTITY, ACC_SCALE_IDENTITY,
    (elem_t *)a, (elem_t *)b, (elem_t *)gold, false);

  if (!rr_acquire_cfg_with_retry(GEMMINI_CFG_ID, (uint64_t)manager_id)) {
    *cycles_out = 0;
    return false;
  }

  rr_set_opc(3, GEMMINI_CFG_ID);
  gemmini_flush(0);

  uint64_t t0 = read_cycles_local();
  for (int i = 0; i < iters; i++) {
    tiled_resadd_auto(DIM, DIM, MVIN_SCALE_IDENTITY, MVIN_SCALE_IDENTITY, ACC_SCALE_IDENTITY,
      (elem_t *)a, (elem_t *)b, (elem_t *)out, false, WS);
  }
  rr_fence(GEMMINI_CFG_ID);
  uint64_t t1 = read_cycles_local();
  rr_release(GEMMINI_CFG_ID);

  bool ok = true;
  for (size_t i = 0; i < (size_t)(DIM * DIM); i++) {
    if (((elem_t *)out)[i] != ((elem_t *)gold)[i]) {
      ok = false;
      break;
    }
  }

  *cycles_out = t1 - t0;
  return ok;
}

static void fill_pattern(uint8_t *buf, size_t n, uint32_t *seed) {
  for (size_t i = 0; i < n; i++) {
    buf[i] = (uint8_t)lcg_next(seed);
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

static int dma_manager_to_shared_gid(int dma_manager_id) {
  return dma_manager_id - REROCC_DMA_BASE_ID + REROCC_GEMMINI_BASE_ID;
}

static bool dma_issue_copy_and_wait(uint64_t src, uint64_t dst, volatile int *flag, size_t nbytes) {
  *flag = 0;
  rerocc_coupleddma_set_dst(dst, (uint64_t)(uintptr_t)flag);
  rerocc_coupleddma_set_src(src, (uint64_t)nbytes);

  for (unsigned long spin = 0; spin < DMA_WAIT_SPINS; spin++) {
    if (*flag != 0) {
      *flag = 0;
      asm volatile("fence");
      return true;
    }
    asm volatile("nop");
  }
  return false;
}

static bool run_dma_workload(int cid, int manager_id, int iters, uint64_t *cycles_out) {
  uint8_t *src = dma_src_global[cid];
  uint8_t *dst = dma_dst_global[cid];
  volatile int *completion = &dma_completion_global[cid];

  int gid = dma_manager_to_shared_gid(manager_id);
  uint64_t shared_a_addr = SHARED_SPAD_LOCAL_ADDR_BASE(gid) + SHARED_DMA_A_OFFSET + (uint64_t)cid * SHARED_DMA_CORE_STRIDE;
  uint64_t shared_b_addr = SHARED_SPAD_LOCAL_ADDR_BASE(gid) + SHARED_DMA_B_OFFSET + (uint64_t)cid * SHARED_DMA_CORE_STRIDE;
  uint8_t *shared_a = (uint8_t *)(uintptr_t)shared_a_addr;
  uint8_t *shared_b = (uint8_t *)(uintptr_t)shared_b_addr;

  if (!rr_acquire_cfg_with_retry(DMA_CFG_ID, (uint64_t)manager_id)) {
    *cycles_out = 0;
    return false;
  }
  rr_set_opc(2, DMA_CFG_ID);

  uint32_t seed = (uint32_t)(0x24681357u ^ (cid * 157u) ^ (manager_id * 491u));
  bool ok = true;
  uint64_t t0 = read_cycles_local();
  for (int i = 0; i < iters; i++) {
    fill_pattern(src, REROCC_DMA_BYTES, &seed);
    memset(dst, 0, REROCC_DMA_BYTES);
    memset(shared_a, 0, REROCC_DMA_BYTES);
    memset(shared_b, 0, REROCC_DMA_BYTES);

    if (!dma_issue_copy_and_wait((uint64_t)(uintptr_t)src, (uint64_t)(uintptr_t)shared_a, completion, REROCC_DMA_BYTES)) {
      ok = false;
      break;
    }
    if (!dma_issue_copy_and_wait((uint64_t)(uintptr_t)shared_a, (uint64_t)(uintptr_t)shared_b, completion, REROCC_DMA_BYTES)) {
      ok = false;
      break;
    }
    if (!dma_issue_copy_and_wait((uint64_t)(uintptr_t)shared_b, (uint64_t)(uintptr_t)dst, completion, REROCC_DMA_BYTES)) {
      ok = false;
      break;
    }
    if (!buffers_equal(src, dst, REROCC_DMA_BYTES)) {
      ok = false;
      break;
    }
  }
  rr_fence(DMA_CFG_ID);
  uint64_t t1 = read_cycles_local();
  rr_release(DMA_CFG_ID);

  *cycles_out = t1 - t0;
  return ok;
}

static bool run_job(int cid, enum test_job job, int iters, uint64_t *cycles_out) {
  switch (job) {
    case JOB_IDLE:
      *cycles_out = 0;
      return true;
    case JOB_CONV_G0:
      return run_conv_workload(cid, REROCC_GEMMINI_BASE_ID + 0, iters, cycles_out);
    case JOB_CONV_G1:
      return run_conv_workload(cid, REROCC_GEMMINI_BASE_ID + 1, iters, cycles_out);
    case JOB_RESADD_G0:
      return run_resadd_workload(cid, REROCC_GEMMINI_BASE_ID + 0, iters, cycles_out);
    case JOB_DMA_D0:
      return run_dma_workload(cid, REROCC_DMA_BASE_ID + 0, iters, cycles_out);
    case JOB_DMA_D1:
      return run_dma_workload(cid, REROCC_DMA_BASE_ID + 1, iters, cycles_out);
    default:
      *cycles_out = 0;
      return false;
  }
}

static void spin_delay(unsigned long spins) {
  for (unsigned long i = 0; i < spins; i++) {
    asm volatile("nop");
  }
}

static void run_stage(int cid, int logical_cores,
                      enum test_job core0_job, int core0_iters,
                      enum test_job core1_job, int core1_iters,
                      unsigned long core1_delay_spins,
                      uint64_t *wall_cycles_out,
                      uint64_t *core0_cycles_out,
                      uint64_t *core1_cycles_out,
                      bool *core0_ok_out,
                      bool *core1_ok_out) {
  barrier_wait(logical_cores);
  if (cid == 0) {
    stage_start_cycle = read_cycles_local();
  }
  barrier_wait(logical_cores);

  bool ok = true;
  uint64_t cycles = 0;
  if (cid == 0) {
    ok = run_job(cid, core0_job, core0_iters, &cycles);
  } else if (cid == 1) {
    if (core1_delay_spins != 0) {
      spin_delay(core1_delay_spins);
    }
    ok = run_job(cid, core1_job, core1_iters, &cycles);
  }
  stage_ok[cid] = ok ? 1 : 0;
  stage_cycles[cid] = cycles;
  __sync_synchronize();

  barrier_wait(logical_cores);
  if (cid == 0) {
    stage_end_cycle = read_cycles_local();
  }
  barrier_wait(logical_cores);

  if (cid == 0) {
    *wall_cycles_out = stage_end_cycle - stage_start_cycle;
    *core0_cycles_out = stage_cycles[0];
    *core1_cycles_out = stage_cycles[1];
    *core0_ok_out = stage_ok[0] != 0;
    *core1_ok_out = stage_ok[1] != 0;
  }
}

static bool run_overlap_scenario(int cid, int logical_cores, const char *name,
                                 enum test_job long_job, int long_iters,
                                 enum test_job short_job, int short_iters) {
  uint64_t wall = 0;
  uint64_t serial_long = 0, serial_short = 0;
  uint64_t c0 = 0, c1 = 0;
  bool ok0 = true, ok1 = true;

  if (cid == 0) {
    printf("SCENARIO_PHASE name=%s phase=serial_long core0=%s core1=%s iters0=%d iters1=%d\n",
      name, job_name(long_job), job_name(JOB_IDLE), long_iters, 0);
  }
  run_stage(cid, logical_cores,
    long_job, long_iters,
    JOB_IDLE, 0,
    0,
    &wall, &c0, &c1, &ok0, &ok1);
  if (cid == 0) {
    serial_long = c0;
    printf("SCENARIO_PHASE_DONE name=%s phase=serial_long ok0=%d ok1=%d wall=%lu c0=%lu c1=%lu\n",
      name, ok0 ? 1 : 0, ok1 ? 1 : 0,
      (unsigned long)wall, (unsigned long)c0, (unsigned long)c1);
  }

  if (cid == 0) {
    printf("SCENARIO_PHASE name=%s phase=serial_short core0=%s core1=%s iters0=%d iters1=%d\n",
      name, job_name(short_job), job_name(JOB_IDLE), short_iters, 0);
  }
  run_stage(cid, logical_cores,
    short_job, short_iters,
    JOB_IDLE, 0,
    0,
    &wall, &c0, &c1, &ok0, &ok1);
  if (cid == 0) {
    serial_short = c0;
    printf("SCENARIO_PHASE_DONE name=%s phase=serial_short ok0=%d ok1=%d wall=%lu c0=%lu c1=%lu\n",
      name, ok0 ? 1 : 0, ok1 ? 1 : 0,
      (unsigned long)wall, (unsigned long)c0, (unsigned long)c1);
  }

  if (cid == 0) {
    printf("SCENARIO_PHASE name=%s phase=parallel core0=%s core1=%s iters0=%d iters1=%d\n",
      name, job_name(long_job), job_name(short_job), long_iters, short_iters);
  }
  run_stage(cid, logical_cores,
    long_job, long_iters,
    short_job, short_iters,
    REROCC_CORE1_DELAY_SPINS,
    &wall, &c0, &c1, &ok0, &ok1);

  if (cid != 0) {
    return true;
  }

  printf("SCENARIO_PHASE_DONE name=%s phase=parallel ok0=%d ok1=%d wall=%lu c0=%lu c1=%lu\n",
    name, ok0 ? 1 : 0, ok1 ? 1 : 0,
    (unsigned long)wall, (unsigned long)c0, (unsigned long)c1);

  uint64_t serial_sum = serial_long + serial_short;
  bool overlap_observed = wall < serial_sum;
  bool short_finished_before_long = c1 < c0;
  // Non-blocking criterion: the delayed short job still finishes before the long job.
  bool pass = ok0 && ok1 && short_finished_before_long;

  printf("SCENARIO_RESULT name=%s pass=%d long_ok=%d short_ok=%d serial_sum=%lu parallel_wall=%lu long_cycles=%lu short_cycles=%lu overlap=%d short_before_long=%d\n",
    name, pass ? 1 : 0, ok0 ? 1 : 0, ok1 ? 1 : 0,
    (unsigned long)serial_sum, (unsigned long)wall,
    (unsigned long)c0, (unsigned long)c1, overlap_observed ? 1 : 0,
    short_finished_before_long ? 1 : 0);

  return pass;
}

void thread_entry(int cid, int nc) {
  int logical_cores = TEST_WORKER_CORES;
  if (REROCC_DEBUG_FORCE_LOGICAL_CORES > 0 && REROCC_DEBUG_FORCE_LOGICAL_CORES < logical_cores) {
    logical_cores = REROCC_DEBUG_FORCE_LOGICAL_CORES;
  }

  if (cid == 0) {
    printf("[rerocc-nonblocking] start runtime_nc=%d logical_cores=%d gemmini=%d dma=%d gemmini_base=%d dma_base=%d dma_bytes=%d\n",
      nc, logical_cores, REROCC_NUM_GEMMINI, REROCC_NUM_DMA,
      REROCC_GEMMINI_BASE_ID, REROCC_DMA_BASE_ID, REROCC_DMA_BYTES);
  }

  if (nc < logical_cores) {
    if (cid == 0) {
      printf("[rerocc-nonblocking] FAIL: requires at least %d cores, got %d\n", logical_cores, nc);
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

  if (REROCC_NUM_GEMMINI < 2 || REROCC_NUM_DMA < 2) {
    if (cid == 0) {
      printf("[rerocc-nonblocking] FAIL: requires >=2 gemmini and >=2 dma managers\n");
      exit(1);
    }
    while (1) {
      asm volatile("wfi");
    }
  }

  if (cid == 0) {
    printf("[rerocc-nonblocking] warmup_start gemmini=%d\n", REROCC_NUM_GEMMINI);
  }
  bool warm_ok = warm_conv_fixtures(cid);
  barrier_wait(logical_cores);
  if (!warm_ok) {
    if (cid == 0) {
      printf("[rerocc-nonblocking] FAIL: conv fixture warmup failed\n");
      exit(1);
    }
    while (1) {
      asm volatile("wfi");
    }
  }
  if (cid == 0) {
    printf("[rerocc-nonblocking] warmup_done\n");
  }

  bool s1 = run_overlap_scenario(cid, logical_cores,
    "conv_dma_parallel_nonblocking",
    JOB_CONV_G0, REROCC_LONG_CONV_ITERS,
    JOB_DMA_D0, REROCC_SHORT_DMA_ITERS);

  bool s2 = run_overlap_scenario(cid, logical_cores,
    "resadd_dma_parallel_nonblocking",
    JOB_RESADD_G0, REROCC_LONG_RESADD_ITERS,
    JOB_DMA_D0, REROCC_SHORT_DMA_ITERS);

  bool s3 = run_overlap_scenario(cid, logical_cores,
    "conv_g0_vs_conv_g1_nonblocking",
    JOB_CONV_G0, REROCC_LONG_CONV_ITERS,
    JOB_CONV_G1, REROCC_SHORT_CONV_ITERS);

  bool s4 = run_overlap_scenario(cid, logical_cores,
    "dma_d0_vs_dma_d1_nonblocking",
    JOB_DMA_D0, REROCC_LONG_DMA_ITERS,
    JOB_DMA_D1, REROCC_SHORT_DMA_ITERS);

  barrier_wait(logical_cores);
  if (cid != 0) {
    while (1) {
      asm volatile("wfi");
    }
  }

  rr_release_all(RR_MAX_CFGS);

  bool pass = s1 && s2 && s3 && s4;
  printf("NONBLOCKING_SUMMARY s1=%d s2=%d s3=%d s4=%d\n",
    s1 ? 1 : 0, s2 ? 1 : 0, s3 ? 1 : 0, s4 ? 1 : 0);
  if (pass) {
    printf("ALL_TESTS_PASS\n");
    exit(0);
  }
  printf("ALL_TESTS_FAIL\n");
  exit(1);
}

int main(void) {
  return 1;
}
