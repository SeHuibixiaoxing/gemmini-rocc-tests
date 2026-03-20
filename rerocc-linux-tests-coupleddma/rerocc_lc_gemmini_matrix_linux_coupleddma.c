#define _GNU_SOURCE

#include <errno.h>
#include <pthread.h>
#include <sched.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#include "include/gemmini.h"
#include "include/gemmini_testutils.h"
#include "include/rerocc_coupleddma.h"
#include "rerocc-linux-tests/rerocc_control.h"
#include "rerocc_linux_pagemap.h"
#include "rerocc_linux_spm_xlate.h"

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

#define MAX_TEST_THREADS 64

#ifndef REROCC_ACQUIRE_MAX_RETRIES
#define REROCC_ACQUIRE_MAX_RETRIES 1000000UL
#endif

#ifndef REROCC_SPM_PAGE_BYTES
#define REROCC_SPM_PAGE_BYTES 1024U
#endif

#ifndef DMA_WAIT_SPINS
#define DMA_WAIT_SPINS 20000000UL
#endif

#define GEMMINI_CFG_ID 0
#define DMA_CFG_ID 1
#define SHARED_SPAD_GLOBAL_ADDR_BASE 0x40000000ULL
#define SHARED_SPAD_LOCAL_SIZE (1024 * 1024ULL)
#define SHARED_SPAD_LOCAL_ADDR_BASE(i) \
  (SHARED_SPAD_GLOBAL_ADDR_BASE + SHARED_SPAD_LOCAL_SIZE * (uint64_t)(i))
#define SHARED_SPAD_ALIGN_BYTES 64ULL
#define SHARED_SPAD_XLATE_RANGE_SIZE (SHARED_SPAD_LOCAL_SIZE)

typedef enum {
  MATRIX_FULL = 0,
  MATRIX_DIAGONAL = 1,
  MATRIX_SINGLE = 2,
} matrix_mode_t;

typedef struct {
  int cid;
  int num_gemmini;
  int num_dma;
  int gemmini_base_id;
  int dma_base_id;
  matrix_mode_t matrix_mode;
  int pass;
  int fail;
} gemmini_worker_t;

static elem_t resadd_a_global[MAX_TEST_THREADS][DIM][DIM] __attribute__((aligned(64)));
static elem_t resadd_b_global[MAX_TEST_THREADS][DIM][DIM] __attribute__((aligned(64)));
static elem_t resadd_out_global[MAX_TEST_THREADS][DIM][DIM] __attribute__((aligned(64)));
static elem_t resadd_gold_global[MAX_TEST_THREADS][DIM][DIM] __attribute__((aligned(64)));
static rerocc_linux_pagemap_t g_pagemap;
static rerocc_linux_spm_xlate_ctx_t g_spm_xlate;
static volatile uint32_t dma_completion_global[MAX_TEST_THREADS] __attribute__((aligned(4096)));
static uint64_t dma_completion_pa_global[MAX_TEST_THREADS];
static void *g_spm_alias_map = NULL;
static size_t g_spm_alias_map_bytes = 0;
static uint64_t g_spm_alias_base = 0;

static bool rr_acquire_cfg_with_retry(uint32_t cfg_id, uint64_t manager_id);

static inline uint32_t lcg_next(uint32_t *state) {
  *state = (*state) * 1664525u + 1013904223u;
  return *state;
}

static inline uint64_t align_up_u64(uint64_t value, uint64_t align) {
  return ((value + align - 1) / align) * align;
}

static inline uint64_t shared_spad_case_bytes(void) {
  return align_up_u64((uint64_t)(DIM * DIM * sizeof(elem_t)), SHARED_SPAD_ALIGN_BYTES);
}

static inline uint64_t shared_spad_core_stride_bytes(void) {
  return 2ULL * shared_spad_case_bytes();
}

static inline uint64_t shared_spad_case_addr(int local_gid, int cid, int case_idx) {
  return SHARED_SPAD_LOCAL_ADDR_BASE(local_gid) +
         shared_spad_core_stride_bytes() * (uint64_t)cid +
         shared_spad_case_bytes() * (uint64_t)case_idx;
}

static inline uint64_t shared_spad_case_vaddr(int local_gid, int cid, int case_idx) {
  return g_spm_alias_base +
         (shared_spad_case_addr(local_gid, cid, case_idx) - SHARED_SPAD_GLOBAL_ADDR_BASE);
}

static void maybe_lock_memory(void) {
  if (mlockall(MCL_CURRENT | MCL_FUTURE) != 0) {
    printf("warning: mlockall failed: %s\n", strerror(errno));
  }
}

static bool reserve_spm_alias_range(void) {
  size_t host_page_bytes;

  if (g_spm_alias_base != 0) {
    return true;
  }

  host_page_bytes = (size_t)getpagesize();
  if (host_page_bytes == 0) {
    printf("getpagesize failed for spm alias range\n");
    return false;
  }

  g_spm_alias_map_bytes = (size_t)align_up_u64(SHARED_SPAD_XLATE_RANGE_SIZE, (uint64_t)host_page_bytes);
  g_spm_alias_map = mmap(NULL,
                         g_spm_alias_map_bytes,
                         PROT_NONE,
                         MAP_PRIVATE | MAP_ANONYMOUS,
                         -1,
                         0);
  if (g_spm_alias_map == MAP_FAILED) {
    printf("spm alias mmap failed: %s\n", strerror(errno));
    g_spm_alias_map = NULL;
    g_spm_alias_map_bytes = 0;
    return false;
  }

  g_spm_alias_base = (uint64_t)(uintptr_t)g_spm_alias_map;
  return true;
}

static inline size_t min_size(size_t a, size_t b) {
  return a < b ? a : b;
}

static bool dma_copy_wait(uint64_t src_pa, uint64_t dst_pa, uint64_t completion_pa,
                          volatile uint32_t *completion, size_t nbytes) {
  *completion = 0;
  asm volatile("fence rw, rw");
  rerocc_coupleddma_set_dst(dst_pa, completion_pa);
  rerocc_coupleddma_set_src(src_pa, (uint64_t)nbytes);

  for (unsigned long spin = 0; spin < DMA_WAIT_SPINS; spin++) {
    asm volatile("fence r, rw");
    if (*completion != 0) {
      *completion = 0;
      asm volatile("fence");
      return true;
    }
    asm volatile("nop");
  }

  return false;
}

static bool issue_dma_copy(int dma_manager_id, uint64_t src_pa, uint64_t dst_pa,
                           volatile uint32_t *completion, uint64_t completion_pa,
                           size_t nbytes) {
  bool completed;

  if (!rr_acquire_cfg_with_retry(DMA_CFG_ID, (uint64_t)dma_manager_id)) {
    return false;
  }

  rr_set_opc(2, DMA_CFG_ID);
  completed = dma_copy_wait(src_pa, dst_pa, completion_pa, completion, nbytes);
  if (completed) {
    rr_fence(DMA_CFG_ID);
  }
  rr_release(DMA_CFG_ID);

  return completed;
}

static bool dma_copy_buffer_to_shared(int dma_manager_id, const void *src_buf, uint64_t dst_shared_pa,
                                      size_t nbytes, volatile uint32_t *completion,
                                      uint64_t completion_pa) {
  const uint8_t *src = (const uint8_t *)src_buf;
  const size_t page_bytes = (size_t)g_pagemap.page_size;
  size_t offset = 0;

  while (offset < nbytes) {
    uint64_t src_pa = 0;
    size_t room = page_bytes - (((size_t)(uintptr_t)(src + offset)) % page_bytes);
    size_t chunk = min_size(nbytes - offset, room);

    if (!rerocc_linux_virt_to_phys(&g_pagemap, (const void *)(src + offset), &src_pa)) {
      return false;
    }
    if (!issue_dma_copy(dma_manager_id,
                        src_pa,
                        dst_shared_pa + (uint64_t)offset,
                        completion,
                        completion_pa,
                        chunk)) {
      return false;
    }
    offset += chunk;
  }

  return true;
}

static bool dma_copy_shared_to_buffer(int dma_manager_id, uint64_t src_shared_pa, void *dst_buf,
                                      size_t nbytes, volatile uint32_t *completion,
                                      uint64_t completion_pa) {
  uint8_t *dst = (uint8_t *)dst_buf;
  const size_t page_bytes = (size_t)g_pagemap.page_size;
  size_t offset = 0;

  while (offset < nbytes) {
    uint64_t dst_pa = 0;
    size_t room = page_bytes - (((size_t)(uintptr_t)(dst + offset)) % page_bytes);
    size_t chunk = min_size(nbytes - offset, room);

    if (!rerocc_linux_virt_to_phys(&g_pagemap, (const void *)(dst + offset), &dst_pa)) {
      return false;
    }
    if (!issue_dma_copy(dma_manager_id,
                        src_shared_pa + (uint64_t)offset,
                        dst_pa,
                        completion,
                        completion_pa,
                        chunk)) {
      return false;
    }
    offset += chunk;
  }

  return true;
}

static int bind_thread_to_cpu(int cpu_id) {
  cpu_set_t set;
  CPU_ZERO(&set);
  CPU_SET(cpu_id, &set);
  return pthread_setaffinity_np(pthread_self(), sizeof(set), &set);
}

static void print_usage(const char *prog) {
  printf("Usage: %s [--num-cores N] [--num-gemmini G] [--num-dma D]\n", prog);
  printf("          [--gemmini-base-id B] [--dma-base-id B] [--matrix MODE]\n");
  printf("  MODE: full | diagonal | single\n");
}

static bool parse_int_arg(const char *name, const char *arg, int *dst) {
  char *end = NULL;
  long v = strtol(arg, &end, 10);
  if (end == NULL || *end != '\0') {
    printf("invalid integer for %s: %s\n", name, arg);
    return false;
  }
  if (v < 0 || v > 1000000) {
    printf("out of range integer for %s: %s\n", name, arg);
    return false;
  }
  *dst = (int)v;
  return true;
}

static bool parse_matrix_mode(const char *arg, matrix_mode_t *mode) {
  if (strcmp(arg, "full") == 0) {
    *mode = MATRIX_FULL;
    return true;
  }
  if (strcmp(arg, "diagonal") == 0) {
    *mode = MATRIX_DIAGONAL;
    return true;
  }
  if (strcmp(arg, "single") == 0) {
    *mode = MATRIX_SINGLE;
    return true;
  }
  printf("invalid matrix mode: %s\n", arg);
  return false;
}

static const char *matrix_mode_name(matrix_mode_t mode) {
  switch (mode) {
    case MATRIX_FULL:
      return "full";
    case MATRIX_DIAGONAL:
      return "diagonal";
    case MATRIX_SINGLE:
      return "single";
    default:
      return "unknown";
  }
}

static bool should_run_pair(matrix_mode_t mode, int cid, int gid, int num_gemmini) {
  switch (mode) {
    case MATRIX_FULL:
      return true;
    case MATRIX_DIAGONAL:
      return gid == (cid % num_gemmini);
    case MATRIX_SINGLE:
      return cid == 0 && gid == 0;
    default:
      return false;
  }
}

static int selected_pairs_for_cpu(matrix_mode_t mode, int cid, int num_gemmini) {
  switch (mode) {
    case MATRIX_FULL:
      return num_gemmini;
    case MATRIX_DIAGONAL:
      return num_gemmini > 0 ? 1 : 0;
    case MATRIX_SINGLE:
      return cid == 0 ? 1 : 0;
    default:
      return 0;
  }
}

static int selected_pairs(matrix_mode_t mode, int num_cores, int num_gemmini) {
  int total = 0;
  for (int cid = 0; cid < num_cores; cid++) {
    total += selected_pairs_for_cpu(mode, cid, num_gemmini);
  }
  return total;
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

static bool output_matches_reference(
    elem_t reference[BATCH_SIZE][OUT_ROW_DIM][OUT_COL_DIM][OUT_CHANNELS],
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

static bool spm_xlate_program_pair(uint64_t src_vaddr, uint64_t src_paddr,
                                   uint64_t dst_vaddr, uint64_t dst_paddr,
                                   uint64_t bytes) {
  rerocc_linux_spm_xlate_window_t window;

  rerocc_linux_spm_xlate_window_reset(&window);
  rerocc_linux_spm_xlate_clear(&g_spm_xlate);
  if (!rerocc_linux_spm_xlate_window_include(&g_spm_xlate, &window, src_vaddr, bytes) ||
      !rerocc_linux_spm_xlate_window_include(&g_spm_xlate, &window, dst_vaddr, bytes) ||
      !rerocc_linux_spm_xlate_map_range(&g_spm_xlate, &window, src_vaddr, src_paddr, bytes) ||
      !rerocc_linux_spm_xlate_map_range(&g_spm_xlate, &window, dst_vaddr, dst_paddr, bytes) ||
      !rerocc_linux_spm_xlate_program(&g_spm_xlate, &window, true)) {
    return false;
  }
  return true;
}

static inline void spm_xlate_reset(void) {
  rerocc_linux_spm_xlate_disable();
}

static bool run_one_gemmini_case(int cid, int local_gid, int manager_id,
                                 int num_dma, int dma_base_id) {
  elem_t input[BATCH_SIZE][IN_ROW_DIM][IN_COL_DIM][IN_CHANNELS] __attribute__((aligned(64)));
  elem_t weights[OUT_CHANNELS][KERNEL_DIM][KERNEL_DIM][IN_CHANNELS] __attribute__((aligned(64)));
  acc_t bias[OUT_CHANNELS] __attribute__((aligned(64)));
  elem_t reference[BATCH_SIZE][OUT_ROW_DIM][OUT_COL_DIM][OUT_CHANNELS] __attribute__((aligned(64)));
  elem_t weights_mat[PATCH_SIZE][OUT_CHANNELS] __attribute__((aligned(64)));
  elem_t output_mat[N_PATCHES][OUT_CHANNELS] __attribute__((aligned(64)));
  elem_t shared_src_shadow[DIM][DIM] __attribute__((aligned(64)));
  elem_t shared_dst_shadow[DIM][DIM] __attribute__((aligned(64)));
  elem_t (*resadd_a)[DIM] = resadd_a_global[cid];
  elem_t (*resadd_b)[DIM] = resadd_b_global[cid];
  elem_t (*resadd_out)[DIM] = resadd_out_global[cid];
  elem_t (*resadd_gold)[DIM] = resadd_gold_global[cid];
  uint32_t state = (uint32_t)(0x1234567u ^ (cid * 131u) ^ (manager_id * 977u));
  const int dma_manager_id = dma_base_id + local_gid;
  const size_t shared_mv_bytes = (size_t)(DIM * DIM * sizeof(elem_t));
  const bool trace_progress = (cid == 0 && local_gid == 0);
  bool conv_ok;
  bool resadd_ok = true;
  bool shared_mv_ok;

  if (local_gid >= num_dma) {
    printf("[dbg][gemmini] cpu=%d mgr=%d missing_dma local_gid=%d num_dma=%d\n",
           cid, manager_id, local_gid, num_dma);
    return false;
  }

  init_random_elem(&input[0][0][0][0], sizeof(input) / sizeof(elem_t), &state);
  init_random_elem(&weights[0][0][0][0], sizeof(weights) / sizeof(elem_t), &state);
  init_random_acc(&bias[0], sizeof(bias) / sizeof(acc_t), &state);
  flatten_weights(weights, weights_mat);
  cpu_conv_reference(input, weights, bias, reference);
  memset(output_mat, 0, sizeof(output_mat));

  if (trace_progress) {
    printf("[dbg][gemmini] cpu=%d mgr=%d stage=prepared\n", cid, manager_id);
    fflush(stdout);
  }

  if (!rr_acquire_cfg_with_retry(GEMMINI_CFG_ID, (uint64_t)manager_id)) {
    printf("[dbg][gemmini] cpu=%d mgr=%d acquire=0 conv=0 resadd=0 shared_mv=0\n", cid, manager_id);
    return false;
  }

  rr_set_opc(3, GEMMINI_CFG_ID);
  gemmini_flush(0);

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
      WS);

  rr_fence(GEMMINI_CFG_ID);
  gemmini_flush(0);
  rr_fence(GEMMINI_CFG_ID);

  if (trace_progress) {
    printf("[dbg][gemmini] cpu=%d mgr=%d stage=conv_done\n", cid, manager_id);
    fflush(stdout);
  }

  for (size_t i = 0; i < (size_t)(DIM * DIM); i++) {
    ((elem_t *)resadd_a)[i] = (elem_t)((int32_t)(lcg_next(&state) % 7) - 3);
    ((elem_t *)resadd_b)[i] = (elem_t)((int32_t)(lcg_next(&state) % 7) - 3);
    ((elem_t *)resadd_out)[i] = 0;
    ((elem_t *)resadd_gold)[i] = 0;
  }

  resadd_cpu(DIM, DIM, DIM, MVIN_SCALE_IDENTITY, MVIN_SCALE_IDENTITY, ACC_SCALE_IDENTITY,
             (elem_t *)resadd_a, (elem_t *)resadd_b, (elem_t *)resadd_gold, false);
  tiled_resadd_auto(DIM, DIM, MVIN_SCALE_IDENTITY, MVIN_SCALE_IDENTITY, ACC_SCALE_IDENTITY,
                    (elem_t *)resadd_a, (elem_t *)resadd_b, (elem_t *)resadd_out, false, WS);
  rr_fence(GEMMINI_CFG_ID);

  if (trace_progress) {
    printf("[dbg][gemmini] cpu=%d mgr=%d stage=resadd_done\n", cid, manager_id);
    fflush(stdout);
  }

  for (size_t i = 0; i < (size_t)(DIM * DIM); i++) {
    if (((elem_t *)resadd_out)[i] != ((elem_t *)resadd_gold)[i]) {
      resadd_ok = false;
      break;
    }
  }

  {
    uint64_t shared_src_addr = shared_spad_case_addr(local_gid, cid, 0);
    uint64_t shared_dst_addr = shared_spad_case_addr(local_gid, cid, 1);
    uint64_t shared_src_vaddr = shared_spad_case_vaddr(local_gid, cid, 0);
    uint64_t shared_dst_vaddr = shared_spad_case_vaddr(local_gid, cid, 1);
    elem_t *shared_src_va = (elem_t *)(uintptr_t)shared_src_vaddr;
    elem_t *shared_dst_va = (elem_t *)(uintptr_t)shared_dst_vaddr;
    bool shared_mv_xlate_ok = true;

    for (size_t i = 0; i < (size_t)(DIM * DIM); i++) {
      ((elem_t *)shared_src_shadow)[i] = (elem_t)((int32_t)(lcg_next(&state) % 9) - 4);
      ((elem_t *)shared_dst_shadow)[i] = 0;
    }

    if (!dma_copy_buffer_to_shared(dma_manager_id,
                                   (const void *)shared_src_shadow,
                                   shared_src_addr,
                                   shared_mv_bytes,
                                   &dma_completion_global[cid],
                                   dma_completion_pa_global[cid])) {
      shared_mv_xlate_ok = false;
    }

    gemmini_config_ld(DIM * sizeof(elem_t));
    gemmini_config_st(DIM * sizeof(elem_t));

    if (shared_mv_xlate_ok) {
      if (trace_progress) {
        printf("[dbg][gemmini] cpu=%d mgr=%d stage=shared_xlate_start\n", cid, manager_id);
        fflush(stdout);
      }
      if (!spm_xlate_program_pair(shared_src_vaddr,
                                  shared_src_addr,
                                  shared_dst_vaddr,
                                  shared_dst_addr,
                                  (uint64_t)shared_mv_bytes)) {
        shared_mv_xlate_ok = false;
      } else {
        gemmini_mvin(shared_src_va, 0);
        gemmini_mvout(shared_dst_va, 0);
        gemmini_fence();
        rr_fence(GEMMINI_CFG_ID);
      }

      if (shared_mv_xlate_ok) {
        memset(shared_dst_shadow, 0, shared_mv_bytes);
        if (!dma_copy_shared_to_buffer(dma_manager_id,
                                       shared_dst_addr,
                                       (void *)shared_dst_shadow,
                                       shared_mv_bytes,
                                       &dma_completion_global[cid],
                                       dma_completion_pa_global[cid])) {
          shared_mv_xlate_ok = false;
        } else {
          for (size_t i = 0; i < (size_t)(DIM * DIM); i++) {
            if (((elem_t *)shared_src_shadow)[i] != ((elem_t *)shared_dst_shadow)[i]) {
              shared_mv_xlate_ok = false;
              break;
            }
          }
        }
      }
    }

    spm_xlate_reset();
    if (trace_progress) {
      printf("[dbg][gemmini] cpu=%d mgr=%d stage=shared_xlate_done ok=%d\n",
             cid, manager_id, shared_mv_xlate_ok ? 1 : 0);
      fflush(stdout);
    }
    /* Bare-metal covers raw shared-spad physical addresses. Linux userspace
     * validates the supported alias-range xlate path to avoid hanging on
     * direct shared-spad passthrough pointers. */
    shared_mv_ok = shared_mv_xlate_ok;
  }

  conv_ok = output_matches_reference(reference, output_mat);
  rr_release(GEMMINI_CFG_ID);

  printf("[dbg][gemmini] cpu=%d mgr=%d conv=%d resadd=%d shared_mv=%d\n",
         cid, manager_id, conv_ok ? 1 : 0, resadd_ok ? 1 : 0, shared_mv_ok ? 1 : 0);
  return conv_ok && resadd_ok && shared_mv_ok;
}

static void *gemmini_worker_main(void *arg) {
  gemmini_worker_t *w = (gemmini_worker_t *)arg;

  if (bind_thread_to_cpu(w->cid) != 0) {
    w->fail = selected_pairs_for_cpu(w->matrix_mode, w->cid, w->num_gemmini);
    printf("[gemmini] bind failed cpu=%d\n", w->cid);
    return NULL;
  }

  for (int gid = 0; gid < w->num_gemmini; gid++) {
    int manager_id;
    bool ok;
    if (!should_run_pair(w->matrix_mode, w->cid, gid, w->num_gemmini)) {
      continue;
    }

    manager_id = w->gemmini_base_id + gid;
    ok = run_one_gemmini_case(w->cid, gid, manager_id, w->num_dma, w->dma_base_id);
    if (ok) {
      w->pass++;
      printf("[gemmini] cpu=%d mgr=%d PASS\n", w->cid, manager_id);
    } else {
      w->fail++;
      printf("[gemmini] cpu=%d mgr=%d FAIL\n", w->cid, manager_id);
    }
  }

  rr_release_all(RR_MAX_CFGS);
  return NULL;
}

int main(int argc, char **argv) {
  int num_cores = 4;
  int num_gemmini = 4;
  int num_dma = -1;
  int gemmini_base_id = 0;
  int dma_base_id = -1;
  matrix_mode_t matrix_mode = MATRIX_FULL;
  long online_cpus;
  pthread_t threads[MAX_TEST_THREADS];
  gemmini_worker_t workers[MAX_TEST_THREADS];
  int pass = 0;
  int fail = 0;

  rerocc_linux_pagemap_reset(&g_pagemap);

  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "--num-cores") == 0 && i + 1 < argc) {
      if (!parse_int_arg("--num-cores", argv[++i], &num_cores)) return 1;
    } else if (strcmp(argv[i], "--num-gemmini") == 0 && i + 1 < argc) {
      if (!parse_int_arg("--num-gemmini", argv[++i], &num_gemmini)) return 1;
    } else if (strcmp(argv[i], "--num-dma") == 0 && i + 1 < argc) {
      if (!parse_int_arg("--num-dma", argv[++i], &num_dma)) return 1;
    } else if (strcmp(argv[i], "--gemmini-base-id") == 0 && i + 1 < argc) {
      if (!parse_int_arg("--gemmini-base-id", argv[++i], &gemmini_base_id)) return 1;
    } else if (strcmp(argv[i], "--dma-base-id") == 0 && i + 1 < argc) {
      if (!parse_int_arg("--dma-base-id", argv[++i], &dma_base_id)) return 1;
    } else if (strcmp(argv[i], "--matrix") == 0 && i + 1 < argc) {
      if (!parse_matrix_mode(argv[++i], &matrix_mode)) return 1;
    } else if (strcmp(argv[i], "--help") == 0) {
      print_usage(argv[0]);
      return 0;
    } else {
      print_usage(argv[0]);
      return 1;
    }
  }

  if (num_dma < 0) {
    num_dma = num_gemmini;
  }
  if (dma_base_id < 0) {
    dma_base_id = num_gemmini;
  }
  if (num_cores <= 0 || num_gemmini <= 0 || num_dma <= 0) {
    printf("num-cores, num-gemmini, and num-dma must be > 0\n");
    return 1;
  }
  if (num_cores > MAX_TEST_THREADS) {
    printf("num-cores=%d exceeds MAX_TEST_THREADS=%d\n", num_cores, MAX_TEST_THREADS);
    return 1;
  }

  online_cpus = sysconf(_SC_NPROCESSORS_ONLN);
  if (online_cpus < num_cores) {
    printf("not enough online cpus: requested=%d online=%ld\n", num_cores, online_cpus);
    return 1;
  }

  maybe_lock_memory();
  rerocc_linux_spm_xlate_ctx_reset(&g_spm_xlate);
  if (!rerocc_linux_pagemap_init(&g_pagemap)) {
    return 1;
  }
  if (!reserve_spm_alias_range()) {
    return 1;
  }
  if (!rerocc_linux_spm_xlate_init(&g_spm_xlate, &g_pagemap)) {
    return 1;
  }
  for (int cid = 0; cid < num_cores; cid++) {
    dma_completion_global[cid] = 0;
    if (!rerocc_linux_virt_to_phys(&g_pagemap, (const void *)&dma_completion_global[cid],
                                   &dma_completion_pa_global[cid])) {
      printf("virt_to_phys failed for dma completion cid=%d\n", cid);
      return 1;
    }
  }
  memset(workers, 0, sizeof(workers));

  for (int cid = 0; cid < num_cores; cid++) {
    workers[cid].cid = cid;
    workers[cid].num_gemmini = num_gemmini;
    workers[cid].num_dma = num_dma;
    workers[cid].gemmini_base_id = gemmini_base_id;
    workers[cid].dma_base_id = dma_base_id;
    workers[cid].matrix_mode = matrix_mode;
    if (pthread_create(&threads[cid], NULL, gemmini_worker_main, &workers[cid]) != 0) {
      printf("pthread_create failed for cpu=%d\n", cid);
      return 1;
    }
  }

  for (int cid = 0; cid < num_cores; cid++) {
    (void)pthread_join(threads[cid], NULL);
  }

  for (int cid = 0; cid < num_cores; cid++) {
    pass += workers[cid].pass;
    fail += workers[cid].fail;
    printf("CORE_RESULT cid=%d gemmini_pass=%d gemmini_fail=%d\n",
           cid, workers[cid].pass, workers[cid].fail);
  }

  printf("GEMMINI_MATRIX_RESULT mode=%s pass=%d fail=%d expected=%d\n",
         matrix_mode_name(matrix_mode), pass, fail, selected_pairs(matrix_mode, num_cores, num_gemmini));
  if (fail == 0 && pass == selected_pairs(matrix_mode, num_cores, num_gemmini)) {
    printf("GEMMINI_ALL_PASS\n");
    return 0;
  }

  printf("GEMMINI_ALL_FAIL\n");
  return 1;
}
