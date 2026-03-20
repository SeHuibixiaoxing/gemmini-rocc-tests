#define _GNU_SOURCE

#include <errno.h>
#include <pthread.h>
#include <sched.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

#include "include/gemmini.h"
#include "include/gemmini_testutils.h"
#include "include/rerocc_coupleddma.h"
#include "rerocc-linux-tests/rerocc_control.h"
#include "rerocc_linux_pagemap.h"

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
#define DMA_WAIT_SPINS 20000000UL
#endif

#ifndef REROCC_LONG_CONV_ITERS
#define REROCC_LONG_CONV_ITERS 16
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
#define GEMMINI_CFG_ID 0
#define DMA_CFG_ID 1

#define SHARED_SPAD_GLOBAL_ADDR_BASE 0x40000000ULL
#define SHARED_SPAD_LOCAL_SIZE (1024 * 1024ULL)
#define SHARED_SPAD_LOCAL_ADDR_BASE(i) \
  (SHARED_SPAD_GLOBAL_ADDR_BASE + SHARED_SPAD_LOCAL_SIZE * (uint64_t)(i))
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

typedef struct {
  bool valid;
  elem_t input[BATCH_SIZE][IN_ROW_DIM][IN_COL_DIM][IN_CHANNELS];
  elem_t weights_mat[PATCH_SIZE][OUT_CHANNELS];
  acc_t bias[OUT_CHANNELS];
  elem_t reference[BATCH_SIZE][OUT_ROW_DIM][OUT_COL_DIM][OUT_CHANNELS];
} conv_fixture_t;

typedef struct {
  int cid;
  int logical_cores;
  int long_conv_iters;
  int short_conv_iters;
  int long_resadd_iters;
  int long_dma_iters;
  int short_dma_iters;
  unsigned long core1_delay_spins;
  int num_gemmini;
  int num_dma;
  int bytes;
  int gemmini_base_id;
  int dma_base_id;
} worker_arg_t;

static rerocc_linux_pagemap_t g_pagemap;
static pthread_barrier_t g_stage_barrier;
static volatile uint64_t stage_start_ns = 0;
static volatile uint64_t stage_end_ns = 0;
static volatile uint64_t stage_job_ns[TEST_WORKER_CORES];
static volatile uint64_t stage_finish_ns[TEST_WORKER_CORES];
static volatile int stage_ok[TEST_WORKER_CORES];

static conv_fixture_t conv_fixture_global[TEST_WORKER_CORES][REROCC_NUM_GEMMINI] __attribute__((aligned(64)));
static elem_t resadd_a_global[TEST_WORKER_CORES][DIM][DIM] __attribute__((aligned(64)));
static elem_t resadd_b_global[TEST_WORKER_CORES][DIM][DIM] __attribute__((aligned(64)));
static elem_t resadd_out_global[TEST_WORKER_CORES][DIM][DIM] __attribute__((aligned(64)));
static elem_t resadd_gold_global[TEST_WORKER_CORES][DIM][DIM] __attribute__((aligned(64)));
static uint8_t dma_src_global[TEST_WORKER_CORES][REROCC_DMA_BYTES] __attribute__((aligned(4096)));
static uint8_t dma_dst_global[TEST_WORKER_CORES][REROCC_DMA_BYTES] __attribute__((aligned(4096)));
static volatile uint32_t dma_completion_global[TEST_WORKER_CORES] __attribute__((aligned(4096)));
static uint64_t dma_completion_pa_global[TEST_WORKER_CORES];

static void reset_rerocc_state(void) {
  rr_release_all(RR_MAX_CFGS);
  asm volatile("fence rw, rw");
}

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

static uint64_t monotonic_ns(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
  return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

static int bind_thread_to_cpu(int cpu_id) {
  cpu_set_t set;
  CPU_ZERO(&set);
  CPU_SET(cpu_id, &set);
  return pthread_setaffinity_np(pthread_self(), sizeof(set), &set);
}

static void maybe_lock_memory(void) {
  if (mlockall(MCL_CURRENT | MCL_FUTURE) != 0) {
    printf("warning: mlockall failed: %s\n", strerror(errno));
  }
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

static conv_fixture_t *prepare_conv_fixture(int cid, int manager_id, int gemmini_base_id, int num_gemmini) {
  int fixture_id = manager_id - gemmini_base_id;
  elem_t weights4d[OUT_CHANNELS][KERNEL_DIM][KERNEL_DIM][IN_CHANNELS] __attribute__((aligned(64)));
  uint32_t seed;
  conv_fixture_t *fixture;

  if (fixture_id < 0 || fixture_id >= num_gemmini || fixture_id >= REROCC_NUM_GEMMINI) {
    return NULL;
  }

  fixture = &conv_fixture_global[cid][fixture_id];
  if (fixture->valid) {
    return fixture;
  }

  seed = (uint32_t)(0x13572468u ^ (cid * 131u) ^ (manager_id * 977u));
  init_random_elem(&fixture->input[0][0][0][0], sizeof(fixture->input) / sizeof(elem_t), &seed);
  init_random_elem(&weights4d[0][0][0][0], sizeof(weights4d) / sizeof(elem_t), &seed);
  init_random_acc(fixture->bias, OUT_CHANNELS, &seed);
  memset(fixture->weights_mat, 0, sizeof(fixture->weights_mat));
  flatten_weights(weights4d, fixture->weights_mat);
  cpu_conv_reference(fixture->input, weights4d, fixture->bias, fixture->reference);
  fixture->valid = true;
  return fixture;
}

static bool warm_conv_fixtures(int cid, int gemmini_base_id, int num_gemmini) {
  for (int i = 0; i < num_gemmini; i++) {
    if (prepare_conv_fixture(cid, gemmini_base_id + i, gemmini_base_id, num_gemmini) == NULL) {
      return false;
    }
  }
  return true;
}

static bool run_conv_workload(int cid, int manager_id, int gemmini_base_id, int num_gemmini,
                              int iters, uint64_t *job_ns_out) {
  conv_fixture_t *fixture;
  elem_t output[N_PATCHES][OUT_CHANNELS] __attribute__((aligned(64)));
  uint64_t t0;
  uint64_t t1;

  fixture = prepare_conv_fixture(cid, manager_id, gemmini_base_id, num_gemmini);
  if (fixture == NULL) {
    *job_ns_out = 0;
    return false;
  }

  memset(output, 0, sizeof(output));
  if (!rr_acquire_cfg_with_retry(GEMMINI_CFG_ID, (uint64_t)manager_id)) {
    *job_ns_out = 0;
    return false;
  }

  rr_set_opc(3, GEMMINI_CFG_ID);
  gemmini_flush(0);

  t0 = monotonic_ns();
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
        WS);
  }
  rr_fence(GEMMINI_CFG_ID);
  t1 = monotonic_ns();
  rr_release(GEMMINI_CFG_ID);

  *job_ns_out = t1 - t0;
  return conv_output_matches(&fixture->reference[0][0][0][0], &output[0][0],
                             (size_t)BATCH_SIZE * OUT_ROW_DIM * OUT_COL_DIM * OUT_CHANNELS);
}

static bool run_resadd_workload(int cid, int manager_id, int iters, uint64_t *job_ns_out) {
  elem_t (*a)[DIM] = resadd_a_global[cid];
  elem_t (*b)[DIM] = resadd_b_global[cid];
  elem_t (*out)[DIM] = resadd_out_global[cid];
  elem_t (*gold)[DIM] = resadd_gold_global[cid];
  uint32_t seed = (uint32_t)(0x89abcdefu ^ (cid * 193u) ^ (manager_id * 389u));
  uint64_t t0;
  uint64_t t1;
  bool ok = true;

  for (size_t i = 0; i < (size_t)(DIM * DIM); i++) {
    ((elem_t *)a)[i] = (elem_t)((int32_t)(lcg_next(&seed) % 7) - 3);
    ((elem_t *)b)[i] = (elem_t)((int32_t)(lcg_next(&seed) % 7) - 3);
    ((elem_t *)out)[i] = 0;
    ((elem_t *)gold)[i] = 0;
  }

  resadd_cpu(DIM, DIM, DIM, MVIN_SCALE_IDENTITY, MVIN_SCALE_IDENTITY, ACC_SCALE_IDENTITY,
             (elem_t *)a, (elem_t *)b, (elem_t *)gold, false);

  if (!rr_acquire_cfg_with_retry(GEMMINI_CFG_ID, (uint64_t)manager_id)) {
    *job_ns_out = 0;
    return false;
  }

  rr_set_opc(3, GEMMINI_CFG_ID);
  gemmini_flush(0);

  t0 = monotonic_ns();
  for (int i = 0; i < iters; i++) {
    tiled_resadd_auto(DIM, DIM, MVIN_SCALE_IDENTITY, MVIN_SCALE_IDENTITY, ACC_SCALE_IDENTITY,
                      (elem_t *)a, (elem_t *)b, (elem_t *)out, false, WS);
  }
  rr_fence(GEMMINI_CFG_ID);
  t1 = monotonic_ns();
  rr_release(GEMMINI_CFG_ID);

  for (size_t i = 0; i < (size_t)(DIM * DIM); i++) {
    if (((elem_t *)out)[i] != ((elem_t *)gold)[i]) {
      ok = false;
      break;
    }
  }

  *job_ns_out = t1 - t0;
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

static bool dma_issue_copy_and_wait(uint64_t src_pa, uint64_t dst_pa,
                                    uint64_t done_pa, volatile uint32_t *flag,
                                    size_t nbytes) {
  *flag = 0;
  asm volatile("fence rw, rw");
  rerocc_coupleddma_set_dst(dst_pa, done_pa);
  rerocc_coupleddma_set_src(src_pa, (uint64_t)nbytes);

  for (unsigned long spin = 0; spin < DMA_WAIT_SPINS; spin++) {
    asm volatile("fence r, rw");
    if (*flag != 0) {
      *flag = 0;
      asm volatile("fence");
      return true;
    }
    asm volatile("nop");
  }
  return false;
}

static bool run_dma_workload(int cid, int manager_id, int dma_base_id, int bytes,
                             int iters, uint64_t *job_ns_out) {
  uint8_t *src = dma_src_global[cid];
  uint8_t *dst = dma_dst_global[cid];
  volatile uint32_t *completion = &dma_completion_global[cid];
  uint64_t completion_pa = dma_completion_pa_global[cid];
  int gid = manager_id - dma_base_id + REROCC_GEMMINI_BASE_ID;
  uint64_t shared_a_addr = SHARED_SPAD_LOCAL_ADDR_BASE(gid) + SHARED_DMA_A_OFFSET +
                           (uint64_t)cid * SHARED_DMA_CORE_STRIDE;
  uint64_t shared_b_addr = SHARED_SPAD_LOCAL_ADDR_BASE(gid) + SHARED_DMA_B_OFFSET +
                           (uint64_t)cid * SHARED_DMA_CORE_STRIDE;
  uint32_t seed = (uint32_t)(0x24681357u ^ (cid * 157u) ^ (manager_id * 491u));
  uint64_t t0;
  uint64_t t1;
  bool ok = true;
  uint64_t src_pa = 0;
  uint64_t dst_pa = 0;

  if (!rr_acquire_cfg_with_retry(DMA_CFG_ID, (uint64_t)manager_id)) {
    *job_ns_out = 0;
    return false;
  }
  rr_set_opc(2, DMA_CFG_ID);

  t0 = monotonic_ns();
  for (int i = 0; i < iters; i++) {
    fill_pattern(src, (size_t)bytes, &seed);
    memset(dst, 0, (size_t)bytes);

    if (!rerocc_linux_virt_to_phys(&g_pagemap, (const void *)src, &src_pa) ||
        !rerocc_linux_virt_to_phys(&g_pagemap, (const void *)dst, &dst_pa)) {
      ok = false;
      break;
    }

    if (!dma_issue_copy_and_wait(src_pa, shared_a_addr, completion_pa, completion, (size_t)bytes)) {
      ok = false;
      break;
    }
    if (!dma_issue_copy_and_wait(shared_a_addr, shared_b_addr, completion_pa, completion, (size_t)bytes)) {
      ok = false;
      break;
    }
    if (!dma_issue_copy_and_wait(shared_b_addr, dst_pa, completion_pa, completion, (size_t)bytes)) {
      ok = false;
      break;
    }
    if (!buffers_equal(src, dst, (size_t)bytes)) {
      ok = false;
      break;
    }
  }
  rr_fence(DMA_CFG_ID);
  t1 = monotonic_ns();
  rr_release(DMA_CFG_ID);

  *job_ns_out = t1 - t0;
  return ok;
}

static bool run_job(const worker_arg_t *cfg, int cid, enum test_job job, int iters, uint64_t *job_ns_out) {
  switch (job) {
    case JOB_IDLE:
      *job_ns_out = 0;
      return true;
    case JOB_CONV_G0:
      return run_conv_workload(cid, cfg->gemmini_base_id + 0, cfg->gemmini_base_id,
                               cfg->num_gemmini, iters, job_ns_out);
    case JOB_CONV_G1:
      return run_conv_workload(cid, cfg->gemmini_base_id + 1, cfg->gemmini_base_id,
                               cfg->num_gemmini, iters, job_ns_out);
    case JOB_RESADD_G0:
      return run_resadd_workload(cid, cfg->gemmini_base_id + 0, iters, job_ns_out);
    case JOB_DMA_D0:
      return run_dma_workload(cid, cfg->dma_base_id + 0, cfg->dma_base_id,
                              cfg->bytes, iters, job_ns_out);
    case JOB_DMA_D1:
      return run_dma_workload(cid, cfg->dma_base_id + 1, cfg->dma_base_id,
                              cfg->bytes, iters, job_ns_out);
    default:
      *job_ns_out = 0;
      return false;
  }
}

static void spin_delay(unsigned long spins) {
  for (unsigned long i = 0; i < spins; i++) {
    asm volatile("nop");
  }
}

static void run_stage(const worker_arg_t *cfg, int cid,
                      enum test_job core0_job, int core0_iters,
                      enum test_job core1_job, int core1_iters,
                      unsigned long core1_delay_spins,
                      uint64_t *wall_ns_out,
                      uint64_t *core0_job_ns_out,
                      uint64_t *core1_job_ns_out,
                      uint64_t *core0_finish_ns_out,
                      uint64_t *core1_finish_ns_out,
                      bool *core0_ok_out,
                      bool *core1_ok_out) {
  bool ok = true;
  uint64_t t0;
  uint64_t t1;
  uint64_t stage_origin;
  uint64_t job_ns = 0;

  (void)cfg;
  pthread_barrier_wait(&g_stage_barrier);
  if (cid == 0) {
    stage_start_ns = monotonic_ns();
  }
  pthread_barrier_wait(&g_stage_barrier);

  stage_origin = stage_start_ns;
  t0 = monotonic_ns();
  if (cid == 0) {
    ok = run_job(cfg, cid, core0_job, core0_iters, &job_ns);
  } else if (cid == 1) {
    if (core1_delay_spins != 0) {
      spin_delay(core1_delay_spins);
    }
    ok = run_job(cfg, cid, core1_job, core1_iters, &job_ns);
  }
  t1 = monotonic_ns();
  stage_job_ns[cid] = job_ns;
  stage_ok[cid] = ok ? 1 : 0;
  stage_finish_ns[cid] = t1 - stage_origin;

  pthread_barrier_wait(&g_stage_barrier);
  if (cid == 0) {
    stage_end_ns = monotonic_ns();
  }
  pthread_barrier_wait(&g_stage_barrier);

  if (cid == 0) {
    *wall_ns_out = stage_end_ns - stage_start_ns;
    *core0_job_ns_out = stage_job_ns[0];
    *core1_job_ns_out = stage_job_ns[1];
    *core0_finish_ns_out = stage_finish_ns[0];
    *core1_finish_ns_out = stage_finish_ns[1];
    *core0_ok_out = stage_ok[0] != 0;
    *core1_ok_out = stage_ok[1] != 0;
  }
}

static bool run_overlap_scenario(const worker_arg_t *cfg, int cid, const char *name,
                                 enum test_job long_job, int long_iters,
                                 enum test_job short_job, int short_iters) {
  uint64_t wall = 0;
  uint64_t serial_long = 0;
  uint64_t serial_short = 0;
  uint64_t c0 = 0;
  uint64_t c1 = 0;
  uint64_t f0 = 0;
  uint64_t f1 = 0;
  bool ok0 = true;
  bool ok1 = true;
  uint64_t serial_sum;
  bool overlap_observed;
  bool short_finished_before_long;
  bool pass;

  if (cid == 0) {
    printf("SCENARIO_PHASE name=%s phase=serial_long core0=%s core1=%s iters0=%d iters1=%d\n",
           name, job_name(long_job), job_name(JOB_IDLE), long_iters, 0);
  }
  run_stage(cfg, cid,
            long_job, long_iters,
            JOB_IDLE, 0,
            0,
            &wall, &c0, &c1, &f0, &f1, &ok0, &ok1);
  if (cid == 0) {
    serial_long = c0;
    printf("SCENARIO_PHASE_DONE name=%s phase=serial_long ok0=%d ok1=%d wall_ns=%lu c0_ns=%lu c1_ns=%lu\n",
           name, ok0 ? 1 : 0, ok1 ? 1 : 0,
           (unsigned long)wall, (unsigned long)c0, (unsigned long)c1);
  }

  if (cid == 0) {
    printf("SCENARIO_PHASE name=%s phase=serial_short core0=%s core1=%s iters0=%d iters1=%d\n",
           name, job_name(short_job), job_name(JOB_IDLE), short_iters, 0);
  }
  run_stage(cfg, cid,
            short_job, short_iters,
            JOB_IDLE, 0,
            0,
            &wall, &c0, &c1, &f0, &f1, &ok0, &ok1);
  if (cid == 0) {
    serial_short = c0;
    printf("SCENARIO_PHASE_DONE name=%s phase=serial_short ok0=%d ok1=%d wall_ns=%lu c0_ns=%lu c1_ns=%lu\n",
           name, ok0 ? 1 : 0, ok1 ? 1 : 0,
           (unsigned long)wall, (unsigned long)c0, (unsigned long)c1);
  }

  if (cid == 0) {
    printf("SCENARIO_PHASE name=%s phase=parallel core0=%s core1=%s iters0=%d iters1=%d\n",
           name, job_name(long_job), job_name(short_job), long_iters, short_iters);
  }
  run_stage(cfg, cid,
            long_job, long_iters,
            short_job, short_iters,
            cfg->core1_delay_spins,
            &wall, &c0, &c1, &f0, &f1, &ok0, &ok1);

  if (cid != 0) {
    return true;
  }

  printf("SCENARIO_PHASE_DONE name=%s phase=parallel ok0=%d ok1=%d wall_ns=%lu c0_ns=%lu c1_ns=%lu finish0_ns=%lu finish1_ns=%lu\n",
         name, ok0 ? 1 : 0, ok1 ? 1 : 0,
         (unsigned long)wall, (unsigned long)c0, (unsigned long)c1,
         (unsigned long)f0, (unsigned long)f1);

  serial_sum = serial_long + serial_short;
  overlap_observed = wall < serial_sum;
  short_finished_before_long = f1 < f0;
  pass = ok0 && ok1 && short_finished_before_long;

  printf("SCENARIO_RESULT name=%s pass=%d long_ok=%d short_ok=%d serial_sum_ns=%lu parallel_wall_ns=%lu long_job_ns=%lu short_job_ns=%lu overlap=%d short_before_long=%d\n",
         name, pass ? 1 : 0, ok0 ? 1 : 0, ok1 ? 1 : 0,
         (unsigned long)serial_sum, (unsigned long)wall,
         (unsigned long)c0, (unsigned long)c1,
         overlap_observed ? 1 : 0, short_finished_before_long ? 1 : 0);

  return pass;
}

static void print_usage(const char *prog) {
  printf("Usage: %s [--target 2c2g2d|4c4g4d|custom]\n", prog);
  printf("          [--num-cores N] [--num-gemmini G] [--num-dma D] [--bytes B]\n");
  printf("          [--gemmini-base-id B] [--dma-base-id B]\n");
  printf("          [--long-conv-iters N] [--short-conv-iters N]\n");
  printf("          [--long-resadd-iters N] [--long-dma-iters N] [--short-dma-iters N]\n");
}

static bool parse_int_arg(const char *name, const char *arg, int *dst) {
  char *end = NULL;
  long v = strtol(arg, &end, 10);
  if (end == NULL || *end != '\0') {
    printf("invalid integer for %s: %s\n", name, arg);
    return false;
  }
  if (v < 0 || v > 100000000) {
    printf("out of range integer for %s: %s\n", name, arg);
    return false;
  }
  *dst = (int)v;
  return true;
}

static void *worker_main(void *arg) {
  worker_arg_t *cfg = (worker_arg_t *)arg;
  int cid = cfg->cid;
  bool warm_ok;
  bool s1;
  bool s2;
  bool s3;
  bool s4;
  int bind_rc;

  bind_rc = bind_thread_to_cpu(cid);
  if (bind_rc != 0) {
    if (cid == 0) {
      printf("[rerocc-nonblocking-linux] FAIL: bind cpu=%d rc=%d\n", cid, bind_rc);
    }
    return (void *)(uintptr_t)1;
  }

  warm_ok = warm_conv_fixtures(cid, cfg->gemmini_base_id, cfg->num_gemmini);
  pthread_barrier_wait(&g_stage_barrier);
  if (!warm_ok) {
    if (cid == 0) {
      printf("[rerocc-nonblocking-linux] FAIL: conv fixture warmup failed\n");
    }
    return (void *)(uintptr_t)1;
  }

  if (cid == 0) {
    printf("[rerocc-nonblocking-linux] warmup_done\n");
  }

  s1 = run_overlap_scenario(cfg, cid,
                            "conv_dma_parallel_nonblocking",
                            JOB_CONV_G0, cfg->long_conv_iters,
                            JOB_DMA_D0, cfg->short_dma_iters);

  s2 = run_overlap_scenario(cfg, cid,
                            "resadd_dma_parallel_nonblocking",
                            JOB_RESADD_G0, cfg->long_resadd_iters,
                            JOB_DMA_D0, cfg->short_dma_iters);

  s3 = run_overlap_scenario(cfg, cid,
                            "conv_g0_vs_conv_g1_nonblocking",
                            JOB_CONV_G0, cfg->long_conv_iters,
                            JOB_CONV_G1, cfg->short_conv_iters);

  s4 = run_overlap_scenario(cfg, cid,
                            "dma_d0_vs_dma_d1_nonblocking",
                            JOB_DMA_D0, cfg->long_dma_iters,
                            JOB_DMA_D1, cfg->short_dma_iters);

  pthread_barrier_wait(&g_stage_barrier);
  rr_release_all(RR_MAX_CFGS);

  if (cid == 0) {
    bool pass = s1 && s2 && s3 && s4;
    printf("NONBLOCKING_SUMMARY s1=%d s2=%d s3=%d s4=%d\n",
           s1 ? 1 : 0, s2 ? 1 : 0, s3 ? 1 : 0, s4 ? 1 : 0);
    if (pass) {
      printf("ALL_TESTS_PASS\n");
      return NULL;
    }
    printf("ALL_TESTS_FAIL\n");
    return (void *)(uintptr_t)1;
  }

  return NULL;
}

int main(int argc, char **argv) {
  const char *target = "2c2g2d";
  int num_cores = -1;
  int num_gemmini = -1;
  int num_dma = -1;
  int bytes = REROCC_DMA_BYTES;
  int gemmini_base_id = REROCC_GEMMINI_BASE_ID;
  int dma_base_id = -1;
  int long_conv_iters = REROCC_LONG_CONV_ITERS;
  int short_conv_iters = REROCC_SHORT_CONV_ITERS;
  int long_resadd_iters = REROCC_LONG_RESADD_ITERS;
  int long_dma_iters = REROCC_LONG_DMA_ITERS;
  int short_dma_iters = REROCC_SHORT_DMA_ITERS;
  long online_cpus;
  pthread_t threads[TEST_WORKER_CORES];
  worker_arg_t args[TEST_WORKER_CORES];
  void *thread_rc = NULL;

  rerocc_linux_pagemap_reset(&g_pagemap);

  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "--target") == 0 && i + 1 < argc) {
      target = argv[++i];
    } else if (strcmp(argv[i], "--num-cores") == 0 && i + 1 < argc) {
      if (!parse_int_arg("--num-cores", argv[++i], &num_cores)) return 1;
    } else if (strcmp(argv[i], "--num-gemmini") == 0 && i + 1 < argc) {
      if (!parse_int_arg("--num-gemmini", argv[++i], &num_gemmini)) return 1;
    } else if (strcmp(argv[i], "--num-dma") == 0 && i + 1 < argc) {
      if (!parse_int_arg("--num-dma", argv[++i], &num_dma)) return 1;
    } else if (strcmp(argv[i], "--bytes") == 0 && i + 1 < argc) {
      if (!parse_int_arg("--bytes", argv[++i], &bytes)) return 1;
    } else if (strcmp(argv[i], "--gemmini-base-id") == 0 && i + 1 < argc) {
      if (!parse_int_arg("--gemmini-base-id", argv[++i], &gemmini_base_id)) return 1;
    } else if (strcmp(argv[i], "--dma-base-id") == 0 && i + 1 < argc) {
      if (!parse_int_arg("--dma-base-id", argv[++i], &dma_base_id)) return 1;
    } else if (strcmp(argv[i], "--long-conv-iters") == 0 && i + 1 < argc) {
      if (!parse_int_arg("--long-conv-iters", argv[++i], &long_conv_iters)) return 1;
    } else if (strcmp(argv[i], "--short-conv-iters") == 0 && i + 1 < argc) {
      if (!parse_int_arg("--short-conv-iters", argv[++i], &short_conv_iters)) return 1;
    } else if (strcmp(argv[i], "--long-resadd-iters") == 0 && i + 1 < argc) {
      if (!parse_int_arg("--long-resadd-iters", argv[++i], &long_resadd_iters)) return 1;
    } else if (strcmp(argv[i], "--long-dma-iters") == 0 && i + 1 < argc) {
      if (!parse_int_arg("--long-dma-iters", argv[++i], &long_dma_iters)) return 1;
    } else if (strcmp(argv[i], "--short-dma-iters") == 0 && i + 1 < argc) {
      if (!parse_int_arg("--short-dma-iters", argv[++i], &short_dma_iters)) return 1;
    } else if (strcmp(argv[i], "--help") == 0) {
      print_usage(argv[0]);
      return 0;
    } else {
      print_usage(argv[0]);
      return 1;
    }
  }

  if (strcmp(target, "2c2g2d") == 0 || strcmp(target, "small") == 0) {
    if (num_cores < 0) num_cores = 2;
    if (num_gemmini < 0) num_gemmini = 2;
    if (num_dma < 0) num_dma = 2;
  } else if (strcmp(target, "4c4g4d") == 0 || strcmp(target, "default") == 0) {
    if (num_cores < 0) num_cores = 4;
    if (num_gemmini < 0) num_gemmini = 4;
    if (num_dma < 0) num_dma = 4;
  } else if (strcmp(target, "custom") == 0) {
    if (num_cores < 0) num_cores = 2;
    if (num_gemmini < 0) num_gemmini = 2;
    if (num_dma < 0) num_dma = 2;
  } else {
    printf("unknown target profile: %s\n", target);
    print_usage(argv[0]);
    return 1;
  }

  if (dma_base_id < 0) {
    dma_base_id = num_gemmini;
  }
  if (num_cores < TEST_WORKER_CORES) {
    printf("[rerocc-nonblocking-linux] FAIL: requires at least %d cores, got %d\n",
           TEST_WORKER_CORES, num_cores);
    return 1;
  }
  if (num_gemmini < 2 || num_dma < 2) {
    printf("[rerocc-nonblocking-linux] FAIL: requires >=2 gemmini and >=2 dma managers\n");
    return 1;
  }

  online_cpus = sysconf(_SC_NPROCESSORS_ONLN);
  if (online_cpus < TEST_WORKER_CORES) {
    printf("[rerocc-nonblocking-linux] FAIL: online_cpus=%ld need=%d\n",
           online_cpus, TEST_WORKER_CORES);
    return 1;
  }

  reset_rerocc_state();
  maybe_lock_memory();
  if (!rerocc_linux_pagemap_init(&g_pagemap)) {
    return 1;
  }

  for (int cid = 0; cid < TEST_WORKER_CORES; cid++) {
    dma_completion_global[cid] = 0;
    if (!rerocc_linux_virt_to_phys(&g_pagemap, (const void *)&dma_completion_global[cid],
                                   &dma_completion_pa_global[cid])) {
      printf("virt_to_phys failed for dma completion cid=%d\n", cid);
      return 1;
    }
  }

  if (pthread_barrier_init(&g_stage_barrier, NULL, TEST_WORKER_CORES) != 0) {
    printf("pthread_barrier_init failed\n");
    return 1;
  }

  printf("[rerocc-nonblocking-linux] start num_cores=%d gemmini=%d dma=%d gemmini_base=%d dma_base=%d dma_bytes=%d\n",
         num_cores, num_gemmini, num_dma, gemmini_base_id, dma_base_id, bytes);
  printf("[rerocc-nonblocking-linux] warmup_start gemmini=%d\n", num_gemmini);

  memset(args, 0, sizeof(args));
  for (int cid = 0; cid < TEST_WORKER_CORES; cid++) {
    args[cid].cid = cid;
    args[cid].logical_cores = TEST_WORKER_CORES;
    args[cid].long_conv_iters = long_conv_iters;
    args[cid].short_conv_iters = short_conv_iters;
    args[cid].long_resadd_iters = long_resadd_iters;
    args[cid].long_dma_iters = long_dma_iters;
    args[cid].short_dma_iters = short_dma_iters;
    args[cid].core1_delay_spins = REROCC_CORE1_DELAY_SPINS;
    args[cid].num_gemmini = num_gemmini;
    args[cid].num_dma = num_dma;
    args[cid].bytes = bytes;
    args[cid].gemmini_base_id = gemmini_base_id;
    args[cid].dma_base_id = dma_base_id;

    if (pthread_create(&threads[cid], NULL, worker_main, &args[cid]) != 0) {
      printf("pthread_create failed cid=%d\n", cid);
      return 1;
    }
  }

  for (int cid = 0; cid < TEST_WORKER_CORES; cid++) {
    if (pthread_join(threads[cid], &thread_rc) != 0) {
      printf("pthread_join failed cid=%d\n", cid);
      return 1;
    }
    if (thread_rc != NULL) {
      return 1;
    }
  }

  pthread_barrier_destroy(&g_stage_barrier);
  return 0;
}
