#ifndef REROCC_LINUX_SPM_XLATE_H
#define REROCC_LINUX_SPM_XLATE_H

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#include "include/rerocc_gemmini_spm_xlate.h"
#include "rerocc_linux_pagemap.h"

#ifndef REROCC_SPM_PAGE_SHIFT
#define REROCC_SPM_PAGE_SHIFT 10U
#endif

#ifndef REROCC_SPM_PAGE_BYTES
#define REROCC_SPM_PAGE_BYTES (1ULL << REROCC_SPM_PAGE_SHIFT)
#endif

typedef struct {
  uint64_t *pte;
  uint32_t pte_cap;
  uint64_t ptbr_pa;
  void *alloc;
  size_t alloc_bytes;
} rerocc_linux_spm_xlate_ctx_t;

typedef struct {
  uint64_t range_base;
  uint64_t range_size;
  uint32_t pte_count;
} rerocc_linux_spm_xlate_window_t;

static inline uint64_t rerocc_linux_spm_xlate_align_down(uint64_t value, uint64_t align) {
  return value & ~(align - 1ULL);
}

static inline uint64_t rerocc_linux_spm_xlate_align_up(uint64_t value, uint64_t align) {
  return (value + align - 1ULL) & ~(align - 1ULL);
}

static inline void rerocc_linux_spm_xlate_ctx_reset(rerocc_linux_spm_xlate_ctx_t *ctx) {
  if (!ctx) {
    return;
  }
  ctx->pte = NULL;
  ctx->pte_cap = 0;
  ctx->ptbr_pa = 0;
  ctx->alloc = NULL;
  ctx->alloc_bytes = 0;
}

static inline bool rerocc_linux_spm_xlate_init(rerocc_linux_spm_xlate_ctx_t *ctx,
                                               const rerocc_linux_pagemap_t *pm) {
  size_t host_page_bytes;
  void *base;
  uint64_t base_pa = 0;

  if (!ctx || !pm) {
    return false;
  }
  if (ctx->pte != NULL) {
    return true;
  }

  host_page_bytes = (size_t)(pm->page_size > 0 ? pm->page_size : getpagesize());
  if (host_page_bytes == 0) {
    printf("spm_xlate init failed: invalid host page size\n");
    return false;
  }

  base = mmap(NULL,
              host_page_bytes,
              PROT_READ | PROT_WRITE,
              MAP_PRIVATE | MAP_ANONYMOUS
#ifdef MAP_POPULATE
                  | MAP_POPULATE
#endif
              ,
              -1,
              0);
  if (base == MAP_FAILED) {
    printf("spm_xlate mmap failed: %s\n", strerror(errno));
    return false;
  }

  memset(base, 0, host_page_bytes);
  *(volatile uint8_t *)base = 0;
  if (!rerocc_linux_virt_to_phys(pm, base, &base_pa)) {
    printf("spm_xlate virt_to_phys failed for ptbr backing\n");
    munmap(base, host_page_bytes);
    return false;
  }

  ctx->pte = (uint64_t *)base;
  ctx->pte_cap = (uint32_t)(host_page_bytes / sizeof(uint64_t));
  ctx->ptbr_pa = base_pa;
  ctx->alloc = base;
  ctx->alloc_bytes = host_page_bytes;
  return ctx->pte_cap != 0;
}

static inline void rerocc_linux_spm_xlate_clear(rerocc_linux_spm_xlate_ctx_t *ctx) {
  if (!ctx || !ctx->pte || ctx->pte_cap == 0) {
    return;
  }
  memset(ctx->pte, 0, (size_t)ctx->pte_cap * sizeof(uint64_t));
}

static inline void rerocc_linux_spm_xlate_window_reset(rerocc_linux_spm_xlate_window_t *window) {
  if (!window) {
    return;
  }
  window->range_base = 0;
  window->range_size = 0;
  window->pte_count = 0;
}

static inline bool rerocc_linux_spm_xlate_window_include(
    const rerocc_linux_spm_xlate_ctx_t *ctx,
    rerocc_linux_spm_xlate_window_t *window,
    uint64_t vaddr,
    uint64_t bytes) {
  const uint64_t page_bytes = (uint64_t)REROCC_SPM_PAGE_BYTES;
  uint64_t start;
  uint64_t end;
  uint64_t merged_start;
  uint64_t merged_end;
  uint64_t needed_pages;

  if (!ctx || !window || bytes == 0) {
    return false;
  }

  start = rerocc_linux_spm_xlate_align_down(vaddr, page_bytes);
  end = rerocc_linux_spm_xlate_align_up(vaddr + bytes, page_bytes);
  if (window->range_size == 0) {
    merged_start = start;
    merged_end = end;
  } else {
    merged_start = start < window->range_base ? start : window->range_base;
    merged_end = end > (window->range_base + window->range_size) ?
        end : (window->range_base + window->range_size);
  }

  needed_pages = (merged_end - merged_start) / page_bytes;
  if (needed_pages == 0 || needed_pages > ctx->pte_cap) {
    return false;
  }

  window->range_base = merged_start;
  window->range_size = merged_end - merged_start;
  window->pte_count = (uint32_t)needed_pages;
  return true;
}

static inline bool rerocc_linux_spm_xlate_map_range(const rerocc_linux_spm_xlate_ctx_t *ctx,
                                                    const rerocc_linux_spm_xlate_window_t *window,
                                                    uint64_t vaddr,
                                                    uint64_t paddr,
                                                    uint64_t bytes) {
  const uint64_t page_bytes = (uint64_t)REROCC_SPM_PAGE_BYTES;
  uint64_t cur_vaddr;
  uint64_t cur_paddr;
  uint64_t end_vaddr;

  if (!ctx || !ctx->pte || !window || window->range_size == 0 || bytes == 0) {
    return false;
  }

  cur_vaddr = rerocc_linux_spm_xlate_align_down(vaddr, page_bytes);
  cur_paddr = rerocc_linux_spm_xlate_align_down(paddr, page_bytes);
  end_vaddr = rerocc_linux_spm_xlate_align_down(vaddr + bytes - 1ULL, page_bytes);

  while (1) {
    uint64_t vpage = (cur_vaddr - window->range_base) >> REROCC_SPM_PAGE_SHIFT;
    if (vpage >= window->pte_count || vpage >= ctx->pte_cap) {
      return false;
    }
    ctx->pte[vpage] = ((cur_paddr >> REROCC_SPM_PAGE_SHIFT) << 1) | 1ULL;
    if (cur_vaddr == end_vaddr) {
      break;
    }
    cur_vaddr += page_bytes;
    cur_paddr += page_bytes;
  }

  return true;
}

static inline bool rerocc_linux_spm_xlate_program(const rerocc_linux_spm_xlate_ctx_t *ctx,
                                                  const rerocc_linux_spm_xlate_window_t *window,
                                                  bool enable) {
  if (!ctx || !ctx->pte || !window || window->range_size == 0 || window->pte_count == 0) {
    return false;
  }

  rerocc_gemmini_spm_xlate_cfg(ctx->ptbr_pa,
                               window->pte_count,
                               REROCC_SPM_PAGE_SHIFT,
                               enable ? 1U : 0U);
  rerocc_gemmini_spm_xlate_range(window->range_base, window->range_size);
  return true;
}

static inline void rerocc_linux_spm_xlate_disable(void) {
  rerocc_gemmini_spm_xlate_cfg(0ULL, 0U, REROCC_SPM_PAGE_SHIFT, 0U);
  rerocc_gemmini_spm_xlate_range(0ULL, 0ULL);
  rerocc_gemmini_spm_xlate_flush();
}

#endif  // REROCC_LINUX_SPM_XLATE_H
