#include "prt_page_table.h"

#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "prt_runtime.h"

#define PRT_SPM_FAULT_OUT_OF_RANGE 1U
#define PRT_SPM_FAULT_INVALID_PTE 2U

static uint32_t rt_page_bytes(const prt_runtime_t *rt) {
  if (!rt || rt->cfg.page_size_bytes == 0) return PRT_PAGE_SIZE_BYTES;
  return rt->cfg.page_size_bytes;
}

static uint64_t rt_spm_base(const prt_runtime_t *rt) {
  if (!rt) return 0;
  return rt->cfg.spm_xlate_range_base;
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

  rt->spm_pte_paddr = NULL;
  rt->spm_pte_valid = NULL;
  rt->spm_pte_cap = 0;
  rt->spm_next_vpage = 0;
  rt->spm_ptbr_pa = 0;
  rt->spm_fault_count = 0;
  rt->spm_last_fault_vaddr = 0;
  rt->spm_last_fault_cause = 0;
  rt->spm_tensor_maps = NULL;
  rt->spm_tensor_map_count = 0;
  rt->spm_tensor_map_cap = 0;

  page_bytes = rt_page_bytes(rt);
  rt->spm_pte_cap = total_pages ? (total_pages * 8U) : 1024U;
  if (rt->spm_pte_cap < total_pages) rt->spm_pte_cap = total_pages;

  if (rt->cfg.spm_xlate_range_size != 0) {
    max_vpages_by_range = rt->cfg.spm_xlate_range_size / (uint64_t)page_bytes;
    if (max_vpages_by_range > 0 && max_vpages_by_range < rt->spm_pte_cap) {
      rt->spm_pte_cap = (uint32_t)max_vpages_by_range;
    }
  }

  rt->spm_pte_paddr = (uint64_t *)calloc(rt->spm_pte_cap, sizeof(uint64_t));
  rt->spm_pte_valid = (uint8_t *)calloc(rt->spm_pte_cap, sizeof(uint8_t));
  if (!rt->spm_pte_paddr || !rt->spm_pte_valid) {
    free(rt->spm_pte_paddr);
    free(rt->spm_pte_valid);
    rt->spm_pte_paddr = NULL;
    rt->spm_pte_valid = NULL;
    free(rt->page_used);
    rt->page_used = NULL;
    pthread_mutex_destroy(&rt->page_lock);
    return PRT_ERR_NOMEM;
  }

  rt->spm_ptbr_pa = (uint64_t)(uintptr_t)rt->spm_pte_paddr;
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

  free(rt->spm_pte_paddr);
  free(rt->spm_pte_valid);
  rt->spm_pte_paddr = NULL;
  rt->spm_pte_valid = NULL;
  rt->spm_pte_cap = 0;
  rt->spm_next_vpage = 0;
  rt->spm_ptbr_pa = 0;

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
    rt->spm_pte_valid[vpage] = 1U;
    rt->spm_pte_paddr[vpage] = PRT_SHARED_SPAD_GLOBAL_ADDR_BASE +
                               (uint64_t)pages->data[i].ppn * (uint64_t)page_bytes;
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
      rt->spm_pte_valid[vpage] = 0;
      rt->spm_pte_paddr[vpage] = 0;
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
    if (!rt->spm_pte_valid[vpage]) {
      record_spm_fault(rt, cur, PRT_SPM_FAULT_INVALID_PTE);
      return PRT_ERR_INVAL;
    }

    paddr = rt->spm_pte_paddr[vpage] + (uint64_t)in_page_off;
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
