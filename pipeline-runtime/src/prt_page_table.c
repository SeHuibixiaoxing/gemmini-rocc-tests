#if defined(__linux__) && !defined(_GNU_SOURCE)
#define _GNU_SOURCE
#endif

#include "prt_page_table.h"

#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if defined(__linux__)
#include <errno.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>
#endif

#include "prt_runtime.h"
#include "prt_progress.h"

#define PRT_SPM_FAULT_OUT_OF_RANGE 1U
#define PRT_SPM_FAULT_INVALID_PTE 2U
#define PRT_SPM_PTE_VALID_MASK 1ULL
#define PRT_SPM_XLATE_HW_MAX_PTES UINT16_MAX

typedef struct {
  size_t offset;
  size_t bytes;
} prt_spm_pt_slice_t;

struct prt_spm_pt_chunk_s {
  void *base;
  size_t bytes;
  uint64_t base_pa;
  prt_spm_pt_slice_t *free_slices;
  uint32_t free_count;
  uint32_t free_cap;
};

size_t prt_host_page_size_bytes(void) {
#if defined(__linux__)
  long page_sz = sysconf(_SC_PAGESIZE);
  if (page_sz > 0) return (size_t)page_sz;
#endif
  return 4096U;
}

static size_t align_up_size(size_t value, size_t align) {
  if (align == 0) return value;
  return ((value + align - 1U) / align) * align;
}

void prt_spm_xlate_ctx_publish(prt_spm_xlate_ctx_t *ctx) {
  if (!ctx || !ctx->pte || ctx->pte_count == 0U) return;
  __sync_synchronize();
#if defined(__riscv)
  asm volatile("fence rw, rw" ::: "memory");
#endif
}

#if defined(__linux__) && defined(__riscv)
static size_t linux_huge_page_size_bytes(void) {
  static size_t cached = 0;
  FILE *fp;
  char line[128];

  if (cached != 0) return cached;

  fp = fopen("/proc/meminfo", "r");
  if (!fp) {
    cached = 2U * 1024U * 1024U;
    return cached;
  }

  while (fgets(line, sizeof(line), fp) != NULL) {
    size_t kb = 0;
    if (sscanf(line, "Hugepagesize: %zu kB", &kb) == 1 && kb != 0) {
      cached = kb * 1024U;
      break;
    }
  }

  fclose(fp);
  if (cached == 0) cached = 2U * 1024U * 1024U;
  return cached;
}
#endif

#if defined(__linux__) && defined(__riscv)
static int linux_pagemap_fd(void) {
  static int fd = -2;
  if (fd != -2) return fd;
  fd = open("/proc/self/pagemap", O_RDONLY);
  return fd;
}

int prt_host_virt_to_phys(const void *vaddr, uint64_t *paddr) {
  const uint64_t va = (uint64_t)(uintptr_t)vaddr;
  const size_t page_sz = prt_host_page_size_bytes();
  const uint64_t vpn = va / (uint64_t)page_sz;
  const off_t offset = (off_t)(vpn * sizeof(uint64_t));
  uint64_t entry = 0;
  ssize_t n;
  const uint64_t present = 1ULL << 63;
  const uint64_t pfn_mask = (1ULL << 55) - 1ULL;
  uint64_t pfn;
  int fd;

  if (!paddr) return PRT_ERR_INVAL;
  fd = linux_pagemap_fd();
  if (fd < 0) return PRT_ERR_IO;

  n = pread(fd, &entry, sizeof(entry), offset);
  if (n != (ssize_t)sizeof(entry)) return PRT_ERR_IO;
  if ((entry & present) == 0) return PRT_ERR_NOT_READY;

  pfn = entry & pfn_mask;
  if (pfn == 0) return PRT_ERR_NOT_READY;

  *paddr = pfn * (uint64_t)page_sz + (va % (uint64_t)page_sz);
  return PRT_OK;
}

static int probe_phys_contig_range(void *base, size_t bytes, size_t probe_page_bytes,
                                   uint64_t *base_pa, int *last_rc) {
  uint64_t first_pa = 0;
  int rc = PRT_OK;

  if (!base || probe_page_bytes == 0 || !base_pa) return PRT_ERR_INVAL;

  memset(base, 0, bytes);
  for (size_t off = 0; off < bytes; off += probe_page_bytes) {
    uint64_t pa = 0;
    volatile uint8_t *ptr = (volatile uint8_t *)base + off;
    *ptr = 0;
    rc = prt_host_virt_to_phys((const void *)ptr, &pa);
    if (rc != PRT_OK) {
      if (last_rc) *last_rc = rc;
      return rc;
    }
    if (off == 0) {
      first_pa = pa;
    } else if (pa != first_pa + (uint64_t)off) {
      if (last_rc) *last_rc = PRT_ERR_NOT_READY;
      return PRT_ERR_NOT_READY;
    }
  }

  *base_pa = first_pa;
  if (last_rc) *last_rc = PRT_OK;
  return PRT_OK;
}

static int try_alloc_contig_hugetlb_anon(size_t req_bytes, size_t probe_page_bytes,
                                         void **out_base, size_t *out_alloc_bytes,
                                         uint64_t *out_base_pa) {
  const size_t huge_page_bytes = linux_huge_page_size_bytes();
  const size_t alloc_bytes = align_up_size(req_bytes, huge_page_bytes);
  void *base;
  uint64_t base_pa = 0;
  int rc = PRT_ERR_NOT_READY;

  if (!out_base || !out_alloc_bytes || !out_base_pa) return PRT_ERR_INVAL;

  base = mmap(NULL, alloc_bytes, PROT_READ | PROT_WRITE,
              MAP_PRIVATE | MAP_ANONYMOUS | MAP_HUGETLB
#ifdef MAP_POPULATE
                  | MAP_POPULATE
#endif
              ,
              -1, 0);
  if (base == MAP_FAILED) {
    int err = errno;
    PRT_PROGRESS_LOG("spm-pt hugetlb anon failed bytes=%llu errno=%d",
                     (unsigned long long)alloc_bytes, err);
    return err == ENOMEM ? PRT_ERR_NOMEM : PRT_ERR_NOT_READY;
  }

  rc = probe_phys_contig_range(base, req_bytes, probe_page_bytes, &base_pa, NULL);
  if (rc != PRT_OK) {
    PRT_PROGRESS_LOG("spm-pt hugetlb anon probe failed req_bytes=%llu alloc_bytes=%llu rc=%d",
                     (unsigned long long)req_bytes,
                     (unsigned long long)alloc_bytes,
                     rc);
    munmap(base, alloc_bytes);
    return rc;
  }

  *out_base = base;
  *out_alloc_bytes = alloc_bytes;
  *out_base_pa = base_pa;
  PRT_PROGRESS_LOG("spm-pt hugetlb anon ok req_bytes=%llu alloc_bytes=%llu pa=0x%llx",
                   (unsigned long long)req_bytes,
                   (unsigned long long)alloc_bytes,
                   (unsigned long long)base_pa);
  return PRT_OK;
}

static int try_alloc_contig_hugetlbfs(size_t req_bytes, size_t probe_page_bytes,
                                      void **out_base, size_t *out_alloc_bytes,
                                      uint64_t *out_base_pa) {
  static unsigned int seq = 0;
  const char *dir = "/dev/hugepages";
  const size_t huge_page_bytes = linux_huge_page_size_bytes();
  const size_t alloc_bytes = align_up_size(req_bytes, huge_page_bytes);
  char path[160];
  int fd;
  void *base;
  uint64_t base_pa = 0;
  int rc = PRT_ERR_NOT_READY;

  if (!out_base || !out_alloc_bytes || !out_base_pa) return PRT_ERR_INVAL;

  snprintf(path, sizeof(path), "%s/prt-spm-pt-%ld-%u", dir, (long)getpid(), seq++);
  fd = open(path, O_CREAT | O_EXCL | O_RDWR, 0600);
  if (fd < 0) {
    int err = errno;
    PRT_PROGRESS_LOG("spm-pt hugetlbfs open failed path=%s errno=%d", path, err);
    return PRT_ERR_NOT_READY;
  }

  if (ftruncate(fd, (off_t)alloc_bytes) != 0) {
    int err = errno;
    PRT_PROGRESS_LOG("spm-pt hugetlbfs ftruncate failed path=%s bytes=%llu errno=%d",
                     path,
                     (unsigned long long)alloc_bytes,
                     err);
    close(fd);
    unlink(path);
    return err == ENOMEM ? PRT_ERR_NOMEM : PRT_ERR_NOT_READY;
  }

  base = mmap(NULL, alloc_bytes, PROT_READ | PROT_WRITE,
              MAP_SHARED
#ifdef MAP_POPULATE
                  | MAP_POPULATE
#endif
              ,
              fd, 0);
  close(fd);
  unlink(path);
  if (base == MAP_FAILED) {
    int err = errno;
    PRT_PROGRESS_LOG("spm-pt hugetlbfs mmap failed bytes=%llu errno=%d",
                     (unsigned long long)alloc_bytes,
                     err);
    return err == ENOMEM ? PRT_ERR_NOMEM : PRT_ERR_NOT_READY;
  }

  rc = probe_phys_contig_range(base, req_bytes, probe_page_bytes, &base_pa, NULL);
  if (rc != PRT_OK) {
    PRT_PROGRESS_LOG("spm-pt hugetlbfs probe failed req_bytes=%llu alloc_bytes=%llu rc=%d",
                     (unsigned long long)req_bytes,
                     (unsigned long long)alloc_bytes,
                     rc);
    munmap(base, alloc_bytes);
    return rc;
  }

  *out_base = base;
  *out_alloc_bytes = alloc_bytes;
  *out_base_pa = base_pa;
  PRT_PROGRESS_LOG("spm-pt hugetlbfs ok req_bytes=%llu alloc_bytes=%llu pa=0x%llx",
                   (unsigned long long)req_bytes,
                   (unsigned long long)alloc_bytes,
                   (unsigned long long)base_pa);
  return PRT_OK;
}

static int try_alloc_contig_anon(size_t req_bytes, size_t probe_page_bytes,
                                 void **out_base, size_t *out_alloc_bytes,
                                 uint64_t *out_base_pa) {
  const size_t alloc_bytes = align_up_size(req_bytes, probe_page_bytes);
  int last_rc = PRT_ERR_NOT_READY;

  if (!out_base || !out_alloc_bytes || !out_base_pa) return PRT_ERR_INVAL;

  for (int attempt = 0; attempt < 32; ++attempt) {
    void *base = mmap(NULL, alloc_bytes, PROT_READ | PROT_WRITE,
                      MAP_PRIVATE | MAP_ANONYMOUS
#ifdef MAP_POPULATE
                      | MAP_POPULATE
#endif
                      ,
                      -1, 0);
    uint64_t base_pa = 0;
    if (base == MAP_FAILED) continue;
    last_rc = probe_phys_contig_range(base, alloc_bytes, probe_page_bytes, &base_pa, &last_rc);
    if (last_rc == PRT_OK) {
      *out_base = base;
      *out_alloc_bytes = alloc_bytes;
      *out_base_pa = base_pa;
      return PRT_OK;
    }
    PRT_PROGRESS_LOG("spm-pt alloc retry attempt=%d bytes=%llu last_rc=%d",
                     attempt + 1,
                     (unsigned long long)alloc_bytes,
                     last_rc);
    munmap(base, alloc_bytes);
  }

  return last_rc;
}

#else

int prt_host_virt_to_phys(const void *vaddr, uint64_t *paddr) {
  (void)vaddr;
  (void)paddr;
  return PRT_ERR_NOT_IMPL;
}

#endif

static size_t runtime_pt_chunk_bytes(const prt_runtime_t *rt) {
  size_t base_bytes = 0U;
  size_t host_page_bytes = prt_host_page_size_bytes();
  size_t min_bytes = host_page_bytes;

  if (rt) {
    uint64_t total_pages = (uint64_t)rt->cfg.num_cores * (uint64_t)rt->cfg.pages_per_acc;
    if (total_pages > (uint64_t)PRT_SPM_XLATE_HW_MAX_PTES) {
      total_pages = (uint64_t)PRT_SPM_XLATE_HW_MAX_PTES;
    }
    if (total_pages != 0U) {
      min_bytes = align_up_size((size_t)total_pages * sizeof(uint64_t), host_page_bytes);
    }
    if (rt->spm_pt_hugepage_bytes != 0U) base_bytes = rt->spm_pt_hugepage_bytes;
  }

  if (base_bytes == 0U) {
#if defined(__linux__) && defined(__riscv)
    base_bytes = linux_huge_page_size_bytes();
#else
    base_bytes = host_page_bytes;
#endif
  }

  return base_bytes > min_bytes ? base_bytes : min_bytes;
}

static int alloc_contig_pt_storage(prt_runtime_t *rt, size_t req_bytes,
                                   void **out_base, size_t *out_alloc_bytes,
                                   uint64_t *out_base_pa) {
  if (!rt || !out_base || !out_alloc_bytes || !out_base_pa || req_bytes == 0U) {
    return PRT_ERR_INVAL;
  }
#if defined(__linux__) && defined(__riscv)
  {
    const size_t probe_page_bytes = prt_host_page_size_bytes();
    const int require_hugetlb = rt->cfg.spm_pt_require_hugetlb != 0U;
    int rc = try_alloc_contig_hugetlb_anon(req_bytes, probe_page_bytes,
                                           out_base, out_alloc_bytes, out_base_pa);
    if (rc == PRT_OK) return PRT_OK;

    rc = try_alloc_contig_hugetlbfs(req_bytes, probe_page_bytes,
                                    out_base, out_alloc_bytes, out_base_pa);
    if (rc == PRT_OK) return PRT_OK;
    if (require_hugetlb) return rc;

    return try_alloc_contig_anon(req_bytes, probe_page_bytes,
                                 out_base, out_alloc_bytes, out_base_pa);
  }
#else
  {
    void *base = calloc(1U, req_bytes);
    if (!base) return PRT_ERR_NOMEM;
    *out_base = base;
    *out_alloc_bytes = req_bytes;
    *out_base_pa = (uint64_t)(uintptr_t)base;
    return PRT_OK;
  }
#endif
}

static int ensure_pt_chunk_slice_capacity(prt_spm_pt_chunk_t *chunk, uint32_t need) {
  if (!chunk) return PRT_ERR_INVAL;
  if (chunk->free_cap >= need) return PRT_OK;
  uint32_t new_cap = chunk->free_cap ? chunk->free_cap : 4U;
  while (new_cap < need) new_cap <<= 1U;
  prt_spm_pt_slice_t *tmp =
    (prt_spm_pt_slice_t *)realloc(chunk->free_slices, sizeof(prt_spm_pt_slice_t) * new_cap);
  if (!tmp) return PRT_ERR_NOMEM;
  chunk->free_slices = tmp;
  chunk->free_cap = new_cap;
  return PRT_OK;
}

static int ensure_pt_chunk_capacity(prt_runtime_t *rt, uint32_t need) {
  if (!rt) return PRT_ERR_INVAL;
  if (rt->spm_pt_chunk_cap >= need) return PRT_OK;
  uint32_t new_cap = rt->spm_pt_chunk_cap ? rt->spm_pt_chunk_cap : 2U;
  while (new_cap < need) new_cap <<= 1U;
  prt_spm_pt_chunk_t *tmp =
    (prt_spm_pt_chunk_t *)realloc(rt->spm_pt_chunks, sizeof(prt_spm_pt_chunk_t) * new_cap);
  if (!tmp) return PRT_ERR_NOMEM;
  memset(tmp + rt->spm_pt_chunk_cap, 0, sizeof(prt_spm_pt_chunk_t) * (new_cap - rt->spm_pt_chunk_cap));
  rt->spm_pt_chunks = tmp;
  rt->spm_pt_chunk_cap = new_cap;
  return PRT_OK;
}

static int pt_pool_add_chunk(prt_runtime_t *rt) {
  void *base = NULL;
  size_t alloc_bytes = 0;
  uint64_t base_pa = 0;
  size_t chunk_bytes;
  prt_spm_pt_chunk_t *chunk;
  int rc;

  if (!rt) return PRT_ERR_INVAL;
  if (rt->cfg.spm_pt_pool_max_hugepages > 0U &&
      rt->spm_pt_chunk_count >= rt->cfg.spm_pt_pool_max_hugepages) {
    return PRT_ERR_NOMEM;
  }

  chunk_bytes = runtime_pt_chunk_bytes(rt);
  rc = alloc_contig_pt_storage(rt, chunk_bytes, &base, &alloc_bytes, &base_pa);
  if (rc != PRT_OK) return rc;

  rc = ensure_pt_chunk_capacity(rt, rt->spm_pt_chunk_count + 1U);
  if (rc != PRT_OK) {
#if defined(__linux__) && defined(__riscv)
    munmap(base, alloc_bytes);
#else
    free(base);
#endif
    return rc;
  }

  chunk = &rt->spm_pt_chunks[rt->spm_pt_chunk_count];
  memset(chunk, 0, sizeof(*chunk));
  chunk->base = base;
  chunk->bytes = alloc_bytes;
  chunk->base_pa = base_pa;
  rc = ensure_pt_chunk_slice_capacity(chunk, 1U);
  if (rc != PRT_OK) {
#if defined(__linux__) && defined(__riscv)
    munmap(base, alloc_bytes);
#else
    free(base);
#endif
    memset(chunk, 0, sizeof(*chunk));
    return rc;
  }
  chunk->free_count = 1U;
  chunk->free_slices[0].offset = 0U;
  chunk->free_slices[0].bytes = alloc_bytes;
  rt->spm_pt_chunk_count += 1U;
  PRT_PROGRESS_LOG("spm-pt pool chunk-added idx=%u bytes=%llu pa=0x%llx",
                   rt->spm_pt_chunk_count - 1U,
                   (unsigned long long)alloc_bytes,
                   (unsigned long long)base_pa);
  return PRT_OK;
}

static int pt_chunk_alloc_slice(prt_spm_pt_chunk_t *chunk, size_t bytes, size_t *out_offset) {
  if (!chunk || !out_offset || bytes == 0U) return PRT_ERR_INVAL;
  for (uint32_t i = 0; i < chunk->free_count; ++i) {
    prt_spm_pt_slice_t *slice = &chunk->free_slices[i];
    if (slice->bytes < bytes) continue;
    *out_offset = slice->offset;
    slice->offset += bytes;
    slice->bytes -= bytes;
    if (slice->bytes == 0U) {
      if (i + 1U < chunk->free_count) {
        memmove(&chunk->free_slices[i], &chunk->free_slices[i + 1U],
                sizeof(prt_spm_pt_slice_t) * (chunk->free_count - i - 1U));
      }
      chunk->free_count -= 1U;
    }
    return PRT_OK;
  }
  return PRT_ERR_NOMEM;
}

static int pt_pool_alloc_slice(prt_runtime_t *rt, size_t bytes,
                               prt_spm_pt_chunk_t **out_chunk,
                               size_t *out_offset) {
  int rc;
  if (!rt || !out_chunk || !out_offset || bytes == 0U) return PRT_ERR_INVAL;
  if (bytes > runtime_pt_chunk_bytes(rt)) return PRT_ERR_NOT_IMPL;

  for (uint32_t i = 0; i < rt->spm_pt_chunk_count; ++i) {
    rc = pt_chunk_alloc_slice(&rt->spm_pt_chunks[i], bytes, out_offset);
    if (rc == PRT_OK) {
      *out_chunk = &rt->spm_pt_chunks[i];
      return PRT_OK;
    }
  }

  rc = pt_pool_add_chunk(rt);
  if (rc != PRT_OK) return rc;
  *out_chunk = &rt->spm_pt_chunks[rt->spm_pt_chunk_count - 1U];
  return pt_chunk_alloc_slice(*out_chunk, bytes, out_offset);
}

static int pt_pool_release_slice(prt_spm_pt_chunk_t *chunk, size_t offset, size_t bytes) {
  uint32_t idx = 0;
  if (!chunk || bytes == 0U) return PRT_ERR_INVAL;
  if (offset + bytes > chunk->bytes) return PRT_ERR_INVAL;
  if (ensure_pt_chunk_slice_capacity(chunk, chunk->free_count + 1U) != PRT_OK) return PRT_ERR_NOMEM;
  while (idx < chunk->free_count && chunk->free_slices[idx].offset < offset) idx += 1U;
  if (idx < chunk->free_count) {
    memmove(&chunk->free_slices[idx + 1U], &chunk->free_slices[idx],
            sizeof(prt_spm_pt_slice_t) * (chunk->free_count - idx));
  }
  chunk->free_slices[idx].offset = offset;
  chunk->free_slices[idx].bytes = bytes;
  chunk->free_count += 1U;

  for (uint32_t i = 1U; i < chunk->free_count; ) {
    prt_spm_pt_slice_t *prev = &chunk->free_slices[i - 1U];
    prt_spm_pt_slice_t *cur = &chunk->free_slices[i];
    size_t prev_end = prev->offset + prev->bytes;
    if (prev_end < cur->offset) {
      i += 1U;
      continue;
    }
    if (prev_end < cur->offset + cur->bytes) {
      prev->bytes = (cur->offset + cur->bytes) - prev->offset;
    }
    if (i + 1U < chunk->free_count) {
      memmove(&chunk->free_slices[i], &chunk->free_slices[i + 1U],
              sizeof(prt_spm_pt_slice_t) * (chunk->free_count - i - 1U));
    }
    chunk->free_count -= 1U;
  }
  return PRT_OK;
}

static uint32_t rt_page_bytes(const prt_runtime_t *rt) {
  if (!rt || rt->cfg.page_size_bytes == 0) return PRT_PAGE_SIZE_BYTES;
  return rt->cfg.page_size_bytes;
}

static prt_spm_xlate_ctx_t *current_spm_xlate_ctx(prt_runtime_t *rt) {
  prt_schedule_action_t *action = prt_runtime_current_action(rt);
  if (!action) return NULL;
  return &action->spm_xlate;
}

static const prt_spm_xlate_ctx_t *current_spm_xlate_ctx_const(const prt_runtime_t *rt) {
  const prt_schedule_action_t *action = prt_runtime_current_action(rt);
  if (!action) return NULL;
  return &action->spm_xlate;
}

static uint64_t rt_spm_base(const prt_runtime_t *rt) {
  const prt_schedule_action_t *action = prt_runtime_current_action(rt);
  if (!action) return 0;
  return action->alias_base_va;
}

static int ensure_free_vpage_capacity(prt_spm_xlate_ctx_t *ctx, uint32_t need) {
  if (!ctx) return PRT_ERR_INVAL;
  if (ctx->free_vpage_cap >= need) return PRT_OK;
  uint32_t new_cap = ctx->free_vpage_cap ? ctx->free_vpage_cap : 8U;
  while (new_cap < need) new_cap <<= 1U;
  prt_vmap_desc_t *tmp =
    (prt_vmap_desc_t *)realloc(ctx->free_vpages, sizeof(prt_vmap_desc_t) * new_cap);
  if (!tmp) return PRT_ERR_NOMEM;
  ctx->free_vpages = tmp;
  ctx->free_vpage_cap = new_cap;
  return PRT_OK;
}

static int reserve_vpages_locked(prt_spm_xlate_ctx_t *ctx, uint32_t page_count,
                                 uint32_t *out_vpage_start) {
  if (!ctx || !out_vpage_start || page_count == 0) return PRT_ERR_INVAL;
  for (uint32_t i = 0; i < ctx->free_vpage_count; ++i) {
    prt_vmap_desc_t *desc = &ctx->free_vpages[i];
    if (desc->count < page_count) continue;
    *out_vpage_start = desc->start;
    desc->start += page_count;
    desc->count -= page_count;
    if (desc->count == 0U) {
      if (i + 1U < ctx->free_vpage_count) {
        memmove(&ctx->free_vpages[i], &ctx->free_vpages[i + 1U],
                sizeof(prt_vmap_desc_t) * (ctx->free_vpage_count - i - 1U));
      }
      ctx->free_vpage_count -= 1U;
    }
    if (ctx->next_vpage < *out_vpage_start + page_count) {
      ctx->next_vpage = *out_vpage_start + page_count;
    }
    return PRT_OK;
  }
  return PRT_ERR_NOMEM;
}

static int release_vpages_locked(prt_spm_xlate_ctx_t *ctx, uint32_t vpage_start, uint32_t page_count) {
  uint64_t start64;
  uint64_t end64;
  uint32_t idx = 0;
  if (!ctx || page_count == 0) return PRT_ERR_INVAL;
  start64 = vpage_start;
  end64 = start64 + page_count;
  if (end64 > ctx->pte_count) return PRT_ERR_INVAL;
  if (ensure_free_vpage_capacity(ctx, ctx->free_vpage_count + 1U) != PRT_OK) return PRT_ERR_NOMEM;
  while (idx < ctx->free_vpage_count && ctx->free_vpages[idx].start < vpage_start) idx += 1U;
  if (idx < ctx->free_vpage_count) {
    memmove(&ctx->free_vpages[idx + 1U], &ctx->free_vpages[idx],
            sizeof(prt_vmap_desc_t) * (ctx->free_vpage_count - idx));
  }
  ctx->free_vpages[idx].start = vpage_start;
  ctx->free_vpages[idx].count = page_count;
  ctx->free_vpage_count += 1U;

  for (uint32_t i = 1; i < ctx->free_vpage_count; ) {
    prt_vmap_desc_t *prev = &ctx->free_vpages[i - 1U];
    prt_vmap_desc_t *cur = &ctx->free_vpages[i];
    uint64_t prev_end = (uint64_t)prev->start + (uint64_t)prev->count;
    if (prev_end < cur->start) {
      i += 1U;
      continue;
    }
    if (prev_end < (uint64_t)cur->start + (uint64_t)cur->count) {
      prev->count = (uint32_t)(((uint64_t)cur->start + (uint64_t)cur->count) - (uint64_t)prev->start);
    }
    if (i + 1U < ctx->free_vpage_count) {
      memmove(&ctx->free_vpages[i], &ctx->free_vpages[i + 1U],
              sizeof(prt_vmap_desc_t) * (ctx->free_vpage_count - i - 1U));
    }
    ctx->free_vpage_count -= 1U;
  }
  return PRT_OK;
}

static uint64_t pack_spm_pte(const prt_runtime_t *rt, uint64_t paddr) {
  uint32_t page_shift = rt && rt->cfg.spm_page_shift ? rt->cfg.spm_page_shift : 10U;
  return ((paddr >> page_shift) << 1) | PRT_SPM_PTE_VALID_MASK;
}

static int spm_pte_valid(uint64_t pte) {
  return (pte & PRT_SPM_PTE_VALID_MASK) != 0ULL;
}

static uint64_t spm_pte_paddr(const prt_runtime_t *rt, uint64_t pte) {
  uint32_t page_shift = rt && rt->cfg.spm_page_shift ? rt->cfg.spm_page_shift : 10U;
  return (pte >> 1) << page_shift;
}

static uint32_t hilbert_order_idx(uint32_t i, uint32_t num_cores) {
  if (num_cores == 4) {
    static const uint32_t order[4] = {0, 2, 3, 1};
    return order[i & 3U];
  }
  return i;
}

static int page_list_reserve(prt_page_list_t *l, uint32_t n) {
  if (l->cap >= n) return PRT_OK;
  uint32_t new_cap = l->cap ? l->cap : 16;
  while (new_cap < n) new_cap <<= 1;
  prt_page_t *p = (prt_page_t *)realloc(l->data, sizeof(prt_page_t) * new_cap);
  if (!p) return PRT_ERR_NOMEM;
  l->data = p;
  l->cap = new_cap;
  return PRT_OK;
}

static int ensure_tensor_alloc_capacity(prt_runtime_t *rt) {
  if (rt->tensor_alloc_count < rt->tensor_alloc_cap) return PRT_OK;
  uint32_t new_cap = rt->tensor_alloc_cap ? (rt->tensor_alloc_cap << 1) : 64;
  prt_tensor_alloc_t *p = (prt_tensor_alloc_t *)realloc(rt->tensor_allocs, sizeof(prt_tensor_alloc_t) * new_cap);
  if (!p) return PRT_ERR_NOMEM;
  memset(p + rt->tensor_alloc_cap, 0, sizeof(prt_tensor_alloc_t) * (new_cap - rt->tensor_alloc_cap));
  rt->tensor_allocs = p;
  rt->tensor_alloc_cap = new_cap;
  return PRT_OK;
}

static prt_tensor_alloc_t *find_tensor_alloc(prt_runtime_t *rt, uint32_t tensor_id) {
  uint32_t i;
  for (i = 0; i < rt->tensor_alloc_count; ++i) {
    if (rt->tensor_allocs[i].tensor_id == tensor_id) return &rt->tensor_allocs[i];
  }
  return NULL;
}

static int list_contains_u32(const uint32_t *arr, uint32_t n, uint32_t v) {
  if (!arr) return 0;
  for (uint32_t i = 0; i < n; ++i) {
    if (arr[i] == v) return 1;
  }
  return 0;
}

static int append_unique_u32(uint32_t *arr, uint32_t *n, uint32_t cap, uint32_t v) {
  if (!arr || !n || *n >= cap) return PRT_ERR_INVAL;
  if (list_contains_u32(arr, *n, v)) return PRT_OK;
  arr[(*n)++] = v;
  return PRT_OK;
}

typedef struct {
  uint32_t acc;
  uint32_t dist;
} core_dist_t;

static void sort_core_dist(core_dist_t *arr, uint32_t n) {
  if (!arr || n <= 1) return;
  for (uint32_t i = 0; i + 1 < n; ++i) {
    uint32_t best = i;
    for (uint32_t j = i + 1; j < n; ++j) {
      if (arr[j].dist < arr[best].dist ||
          (arr[j].dist == arr[best].dist && arr[j].acc < arr[best].acc)) {
        best = j;
      }
    }
    if (best != i) {
      core_dist_t tmp = arr[i];
      arr[i] = arr[best];
      arr[best] = tmp;
    }
  }
}

static void build_all_core_order(uint32_t num_cores, uint32_t *out, uint32_t *out_n) {
  uint32_t n = 0;
  if (!out || !out_n) return;
  for (uint32_t i = 0; i < num_cores; ++i) {
    uint32_t acc = hilbert_order_idx(i, num_cores);
    if (acc >= num_cores) continue;
    if (!list_contains_u32(out, n, acc)) out[n++] = acc;
  }
  *out_n = n;
}

static void build_fallback_order(uint32_t num_cores, const uint32_t *preferred, uint32_t preferred_n,
                                 uint32_t *out, uint32_t *out_n) {
  core_dist_t tmp[PRT_MAX_CORES];
  uint32_t n = 0;
  if (!out || !out_n) return;
  if (!preferred || preferred_n == 0) {
    build_all_core_order(num_cores, out, out_n);
    return;
  }

  for (uint32_t acc = 0; acc < num_cores && n < PRT_MAX_CORES; ++acc) {
    uint32_t dist_sum = 0;
    if (list_contains_u32(preferred, preferred_n, acc)) continue;
    for (uint32_t j = 0; j < preferred_n; ++j) {
      uint32_t p = preferred[j];
      dist_sum += (acc > p) ? (acc - p) : (p - acc);
    }
    tmp[n].acc = acc;
    tmp[n].dist = dist_sum / preferred_n;
    n += 1;
  }

  sort_core_dist(tmp, n);
  for (uint32_t i = 0; i < n; ++i) out[i] = tmp[i].acc;
  *out_n = n;
}

static void alloc_pages_from_order(prt_runtime_t *rt, prt_tensor_alloc_t *slot,
                                   uint32_t need_pages, const uint32_t *order, uint32_t order_n) {
  if (!rt || !slot || !order || order_n == 0 || need_pages == 0) return;
  // Keep a stable bank order for every local-page layer so the resulting page
  // list matches the fixed all-bank interleave contract used by MudnacSim and
  // the exported runtime mapping: bank0:lp0, bank1:lp0, ..., bank0:lp1, ...
  for (uint32_t lp = 0; lp < rt->cfg.pages_per_acc && slot->pages.size < need_pages; ++lp) {
    for (uint32_t i = 0; i < order_n && slot->pages.size < need_pages; ++i) {
      uint32_t acc = order[i];
      uint32_t idx;
      if (acc >= rt->cfg.num_cores) continue;
      idx = acc * rt->cfg.pages_per_acc + lp;
      if (rt->page_used[idx]) continue;
      rt->page_used[idx] = 1;
      slot->pages.data[slot->pages.size].acc_id = acc;
      slot->pages.data[slot->pages.size].local_page_idx = lp;
      slot->pages.data[slot->pages.size].ppn = idx;
      slot->pages.size += 1;
    }
  }
}

static int ensure_spm_tensor_map_capacity(prt_spm_xlate_ctx_t *ctx) {
  uint32_t new_cap;
  prt_spm_tensor_map_t *tmp;
  if (!ctx) return PRT_ERR_INVAL;
  if (ctx->tensor_map_count < ctx->tensor_map_cap) return PRT_OK;
  new_cap = ctx->tensor_map_cap ? (ctx->tensor_map_cap << 1) : 64U;
  tmp = (prt_spm_tensor_map_t *)realloc(ctx->tensor_maps, sizeof(prt_spm_tensor_map_t) * new_cap);
  if (!tmp) return PRT_ERR_NOMEM;
  memset(tmp + ctx->tensor_map_cap, 0, sizeof(prt_spm_tensor_map_t) * (new_cap - ctx->tensor_map_cap));
  ctx->tensor_maps = tmp;
  ctx->tensor_map_cap = new_cap;
  return PRT_OK;
}

static prt_spm_tensor_map_t *find_spm_tensor_map(prt_spm_xlate_ctx_t *ctx, uint32_t tensor_id) {
  if (!ctx) return NULL;
  for (uint32_t i = 0; i < ctx->tensor_map_count; ++i) {
    if (ctx->tensor_maps[i].tensor_id == tensor_id) return &ctx->tensor_maps[i];
  }
  return NULL;
}

static void record_spm_fault(prt_spm_xlate_ctx_t *ctx, uint64_t vaddr, uint32_t cause) {
  if (!ctx) return;
  ctx->fault_count += 1ULL;
  ctx->last_fault_vaddr = vaddr;
  ctx->last_fault_cause = cause;
}

int prt_page_table_init(prt_runtime_t *rt) {
  uint32_t total_pages;
  if (!rt) return PRT_ERR_INVAL;
  pthread_mutex_init(&rt->page_lock, NULL);

  total_pages = rt->cfg.num_cores * rt->cfg.pages_per_acc;
  rt->page_used = (uint8_t *)calloc(total_pages, sizeof(uint8_t));
  if (!rt->page_used) return PRT_ERR_NOMEM;

  rt->tensor_allocs = NULL;
  rt->tensor_alloc_count = 0;
  rt->tensor_alloc_cap = 0;

  rt->spm_pt_chunks = NULL;
  rt->spm_pt_chunk_count = 0;
  rt->spm_pt_chunk_cap = 0;
  rt->spm_pt_hugepage_bytes = runtime_pt_chunk_bytes(rt);
  PRT_PROGRESS_LOG("spm-pt pool init chunk_bytes=%llu total_pages=%u host_page=%llu",
                   (unsigned long long)rt->spm_pt_hugepage_bytes,
                   total_pages,
                   (unsigned long long)prt_host_page_size_bytes());

  rt->spm_pte = NULL;
  rt->spm_pte_cap = 0;
  rt->spm_next_vpage = 0;
  rt->spm_free_vpages = NULL;
  rt->spm_free_vpage_count = 0;
  rt->spm_free_vpage_cap = 0;
  rt->spm_ptbr_pa = 0;
  rt->spm_pte_alloc = NULL;
  rt->spm_pte_alloc_bytes = 0;
  rt->spm_alias_map = NULL;
  rt->spm_alias_map_bytes = 0;
  rt->spm_fault_count = 0;
  rt->spm_last_fault_vaddr = 0;
  rt->spm_last_fault_cause = 0;
  rt->spm_tensor_maps = NULL;
  rt->spm_tensor_map_count = 0;
  rt->spm_tensor_map_cap = 0;

  if (rt->cfg.spm_pt_pool_prealloc_hugepages > 0U) {
    for (uint32_t i = 0; i < rt->cfg.spm_pt_pool_prealloc_hugepages; ++i) {
      int rc = pt_pool_add_chunk(rt);
      if (rc != PRT_OK) {
        prt_page_table_destroy(rt);
        return rc;
      }
    }
  }

  return PRT_OK;
}

void prt_page_table_destroy(prt_runtime_t *rt) {
  uint32_t i;
  if (!rt) return;

  if (rt->tensor_allocs) {
    for (i = 0; i < rt->tensor_alloc_count; ++i) {
      free(rt->tensor_allocs[i].pages.data);
      rt->tensor_allocs[i].pages.data = NULL;
      rt->tensor_allocs[i].pages.size = 0;
      rt->tensor_allocs[i].pages.cap = 0;
    }
    free(rt->tensor_allocs);
  }
  rt->tensor_allocs = NULL;
  rt->tensor_alloc_count = 0;
  rt->tensor_alloc_cap = 0;

  for (i = 0; i < rt->spm_pt_chunk_count; ++i) {
    free(rt->spm_pt_chunks[i].free_slices);
#if defined(__linux__) && defined(__riscv)
    if (rt->spm_pt_chunks[i].base && rt->spm_pt_chunks[i].bytes > 0U) {
      munmap(rt->spm_pt_chunks[i].base, rt->spm_pt_chunks[i].bytes);
    }
#else
    free(rt->spm_pt_chunks[i].base);
#endif
    memset(&rt->spm_pt_chunks[i], 0, sizeof(rt->spm_pt_chunks[i]));
  }
  free(rt->spm_pt_chunks);
  rt->spm_pt_chunks = NULL;
  rt->spm_pt_chunk_count = 0;
  rt->spm_pt_chunk_cap = 0;
  rt->spm_pt_hugepage_bytes = 0U;

  rt->spm_pte = NULL;
  rt->spm_pte_cap = 0;
  rt->spm_next_vpage = 0;
  rt->spm_free_vpages = NULL;
  rt->spm_free_vpage_count = 0;
  rt->spm_free_vpage_cap = 0;
  rt->spm_ptbr_pa = 0;
  rt->spm_pte_alloc = NULL;
  rt->spm_pte_alloc_bytes = 0U;
  rt->spm_alias_map = NULL;
  rt->spm_alias_map_bytes = 0U;
  rt->spm_fault_count = 0ULL;
  rt->spm_last_fault_vaddr = 0ULL;
  rt->spm_last_fault_cause = 0U;
  rt->spm_tensor_maps = NULL;
  rt->spm_tensor_map_count = 0U;
  rt->spm_tensor_map_cap = 0U;

  free(rt->page_used);
  rt->page_used = NULL;

  pthread_mutex_destroy(&rt->page_lock);
}

void prt_spm_xlate_ctx_clear_faults(prt_spm_xlate_ctx_t *ctx) {
  if (!ctx) return;
  ctx->fault_count = 0ULL;
  ctx->last_fault_vaddr = 0ULL;
  ctx->last_fault_cause = 0U;
}

int prt_spm_xlate_ctx_alloc(prt_runtime_t *rt, prt_spm_xlate_ctx_t *ctx, uint32_t page_count) {
  size_t host_page_bytes;
  size_t pt_bytes;
  int rc;
  if (!rt || !ctx || page_count == 0U) return PRT_ERR_INVAL;
  if (page_count > PRT_SPM_XLATE_HW_MAX_PTES) return PRT_ERR_NOT_IMPL;
  if (ctx->pte || ctx->pt_chunk) return PRT_ERR_STATE;

  host_page_bytes = prt_host_page_size_bytes();
  pt_bytes = align_up_size((size_t)page_count * sizeof(uint64_t), host_page_bytes);
  rc = pt_pool_alloc_slice(rt, pt_bytes, &ctx->pt_chunk, &ctx->pt_slice_offset);
  if (rc != PRT_OK) return rc;

  memset((uint8_t *)ctx->pt_chunk->base + ctx->pt_slice_offset, 0, pt_bytes);
  ctx->pte = (uint64_t *)((uint8_t *)ctx->pt_chunk->base + ctx->pt_slice_offset);
  ctx->pte_count = page_count;
  ctx->next_vpage = 0U;
  ctx->pt_slice_bytes = pt_bytes;
  ctx->ptbr_pa = ctx->pt_chunk->base_pa + (uint64_t)ctx->pt_slice_offset;
  ctx->free_vpages = NULL;
  ctx->free_vpage_count = 0U;
  ctx->free_vpage_cap = 0U;
  ctx->tensor_maps = NULL;
  ctx->tensor_map_count = 0U;
  ctx->tensor_map_cap = 0U;
  prt_spm_xlate_ctx_clear_faults(ctx);

  rc = ensure_free_vpage_capacity(ctx, 1U);
  if (rc != PRT_OK) {
    prt_spm_xlate_ctx_release(rt, ctx);
    return rc;
  }
  ctx->free_vpage_count = 1U;
  ctx->free_vpages[0].start = 0U;
  ctx->free_vpages[0].count = page_count;
  return PRT_OK;
}

void prt_spm_xlate_ctx_release(prt_runtime_t *rt, prt_spm_xlate_ctx_t *ctx) {
  if (!rt || !ctx) return;
  free(ctx->tensor_maps);
  ctx->tensor_maps = NULL;
  ctx->tensor_map_count = 0U;
  ctx->tensor_map_cap = 0U;
  free(ctx->free_vpages);
  ctx->free_vpages = NULL;
  ctx->free_vpage_count = 0U;
  ctx->free_vpage_cap = 0U;
  if (ctx->pte && ctx->pt_slice_bytes > 0U) {
    memset(ctx->pte, 0, ctx->pt_slice_bytes);
  }
  if (ctx->pt_chunk && ctx->pt_slice_bytes > 0U) {
    (void)pt_pool_release_slice(ctx->pt_chunk, ctx->pt_slice_offset, ctx->pt_slice_bytes);
  }
  memset(ctx, 0, sizeof(*ctx));
}

int prt_alloc_tensor_pages(prt_runtime_t *rt, uint32_t tensor_id, size_t bytes,
                           const uint32_t *preferred_accs, uint32_t preferred_cnt,
                           prt_page_list_t *out) {
  uint32_t need_pages;
  uint32_t pref_order[PRT_MAX_CORES];
  uint32_t pref_order_n = 0;
  uint32_t fallback_order[PRT_MAX_CORES];
  uint32_t fallback_order_n = 0;
  uint32_t all_order[PRT_MAX_CORES];
  uint32_t all_order_n = 0;
  prt_tensor_alloc_t *slot;
  uint32_t page_bytes;

  if (!rt || !out || bytes == 0) return PRT_ERR_INVAL;

  page_bytes = rt_page_bytes(rt);
  need_pages = (uint32_t)((bytes + page_bytes - 1U) / page_bytes);

  pthread_mutex_lock(&rt->page_lock);

  slot = find_tensor_alloc(rt, tensor_id);
  if (slot) {
    slot->refcnt += 1;
    *out = slot->pages;
    pthread_mutex_unlock(&rt->page_lock);
    return PRT_OK;
  }

  if (ensure_tensor_alloc_capacity(rt) != PRT_OK) {
    pthread_mutex_unlock(&rt->page_lock);
    return PRT_ERR_NOMEM;
  }

  slot = &rt->tensor_allocs[rt->tensor_alloc_count];
  memset(slot, 0, sizeof(*slot));
  slot->tensor_id = tensor_id;
  slot->refcnt = 1;

  if (page_list_reserve(&slot->pages, need_pages) != PRT_OK) {
    pthread_mutex_unlock(&rt->page_lock);
    return PRT_ERR_NOMEM;
  }

  if (preferred_accs && preferred_cnt > 0) {
    for (uint32_t i = 0; i < preferred_cnt && pref_order_n < PRT_MAX_CORES; ++i) {
      uint32_t acc = preferred_accs[i];
      if (acc >= rt->cfg.num_cores) continue;
      if (append_unique_u32(pref_order, &pref_order_n, PRT_MAX_CORES, acc) != PRT_OK) {
        pthread_mutex_unlock(&rt->page_lock);
        return PRT_ERR_INVAL;
      }
    }
  }

  if (pref_order_n > 0) {
    alloc_pages_from_order(rt, slot, need_pages, pref_order, pref_order_n);
    if (slot->pages.size < need_pages) {
      build_fallback_order(rt->cfg.num_cores, pref_order, pref_order_n, fallback_order, &fallback_order_n);
      alloc_pages_from_order(rt, slot, need_pages, fallback_order, fallback_order_n);
    }
  } else {
    build_all_core_order(rt->cfg.num_cores, all_order, &all_order_n);
    alloc_pages_from_order(rt, slot, need_pages, all_order, all_order_n);
  }

  if (slot->pages.size != need_pages) {
    for (uint32_t lp = 0; lp < slot->pages.size; ++lp) {
      rt->page_used[slot->pages.data[lp].ppn] = 0;
    }
    free(slot->pages.data);
    memset(slot, 0, sizeof(*slot));
    pthread_mutex_unlock(&rt->page_lock);
    return PRT_ERR_NOMEM;
  }

  rt->tensor_alloc_count += 1;
  *out = slot->pages;
  pthread_mutex_unlock(&rt->page_lock);
  return PRT_OK;
}

int prt_release_tensor_pages(prt_runtime_t *rt, uint32_t tensor_id) {
  uint32_t i;
  if (!rt) return PRT_ERR_INVAL;

  pthread_mutex_lock(&rt->page_lock);
  for (i = 0; i < rt->tensor_alloc_count; ++i) {
    if (rt->tensor_allocs[i].tensor_id == tensor_id) {
      uint32_t j;
      if (rt->tensor_allocs[i].refcnt > 1) {
        rt->tensor_allocs[i].refcnt -= 1;
        pthread_mutex_unlock(&rt->page_lock);
        return PRT_OK;
      }

      (void)prt_spm_unmap_tensor(rt, tensor_id);

      for (j = 0; j < rt->tensor_allocs[i].pages.size; ++j) {
        rt->page_used[rt->tensor_allocs[i].pages.data[j].ppn] = 0;
      }
      free(rt->tensor_allocs[i].pages.data);

      if (i + 1 < rt->tensor_alloc_count) {
        memmove(&rt->tensor_allocs[i], &rt->tensor_allocs[i + 1],
                sizeof(prt_tensor_alloc_t) * (rt->tensor_alloc_count - i - 1));
      }
      rt->tensor_alloc_count -= 1;
      pthread_mutex_unlock(&rt->page_lock);
      return PRT_OK;
    }
  }
  pthread_mutex_unlock(&rt->page_lock);
  return PRT_ERR_INVAL;
}

int prt_spm_map_tensor_ctx(prt_runtime_t *rt, prt_spm_xlate_ctx_t *ctx,
                           uint32_t tensor_id, const prt_page_list_t *pages,
                           uint64_t alias_base_va, uint64_t *out_va_base) {
  prt_spm_tensor_map_t *m;
  uint32_t page_bytes;
  if (!rt || !ctx || !pages || !pages->data || pages->size == 0 || !out_va_base) return PRT_ERR_INVAL;

  page_bytes = rt_page_bytes(rt);

  pthread_mutex_lock(&rt->page_lock);
  m = find_spm_tensor_map(ctx, tensor_id);
  if (m) {
    m->refcnt += 1;
    *out_va_base = alias_base_va + (uint64_t)m->vpage_start * (uint64_t)page_bytes;
    pthread_mutex_unlock(&rt->page_lock);
    return PRT_OK;
  }

  if (ensure_spm_tensor_map_capacity(ctx) != PRT_OK) {
    pthread_mutex_unlock(&rt->page_lock);
    return PRT_ERR_NOMEM;
  }

  m = &ctx->tensor_maps[ctx->tensor_map_count++];
  memset(m, 0, sizeof(*m));
  m->tensor_id = tensor_id;
  if (reserve_vpages_locked(ctx, pages->size, &m->vpage_start) != PRT_OK) {
    ctx->tensor_map_count -= 1U;
    pthread_mutex_unlock(&rt->page_lock);
    return PRT_ERR_NOMEM;
  }
  m->page_count = pages->size;
  m->refcnt = 1;

  for (uint32_t i = 0; i < pages->size; ++i) {
    uint32_t vpage = m->vpage_start + i;
    ctx->pte[vpage] = pack_spm_pte(
      rt,
      PRT_SHARED_SPAD_GLOBAL_ADDR_BASE +
        (uint64_t)pages->data[i].ppn * (uint64_t)page_bytes);
  }
  prt_spm_xlate_ctx_publish(ctx);
  *out_va_base = alias_base_va + (uint64_t)m->vpage_start * (uint64_t)page_bytes;
  pthread_mutex_unlock(&rt->page_lock);
  return PRT_OK;
}

int prt_spm_unmap_tensor_ctx(prt_runtime_t *rt, prt_spm_xlate_ctx_t *ctx, uint32_t tensor_id) {
  if (!rt || !ctx) return PRT_ERR_INVAL;
  for (uint32_t i = 0; i < ctx->tensor_map_count; ++i) {
    prt_spm_tensor_map_t *m = &ctx->tensor_maps[i];
    if (m->tensor_id != tensor_id) continue;

    if (m->refcnt > 1) {
      m->refcnt -= 1;
      return PRT_OK;
    }

    for (uint32_t j = 0; j < m->page_count; ++j) {
      uint32_t vpage = m->vpage_start + j;
      if (vpage >= ctx->pte_count) break;
      ctx->pte[vpage] = 0;
    }
    prt_spm_xlate_ctx_publish(ctx);

    if (i + 1 < ctx->tensor_map_count) {
      memmove(&ctx->tensor_maps[i], &ctx->tensor_maps[i + 1],
              sizeof(prt_spm_tensor_map_t) * (ctx->tensor_map_count - i - 1));
    }
    (void)release_vpages_locked(ctx, m->vpage_start, m->page_count);
    ctx->tensor_map_count -= 1;
    return PRT_OK;
  }
  return PRT_ERR_INVAL;
}

int prt_spm_reserve_vpages_ctx(prt_runtime_t *rt, prt_spm_xlate_ctx_t *ctx,
                               uint32_t page_count, uint32_t *out_vpage_start) {
  if (!rt || !ctx || !out_vpage_start || page_count == 0) return PRT_ERR_INVAL;
  pthread_mutex_lock(&rt->page_lock);
  int rc = reserve_vpages_locked(ctx, page_count, out_vpage_start);
  pthread_mutex_unlock(&rt->page_lock);
  return rc;
}

int prt_spm_release_vpages_ctx(prt_runtime_t *rt, prt_spm_xlate_ctx_t *ctx,
                               uint32_t vpage_start, uint32_t page_count) {
  if (!rt || !ctx || page_count == 0) return PRT_ERR_INVAL;
  pthread_mutex_lock(&rt->page_lock);
  int rc = release_vpages_locked(ctx, vpage_start, page_count);
  pthread_mutex_unlock(&rt->page_lock);
  return rc;
}

int prt_spm_bind_vpages_ctx(prt_runtime_t *rt, prt_spm_xlate_ctx_t *ctx,
                            uint32_t vpage_start, const prt_page_list_t *pages,
                            uint32_t page_count) {
  uint32_t page_bytes;
  if (!rt || !ctx || !pages || !pages->data || page_count == 0) return PRT_ERR_INVAL;
  if (pages->size < page_count) return PRT_ERR_INVAL;
  if (vpage_start + page_count > ctx->pte_count) return PRT_ERR_INVAL;
  page_bytes = rt_page_bytes(rt);

  pthread_mutex_lock(&rt->page_lock);
  for (uint32_t i = 0; i < page_count; ++i) {
    ctx->pte[vpage_start + i] = pack_spm_pte(
      rt,
      PRT_SHARED_SPAD_GLOBAL_ADDR_BASE +
        (uint64_t)pages->data[i].ppn * (uint64_t)page_bytes);
  }
  prt_spm_xlate_ctx_publish(ctx);
  pthread_mutex_unlock(&rt->page_lock);
  return PRT_OK;
}

int prt_spm_unbind_vpages_ctx(prt_runtime_t *rt, prt_spm_xlate_ctx_t *ctx,
                              uint32_t vpage_start, uint32_t page_count) {
  if (!rt || !ctx || page_count == 0) return PRT_ERR_INVAL;
  if (vpage_start + page_count > ctx->pte_count) return PRT_ERR_INVAL;

  pthread_mutex_lock(&rt->page_lock);
  for (uint32_t i = 0; i < page_count; ++i) {
    ctx->pte[vpage_start + i] = 0;
  }
  prt_spm_xlate_ctx_publish(ctx);
  pthread_mutex_unlock(&rt->page_lock);
  return PRT_OK;
}

int prt_spm_translate_range_ctx(prt_runtime_t *rt, prt_spm_xlate_ctx_t *ctx,
                                uint64_t alias_base_va, uint64_t vaddr, uint64_t bytes,
                                prt_spm_xlate_seg_t *segs, uint32_t seg_cap,
                                uint32_t *out_seg_count) {
  uint64_t page_bytes;
  uint64_t cur;
  uint64_t left;
  uint32_t n = 0;

  if (!rt || !ctx || !segs || seg_cap == 0 || !out_seg_count) return PRT_ERR_INVAL;
  if (bytes == 0) {
    *out_seg_count = 0;
    return PRT_OK;
  }

  page_bytes = rt_page_bytes(rt);
  if (vaddr < alias_base_va) {
    record_spm_fault(ctx, vaddr, PRT_SPM_FAULT_OUT_OF_RANGE);
    return PRT_ERR_INVAL;
  }

  cur = vaddr;
  left = bytes;
  while (left > 0) {
    uint64_t off = cur - alias_base_va;
    uint32_t vpage = (uint32_t)(off / page_bytes);
    uint32_t in_page_off = (uint32_t)(off % page_bytes);
    uint32_t chunk = (uint32_t)((page_bytes - in_page_off) < left ?
                     (page_bytes - in_page_off) : left);
    uint64_t paddr;

    if (vpage >= ctx->pte_count) {
      record_spm_fault(ctx, cur, PRT_SPM_FAULT_OUT_OF_RANGE);
      return PRT_ERR_INVAL;
    }
    if (!spm_pte_valid(ctx->pte[vpage])) {
      record_spm_fault(ctx, cur, PRT_SPM_FAULT_INVALID_PTE);
      return PRT_ERR_INVAL;
    }

    paddr = spm_pte_paddr(rt, ctx->pte[vpage]) + (uint64_t)in_page_off;
    if (n > 0 && segs[n - 1].paddr + segs[n - 1].bytes == paddr) {
      segs[n - 1].bytes += chunk;
    } else {
      if (n >= seg_cap) return PRT_ERR_NOMEM;
      segs[n].vaddr = cur;
      segs[n].paddr = paddr;
      segs[n].bytes = chunk;
      n += 1;
    }

    cur += chunk;
    left -= chunk;
  }

  *out_seg_count = n;
  return PRT_OK;
}

uint64_t prt_spm_ptbr_pa(const prt_runtime_t *rt) {
  const prt_spm_xlate_ctx_t *ctx = current_spm_xlate_ctx_const(rt);
  if (!ctx) return 0;
  return ctx->ptbr_pa;
}

uint32_t prt_spm_pte_count(const prt_runtime_t *rt) {
  const prt_spm_xlate_ctx_t *ctx = current_spm_xlate_ctx_const(rt);
  if (!ctx) return 0;
  return ctx->pte_count;
}

uint64_t prt_spm_fault_count(const prt_runtime_t *rt) {
  const prt_spm_xlate_ctx_t *ctx = current_spm_xlate_ctx_const(rt);
  if (!ctx) return 0;
  return ctx->fault_count;
}

uint64_t prt_spm_last_fault_vaddr(const prt_runtime_t *rt) {
  const prt_spm_xlate_ctx_t *ctx = current_spm_xlate_ctx_const(rt);
  if (!ctx) return 0;
  return ctx->last_fault_vaddr;
}

uint32_t prt_spm_last_fault_cause(const prt_runtime_t *rt) {
  const prt_spm_xlate_ctx_t *ctx = current_spm_xlate_ctx_const(rt);
  if (!ctx) return 0;
  return ctx->last_fault_cause;
}

int prt_spm_map_tensor(prt_runtime_t *rt, uint32_t tensor_id, const prt_page_list_t *pages,
                       uint64_t *out_va_base) {
  prt_schedule_action_t *action = prt_runtime_current_action(rt);
  prt_spm_xlate_ctx_t *ctx = current_spm_xlate_ctx(rt);
  if (!rt || !action || !ctx) return PRT_ERR_STATE;
  return prt_spm_map_tensor_ctx(rt, ctx, tensor_id, pages, action->alias_base_va, out_va_base);
}

int prt_spm_unmap_tensor(prt_runtime_t *rt, uint32_t tensor_id) {
  prt_spm_xlate_ctx_t *ctx = current_spm_xlate_ctx(rt);
  if (!ctx) return PRT_ERR_STATE;
  return prt_spm_unmap_tensor_ctx(rt, ctx, tensor_id);
}

int prt_spm_reserve_vpages(prt_runtime_t *rt, uint32_t page_count, uint32_t *out_vpage_start) {
  prt_spm_xlate_ctx_t *ctx = current_spm_xlate_ctx(rt);
  if (!ctx) return PRT_ERR_STATE;
  return prt_spm_reserve_vpages_ctx(rt, ctx, page_count, out_vpage_start);
}

int prt_spm_release_vpages(prt_runtime_t *rt, uint32_t vpage_start, uint32_t page_count) {
  prt_spm_xlate_ctx_t *ctx = current_spm_xlate_ctx(rt);
  if (!ctx) return PRT_ERR_STATE;
  return prt_spm_release_vpages_ctx(rt, ctx, vpage_start, page_count);
}

int prt_spm_bind_vpages(prt_runtime_t *rt, uint32_t vpage_start, const prt_page_list_t *pages,
                        uint32_t page_count) {
  prt_spm_xlate_ctx_t *ctx = current_spm_xlate_ctx(rt);
  if (!ctx) return PRT_ERR_STATE;
  return prt_spm_bind_vpages_ctx(rt, ctx, vpage_start, pages, page_count);
}

int prt_spm_unbind_vpages(prt_runtime_t *rt, uint32_t vpage_start, uint32_t page_count) {
  prt_spm_xlate_ctx_t *ctx = current_spm_xlate_ctx(rt);
  if (!ctx) return PRT_ERR_STATE;
  return prt_spm_unbind_vpages_ctx(rt, ctx, vpage_start, page_count);
}

int prt_spm_translate_range(prt_runtime_t *rt, uint64_t vaddr, uint64_t bytes,
                            prt_spm_xlate_seg_t *segs, uint32_t seg_cap, uint32_t *out_seg_count) {
  prt_schedule_action_t *action = prt_runtime_current_action(rt);
  prt_spm_xlate_ctx_t *ctx = current_spm_xlate_ctx(rt);
  if (!rt || !ctx || !action) return PRT_ERR_STATE;
  return prt_spm_translate_range_ctx(rt, ctx, action->alias_base_va,
                                     vaddr, bytes, segs, seg_cap, out_seg_count);
}

int prt_map_tensor_for_acc(prt_runtime_t *rt, uint32_t tensor_id, uint32_t acc_id,
                           prt_vmap_desc_t *out) {
  prt_tensor_alloc_t *slot;
  uint64_t va_base;
  int rc;
  (void)acc_id;
  if (!rt || !out) return PRT_ERR_INVAL;

  pthread_mutex_lock(&rt->page_lock);
  slot = find_tensor_alloc(rt, tensor_id);
  pthread_mutex_unlock(&rt->page_lock);
  if (!slot) return PRT_ERR_INVAL;

  rc = prt_spm_map_tensor(rt, tensor_id, &slot->pages, &va_base);
  if (rc != PRT_OK) return rc;

  out->start = (uint32_t)((va_base - rt_spm_base(rt)) / (uint64_t)rt_page_bytes(rt));
  out->count = slot->pages.size;
  return PRT_OK;
}

int prt_unmap_tensor_for_acc(prt_runtime_t *rt, uint32_t acc_id,
                             const prt_vmap_desc_t *map) {
  (void)rt;
  (void)acc_id;
  (void)map;
  return PRT_OK;
}
