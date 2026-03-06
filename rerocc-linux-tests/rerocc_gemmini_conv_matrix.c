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

#include "include/gemmini_testutils.h"
#include "rerocc_control.h"

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
#define REROCC_ACQUIRE_MAX_RETRIES 1000000UL

typedef enum {
  MATRIX_FULL = 0,
  MATRIX_DIAGONAL = 1,
  MATRIX_SINGLE = 2,
} matrix_mode_t;

typedef struct {
  int cid;
  int num_gemmini;
  int gemmini_base_id;
  matrix_mode_t matrix_mode;
  int pass;
  int fail;
} gemmini_worker_t;

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
          const int row = krow * KERNEL_DIM * IN_CHANNELS + kcol * IN_CHANNELS + inc;
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
  elem_t reference_output[BATCH_SIZE][OUT_ROW_DIM][OUT_COL_DIM][OUT_CHANNELS]) {
  for (int b = 0; b < BATCH_SIZE; b++) {
    for (int orow = 0; orow < OUT_ROW_DIM; orow++) {
      for (int ocol = 0; ocol < OUT_COL_DIM; ocol++) {
        for (int och = 0; och < OUT_CHANNELS; och++) {
          acc_t acc = bias[och];
          for (int krow = 0; krow < KERNEL_DIM; krow++) {
            for (int kcol = 0; kcol < KERNEL_DIM; kcol++) {
              for (int ich = 0; ich < IN_CHANNELS; ich++) {
                const int irow = orow * STRIDE + krow - PADDING;
                const int icol = ocol * STRIDE + kcol - PADDING;
                const elem_t px = (irow < 0 || irow >= IN_ROW_DIM || icol < 0 || icol >= IN_COL_DIM) ?
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
          reference_output[b][orow][ocol][och] = (elem_t)acc;
        }
      }
    }
  }
}

static bool output_matches_reference(
  elem_t reference_output[BATCH_SIZE][OUT_ROW_DIM][OUT_COL_DIM][OUT_CHANNELS],
  elem_t output_mat[N_PATCHES][OUT_CHANNELS]) {
  const elem_t *ref = &reference_output[0][0][0][0];
  const elem_t *out = &output_mat[0][0];
  const size_t n = sizeof(elem_t) * BATCH_SIZE * OUT_ROW_DIM * OUT_COL_DIM * OUT_CHANNELS;
  return memcmp(ref, out, n) == 0;
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

static void print_usage(const char *prog) {
  printf("Usage: %s [--num-cores N] [--num-gemmini G] [--gemmini-base-id B] [--matrix MODE]\n", prog);
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
  } else if (strcmp(arg, "diagonal") == 0) {
    *mode = MATRIX_DIAGONAL;
    return true;
  } else if (strcmp(arg, "single") == 0) {
    *mode = MATRIX_SINGLE;
    return true;
  }
  printf("invalid matrix mode: %s\n", arg);
  return false;
}

static const char *matrix_mode_name(matrix_mode_t mode) {
  switch (mode) {
    case MATRIX_FULL: return "full";
    case MATRIX_DIAGONAL: return "diagonal";
    case MATRIX_SINGLE: return "single";
    default: return "unknown";
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

static bool run_one_gemmini_case(int cid, int manager_id, uint32_t cfg_id) {
  elem_t input[BATCH_SIZE][IN_ROW_DIM][IN_COL_DIM][IN_CHANNELS] __attribute__((aligned(64)));
  elem_t weights[OUT_CHANNELS][KERNEL_DIM][KERNEL_DIM][IN_CHANNELS] __attribute__((aligned(64)));
  acc_t bias[OUT_CHANNELS] __attribute__((aligned(64)));
  elem_t reference_output[BATCH_SIZE][OUT_ROW_DIM][OUT_COL_DIM][OUT_CHANNELS] __attribute__((aligned(64)));
  elem_t weights_mat[PATCH_SIZE][OUT_CHANNELS] __attribute__((aligned(64)));
  elem_t output_mat[N_PATCHES][OUT_CHANNELS] __attribute__((aligned(64)));
  elem_t resadd_a[DIM][DIM] row_align(1);
  elem_t resadd_b[DIM][DIM] row_align(1);
  elem_t resadd_out[DIM][DIM] row_align(1);
  elem_t resadd_gold[DIM][DIM] row_align(1);
  bool conv_ok;
  bool resadd_ok = true;

  uint32_t state = (uint32_t)(0x1234567u ^ (cid * 131u) ^ (manager_id * 977u));
  init_random_elem(&input[0][0][0][0], sizeof(input) / sizeof(elem_t), &state);
  init_random_elem(&weights[0][0][0][0], sizeof(weights) / sizeof(elem_t), &state);
  init_random_acc(&bias[0], sizeof(bias) / sizeof(acc_t), &state);
  flatten_weights(weights, weights_mat);
  cpu_conv_reference(input, weights, bias, reference_output);
  memset(output_mat, 0, sizeof(output_mat));

  if (!rr_acquire_cfg_with_retry(cfg_id, (uint64_t)manager_id)) {
    return false;
  }

  rr_set_opc(3, cfg_id);
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
    WS
  );

  gemmini_flush(0);
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
  gemmini_fence();

  for (size_t i = 0; i < (size_t)(DIM * DIM); i++) {
    if (((elem_t *)resadd_out)[i] != ((elem_t *)resadd_gold)[i]) {
      resadd_ok = false;
      break;
    }
  }

  rr_fence(cfg_id);
  conv_ok = output_matches_reference(reference_output, output_mat);
  rr_release(cfg_id);
  return conv_ok && resadd_ok;
}

static void *gemmini_worker_main(void *arg) {
  gemmini_worker_t *w = (gemmini_worker_t *)arg;

  const int bind_rc = bind_thread_to_cpu(w->cid);
  if (bind_rc != 0) {
    w->fail = selected_pairs_for_cpu(w->matrix_mode, w->cid, w->num_gemmini);
    printf("[gemmini] bind failed cpu=%d rc=%d\n", w->cid, bind_rc);
    return NULL;
  }

  const uint32_t cfg_id = (uint32_t)(w->cid % RR_MAX_CFGS);
  for (int gid = 0; gid < w->num_gemmini; gid++) {
    if (!should_run_pair(w->matrix_mode, w->cid, gid, w->num_gemmini)) {
      continue;
    }

    const int manager_id = w->gemmini_base_id + gid;
    const bool ok = run_one_gemmini_case(w->cid, manager_id, cfg_id);
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
  int gemmini_base_id = 0;
  matrix_mode_t matrix_mode = MATRIX_FULL;

  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "--num-cores") == 0 && i + 1 < argc) {
      if (!parse_int_arg("--num-cores", argv[++i], &num_cores)) return 1;
    } else if (strcmp(argv[i], "--num-gemmini") == 0 && i + 1 < argc) {
      if (!parse_int_arg("--num-gemmini", argv[++i], &num_gemmini)) return 1;
    } else if (strcmp(argv[i], "--gemmini-base-id") == 0 && i + 1 < argc) {
      if (!parse_int_arg("--gemmini-base-id", argv[++i], &gemmini_base_id)) return 1;
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

  if (num_cores <= 0 || num_gemmini <= 0) {
    printf("num-cores and num-gemmini must be > 0\n");
    return 1;
  }
  if (num_cores > MAX_TEST_THREADS) {
    printf("num-cores=%d exceeds MAX_TEST_THREADS=%d\n", num_cores, MAX_TEST_THREADS);
    return 1;
  }

  const long online_cpus = sysconf(_SC_NPROCESSORS_ONLN);
  if (online_cpus < num_cores) {
    printf("not enough online cpus: requested=%d online=%ld\n", num_cores, online_cpus);
    return 1;
  }

  maybe_lock_memory();

  pthread_t threads[MAX_TEST_THREADS];
  gemmini_worker_t workers[MAX_TEST_THREADS];
  memset(workers, 0, sizeof(workers));

  for (int cid = 0; cid < num_cores; cid++) {
    workers[cid].cid = cid;
    workers[cid].num_gemmini = num_gemmini;
    workers[cid].gemmini_base_id = gemmini_base_id;
    workers[cid].matrix_mode = matrix_mode;
    if (pthread_create(&threads[cid], NULL, gemmini_worker_main, &workers[cid]) != 0) {
      printf("pthread_create failed for cpu=%d\n", cid);
      return 1;
    }
  }

  for (int cid = 0; cid < num_cores; cid++) {
    (void)pthread_join(threads[cid], NULL);
  }

  int pass = 0;
  int fail = 0;
  for (int cid = 0; cid < num_cores; cid++) {
    pass += workers[cid].pass;
    fail += workers[cid].fail;
    printf("CORE_RESULT cid=%d gemmini_pass=%d gemmini_fail=%d\n",
      cid, workers[cid].pass, workers[cid].fail);
  }

  const int expected = selected_pairs(matrix_mode, num_cores, num_gemmini);
  printf("GEMMINI_MATRIX_RESULT mode=%s pass=%d fail=%d expected=%d\n",
    matrix_mode_name(matrix_mode), pass, fail, expected);
  if (fail == 0 && pass == expected) {
    printf("GEMMINI_ALL_PASS\n");
    return 0;
  }

  printf("GEMMINI_ALL_FAIL\n");
  return 1;
}
