#define _GNU_SOURCE

#include <errno.h>
#include <fcntl.h>
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
#define DMA_WAIT_SPINS 20000000UL

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
  int pass;
  int fail;
} dma_worker_t;

typedef struct {
  uint8_t *src;
  uint8_t *dst;
  volatile uint32_t *done;
  uint64_t src_pa;
  uint64_t dst_pa;
  uint64_t done_pa;
  size_t alloc_bytes;
} dma_buffers_t;

static int g_page_size = 0;
static int g_pagemap_fd = -1;

static inline void dma_set_dst(uint64_t addr, uint64_t completion_pa) {
  ROCC_INSTRUCTION_0_R_R(DMA_XCUSTOM, addr, completion_pa, 2);
}

static inline void dma_set_src(uint64_t addr, uint64_t len) {
  ROCC_INSTRUCTION_0_R_R(DMA_XCUSTOM, addr, len, 1);
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

static bool init_pagemap(void) {
  if (g_pagemap_fd >= 0) {
    return true;
  }
  g_page_size = getpagesize();
  if (g_page_size <= 0) {
    printf("getpagesize failed\n");
    return false;
  }
  g_pagemap_fd = open("/proc/self/pagemap", O_RDONLY);
  if (g_pagemap_fd < 0) {
    printf("open /proc/self/pagemap failed: %s\n", strerror(errno));
    return false;
  }
  return true;
}

static bool virt_to_phys(const void *vaddr, uint64_t *paddr) {
  const uint64_t va = (uint64_t)(uintptr_t)vaddr;
  const uint64_t vpn = va / (uint64_t)g_page_size;
  const off_t offset = (off_t)(vpn * sizeof(uint64_t));
  uint64_t entry = 0;
  const ssize_t n = pread(g_pagemap_fd, &entry, sizeof(entry), offset);
  if (n != (ssize_t)sizeof(entry)) {
    return false;
  }

  const uint64_t PRESENT = 1ULL << 63;
  const uint64_t PFN_MASK = ((1ULL << 55) - 1ULL);
  if ((entry & PRESENT) == 0) {
    return false;
  }

  const uint64_t pfn = entry & PFN_MASK;
  if (pfn == 0) {
    return false;
  }

  *paddr = pfn * (uint64_t)g_page_size + (va % (uint64_t)g_page_size);
  return true;
}

static bool alloc_dma_buffers(int bytes, dma_buffers_t *b) {
  size_t rounded_bytes;
  memset(b, 0, sizeof(*b));

  if (bytes <= 0) {
    printf("bytes=%d must be > 0\n", bytes);
    return false;
  }

  rounded_bytes = ((size_t)bytes + (size_t)g_page_size - 1U) / (size_t)g_page_size;
  rounded_bytes *= (size_t)g_page_size;
  b->alloc_bytes = rounded_bytes;
  b->src = (uint8_t *)mmap(NULL, b->alloc_bytes, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  b->dst = (uint8_t *)mmap(NULL, b->alloc_bytes, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  b->done = (volatile uint32_t *)mmap(NULL, b->alloc_bytes, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

  if (b->src == MAP_FAILED || b->dst == MAP_FAILED || b->done == MAP_FAILED) {
    printf("mmap failed: %s\n", strerror(errno));
    return false;
  }

  (void)mlock((const void *)b->src, b->alloc_bytes);
  (void)mlock((const void *)b->dst, b->alloc_bytes);
  (void)mlock((const void *)b->done, b->alloc_bytes);

  memset(b->src, 0, b->alloc_bytes);
  memset(b->dst, 0, b->alloc_bytes);
  *((volatile uint32_t *)b->done) = 0;
  asm volatile("fence rw, rw");

  if (!virt_to_phys((const void *)b->src, &b->src_pa) ||
      !virt_to_phys((const void *)b->dst, &b->dst_pa) ||
      !virt_to_phys((const void *)b->done, &b->done_pa)) {
    printf("virt_to_phys failed (need readable PFN in /proc/self/pagemap)\n");
    return false;
  }

  return true;
}

static void free_dma_buffers(dma_buffers_t *b) {
  if (b->src && b->src != MAP_FAILED) {
    munmap((void *)b->src, b->alloc_bytes);
  }
  if (b->dst && b->dst != MAP_FAILED) {
    munmap((void *)b->dst, b->alloc_bytes);
  }
  if (b->done && b->done != MAP_FAILED) {
    munmap((void *)b->done, b->alloc_bytes);
  }
}

static bool dma_wait_done(volatile uint32_t *done) {
  for (unsigned long i = 0; i < DMA_WAIT_SPINS; i++) {
    asm volatile("fence r, rw");
    if (*done != 0) {
      return true;
    }
  }
  return false;
}

static bool run_one_dma_case(dma_worker_t *w, int dma_idx, uint32_t cfg_id) {
  const int manager_id = w->dma_base_id + dma_idx;

  if (!rr_acquire_cfg_with_retry(cfg_id, (uint64_t)manager_id)) {
    return false;
  }
  rr_set_opc(2, cfg_id);

  dma_buffers_t buf;
  if (!alloc_dma_buffers(w->bytes, &buf)) {
    rr_release(cfg_id);
    return false;
  }

  bool ok = true;
  for (int round = 0; round < 2; round++) {
    size_t left = (size_t)w->bytes;
    size_t offset = 0;
    fill_pattern(buf.src, (size_t)w->bytes, w->cid, dma_idx, round);
    memset(buf.dst, 0, (size_t)w->bytes);
    while (left > 0) {
      uint64_t src_pa = 0;
      uint64_t dst_pa = 0;
      size_t src_off_in_page = ((size_t)(uintptr_t)(buf.src + offset)) % (size_t)g_page_size;
      size_t dst_off_in_page = ((size_t)(uintptr_t)(buf.dst + offset)) % (size_t)g_page_size;
      size_t src_room = (size_t)g_page_size - src_off_in_page;
      size_t dst_room = (size_t)g_page_size - dst_off_in_page;
      size_t chunk = left;
      if (chunk > src_room) chunk = src_room;
      if (chunk > dst_room) chunk = dst_room;

      if (!virt_to_phys((const void *)(buf.src + offset), &src_pa) ||
          !virt_to_phys((const void *)(buf.dst + offset), &dst_pa)) {
        printf("[dma] cpu=%d mgr=%d virt_to_phys failed offset=%lu\n",
               w->cid, manager_id, (unsigned long)offset);
        ok = false;
        break;
      }

      *buf.done = 0;
      asm volatile("fence rw, rw");
      dma_set_dst(dst_pa, buf.done_pa);
      dma_set_src(src_pa, (uint64_t)chunk);

      if (!dma_wait_done(buf.done)) {
        printf("[dma] cpu=%d mgr=%d timeout waiting done flag src_pa=0x%lx dst_pa=0x%lx done_pa=0x%lx chunk=%lu offset=%lu\n",
               w->cid, manager_id, (unsigned long)src_pa, (unsigned long)dst_pa,
               (unsigned long)buf.done_pa, (unsigned long)chunk, (unsigned long)offset);
        ok = false;
        break;
      }

      rr_fence(cfg_id);
      offset += chunk;
      left -= chunk;
    }

    if (!ok) break;

    if (memcmp(buf.src, buf.dst, (size_t)w->bytes) != 0) {
      printf("[dma] cpu=%d mgr=%d data mismatch\n", w->cid, manager_id);
      ok = false;
      break;
    }
  }

  free_dma_buffers(&buf);
  rr_release(cfg_id);
  return ok;
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
  int num_cores = 1;
  int num_gemmini = 2;
  int num_dma = 1;
  int dma_base_id = -1;
  int bytes = 1024;
  matrix_mode_t matrix_mode = MATRIX_SINGLE;

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

  if (!init_pagemap()) {
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
