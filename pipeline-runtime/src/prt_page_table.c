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

static int try_alloc_spm_pte_hugetlb_anon(prt_runtime_t *rt, size_t req_bytes, size_t probe_page_bytes) {
  const size_t huge_page_bytes = linux_huge_page_size_bytes();
  const size_t alloc_bytes = align_up_size(req_bytes, huge_page_bytes);
  void *base;
  uint64_t base_pa = 0;
  int rc = PRT_ERR_NOT_READY;

  base = mmap(NULL, alloc_bytes, PROT_READ | PROT_WRITE,
              MAP_PRIVATE | MAP_ANONYMOUS | MAP_HUGETLB
#ifdef MAP_POPULATE
                  | MAP_POPULATE
#endif
              ,
              -1, 0);
  if (base == MAP_FAILED) {
    int err = errno;
    PRT_PROGRESS_LOG("spm-pte hugetlb anon failed bytes=%llu errno=%d",
                     (unsigned long long)alloc_bytes,
                     err);
    return err == ENOMEM ? PRT_ERR_NOMEM : PRT_ERR_NOT_READY;
  }

  rc = probe_phys_contig_range(base, req_bytes, probe_page_bytes, &base_pa, NULL);
  if (rc == PRT_OK) {
    rt->spm_pte = (uint64_t *)base;
    rt->spm_pte_alloc = base;
    rt->spm_pte_alloc_bytes = alloc_bytes;
    rt->spm_ptbr_pa = base_pa;
    PRT_PROGRESS_LOG("spm-pte hugetlb anon ok req_bytes=%llu alloc_bytes=%llu pa=0x%llx",
                     (unsigned long long)req_bytes,
                     (unsigned long long)alloc_bytes,
                     (unsigned long long)base_pa);
    return PRT_OK;
  }

  PRT_PROGRESS_LOG("spm-pte hugetlb anon probe failed req_bytes=%llu alloc_bytes=%llu rc=%d",
                   (unsigned long long)req_bytes,
                   (unsigned long long)alloc_bytes,
                   rc);
  munmap(base, alloc_bytes);
  return rc;
}

static int try_alloc_spm_pte_hugetlbfs(prt_runtime_t *rt, size_t req_bytes, size_t probe_page_bytes) {
  static unsigned int seq = 0;
  const char *dir = "/dev/hugepages";
  const size_t huge_page_bytes = linux_huge_page_size_bytes();
  const size_t alloc_bytes = align_up_size(req_bytes, huge_page_bytes);
  char path[160];
  int fd;
  void *base;
  uint64_t base_pa = 0;
  int rc = PRT_ERR_NOT_READY;

  snprintf(path, sizeof(path), "%s/prt-spm-pte-%ld-%u", dir, (long)getpid(), seq++);
  fd = open(path, O_CREAT | O_EXCL | O_RDWR, 0600);
  if (fd < 0) {
    int err = errno;
    PRT_PROGRESS_LOG("spm-pte hugetlbfs open failed path=%s errno=%d", path, err);
    return PRT_ERR_NOT_READY;
  }

  if (ftruncate(fd, (off_t)alloc_bytes) != 0) {
    int err = errno;
    PRT_PROGRESS_LOG("spm-pte hugetlbfs ftruncate failed path=%s bytes=%llu errno=%d",
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
    PRT_PROGRESS_LOG("spm-pte hugetlbfs mmap failed bytes=%llu errno=%d",
                     (unsigned long long)alloc_bytes,
                     err);
    return err == ENOMEM ? PRT_ERR_NOMEM : PRT_ERR_NOT_READY;
  }

  rc = probe_phys_contig_range(base, req_bytes, probe_page_bytes, &base_pa, NULL);
  if (rc == PRT_OK) {
    rt->spm_pte = (uint64_t *)base;
    rt->spm_pte_alloc = base;
    rt->spm_pte_alloc_bytes = alloc_bytes;
    rt->spm_ptbr_pa = base_pa;
    PRT_PROGRESS_LOG("spm-pte hugetlbfs ok req_bytes=%llu alloc_bytes=%llu pa=0x%llx",
                     (unsigned long long)req_bytes,
                     (unsigned long long)alloc_bytes,
                     (unsigned long long)base_pa);
    return PRT_OK;
  }

  PRT_PROGRESS_LOG("spm-pte hugetlbfs probe failed req_bytes=%llu alloc_bytes=%llu rc=%d",
                   (unsigned long long)req_bytes,
                   (unsigned long long)alloc_bytes,
                   rc);
  munmap(base, alloc_bytes);
  return rc;
}

#else

int prt_host_virt_to_phys(const void *vaddr, uint64_t *paddr) {
  (void)vaddr;
  (void)paddr;
  return PRT_ERR_NOT_IMPL;
}

#endif

static int reserve_spm_alias_range(prt_runtime_t *rt) {
  if (!rt) return PRT_ERR_INVAL;
  if (rt->cfg.spm_xlate_range_base != 0 || rt->cfg.spm_xlate_range_size == 0) return PRT_OK;
#if defined(__linux__)
  {
    size_t bytes = align_up_size((size_t)rt->cfg.spm_xlate_range_size, prt_host_page_size_bytes());
    void *base = mmap(NULL, bytes, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (base == MAP_FAILED) return PRT_ERR_NOMEM;
    rt->spm_alias_map = base;
    rt->spm_alias_map_bytes = bytes;
    rt->cfg.spm_xlate_range_base = (uint64_t)(uintptr_t)base;
    return PRT_OK;
  }
#else
  rt->cfg.spm_xlate_range_base = 0x80000000ULL;
  return PRT_OK;
#endif
}

static int alloc_spm_pte_storage(prt_runtime_t *rt) {
  if (!rt || rt->spm_pte_cap == 0) return PRT_ERR_INVAL;
#if defined(__linux__) && defined(__riscv)
  {
    const size_t page_sz = prt_host_page_size_bytes();
    const size_t bytes = align_up_size((size_t)rt->spm_pte_cap * sizeof(uint64_t), page_sz);
    int last_rc = PRT_ERR_NOT_READY;
    int attempt;

    last_rc = try_alloc_spm_pte_hugetlb_anon(rt, bytes, page_sz);
    if (last_rc == PRT_OK) return PRT_OK;

    last_rc = try_alloc_spm_pte_hugetlbfs(rt, bytes, page_sz);
    if (last_rc == PRT_OK) return PRT_OK;

    for (attempt = 0; attempt < 32; ++attempt) {
      void *base;
      uint64_t base_pa = 0;
      base = mmap(NULL, bytes, PROT_READ | PROT_WRITE,
                  MAP_PRIVATE | MAP_ANONYMOUS
#ifdef MAP_POPULATE
                  | MAP_POPULATE
#endif
                  , -1, 0);
      if (base == MAP_FAILED) continue;
      last_rc = probe_phys_contig_range(base, bytes, page_sz, &base_pa, &last_rc);
      if (last_rc == PRT_OK) {
        rt->spm_pte = (uint64_t *)base;
        rt->spm_pte_alloc = base;
        rt->spm_pte_alloc_bytes = bytes;
        rt->spm_ptbr_pa = base_pa;
        return PRT_OK;
      }
      PRT_PROGRESS_LOG("spm-pte alloc retry attempt=%d bytes=%llu last_rc=%d",
                       attempt + 1,
                       (unsigned long long)bytes,
                       last_rc);
      munmap(base, bytes);
    }
    fprintf(stderr,
            "spm_pte alloc failed: entries=%u bytes=%llu attempts=%d last_rc=%s(%d)\n",
            rt->spm_pte_cap,
            (unsigned long long)bytes,
            32,
            prt_err_str(last_rc),
            last_rc);
    return last_rc == PRT_OK ? PRT_ERR_NOT_READY : last_rc;
  }
#else
  rt->spm_pte = (uint64_t *)calloc(rt->spm_pte_cap, sizeof(uint64_t));
  if (!rt->spm_pte) return PRT_ERR_NOMEM;
  rt->spm_pte_alloc = rt->spm_pte;
  rt->spm_pte_alloc_bytes = (size_t)rt->spm_pte_cap * sizeof(uint64_t);
  rt->spm_ptbr_pa = (uint64_t)(uintptr_t)rt->spm_pte;
  return PRT_OK;
#endif
}

static uint32_t rt_page_bytes(const prt_runtime_t *rt) {
  if (!rt || rt->cfg.page_size_bytes == 0) return PRT_PAGE_SIZE_BYTES;
  return rt->cfg.page_size_bytes;
}

static uint64_t rt_spm_base(const prt_runtime_t *rt) {
  if (!rt) return 0;
  return rt->cfg.spm_xlate_range_base;
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
  uint32_t acc_cursor = 0;
  if (!rt || !slot || !order || order_n == 0 || need_pages == 0) return;
  for (uint32_t lp = 0; lp < rt->cfg.pages_per_acc && slot->pages.size < need_pages; ++lp) {
    for (uint32_t i = 0; i < order_n && slot->pages.size < need_pages; ++i) {
      uint32_t acc = order[(acc_cursor + i) % order_n];
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
    acc_cursor += 1;
  }
}

static int ensure_spm_tensor_map_capacity(prt_runtime_t *rt) {
  uint32_t new_cap;
  prt_spm_tensor_map_t *tmp;
  if (!rt) return PRT_ERR_INVAL;
  if (rt->spm_tensor_map_count < rt->spm_tensor_map_cap) return PRT_OK;
  new_cap = rt->spm_tensor_map_cap ? (rt->spm_tensor_map_cap << 1) : 64U;
  tmp = (prt_spm_tensor_map_t *)realloc(rt->spm_tensor_maps, sizeof(prt_spm_tensor_map_t) * new_cap);
  if (!tmp) return PRT_ERR_NOMEM;
  memset(tmp + rt->spm_tensor_map_cap, 0, sizeof(prt_spm_tensor_map_t) * (new_cap - rt->spm_tensor_map_cap));
  rt->spm_tensor_maps = tmp;
  rt->spm_tensor_map_cap = new_cap;
  return PRT_OK;
}

static prt_spm_tensor_map_t *find_spm_tensor_map(prt_runtime_t *rt, uint32_t tensor_id) {
  if (!rt) return NULL;
  for (uint32_t i = 0; i < rt->spm_tensor_map_count; ++i) {
    if (rt->spm_tensor_maps[i].tensor_id == tensor_id) return &rt->spm_tensor_maps[i];
  }
  return NULL;
}

static void record_spm_fault(prt_runtime_t *rt, uint64_t vaddr, uint32_t cause) {
  if (!rt) return;
  rt->spm_fault_count += 1ULL;
  rt->spm_last_fault_vaddr = vaddr;
  rt->spm_last_fault_cause = cause;
}

int prt_page_table_init(prt_runtime_t *rt) {
  uint32_t total_pages;
  uint32_t page_bytes;
  uint64_t max_vpages_by_range;
  if (!rt) return PRT_ERR_INVAL;
  pthread_mutex_init(&rt->page_lock, NULL);

  total_pages = rt->cfg.num_cores * rt->cfg.pages_per_acc;
  rt->page_used = (uint8_t *)calloc(total_pages, sizeof(uint8_t));
  if (!rt->page_used) return PRT_ERR_NOMEM;

  rt->tensor_allocs = NULL;
  rt->tensor_alloc_count = 0;
  rt->tensor_alloc_cap = 0;

  rt->spm_pte = NULL;
  rt->spm_pte_cap = 0;
  rt->spm_next_vpage = 0;
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

  page_bytes = rt_page_bytes(rt);
  // `spm_pte_cap` is counted in PTE entries, not bytes.
  rt->spm_pte_cap = total_pages ? total_pages : 1024U;
  if (rt->spm_pte_cap < total_pages) rt->spm_pte_cap = total_pages;

  if (rt->cfg.spm_xlate_range_size != 0) {
    max_vpages_by_range = rt->cfg.spm_xlate_range_size / (uint64_t)page_bytes;
    if (max_vpages_by_range > 0 && max_vpages_by_range < rt->spm_pte_cap) {
      rt->spm_pte_cap = (uint32_t)max_vpages_by_range;
    }
  }

  if (reserve_spm_alias_range(rt) != PRT_OK) {
    free(rt->page_used);
    rt->page_used = NULL;
    pthread_mutex_destroy(&rt->page_lock);
    return PRT_ERR_NOMEM;
  }

  if (alloc_spm_pte_storage(rt) != PRT_OK) {
#if defined(__linux__)
    if (rt->spm_alias_map && rt->spm_alias_map_bytes) {
      munmap(rt->spm_alias_map, rt->spm_alias_map_bytes);
      rt->spm_alias_map = NULL;
      rt->spm_alias_map_bytes = 0;
    }
#endif
    rt->spm_pte = NULL;
    free(rt->page_used);
    rt->page_used = NULL;
    pthread_mutex_destroy(&rt->page_lock);
    return PRT_ERR_NOT_READY;
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

  free(rt->spm_tensor_maps);
  rt->spm_tensor_maps = NULL;
  rt->spm_tensor_map_count = 0;
  rt->spm_tensor_map_cap = 0;

#if defined(__linux__) && defined(__riscv)
  if (rt->spm_pte_alloc && rt->spm_pte_alloc_bytes) {
    munmap(rt->spm_pte_alloc, rt->spm_pte_alloc_bytes);
  }
#else
  free(rt->spm_pte);
#endif
  rt->spm_pte = NULL;
  rt->spm_pte_cap = 0;
  rt->spm_next_vpage = 0;
  rt->spm_ptbr_pa = 0;
  rt->spm_pte_alloc = NULL;
  rt->spm_pte_alloc_bytes = 0;
#if defined(__linux__)
  if (rt->spm_alias_map && rt->spm_alias_map_bytes) {
    munmap(rt->spm_alias_map, rt->spm_alias_map_bytes);
  }
#endif
  rt->spm_alias_map = NULL;
  rt->spm_alias_map_bytes = 0;

  free(rt->page_used);
  rt->page_used = NULL;

  pthread_mutex_destroy(&rt->page_lock);
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

int prt_spm_map_tensor(prt_runtime_t *rt, uint32_t tensor_id, const prt_page_list_t *pages,
                       uint64_t *out_va_base) {
  prt_spm_tensor_map_t *m;
  uint32_t page_bytes;
  uint64_t base;
  if (!rt || !pages || !pages->data || pages->size == 0 || !out_va_base) return PRT_ERR_INVAL;

  page_bytes = rt_page_bytes(rt);
  base = rt_spm_base(rt);

  pthread_mutex_lock(&rt->page_lock);
  m = find_spm_tensor_map(rt, tensor_id);
  if (m) {
    m->refcnt += 1;
    *out_va_base = base + (uint64_t)m->vpage_start * (uint64_t)page_bytes;
    pthread_mutex_unlock(&rt->page_lock);
    return PRT_OK;
  }

  if (ensure_spm_tensor_map_capacity(rt) != PRT_OK) {
    pthread_mutex_unlock(&rt->page_lock);
    return PRT_ERR_NOMEM;
  }

  if (rt->spm_next_vpage + pages->size > rt->spm_pte_cap) {
    pthread_mutex_unlock(&rt->page_lock);
    return PRT_ERR_NOMEM;
  }

  m = &rt->spm_tensor_maps[rt->spm_tensor_map_count++];
  memset(m, 0, sizeof(*m));
  m->tensor_id = tensor_id;
  m->vpage_start = rt->spm_next_vpage;
  m->page_count = pages->size;
  m->refcnt = 1;

  for (uint32_t i = 0; i < pages->size; ++i) {
    uint32_t vpage = m->vpage_start + i;
    rt->spm_pte[vpage] = pack_spm_pte(
      rt,
      PRT_SHARED_SPAD_GLOBAL_ADDR_BASE +
        (uint64_t)pages->data[i].ppn * (uint64_t)page_bytes);
  }

  rt->spm_next_vpage += pages->size;
  *out_va_base = base + (uint64_t)m->vpage_start * (uint64_t)page_bytes;
  pthread_mutex_unlock(&rt->page_lock);
  return PRT_OK;
}

int prt_spm_unmap_tensor(prt_runtime_t *rt, uint32_t tensor_id) {
  if (!rt) return PRT_ERR_INVAL;
  for (uint32_t i = 0; i < rt->spm_tensor_map_count; ++i) {
    prt_spm_tensor_map_t *m = &rt->spm_tensor_maps[i];
    if (m->tensor_id != tensor_id) continue;

    if (m->refcnt > 1) {
      m->refcnt -= 1;
      return PRT_OK;
    }

    for (uint32_t j = 0; j < m->page_count; ++j) {
      uint32_t vpage = m->vpage_start + j;
      if (vpage >= rt->spm_pte_cap) break;
      rt->spm_pte[vpage] = 0;
    }

    if (i + 1 < rt->spm_tensor_map_count) {
      memmove(&rt->spm_tensor_maps[i], &rt->spm_tensor_maps[i + 1],
              sizeof(prt_spm_tensor_map_t) * (rt->spm_tensor_map_count - i - 1));
    }
    rt->spm_tensor_map_count -= 1;
    return PRT_OK;
  }
  return PRT_ERR_INVAL;
}

int prt_spm_reserve_vpages(prt_runtime_t *rt, uint32_t page_count, uint32_t *out_vpage_start) {
  uint32_t start;
  if (!rt || !out_vpage_start || page_count == 0) return PRT_ERR_INVAL;
  pthread_mutex_lock(&rt->page_lock);
  if (rt->spm_next_vpage + page_count > rt->spm_pte_cap) {
    pthread_mutex_unlock(&rt->page_lock);
    return PRT_ERR_NOMEM;
  }
  start = rt->spm_next_vpage;
  rt->spm_next_vpage += page_count;
  pthread_mutex_unlock(&rt->page_lock);
  *out_vpage_start = start;
  return PRT_OK;
}

int prt_spm_bind_vpages(prt_runtime_t *rt, uint32_t vpage_start, const prt_page_list_t *pages,
                        uint32_t page_count) {
  uint32_t page_bytes;
  if (!rt || !pages || !pages->data || page_count == 0) return PRT_ERR_INVAL;
  if (pages->size < page_count) return PRT_ERR_INVAL;
  if (vpage_start + page_count > rt->spm_pte_cap) return PRT_ERR_INVAL;
  page_bytes = rt_page_bytes(rt);

  pthread_mutex_lock(&rt->page_lock);
  for (uint32_t i = 0; i < page_count; ++i) {
    rt->spm_pte[vpage_start + i] = pack_spm_pte(
      rt,
      PRT_SHARED_SPAD_GLOBAL_ADDR_BASE +
        (uint64_t)pages->data[i].ppn * (uint64_t)page_bytes);
  }
  pthread_mutex_unlock(&rt->page_lock);
  return PRT_OK;
}

int prt_spm_unbind_vpages(prt_runtime_t *rt, uint32_t vpage_start, uint32_t page_count) {
  if (!rt || page_count == 0) return PRT_ERR_INVAL;
  if (vpage_start + page_count > rt->spm_pte_cap) return PRT_ERR_INVAL;

  pthread_mutex_lock(&rt->page_lock);
  for (uint32_t i = 0; i < page_count; ++i) {
    rt->spm_pte[vpage_start + i] = 0;
  }
  pthread_mutex_unlock(&rt->page_lock);
  return PRT_OK;
}

int prt_spm_translate_range(prt_runtime_t *rt, uint64_t vaddr, uint64_t bytes,
                            prt_spm_xlate_seg_t *segs, uint32_t seg_cap, uint32_t *out_seg_count) {
  uint64_t base;
  uint64_t page_bytes;
  uint64_t cur;
  uint64_t left;
  uint32_t n = 0;

  if (!rt || !segs || seg_cap == 0 || !out_seg_count) return PRT_ERR_INVAL;
  if (bytes == 0) {
    *out_seg_count = 0;
    return PRT_OK;
  }

  base = rt_spm_base(rt);
  page_bytes = rt_page_bytes(rt);
  if (vaddr < base) {
    record_spm_fault(rt, vaddr, PRT_SPM_FAULT_OUT_OF_RANGE);
    return PRT_ERR_INVAL;
  }

  cur = vaddr;
  left = bytes;
  while (left > 0) {
    uint64_t off = cur - base;
    uint32_t vpage = (uint32_t)(off / page_bytes);
    uint32_t in_page_off = (uint32_t)(off % page_bytes);
    uint32_t chunk = (uint32_t)((page_bytes - in_page_off) < left ?
                     (page_bytes - in_page_off) : left);
    uint64_t paddr;

    if (vpage >= rt->spm_pte_cap) {
      record_spm_fault(rt, cur, PRT_SPM_FAULT_OUT_OF_RANGE);
      return PRT_ERR_INVAL;
    }
    if (!spm_pte_valid(rt->spm_pte[vpage])) {
      record_spm_fault(rt, cur, PRT_SPM_FAULT_INVALID_PTE);
      return PRT_ERR_INVAL;
    }

    paddr = spm_pte_paddr(rt, rt->spm_pte[vpage]) + (uint64_t)in_page_off;
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
  if (!rt) return 0;
  return rt->spm_ptbr_pa;
}

uint32_t prt_spm_pte_count(const prt_runtime_t *rt) {
  if (!rt) return 0;
  return rt->spm_pte_cap;
}

uint64_t prt_spm_fault_count(const prt_runtime_t *rt) {
  if (!rt) return 0;
  return rt->spm_fault_count;
}

uint64_t prt_spm_last_fault_vaddr(const prt_runtime_t *rt) {
  if (!rt) return 0;
  return rt->spm_last_fault_vaddr;
}

uint32_t prt_spm_last_fault_cause(const prt_runtime_t *rt) {
  if (!rt) return 0;
  return rt->spm_last_fault_cause;
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
