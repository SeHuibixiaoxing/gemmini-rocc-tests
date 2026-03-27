#if defined(__linux__) && !defined(_GNU_SOURCE)
#define _GNU_SOURCE
#endif

#include "prt_schedule_action.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#if defined(__linux__)
#include <sys/mman.h>
#endif

#include "prt_error.h"
#include "prt_page_table.h"
#include "prt_progress.h"
#include "prt_rerocc.h"
#include "prt_runtime.h"

static uint32_t g_next_action_id = 1;

static int is_valid_tile_count(uint32_t v) {
  return v == 1U || v == 2U || v == 4U || v == 8U || v == 16U || v == 32U;
}

#if PRT_ENABLE_ONLY_MARKER
static const char *buffer_binding_kind_name(uint32_t kind) {
  switch ((prt_buffer_binding_kind_t)kind) {
    case PRT_BUFFER_BINDING_WEIGHT: return "WEIGHT";
    case PRT_BUFFER_BINDING_PIPE: return "PIPE";
    case PRT_BUFFER_BINDING_RING: return "RING";
    default: return "UNKNOWN";
  }
}
#endif

static void format_u32_list(const uint32_t *vals, uint32_t count, char *buf, size_t buf_size) {
  size_t used = 0U;
  if (!buf || buf_size == 0U) return;
  buf[0] = '\0';
  if (!vals || count == 0U) {
    (void)snprintf(buf, buf_size, "-");
    return;
  }
  for (uint32_t i = 0; i < count; ++i) {
    int n = snprintf(buf + used, buf_size - used, "%s%u", i == 0U ? "" : ",", vals[i]);
    if (n < 0) break;
    if ((size_t)n >= buf_size - used) {
      used = buf_size - 1U;
      break;
    }
    used += (size_t)n;
  }
}

#if PRT_ENABLE_ONLY_MARKER
static int should_log_action_page_details(const prt_schedule_action_t *action) {
  return action && action->segment_idx == 0U;
}

static uint64_t action_page_paddr(const prt_runtime_t *rt, const prt_page_t *page) {
  const uint32_t page_bytes =
    rt && rt->cfg.page_size_bytes ? rt->cfg.page_size_bytes : PRT_PAGE_SIZE_BYTES;
  if (!page) return 0ULL;
  return PRT_SHARED_SPAD_GLOBAL_ADDR_BASE + (uint64_t)page->ppn * (uint64_t)page_bytes;
}
#endif

static void log_action_page_list(const prt_runtime_t *rt, const prt_schedule_action_t *action,
                                 const char *kind, uint32_t buffer_id, uint32_t tensor_id,
                                 uint32_t stage_id, uint32_t slot_id,
                                 const prt_page_list_t *pages) {
#if !PRT_ENABLE_ONLY_MARKER
  (void)rt;
  (void)action;
  (void)kind;
  (void)buffer_id;
  (void)tensor_id;
  (void)stage_id;
  (void)slot_id;
  (void)pages;
  return;
#else
  if (!rt || !action || !pages || !pages->data) return;
  PRT_MARKER_LOG("action=%u segment=%u alloc-pages kind=%s buffer=%u tensor=%u stage=%u slot=%u count=%u",
                 action->action_id, action->segment_idx,
                 kind ? kind : "UNKNOWN",
                 buffer_id, tensor_id, stage_id, slot_id, pages->size);
  if (!should_log_action_page_details(action)) return;
  for (uint32_t base = 0; base < pages->size; base += 4U) {
    char detail[768];
    size_t used = 0U;
    const uint32_t end = (base + 4U < pages->size) ? (base + 4U) : pages->size;
    detail[0] = '\0';
    for (uint32_t i = base; i < end; ++i) {
      const prt_page_t *page = &pages->data[i];
      int n = snprintf(detail + used, sizeof(detail) - used,
                       "%s%u:ppn%u@a%u/l%u/p0x%llx",
                       i == base ? "" : " | ",
                       i, page->ppn, page->acc_id, page->local_page_idx,
                       (unsigned long long)action_page_paddr(rt, page));
      if (n < 0) break;
      if ((size_t)n >= sizeof(detail) - used) {
        used = sizeof(detail) - 1U;
        break;
      }
      used += (size_t)n;
    }
    PRT_MARKER_LOG("action=%u segment=%u alloc-pages kind=%s buffer=%u tensor=%u stage=%u slot=%u idx=%u..%u %s",
                   action->action_id, action->segment_idx,
                   kind ? kind : "UNKNOWN",
                   buffer_id, tensor_id, stage_id, slot_id,
                   base, end == 0U ? 0U : (end - 1U), detail);
  }
#endif
}

static int append_unique_mgr(uint32_t **arr, uint32_t *n, uint32_t *cap, uint32_t v) {
  uint32_t i;
  uint32_t *tmp;
  uint32_t new_cap;
  if (!arr || !n || !cap) return PRT_ERR_INVAL;
  for (i = 0; i < *n; ++i) {
    if ((*arr)[i] == v) return PRT_OK;
  }
  if (*n < *cap) {
    (*arr)[*n] = v;
    *n += 1;
    return PRT_OK;
  }
  new_cap = *cap ? (*cap << 1) : 8U;
  tmp = (uint32_t *)realloc(*arr, sizeof(uint32_t) * new_cap);
  if (!tmp) return PRT_ERR_NOMEM;
  *arr = tmp;
  *cap = new_cap;
  (*arr)[*n] = v;
  *n += 1;
  return PRT_OK;
}

static int assign_stage_manager_slot(prt_runtime_t *rt, prt_schedule_action_t *action,
                                     prt_stage_acc_assign_t *assign, uint32_t slot_idx,
                                     uint32_t gm_local, uint32_t *all_g_cap, uint32_t *all_d_cap) {
  uint32_t dm_local;
  uint32_t gm_id;
  uint32_t dm_id;
  int rc;
  if (!rt || !action || !assign || !all_g_cap || !all_d_cap) return PRT_ERR_INVAL;
  if (gm_local >= rt->cfg.num_gemmini_mgrs) return PRT_ERR_PARSE;
  dm_local = gm_local;
  if (dm_local >= rt->cfg.num_dma_mgrs) return PRT_ERR_PARSE;
  gm_id = rt->cfg.gemmini_mgr_base_id + gm_local;
  dm_id = rt->cfg.dma_mgr_base_id + dm_local;
  assign->gemmini_mgr_ids[slot_idx] = gm_id;
  assign->dma_mgr_ids[slot_idx] = dm_id;
  rc = append_unique_mgr(&action->acc_source.all_gemmini_mgr_ids,
                         &action->acc_source.all_count, all_g_cap, gm_id);
  if (rc != PRT_OK) return rc;
  rc = append_unique_mgr(&action->acc_source.all_dma_mgr_ids,
                         &action->acc_source.num_acc, all_d_cap, dm_id);
  if (rc != PRT_OK) return rc;
  return PRT_OK;
}

static void free_acc_source(prt_acc_source_t *accs) {
  uint32_t i;
  if (!accs) return;
  if (accs->stage_assign) {
    for (i = 0; i < accs->stage_count; ++i) {
      free(accs->stage_assign[i].gemmini_mgr_ids);
      free(accs->stage_assign[i].dma_mgr_ids);
      accs->stage_assign[i].gemmini_mgr_ids = NULL;
      accs->stage_assign[i].dma_mgr_ids = NULL;
      accs->stage_assign[i].acc_util = 0;
    }
    free(accs->stage_assign);
  }
  free(accs->all_gemmini_mgr_ids);
  free(accs->all_dma_mgr_ids);
  memset(accs, 0, sizeof(*accs));
}

static void free_spm_binding_array(prt_spm_page_binding_t *arr, uint32_t n) {
  if (!arr) return;
  for (uint32_t i = 0; i < n; ++i) {
    free(arr[i].pages.data);
    arr[i].pages.data = NULL;
    arr[i].pages.size = 0;
    arr[i].pages.cap = 0;
  }
  free(arr);
}

static void free_spm_source(prt_spm_source_t *spm) {
  if (!spm) return;
  free(spm->alloc_keys);
  free_spm_binding_array(spm->weight_pages, spm->weight_count);
  free_spm_binding_array(spm->in_stage_pages, spm->in_stage_count);
  free_spm_binding_array(spm->ring_pages, spm->ring_count);
  memset(spm, 0, sizeof(*spm));
}

static int clone_page_list(prt_page_list_t *dst, const prt_page_list_t *src) {
  if (!dst || !src) return PRT_ERR_INVAL;
  memset(dst, 0, sizeof(*dst));
  if (src->size == 0 || !src->data) return PRT_OK;
  dst->data = (prt_page_t *)calloc(src->size, sizeof(prt_page_t));
  if (!dst->data) return PRT_ERR_NOMEM;
  memcpy(dst->data, src->data, sizeof(prt_page_t) * src->size);
  dst->size = src->size;
  dst->cap = src->size;
  return PRT_OK;
}

static int append_spm_binding(prt_spm_page_binding_t **arr, uint32_t *n, uint32_t *cap,
                              uint32_t buffer_id, uint32_t tensor_id,
                              uint32_t stage_id, uint32_t slot_id,
                              const prt_page_list_t *pages) {
  prt_spm_page_binding_t *tmp;
  uint32_t new_cap;
  int rc;
  if (!arr || !n || !cap || !pages) return PRT_ERR_INVAL;
  if (*n >= *cap) {
    new_cap = *cap ? (*cap << 1) : 16U;
    tmp = (prt_spm_page_binding_t *)realloc(*arr, sizeof(prt_spm_page_binding_t) * new_cap);
    if (!tmp) return PRT_ERR_NOMEM;
    *arr = tmp;
    *cap = new_cap;
  }
  memset(&(*arr)[*n], 0, sizeof((*arr)[*n]));
  (*arr)[*n].buffer_id = buffer_id;
  (*arr)[*n].tensor_id = tensor_id;
  (*arr)[*n].stage_id = stage_id;
  (*arr)[*n].slot_id = slot_id;
  rc = clone_page_list(&(*arr)[*n].pages, pages);
  if (rc != PRT_OK) return rc;
  *n += 1U;
  return PRT_OK;
}

typedef struct {
  uint32_t alias_group_id;
  uint32_t slot_count;
  prt_page_list_t slot_pages[2];
} alias_group_plan_t;

static int alloc_action_alias_window(prt_runtime_t *rt, prt_schedule_action_t *action, uint32_t page_count) {
  const uint32_t page_bytes = rt->cfg.page_size_bytes ? rt->cfg.page_size_bytes : PRT_PAGE_SIZE_BYTES;
  const size_t alias_bytes = (size_t)page_count * (size_t)page_bytes;
  size_t alloc_bytes = alias_bytes;
  if (!rt || !action || page_count == 0U) return PRT_ERR_INVAL;
  if (alloc_bytes == 0U) return PRT_ERR_INVAL;
#if defined(__linux__)
  {
    const size_t host_page_bytes = prt_host_page_size_bytes();
    if (host_page_bytes > 0U) {
      const size_t rem = alloc_bytes % host_page_bytes;
      if (rem != 0U) alloc_bytes += host_page_bytes - rem;
    }
    void *base = mmap(NULL, alloc_bytes, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (base == MAP_FAILED) return PRT_ERR_NOMEM;
    action->alias_alloc = base;
    action->alias_alloc_bytes = alloc_bytes;
    action->alias_base_va = (uint64_t)(uintptr_t)base;
  }
#else
  // Non-Linux environments still rely on a caller-provided alias base hint.
  // The action-private VA allocator is currently implemented on the Linux path.
  action->alias_base_va = rt->cfg.spm_xlate_range_base;
#endif
  action->alias_bytes = (uint64_t)alias_bytes;
  return PRT_OK;
}

static void free_action_alias_window(prt_schedule_action_t *action) {
  if (!action) return;
#if defined(__linux__)
  if (action->alias_alloc && action->alias_alloc_bytes > 0U) {
    munmap(action->alias_alloc, action->alias_alloc_bytes);
  }
#endif
  action->alias_alloc = NULL;
  action->alias_alloc_bytes = 0U;
  action->alias_base_va = 0ULL;
  action->alias_bytes = 0ULL;
}

static uint32_t stage_preferred_mgrs(const prt_schedule_action_t *action, uint32_t stage_id,
                                     uint32_t *out_mgrs, uint32_t cap) {
  if (!action || !out_mgrs || cap == 0U) return 0U;
  if (stage_id >= action->acc_source.stage_count || !action->acc_source.stage_assign) return 0U;
  const prt_stage_acc_assign_t *assign = &action->acc_source.stage_assign[stage_id];
  uint32_t n = assign->acc_util;
  if (n > cap) n = cap;
  for (uint32_t i = 0; i < n; ++i) out_mgrs[i] = assign->gemmini_mgr_ids[i];
  return n;
}

static int alloc_slot_pages_for_action(prt_runtime_t *rt, prt_schedule_action_t *action,
                                       uint32_t *alloc_key_cursor, uint32_t pages_per_slot,
                                       const uint32_t *preferred_accs, uint32_t preferred_cnt,
                                       prt_page_list_t *out_pages) {
  const uint32_t page_bytes = rt->cfg.page_size_bytes ? rt->cfg.page_size_bytes : PRT_PAGE_SIZE_BYTES;
  const uint32_t alloc_key = (*alloc_key_cursor)++;
  int rc;
  if (!rt || !action || !alloc_key_cursor || !out_pages || pages_per_slot == 0U) return PRT_ERR_INVAL;
  memset(out_pages, 0, sizeof(*out_pages));
  rc = prt_alloc_tensor_pages(rt, alloc_key, (size_t)pages_per_slot * (size_t)page_bytes,
                              preferred_accs, preferred_cnt, out_pages);
  if (rc != PRT_OK) return rc;
  rc = prt_action_track_alloc_key(action, alloc_key);
  if (rc != PRT_OK) {
    (void)prt_release_tensor_pages(rt, alloc_key);
    memset(out_pages, 0, sizeof(*out_pages));
    return rc;
  }
  action->spm_source.num_spm_pages += out_pages->size;
  return PRT_OK;
}

static alias_group_plan_t *find_alias_group_plan(alias_group_plan_t *plans, uint32_t n, uint32_t alias_group_id) {
  if (!plans || alias_group_id == 0U) return NULL;
  for (uint32_t i = 0; i < n; ++i) {
    if (plans[i].alias_group_id == alias_group_id) return &plans[i];
  }
  return NULL;
}

static const prt_spm_page_binding_t *find_action_binding(const prt_spm_page_binding_t *arr, uint32_t n,
                                                         uint32_t buffer_id, uint32_t slot_id) {
  if (!arr) return NULL;
  for (uint32_t i = 0; i < n; ++i) {
    if (arr[i].buffer_id == buffer_id && arr[i].slot_id == slot_id) {
      return &arr[i];
    }
  }
  return NULL;
}

static int clone_into_runtime_page_list(prt_page_list_t *dst, const prt_page_list_t *src) {
  free(dst->data);
  dst->data = NULL;
  dst->size = 0;
  dst->cap = 0;
  return clone_page_list(dst, src);
}

static const prt_buffer_binding_t *find_segment_buffer_binding(const prt_segment_desc_t *seg, uint32_t buffer_id) {
  if (!seg || !seg->buffer_bindings || buffer_id == 0U) return NULL;
  for (uint32_t i = 0; i < seg->buffer_binding_count; ++i) {
    if (seg->buffer_bindings[i].buffer_id == buffer_id) return &seg->buffer_bindings[i];
  }
  return NULL;
}

static void clear_exec_weight_bindings(prt_action_exec_t *exec) {
  if (!exec || !exec->topo_weight_pages) return;
  for (uint32_t i = 0; i < exec->topo_weight_count; ++i) {
    free(exec->topo_weight_pages[i].pages.data);
    exec->topo_weight_pages[i].pages.data = NULL;
    exec->topo_weight_pages[i].pages.size = 0;
    exec->topo_weight_pages[i].pages.cap = 0;
  }
  exec->topo_weight_count = 0;
}

static void clear_exec_pipebuf_pages(prt_action_exec_t *exec) {
  if (!exec || !exec->pipebufs) return;
  for (uint32_t i = 0; i < exec->pipebuf_count; ++i) {
    for (uint32_t slot = 0; slot < 2U; ++slot) {
      free(exec->pipebufs[i].slot_pages[slot].data);
      exec->pipebufs[i].slot_pages[slot].data = NULL;
      exec->pipebufs[i].slot_pages[slot].size = 0;
      exec->pipebufs[i].slot_pages[slot].cap = 0;
    }
  }
}

static void clear_exec_ringbuf_pages(prt_action_exec_t *exec) {
  if (!exec || !exec->ringbufs) return;
  for (uint32_t i = 0; i < exec->ringbuf_count; ++i) {
    prt_ringbuf_t *rb = &exec->ringbufs[i];
    if (!rb->slot_pages) continue;
    for (uint32_t slot = 0; slot < rb->size; ++slot) {
      free(rb->slot_pages[slot].data);
      rb->slot_pages[slot].data = NULL;
      rb->slot_pages[slot].size = 0;
      rb->slot_pages[slot].cap = 0;
    }
  }
}

static int runtime_shared_aliasing_valid(const prt_action_exec_t *exec) {
  if (!exec || !exec->pipebufs) return PRT_ERR_INVAL;
  for (uint32_t i = 0; i < exec->pipebuf_count; ++i) {
    const prt_pipebuf_t *a = &exec->pipebufs[i];
    if (a->kind != PRT_BUF_C4_SHARED_NO_RING_PAIR) continue;
    for (uint32_t j = i + 1U; j < exec->pipebuf_count; ++j) {
      const prt_pipebuf_t *b = &exec->pipebufs[j];
      uint32_t slots_a;
      uint32_t slots_b;
      uint32_t slots;
      if (b->kind != PRT_BUF_C4_SHARED_NO_RING_PAIR) continue;
      if (a->segment_idx != b->segment_idx || a->tensor_id != b->tensor_id) continue;
      slots_a = a->with_double_buffer ? 2U : 1U;
      slots_b = b->with_double_buffer ? 2U : 1U;
      slots = slots_a < slots_b ? slots_a : slots_b;
      for (uint32_t slot = 0; slot < slots; ++slot) {
        const prt_page_list_t *pa = &a->slot_pages[slot];
        const prt_page_list_t *pb = &b->slot_pages[slot];
        if (pa->size != pb->size) return PRT_ERR_STATE;
        for (uint32_t k = 0; k < pa->size; ++k) {
          if (pa->data[k].ppn != pb->data[k].ppn) return PRT_ERR_STATE;
        }
      }
    }
  }
  return PRT_OK;
}

static int append_runtime_weight_binding(prt_action_exec_t *exec, const prt_spm_page_binding_t *binding) {
  prt_spm_page_binding_t *tmp;
  uint32_t new_cap;
  int rc;
  if (!exec || !binding) return PRT_ERR_INVAL;
  if (exec->topo_weight_count >= exec->topo_weight_cap) {
    new_cap = exec->topo_weight_cap ? (exec->topo_weight_cap << 1U) : 16U;
    tmp = (prt_spm_page_binding_t *)realloc(exec->topo_weight_pages, sizeof(prt_spm_page_binding_t) * new_cap);
    if (!tmp) return PRT_ERR_NOMEM;
    memset(tmp + exec->topo_weight_cap, 0, sizeof(prt_spm_page_binding_t) * (new_cap - exec->topo_weight_cap));
    exec->topo_weight_pages = tmp;
    exec->topo_weight_cap = new_cap;
  }
  memset(&exec->topo_weight_pages[exec->topo_weight_count], 0, sizeof(prt_spm_page_binding_t));
  exec->topo_weight_pages[exec->topo_weight_count].buffer_id = binding->buffer_id;
  exec->topo_weight_pages[exec->topo_weight_count].tensor_id = binding->tensor_id;
  exec->topo_weight_pages[exec->topo_weight_count].stage_id = binding->stage_id;
  exec->topo_weight_pages[exec->topo_weight_count].slot_id = binding->slot_id;
  rc = clone_page_list(&exec->topo_weight_pages[exec->topo_weight_count].pages, &binding->pages);
  if (rc != PRT_OK) return rc;
  exec->topo_weight_count += 1U;
  return PRT_OK;
}

static int configure_action_spm_xlate(prt_runtime_t *rt, prt_schedule_action_t *action) {
  if (!rt || !action) return PRT_ERR_INVAL;
  if (!rt->cfg.spm_xlate_enable) return PRT_OK;
  if (!action->spm_xlate.pte || action->spm_xlate.pte_count == 0U) return PRT_OK;
  prt_spm_xlate_ctx_publish(&action->spm_xlate);
  PRT_MARKER_LOG("action=%u segment=%u xlate-range-begin base=0x%llx bytes=%llu mgrs=%u",
                 action->action_id, action->segment_idx,
                 (unsigned long long)action->alias_base_va,
                 (unsigned long long)action->alias_bytes,
                 action->acc_source.all_count);
  for (uint32_t i = 0; i < action->acc_source.all_count; ++i) {
    uint32_t manager_id = action->acc_source.all_gemmini_mgr_ids[i];
    int rc = prt_gemmini_spm_xlate_program(manager_id, action->spm_xlate.ptbr_pa,
                                           action->spm_xlate.pte_count,
                                           rt->cfg.spm_page_shift,
                                           action->alias_base_va,
                                           action->alias_bytes,
                                           1U);
    PRT_MARKER_LOG("action=%u segment=%u xlate-install mgr=%u ptbr=0x%llx ptes=%u base=0x%llx bytes=%llu",
                   action->action_id, action->segment_idx, manager_id,
                   (unsigned long long)action->spm_xlate.ptbr_pa,
                   action->spm_xlate.pte_count,
                   (unsigned long long)action->alias_base_va,
                   (unsigned long long)action->alias_bytes);
    if (rc != PRT_OK) return rc;
  }
  PRT_MARKER_LOG("action=%u segment=%u xlate-range-end base=0x%llx bytes=%llu mgrs=%u",
                 action->action_id, action->segment_idx,
                 (unsigned long long)action->alias_base_va,
                 (unsigned long long)action->alias_bytes,
                 action->acc_source.all_count);
  return PRT_OK;
}

static int disable_action_spm_xlate(prt_runtime_t *rt, const prt_schedule_action_t *action) {
  if (!rt || !action) return PRT_ERR_INVAL;
  if (!rt->cfg.spm_xlate_enable) return PRT_OK;
  for (uint32_t i = 0; i < action->acc_source.all_count; ++i) {
    uint32_t manager_id = action->acc_source.all_gemmini_mgr_ids[i];
    int rc = prt_gemmini_spm_xlate_reset(manager_id, rt->cfg.spm_page_shift);
    if (rc != PRT_OK) return rc;
    PRT_MARKER_LOG("action=%u segment=%u xlate-disable mgr=%u",
                   action->action_id, action->segment_idx, manager_id);
  }
  return PRT_OK;
}

int prt_action_generate(prt_runtime_t *rt, uint32_t segment_idx,
                        const prt_segment_desc_t *segment,
                        const prt_model_desc_t *model,
                        prt_schedule_action_t **out_action) {
  prt_schedule_action_t *a;
  (void)rt;
  if (!segment || !model || !out_action) return PRT_ERR_INVAL;
  a = (prt_schedule_action_t *)calloc(1, sizeof(*a));
  if (!a) return PRT_ERR_NOMEM;
  a->action_id = g_next_action_id++;
  a->segment_idx = segment_idx;
  a->pipeline_segment_ref = segment;
  a->model_ref = model;
  if (prt_action_exec_ensure(a) != PRT_OK) {
    free(a);
    return PRT_ERR_NOMEM;
  }
  a->state = PRT_ACTION_CREATED;
  *out_action = a;
  return PRT_OK;
}

int prt_action_alloc_acc(prt_runtime_t *rt, prt_schedule_action_t *action) {
  uint32_t i;
  uint32_t all_g_cap = 0;
  uint32_t all_d_cap = 0;
  uint32_t rr_cursor = 0;
  uint32_t total_needed = 0;
  uint8_t used_local[PRT_MAX_CORES];
  const prt_segment_desc_t *seg;
  if (!rt || !action || !action->pipeline_segment_ref) return PRT_ERR_INVAL;
  if (action->state != PRT_ACTION_CREATED) return PRT_ERR_STATE;

  seg = action->pipeline_segment_ref;
  if (rt->cfg.num_gemmini_mgrs > PRT_MAX_CORES || rt->cfg.num_dma_mgrs > PRT_MAX_CORES) {
    fprintf(stderr, "action_alloc_acc: manager count exceeds compile-time max cores\n");
    return PRT_ERR_NOT_IMPL;
  }
  action->acc_source.stage_count = seg->num_stages;
  action->acc_source.stage_assign =
    (prt_stage_acc_assign_t *)calloc(seg->num_stages, sizeof(prt_stage_acc_assign_t));
  if (!action->acc_source.stage_assign) return PRT_ERR_NOMEM;
  memset(used_local, 0, sizeof(used_local));
  PRT_PROGRESS_LOG("action=%u segment=%u alloc-acc begin stages=%u gemmini=%u dma=%u",
                   action->action_id, action->segment_idx, seg->num_stages,
                   rt->cfg.num_gemmini_mgrs, rt->cfg.num_dma_mgrs);

  for (i = 0; i < seg->num_stages; ++i) {
    prt_stage_acc_assign_t *assign = &action->acc_source.stage_assign[i];
    const prt_stage_map_t *stage = &seg->stages[i];
    uint32_t k;
    if (!stage->acc_util_present) {
      fprintf(stderr, "action_alloc_acc: stage=%u missing acc_util_present\n", i);
      return PRT_ERR_PARSE;
    }
    if (!is_valid_tile_count(stage->acc_util)) {
      fprintf(stderr, "action_alloc_acc: stage=%u invalid acc_util=%u\n", i, stage->acc_util);
      return PRT_ERR_PARSE;
    }
    if (stage->acc_util == 0) {
      fprintf(stderr, "action_alloc_acc: stage=%u zero acc_util\n", i);
      return PRT_ERR_PARSE;
    }
    if (stage->acc_util > rt->cfg.num_gemmini_mgrs) {
      fprintf(stderr, "action_alloc_acc: stage=%u acc_util=%u exceeds num_gemmini=%u\n",
              i, stage->acc_util, rt->cfg.num_gemmini_mgrs);
      return PRT_ERR_NOT_READY;
    }
    if (!stage->virtual_acc_ids_present || stage->num_virtual_acc_ids != stage->acc_util) {
      fprintf(stderr,
              "action_alloc_acc: stage=%u invalid vAccIdxList acc_util=%u ids=%u\n",
              i, stage->acc_util, stage->num_virtual_acc_ids);
      return PRT_ERR_PARSE;
    }
    if (stage->physical_acc_ids_present && stage->num_physical_acc_ids != stage->acc_util) {
      fprintf(stderr,
              "action_alloc_acc: stage=%u invalid explicit physical binding acc_util=%u ids=%u\n",
              i, stage->acc_util, stage->num_physical_acc_ids);
      return PRT_ERR_PARSE;
    }
    if (rt->cfg.num_dma_mgrs < rt->cfg.num_gemmini_mgrs) {
      fprintf(stderr, "action_alloc_acc: num_dma=%u less than num_gemmini=%u\n",
              rt->cfg.num_dma_mgrs, rt->cfg.num_gemmini_mgrs);
      return PRT_ERR_NOT_READY;
    }
    total_needed += stage->acc_util;
    if (total_needed > rt->cfg.num_gemmini_mgrs) {
      fprintf(stderr,
              "action_alloc_acc: segment over-subscribes accelerators total=%u num_gemmini=%u\n",
              total_needed, rt->cfg.num_gemmini_mgrs);
      return PRT_ERR_NOT_READY;
    }

    assign->stage_id = i;
    assign->acc_util = stage->acc_util;
    assign->gemmini_mgr_ids = (uint32_t *)calloc(assign->acc_util, sizeof(uint32_t));
    assign->dma_mgr_ids = (uint32_t *)calloc(assign->acc_util, sizeof(uint32_t));
    if (!assign->gemmini_mgr_ids || !assign->dma_mgr_ids) return PRT_ERR_NOMEM;

    if (stage->physical_acc_ids_present) {
      for (k = 0; k < assign->acc_util; ++k) {
        uint32_t gm_local = stage->physical_acc_ids[k];
        int rc;
        if (gm_local >= rt->cfg.num_gemmini_mgrs) {
          fprintf(stderr, "action_alloc_acc: stage=%u invalid physical gemmini id=%u num_gemmini=%u\n",
                  i, gm_local, rt->cfg.num_gemmini_mgrs);
          return PRT_ERR_PARSE;
        }
        if (used_local[gm_local]) {
          fprintf(stderr, "action_alloc_acc: stage=%u physical gemmini id=%u already reserved\n",
                  i, gm_local);
          return PRT_ERR_NOT_READY;
        }
        rc = assign_stage_manager_slot(rt, action, assign, k, gm_local, &all_g_cap, &all_d_cap);
        if (rc != PRT_OK) return rc;
        used_local[gm_local] = 1U;
      }
    } else {
      uint32_t assigned = 0;
      uint32_t searched = 0;
      while (assigned < assign->acc_util && searched < rt->cfg.num_gemmini_mgrs) {
        uint32_t gm_local = rr_cursor % rt->cfg.num_gemmini_mgrs;
        int rc;
        rr_cursor = (rr_cursor + 1U) % rt->cfg.num_gemmini_mgrs;
        searched += 1U;
        if (used_local[gm_local]) continue;
        rc = assign_stage_manager_slot(rt, action, assign, assigned, gm_local, &all_g_cap, &all_d_cap);
        if (rc != PRT_OK) return rc;
        used_local[gm_local] = 1U;
        assigned += 1U;
      }
      if (assigned != assign->acc_util) {
        fprintf(stderr, "action_alloc_acc: stage=%u unable to allocate %u accelerators\n",
                i, assign->acc_util);
        return PRT_ERR_NOT_READY;
      }
    }
#if PRT_ENABLE_PROGRESS_LOG
    {
      uint32_t gm0 = assign->acc_util > 0 ? assign->gemmini_mgr_ids[0] : 0U;
      uint32_t dm0 = assign->acc_util > 0 ? assign->dma_mgr_ids[0] : 0U;
      PRT_PROGRESS_LOG("action=%u stage=%u layer=%u acc_util=%u split=%u gm0=%u dm0=%u explicit=%u",
                       action->action_id, i, stage->layer_id, assign->acc_util,
                       (uint32_t)stage->split_kind, gm0, dm0, stage->physical_acc_ids_present);
    }
#endif
  }

  if (action->acc_source.num_acc < action->acc_source.all_count) {
    action->acc_source.num_acc = action->acc_source.all_count;
  }
  PRT_MARKER_LOG("action=%u segment=%u alloc-acc unique_gemmini=%u unique_dma=%u stages=%u",
                 action->action_id, action->segment_idx,
                 action->acc_source.all_count, action->acc_source.num_acc,
                 action->acc_source.stage_count);
  PRT_PROGRESS_LOG("action=%u segment=%u alloc-acc done unique_gemmini=%u unique_dma=%u",
                   action->action_id, action->segment_idx,
                   action->acc_source.all_count, action->acc_source.num_acc);
  return PRT_OK;
}

int prt_action_alloc_spm(prt_runtime_t *rt, prt_schedule_action_t *action) {
  const prt_segment_desc_t *seg;
  alias_group_plan_t *alias_groups = NULL;
  uint32_t alias_group_count = 0;
  uint32_t alloc_key_cursor = 0x71000000U;
  uint32_t page_count = 0;
  int rc = PRT_OK;
  if (!rt || !action) return PRT_ERR_INVAL;
  if (action->state != PRT_ACTION_CREATED) return PRT_ERR_STATE;
  seg = action->pipeline_segment_ref;
  if (!seg) return PRT_ERR_INVAL;

  free_spm_binding_array(action->spm_source.weight_pages, action->spm_source.weight_count);
  free_spm_binding_array(action->spm_source.in_stage_pages, action->spm_source.in_stage_count);
  free_spm_binding_array(action->spm_source.ring_pages, action->spm_source.ring_count);
  action->spm_source.weight_pages = NULL;
  action->spm_source.weight_count = 0;
  action->spm_source.weight_cap = 0;
  action->spm_source.in_stage_pages = NULL;
  action->spm_source.in_stage_count = 0;
  action->spm_source.in_stage_cap = 0;
  action->spm_source.ring_pages = NULL;
  action->spm_source.ring_count = 0;
  action->spm_source.ring_cap = 0;
  action->spm_source.num_spm_pages = 0;

  page_count = seg->segment_spm_page_span;
  if (page_count == 0U) {
    for (uint32_t i = 0; i < seg->num_stages; ++i) page_count += seg->stages[i].local_spm_page_span;
  }
  action->alias_page_count = page_count;

  if (rt->cfg.spm_xlate_enable && page_count > 0U) {
    rc = alloc_action_alias_window(rt, action, page_count);
    if (rc != PRT_OK) goto out;
    rc = prt_spm_xlate_ctx_alloc(rt, &action->spm_xlate, page_count);
    if (rc != PRT_OK) goto out;
    // Hardware computes the PTE index from (vaddr - range_base), so each
    // action-local alias window must occupy vpage [0, page_count) inside the
    // active window. The old reserve_vpages path produced a software-only
    // global offset which the controller cannot observe.
    action->alias_vpage_start = 0U;
  }

  if (seg->buffer_binding_count > 0U) {
    alias_groups = (alias_group_plan_t *)calloc(seg->buffer_binding_count, sizeof(*alias_groups));
    if (!alias_groups) {
      rc = PRT_ERR_NOMEM;
      goto out;
    }
  }

  for (uint32_t i = 0; i < seg->buffer_binding_count; ++i) {
    const prt_buffer_binding_t *binding = &seg->buffer_bindings[i];
    uint32_t preferred_mgrs[PRT_MAX_CORES];
    uint32_t preferred_cnt = 0U;
    if (binding->kind == PRT_BUFFER_BINDING_UNKNOWN) {
      rc = PRT_ERR_PARSE;
      goto out;
    }
    if (binding->kind != PRT_BUFFER_BINDING_RING && binding->stage_local_id >= seg->num_stages) {
      rc = PRT_ERR_PARSE;
      goto out;
    }

    if (binding->kind == PRT_BUFFER_BINDING_RING || binding->alias_group_id != 0U) {
      preferred_cnt = action->acc_source.all_count;
      if (preferred_cnt > PRT_MAX_CORES) preferred_cnt = PRT_MAX_CORES;
      for (uint32_t k = 0; k < preferred_cnt; ++k) preferred_mgrs[k] = action->acc_source.all_gemmini_mgr_ids[k];
    } else {
      preferred_cnt = stage_preferred_mgrs(action, binding->stage_local_id, preferred_mgrs, PRT_MAX_CORES);
    }

    {
      char preferred_buf[96];
      format_u32_list(preferred_mgrs, preferred_cnt, preferred_buf, sizeof(preferred_buf));
      PRT_MARKER_LOG("action=%u segment=%u bind-plan buffer=%u tensor=%u kind=%s stage=%u entry=%u slots=%u pages_per_slot=%u alias_group=%u preferred_mgrs=%s",
                     action->action_id, action->segment_idx,
                     binding->buffer_id, binding->tensor_id,
                     buffer_binding_kind_name(binding->kind),
                     binding->stage_local_id, binding->is_entry,
                     binding->slot_count, binding->pages_per_slot,
                     binding->alias_group_id, preferred_buf);
    }

    if (binding->kind == PRT_BUFFER_BINDING_RING) {
      for (uint32_t slot = 0; slot < binding->slot_count; ++slot) {
        prt_page_list_t pages;
        if (binding->pages_per_slot == 0U) continue;
        rc = alloc_slot_pages_for_action(rt, action, &alloc_key_cursor, binding->pages_per_slot,
                                         preferred_mgrs, preferred_cnt, &pages);
        if (rc != PRT_OK) goto out;
        rc = append_spm_binding(&action->spm_source.ring_pages, &action->spm_source.ring_count,
                                &action->spm_source.ring_cap, binding->buffer_id,
                                binding->tensor_id, UINT32_MAX, slot, &pages);
        if (rc != PRT_OK) goto out;
        log_action_page_list(rt, action, "RING", binding->buffer_id, binding->tensor_id,
                             UINT32_MAX, slot, &pages);
      }
      continue;
    }

    if (binding->kind == PRT_BUFFER_BINDING_WEIGHT) {
      prt_page_list_t pages;
      if (binding->slot_count == 0U || binding->pages_per_slot == 0U) continue;
      rc = alloc_slot_pages_for_action(rt, action, &alloc_key_cursor, binding->pages_per_slot,
                                       preferred_mgrs, preferred_cnt, &pages);
      if (rc != PRT_OK) goto out;
      rc = append_spm_binding(&action->spm_source.weight_pages, &action->spm_source.weight_count,
                              &action->spm_source.weight_cap, binding->buffer_id,
                              binding->tensor_id, binding->stage_local_id, 0U, &pages);
      if (rc != PRT_OK) goto out;
      log_action_page_list(rt, action, "WEIGHT", binding->buffer_id, binding->tensor_id,
                           binding->stage_local_id, 0U, &pages);
      continue;
    }

    if (binding->kind == PRT_BUFFER_BINDING_PIPE) {
      alias_group_plan_t *alias_plan = NULL;
      if (binding->slot_count == 0U || binding->pages_per_slot == 0U) continue;
      if (binding->alias_group_id != 0U) {
        alias_plan = find_alias_group_plan(alias_groups, alias_group_count, binding->alias_group_id);
        if (!alias_plan) {
          if (binding->slot_count > 2U) {
            rc = PRT_ERR_NOT_IMPL;
            goto out;
          }
          alias_plan = &alias_groups[alias_group_count++];
          memset(alias_plan, 0, sizeof(*alias_plan));
          alias_plan->alias_group_id = binding->alias_group_id;
          alias_plan->slot_count = binding->slot_count;
          for (uint32_t slot = 0; slot < binding->slot_count; ++slot) {
            rc = alloc_slot_pages_for_action(rt, action, &alloc_key_cursor, binding->pages_per_slot,
                                             preferred_mgrs, preferred_cnt, &alias_plan->slot_pages[slot]);
            if (rc != PRT_OK) goto out;
          }
        } else if (alias_plan->slot_count < binding->slot_count) {
          rc = PRT_ERR_PARSE;
          goto out;
        }
      }
      for (uint32_t slot = 0; slot < binding->slot_count; ++slot) {
        prt_page_list_t pages;
        if (alias_plan) pages = alias_plan->slot_pages[slot];
        else {
          rc = alloc_slot_pages_for_action(rt, action, &alloc_key_cursor, binding->pages_per_slot,
                                           preferred_mgrs, preferred_cnt, &pages);
          if (rc != PRT_OK) goto out;
        }
        rc = append_spm_binding(&action->spm_source.in_stage_pages, &action->spm_source.in_stage_count,
                                &action->spm_source.in_stage_cap, binding->buffer_id,
                                binding->tensor_id, binding->stage_local_id,
                                slot, &pages);
        if (rc != PRT_OK) goto out;
        log_action_page_list(rt, action, "PIPE", binding->buffer_id, binding->tensor_id,
                             binding->stage_local_id, slot, &pages);
      }
      continue;
    }
  }

  action->spm_ptbr_pa = action->spm_xlate.ptbr_pa;
  action->spm_pte_count = action->spm_xlate.pte_count;
  action->spm_fault_count = action->spm_xlate.fault_count;
  action->spm_last_fault_vaddr = action->spm_xlate.last_fault_vaddr;
  action->spm_last_fault_cause = action->spm_xlate.last_fault_cause;
  PRT_MARKER_LOG(
    "action=%u segment=%u alloc-spm alias_base=0x%llx alias_bytes=%llu alias_vpage=%u pages=%u weights=%u pipes=%u rings=%u spm_pages=%u",
    action->action_id, action->segment_idx,
    (unsigned long long)action->alias_base_va,
    (unsigned long long)action->alias_bytes,
    action->alias_vpage_start, action->alias_page_count,
    action->spm_source.weight_count, action->spm_source.in_stage_count,
    action->spm_source.ring_count, action->spm_source.num_spm_pages);
  action->state = PRT_ACTION_ALLOCATED;
  free(alias_groups);
  return PRT_OK;

out:
  free(alias_groups);
  if (rt && action && action->alias_page_count > 0U && rt->cfg.spm_xlate_enable &&
      action->spm_xlate.pte_count > 0U) {
    (void)prt_spm_unbind_vpages_ctx(rt, &action->spm_xlate, 0U, action->alias_page_count);
  }
  prt_spm_xlate_ctx_release(rt, &action->spm_xlate);
  action->alias_vpage_start = 0U;
  action->alias_page_count = 0U;
  free_action_alias_window(action);
  free_spm_source(&action->spm_source);
  return rc;
}

int prt_action_track_alloc_key(prt_schedule_action_t *action, uint32_t key) {
  uint32_t i;
  uint32_t new_cap;
  uint32_t *tmp;
  if (!action) return PRT_ERR_INVAL;
  for (i = 0; i < action->spm_source.alloc_count; ++i) {
    if (action->spm_source.alloc_keys[i] == key) return PRT_OK;
  }
  if (action->spm_source.alloc_count < action->spm_source.alloc_cap) {
    action->spm_source.alloc_keys[action->spm_source.alloc_count++] = key;
    return PRT_OK;
  }
  new_cap = action->spm_source.alloc_cap ? (action->spm_source.alloc_cap << 1) : 64U;
  tmp = (uint32_t *)realloc(action->spm_source.alloc_keys, sizeof(uint32_t) * new_cap);
  if (!tmp) return PRT_ERR_NOMEM;
  action->spm_source.alloc_keys = tmp;
  action->spm_source.alloc_cap = new_cap;
  action->spm_source.alloc_keys[action->spm_source.alloc_count++] = key;
  return PRT_OK;
}

int prt_action_bind_topology(prt_runtime_t *rt, prt_schedule_action_t *action) {
  prt_action_exec_t *exec;
  const prt_segment_desc_t *seg;
  uint32_t i;
  int rc;
  if (!rt || !action) return PRT_ERR_INVAL;
  if (action->state != PRT_ACTION_ALLOCATED) return PRT_ERR_STATE;
  if (prt_action_exec_ensure(action) != PRT_OK) return PRT_ERR_NOMEM;
  exec = action->exec;
  if (!exec) return PRT_ERR_STATE;
  if (action->acc_source.stage_count != exec->stage_thread_count) return PRT_ERR_STATE;
  seg = action->pipeline_segment_ref;
  if (!seg) return PRT_ERR_INVAL;

  for (i = 0; i < exec->stage_thread_count; ++i) {
    const prt_stage_acc_assign_t *assign = &action->acc_source.stage_assign[i];
    if (assign->acc_util == 0 || !assign->gemmini_mgr_ids || !assign->dma_mgr_ids) {
      return PRT_ERR_STATE;
    }
    exec->stage_acc_ids[i] = assign->gemmini_mgr_ids[0];
    exec->stage_tile_counts[i] = assign->acc_util;
    exec->stage_dma_ids[i] = assign->dma_mgr_ids[0];
    memset(exec->stage_mgr_ids[i], 0, sizeof(exec->stage_mgr_ids[i]));
    for (uint32_t k = 0; k < assign->acc_util && k < PRT_MAX_CORES; ++k) {
      exec->stage_mgr_ids[i][k] = assign->gemmini_mgr_ids[k];
    }
    {
      char gm_buf[96];
      char dm_buf[96];
      format_u32_list(assign->gemmini_mgr_ids, assign->acc_util, gm_buf, sizeof(gm_buf));
      format_u32_list(assign->dma_mgr_ids, assign->acc_util, dm_buf, sizeof(dm_buf));
      PRT_MARKER_LOG("action=%u segment=%u bind-stage stage=%u layer=%u split=%u acc=%u gemmini=%s dma=%s",
                     action->action_id, action->segment_idx,
                     i, seg->stages[i].layer_id, (uint32_t)seg->stages[i].split_kind,
                     assign->acc_util, gm_buf, dm_buf);
    }
  }

  for (i = 0; i < exec->pipebuf_count; ++i) {
    prt_pipebuf_t *b = &exec->pipebufs[i];
    if (b->stage_idx >= exec->stage_thread_count) return PRT_ERR_STATE;
    b->cmd_acc[0] = exec->stage_dma_ids[b->stage_idx];
    b->cmd_acc[1] = exec->stage_dma_ids[b->stage_idx];
  }

  clear_exec_weight_bindings(exec);
  clear_exec_pipebuf_pages(exec);
  clear_exec_ringbuf_pages(exec);

  for (i = 0; i < exec->ringbuf_count; ++i) {
    prt_ringbuf_t *rb = &exec->ringbufs[i];
    const prt_buffer_binding_t *binding_meta = find_segment_buffer_binding(seg, rb->buffer_id);
    uint32_t slots = rb->size;
    if (binding_meta) {
      if (binding_meta->kind != PRT_BUFFER_BINDING_RING) return PRT_ERR_PARSE;
      if (binding_meta->slot_count > 0U && binding_meta->slot_count != rb->size) return PRT_ERR_STATE;
      if (binding_meta->slot_count > 0U) slots = binding_meta->slot_count;
    }
    for (uint32_t slot = 0; slot < slots; ++slot) {
      const prt_spm_page_binding_t *binding =
        find_action_binding(action->spm_source.ring_pages, action->spm_source.ring_count,
                            rb->buffer_id, slot);
      if (!binding) {
        if (binding_meta && binding_meta->pages_per_slot > 0U) return PRT_ERR_STATE;
        continue;
      }
      rc = clone_into_runtime_page_list(&rb->slot_pages[slot], &binding->pages);
      if (rc != PRT_OK) return rc;
    }
  }

  for (i = 0; i < exec->pipebuf_count; ++i) {
    prt_pipebuf_t *b = &exec->pipebufs[i];
    const prt_buffer_binding_t *binding_meta = find_segment_buffer_binding(seg, b->buffer_id);
    uint32_t slots;
    if (!binding_meta) return PRT_ERR_PARSE;
    if (binding_meta->kind != PRT_BUFFER_BINDING_PIPE) return PRT_ERR_PARSE;
    slots = b->with_double_buffer ? 2U : 1U;
    if (binding_meta->slot_count > 0U && binding_meta->slot_count < slots) return PRT_ERR_STATE;

    for (uint32_t slot = 0; slot < slots; ++slot) {
      const prt_spm_page_binding_t *binding =
        find_action_binding(action->spm_source.in_stage_pages, action->spm_source.in_stage_count,
                            b->buffer_id, slot);
      if (!binding) {
        if (binding_meta->pages_per_slot > 0U) return PRT_ERR_STATE;
        continue;
      }
      rc = clone_into_runtime_page_list(&b->slot_pages[slot], &binding->pages);
      if (rc != PRT_OK) return rc;
    }
  }

  for (i = 0; i < action->spm_source.weight_count; ++i) {
    rc = append_runtime_weight_binding(exec, &action->spm_source.weight_pages[i]);
    if (rc != PRT_OK) return rc;
  }

  rc = runtime_shared_aliasing_valid(exec);
  if (rc != PRT_OK) return rc;

  rc = configure_action_spm_xlate(rt, action);
  if (rc != PRT_OK) return rc;

  action->spm_ptbr_pa = action->spm_xlate.ptbr_pa;
  action->spm_pte_count = action->spm_xlate.pte_count;
  action->spm_fault_count = action->spm_xlate.fault_count;
  action->spm_last_fault_vaddr = action->spm_xlate.last_fault_vaddr;
  action->spm_last_fault_cause = action->spm_xlate.last_fault_cause;

  action->state = PRT_ACTION_RUNNING;
  PRT_MARKER_LOG("action=%u segment=%u bind-topology weights=%u pipes=%u rings=%u alias_vpage=%u alias_pages=%u",
                 action->action_id, action->segment_idx,
                 action->spm_source.weight_count, exec->pipebuf_count, exec->ringbuf_count,
                 action->alias_vpage_start, action->alias_page_count);
  return PRT_OK;
}

int prt_action_release(prt_runtime_t *rt, prt_schedule_action_t **action_ptr) {
  prt_schedule_action_t *action;
  if (!action_ptr || !*action_ptr) return PRT_OK;
  action = *action_ptr;

  if (rt) {
    if (rt->cfg.spm_xlate_enable && action->alias_page_count > 0U &&
        action->spm_xlate.pte_count > 0U) {
      PRT_MARKER_LOG("action=%u segment=%u release alias_vpage=%u pages=%u alloc_keys=%u",
                     action->action_id, action->segment_idx,
                     action->alias_vpage_start, action->alias_page_count,
                     action->spm_source.alloc_count);
      (void)disable_action_spm_xlate(rt, action);
      (void)prt_spm_unbind_vpages_ctx(rt, &action->spm_xlate, 0U, action->alias_page_count);
    }
    for (uint32_t i = 0; i < action->spm_source.alloc_count; ++i) {
      (void)prt_release_tensor_pages(rt, action->spm_source.alloc_keys[i]);
    }
  }

  prt_spm_xlate_ctx_release(rt, &action->spm_xlate);
  action->alias_vpage_start = 0U;
  action->alias_page_count = 0U;
  free_action_alias_window(action);
  prt_action_exec_destroy(rt, action);
  free_acc_source(&action->acc_source);
  free_spm_source(&action->spm_source);
  action->state = PRT_ACTION_RELEASED;
  free(action);
  *action_ptr = NULL;
  return PRT_OK;
}

int prt_action_to_json(const prt_schedule_action_t *action, char *buf, size_t buf_size) {
  if (!action || !buf || buf_size == 0) return PRT_ERR_INVAL;
  (void)snprintf(
    buf, buf_size,
    "{\"action_id\":%u,\"segment_idx\":%u,\"state\":%u,\"stage_count\":%u,"
    "\"num_acc\":%u,\"spm_pages\":%u,\"alloc_keys\":%u,"
    "\"alias_base_va\":%llu,\"alias_bytes\":%llu,\"alias_vpage_start\":%u,\"alias_page_count\":%u,"
    "\"in_stage_views\":%u,\"ring_views\":%u,\"weight_views\":%u,"
    "\"spm_ptbr_pa\":%llu,\"spm_pte_count\":%u,"
    "\"spm_fault_count\":%llu,\"spm_fault_vaddr\":%llu,\"spm_fault_cause\":%u}",
    action->action_id, action->segment_idx, (uint32_t)action->state,
    action->acc_source.stage_count, action->acc_source.num_acc,
    action->spm_source.num_spm_pages, action->spm_source.alloc_count,
    (unsigned long long)action->alias_base_va,
    (unsigned long long)action->alias_bytes,
    action->alias_vpage_start, action->alias_page_count,
    action->spm_source.in_stage_count, action->spm_source.ring_count,
    action->spm_source.weight_count,
    (unsigned long long)action->spm_ptbr_pa, action->spm_pte_count,
    (unsigned long long)action->spm_fault_count,
    (unsigned long long)action->spm_last_fault_vaddr,
    action->spm_last_fault_cause);
  return PRT_OK;
}
