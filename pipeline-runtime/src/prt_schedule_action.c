#include "prt_schedule_action.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "prt_error.h"
#include "prt_page_table.h"
#include "prt_runtime.h"

static uint32_t g_next_action_id = 1;

static int is_valid_tile_count(uint32_t v) {
  return v == 1U || v == 2U || v == 4U || v == 8U || v == 16U || v == 32U;
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
                              uint32_t tensor_id, uint32_t stage_id, uint32_t slot_id,
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
  (*arr)[*n].tensor_id = tensor_id;
  (*arr)[*n].stage_id = stage_id;
  (*arr)[*n].slot_id = slot_id;
  rc = clone_page_list(&(*arr)[*n].pages, pages);
  if (rc != PRT_OK) return rc;
  *n += 1U;
  return PRT_OK;
}

static int collect_spm_views(prt_runtime_t *rt, prt_schedule_action_t *action) {
  int rc;
  prt_spm_source_t *spm;
  if (!rt || !action) return PRT_ERR_INVAL;
  spm = &action->spm_source;

  free_spm_binding_array(spm->weight_pages, spm->weight_count);
  free_spm_binding_array(spm->in_stage_pages, spm->in_stage_count);
  free_spm_binding_array(spm->ring_pages, spm->ring_count);
  spm->weight_pages = NULL;
  spm->weight_count = 0;
  spm->weight_cap = 0;
  spm->in_stage_pages = NULL;
  spm->in_stage_count = 0;
  spm->in_stage_cap = 0;
  spm->ring_pages = NULL;
  spm->ring_count = 0;
  spm->ring_cap = 0;
  spm->num_spm_pages = 0;

  for (uint32_t i = 0; i < rt->pipebuf_count; ++i) {
    const prt_pipebuf_t *b = &rt->pipebufs[i];
    uint32_t slots = b->with_double_buffer ? 2U : 1U;
    for (uint32_t slot = 0; slot < slots; ++slot) {
      const prt_page_list_t *pl = &b->slot_pages[slot];
      if (!pl->data || pl->size == 0) continue;
      rc = append_spm_binding(&spm->in_stage_pages, &spm->in_stage_count, &spm->in_stage_cap,
                              b->tensor_id, b->stage_idx, slot, pl);
      if (rc != PRT_OK) return rc;
      spm->num_spm_pages += pl->size;
    }
  }

  for (uint32_t i = 0; i < rt->ringbuf_count; ++i) {
    const prt_ringbuf_t *rb = &rt->ringbufs[i];
    for (uint32_t slot = 0; slot < rb->size; ++slot) {
      const prt_page_list_t *pl = &rb->slot_pages[slot];
      if (!pl->data || pl->size == 0) continue;
      rc = append_spm_binding(&spm->ring_pages, &spm->ring_count, &spm->ring_cap,
                              rb->tensor_id, UINT32_MAX, slot, pl);
      if (rc != PRT_OK) return rc;
      spm->num_spm_pages += pl->size;
    }
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
  a->state = PRT_ACTION_CREATED;
  *out_action = a;
  return PRT_OK;
}

int prt_action_alloc_acc(prt_runtime_t *rt, prt_schedule_action_t *action) {
  uint32_t i;
  uint32_t rr_cursor = 0;
  uint32_t all_g_cap = 0;
  uint32_t all_d_cap = 0;
  const prt_segment_desc_t *seg;
  if (!rt || !action || !action->pipeline_segment_ref) return PRT_ERR_INVAL;
  if (action->state != PRT_ACTION_CREATED) return PRT_ERR_STATE;

  seg = action->pipeline_segment_ref;
  action->acc_source.stage_count = seg->num_stages;
  action->acc_source.stage_assign =
    (prt_stage_acc_assign_t *)calloc(seg->num_stages, sizeof(prt_stage_acc_assign_t));
  if (!action->acc_source.stage_assign) return PRT_ERR_NOMEM;

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
    if (rt->cfg.num_dma_mgrs < rt->cfg.num_gemmini_mgrs) {
      fprintf(stderr, "action_alloc_acc: num_dma=%u less than num_gemmini=%u\n",
              rt->cfg.num_dma_mgrs, rt->cfg.num_gemmini_mgrs);
      return PRT_ERR_NOT_READY;
    }

    assign->stage_id = i;
    assign->acc_util = stage->acc_util;
    assign->gemmini_mgr_ids = (uint32_t *)calloc(assign->acc_util, sizeof(uint32_t));
    assign->dma_mgr_ids = (uint32_t *)calloc(assign->acc_util, sizeof(uint32_t));
    if (!assign->gemmini_mgr_ids || !assign->dma_mgr_ids) return PRT_ERR_NOMEM;

    for (k = 0; k < assign->acc_util; ++k) {
      uint32_t gm_local = (rr_cursor + k) % rt->cfg.num_gemmini_mgrs;
      uint32_t dm_local = gm_local;
      uint32_t gm_id = rt->cfg.gemmini_mgr_base_id + gm_local;
      uint32_t dm_id = rt->cfg.dma_mgr_base_id + dm_local;
      int rc;
      assign->gemmini_mgr_ids[k] = gm_id;
      assign->dma_mgr_ids[k] = dm_id;
      rc = append_unique_mgr(&action->acc_source.all_gemmini_mgr_ids,
                             &action->acc_source.all_count, &all_g_cap, gm_id);
      if (rc != PRT_OK) return rc;
      rc = append_unique_mgr(&action->acc_source.all_dma_mgr_ids,
                             &action->acc_source.num_acc, &all_d_cap, dm_id);
      if (rc != PRT_OK) return rc;
    }
    rr_cursor = (rr_cursor + assign->acc_util) % rt->cfg.num_gemmini_mgrs;
  }

  if (action->acc_source.num_acc < action->acc_source.all_count) {
    action->acc_source.num_acc = action->acc_source.all_count;
  }
  return PRT_OK;
}

int prt_action_alloc_spm(prt_runtime_t *rt, prt_schedule_action_t *action) {
  if (!rt || !action) return PRT_ERR_INVAL;
  if (action->state != PRT_ACTION_CREATED) return PRT_ERR_STATE;
  action->spm_ptbr_pa = prt_spm_ptbr_pa(rt);
  action->spm_pte_count = prt_spm_pte_count(rt);
  action->spm_fault_count = prt_spm_fault_count(rt);
  action->spm_last_fault_vaddr = prt_spm_last_fault_vaddr(rt);
  action->spm_last_fault_cause = prt_spm_last_fault_cause(rt);
  action->state = PRT_ACTION_ALLOCATED;
  return PRT_OK;
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
  uint32_t i;
  int rc;
  if (!rt || !action) return PRT_ERR_INVAL;
  if (action->state != PRT_ACTION_ALLOCATED) return PRT_ERR_STATE;
  if (action->acc_source.stage_count != rt->stage_thread_count) return PRT_ERR_STATE;

  for (i = 0; i < rt->stage_thread_count; ++i) {
    const prt_stage_acc_assign_t *assign = &action->acc_source.stage_assign[i];
    if (assign->acc_util == 0 || !assign->gemmini_mgr_ids || !assign->dma_mgr_ids) {
      return PRT_ERR_STATE;
    }
    rt->stage_acc_ids[i] = assign->gemmini_mgr_ids[0];
    rt->stage_tile_counts[i] = assign->acc_util;
    rt->stage_dma_ids[i] = assign->dma_mgr_ids[0];
    memset(rt->stage_mgr_ids[i], 0, sizeof(rt->stage_mgr_ids[i]));
    for (uint32_t k = 0; k < assign->acc_util && k < PRT_MAX_CORES; ++k) {
      rt->stage_mgr_ids[i][k] = assign->gemmini_mgr_ids[k];
    }
  }

  for (i = 0; i < rt->pipebuf_count; ++i) {
    prt_pipebuf_t *b = &rt->pipebufs[i];
    if (b->stage_idx >= rt->stage_thread_count) return PRT_ERR_STATE;
    b->cmd_acc[0] = rt->stage_dma_ids[b->stage_idx];
    b->cmd_acc[1] = rt->stage_dma_ids[b->stage_idx];
  }

  rc = collect_spm_views(rt, action);
  if (rc != PRT_OK) return rc;

  action->spm_fault_count = prt_spm_fault_count(rt);
  action->spm_last_fault_vaddr = prt_spm_last_fault_vaddr(rt);
  action->spm_last_fault_cause = prt_spm_last_fault_cause(rt);

  action->state = PRT_ACTION_RUNNING;
  return PRT_OK;
}

int prt_action_release(prt_runtime_t *rt, prt_schedule_action_t **action_ptr) {
  prt_schedule_action_t *action;
  if (!action_ptr || !*action_ptr) return PRT_OK;
  action = *action_ptr;

  if (rt) {
    for (uint32_t i = 0; i < action->spm_source.alloc_count; ++i) {
      (void)prt_release_tensor_pages(rt, action->spm_source.alloc_keys[i]);
    }
  }

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
    "\"in_stage_views\":%u,\"ring_views\":%u,\"weight_views\":%u,"
    "\"spm_ptbr_pa\":%llu,\"spm_pte_count\":%u,"
    "\"spm_fault_count\":%llu,\"spm_fault_vaddr\":%llu,\"spm_fault_cause\":%u}",
    action->action_id, action->segment_idx, (uint32_t)action->state,
    action->acc_source.stage_count, action->acc_source.num_acc,
    action->spm_source.num_spm_pages, action->spm_source.alloc_count,
    action->spm_source.in_stage_count, action->spm_source.ring_count,
    action->spm_source.weight_count,
    (unsigned long long)action->spm_ptbr_pa, action->spm_pte_count,
    (unsigned long long)action->spm_fault_count,
    (unsigned long long)action->spm_last_fault_vaddr,
    action->spm_last_fault_cause);
  return PRT_OK;
}
