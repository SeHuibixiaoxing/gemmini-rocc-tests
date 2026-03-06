#include "prt_dma.h"

#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "prt_runtime.h"
#include "prt_rerocc.h"

#if defined(__riscv)
#include "include/gemmini.h"
#define XCUSTOM_DMA 2
#endif

static int dma_blocking_submit(prt_runtime_t *rt, const prt_dma_req_t *req, prt_dma_token_t *tok);
static int dma_blocking_wait(prt_runtime_t *rt, prt_dma_token_t *tok, uint64_t timeout_ns);
static int dma_blocking_submit_and_wait(prt_runtime_t *rt, const prt_dma_req_t *req, uint64_t timeout_ns);

static int dma_poll_submit(prt_runtime_t *rt, const prt_dma_req_t *req, prt_dma_token_t *tok);
static int dma_poll_wait(prt_runtime_t *rt, prt_dma_token_t *tok, uint64_t timeout_ns);
static int dma_poll_submit_and_wait(prt_runtime_t *rt, const prt_dma_req_t *req, uint64_t timeout_ns);
static void *dma_progress_thread_main(void *arg);
static int dma_submit_wait_annotated(prt_runtime_t *rt, const prt_dma_req_t *req,
                                     uint32_t stage_idx, uint32_t tensor_id,
                                     uint64_t timeout_ns);
static uint64_t page_base_addr(const prt_runtime_t *rt, const prt_page_t *page);

typedef struct prt_dma_pending_node_s {
  prt_dma_token_t *tok;
  struct prt_dma_pending_node_s *next;
} prt_dma_pending_node_t;

static void build_abs_timeout(uint64_t timeout_ns, struct timespec *out) {
  struct timespec now;
  uint64_t nsec;
  clock_gettime(CLOCK_REALTIME, &now);
  nsec = (uint64_t)now.tv_nsec + timeout_ns;
  out->tv_sec = now.tv_sec + (time_t)(nsec / 1000000000ULL);
  out->tv_nsec = (long)(nsec % 1000000000ULL);
}

static uint64_t page_base_addr(const prt_runtime_t *rt, const prt_page_t *page) {
  uint64_t page_bytes;
  if (!rt || !page) return 0;
  page_bytes = rt->cfg.page_size_bytes ? rt->cfg.page_size_bytes : PRT_PAGE_SIZE_BYTES;
  return PRT_SHARED_SPAD_GLOBAL_ADDR_BASE + (uint64_t)page->ppn * page_bytes;
}

static int dma_submit_wait_annotated(prt_runtime_t *rt, const prt_dma_req_t *req,
                                     uint32_t stage_idx, uint32_t tensor_id,
                                     uint64_t timeout_ns) {
  int rc;
  prt_dma_token_t tok;
  if (!rt || !req) return PRT_ERR_INVAL;
  memset(&tok, 0, sizeof(tok));
  tok.stage_idx = stage_idx;
  tok.tensor_id = tensor_id;
  rc = prt_dma_submit(rt, req, &tok);
  if (rc != PRT_OK) {
    (void)prt_dma_token_cleanup(&tok);
    return rc;
  }
  rc = prt_dma_wait(rt, &tok, timeout_ns);
  (void)prt_dma_token_cleanup(&tok);
  return rc;
}

static void dma_token_init(prt_dma_token_t *tok) {
  uint32_t stage_idx = 0;
  uint32_t tensor_id = 0;
  int rr_scope_valid = 0;
  uint32_t rr_cfg_id = 0;
  uint32_t rr_manager_id = 0;
  uint32_t rr_opcode_id = 0;
  if (tok) {
    stage_idx = tok->stage_idx;
    tensor_id = tok->tensor_id;
    rr_scope_valid = tok->rr_scope_valid;
    rr_cfg_id = tok->rr_cfg_id;
    rr_manager_id = tok->rr_manager_id;
    rr_opcode_id = tok->rr_opcode_id;
  }
  memset(tok, 0, sizeof(*tok));
  tok->stage_idx = stage_idx;
  tok->tensor_id = tensor_id;
  tok->rr_scope_valid = rr_scope_valid;
  tok->rr_cfg_id = rr_cfg_id;
  tok->rr_manager_id = rr_manager_id;
  tok->rr_opcode_id = rr_opcode_id;
  tok->hw_done_flag = 0;
  tok->submit_ns = 0;
  tok->traced_complete = 0;
  pthread_mutex_init(&tok->lock, NULL);
  pthread_cond_init(&tok->cv, NULL);
  tok->initialized = 1;
}

static void dma_token_release_scope(prt_dma_token_t *tok, int with_fence) {
#if defined(__riscv)
  prt_rr_scope_t scope;
#else
  (void)with_fence;
#endif
  if (!tok || !tok->rr_scope_valid) return;
#if defined(__riscv)
  memset(&scope, 0, sizeof(scope));
  scope.valid = tok->rr_scope_valid;
  scope.cfg_id = tok->rr_cfg_id;
  scope.stage_id = tok->stage_idx;
  scope.manager_id = tok->rr_manager_id;
  scope.opcode_id = tok->rr_opcode_id;
  if (with_fence) (void)prt_rr_fence_scope(&scope);
  (void)prt_rr_release_scope(&scope);
#endif
  tok->rr_scope_valid = 0;
}

static void dma_token_fini(prt_dma_token_t *tok) {
  if (!tok) return;
  if (!tok->initialized) return;
  dma_token_release_scope(tok, 0);
  pthread_cond_destroy(&tok->cv);
  pthread_mutex_destroy(&tok->lock);
  tok->initialized = 0;
}

static void dma_token_complete(prt_dma_token_t *tok, int status) {
  pthread_mutex_lock(&tok->lock);
  tok->status = status;
  tok->done = 1;
  pthread_cond_broadcast(&tok->cv);
  pthread_mutex_unlock(&tok->lock);
}

static void dma_trace_complete_once(prt_runtime_t *rt, prt_dma_token_t *tok) {
  uint64_t end_ns;
  uint64_t elapsed = 0;
  int do_trace = 0;
  if (!rt || !tok || !tok->initialized) return;

  pthread_mutex_lock(&tok->lock);
  if (tok->done && !tok->traced_complete) {
    tok->traced_complete = 1;
    do_trace = 1;
    if (tok->submit_ns > 0) {
      end_ns = prt_now_ns();
      if (end_ns > tok->submit_ns) elapsed = end_ns - tok->submit_ns;
    }
  }
  pthread_mutex_unlock(&tok->lock);

  if (do_trace) {
    prt_trace_log_event(rt, tok->stage_idx, PRT_TRACE_EVT_DMA_COMPLETE,
                        tok->tensor_id, (uint32_t)(tok->status < 0 ? 0 : tok->status));
    prt_trace_on_dma_complete(rt, elapsed);
  }
}

static int dma_pending_push(prt_runtime_t *rt, prt_dma_token_t *tok) {
  prt_dma_pending_node_t *node;
  prt_dma_pending_node_t *tail;
  if (!rt || !tok) return PRT_ERR_INVAL;
  node = (prt_dma_pending_node_t *)calloc(1, sizeof(*node));
  if (!node) return PRT_ERR_NOMEM;
  node->tok = tok;

  pthread_mutex_lock(&rt->dma_pending_lock);
  tail = (prt_dma_pending_node_t *)rt->dma_pending_tail;
  if (tail) tail->next = node;
  else rt->dma_pending_head = (void *)node;
  rt->dma_pending_tail = (void *)node;
  rt->dma_pending_count += 1;
  pthread_cond_broadcast(&rt->dma_pending_cv);
  pthread_mutex_unlock(&rt->dma_pending_lock);
  return PRT_OK;
}

static int dma_pending_remove_locked(prt_runtime_t *rt, prt_dma_token_t *tok) {
  prt_dma_pending_node_t *prev = NULL;
  prt_dma_pending_node_t *cur;
  if (!rt || !tok) return 0;
  cur = (prt_dma_pending_node_t *)rt->dma_pending_head;
  while (cur) {
    if (cur->tok == tok) {
      prt_dma_pending_node_t *next = cur->next;
      if (prev) prev->next = next;
      else rt->dma_pending_head = (void *)next;
      if ((void *)cur == rt->dma_pending_tail) {
        rt->dma_pending_tail = (void *)prev;
      }
      if (rt->dma_pending_count > 0) rt->dma_pending_count -= 1;
      free(cur);
      return 1;
    }
    prev = cur;
    cur = cur->next;
  }
  return 0;
}

static int dma_pending_remove(prt_runtime_t *rt, prt_dma_token_t *tok) {
  int removed;
  if (!rt || !tok) return 0;
  pthread_mutex_lock(&rt->dma_pending_lock);
  removed = dma_pending_remove_locked(rt, tok);
  pthread_mutex_unlock(&rt->dma_pending_lock);
  return removed;
}

static void dma_pending_clear_with_status(prt_runtime_t *rt, int status) {
  prt_dma_pending_node_t *cur;
  if (!rt) return;
  pthread_mutex_lock(&rt->dma_pending_lock);
  cur = (prt_dma_pending_node_t *)rt->dma_pending_head;
  while (cur) {
    prt_dma_pending_node_t *next = cur->next;
    if (cur->tok) {
      pthread_mutex_lock(&cur->tok->lock);
      if (!cur->tok->done) {
        cur->tok->status = status;
        cur->tok->done = 1;
        pthread_cond_broadcast(&cur->tok->cv);
      }
      pthread_mutex_unlock(&cur->tok->lock);
      dma_token_release_scope(cur->tok, 0);
    }
    free(cur);
    cur = next;
  }
  rt->dma_pending_head = NULL;
  rt->dma_pending_tail = NULL;
  rt->dma_pending_count = 0;
  pthread_mutex_unlock(&rt->dma_pending_lock);
}

#if defined(__riscv)
static inline void hw_dma_set_dst(uint64_t addr, volatile int *done_flag) {
  ROCC_INSTRUCTION_0_R_R(XCUSTOM_DMA, addr, (uint64_t)done_flag, 2);
}

static inline void hw_dma_set_src(uint64_t addr, uint64_t len) {
  ROCC_INSTRUCTION_0_R_R(XCUSTOM_DMA, addr, len, 1);
}

static inline uint64_t hw_dma_fence(void) {
  uint64_t status = 0;
  asm volatile("fence" ::: "memory");
  ROCC_INSTRUCTION_R_R_R(XCUSTOM_DMA, status, 0, 0, 3);
  asm volatile("fence" ::: "memory");
  return status;
}
#endif

int prt_dma_backend_init(prt_runtime_t *rt) {
  if (!rt) return PRT_ERR_INVAL;

  rt->progress_thread_enabled = 0;
  rt->dma_pending_head = NULL;
  rt->dma_pending_tail = NULL;
  rt->dma_pending_count = 0;

  if (rt->cfg.dma_backend == PRT_DMA_BACKEND_BLOCKING_FENCE) {
    rt->dma_ops.submit = dma_blocking_submit;
    rt->dma_ops.wait = dma_blocking_wait;
    rt->dma_ops.submit_and_wait = dma_blocking_submit_and_wait;
    rt->dma_ops.name = "blocking_fence";
    return PRT_OK;
  }

  rt->dma_ops.submit = dma_poll_submit;
  rt->dma_ops.wait = dma_poll_wait;
  rt->dma_ops.submit_and_wait = dma_poll_submit_and_wait;
  rt->dma_ops.name = "poll_progress_thread";

  if (pthread_mutex_init(&rt->dma_pending_lock, NULL) != 0) return PRT_ERR_STATE;
  if (pthread_cond_init(&rt->dma_pending_cv, NULL) != 0) {
    pthread_mutex_destroy(&rt->dma_pending_lock);
    return PRT_ERR_STATE;
  }
  rt->progress_thread.rt = rt;
  rt->progress_thread.stop = 0;
  if (pthread_create(&rt->progress_thread.thread, NULL, dma_progress_thread_main,
                     &rt->progress_thread) != 0) {
    pthread_cond_destroy(&rt->dma_pending_cv);
    pthread_mutex_destroy(&rt->dma_pending_lock);
    return PRT_ERR_STATE;
  }
  rt->progress_thread_enabled = 1;
  return PRT_OK;
}

void prt_dma_backend_destroy(prt_runtime_t *rt) {
  if (!rt) return;
  if (!rt->progress_thread_enabled) return;

  pthread_mutex_lock(&rt->dma_pending_lock);
  rt->progress_thread.stop = 1;
  pthread_cond_broadcast(&rt->dma_pending_cv);
  pthread_mutex_unlock(&rt->dma_pending_lock);

  pthread_join(rt->progress_thread.thread, NULL);
  dma_pending_clear_with_status(rt, PRT_ERR_STATE);
  pthread_cond_destroy(&rt->dma_pending_cv);
  pthread_mutex_destroy(&rt->dma_pending_lock);
  rt->progress_thread_enabled = 0;
}

int prt_dma_submit(prt_runtime_t *rt, const prt_dma_req_t *req, prt_dma_token_t *tok) {
  if (!rt || !req || !tok || !rt->dma_ops.submit) return PRT_ERR_INVAL;
  return rt->dma_ops.submit(rt, req, tok);
}

int prt_dma_wait(prt_runtime_t *rt, prt_dma_token_t *tok, uint64_t timeout_ns) {
  if (!rt || !tok || !rt->dma_ops.wait) return PRT_ERR_INVAL;
  return rt->dma_ops.wait(rt, tok, timeout_ns);
}

int prt_dma_try_wait(prt_runtime_t *rt, prt_dma_token_t *tok) {
  int done;
  int status;
  if (!rt || !tok) return PRT_ERR_INVAL;
  if (!tok->initialized) return PRT_ERR_NOT_READY;

  pthread_mutex_lock(&tok->lock);
  done = tok->done;
  status = tok->status;
  pthread_mutex_unlock(&tok->lock);

  if (!done) return PRT_ERR_NOT_READY;
  dma_token_release_scope(tok, 1);
  dma_trace_complete_once(rt, tok);
  (void)dma_pending_remove(rt, tok);
  return status;
}

int prt_dma_token_cleanup(prt_dma_token_t *tok) {
  if (!tok) return PRT_ERR_INVAL;
  if (!tok->initialized) {
    dma_token_release_scope(tok, 0);
    memset(tok, 0, sizeof(*tok));
    return PRT_OK;
  }
  dma_token_fini(tok);
  memset(tok, 0, sizeof(*tok));
  return PRT_OK;
}

int prt_dma_submit_and_wait(prt_runtime_t *rt, const prt_dma_req_t *req, uint64_t timeout_ns) {
  if (!rt || !req || !rt->dma_ops.submit_and_wait) return PRT_ERR_INVAL;
  return rt->dma_ops.submit_and_wait(rt, req, timeout_ns);
}

int prt_dma_copy_spm_pages(prt_runtime_t *rt, const prt_page_list_t *dst_pages,
                           const prt_page_list_t *src_pages, uint32_t manager_id,
                           uint32_t stage_idx, uint32_t tensor_id, uint64_t timeout_ns) {
  prt_dma_req_t req;
  uint32_t page_count;
  uint64_t page_bytes;
  if (!rt || !dst_pages || !src_pages || !dst_pages->data || !src_pages->data) return PRT_ERR_INVAL;
  if (dst_pages->size == 0 || src_pages->size == 0 || dst_pages->size != src_pages->size) return PRT_ERR_INVAL;

  page_count = dst_pages->size;
  page_bytes = rt->cfg.page_size_bytes ? rt->cfg.page_size_bytes : PRT_PAGE_SIZE_BYTES;
#if !defined(__riscv)
  req.src_addr = page_base_addr(rt, &src_pages->data[0]);
  req.dst_addr = page_base_addr(rt, &dst_pages->data[0]);
  req.bytes = (uint64_t)page_count * page_bytes;
  req.src_acc = manager_id;
  req.dst_acc = manager_id;
  return dma_submit_wait_annotated(rt, &req, stage_idx, tensor_id, timeout_ns);
#endif
  req.src_acc = manager_id;
  req.dst_acc = manager_id;
  for (uint32_t i = 0; i < page_count; ++i) {
    int rc;
    req.src_addr = page_base_addr(rt, &src_pages->data[i]);
    req.dst_addr = page_base_addr(rt, &dst_pages->data[i]);
    req.bytes = page_bytes;
    rc = dma_submit_wait_annotated(rt, &req, stage_idx, tensor_id, timeout_ns);
    if (rc != PRT_OK) return rc;
  }
  return PRT_OK;
}

int prt_dma_copy_dram_to_spm_pages(prt_runtime_t *rt, const prt_page_list_t *dst_pages,
                                   uint64_t src_dram_addr, uint32_t manager_id,
                                   uint32_t stage_idx, uint32_t tensor_id, uint64_t timeout_ns) {
  prt_dma_req_t req;
  uint64_t page_bytes;
  if (!rt || !dst_pages || !dst_pages->data || dst_pages->size == 0) return PRT_ERR_INVAL;
  page_bytes = rt->cfg.page_size_bytes ? rt->cfg.page_size_bytes : PRT_PAGE_SIZE_BYTES;
#if !defined(__riscv)
  req.src_addr = src_dram_addr;
  req.dst_addr = page_base_addr(rt, &dst_pages->data[0]);
  req.bytes = (uint64_t)dst_pages->size * page_bytes;
  req.src_acc = manager_id;
  req.dst_acc = manager_id;
  return dma_submit_wait_annotated(rt, &req, stage_idx, tensor_id, timeout_ns);
#endif
  req.src_acc = manager_id;
  req.dst_acc = manager_id;
  for (uint32_t i = 0; i < dst_pages->size; ++i) {
    int rc;
    req.src_addr = src_dram_addr + (uint64_t)i * page_bytes;
    req.dst_addr = page_base_addr(rt, &dst_pages->data[i]);
    req.bytes = page_bytes;
    rc = dma_submit_wait_annotated(rt, &req, stage_idx, tensor_id, timeout_ns);
    if (rc != PRT_OK) return rc;
  }
  return PRT_OK;
}

int prt_dma_copy_spm_pages_to_dram(prt_runtime_t *rt, uint64_t dst_dram_addr,
                                   const prt_page_list_t *src_pages, uint32_t manager_id,
                                   uint32_t stage_idx, uint32_t tensor_id, uint64_t timeout_ns) {
  prt_dma_req_t req;
  uint64_t page_bytes;
  if (!rt || !src_pages || !src_pages->data || src_pages->size == 0) return PRT_ERR_INVAL;
  page_bytes = rt->cfg.page_size_bytes ? rt->cfg.page_size_bytes : PRT_PAGE_SIZE_BYTES;
#if !defined(__riscv)
  req.src_addr = page_base_addr(rt, &src_pages->data[0]);
  req.dst_addr = dst_dram_addr;
  req.bytes = (uint64_t)src_pages->size * page_bytes;
  req.src_acc = manager_id;
  req.dst_acc = manager_id;
  return dma_submit_wait_annotated(rt, &req, stage_idx, tensor_id, timeout_ns);
#endif
  req.src_acc = manager_id;
  req.dst_acc = manager_id;
  for (uint32_t i = 0; i < src_pages->size; ++i) {
    int rc;
    req.src_addr = page_base_addr(rt, &src_pages->data[i]);
    req.dst_addr = dst_dram_addr + (uint64_t)i * page_bytes;
    req.bytes = page_bytes;
    rc = dma_submit_wait_annotated(rt, &req, stage_idx, tensor_id, timeout_ns);
    if (rc != PRT_OK) return rc;
  }
  return PRT_OK;
}

int prt_dma_copy_spm_va(prt_runtime_t *rt, uint64_t dst_va, uint64_t src_va, uint64_t bytes,
                        uint32_t manager_id, uint32_t stage_idx, uint32_t tensor_id,
                        uint64_t timeout_ns) {
  prt_spm_xlate_seg_t *src_segs = NULL;
  prt_spm_xlate_seg_t *dst_segs = NULL;
  uint32_t src_count = 0;
  uint32_t dst_count = 0;
  uint32_t seg_cap;
  uint64_t left = bytes;
  uint32_t si = 0;
  uint32_t di = 0;
  uint32_t soff = 0;
  uint32_t doff = 0;
  uint64_t page_bytes;
  int rc;

  if (!rt || bytes == 0) return PRT_ERR_INVAL;
  page_bytes = rt->cfg.page_size_bytes ? rt->cfg.page_size_bytes : PRT_PAGE_SIZE_BYTES;
  seg_cap = (uint32_t)((bytes + page_bytes - 1ULL) / page_bytes) + 2U;
#if !defined(__riscv)
  {
    prt_dma_req_t req;
    req.src_addr = src_va;
    req.dst_addr = dst_va;
    req.bytes = bytes;
    req.src_acc = manager_id;
    req.dst_acc = manager_id;
    return dma_submit_wait_annotated(rt, &req, stage_idx, tensor_id, timeout_ns);
  }
#endif

  src_segs = (prt_spm_xlate_seg_t *)calloc(seg_cap, sizeof(*src_segs));
  dst_segs = (prt_spm_xlate_seg_t *)calloc(seg_cap, sizeof(*dst_segs));
  if (!src_segs || !dst_segs) {
    free(src_segs);
    free(dst_segs);
    return PRT_ERR_NOMEM;
  }

  rc = prt_spm_translate_range(rt, src_va, bytes, src_segs, seg_cap, &src_count);
  if (rc != PRT_OK) goto done;
  rc = prt_spm_translate_range(rt, dst_va, bytes, dst_segs, seg_cap, &dst_count);
  if (rc != PRT_OK) goto done;

  while (left > 0 && si < src_count && di < dst_count) {
    prt_dma_req_t req;
    uint32_t sleft = src_segs[si].bytes - soff;
    uint32_t dleft = dst_segs[di].bytes - doff;
    uint32_t chunk = sleft < dleft ? sleft : dleft;
    if (chunk > left) chunk = (uint32_t)left;

    req.src_addr = src_segs[si].paddr + (uint64_t)soff;
    req.dst_addr = dst_segs[di].paddr + (uint64_t)doff;
    req.bytes = chunk;
    req.src_acc = manager_id;
    req.dst_acc = manager_id;
    rc = dma_submit_wait_annotated(rt, &req, stage_idx, tensor_id, timeout_ns);
    if (rc != PRT_OK) goto done;

    left -= chunk;
    soff += chunk;
    doff += chunk;
    if (soff >= src_segs[si].bytes) {
      si += 1U;
      soff = 0;
    }
    if (doff >= dst_segs[di].bytes) {
      di += 1U;
      doff = 0;
    }
  }

  if (left != 0) rc = PRT_ERR_STATE;
  else rc = PRT_OK;

done:
  free(src_segs);
  free(dst_segs);
  return rc;
}

static int dma_blocking_submit(prt_runtime_t *rt, const prt_dma_req_t *req, prt_dma_token_t *tok) {
  if (!req || !tok) return PRT_ERR_INVAL;

  dma_token_init(tok);
  tok->id = __sync_add_and_fetch(&rt->next_dma_token_id, 1);
  tok->submit_ns = prt_now_ns();
  prt_trace_on_dma_submit(rt);
  prt_trace_log_event(rt, tok->stage_idx, PRT_TRACE_EVT_DMA_SUBMIT,
                      tok->tensor_id,
                      req->bytes > UINT32_MAX ? UINT32_MAX : (uint32_t)req->bytes);

#if defined(__riscv)
  {
    prt_rr_scope_t scope;
    int rrc = prt_rr_acquire_scope(rt, tok->stage_idx, req->dst_acc, 2U, &scope);
    if (rrc != PRT_OK) {
      tok->done = 1;
      tok->status = rrc;
      return rrc;
    }
    tok->rr_scope_valid = scope.valid;
    tok->rr_cfg_id = scope.cfg_id;
    tok->rr_manager_id = scope.manager_id;
    tok->rr_opcode_id = scope.opcode_id;
  }
  tok->hw_done_flag = 0;
  hw_dma_set_dst(req->dst_addr, &tok->hw_done_flag);
  hw_dma_set_src(req->src_addr, req->bytes);
#else
  (void)req;
  // Host-mode smoke path: do not dereference device-like addresses.
  // Functional data movement is validated on target runtime.
#endif

  tok->done = 0;
  tok->status = PRT_OK;
  return PRT_OK;
}

static int dma_blocking_wait(prt_runtime_t *rt, prt_dma_token_t *tok, uint64_t timeout_ns) {
  (void)timeout_ns;
  if (!tok) return PRT_ERR_INVAL;

#if defined(__riscv)
  (void)hw_dma_fence();
  dma_token_release_scope(tok, 1);
#endif

  dma_token_complete(tok, PRT_OK);
  dma_trace_complete_once(rt, tok);
  return PRT_OK;
}

static int dma_blocking_submit_and_wait(prt_runtime_t *rt, const prt_dma_req_t *req, uint64_t timeout_ns) {
  prt_dma_token_t tok;
  memset(&tok, 0, sizeof(tok));
  int rc = dma_blocking_submit(rt, req, &tok);
  if (rc != PRT_OK) return rc;
  return dma_blocking_wait(rt, &tok, timeout_ns);
}

static int dma_poll_submit(prt_runtime_t *rt, const prt_dma_req_t *req, prt_dma_token_t *tok) {
  int rc;
  if (!rt || !req || !tok) return PRT_ERR_INVAL;
  if (!rt->progress_thread_enabled) return PRT_ERR_STATE;

  rc = dma_blocking_submit(rt, req, tok);
  if (rc != PRT_OK) return rc;

  rc = dma_pending_push(rt, tok);
  if (rc != PRT_OK) {
    dma_token_complete(tok, rc);
    return rc;
  }
  return PRT_OK;
}

static int dma_poll_wait(prt_runtime_t *rt, prt_dma_token_t *tok, uint64_t timeout_ns) {
  int rc = PRT_OK;
  int wait_rc = 0;
  if (!rt || !tok) return PRT_ERR_INVAL;

  pthread_mutex_lock(&tok->lock);
  while (!tok->done) {
    if (timeout_ns == 0) {
      wait_rc = pthread_cond_wait(&tok->cv, &tok->lock);
      if (wait_rc != 0) {
        pthread_mutex_unlock(&tok->lock);
        (void)dma_pending_remove(rt, tok);
        return PRT_ERR_STATE;
      }
    } else {
      struct timespec abs;
      build_abs_timeout(timeout_ns, &abs);
      wait_rc = pthread_cond_timedwait(&tok->cv, &tok->lock, &abs);
      if (wait_rc == ETIMEDOUT) break;
      if (wait_rc != 0) {
        pthread_mutex_unlock(&tok->lock);
        (void)dma_pending_remove(rt, tok);
        return PRT_ERR_STATE;
      }
    }
  }
  if (tok->done) rc = tok->status;
  pthread_mutex_unlock(&tok->lock);

  if (wait_rc == ETIMEDOUT) {
    (void)dma_pending_remove(rt, tok);
    pthread_mutex_lock(&tok->lock);
    if (!tok->done) {
      tok->done = 1;
      tok->status = PRT_ERR_TIMEOUT;
      rc = PRT_ERR_TIMEOUT;
    } else {
      rc = tok->status;
    }
    pthread_mutex_unlock(&tok->lock);
    dma_token_release_scope(tok, 1);
    dma_trace_complete_once(rt, tok);
    return rc;
  }

  dma_token_release_scope(tok, 1);
  dma_trace_complete_once(rt, tok);
  (void)dma_pending_remove(rt, tok);
  return rc;
}

static int dma_poll_submit_and_wait(prt_runtime_t *rt, const prt_dma_req_t *req, uint64_t timeout_ns) {
  prt_dma_token_t *tok;
  int rc;
  tok = (prt_dma_token_t *)calloc(1, sizeof(*tok));
  if (!tok) return PRT_ERR_NOMEM;
  rc = dma_poll_submit(rt, req, tok);
  if (rc != PRT_OK) {
    free(tok);
    return rc;
  }
  rc = dma_poll_wait(rt, tok, timeout_ns);
  dma_token_fini(tok);
  free(tok);
  return rc;
}

static void *dma_progress_thread_main(void *arg) {
  prt_progress_thread_ctx_t *ctx = (prt_progress_thread_ctx_t *)arg;
  prt_runtime_t *rt;
  if (!ctx || !ctx->rt) return NULL;
  rt = ctx->rt;

  while (1) {
    int progressed = 0;
    prt_dma_pending_node_t *prev = NULL;
    prt_dma_pending_node_t *cur;

    pthread_mutex_lock(&rt->dma_pending_lock);
    cur = (prt_dma_pending_node_t *)rt->dma_pending_head;

    while (cur) {
      prt_dma_pending_node_t *next = cur->next;
      int remove_now = 0;
      prt_dma_token_t *tok = cur->tok;

      if (!tok) {
        remove_now = 1;
      } else {
        pthread_mutex_lock(&tok->lock);
        if (tok->done) {
          remove_now = 1;
        } else if (ctx->stop) {
          tok->status = PRT_ERR_STATE;
          tok->done = 1;
          pthread_cond_broadcast(&tok->cv);
          remove_now = 1;
        } else {
#if defined(__riscv)
          if (tok->hw_done_flag) {
            tok->status = PRT_OK;
            tok->done = 1;
            pthread_cond_broadcast(&tok->cv);
            remove_now = 1;
          }
#else
          tok->status = PRT_OK;
          tok->done = 1;
          pthread_cond_broadcast(&tok->cv);
          remove_now = 1;
#endif
        }
        pthread_mutex_unlock(&tok->lock);
      }

      if (remove_now) {
        if (prev) prev->next = next;
        else rt->dma_pending_head = (void *)next;
        if ((void *)cur == rt->dma_pending_tail) {
          rt->dma_pending_tail = (void *)prev;
        }
        free(cur);
        if (rt->dma_pending_count > 0) rt->dma_pending_count -= 1;
        progressed = 1;
      } else {
        prev = cur;
      }
      cur = next;
    }

    if (ctx->stop && rt->dma_pending_count == 0) {
      pthread_mutex_unlock(&rt->dma_pending_lock);
      break;
    }

    if (!progressed) {
      struct timespec abs;
      build_abs_timeout(1000000ULL, &abs); // 1ms poll
      (void)pthread_cond_timedwait(&rt->dma_pending_cv, &rt->dma_pending_lock, &abs);
    }
    pthread_mutex_unlock(&rt->dma_pending_lock);
  }
  return NULL;
}
