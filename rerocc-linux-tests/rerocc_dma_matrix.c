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

#include "rocc-software/src/xcustom.h"
#include "rerocc_control.h"

#define DMA_XCUSTOM 2
#define MAX_TEST_THREADS 64
#define REROCC_ACQUIRE_MAX_RETRIES 1000000UL

typedef enum {
  MATRIX_FULL = 0,
  MATRIX_DIAGONAL = 1,
  MATRIX_SINGLE = 2,
} matrix_mode_t;

typedef struct {
  int cid;
  int num_dma;
  int dma_base_id;
  int bytes;
  matrix_mode_t matrix_mode;
  uint8_t *src;
  uint8_t *dst;
  volatile int completion_flag;
  int pass;
  int fail;
} dma_worker_t;

static inline void dma_set_dst(uint64_t addr, volatile int *completion_flag) {
  ROCC_INSTRUCTION_0_R_R(DMA_XCUSTOM, addr, (uint64_t)completion_flag, 2);
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

static void fill_pattern(uint8_t *buf, size_t n, int cpu_id, int dma_idx, int round) {
  for (size_t i = 0; i < n; i++) {
    const uint8_t seed = (uint8_t)((cpu_id + 1) * 19 + (dma_idx + 3) * 7 + (round + 1) * 11);
    buf[i] = (uint8_t)(seed + (uint8_t)i);
  }
}

static void print_usage(const char *prog) {
  printf("Usage: %s [--num-cores N] [--num-gemmini G] [--num-dma D] [--dma-base-id B] [--bytes N] [--matrix MODE]\n", prog);
  printf("  MODE: full | diagonal | single\n");
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

static bool should_run_pair(matrix_mode_t mode, int cid, int did, int num_dma) {
  switch (mode) {
    case MATRIX_FULL:
      return true;
    case MATRIX_DIAGONAL:
      return did == (cid % num_dma);
    case MATRIX_SINGLE:
      return cid == 0 && did == 0;
    default:
      return false;
  }
}

static int selected_pairs_for_cpu(matrix_mode_t mode, int cid, int num_dma) {
  switch (mode) {
    case MATRIX_FULL:
      return num_dma;
    case MATRIX_DIAGONAL:
      return num_dma > 0 ? 1 : 0;
    case MATRIX_SINGLE:
      return cid == 0 ? 1 : 0;
    default:
      return 0;
  }
}

static int selected_pairs(matrix_mode_t mode, int num_cores, int num_dma) {
  int total = 0;
  for (int cid = 0; cid < num_cores; cid++) {
    total += selected_pairs_for_cpu(mode, cid, num_dma);
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

static bool run_one_dma_case(dma_worker_t *w, int dma_idx, uint32_t cfg_id) {
  const int manager_id = w->dma_base_id + dma_idx;

  if (!rr_acquire_cfg_with_retry(cfg_id, (uint64_t)manager_id)) {
    return false;
  }
  rr_set_opc(2, cfg_id);

  for (int round = 0; round < 2; round++) {
    fill_pattern(w->src, (size_t)w->bytes, w->cid, dma_idx, round);
    memset(w->dst, 0, (size_t)w->bytes);
    w->completion_flag = 0;

    dma_set_dst((uint64_t)w->dst, &w->completion_flag);
    dma_set_src((uint64_t)w->src, (uint64_t)w->bytes);
    dma_fence_wait(&w->completion_flag);
    rr_fence(cfg_id);

    if (memcmp(w->src, w->dst, (size_t)w->bytes) != 0) {
      rr_release(cfg_id);
      return false;
    }
  }

  rr_release(cfg_id);
  return true;
}

static void *dma_worker_main(void *arg) {
  dma_worker_t *w = (dma_worker_t *)arg;
  const int bind_rc = bind_thread_to_cpu(w->cid);
  if (bind_rc != 0) {
    w->fail = selected_pairs_for_cpu(w->matrix_mode, w->cid, w->num_dma);
    printf("[dma] bind failed cpu=%d rc=%d\n", w->cid, bind_rc);
    return NULL;
  }

  const uint32_t cfg_id = (uint32_t)(w->cid % RR_MAX_CFGS);
  for (int did = 0; did < w->num_dma; did++) {
    if (!should_run_pair(w->matrix_mode, w->cid, did, w->num_dma)) {
      continue;
    }
    const int manager_id = w->dma_base_id + did;
    const bool ok = run_one_dma_case(w, did, cfg_id);
    if (ok) {
      w->pass++;
      printf("[dma] cpu=%d mgr=%d PASS\n", w->cid, manager_id);
    } else {
      w->fail++;
      printf("[dma] cpu=%d mgr=%d FAIL\n", w->cid, manager_id);
    }
  }

  rr_release_all(RR_MAX_CFGS);
  return NULL;
}

int main(int argc, char **argv) {
  int num_cores = 4;
  int num_gemmini = 4;
  int num_dma = 4;
  int dma_base_id = -1;
  int bytes = 64 * 1024;
  matrix_mode_t matrix_mode = MATRIX_FULL;

  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "--num-cores") == 0 && i + 1 < argc) {
      if (!parse_int_arg("--num-cores", argv[++i], &num_cores)) return 1;
    } else if (strcmp(argv[i], "--num-gemmini") == 0 && i + 1 < argc) {
      if (!parse_int_arg("--num-gemmini", argv[++i], &num_gemmini)) return 1;
    } else if (strcmp(argv[i], "--num-dma") == 0 && i + 1 < argc) {
      if (!parse_int_arg("--num-dma", argv[++i], &num_dma)) return 1;
    } else if (strcmp(argv[i], "--dma-base-id") == 0 && i + 1 < argc) {
      if (!parse_int_arg("--dma-base-id", argv[++i], &dma_base_id)) return 1;
    } else if (strcmp(argv[i], "--bytes") == 0 && i + 1 < argc) {
      if (!parse_int_arg("--bytes", argv[++i], &bytes)) return 1;
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

  if (dma_base_id < 0) {
    dma_base_id = num_gemmini;
  }
  if (num_cores <= 0 || num_dma <= 0 || bytes <= 0) {
    printf("num-cores/num-dma/bytes must be > 0\n");
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
  dma_worker_t workers[MAX_TEST_THREADS];
  memset(workers, 0, sizeof(workers));

  for (int cid = 0; cid < num_cores; cid++) {
    workers[cid].cid = cid;
    workers[cid].num_dma = num_dma;
    workers[cid].dma_base_id = dma_base_id;
    workers[cid].bytes = bytes;
    workers[cid].matrix_mode = matrix_mode;

    if (posix_memalign((void **)&workers[cid].src, 64, (size_t)bytes) != 0) {
      printf("posix_memalign failed for src(cpu=%d)\n", cid);
      return 1;
    }
    if (posix_memalign((void **)&workers[cid].dst, 64, (size_t)bytes) != 0) {
      printf("posix_memalign failed for dst(cpu=%d)\n", cid);
      return 1;
    }

    if (pthread_create(&threads[cid], NULL, dma_worker_main, &workers[cid]) != 0) {
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
    printf("CORE_RESULT cid=%d dma_pass=%d dma_fail=%d\n", cid, workers[cid].pass, workers[cid].fail);
    free(workers[cid].src);
    free(workers[cid].dst);
  }

  const int expected = selected_pairs(matrix_mode, num_cores, num_dma);
  printf("DMA_MATRIX_RESULT mode=%s pass=%d fail=%d expected=%d bytes=%d\n",
    matrix_mode_name(matrix_mode), pass, fail, expected, bytes);
  if (fail == 0 && pass == expected) {
    printf("DMA_ALL_PASS\n");
    return 0;
  }

  printf("DMA_ALL_FAIL\n");
  return 1;
}
