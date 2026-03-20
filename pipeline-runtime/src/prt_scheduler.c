#include "prt_scheduler.h"

#include <pthread.h>
#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "prt_dma.h"
#include "prt_page_table.h"
#include "prt_runtime.h"

static void build_abs_timeout(uint64_t timeout_ns, struct timespec *out) {
  struct timespec ts;
  uint64_t nsec;
  clock_gettime(CLOCK_REALTIME, &ts);
  nsec = (uint64_t)ts.tv_nsec + timeout_ns;
  out->tv_sec = ts.tv_sec + (time_t)(nsec / 1000000000ULL);
  out->tv_nsec = (long)(nsec % 1000000000ULL);
}

static int cond_wait_pred(pthread_mutex_t *lock, pthread_cond_t *cv, int (*pred)(void *), void *arg, uint64_t timeout_ns) {
  int rc = 0;
  while (!pred(arg)) {
    if (timeout_ns == 0) {
      rc = pthread_cond_wait(cv, lock);
      if (rc != 0) return PRT_ERR_STATE;
    } else {
      struct timespec abs;
      build_abs_timeout(timeout_ns, &abs);
      rc = pthread_cond_timedwait(cv, lock, &abs);
      if (rc == ETIMEDOUT) return PRT_ERR_TIMEOUT;
      if (rc != 0) return PRT_ERR_STATE;
    }
  }
  return PRT_OK;
}

typedef struct {
  prt_pipebuf_t *buf;
  uint32_t idx;
} buf_idx_arg_t;

static int pred_buf_full(void *arg) {
  buf_idx_arg_t *a = (buf_idx_arg_t *)arg;
  return a->buf->full[a->idx] != 0;
}

static int pred_buf_empty(void *arg) {
  buf_idx_arg_t *a = (buf_idx_arg_t *)arg;
  return a->buf->full[a->idx] == 0;
}

typedef struct {
  prt_ringbuf_t *rb;
  uint32_t offset;
} ring_arg_t;

static int pred_ring_ready(void *arg) {
  ring_arg_t *a = (ring_arg_t *)arg;
  return (a->rb->head <= a->offset) && (a->offset < a->rb->tail);
}

static int pred_ring_idle(void *arg) {
  ring_arg_t *a = (ring_arg_t *)arg;
  return (a->rb->tail - a->rb->head) < a->rb->size;
}

static int ring_use_locked(prt_ringbuf_t *rb, uint32_t subbatch_offset) {
  for (uint32_t i = 0; i < rb->use_count.size; ++i) {
    if (rb->use_count.data[i].key == subbatch_offset) {
      if (rb->use_count.data[i].value == 0) return PRT_ERR_STATE;
      rb->use_count.data[i].value -= 1;
      while (rb->head == subbatch_offset && rb->use_count.data[i].value == 0) {
        rb->head += 1;
        pthread_cond_broadcast(&rb->cv);
        break;
      }
      return PRT_OK;
    }
  }
  return PRT_ERR_INVAL;
}

static int ring_fill_locked(prt_ringbuf_t *rb, uint32_t subbatch_offset) {
  if (subbatch_offset != rb->tail) return PRT_ERR_STATE;
  rb->tail += 1;

  if (rb->use_count.size == rb->use_count.cap) {
    uint32_t new_cap = rb->use_count.cap ? (rb->use_count.cap << 1) : 16;
    prt_u32_kv_t *p = (prt_u32_kv_t *)realloc(rb->use_count.data, sizeof(prt_u32_kv_t) * new_cap);
    if (!p) return PRT_ERR_NOMEM;
    rb->use_count.data = p;
    rb->use_count.cap = new_cap;
  }

  rb->use_count.data[rb->use_count.size].key = subbatch_offset;
  rb->use_count.data[rb->use_count.size].value = rb->out_degree;
  rb->use_count.size += 1;
  pthread_cond_broadcast(&rb->cv);
  return PRT_OK;
}

static uint64_t pages_bytes_rt(const prt_runtime_t *rt, const prt_page_list_t *pl) {
  uint64_t page_bytes;
  if (!pl || !rt) return 0;
  page_bytes = rt->cfg.page_size_bytes ? rt->cfg.page_size_bytes : PRT_PAGE_SIZE_BYTES;
  return (uint64_t)pl->size * page_bytes;
}

static uint64_t pages_addr_base_rt(const prt_runtime_t *rt, const prt_page_list_t *pl) {
  uint64_t page_bytes;
  if (!rt || !pl || pl->size == 0 || !pl->data) return 0;
  page_bytes = rt->cfg.page_size_bytes ? rt->cfg.page_size_bytes : PRT_PAGE_SIZE_BYTES;
  return PRT_SHARED_SPAD_GLOBAL_ADDR_BASE +
         (uint64_t)pl->data[0].ppn * page_bytes;
}

static int page_list_is_contiguous(const prt_page_list_t *pl) {
  if (!pl || !pl->data || pl->size <= 1) return 1;
  for (uint32_t i = 1; i < pl->size; ++i) {
    if (pl->data[i].ppn != pl->data[i - 1].ppn + 1U) return 0;
  }
  return 1;
}

static int can_submit_overlap_single_req(const prt_runtime_t *rt, const prt_pipebuf_t *buf,
                                         uint32_t idx, int to_ring) {
  const prt_page_list_t *src;
  const prt_page_list_t *dst;
  if (!rt || !buf || idx > 1U) return 0;
  if (!to_ring) return 0;
  src = &buf->slot_pages[idx];
  if (!page_list_is_contiguous(src)) return 0;
  if (to_ring) {
    if (!buf->ring || buf->ring->size == 0) return 0;
    dst = &buf->ring->slot_pages[buf->subbatch_offset % buf->ring->size];
    if (!page_list_is_contiguous(dst)) return 0;
  }
  return 1;
}

static int page_list_copy(prt_page_list_t *dst, const prt_page_list_t *src) {
  if (!dst || !src) return PRT_ERR_INVAL;
  if (src->size == 0) {
    free(dst->data);
    dst->data = NULL;
    dst->size = 0;
    dst->cap = 0;
    return PRT_OK;
  }

  if (dst->cap < src->size) {
    prt_page_t *tmp = (prt_page_t *)realloc(dst->data, sizeof(prt_page_t) * src->size);
    if (!tmp) return PRT_ERR_NOMEM;
    dst->data = tmp;
    dst->cap = src->size;
  }
  memcpy(dst->data, src->data, sizeof(prt_page_t) * src->size);
  dst->size = src->size;
  return PRT_OK;
}

static int export_overlap_enabled(const prt_runtime_t *rt) {
  if (!rt) return 0;
  return rt->cfg.dma_backend == PRT_DMA_BACKEND_POLL_PROGRESS_THREAD;
}

static uint32_t export_next_submit_sbatch_locked(const prt_pipebuf_t *buf) {
  uint32_t next;
  if (!buf) return 0;
  next = buf->subbatch_offset;
  for (uint32_t i = 0; i < 2; ++i) {
    if (!buf->dma_token_live[i]) continue;
    if (buf->dma_submit_sbatch[i] >= next) next = buf->dma_submit_sbatch[i] + 1U;
  }
  return next;
}

int prt_pipebuf_wait_full(prt_pipebuf_t *buf, uint32_t idx, uint64_t timeout_ns) {
  buf_idx_arg_t arg;
  if (!buf || idx > 1) return PRT_ERR_INVAL;
  arg.buf = buf;
  arg.idx = idx;
  pthread_mutex_lock(&buf->lock);
  int rc = cond_wait_pred(&buf->lock, &buf->cv, pred_buf_full, &arg, timeout_ns);
  pthread_mutex_unlock(&buf->lock);
  return rc;
}

int prt_pipebuf_wait_empty(prt_pipebuf_t *buf, uint32_t idx, uint64_t timeout_ns) {
  buf_idx_arg_t arg;
  if (!buf || idx > 1) return PRT_ERR_INVAL;
  arg.buf = buf;
  arg.idx = idx;
  pthread_mutex_lock(&buf->lock);
  int rc = cond_wait_pred(&buf->lock, &buf->cv, pred_buf_empty, &arg, timeout_ns);
  pthread_mutex_unlock(&buf->lock);
  return rc;
}

int prt_ring_wait_ready(prt_ringbuf_t *rb, uint32_t offset, uint64_t timeout_ns) {
  ring_arg_t arg;
  if (!rb) return PRT_ERR_INVAL;
  arg.rb = rb;
  arg.offset = offset;
  pthread_mutex_lock(&rb->lock);
  int rc = cond_wait_pred(&rb->lock, &rb->cv, pred_ring_ready, &arg, timeout_ns);
  pthread_mutex_unlock(&rb->lock);
  return rc;
}

int prt_ring_wait_idle(prt_ringbuf_t *rb, uint64_t timeout_ns) {
  ring_arg_t arg;
  if (!rb) return PRT_ERR_INVAL;
  arg.rb = rb;
  arg.offset = 0;
  pthread_mutex_lock(&rb->lock);
  int rc = cond_wait_pred(&rb->lock, &rb->cv, pred_ring_idle, &arg, timeout_ns);
  pthread_mutex_unlock(&rb->lock);
  return rc;
}

int prt_process_c1(prt_runtime_t *rt, prt_pipebuf_t *buf, uint32_t idx, uint64_t timeout_ns) {
  int rc;
  if (!rt || !buf || idx > 1) return PRT_ERR_INVAL;

  if (buf->ring && buf->kind == PRT_BUF_C1_ENTRY_DRAM_OR_DEPEN && buf->dram_base_addr[idx] == 0) {
    int rc = prt_ring_wait_ready(buf->ring, buf->subbatch_offset, timeout_ns);
    if (rc != PRT_OK) return rc;
  }

  pthread_mutex_lock(&buf->lock);
  if (buf->full[idx]) {
    pthread_mutex_unlock(&buf->lock);
    return PRT_ERR_BUSY;
  }
  buf->cmd_running[idx] = 1;
  buf->cmd_count[idx] = 1;
  pthread_mutex_unlock(&buf->lock);

  if (buf->ring && buf->kind == PRT_BUF_C1_ENTRY_DRAM_OR_DEPEN && buf->dram_base_addr[idx] == 0) {
    rc = prt_dma_copy_spm_pages(rt, &buf->slot_pages[idx],
                                &buf->ring->slot_pages[buf->subbatch_offset % buf->ring->size],
                                buf->cmd_acc[idx], buf->stage_idx, buf->tensor_id, timeout_ns);
  } else {
    rc = prt_dma_copy_dram_to_spm_pages(rt, &buf->slot_pages[idx], buf->dram_base_addr[idx],
                                        buf->cmd_acc[idx], buf->stage_idx, buf->tensor_id, timeout_ns);
  }

  pthread_mutex_lock(&buf->lock);
  buf->cmd_count[idx] = 0;
  buf->cmd_running[idx] = 0;
  if (rc == PRT_OK) {
    buf->full[idx] = 1;
    if (buf->ring) {
      pthread_mutex_lock(&buf->ring->lock);
      (void)ring_use_locked(buf->ring, buf->subbatch_offset);
      pthread_mutex_unlock(&buf->ring->lock);
    }
    buf->subbatch_offset += 1;
    buf->state_epoch += 1;
    pthread_cond_broadcast(&buf->cv);
  }
  pthread_mutex_unlock(&buf->lock);

  return rc;
}

int prt_progress_export_dma(prt_runtime_t *rt, prt_pipebuf_t *buf, uint64_t timeout_ns, int nonblocking) {
  if (!rt || !buf) return PRT_ERR_INVAL;
  if (buf->kind != PRT_BUF_C2_EXPORT_DRAM_OR_DEPEN &&
      buf->kind != PRT_BUF_C6_EXPORT_ISOLATE_WITH_RING) {
    return PRT_OK;
  }

  while (1) {
    uint32_t expect;
    uint32_t target_idx = 2U;
    int has_live = 0;
    int rc;

    pthread_mutex_lock(&buf->lock);
    expect = buf->subbatch_offset;
    for (uint32_t i = 0; i < 2; ++i) {
      if (!buf->dma_token_live[i]) continue;
      has_live = 1;
      if (buf->dma_submit_sbatch[i] == expect && target_idx > 1U) target_idx = i;
    }
    pthread_mutex_unlock(&buf->lock);

    if (!has_live) return PRT_OK;
    if (target_idx > 1U) return PRT_ERR_STATE;

    if (nonblocking) rc = prt_dma_try_wait(rt, &buf->dma_tokens[target_idx]);
    else rc = prt_dma_wait(rt, &buf->dma_tokens[target_idx], timeout_ns);

    if (rc == PRT_ERR_NOT_READY) return PRT_OK;

    pthread_mutex_lock(&buf->lock);
    if (!buf->dma_token_live[target_idx]) {
      pthread_mutex_unlock(&buf->lock);
      continue;
    }

    if (rc != PRT_OK) {
      buf->cmd_running[target_idx] = 0;
      buf->cmd_count[target_idx] = 0;
      buf->dma_token_live[target_idx] = 0;
      (void)prt_dma_token_cleanup(&buf->dma_tokens[target_idx]);
      buf->state_epoch += 1;
      pthread_cond_broadcast(&buf->cv);
      pthread_mutex_unlock(&buf->lock);
      return rc;
    }

    if (buf->ring) {
      pthread_mutex_lock(&buf->ring->lock);
      rc = ring_fill_locked(buf->ring, buf->dma_submit_sbatch[target_idx]);
      pthread_mutex_unlock(&buf->ring->lock);
      if (rc != PRT_OK) {
        buf->cmd_running[target_idx] = 0;
        buf->cmd_count[target_idx] = 0;
        buf->dma_token_live[target_idx] = 0;
        (void)prt_dma_token_cleanup(&buf->dma_tokens[target_idx]);
        buf->state_epoch += 1;
        pthread_cond_broadcast(&buf->cv);
        pthread_mutex_unlock(&buf->lock);
        return rc;
      }
    }

    buf->full[target_idx] = 0;
    buf->cmd_running[target_idx] = 0;
    buf->cmd_count[target_idx] = 0;
    buf->dma_token_live[target_idx] = 0;
    (void)prt_dma_token_cleanup(&buf->dma_tokens[target_idx]);

    if (buf->subbatch_offset == expect) buf->subbatch_offset += 1;
    else if (buf->subbatch_offset < expect) buf->subbatch_offset = expect + 1U;

    prt_trace_on_export_retire(rt);
    prt_trace_log_event(rt, buf->stage_idx, PRT_TRACE_EVT_EXPORT_RETIRE,
                        buf->tensor_id, expect);
    buf->state_epoch += 1;
    pthread_cond_broadcast(&buf->cv);
    pthread_mutex_unlock(&buf->lock);

    if (nonblocking) return PRT_OK;
  }
}

int prt_process_c2(prt_runtime_t *rt, prt_pipebuf_t *buf, uint32_t idx, uint64_t timeout_ns) {
  prt_dma_req_t req;
  uint32_t submit_sbatch = 0;
  int to_ring = 0;
  int allow_overlap = 0;
  if (!rt || !buf || idx > 1) return PRT_ERR_INVAL;

  pthread_mutex_lock(&buf->lock);
  if (!buf->full[idx]) {
    pthread_mutex_unlock(&buf->lock);
    return PRT_ERR_EMPTY;
  }
  pthread_mutex_unlock(&buf->lock);

  if (buf->ring) {
    int rc = prt_ring_wait_idle(buf->ring, timeout_ns);
    if (rc != PRT_OK) return rc;
  }

  to_ring = (buf->ring && buf->kind == PRT_BUF_C2_EXPORT_DRAM_OR_DEPEN && buf->dram_base_addr[idx] == 0);
  allow_overlap = export_overlap_enabled(rt) && can_submit_overlap_single_req(rt, buf, idx, to_ring);

  if (allow_overlap) {
    int rc = prt_progress_export_dma(rt, buf, timeout_ns, 1);
    if (rc != PRT_OK) return rc;
  }

  pthread_mutex_lock(&buf->lock);
  submit_sbatch = buf->subbatch_offset;
  if (allow_overlap) {
    submit_sbatch = export_next_submit_sbatch_locked(buf);
    if (buf->dma_token_live[idx]) {
      pthread_mutex_unlock(&buf->lock);
      return PRT_OK;
    }
  }
  buf->cmd_running[idx] = 1;
  buf->cmd_count[idx] = 1;
  pthread_mutex_unlock(&buf->lock);

  req.dst_addr = buf->dram_base_addr[idx];
  if (buf->ring && buf->kind == PRT_BUF_C2_EXPORT_DRAM_OR_DEPEN && req.dst_addr == 0) {
    req.dst_addr = pages_addr_base_rt(rt, &buf->ring->slot_pages[submit_sbatch % buf->ring->size]);
  }
  req.src_addr = pages_addr_base_rt(rt, &buf->slot_pages[idx]);
  req.bytes = pages_bytes_rt(rt, &buf->slot_pages[idx]);
  req.src_acc = buf->cmd_acc[idx];
  req.dst_acc = buf->cmd_acc[idx];

  if (!allow_overlap) {
    int rc;
    if (to_ring) {
      rc = prt_dma_copy_spm_pages(rt,
                                  &buf->ring->slot_pages[submit_sbatch % buf->ring->size],
                                  &buf->slot_pages[idx],
                                  buf->cmd_acc[idx], buf->stage_idx, buf->tensor_id, timeout_ns);
    } else {
      rc = prt_dma_copy_spm_pages_to_dram(rt, buf->dram_base_addr[idx], &buf->slot_pages[idx],
                                          buf->cmd_acc[idx], buf->stage_idx, buf->tensor_id, timeout_ns);
    }

    pthread_mutex_lock(&buf->lock);
    buf->cmd_count[idx] = 0;
    buf->cmd_running[idx] = 0;
    if (rc == PRT_OK) {
      if (buf->ring) {
        pthread_mutex_lock(&buf->ring->lock);
        (void)ring_fill_locked(buf->ring, buf->subbatch_offset);
        pthread_mutex_unlock(&buf->ring->lock);
      }
      buf->full[idx] = 0;
      buf->subbatch_offset += 1;
      buf->state_epoch += 1;
      pthread_cond_broadcast(&buf->cv);
    }
    pthread_mutex_unlock(&buf->lock);
    return rc;
  }

  buf->dma_tokens[idx].stage_idx = buf->stage_idx;
  buf->dma_tokens[idx].tensor_id = buf->tensor_id;
  int rc = prt_dma_submit(rt, &req, &buf->dma_tokens[idx]);
  pthread_mutex_lock(&buf->lock);
  if (rc == PRT_OK) {
    buf->dma_token_live[idx] = 1;
    buf->dma_submit_sbatch[idx] = submit_sbatch;
    prt_trace_on_export_submit_ahead(rt);
    prt_trace_log_event(rt, buf->stage_idx, PRT_TRACE_EVT_EXPORT_SUBMIT_AHEAD,
                        buf->tensor_id, submit_sbatch);
  } else {
    buf->cmd_running[idx] = 0;
    buf->cmd_count[idx] = 0;
    (void)prt_dma_token_cleanup(&buf->dma_tokens[idx]);
    buf->state_epoch += 1;
    pthread_cond_broadcast(&buf->cv);
  }
  pthread_mutex_unlock(&buf->lock);
  return rc;
}

int prt_process_c3(prt_runtime_t *rt, prt_isolate_pair_t *pair, uint64_t timeout_ns) {
  uint32_t pre_idx;
  uint32_t nxt_idx;
  if (!rt || !pair || !pair->pre_export || !pair->nxt_entry || !pair->pre_idx || !pair->nxt_idx) return PRT_ERR_INVAL;

  pre_idx = *pair->pre_idx;
  nxt_idx = *pair->nxt_idx;

  if (prt_pipebuf_wait_full(pair->pre_export, pre_idx, timeout_ns) != PRT_OK) return PRT_ERR_TIMEOUT;
  if (prt_pipebuf_wait_empty(pair->nxt_entry, nxt_idx, timeout_ns) != PRT_OK) return PRT_ERR_TIMEOUT;

  pthread_mutex_lock(&pair->pre_export->lock);
  pthread_mutex_lock(&pair->nxt_entry->lock);
  if (pair->pre_export->subbatch_offset != pair->nxt_entry->subbatch_offset) {
    pthread_mutex_unlock(&pair->nxt_entry->lock);
    pthread_mutex_unlock(&pair->pre_export->lock);
    return PRT_ERR_NOT_READY;
  }

  pair->pre_export->cmd_running[pre_idx] = 1;
  pair->nxt_entry->cmd_running[nxt_idx] = 1;
  pair->pre_export->cmd_count[pre_idx] += 1;
  pair->nxt_entry->cmd_count[nxt_idx] += 1;
  pthread_mutex_unlock(&pair->nxt_entry->lock);
  pthread_mutex_unlock(&pair->pre_export->lock);

  int rc = prt_dma_copy_spm_pages(rt,
                                  &pair->nxt_entry->slot_pages[nxt_idx],
                                  &pair->pre_export->slot_pages[pre_idx],
                                  pair->pre_export->cmd_acc[pre_idx],
                                  pair->pre_export->stage_idx,
                                  pair->pre_export->tensor_id,
                                  timeout_ns);

  pthread_mutex_lock(&pair->pre_export->lock);
  pthread_mutex_lock(&pair->nxt_entry->lock);
  pair->pre_export->cmd_running[pre_idx] = 0;
  pair->nxt_entry->cmd_running[nxt_idx] = 0;
  if (pair->pre_export->cmd_count[pre_idx]) pair->pre_export->cmd_count[pre_idx] -= 1;
  if (pair->nxt_entry->cmd_count[nxt_idx]) pair->nxt_entry->cmd_count[nxt_idx] -= 1;

  if (rc == PRT_OK) {
    pair->nxt_entry->full[nxt_idx] = 1;
    pair->nxt_entry->subbatch_offset += 1;

    if (pair->pre_export->fanout_pending > 1) {
      pair->pre_export->fanout_pending -= 1;
    } else {
      pair->pre_export->fanout_pending = 0;
      pair->pre_export->full[pre_idx] = 0;
      pair->pre_export->subbatch_offset += 1;
      if (pair->pre_export->with_double_buffer) {
        *pair->pre_idx = (*pair->pre_idx + 1U) & 1U;
      }
    }

    if (pair->nxt_entry->with_double_buffer) {
      *pair->nxt_idx = (*pair->nxt_idx + 1U) & 1U;
    }

    pair->pre_export->state_epoch += 1;
    pair->nxt_entry->state_epoch += 1;
    pthread_cond_broadcast(&pair->pre_export->cv);
    pthread_cond_broadcast(&pair->nxt_entry->cv);
  }
  pthread_mutex_unlock(&pair->nxt_entry->lock);
  pthread_mutex_unlock(&pair->pre_export->lock);

  return rc;
}

int prt_process_c4(prt_runtime_t *rt, prt_shared_pair_t *pair) {
  prt_pipebuf_t *pre;
  prt_pipebuf_t *nxt_list[PRT_MAX_TENSORS];
  uint32_t nxt_count = 0;
  uint32_t idx;
  int any_full;
  int all_empty;

  if (!rt || !pair || !pair->pre_export || !pair->nxt_entry || !pair->tag) return PRT_ERR_INVAL;
  if (pair->buffer_idx > 1U) return PRT_ERR_INVAL;

  pre = pair->pre_export;
  idx = pair->buffer_idx;

  for (uint32_t i = 0; i < rt->shared_pair_count; ++i) {
    prt_shared_pair_t *p = &rt->shared_pairs[i];
    int seen = 0;
    if (p->pre_export != pre || p->buffer_idx != idx || !p->nxt_entry) continue;
    for (uint32_t j = 0; j < nxt_count; ++j) {
      if (nxt_list[j] == p->nxt_entry) {
        seen = 1;
        break;
      }
    }
    if (!seen && nxt_count < PRT_MAX_TENSORS) {
      nxt_list[nxt_count++] = p->nxt_entry;
    }
  }

  if (nxt_count == 0) return PRT_OK;

  pthread_mutex_lock(&pre->lock);
  for (uint32_t i = 0; i < nxt_count; ++i) pthread_mutex_lock(&nxt_list[i]->lock);

  any_full = pre->full[idx] != 0;
  all_empty = 1;
  for (uint32_t i = 0; i < nxt_count; ++i) {
    if (nxt_list[i]->full[idx]) {
      all_empty = 0;
      break;
    }
  }

  if (any_full && all_empty) {
    if (*pair->tag == 0) {
      for (uint32_t i = 0; i < nxt_count; ++i) nxt_list[i]->full[idx] = 1;
    } else {
      pre->full[idx] = 0;
    }
    *pair->tag = !(*pair->tag);
    pre->state_epoch += 1;
    pthread_cond_broadcast(&pre->cv);
    for (uint32_t i = 0; i < nxt_count; ++i) {
      nxt_list[i]->state_epoch += 1;
      pthread_cond_broadcast(&nxt_list[i]->cv);
    }
  }

  for (uint32_t i = nxt_count; i > 0; --i) pthread_mutex_unlock(&nxt_list[i - 1U]->lock);
  pthread_mutex_unlock(&pre->lock);
  return PRT_OK;
}

int prt_process_c5(prt_runtime_t *rt, prt_pipebuf_t *buf, uint32_t idx, uint64_t timeout_ns) {
  if (!rt || !buf || !buf->ring || idx > 1) return PRT_ERR_INVAL;
  int rc = prt_ring_wait_ready(buf->ring, buf->subbatch_offset, timeout_ns);
  if (rc != PRT_OK) return rc;

  rc = prt_dma_copy_spm_pages(rt,
                              &buf->slot_pages[idx],
                              &buf->ring->slot_pages[buf->subbatch_offset % buf->ring->size],
                              buf->cmd_acc[idx], buf->stage_idx, buf->tensor_id, timeout_ns);

  pthread_mutex_lock(&buf->lock);
  if (rc == PRT_OK) {
    pthread_mutex_lock(&buf->ring->lock);
    (void)ring_use_locked(buf->ring, buf->subbatch_offset);
    pthread_mutex_unlock(&buf->ring->lock);

    buf->full[idx] = 1;
    buf->subbatch_offset += 1;
    buf->state_epoch += 1;
    pthread_cond_broadcast(&buf->cv);
  }
  pthread_mutex_unlock(&buf->lock);
  return rc;
}

int prt_process_c6(prt_runtime_t *rt, prt_pipebuf_t *buf, uint32_t idx, uint64_t timeout_ns) {
  uint32_t submit_sbatch = 0;
  int allow_overlap = 0;
  if (!rt || !buf || !buf->ring || idx > 1) return PRT_ERR_INVAL;
  int rc = prt_ring_wait_idle(buf->ring, timeout_ns);
  if (rc != PRT_OK) return rc;

  pthread_mutex_lock(&buf->lock);
  if (!buf->full[idx]) {
    pthread_mutex_unlock(&buf->lock);
    return PRT_ERR_EMPTY;
  }
  pthread_mutex_unlock(&buf->lock);

  allow_overlap = export_overlap_enabled(rt) && can_submit_overlap_single_req(rt, buf, idx, 1);
  if (allow_overlap) {
    rc = prt_progress_export_dma(rt, buf, timeout_ns, 1);
    if (rc != PRT_OK) return rc;
  }

  pthread_mutex_lock(&buf->lock);
  submit_sbatch = buf->subbatch_offset;
  if (allow_overlap) {
    submit_sbatch = export_next_submit_sbatch_locked(buf);
    if (buf->dma_token_live[idx]) {
      pthread_mutex_unlock(&buf->lock);
      return PRT_OK;
    }
  }
  buf->cmd_running[idx] = 1;
  buf->cmd_count[idx] = 1;
  pthread_mutex_unlock(&buf->lock);

  prt_dma_req_t req;
  req.src_addr = pages_addr_base_rt(rt, &buf->slot_pages[idx]);
  req.dst_addr = pages_addr_base_rt(rt, &buf->ring->slot_pages[submit_sbatch % buf->ring->size]);
  req.bytes = pages_bytes_rt(rt, &buf->slot_pages[idx]);
  req.src_acc = buf->cmd_acc[idx];
  req.dst_acc = buf->cmd_acc[idx];

  if (!allow_overlap) {
    rc = prt_dma_copy_spm_pages(rt,
                                &buf->ring->slot_pages[submit_sbatch % buf->ring->size],
                                &buf->slot_pages[idx],
                                buf->cmd_acc[idx], buf->stage_idx, buf->tensor_id, timeout_ns);

    pthread_mutex_lock(&buf->lock);
    if (rc == PRT_OK) {
      pthread_mutex_lock(&buf->ring->lock);
      (void)ring_fill_locked(buf->ring, buf->subbatch_offset);
      pthread_mutex_unlock(&buf->ring->lock);

      buf->full[idx] = 0;
      buf->subbatch_offset += 1;
      buf->state_epoch += 1;
      pthread_cond_broadcast(&buf->cv);
    }
    buf->cmd_running[idx] = 0;
    buf->cmd_count[idx] = 0;
    pthread_mutex_unlock(&buf->lock);
    return rc;
  }

  buf->dma_tokens[idx].stage_idx = buf->stage_idx;
  buf->dma_tokens[idx].tensor_id = buf->tensor_id;
  rc = prt_dma_submit(rt, &req, &buf->dma_tokens[idx]);
  pthread_mutex_lock(&buf->lock);
  if (rc == PRT_OK) {
    buf->dma_token_live[idx] = 1;
    buf->dma_submit_sbatch[idx] = submit_sbatch;
    prt_trace_on_export_submit_ahead(rt);
    prt_trace_log_event(rt, buf->stage_idx, PRT_TRACE_EVT_EXPORT_SUBMIT_AHEAD,
                        buf->tensor_id, submit_sbatch);
  } else {
    buf->cmd_running[idx] = 0;
    buf->cmd_count[idx] = 0;
    (void)prt_dma_token_cleanup(&buf->dma_tokens[idx]);
    buf->state_epoch += 1;
    pthread_cond_broadcast(&buf->cv);
  }
  pthread_mutex_unlock(&buf->lock);
  return rc;
}

int prt_process_c7(prt_runtime_t *rt, prt_pipebuf_t *buf) {
  (void)rt;
  if (!buf || !buf->ring) return PRT_ERR_INVAL;

  pthread_mutex_lock(&buf->lock);
  pthread_mutex_lock(&buf->ring->lock);

  if (!buf->tag) {
    if ((buf->ring->head <= buf->subbatch_offset) && (buf->subbatch_offset < buf->ring->tail)) {
      uint32_t slot = buf->subbatch_offset % buf->ring->size;
      int rc = page_list_copy(&buf->slot_pages[0], &buf->ring->slot_pages[slot]);
      if (rc == PRT_OK) {
        buf->full[0] = 1;
        buf->subbatch_offset += 1;
        buf->tag = 1;
        pthread_cond_broadcast(&buf->cv);
      }
    }
  } else if (!buf->full[0]) {
    (void)ring_use_locked(buf->ring, buf->subbatch_offset - 1U);
    buf->tag = 0;
    pthread_cond_broadcast(&buf->cv);
  }

  pthread_mutex_unlock(&buf->ring->lock);
  pthread_mutex_unlock(&buf->lock);
  return PRT_OK;
}

int prt_process_c8(prt_runtime_t *rt, prt_pipebuf_t *buf) {
  (void)rt;
  if (!buf || !buf->ring) return PRT_ERR_INVAL;

  pthread_mutex_lock(&buf->lock);
  pthread_mutex_lock(&buf->ring->lock);

  if (!buf->tag && !buf->full[0]) {
    if ((buf->ring->tail - buf->ring->head) < buf->ring->size) {
      uint32_t slot = buf->subbatch_offset % buf->ring->size;
      if (page_list_copy(&buf->slot_pages[0], &buf->ring->slot_pages[slot]) == PRT_OK) {
        buf->tag = 1;
      }
    }
  }

  if (buf->tag && buf->full[0]) {
    (void)ring_fill_locked(buf->ring, buf->subbatch_offset);
    buf->subbatch_offset += 1;
    buf->tag = 0;
  }

  if (!buf->tag && buf->full[0]) {
    if ((buf->ring->tail - buf->ring->head) < buf->ring->size) {
      uint32_t slot = buf->subbatch_offset % buf->ring->size;
      if (page_list_copy(&buf->slot_pages[0], &buf->ring->slot_pages[slot]) == PRT_OK) {
        buf->full[0] = 0;
        buf->tag = 1;
        pthread_cond_broadcast(&buf->cv);
      }
    }
  }

  pthread_mutex_unlock(&buf->ring->lock);
  pthread_mutex_unlock(&buf->lock);
  return PRT_OK;
}
