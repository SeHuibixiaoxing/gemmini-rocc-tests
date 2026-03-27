#include "prt_runtime.h"

#include <stdlib.h>
#include <string.h>

#include "prt_dma.h"

static void free_page_list_storage(prt_page_list_t *pl) {
  if (!pl) return;
  free(pl->data);
  pl->data = NULL;
  pl->size = 0;
  pl->cap = 0;
}

static void pipebuf_destroy_local(prt_pipebuf_t *b) {
  if (!b) return;
  for (int i = 0; i < 2; ++i) {
    if (b->dma_token_live[i]) {
      (void)prt_dma_token_cleanup(&b->dma_tokens[i]);
      b->dma_token_live[i] = 0;
    }
    free_page_list_storage(&b->slot_pages[i]);
  }
  pthread_cond_destroy(&b->cv);
  pthread_mutex_destroy(&b->lock);
}

static void ringbuf_destroy_local(prt_ringbuf_t *rb) {
  if (!rb) return;
  if (rb->slot_pages) {
    for (uint32_t i = 0; i < rb->size; ++i) {
      free_page_list_storage(&rb->slot_pages[i]);
    }
    free(rb->slot_pages);
  }
  free(rb->use_count.data);
  rb->slot_pages = NULL;
  rb->use_count.data = NULL;
  rb->use_count.size = 0;
  rb->use_count.cap = 0;
  pthread_cond_destroy(&rb->cv);
  pthread_mutex_destroy(&rb->lock);
}

prt_action_exec_t *prt_runtime_current_exec(prt_runtime_t *rt) {
  prt_schedule_action_t *action = prt_runtime_current_action(rt);
  return action ? action->exec : NULL;
}

const prt_action_exec_t *prt_runtime_current_exec_const(const prt_runtime_t *rt) {
  const prt_schedule_action_t *action = prt_runtime_current_action(rt);
  return action ? action->exec : NULL;
}

int prt_action_exec_ensure(prt_schedule_action_t *action) {
  if (!action) return PRT_ERR_INVAL;
  if (action->exec) return PRT_OK;
  action->exec = (prt_action_exec_t *)calloc(1, sizeof(*action->exec));
  if (!action->exec) return PRT_ERR_NOMEM;
  return PRT_OK;
}

void prt_action_exec_destroy(prt_runtime_t *rt, prt_schedule_action_t *action) {
  prt_action_exec_t *exec;
  (void)rt;
  if (!action || !action->exec) return;
  exec = action->exec;

  for (uint32_t i = 0; i < exec->topo_weight_count; ++i) {
    free_page_list_storage(&exec->topo_weight_pages[i].pages);
  }
  free(exec->topo_weight_pages);
  exec->topo_weight_pages = NULL;
  exec->topo_weight_count = 0;
  exec->topo_weight_cap = 0;

  for (uint32_t i = 0; i < exec->pipebuf_count; ++i) {
    pipebuf_destroy_local(&exec->pipebufs[i]);
  }
  free(exec->pipebufs);
  exec->pipebufs = NULL;
  exec->pipebuf_count = 0;

  for (uint32_t i = 0; i < exec->ringbuf_count; ++i) {
    ringbuf_destroy_local(&exec->ringbufs[i]);
  }
  free(exec->ringbufs);
  exec->ringbufs = NULL;
  exec->ringbuf_count = 0;

  free(exec->isolate_pairs);
  free(exec->shared_pairs);
  exec->isolate_pairs = NULL;
  exec->shared_pairs = NULL;
  exec->isolate_pair_count = 0;
  exec->shared_pair_count = 0;

  free(exec->topo_alloc_keys);
  exec->topo_alloc_keys = NULL;
  exec->topo_alloc_count = 0;
  exec->topo_alloc_cap = 0;

  for (uint32_t i = 0; i < PRT_MAX_STAGES; ++i) {
    free(exec->stage_spm_shadow[i]);
    exec->stage_spm_shadow[i] = NULL;
    exec->stage_spm_shadow_bytes[i] = 0;
    free(exec->stage_dma_bounce[i]);
    exec->stage_dma_bounce[i] = NULL;
    exec->stage_dma_bounce_bytes[i] = 0;
    exec->stage_spm_rebase_vpage[i] = 0;
    exec->stage_spm_window_pages[i] = 0;
    memset(exec->stage_fixed_lazy_loaded[i], 0, sizeof(exec->stage_fixed_lazy_loaded[i]));
  }
  exec->stage_thread_count = 0;

  free(exec);
  action->exec = NULL;
}
