#include "prt_dma.h"

#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#if defined(__linux__)
#include <fcntl.h>
#include <sys/mman.h>
#endif
#if defined(__linux__) && defined(__riscv)
#include <sys/syscall.h>
#include <unistd.h>
#endif

#include "prt_progress.h"
#include "prt_breadcrumb.h"
#include "prt_debug_state.h"
#include "prt_runtime.h"
#include "prt_rerocc.h"
#include "prt_trigger_log.h"

#if defined(__riscv)
#include "include/gemmini.h"
#include "rerocc-linux-tests/rerocc_control.h"
#define XCUSTOM_DMA 2
#define DMA_MON_VALID 0ULL
#define DMA_MON_SRC_CMDS 1ULL
#define DMA_MON_DST_CMDS 2ULL
#define DMA_MON_REQ_COPY_BYTES 3ULL
#define DMA_MON_CYCLES 4ULL
#define DMA_MON_EFFECTIVE_BYTES 5ULL
#define DMA_MON_EFF_BW_X1000_BPC 6ULL
#endif

#ifndef PRT_ENABLE_FIRESIM_TRACERV_DMA_WINDOW_MARKERS
#define PRT_ENABLE_FIRESIM_TRACERV_DMA_WINDOW_MARKERS 0
#endif

#if defined(__GNUC__)
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wunused-variable"
#endif

#define PRT_DMA_COMPLETION_SLOT_COUNT_DEFAULT 1024U

typedef enum {
  PRT_DMA_TRACERV_MARKER_PROGRAM_BEGIN = 0,
  PRT_DMA_TRACERV_MARKER_PROGRAM_POST_FENCE,
  PRT_DMA_TRACERV_MARKER_PROGRAM_POST_DST,
  PRT_DMA_TRACERV_MARKER_PROGRAM_POST_SRC,
  PRT_DMA_TRACERV_MARKER_WAIT_BEFORE_FENCE,
  PRT_DMA_TRACERV_MARKER_WAIT_AFTER_FENCE,
  PRT_DMA_TRACERV_MARKER_WAIT_BEFORE_SHARED_FENCE,
  PRT_DMA_TRACERV_MARKER_WAIT_AFTER_SHARED_FENCE,
  PRT_DMA_TRACERV_MARKER_BATCH_RELEASE_BEGIN,
  PRT_DMA_TRACERV_MARKER_BATCH_RELEASE_END,
} prt_dma_tracerv_marker_t;

typedef struct {
  int valid;
  uint32_t page_idx;
  uint64_t copied_bytes;
} prt_dma_tracerv_scope_ctx_t;

static __thread prt_dma_tracerv_scope_ctx_t g_prt_dma_tracerv_scope_ctx = {
  .valid = 0,
  .page_idx = UINT32_MAX,
  .copied_bytes = 0ULL,
};

static int dma_blocking_submit(prt_runtime_t *rt, const prt_dma_req_t *req, prt_dma_token_t *tok);
static int dma_blocking_wait(prt_runtime_t *rt, prt_dma_token_t *tok, uint64_t timeout_ns);
static int dma_blocking_submit_and_wait(prt_runtime_t *rt, const prt_dma_req_t *req, uint64_t timeout_ns);

static int dma_poll_submit(prt_runtime_t *rt, const prt_dma_req_t *req, prt_dma_token_t *tok);
static int dma_poll_wait(prt_runtime_t *rt, prt_dma_token_t *tok, uint64_t timeout_ns);
static int dma_poll_submit_and_wait(prt_runtime_t *rt, const prt_dma_req_t *req, uint64_t timeout_ns);
static void *dma_progress_thread_main(void *arg);
static int dma_should_marker_trace_submit(const prt_dma_req_t *req, uint32_t tensor_id);
static int dma_should_checkpoint_doneflag_tok(const prt_dma_token_t *tok);
static int dma_should_checkpoint_submit_wait_tok(const prt_dma_token_t *tok);
static int dma_fixed_load_checkpoint_enabled(void);
static int dma_fixed_load_probe_enabled(void);
static int dma_fixed_load_monitor_probe_enabled(void);
static uint32_t dma_fixed_load_probe_stage_id(void);
static uint32_t dma_fixed_load_probe_tensor_id(void);
static int dma_fixed_load_probe_target_match(uint32_t stage_idx, uint32_t tensor_id);
static uint32_t dma_fixed_load_pre_src_nops(void);
static int dma_fixed_load_probe_token_in_window(uint32_t token_id);
static int dma_fixed_load_page_probe_range_match(uint32_t page_idx);
static int dma_export_probe_enabled(void);
static int dma_export_probe_token_in_window(uint32_t token_id);
static int dma_export_page_probe_range_match(uint32_t page_idx);
static int dma_should_sparse_export_page_probe(uint32_t stage_idx, uint32_t tensor_id,
                                               uint32_t page_idx, uint32_t page_count,
                                               uint64_t copied_bytes);
static int dma_should_sparse_fixed_load_page_probe(uint32_t stage_idx, uint32_t tensor_id,
                                                   uint32_t page_idx, uint32_t page_count,
                                                   uint64_t copied_bytes);
static int dma_should_sparse_fixed_load_submit_probe_tok(const prt_dma_token_t *tok);
static int dma_should_sparse_fixed_load_wait_probe_tok(const prt_dma_token_t *tok);
static int dma_should_sparse_export_submit_probe_tok(const prt_dma_token_t *tok);
static int dma_submit_marker_trace_enabled(void);
static int dma_should_log_export_chunk_marker(uint32_t page_idx, uint32_t page_count,
                                              uint64_t copied_bytes);
static int dma_env_flag_enabled(const char *name, int default_value);
static uint32_t dma_env_u32(const char *name, uint32_t default_value);
static int dma_env_has_value(const char *name);
static int dma_bounce_bypass_enabled(void);
static int dma_blocking_wait_poll_timeout_enabled(void);
#if defined(__linux__) && defined(__riscv)
static int dma_force_direct_enabled(void);
#endif
static int dma_tracerv_dma_window_page_range_match(uint32_t page_idx);
static inline int dma_tracerv_dma_window_target_match(uint32_t stage_idx, uint32_t tensor_id,
                                                      uint32_t page_idx, uint64_t copied_bytes);
static inline void dma_tracerv_dma_window_scope_push(uint32_t page_idx, uint64_t copied_bytes);
static inline void dma_tracerv_dma_window_scope_pop(void);
static inline int dma_tracerv_dma_window_scope_match(uint32_t stage_idx, uint32_t tensor_id);
static inline void dma_tracerv_dma_window_emit(prt_dma_tracerv_marker_t marker);
static inline void dma_tracerv_dma_window_marker_if_scope(uint32_t stage_idx, uint32_t tensor_id,
                                                          prt_dma_tracerv_marker_t marker);
static inline void dma_tracerv_dma_window_marker_if_match(uint32_t stage_idx, uint32_t tensor_id,
                                                          uint32_t page_idx, uint64_t copied_bytes,
                                                          prt_dma_tracerv_marker_t marker);
static int dma_submit_wait_annotated(prt_runtime_t *rt, const prt_dma_req_t *req,
                                     uint32_t stage_idx, uint32_t tensor_id,
                                     uint64_t timeout_ns);
static int dma_submit_wait_annotated_scoped(prt_runtime_t *rt, const prt_dma_req_t *req,
                                            uint32_t stage_idx, uint32_t tensor_id,
                                            uint64_t timeout_ns, prt_rr_scope_t *scope,
                                            int force_export_probe);
static uint64_t page_base_addr(const prt_runtime_t *rt, const prt_page_t *page);
static int dma_completion_pool_init(prt_runtime_t *rt);
static void dma_completion_pool_destroy(prt_runtime_t *rt);
static int dma_completion_flag_acquire(prt_runtime_t *rt, prt_dma_token_t *tok);
static void dma_completion_flag_release(prt_dma_token_t *tok);
static int dma_completion_flag_value(const prt_dma_token_t *tok);
static int dma_completion_flag_refresh(prt_dma_token_t *tok);
static int dma_batch_scope_acquire(prt_runtime_t *rt, uint32_t stage_idx,
                                   uint32_t manager_id, uint32_t opcode_id,
                                   prt_rr_scope_t *scope);
static int dma_validate_stage_manager(const prt_runtime_t *rt, uint32_t stage_idx,
                                      uint32_t manager_id, const char *where);

static inline void dma_cpu_fence_rw(void) {
#if defined(__riscv)
  __asm__ volatile("fence rw, rw" ::: "memory");
#else
  __asm__ volatile("" ::: "memory");
#endif
}

static int dma_validate_stage_manager(const prt_runtime_t *rt, uint32_t stage_idx,
                                      uint32_t manager_id, const char *where) {
  const prt_action_exec_t *exec;
  uint32_t tile_count;
  if (!rt) return PRT_ERR_INVAL;
  exec = prt_runtime_current_exec_const(rt);
  if (!exec) return PRT_OK;
  if (stage_idx >= exec->stage_thread_count) {
    fprintf(stderr,
            "dma manager contract violation at %s: stage=%u stage_count=%u manager=%u\n",
            where ? where : "unknown", stage_idx, exec->stage_thread_count,
            manager_id);
    return PRT_ERR_INVAL;
  }
  if (exec->stage_dma_ids[stage_idx] == manager_id) return PRT_OK;
  if (!prt_cfg_pair_manager_mode_enabled(&rt->cfg)) {
    fprintf(stderr,
            "dma manager contract violation at %s: stage=%u manager=%u assigned_dma=%u pair=0\n",
            where ? where : "unknown", stage_idx, manager_id,
            exec->stage_dma_ids[stage_idx]);
    return PRT_ERR_STATE;
  }
  tile_count = exec->stage_tile_counts[stage_idx];
  if (tile_count > PRT_MAX_CORES) tile_count = PRT_MAX_CORES;
  for (uint32_t i = 0; i < tile_count; ++i) {
    if (exec->stage_mgr_ids[stage_idx][i] == manager_id) return PRT_OK;
  }
  fprintf(stderr,
          "dma manager contract violation at %s: stage=%u manager=%u assigned_dma=%u tile_count=%u pair=1\n",
          where ? where : "unknown", stage_idx, manager_id,
          exec->stage_dma_ids[stage_idx], tile_count);
  return PRT_ERR_STATE;
}

static uint32_t dma_breadcrumb_flags_from_token(const prt_dma_token_t *tok, uint32_t extra_flags) {
  uint32_t flags = extra_flags;
  if (!tok) return flags;
  if (tok->rr_scope_valid) flags |= PRT_BREADCRUMB_FLAG_SCOPE_VALID;
  if (tok->rr_scope_external) flags |= PRT_BREADCRUMB_FLAG_SCOPE_EXTERNAL;
  if (tok->hw_done_flag) flags |= PRT_BREADCRUMB_FLAG_HW_DONE;
  return flags;
}

static void dma_breadcrumb_page_begin(uint32_t tensor_id, uint32_t manager_id,
                                      uint32_t page_idx, const prt_dma_req_t *req,
                                      uint32_t flags, uint64_t timeout_ns) {
  if (!req || !prt_breadcrumb_enabled()) return;
  prt_breadcrumb_set_dma_transfer_context(tensor_id, manager_id, page_idx,
                                          req->src_addr, req->dst_addr, req->bytes);
  prt_breadcrumb_note(PRT_BREADCRUMB_KIND_DMA,
                      PRT_BREADCRUMB_PHASE_DMA_PAGE_BEGIN,
                      tensor_id,
                      0U,
                      manager_id,
                      page_idx,
                      PRT_OK,
                      flags,
                      req->src_addr,
                      req->dst_addr,
                      req->bytes,
                      timeout_ns,
                      __LINE__);
}

static void dma_breadcrumb_page_end(uint32_t tensor_id, uint32_t manager_id,
                                    uint32_t page_idx, const prt_dma_req_t *req,
                                    int rc, uint32_t flags, uint64_t timeout_ns) {
  if (prt_breadcrumb_enabled() && req) {
    prt_breadcrumb_note(PRT_BREADCRUMB_KIND_DMA,
                        PRT_BREADCRUMB_PHASE_DMA_PAGE_END,
                        tensor_id,
                        0U,
                        manager_id,
                        page_idx,
                        rc,
                        flags,
                        req->src_addr,
                        req->dst_addr,
                        req->bytes,
                        timeout_ns,
                        __LINE__);
  }
  prt_breadcrumb_clear_dma_transfer_context();
}

static void dma_breadcrumb_export_loop_phase(uint32_t stage_idx,
                                             uint32_t tensor_id,
                                             uint32_t manager_id,
                                             uint32_t page_idx,
                                             const prt_dma_req_t *req,
                                             int rc,
                                             uint32_t flags,
                                             uint64_t timeout_ns,
                                             uint32_t phase,
                                             uint32_t line) {
  if (stage_idx != 0U || tensor_id != 2U) return;
  if (!prt_breadcrumb_enabled() || !req) return;
  prt_breadcrumb_note(PRT_BREADCRUMB_KIND_DMA,
                      phase,
                      tensor_id,
                      0U,
                      manager_id,
                      page_idx,
                      rc,
                      flags,
                      req->src_addr,
                      req->dst_addr,
                      req->bytes,
                      timeout_ns,
                      line);
}

static void dma_breadcrumb_export_loop_phase_token(uint32_t stage_idx,
                                                   uint32_t tensor_id,
                                                   uint32_t manager_id,
                                                   uint32_t page_idx,
                                                   const prt_dma_req_t *req,
                                                   int rc,
                                                   uint32_t flags,
                                                   uint64_t timeout_ns,
                                                   uint32_t token_id,
                                                   uint32_t phase,
                                                   uint32_t line) {
  if (stage_idx != 0U || tensor_id != 2U) return;
  if (!prt_breadcrumb_enabled() || !req) return;
  prt_breadcrumb_note(PRT_BREADCRUMB_KIND_DMA,
                      phase,
                      tensor_id,
                      token_id,
                      manager_id,
                      page_idx,
                      rc,
                      flags,
                      req->src_addr,
                      req->dst_addr,
                      req->bytes, timeout_ns, line);
}

static void dma_trigger_note(prt_trigger_log_family_t family,
                             const char *phase,
                             uint32_t stage_idx,
                             uint32_t manager_id,
                             uint32_t tensor_id,
                             uint32_t page_idx,
                             uint32_t token_id,
                             int rc) {
  prt_trigger_log_note(&(const prt_trigger_log_event_t){
    .family = family,
    .phase = phase,
    .segment_idx = PRT_TRIGGER_LOG_ANY_U32,
    .global_stage_id = stage_idx,
    .local_stage_id = stage_idx,
    .subbatch_id = PRT_TRIGGER_LOG_ANY_U32,
    .manager_id = manager_id,
    .tensor_id = tensor_id,
    .page_idx = page_idx,
    .token_id = token_id,
    .rc = rc,
  });
}

static void dma_trigger_fixed_load_host(const char *phase,
                                        uint32_t stage_idx,
                                        uint32_t manager_id,
                                        uint32_t tensor_id,
                                        uint32_t page_idx,
                                        int rc) {
  dma_trigger_note(PRT_TRIGGER_LOG_FAMILY_DMA_FIXED_LOAD, phase,
                   stage_idx, manager_id, tensor_id, page_idx,
                   PRT_TRIGGER_LOG_ANY_U32, rc);
}

static void dma_trigger_export_host(const char *phase,
                                    uint32_t stage_idx,
                                    uint32_t manager_id,
                                    uint32_t tensor_id,
                                    uint32_t page_idx,
                                    int rc) {
  dma_trigger_note(PRT_TRIGGER_LOG_FAMILY_DMA_EXPORT, phase,
                   stage_idx, manager_id, tensor_id, page_idx,
                   PRT_TRIGGER_LOG_ANY_U32, rc);
}

static void dma_trigger_wait(const prt_dma_token_t *tok,
                             prt_trigger_log_family_t family,
                             const char *phase,
                             int rc) {
  if (!tok) return;
  dma_trigger_note(family, phase,
                   tok->stage_idx,
                   tok->rr_manager_id,
                   tok->tensor_id,
                   PRT_TRIGGER_LOG_ANY_U32,
                   tok->id,
                   rc);
}

static void dma_batch_scope_release(prt_rr_scope_t *scope);
static int dma_token_fence_scope(prt_dma_token_t *tok);
#if defined(__linux__) && defined(__riscv)
static uint64_t dma_min_u64(uint64_t a, uint64_t b);
static inline uint64_t dma_debug_mod64(uint64_t addr);
static int dma_chunk_needs_bounce(uint64_t src_addr, uint64_t dst_addr, uint64_t bytes);
static int dma_stage_bounce_ensure(prt_runtime_t *rt, uint32_t stage_idx);
static int dma_stage_bounce_region(prt_runtime_t *rt, uint32_t stage_idx, uint64_t align_mod64,
                                   uint8_t **out_ptr, uint64_t *out_room);
static int dma_copy_host_to_spm_pages_linux(prt_runtime_t *rt, const prt_page_list_t *dst_pages,
                                            const uint8_t *src_host, uint32_t manager_id,
                                            uint32_t stage_idx, uint32_t tensor_id,
                                            uint64_t timeout_ns, uint64_t total_bytes);
static int dma_copy_spm_pages_to_host_linux(prt_runtime_t *rt, uint8_t *dst_host,
                                            const prt_page_list_t *src_pages, uint32_t manager_id,
                                            uint32_t stage_idx, uint32_t tensor_id,
                                            uint64_t timeout_ns, uint64_t total_bytes);
#endif

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

#if defined(__riscv)
static uint64_t dma_debug_rr_read_opc_map(uint32_t opcode_id) {
  if (opcode_id >= 4U) return UINT64_MAX;
  return rr_read_csr(CSR_RROPC0 + opcode_id);
}

static uint64_t dma_debug_rr_read_cfg(uint32_t cfg_id) {
  if (cfg_id >= RR_MAX_CFGS) return UINT64_MAX;
  return rr_read_csr(CSR_RRCFG0 + cfg_id);
}
#endif

static uint64_t page_base_addr(const prt_runtime_t *rt, const prt_page_t *page) {
  uint64_t page_bytes;
  if (!rt || !page) return 0;
  page_bytes = rt->cfg.page_size_bytes ? rt->cfg.page_size_bytes : PRT_PAGE_SIZE_BYTES;
  return PRT_SHARED_SPAD_GLOBAL_ADDR_BASE + (uint64_t)page->ppn * page_bytes;
}

static uint64_t dma_min_u64(uint64_t a, uint64_t b) {
  return a < b ? a : b;
}

#if defined(__linux__) && defined(__riscv)
static int dma_prefault_and_lock_buffer(const char *tag, void *buf, size_t bytes) {
  volatile uint8_t *touch;
  const size_t page_bytes = prt_host_page_size_bytes();
  int mlock_rc;
  int mlock_errno = 0;

  if (!tag || !buf || bytes == 0U) return PRT_ERR_INVAL;
  if (page_bytes == 0U) return PRT_ERR_STATE;

  touch = (volatile uint8_t *)buf;
  PRT_PROGRESS_LOG("%s before-prefault ptr=%p size=%zu page_bytes=%zu",
                   tag, buf, bytes, page_bytes);
  for (size_t off = 0; off < bytes; off += page_bytes) {
    touch[off] = 0;
  }
  touch[bytes - 1U] = 0;
  __asm__ volatile("fence rw, rw" ::: "memory");
  PRT_PROGRESS_LOG("%s after-prefault ptr=%p size=%zu page_bytes=%zu",
                   tag, buf, bytes, page_bytes);

  PRT_PROGRESS_LOG("%s before-mlock ptr=%p size=%zu", tag, buf, bytes);
  mlock_rc = mlock(buf, bytes);
  if (mlock_rc != 0) mlock_errno = errno;
  PRT_PROGRESS_LOG("%s after-mlock rc=%d errno=%d ptr=%p size=%zu",
                   tag, mlock_rc, mlock_errno, buf, bytes);
  if (mlock_rc != 0) return PRT_ERR_IO;

  return PRT_OK;
}
#endif

static int dma_completion_pool_init(prt_runtime_t *rt) {
  if (!rt) return PRT_ERR_INVAL;
  if (rt->dma_completion_flags && rt->dma_completion_flag_pas &&
      rt->dma_completion_flag_used && rt->dma_completion_flag_count != 0U) {
    return PRT_OK;
  }

#if defined(__linux__) && defined(__riscv)
  {
    const size_t host_page_bytes = prt_host_page_size_bytes();
    const uint32_t count = PRT_DMA_COMPLETION_SLOT_COUNT_DEFAULT;
    const size_t slot_bytes = (size_t)count * sizeof(*rt->dma_completion_flags);
    const size_t alloc_bytes =
      host_page_bytes == 0U
        ? slot_bytes
        : ((slot_bytes + host_page_bytes - 1U) / host_page_bytes) * host_page_bytes;
    volatile uint32_t *flags = NULL;
    uint64_t *pas = NULL;
    uint8_t *used = NULL;
    void *alloc = NULL;
    int rc = PRT_OK;

    if (host_page_bytes == 0U) return PRT_ERR_STATE;
    if (pthread_mutex_init(&rt->dma_completion_lock, NULL) != 0) return PRT_ERR_STATE;
    if (posix_memalign(&alloc, host_page_bytes, alloc_bytes) != 0) {
      pthread_mutex_destroy(&rt->dma_completion_lock);
      return PRT_ERR_NOMEM;
    }
    memset(alloc, 0, alloc_bytes);
    pas = (uint64_t *)calloc(count, sizeof(*pas));
    used = (uint8_t *)calloc(count, sizeof(*used));
    if (!pas || !used) {
      free((void *)pas);
      free((void *)used);
      free(alloc);
      pthread_mutex_destroy(&rt->dma_completion_lock);
      return PRT_ERR_NOMEM;
    }
    rc = dma_prefault_and_lock_buffer("dma-completion-pool", alloc, alloc_bytes);
    if (rc != PRT_OK) {
      free((void *)pas);
      free((void *)used);
      free(alloc);
      pthread_mutex_destroy(&rt->dma_completion_lock);
      return rc;
    }

    flags = (volatile uint32_t *)alloc;
    for (uint32_t i = 0; i < count; ++i) {
      uint64_t pa = 0;
      rc = prt_host_virt_to_phys((const void *)&flags[i], &pa);
      if (rc != PRT_OK) {
        (void)munlock((const void *)alloc, alloc_bytes);
        free((void *)pas);
        free((void *)used);
        free(alloc);
        pthread_mutex_destroy(&rt->dma_completion_lock);
        return rc;
      }
      pas[i] = pa;
    }

    rt->dma_completion_flags = flags;
    rt->dma_completion_flag_pas = pas;
    rt->dma_completion_flag_used = used;
    rt->dma_completion_flag_count = count;
    rt->dma_completion_flag_alloc_bytes = alloc_bytes;
    PRT_PROGRESS_LOG("dma-completion-pool ready slots=%u bytes=%zu va=%p pa0=0x%llx palast=0x%llx",
                     count, alloc_bytes, (const void *)flags,
                     (unsigned long long)pas[0],
                     (unsigned long long)pas[count - 1U]);
    return PRT_OK;
  }
#else
  if (pthread_mutex_init(&rt->dma_completion_lock, NULL) != 0) return PRT_ERR_STATE;
  rt->dma_completion_flag_count = PRT_DMA_COMPLETION_SLOT_COUNT_DEFAULT;
  return PRT_OK;
#endif
}

static void dma_completion_pool_destroy(prt_runtime_t *rt) {
  if (!rt) return;
  if (rt->dma_completion_flags && rt->dma_completion_flag_alloc_bytes != 0U) {
#if defined(__linux__) && defined(__riscv)
    (void)munlock((const void *)rt->dma_completion_flags, rt->dma_completion_flag_alloc_bytes);
#endif
    free((void *)rt->dma_completion_flags);
  }
  free(rt->dma_completion_flag_pas);
  free(rt->dma_completion_flag_used);
  rt->dma_completion_flags = NULL;
  rt->dma_completion_flag_pas = NULL;
  rt->dma_completion_flag_used = NULL;
  rt->dma_completion_flag_count = 0U;
  rt->dma_completion_flag_alloc_bytes = 0U;
  pthread_mutex_destroy(&rt->dma_completion_lock);
}

static int dma_completion_flag_acquire(prt_runtime_t *rt, prt_dma_token_t *tok) {
  int rc;
  int lock_rc = 0;
  int lock_held = 0;
  uint32_t slot = UINT32_MAX;
  const int checkpoint_doneflag = dma_should_checkpoint_doneflag_tok(tok);
  if (!rt || !tok) return PRT_ERR_INVAL;
  if (checkpoint_doneflag) {
    PRT_CHECKPOINT_LOG("dma stage=%u checkpoint=doneflag-acquire-enter tensor=%u token=%u",
                       tok->stage_idx, tok->tensor_id, tok->id);
  }
  rc = dma_completion_pool_init(rt);
  if (checkpoint_doneflag) {
    PRT_CHECKPOINT_LOG("dma stage=%u checkpoint=doneflag-acquire-pool tensor=%u token=%u rc=%d slots=%u flags=%p used=%p",
                       tok->stage_idx, tok->tensor_id, tok->id, rc,
                       rt->dma_completion_flag_count,
                       (const void *)rt->dma_completion_flags,
                       (const void *)rt->dma_completion_flag_used);
  }
  if (rc != PRT_OK) return rc;

  if (!checkpoint_doneflag) {
    pthread_mutex_lock(&rt->dma_completion_lock);
    lock_held = 1;
  } else {
    lock_rc = pthread_mutex_trylock(&rt->dma_completion_lock);
    if (lock_rc == 0) {
      lock_held = 1;
    } else if (lock_rc == EBUSY) {
      PRT_CHECKPOINT_LOG("dma stage=%u checkpoint=doneflag-acquire-lock tensor=%u token=%u mode=trylock rc=%d",
                         tok->stage_idx, tok->tensor_id, tok->id, lock_rc);
      pthread_mutex_lock(&rt->dma_completion_lock);
      lock_held = 1;
    } else {
      PRT_CHECKPOINT_LOG("dma stage=%u checkpoint=doneflag-acquire-lock tensor=%u token=%u mode=trylock rc=%d",
                         tok->stage_idx, tok->tensor_id, tok->id, lock_rc);
      return PRT_ERR_STATE;
    }
    PRT_CHECKPOINT_LOG("dma stage=%u checkpoint=doneflag-acquire-lock tensor=%u token=%u mode=held rc=%d",
                       tok->stage_idx, tok->tensor_id, tok->id, lock_rc);
  }

  for (uint32_t i = 0; i < rt->dma_completion_flag_count; ++i) {
    if (rt->dma_completion_flag_used[i] == 0U) {
      rt->dma_completion_flag_used[i] = 1U;
      slot = i;
      break;
    }
  }
  if (checkpoint_doneflag) {
    PRT_CHECKPOINT_LOG("dma stage=%u checkpoint=doneflag-acquire-scan tensor=%u token=%u slot=%u",
                       tok->stage_idx, tok->tensor_id, tok->id, slot);
  }
  if (lock_held) pthread_mutex_unlock(&rt->dma_completion_lock);

  if (slot == UINT32_MAX) return PRT_ERR_NOMEM;

  tok->completion_slot = slot;
  tok->completion_flag = &rt->dma_completion_flags[slot];
  *tok->completion_flag = 0U;
  dma_cpu_fence_rw();
  tok->hw_done_flag = 0;
  tok->debug_done_flag_va = (uint64_t)(uintptr_t)tok->completion_flag;
  tok->debug_done_flag_pa = rt->dma_completion_flag_pas ? rt->dma_completion_flag_pas[slot] : 0ULL;
  tok->hw_done_flag_pa_rc = tok->debug_done_flag_pa != 0ULL ? PRT_OK : PRT_ERR_STATE;
  if (checkpoint_doneflag) {
    PRT_CHECKPOINT_LOG("dma stage=%u checkpoint=doneflag-acquire-exit tensor=%u token=%u slot=%u rc=%d done_pa=0x%llx",
                       tok->stage_idx, tok->tensor_id, tok->id,
                       tok->completion_slot, tok->hw_done_flag_pa_rc,
                       (unsigned long long)tok->debug_done_flag_pa);
  }
  return tok->hw_done_flag_pa_rc;
}

static void dma_completion_flag_release(prt_dma_token_t *tok) {
  prt_runtime_t *rt;
  if (!tok) return;
  rt = tok->owner_rt;
  if (!rt || tok->completion_slot == UINT32_MAX) return;
  if (tok->completion_flag) {
    *tok->completion_flag = 0U;
    dma_cpu_fence_rw();
  }
  if (rt->dma_completion_flag_used && tok->completion_slot < rt->dma_completion_flag_count) {
    pthread_mutex_lock(&rt->dma_completion_lock);
    rt->dma_completion_flag_used[tok->completion_slot] = 0U;
    pthread_mutex_unlock(&rt->dma_completion_lock);
  }
  tok->completion_flag = NULL;
  tok->completion_slot = UINT32_MAX;
}

static int dma_completion_flag_value(const prt_dma_token_t *tok) {
  if (!tok) return 0;
  if (!tok->completion_flag) return tok->hw_done_flag;
  return *tok->completion_flag != 0U;
}

static int dma_completion_flag_refresh(prt_dma_token_t *tok) {
  int value;
  if (!tok) return 0;
  value = dma_completion_flag_value(tok);
  tok->hw_done_flag = value;
  return value;
}

#if defined(__linux__) && defined(__riscv)
static int dma_copy_host_to_spm_pages_linux(prt_runtime_t *rt, const prt_page_list_t *dst_pages,
                                            const uint8_t *src_host, uint32_t manager_id,
                                            uint32_t stage_idx, uint32_t tensor_id,
                                            uint64_t timeout_ns, uint64_t total_bytes) {
  prt_dma_req_t req;
  prt_rr_scope_t scope;
  const size_t host_page_bytes = prt_host_page_size_bytes();
  const uint64_t page_bytes = rt->cfg.page_size_bytes ? rt->cfg.page_size_bytes : PRT_PAGE_SIZE_BYTES;
  uint64_t remaining = total_bytes;
  int rc = PRT_OK;

  if (!rt || !dst_pages || !dst_pages->data || dst_pages->size == 0 || !src_host || total_bytes == 0U) {
    return PRT_ERR_INVAL;
  }
  if (host_page_bytes == 0) return PRT_ERR_STATE;

  memset(&scope, 0, sizeof(scope));
  req.src_acc = manager_id;
  req.dst_acc = manager_id;
  rc = dma_batch_scope_acquire(rt, stage_idx, manager_id, 2U, &scope);
  if (rc != PRT_OK) return rc;

  for (uint32_t i = 0; i < dst_pages->size && remaining > 0U; ++i) {
    const uint64_t dst_page_base = page_base_addr(rt, &dst_pages->data[i]);
    uint64_t copied = 0;

    while (copied < page_bytes && remaining > 0U) {
      const uint64_t src_off = (uint64_t)i * page_bytes + copied;
      const uint8_t *src_ptr = src_host + (size_t)src_off;
      const size_t host_page_off = (size_t)((uintptr_t)src_ptr % (uintptr_t)host_page_bytes);
      const uint64_t host_page_room = (uint64_t)(host_page_bytes - host_page_off);
      const uint64_t dst_addr = dst_page_base + copied;
      const int fixed_page_probe =
        dma_should_sparse_fixed_load_page_probe(stage_idx, tensor_id, i, dst_pages->size, copied);
      uint64_t chunk = dma_min_u64(page_bytes - copied, host_page_room);
      chunk = dma_min_u64(chunk, remaining);
      uint64_t src_pa = 0;
      int used_bounce = 0;
      const int bounce_needed = dma_chunk_needs_bounce((uint64_t)(uintptr_t)src_ptr, dst_addr, chunk);
      dma_trigger_fixed_load_host("pset", stage_idx, manager_id, tensor_id, i, PRT_OK);
      if (fixed_page_probe) {
        PRT_PROGRESS_LOG("dma-fixed-load-host stage=%u tensor=%u phase=page-setup page=%u/%u copied=%llu chunk=%llu src_va=0x%llx dst=0x%llx host_page_off=%zu host_page_room=%llu bounce_need=%u",
                         stage_idx, tensor_id, i, dst_pages->size,
                         (unsigned long long)copied,
                         (unsigned long long)chunk,
                         (unsigned long long)(uintptr_t)src_ptr,
                         (unsigned long long)dst_addr,
                         host_page_off,
                         (unsigned long long)host_page_room,
                         (uint32_t)bounce_needed);
      }
      if (bounce_needed) {
        uint8_t *bounce_ptr = NULL;
        uint64_t bounce_room = 0;
        used_bounce = 1;
        if (fixed_page_probe) {
          PRT_PROGRESS_LOG("dma-fixed-load-host stage=%u tensor=%u phase=bounce-begin page=%u/%u copied=%llu chunk=%llu dst=0x%llx align_mod64=%llu",
                           stage_idx, tensor_id, i, dst_pages->size,
                           (unsigned long long)copied,
                           (unsigned long long)chunk,
                           (unsigned long long)dst_addr,
                           (unsigned long long)dma_debug_mod64(dst_addr));
        }
        rc = dma_stage_bounce_region(rt, stage_idx, dma_debug_mod64(dst_addr), &bounce_ptr, &bounce_room);
        if (fixed_page_probe) {
          PRT_PROGRESS_LOG("dma-fixed-load-host stage=%u tensor=%u phase=bounce-end page=%u/%u copied=%llu rc=%d bounce_ptr=0x%llx bounce_room=%llu",
                           stage_idx, tensor_id, i, dst_pages->size,
                           (unsigned long long)copied,
                           rc,
                           (unsigned long long)(uintptr_t)bounce_ptr,
                           (unsigned long long)bounce_room);
        }
        if (rc != PRT_OK) goto done;
        chunk = dma_min_u64(chunk, bounce_room);
        if (dma_bounce_bypass_enabled()) {
          if (fixed_page_probe) {
            PRT_PROGRESS_LOG("dma-fixed-load-host stage=%u tensor=%u phase=bounce-bypass page=%u/%u copied=%llu chunk=%llu src_va=0x%llx dst=0x%llx bounce_ptr=0x%llx",
                             stage_idx, tensor_id, i, dst_pages->size,
                             (unsigned long long)copied,
                             (unsigned long long)chunk,
                             (unsigned long long)(uintptr_t)src_ptr,
                             (unsigned long long)dst_addr,
                             (unsigned long long)(uintptr_t)bounce_ptr);
          }
          copied += chunk;
          remaining -= chunk;
          continue;
        }
        memcpy(bounce_ptr, src_ptr, (size_t)chunk);
        dma_trigger_fixed_load_host("v2p-b", stage_idx, manager_id, tensor_id, i, PRT_OK);
        if (fixed_page_probe) {
          PRT_PROGRESS_LOG("dma-fixed-load-host stage=%u tensor=%u phase=v2p-begin page=%u/%u copied=%llu src_kind=bounce src_va=0x%llx chunk=%llu",
                           stage_idx, tensor_id, i, dst_pages->size,
                           (unsigned long long)copied,
                           (unsigned long long)(uintptr_t)bounce_ptr,
                           (unsigned long long)chunk);
        }
        prt_host_virt_to_phys_debug_scope_push("dma-fixed-load-bounce-src");
        rc = prt_host_virt_to_phys((const void *)bounce_ptr, &src_pa);
        prt_host_virt_to_phys_debug_scope_pop();
        dma_trigger_fixed_load_host("v2p-e", stage_idx, manager_id, tensor_id, i, rc);
        if (fixed_page_probe) {
          PRT_PROGRESS_LOG("dma-fixed-load-host stage=%u tensor=%u phase=v2p-end page=%u/%u copied=%llu src_kind=bounce src_va=0x%llx src_pa=0x%llx chunk=%llu rc=%d",
                           stage_idx, tensor_id, i, dst_pages->size,
                           (unsigned long long)copied,
                           (unsigned long long)(uintptr_t)bounce_ptr,
                           (unsigned long long)src_pa,
                           (unsigned long long)chunk,
                           rc);
        }
      } else {
        dma_trigger_fixed_load_host("v2p-b", stage_idx, manager_id, tensor_id, i, PRT_OK);
        if (fixed_page_probe) {
          PRT_PROGRESS_LOG("dma-fixed-load-host stage=%u tensor=%u phase=v2p-begin page=%u/%u copied=%llu src_kind=direct src_va=0x%llx chunk=%llu",
                           stage_idx, tensor_id, i, dst_pages->size,
                           (unsigned long long)copied,
                           (unsigned long long)(uintptr_t)src_ptr,
                           (unsigned long long)chunk);
        }
        prt_host_virt_to_phys_debug_scope_push("dma-fixed-load-direct-src");
        rc = prt_host_virt_to_phys((const void *)src_ptr, &src_pa);
        prt_host_virt_to_phys_debug_scope_pop();
        dma_trigger_fixed_load_host("v2p-e", stage_idx, manager_id, tensor_id, i, rc);
        if (fixed_page_probe) {
          PRT_PROGRESS_LOG("dma-fixed-load-host stage=%u tensor=%u phase=v2p-end page=%u/%u copied=%llu src_kind=direct src_va=0x%llx src_pa=0x%llx chunk=%llu rc=%d",
                           stage_idx, tensor_id, i, dst_pages->size,
                           (unsigned long long)copied,
                           (unsigned long long)(uintptr_t)src_ptr,
                           (unsigned long long)src_pa,
                           (unsigned long long)chunk,
                           rc);
        }
      }
      if (rc != PRT_OK) goto done;

      req.src_addr = src_pa;
      req.dst_addr = dst_addr;
      req.bytes = chunk;
      dma_trigger_fixed_load_host("sw-b", stage_idx, manager_id, tensor_id, i, PRT_OK);
      if (fixed_page_probe) {
        PRT_PROGRESS_LOG("dma-fixed-load-host stage=%u tensor=%u phase=submitwait-begin page=%u/%u copied=%llu chunk=%llu src_va=0x%llx src_pa=0x%llx dst=0x%llx bounce=%u",
                         stage_idx, tensor_id, i, dst_pages->size,
                         (unsigned long long)copied,
                         (unsigned long long)chunk,
                         (unsigned long long)(uintptr_t)src_ptr,
                         (unsigned long long)src_pa,
                         (unsigned long long)dst_addr,
                         (uint32_t)used_bounce);
      }
      if (tensor_id >= 1000000U && chunk != page_bytes) {
        PRT_MARKER_LOG("dma-fixed-load-chunk stage=%u tensor=%u page=%u copied=%llu chunk=%llu page_bytes=%llu src_va=0x%llx src_pa=0x%llx dst=0x%llx host_page_off=%zu host_page_room=%llu bounce=%u begin",
                       stage_idx, tensor_id, i,
                       (unsigned long long)copied,
                       (unsigned long long)chunk,
                       (unsigned long long)page_bytes,
                       (unsigned long long)(uintptr_t)src_ptr,
                       (unsigned long long)src_pa,
                       (unsigned long long)dst_addr,
                       host_page_off,
                       (unsigned long long)host_page_room,
                       (uint32_t)used_bounce);
      }
      dma_breadcrumb_page_begin(tensor_id, manager_id, i, &req,
                                used_bounce ? PRT_BREADCRUMB_FLAG_BOUNCE : 0U,
                                timeout_ns);
      rc = dma_submit_wait_annotated_scoped(rt, &req, stage_idx, tensor_id, timeout_ns, &scope, 0);
      dma_breadcrumb_page_end(tensor_id, manager_id, i, &req, rc,
                              used_bounce ? PRT_BREADCRUMB_FLAG_BOUNCE : 0U,
                              timeout_ns);
      dma_trigger_fixed_load_host("sw-e", stage_idx, manager_id, tensor_id, i, rc);
      if (fixed_page_probe) {
        PRT_PROGRESS_LOG("dma-fixed-load-host stage=%u tensor=%u phase=submitwait-end page=%u/%u copied=%llu chunk=%llu src_pa=0x%llx dst=0x%llx bounce=%u rc=%d",
                         stage_idx, tensor_id, i, dst_pages->size,
                         (unsigned long long)copied,
                         (unsigned long long)chunk,
                         (unsigned long long)src_pa,
                         (unsigned long long)dst_addr,
                         (uint32_t)used_bounce,
                         rc);
      }
      if (tensor_id >= 1000000U && chunk != page_bytes) {
        PRT_MARKER_LOG("dma-fixed-load-chunk stage=%u tensor=%u page=%u copied=%llu chunk=%llu page_bytes=%llu src_pa=0x%llx dst=0x%llx bounce=%u rc=%d end",
                       stage_idx, tensor_id, i,
                       (unsigned long long)copied,
                       (unsigned long long)chunk,
                       (unsigned long long)page_bytes,
                       (unsigned long long)src_pa,
                       (unsigned long long)dst_addr,
                       (uint32_t)used_bounce,
                       rc);
      }
      if (rc != PRT_OK) goto done;
      copied += chunk;
      remaining -= chunk;
    }
  }

done:
  dma_batch_scope_release(&scope);
  return rc;
}

static int dma_copy_spm_pages_to_host_linux(prt_runtime_t *rt, uint8_t *dst_host,
                                            const prt_page_list_t *src_pages, uint32_t manager_id,
                                            uint32_t stage_idx, uint32_t tensor_id,
                                            uint64_t timeout_ns, uint64_t total_bytes) {
  prt_dma_req_t req;
  prt_rr_scope_t scope;
  const size_t host_page_bytes = prt_host_page_size_bytes();
  const uint64_t page_bytes = rt->cfg.page_size_bytes ? rt->cfg.page_size_bytes : PRT_PAGE_SIZE_BYTES;
  uint64_t remaining = total_bytes;
  int rc = PRT_OK;
  uint32_t tracerv_release_page_idx = UINT32_MAX;
  uint64_t tracerv_release_copied_bytes = 0ULL;

  if (!rt || !src_pages || !src_pages->data || src_pages->size == 0 || !dst_host || total_bytes == 0U) {
    return PRT_ERR_INVAL;
  }
  if (host_page_bytes == 0) return PRT_ERR_STATE;

  memset(&scope, 0, sizeof(scope));
  req.src_acc = manager_id;
  req.dst_acc = manager_id;
  rc = dma_batch_scope_acquire(rt, stage_idx, manager_id, 2U, &scope);
  if (rc != PRT_OK) return rc;

  for (uint32_t i = 0; i < src_pages->size && remaining > 0U; ++i) {
    const uint64_t src_page_base = page_base_addr(rt, &src_pages->data[i]);
    uint64_t copied = 0;

    while (copied < page_bytes && remaining > 0U) {
      const uint64_t dst_off = (uint64_t)i * page_bytes + copied;
      uint8_t *dst_ptr = dst_host + (size_t)dst_off;
      const size_t host_page_off = (size_t)((uintptr_t)dst_ptr % (uintptr_t)host_page_bytes);
      const uint64_t host_page_room = (uint64_t)(host_page_bytes - host_page_off);
      const uint64_t src_addr = src_page_base + copied;
      uint64_t chunk = dma_min_u64(page_bytes - copied, host_page_room);
      chunk = dma_min_u64(chunk, remaining);
      uint64_t dst_pa = 0;
      int used_bounce = 0;
      const int first_chunk_probe = stage_idx == 0U && tensor_id == 2U && i == 0U && copied == 0ULL;
      const int page_probe = dma_should_sparse_export_page_probe(stage_idx, tensor_id,
                                                                 i, src_pages->size, copied);
      const int log_chunk_marker =
        dma_should_log_export_chunk_marker(i, src_pages->size, copied);
      dma_trigger_export_host("pset", stage_idx, manager_id, tensor_id, i, PRT_OK);
      if (log_chunk_marker) {
        PRT_MARKER_LOG("dma-export-chunk stage=%u tensor=%u page=%u copied=%llu chunk=%llu page_bytes=%llu src=0x%llx dst_va=0x%llx host_page_off=%zu host_page_room=%llu bounce=%u begin",
                       stage_idx, tensor_id, i,
                       (unsigned long long)copied,
                       (unsigned long long)chunk,
                       (unsigned long long)page_bytes,
                       (unsigned long long)src_addr,
                       (unsigned long long)(uintptr_t)dst_ptr,
                       host_page_off,
                       (unsigned long long)host_page_room,
                       0U);
      }
      if (dma_chunk_needs_bounce(src_addr, (uint64_t)(uintptr_t)dst_ptr, chunk)) {
        uint8_t *bounce_ptr = NULL;
        uint64_t bounce_room = 0;
        used_bounce = 1;
        rc = dma_stage_bounce_region(rt, stage_idx, dma_debug_mod64(src_addr), &bounce_ptr, &bounce_room);
        if (rc != PRT_OK) goto done;
        chunk = dma_min_u64(chunk, bounce_room);
        if (dma_bounce_bypass_enabled()) {
          req.src_addr = src_addr;
          req.dst_addr = (uint64_t)(uintptr_t)dst_ptr;
          req.bytes = chunk;
          if (page_probe) {
            PRT_PROGRESS_LOG("dma-export-host stage=%u tensor=%u phase=bounce-bypass page=%u/%u copied=%llu chunk=%llu src=0x%llx dst_va=0x%llx bounce_ptr=0x%llx bounce=%u",
                             stage_idx, tensor_id, i, src_pages->size,
                             (unsigned long long)copied,
                             (unsigned long long)chunk,
                             (unsigned long long)src_addr,
                             (unsigned long long)(uintptr_t)dst_ptr,
                             (unsigned long long)(uintptr_t)bounce_ptr,
                             (uint32_t)used_bounce);
          }
          if (first_chunk_probe) {
            PRT_PROGRESS_LOG("dma-export-host stage=%u tensor=%u phase=first-chunk-bounce-bypass page=%u copied=%llu chunk=%llu src=0x%llx dst_va=0x%llx bounce=%u",
                             stage_idx, tensor_id, i,
                             (unsigned long long)copied,
                             (unsigned long long)chunk,
                             (unsigned long long)src_addr,
                             (unsigned long long)(uintptr_t)dst_ptr,
                             (uint32_t)used_bounce);
          }
          dma_breadcrumb_export_loop_phase(stage_idx, tensor_id, manager_id, i, &req, PRT_OK,
                                           PRT_BREADCRUMB_FLAG_BOUNCE,
                                           timeout_ns,
                                           PRT_BREADCRUMB_PHASE_DMA_PAGE_AFTER_ACCOUNTING,
                                           __LINE__);
          copied += chunk;
          remaining -= chunk;
          continue;
        }
        req.src_addr = src_addr;
        req.dst_addr = (uint64_t)(uintptr_t)bounce_ptr;
        req.bytes = chunk;
        dma_trigger_export_host("v2p-b", stage_idx, manager_id, tensor_id, i, PRT_OK);
        dma_breadcrumb_export_loop_phase(stage_idx, tensor_id, manager_id, i, &req, PRT_OK,
                                         used_bounce ? PRT_BREADCRUMB_FLAG_BOUNCE : 0U,
                                         timeout_ns,
                                         PRT_BREADCRUMB_PHASE_DMA_PAGE_BEFORE_V2P,
                                         __LINE__);
        prt_host_virt_to_phys_debug_scope_push("dma-export-bounce-dst");
        rc = prt_host_virt_to_phys((const void *)bounce_ptr, &dst_pa);
        prt_host_virt_to_phys_debug_scope_pop();
        req.dst_addr = dst_pa;
        dma_trigger_export_host("v2p-e", stage_idx, manager_id, tensor_id, i, rc);
        dma_breadcrumb_export_loop_phase(stage_idx, tensor_id, manager_id, i, &req, rc,
                                         used_bounce ? PRT_BREADCRUMB_FLAG_BOUNCE : 0U,
                                         timeout_ns,
                                         PRT_BREADCRUMB_PHASE_DMA_PAGE_AFTER_V2P,
                                         __LINE__);
        if (rc != PRT_OK) goto done;
        req.src_addr = src_addr;
        req.dst_addr = dst_pa;
        req.bytes = chunk;
        dma_trigger_export_host("sw-b", stage_idx, manager_id, tensor_id, i, PRT_OK);
        if (page_probe) {
          PRT_PROGRESS_LOG("dma-export-host stage=%u tensor=%u phase=page-submitwait-begin page=%u/%u copied=%llu chunk=%llu src=0x%llx dst_va=0x%llx dst_pa=0x%llx bounce=%u",
                           stage_idx, tensor_id, i, src_pages->size,
                           (unsigned long long)copied,
                           (unsigned long long)chunk,
                           (unsigned long long)src_addr,
                           (unsigned long long)(uintptr_t)dst_ptr,
                           (unsigned long long)dst_pa,
                           (uint32_t)used_bounce);
        }
        if (first_chunk_probe) {
          PRT_PROGRESS_LOG("dma-export-host stage=%u tensor=%u phase=first-chunk-submitwait-begin page=%u copied=%llu chunk=%llu src=0x%llx dst_va=0x%llx dst_pa=0x%llx bounce=%u",
                           stage_idx, tensor_id, i,
                           (unsigned long long)copied,
                           (unsigned long long)chunk,
                           (unsigned long long)src_addr,
                           (unsigned long long)(uintptr_t)dst_ptr,
                           (unsigned long long)dst_pa,
                           (uint32_t)used_bounce);
        }
        if (log_chunk_marker) {
          PRT_MARKER_LOG("dma-export-chunk stage=%u tensor=%u page=%u copied=%llu chunk=%llu src=0x%llx dst_va=0x%llx dst_pa=0x%llx bounce=%u submit-begin",
                         stage_idx, tensor_id, i,
                         (unsigned long long)copied,
                         (unsigned long long)chunk,
                         (unsigned long long)src_addr,
                         (unsigned long long)(uintptr_t)dst_ptr,
                         (unsigned long long)dst_pa,
                         (uint32_t)used_bounce);
        }
        dma_breadcrumb_page_begin(tensor_id, manager_id, i, &req,
                                  used_bounce ? PRT_BREADCRUMB_FLAG_BOUNCE : 0U,
                                  timeout_ns);
        prt_gdb_marker_note(PRT_GDB_MARKER_SITE_DMA_EXPORT_PAGE_SUBMIT_BEGIN,
                            PRT_DEBUG_U32_NONE, PRT_DEBUG_U32_NONE, stage_idx,
                            PRT_DEBUG_U32_NONE, manager_id, tensor_id, i,
                            prt_breadcrumb_get_export_target_token(), PRT_OK,
                            copied, chunk, __LINE__);
        if (dma_tracerv_dma_window_target_match(stage_idx, tensor_id, i, copied)) {
          tracerv_release_page_idx = i;
          tracerv_release_copied_bytes = copied;
        }
        dma_tracerv_dma_window_scope_push(i, copied);
        rc = dma_submit_wait_annotated_scoped(rt, &req, stage_idx, tensor_id, timeout_ns, &scope,
                                              dma_export_probe_enabled() && (page_probe || first_chunk_probe));
        dma_tracerv_dma_window_scope_pop();
        dma_breadcrumb_export_loop_phase(stage_idx, tensor_id, manager_id, i, &req, rc,
                                         used_bounce ? PRT_BREADCRUMB_FLAG_BOUNCE : 0U,
                                         timeout_ns,
                                         PRT_BREADCRUMB_PHASE_DMA_PAGE_AFTER_SUBMITWAIT_RETURN,
                                         __LINE__);
        dma_breadcrumb_page_end(tensor_id, manager_id, i, &req, rc,
                                used_bounce ? PRT_BREADCRUMB_FLAG_BOUNCE : 0U,
                                timeout_ns);
        prt_gdb_marker_note(PRT_GDB_MARKER_SITE_DMA_EXPORT_PAGE_SUBMIT_END,
                            PRT_DEBUG_U32_NONE, PRT_DEBUG_U32_NONE, stage_idx,
                            PRT_DEBUG_U32_NONE, manager_id, tensor_id, i,
                            prt_breadcrumb_get_export_target_token(), rc,
                            copied, chunk, __LINE__);
        dma_trigger_export_host("sw-e", stage_idx, manager_id, tensor_id, i, rc);
        dma_breadcrumb_export_loop_phase(stage_idx, tensor_id, manager_id, i, &req, rc,
                                         used_bounce ? PRT_BREADCRUMB_FLAG_BOUNCE : 0U,
                                         timeout_ns,
                                         PRT_BREADCRUMB_PHASE_DMA_PAGE_AFTER_PAGE_END,
                                         __LINE__);
        if (page_probe) {
          PRT_PROGRESS_LOG("dma-export-host stage=%u tensor=%u phase=page-submitwait-end page=%u/%u copied=%llu chunk=%llu rc=%d bounce=%u",
                           stage_idx, tensor_id, i, src_pages->size,
                           (unsigned long long)copied,
                           (unsigned long long)chunk,
                           rc,
                           (uint32_t)used_bounce);
        }
        if (first_chunk_probe) {
          PRT_PROGRESS_LOG("dma-export-host stage=%u tensor=%u phase=first-chunk-submitwait-end page=%u copied=%llu chunk=%llu rc=%d bounce=%u",
                           stage_idx, tensor_id, i,
                           (unsigned long long)copied,
                           (unsigned long long)chunk,
                           rc,
                           (uint32_t)used_bounce);
        }
        if (rc != PRT_OK) {
          PRT_MARKER_LOG("dma-export fail stage=%u tensor=%u page=%u copied=%llu chunk=%llu bounce=1 timeout_ms=%llu src=0x%llx dst_pa=0x%llx dst_off=%llu rc=%d",
                         stage_idx, tensor_id, i,
                         (unsigned long long)copied,
                         (unsigned long long)chunk,
                         (unsigned long long)(timeout_ns / 1000000ULL),
                         (unsigned long long)src_addr,
                         (unsigned long long)dst_pa,
                         (unsigned long long)dst_off,
                         rc);
          goto done;
        }
        if (log_chunk_marker) {
          PRT_MARKER_LOG("dma-export-chunk stage=%u tensor=%u page=%u copied=%llu chunk=%llu src=0x%llx dst_pa=0x%llx bounce=%u rc=%d end",
                         stage_idx, tensor_id, i,
                         (unsigned long long)copied,
                         (unsigned long long)chunk,
                         (unsigned long long)src_addr,
                         (unsigned long long)dst_pa,
                         (uint32_t)used_bounce,
                         rc);
        }
        memcpy(dst_ptr, bounce_ptr, (size_t)chunk);
        copied += chunk;
        remaining -= chunk;
        dma_breadcrumb_export_loop_phase(stage_idx, tensor_id, manager_id, i, &req, rc,
                                         used_bounce ? PRT_BREADCRUMB_FLAG_BOUNCE : 0U,
                                         timeout_ns,
                                         PRT_BREADCRUMB_PHASE_DMA_PAGE_AFTER_ACCOUNTING,
                                         __LINE__);
        continue;
      }
      req.src_addr = src_addr;
      req.dst_addr = (uint64_t)(uintptr_t)dst_ptr;
      req.bytes = chunk;
      dma_breadcrumb_export_loop_phase_token(stage_idx, tensor_id, manager_id, i, &req, PRT_OK,
                                             0U,
                                             timeout_ns,
                                             prt_breadcrumb_get_export_target_token(),
                                             PRT_BREADCRUMB_PHASE_DMA_PAGE_DIRECT_PATH_DECIDED,
                                             __LINE__);
      if (page_probe) {
        PRT_PROGRESS_LOG("dma-export-host stage=%u tensor=%u phase=v2p-begin page=%u/%u copied=%llu chunk=%llu dst_va=0x%llx bounce=%u",
                         stage_idx, tensor_id, i, src_pages->size,
                         (unsigned long long)copied,
                         (unsigned long long)chunk,
                         (unsigned long long)(uintptr_t)dst_ptr,
                         (uint32_t)used_bounce);
      }
      dma_trigger_export_host("v2p-b", stage_idx, manager_id, tensor_id, i, PRT_OK);
      dma_breadcrumb_export_loop_phase(stage_idx, tensor_id, manager_id, i, &req, PRT_OK,
                                       used_bounce ? PRT_BREADCRUMB_FLAG_BOUNCE : 0U,
                                       timeout_ns,
                                       PRT_BREADCRUMB_PHASE_DMA_PAGE_BEFORE_V2P,
                                       __LINE__);
      prt_host_virt_to_phys_debug_scope_push("dma-export-direct-dst");
      rc = prt_host_virt_to_phys((const void *)dst_ptr, &dst_pa);
      prt_host_virt_to_phys_debug_scope_pop();
      req.dst_addr = dst_pa;
      dma_trigger_export_host("v2p-e", stage_idx, manager_id, tensor_id, i, rc);
      dma_breadcrumb_export_loop_phase(stage_idx, tensor_id, manager_id, i, &req, rc,
                                       used_bounce ? PRT_BREADCRUMB_FLAG_BOUNCE : 0U,
                                       timeout_ns,
                                       PRT_BREADCRUMB_PHASE_DMA_PAGE_AFTER_V2P,
                                       __LINE__);
      if (page_probe) {
        PRT_PROGRESS_LOG("dma-export-host stage=%u tensor=%u phase=v2p-end page=%u/%u copied=%llu chunk=%llu dst_va=0x%llx dst_pa=0x%llx rc=%d bounce=%u",
                         stage_idx, tensor_id, i, src_pages->size,
                         (unsigned long long)copied,
                         (unsigned long long)chunk,
                         (unsigned long long)(uintptr_t)dst_ptr,
                         (unsigned long long)dst_pa,
                         rc,
                         (uint32_t)used_bounce);
      }
      if (rc != PRT_OK) goto done;

      req.src_addr = src_addr;
      req.dst_addr = dst_pa;
      req.bytes = chunk;
      dma_trigger_export_host("sw-b", stage_idx, manager_id, tensor_id, i, PRT_OK);
      if (page_probe) {
        PRT_PROGRESS_LOG("dma-export-host stage=%u tensor=%u phase=page-submitwait-begin page=%u/%u copied=%llu chunk=%llu src=0x%llx dst_va=0x%llx dst_pa=0x%llx bounce=%u",
                         stage_idx, tensor_id, i, src_pages->size,
                         (unsigned long long)copied,
                         (unsigned long long)chunk,
                         (unsigned long long)src_addr,
                         (unsigned long long)(uintptr_t)dst_ptr,
                         (unsigned long long)dst_pa,
                         (uint32_t)used_bounce);
      }
      if (first_chunk_probe) {
        PRT_PROGRESS_LOG("dma-export-host stage=%u tensor=%u phase=first-chunk-submitwait-begin page=%u copied=%llu chunk=%llu src=0x%llx dst_va=0x%llx dst_pa=0x%llx bounce=%u",
                         stage_idx, tensor_id, i,
                         (unsigned long long)copied,
                         (unsigned long long)chunk,
                         (unsigned long long)src_addr,
                         (unsigned long long)(uintptr_t)dst_ptr,
                         (unsigned long long)dst_pa,
                         (uint32_t)used_bounce);
      }
      if (log_chunk_marker) {
        PRT_MARKER_LOG("dma-export-chunk stage=%u tensor=%u page=%u copied=%llu chunk=%llu src=0x%llx dst_va=0x%llx dst_pa=0x%llx bounce=%u submit-begin",
                       stage_idx, tensor_id, i,
                       (unsigned long long)copied,
                       (unsigned long long)chunk,
                       (unsigned long long)src_addr,
                       (unsigned long long)(uintptr_t)dst_ptr,
                       (unsigned long long)dst_pa,
                       (uint32_t)used_bounce);
      }
      dma_breadcrumb_page_begin(tensor_id, manager_id, i, &req,
                                used_bounce ? PRT_BREADCRUMB_FLAG_BOUNCE : 0U,
                                timeout_ns);
      prt_gdb_marker_note(PRT_GDB_MARKER_SITE_DMA_EXPORT_PAGE_SUBMIT_BEGIN,
                          PRT_DEBUG_U32_NONE, PRT_DEBUG_U32_NONE, stage_idx,
                          PRT_DEBUG_U32_NONE, manager_id, tensor_id, i,
                          prt_breadcrumb_get_export_target_token(), PRT_OK,
                          copied, chunk, __LINE__);
      if (dma_tracerv_dma_window_target_match(stage_idx, tensor_id, i, copied)) {
        tracerv_release_page_idx = i;
        tracerv_release_copied_bytes = copied;
      }
      dma_tracerv_dma_window_scope_push(i, copied);
      rc = dma_submit_wait_annotated_scoped(rt, &req, stage_idx, tensor_id, timeout_ns, &scope,
                                            dma_export_probe_enabled() && (page_probe || first_chunk_probe));
      dma_tracerv_dma_window_scope_pop();
      dma_breadcrumb_export_loop_phase(stage_idx, tensor_id, manager_id, i, &req, rc,
                                       used_bounce ? PRT_BREADCRUMB_FLAG_BOUNCE : 0U,
                                       timeout_ns,
                                       PRT_BREADCRUMB_PHASE_DMA_PAGE_AFTER_SUBMITWAIT_RETURN,
                                       __LINE__);
      dma_breadcrumb_page_end(tensor_id, manager_id, i, &req, rc,
                              used_bounce ? PRT_BREADCRUMB_FLAG_BOUNCE : 0U,
                              timeout_ns);
      prt_gdb_marker_note(PRT_GDB_MARKER_SITE_DMA_EXPORT_PAGE_SUBMIT_END,
                          PRT_DEBUG_U32_NONE, PRT_DEBUG_U32_NONE, stage_idx,
                          PRT_DEBUG_U32_NONE, manager_id, tensor_id, i,
                          prt_breadcrumb_get_export_target_token(), rc,
                          copied, chunk, __LINE__);
      dma_trigger_export_host("sw-e", stage_idx, manager_id, tensor_id, i, rc);
      dma_breadcrumb_export_loop_phase(stage_idx, tensor_id, manager_id, i, &req, rc,
                                       used_bounce ? PRT_BREADCRUMB_FLAG_BOUNCE : 0U,
                                       timeout_ns,
                                       PRT_BREADCRUMB_PHASE_DMA_PAGE_AFTER_PAGE_END,
                                       __LINE__);
      if (page_probe) {
        PRT_PROGRESS_LOG("dma-export-host stage=%u tensor=%u phase=page-submitwait-end page=%u/%u copied=%llu chunk=%llu rc=%d bounce=%u",
                         stage_idx, tensor_id, i, src_pages->size,
                         (unsigned long long)copied,
                         (unsigned long long)chunk,
                         rc,
                         (uint32_t)used_bounce);
      }
      if (first_chunk_probe) {
        PRT_PROGRESS_LOG("dma-export-host stage=%u tensor=%u phase=first-chunk-submitwait-end page=%u copied=%llu chunk=%llu rc=%d bounce=%u",
                         stage_idx, tensor_id, i,
                         (unsigned long long)copied,
                         (unsigned long long)chunk,
                         rc,
                         (uint32_t)used_bounce);
      }
      if (rc != PRT_OK) {
        PRT_MARKER_LOG("dma-export fail stage=%u tensor=%u page=%u copied=%llu chunk=%llu bounce=0 timeout_ms=%llu src=0x%llx dst_pa=0x%llx dst_off=%llu rc=%d",
                       stage_idx, tensor_id, i,
                       (unsigned long long)copied,
                       (unsigned long long)chunk,
                       (unsigned long long)(timeout_ns / 1000000ULL),
                       (unsigned long long)src_addr,
                       (unsigned long long)dst_pa,
                       (unsigned long long)dst_off,
                       rc);
        goto done;
      }
      if (log_chunk_marker) {
        PRT_MARKER_LOG("dma-export-chunk stage=%u tensor=%u page=%u copied=%llu chunk=%llu src=0x%llx dst_pa=0x%llx bounce=%u rc=%d end",
                       stage_idx, tensor_id, i,
                       (unsigned long long)copied,
                       (unsigned long long)chunk,
                       (unsigned long long)src_addr,
                       (unsigned long long)dst_pa,
                       (uint32_t)used_bounce,
                       rc);
      }
      copied += chunk;
      remaining -= chunk;
      dma_breadcrumb_export_loop_phase(stage_idx, tensor_id, manager_id, i, &req, rc,
                                       used_bounce ? PRT_BREADCRUMB_FLAG_BOUNCE : 0U,
                                       timeout_ns,
                                       PRT_BREADCRUMB_PHASE_DMA_PAGE_AFTER_ACCOUNTING,
                                       __LINE__);
    }
  }

done:
  dma_tracerv_dma_window_marker_if_match(stage_idx, tensor_id,
                                         tracerv_release_page_idx,
                                         tracerv_release_copied_bytes,
                                         PRT_DMA_TRACERV_MARKER_BATCH_RELEASE_BEGIN);
  dma_batch_scope_release(&scope);
  dma_tracerv_dma_window_marker_if_match(stage_idx, tensor_id,
                                         tracerv_release_page_idx,
                                         tracerv_release_copied_bytes,
                                         PRT_DMA_TRACERV_MARKER_BATCH_RELEASE_END);
  return rc;
}
#endif

static int dma_should_progress_log(const prt_dma_token_t *tok) {
  if (!tok) return 0;
  return tok->id > 0 && tok->id <= 16U;
}

static int dma_should_checkpoint_doneflag_tok(const prt_dma_token_t *tok) {
  if (!tok) return 0;
  if (!dma_fixed_load_probe_target_match(tok->stage_idx, tok->tensor_id)) return 0;
  if (!dma_fixed_load_checkpoint_enabled()) return 0;
  return dma_fixed_load_probe_token_in_window(tok->id);
}

static int dma_should_checkpoint_submit_wait_tok(const prt_dma_token_t *tok) {
  if (!tok) return 0;
  if (!dma_fixed_load_probe_target_match(tok->stage_idx, tok->tensor_id)) return 0;
  if (!dma_fixed_load_checkpoint_enabled()) return 0;
  return dma_fixed_load_probe_token_in_window(tok->id);
}

static int dma_should_sparse_submit_probe_tok(const prt_dma_token_t *tok) {
  if (!tok) return 0;
  if (!dma_submit_marker_trace_enabled()) return 0;
  if (!prt_log_gate_allow_deep_logs()) return 0;
  if (tok->stage_idx != 0U) return 0;
  if (tok->tensor_id >= 1000000U) return 0;
  return tok->debug_bytes > 0ULL && tok->debug_bytes <= 4096ULL;
}

static int dma_should_sparse_fixed_load_submit_probe_tok(const prt_dma_token_t *tok) {
  if (!tok) return 0;
  if (!dma_fixed_load_probe_enabled()) return 0;
  if (!dma_fixed_load_probe_target_match(tok->stage_idx, tok->tensor_id)) return 0;
  if (!dma_fixed_load_probe_token_in_window(tok->id)) return 0;
  return tok->debug_bytes > 0ULL && tok->debug_bytes <= 1024ULL;
}

static int dma_should_sparse_fixed_load_wait_probe_tok(const prt_dma_token_t *tok) {
  if (!tok) return 0;
  if (!dma_fixed_load_probe_enabled()) return 0;
  if (!dma_fixed_load_probe_target_match(tok->stage_idx, tok->tensor_id)) return 0;
  if (!dma_fixed_load_probe_token_in_window(tok->id)) return 0;
  return tok->debug_bytes > 0ULL && tok->debug_bytes <= 1024ULL;
}

static int dma_should_sparse_export_submit_probe_tok(const prt_dma_token_t *tok) {
  if (!tok) return 0;
  if (tok->debug_force_export_probe) {
    return tok->stage_idx == 0U &&
           tok->tensor_id == 2U &&
           tok->debug_bytes > 0ULL &&
           tok->debug_bytes <= 1024ULL;
  }
  if (!dma_export_probe_enabled()) return 0;
  if (tok->stage_idx != 0U) return 0;
  if (tok->tensor_id != 2U) return 0;
  if (!dma_export_probe_token_in_window(tok->id)) return 0;
  return tok->debug_bytes > 0ULL && tok->debug_bytes <= 1024ULL;
}

static int dma_export_page_probe_range_match(uint32_t page_idx) {
#if !defined(BAREMETAL)
  static int initialized = 0;
  static int configured = 0;
  static uint32_t start = 0U;
  static uint32_t end = UINT32_MAX;
  if (!initialized) {
    const int has_start = dma_env_has_value("PIPELINE_RUNTIME_DMA_EXPORT_PAGE_START");
    const int has_end = dma_env_has_value("PIPELINE_RUNTIME_DMA_EXPORT_PAGE_END");
    configured = has_start || has_end;
    if (has_start) start = dma_env_u32("PIPELINE_RUNTIME_DMA_EXPORT_PAGE_START", 0U);
    if (has_end) end = dma_env_u32("PIPELINE_RUNTIME_DMA_EXPORT_PAGE_END", UINT32_MAX);
    if (!has_start) start = 0U;
    if (!has_end) end = UINT32_MAX;
    if (start > end) {
      const uint32_t tmp = start;
      start = end;
      end = tmp;
    }
    initialized = 1;
  }
  if (!configured) return -1;
  return page_idx >= start && page_idx <= end;
#else
  (void)page_idx;
  return -1;
#endif
}

static int dma_fixed_load_page_probe_range_match(uint32_t page_idx) {
#if !defined(BAREMETAL)
  static int initialized = 0;
  static int configured = 0;
  static uint32_t start = 0U;
  static uint32_t end = UINT32_MAX;
  if (!initialized) {
    const int has_start = dma_env_has_value("PIPELINE_RUNTIME_DMA_FIXED_LOAD_PAGE_START");
    const int has_end = dma_env_has_value("PIPELINE_RUNTIME_DMA_FIXED_LOAD_PAGE_END");
    configured = has_start || has_end;
    if (has_start) start = dma_env_u32("PIPELINE_RUNTIME_DMA_FIXED_LOAD_PAGE_START", 0U);
    if (has_end) end = dma_env_u32("PIPELINE_RUNTIME_DMA_FIXED_LOAD_PAGE_END", UINT32_MAX);
    if (!has_start) start = 0U;
    if (!has_end) end = UINT32_MAX;
    if (start > end) {
      const uint32_t tmp = start;
      start = end;
      end = tmp;
    }
    initialized = 1;
  }
  if (!configured) return -1;
  return page_idx >= start && page_idx <= end;
#else
  (void)page_idx;
  return -1;
#endif
}

static int dma_should_sparse_export_page_probe(uint32_t stage_idx, uint32_t tensor_id,
                                               uint32_t page_idx, uint32_t page_count,
                                               uint64_t copied_bytes) {
  const int range_match = dma_export_page_probe_range_match(page_idx);
  if (stage_idx != 0U || tensor_id != 2U) return 0;
  if (copied_bytes != 0ULL) return 0;
  if (range_match >= 0) return range_match;
  if (page_idx >= 48U) return 1;
  if (page_idx == 0U) return 1;
  if (page_count > 0U && page_idx + 1U == page_count) return 1;
  return (page_idx % 8U) == 0U;
}

static int dma_should_sparse_fixed_load_page_probe(uint32_t stage_idx, uint32_t tensor_id,
                                                   uint32_t page_idx, uint32_t page_count,
                                                   uint64_t copied_bytes) {
  const int range_match = dma_fixed_load_page_probe_range_match(page_idx);
  if (!dma_fixed_load_probe_enabled()) return 0;
  if (!dma_fixed_load_probe_target_match(stage_idx, tensor_id)) return 0;
  if (copied_bytes != 0ULL) return 0;
  if (range_match >= 0) return range_match;
  if (page_idx < 8U) return 1;
  if (page_count > 0U && page_idx + 1U == page_count) return 1;
  return (page_idx % 8U) == 0U;
}

static void dma_debug_capture_req(prt_dma_token_t *tok, const prt_dma_req_t *req) {
  if (!tok || !req) return;
  tok->debug_src_addr = req->src_addr;
  tok->debug_dst_addr = req->dst_addr;
  tok->debug_bytes = req->bytes;
  tok->debug_src_acc = req->src_acc;
  tok->debug_dst_acc = req->dst_acc;
}

static inline uint64_t dma_debug_mod64(uint64_t addr) {
  return addr & 63ULL;
}

static inline int dma_debug_initial_wide_hint(uint64_t src_addr, uint64_t dst_addr, uint64_t bytes) {
  return dma_debug_mod64(src_addr) == 0ULL &&
         dma_debug_mod64(dst_addr) == 0ULL &&
         bytes >= 64ULL;
}

static inline int dma_debug_full_byte_mode_hint(uint64_t src_addr, uint64_t dst_addr, uint64_t bytes) {
  return bytes < 64ULL || dma_debug_mod64(src_addr) != dma_debug_mod64(dst_addr);
}

#if !defined(BAREMETAL)
static int dma_env_flag_enabled(const char *name, int default_value) {
  const char *value = getenv(name);
  if (!value || !*value) return default_value;
  switch (value[0]) {
    case '0':
    case 'n':
    case 'N':
    case 'f':
    case 'F':
      return 0;
    default:
      return 1;
  }
}

static uint32_t dma_env_u32(const char *name, uint32_t default_value) {
  const char *value = getenv(name);
  char *end = NULL;
  unsigned long parsed;
  if (!value || !*value) return default_value;
  errno = 0;
  parsed = strtoul(value, &end, 0);
  if (errno != 0 || end == value || (end && *end != '\0')) return default_value;
  if (parsed > UINT32_MAX) return UINT32_MAX;
  return (uint32_t)parsed;
}

static int dma_env_has_value(const char *name) {
  const char *value = getenv(name);
  return value != NULL && *value != '\0';
}
#endif

static int dma_bounce_bypass_enabled(void) {
#if !defined(BAREMETAL)
  static int initialized = 0;
  static int enabled = 0;
  if (!initialized) {
    enabled = dma_env_flag_enabled("PIPELINE_RUNTIME_DMA_BOUNCE_BYPASS_ENABLE", 0);
    initialized = 1;
  }
  return enabled;
#else
  return 0;
#endif
}

static int dma_blocking_wait_poll_timeout_enabled(void) {
#if !defined(BAREMETAL)
  static int initialized = 0;
  static int enabled = 0;
  if (!initialized) {
    enabled = dma_env_flag_enabled("PIPELINE_RUNTIME_DMA_BLOCKING_WAIT_POLL_TIMEOUT_ENABLE", 0);
    initialized = 1;
  }
  return enabled;
#else
  return 0;
#endif
}

#if defined(__linux__) && defined(__riscv)
static int dma_force_direct_enabled(void) {
#if !defined(BAREMETAL)
  static int initialized = 0;
  static int enabled = 0;
  if (!initialized) {
    enabled = dma_env_flag_enabled("PIPELINE_RUNTIME_DMA_FORCE_DIRECT_ENABLE", 0);
    initialized = 1;
  }
  return enabled;
#else
  return 0;
#endif
}
#endif

static int dma_tracerv_dma_window_page_range_match(uint32_t page_idx) {
#if !defined(BAREMETAL)
  static int initialized = 0;
  static int configured = 0;
  static uint32_t start = 0U;
  static uint32_t end = UINT32_MAX;
  if (!initialized) {
    const int has_start = dma_env_has_value("PIPELINE_RUNTIME_DMA_TRACERV_PAGE_START");
    const int has_end = dma_env_has_value("PIPELINE_RUNTIME_DMA_TRACERV_PAGE_END");
    configured = has_start || has_end;
    if (has_start) start = dma_env_u32("PIPELINE_RUNTIME_DMA_TRACERV_PAGE_START", 0U);
    if (has_end) end = dma_env_u32("PIPELINE_RUNTIME_DMA_TRACERV_PAGE_END", UINT32_MAX);
    if (!has_start) start = 0U;
    if (!has_end) end = UINT32_MAX;
    if (start > end) {
      const uint32_t tmp = start;
      start = end;
      end = tmp;
    }
    initialized = 1;
  }
  if (!configured) return 1;
  return page_idx >= start && page_idx <= end;
#else
  (void)page_idx;
  return 1;
#endif
}

static inline int dma_tracerv_dma_window_target_match(uint32_t stage_idx,
                                                      uint32_t tensor_id,
                                                      uint32_t page_idx,
                                                      uint64_t copied_bytes) {
#if defined(__riscv) && PRT_ENABLE_FIRESIM_TRACERV_DMA_WINDOW_MARKERS
  if (stage_idx != 0U) return 0;
  if (tensor_id != 6U) return 0;
  if (copied_bytes != 0ULL) return 0;
  return dma_tracerv_dma_window_page_range_match(page_idx);
#else
  (void)stage_idx;
  (void)tensor_id;
  (void)page_idx;
  (void)copied_bytes;
  return 0;
#endif
}

static inline void dma_tracerv_dma_window_scope_push(uint32_t page_idx, uint64_t copied_bytes) {
#if defined(__riscv) && PRT_ENABLE_FIRESIM_TRACERV_DMA_WINDOW_MARKERS
  g_prt_dma_tracerv_scope_ctx.valid = 1;
  g_prt_dma_tracerv_scope_ctx.page_idx = page_idx;
  g_prt_dma_tracerv_scope_ctx.copied_bytes = copied_bytes;
#else
  (void)page_idx;
  (void)copied_bytes;
#endif
}

static inline void dma_tracerv_dma_window_scope_pop(void) {
#if defined(__riscv) && PRT_ENABLE_FIRESIM_TRACERV_DMA_WINDOW_MARKERS
  g_prt_dma_tracerv_scope_ctx.valid = 0;
  g_prt_dma_tracerv_scope_ctx.page_idx = UINT32_MAX;
  g_prt_dma_tracerv_scope_ctx.copied_bytes = 0ULL;
#endif
}

static inline int dma_tracerv_dma_window_scope_match(uint32_t stage_idx, uint32_t tensor_id) {
#if defined(__riscv) && PRT_ENABLE_FIRESIM_TRACERV_DMA_WINDOW_MARKERS
  if (!g_prt_dma_tracerv_scope_ctx.valid) return 0;
  return dma_tracerv_dma_window_target_match(stage_idx,
                                             tensor_id,
                                             g_prt_dma_tracerv_scope_ctx.page_idx,
                                             g_prt_dma_tracerv_scope_ctx.copied_bytes);
#else
  (void)stage_idx;
  (void)tensor_id;
  return 0;
#endif
}

static inline void dma_tracerv_dma_window_emit(prt_dma_tracerv_marker_t marker) {
#if defined(__riscv) && PRT_ENABLE_FIRESIM_TRACERV_DMA_WINDOW_MARKERS
  switch (marker) {
    case PRT_DMA_TRACERV_MARKER_PROGRAM_BEGIN:
      __asm__ volatile(".word 0x00018013" ::: "memory");
      break;
    case PRT_DMA_TRACERV_MARKER_PROGRAM_POST_FENCE:
      __asm__ volatile(".word 0x00020013" ::: "memory");
      break;
    case PRT_DMA_TRACERV_MARKER_PROGRAM_POST_DST:
      __asm__ volatile(".word 0x00028013" ::: "memory");
      break;
    case PRT_DMA_TRACERV_MARKER_PROGRAM_POST_SRC:
      __asm__ volatile(".word 0x00030013" ::: "memory");
      break;
    case PRT_DMA_TRACERV_MARKER_WAIT_BEFORE_FENCE:
      __asm__ volatile(".word 0x00038013" ::: "memory");
      break;
    case PRT_DMA_TRACERV_MARKER_WAIT_AFTER_FENCE:
      __asm__ volatile(".word 0x00040013" ::: "memory");
      break;
    case PRT_DMA_TRACERV_MARKER_WAIT_BEFORE_SHARED_FENCE:
      __asm__ volatile(".word 0x00048013" ::: "memory");
      break;
    case PRT_DMA_TRACERV_MARKER_WAIT_AFTER_SHARED_FENCE:
      __asm__ volatile(".word 0x00050013" ::: "memory");
      break;
    case PRT_DMA_TRACERV_MARKER_BATCH_RELEASE_BEGIN:
      __asm__ volatile(".word 0x00058013" ::: "memory");
      break;
    case PRT_DMA_TRACERV_MARKER_BATCH_RELEASE_END:
      __asm__ volatile(".word 0x00060013" ::: "memory");
      break;
  }
#else
  (void)marker;
#endif
}

static inline void dma_tracerv_dma_window_marker_if_scope(uint32_t stage_idx,
                                                          uint32_t tensor_id,
                                                          prt_dma_tracerv_marker_t marker) {
  if (!dma_tracerv_dma_window_scope_match(stage_idx, tensor_id)) return;
  dma_tracerv_dma_window_emit(marker);
}

static inline void dma_tracerv_dma_window_marker_if_match(uint32_t stage_idx,
                                                          uint32_t tensor_id,
                                                          uint32_t page_idx,
                                                          uint64_t copied_bytes,
                                                          prt_dma_tracerv_marker_t marker) {
  if (!dma_tracerv_dma_window_target_match(stage_idx, tensor_id, page_idx, copied_bytes)) return;
  dma_tracerv_dma_window_emit(marker);
}

static int dma_submit_marker_trace_enabled(void) {
#if !defined(BAREMETAL)
  static int initialized = 0;
  static int enabled = 0;
  if (!initialized) {
    enabled = dma_env_flag_enabled("PIPELINE_RUNTIME_DMA_SUBMIT_TRACE_ENABLE", 0);
    initialized = 1;
  }
  return enabled;
#else
  return 1;
#endif
}

static int dma_fixed_load_checkpoint_enabled(void) {
#if !defined(BAREMETAL)
  static int initialized = 0;
  static int enabled = 0;
  if (!initialized) {
    enabled = dma_env_flag_enabled("PIPELINE_RUNTIME_DMA_FIXED_LOAD_CHECKPOINT_ENABLE", 0);
    initialized = 1;
  }
  return enabled;
#else
  return 0;
#endif
}

static int dma_fixed_load_probe_enabled(void) {
#if !defined(BAREMETAL)
  static int initialized = 0;
  static int enabled = 0;
  if (!initialized) {
    enabled = dma_env_flag_enabled("PIPELINE_RUNTIME_DMA_FIXED_LOAD_PROBE_ENABLE", 0);
    initialized = 1;
  }
  return enabled;
#else
  return 1;
#endif
}

static int dma_fixed_load_probe_token_in_window(uint32_t token_id) {
#if !defined(BAREMETAL)
  static int initialized = 0;
  static int has_start = 0;
  static int has_end = 0;
  static uint32_t start = 0U;
  static uint32_t end = UINT32_MAX;
  if (!initialized) {
    has_start = dma_env_has_value("PIPELINE_RUNTIME_DMA_FIXED_LOAD_PROBE_TOKEN_START");
    has_end = dma_env_has_value("PIPELINE_RUNTIME_DMA_FIXED_LOAD_PROBE_TOKEN_END");
    if (has_start) start = dma_env_u32("PIPELINE_RUNTIME_DMA_FIXED_LOAD_PROBE_TOKEN_START", 0U);
    if (has_end) end = dma_env_u32("PIPELINE_RUNTIME_DMA_FIXED_LOAD_PROBE_TOKEN_END", UINT32_MAX);
    if (!has_start) start = 0U;
    if (!has_end) end = UINT32_MAX;
    if (start > end) {
      const uint32_t tmp = start;
      start = end;
      end = tmp;
    }
    initialized = 1;
  }
  if (!has_start && !has_end) return 1;
  return token_id >= start && token_id <= end;
#else
  (void)token_id;
  return 1;
#endif
}

static int dma_export_probe_enabled(void) {
#if !defined(BAREMETAL)
  static int initialized = 0;
  static int enabled = 0;
  if (!initialized) {
    enabled = dma_env_flag_enabled("PIPELINE_RUNTIME_DMA_EXPORT_PROBE_ENABLE", 0);
    initialized = 1;
  }
  return enabled;
#else
  return 1;
#endif
}

static int dma_fixed_load_monitor_probe_enabled(void) {
#if !defined(BAREMETAL)
  static int initialized = 0;
  static int enabled = 0;
  if (!initialized) {
    enabled = dma_env_flag_enabled("PIPELINE_RUNTIME_DMA_FIXED_LOAD_MONITOR_PROBE_ENABLE", 0);
    initialized = 1;
  }
  return enabled;
#else
  return 0;
#endif
}

static uint32_t dma_fixed_load_probe_stage_id(void) {
#if !defined(BAREMETAL)
  static int initialized = 0;
  static uint32_t stage_id = 0U;
  if (!initialized) {
    stage_id = dma_env_u32("PIPELINE_RUNTIME_DMA_FIXED_LOAD_PROBE_STAGE_ID", 0U);
    initialized = 1;
  }
  return stage_id;
#else
  return 0U;
#endif
}

static uint32_t dma_fixed_load_probe_tensor_id(void) {
#if !defined(BAREMETAL)
  static int initialized = 0;
  static uint32_t tensor_id = 1000001U;
  if (!initialized) {
    tensor_id = dma_env_u32("PIPELINE_RUNTIME_DMA_FIXED_LOAD_PROBE_TENSOR_ID", 1000001U);
    initialized = 1;
  }
  return tensor_id;
#else
  return 1000001U;
#endif
}

static int dma_fixed_load_probe_target_match(uint32_t stage_idx, uint32_t tensor_id) {
  return stage_idx == dma_fixed_load_probe_stage_id() &&
         tensor_id == dma_fixed_load_probe_tensor_id();
}

static uint32_t dma_fixed_load_pre_src_nops(void) {
#if !defined(BAREMETAL)
  static int initialized = 0;
  static uint32_t count = 0U;
  if (!initialized) {
    count = dma_env_u32("PIPELINE_RUNTIME_DMA_FIXED_LOAD_PRE_SRC_NOPS", 0U);
    initialized = 1;
  }
  return count;
#else
  return 0U;
#endif
}

static void dma_busy_wait_nops(uint32_t count) {
  for (uint32_t i = 0; i < count; ++i) {
#if defined(__riscv)
    __asm__ volatile("nop");
#else
    __asm__ volatile("" ::: "memory");
#endif
  }
}

static int dma_export_probe_token_in_window(uint32_t token_id) {
#if !defined(BAREMETAL)
  static int initialized = 0;
  static int has_start = 0;
  static int has_end = 0;
  static uint32_t start = 0U;
  static uint32_t end = UINT32_MAX;
  if (!initialized) {
    has_start = dma_env_has_value("PIPELINE_RUNTIME_DMA_EXPORT_PROBE_TOKEN_START");
    has_end = dma_env_has_value("PIPELINE_RUNTIME_DMA_EXPORT_PROBE_TOKEN_END");
    if (has_start) start = dma_env_u32("PIPELINE_RUNTIME_DMA_EXPORT_PROBE_TOKEN_START", 0U);
    if (has_end) end = dma_env_u32("PIPELINE_RUNTIME_DMA_EXPORT_PROBE_TOKEN_END", UINT32_MAX);
    if (!has_start) start = 0U;
    if (!has_end) end = UINT32_MAX;
    if (start > end) {
      const uint32_t tmp = start;
      start = end;
      end = tmp;
    }
    initialized = 1;
  }
  if (!has_start && !has_end) return 1;
  return token_id >= start && token_id <= end;
#else
  (void)token_id;
  return 1;
#endif
}

static uint32_t dma_export_chunk_log_stride(void) {
#if !defined(BAREMETAL)
  static int initialized = 0;
  static uint32_t stride = 32U;
  if (!initialized) {
    stride = dma_env_u32("PIPELINE_RUNTIME_DMA_EXPORT_CHUNK_LOG_STRIDE", 32U);
    initialized = 1;
  }
  return stride;
#else
  return 1U;
#endif
}

#if defined(__linux__) && defined(__riscv)
static int dma_chunk_needs_bounce(uint64_t src_addr, uint64_t dst_addr, uint64_t bytes) {
  if (dma_force_direct_enabled()) return 0;
  return bytes >= 64ULL && dma_debug_mod64(src_addr) != dma_debug_mod64(dst_addr);
}

static int dma_stage_bounce_ensure(prt_runtime_t *rt, uint32_t stage_idx) {
  prt_action_exec_t *exec;
  void *buf = NULL;
  size_t host_page_bytes;
  int rc = PRT_OK;

  if (!rt || stage_idx >= PRT_MAX_STAGES) return PRT_ERR_INVAL;
  exec = prt_runtime_current_exec(rt);
  if (!exec) return PRT_ERR_STATE;
  if (exec->stage_dma_bounce[stage_idx] && exec->stage_dma_bounce_bytes[stage_idx] != 0U) {
    return PRT_OK;
  }

  host_page_bytes = prt_host_page_size_bytes();
  if (host_page_bytes == 0U) return PRT_ERR_STATE;

  pthread_mutex_lock(&rt->state_lock);
  if (!exec->stage_dma_bounce[stage_idx] || exec->stage_dma_bounce_bytes[stage_idx] == 0U) {
    if (posix_memalign(&buf, host_page_bytes, host_page_bytes) != 0) {
      rc = PRT_ERR_NOMEM;
    } else {
      rc = dma_prefault_and_lock_buffer("dma-stage-bounce", buf, host_page_bytes);
      if (rc != PRT_OK) {
        free(buf);
      } else {
        exec->stage_dma_bounce[stage_idx] = (uint8_t *)buf;
        exec->stage_dma_bounce_bytes[stage_idx] = host_page_bytes;
      }
    }
  }
  pthread_mutex_unlock(&rt->state_lock);

  return rc;
}

static int dma_stage_bounce_region(prt_runtime_t *rt, uint32_t stage_idx, uint64_t align_mod64,
                                   uint8_t **out_ptr, uint64_t *out_room) {
  prt_action_exec_t *exec;
  int rc;

  if (!out_ptr || !out_room) return PRT_ERR_INVAL;
  *out_ptr = NULL;
  *out_room = 0;

  rc = dma_stage_bounce_ensure(rt, stage_idx);
  if (rc != PRT_OK) return rc;
  exec = prt_runtime_current_exec(rt);
  if (!exec) return PRT_ERR_STATE;
  if (!exec->stage_dma_bounce[stage_idx] || exec->stage_dma_bounce_bytes[stage_idx] == 0U) {
    return PRT_ERR_NOT_READY;
  }
  if (align_mod64 >= exec->stage_dma_bounce_bytes[stage_idx]) return PRT_ERR_STATE;

  *out_ptr = exec->stage_dma_bounce[stage_idx] + align_mod64;
  *out_room = (uint64_t)exec->stage_dma_bounce_bytes[stage_idx] - align_mod64;
  return PRT_OK;
}
#endif

#if defined(__riscv)
static void dma_debug_capture_done_flag(prt_runtime_t *rt, prt_dma_token_t *tok) {
  int rc;
  const int trace_doneflag = tok != NULL && prt_log_gate_allow_deep_logs();
  if (!tok) return;
  if (trace_doneflag) {
    PRT_PROGRESS_HOT_LOG("dma-doneflag-slot begin token=%u stage=%u tensor=%u",
                         tok->id,
                         tok->stage_idx,
                         tok->tensor_id);
  }
  rc = dma_completion_flag_acquire(rt, tok);
  tok->hw_done_flag_pa_rc = rc;
  if (trace_doneflag) {
    PRT_PROGRESS_HOT_LOG("dma-doneflag-slot end token=%u stage=%u tensor=%u rc=%d slot=%u done_va=0x%llx done_pa=0x%llx",
                         tok->id,
                         tok->stage_idx,
                         tok->tensor_id,
                         rc,
                         tok->completion_slot,
                         (unsigned long long)tok->debug_done_flag_va,
                         (unsigned long long)tok->debug_done_flag_pa);
  }
}
#endif

static void dma_log_pending_state(const char *tag, const prt_dma_token_t *tok, uint64_t elapsed_ns) {
#if !PRT_ENABLE_PROGRESS_LOG
  (void)tag;
  (void)tok;
  (void)elapsed_ns;
#else
  int hw_done;
  if (!dma_should_progress_log(tok)) return;
  hw_done = dma_completion_flag_value(tok);
  PRT_PROGRESS_LOG(
    "dma-%s token=%u stage=%u tensor=%u polls=%u elapsed_ms=%llu hw_done=%d done=%d status=%d src=0x%llx dst=0x%llx bytes=%llu src_acc=%u dst_acc=%u done_va=0x%llx done_pa=0x%llx done_pa_rc=%d(%s)",
    tag,
    tok->id,
    tok->stage_idx,
    tok->tensor_id,
    tok->debug_progress_polls,
    (unsigned long long)(elapsed_ns / 1000000ULL),
    hw_done,
    tok->done,
    tok->status,
    (unsigned long long)tok->debug_src_addr,
    (unsigned long long)tok->debug_dst_addr,
    (unsigned long long)tok->debug_bytes,
    tok->debug_src_acc,
    tok->debug_dst_acc,
    (unsigned long long)tok->debug_done_flag_va,
    (unsigned long long)tok->debug_done_flag_pa,
    tok->hw_done_flag_pa_rc,
    prt_err_str(tok->hw_done_flag_pa_rc));
#endif
}

static int dma_should_marker_trace_submit(const prt_dma_req_t *req, uint32_t tensor_id) {
  if (!req) return 0;
  if (!dma_submit_marker_trace_enabled()) return 0;
  if (tensor_id >= 1000000U) return 0;
  return req->bytes > 0ULL && req->bytes <= 4096ULL;
}

static int __attribute__((unused))
dma_should_log_export_chunk_marker(uint32_t page_idx, uint32_t page_count,
                                   uint64_t copied_bytes) {
  const uint32_t stride = dma_export_chunk_log_stride();
  if (copied_bytes != 0ULL) return 0;
  if (page_idx == 0U) return 1;
  if (page_count > 0U && page_idx + 1U == page_count) return 1;
  if (stride <= 1U) return 1;
  return (page_idx % stride) == 0U;
}

static int dma_submit_wait_annotated(prt_runtime_t *rt, const prt_dma_req_t *req,
                                     uint32_t stage_idx, uint32_t tensor_id,
                                     uint64_t timeout_ns) {
  int rc;
  prt_dma_token_t tok;
  const int trace_submit = dma_should_marker_trace_submit(req, tensor_id);
  if (!rt || !req) return PRT_ERR_INVAL;
  memset(&tok, 0, sizeof(tok));
  tok.stage_idx = stage_idx;
  tok.tensor_id = tensor_id;
  if (trace_submit) {
    PRT_MARKER_LOG("dma-submit stage=%u tensor=%u phase=submit-begin src=0x%llx dst=0x%llx bytes=%llu src_acc=%u dst_acc=%u timeout_ms=%llu",
                   stage_idx, tensor_id,
                   (unsigned long long)req->src_addr,
                   (unsigned long long)req->dst_addr,
                   (unsigned long long)req->bytes,
                   req->src_acc,
                   req->dst_acc,
                   (unsigned long long)(timeout_ns / 1000000ULL));
  }
  rc = prt_dma_submit(rt, req, &tok);
  if (trace_submit) {
    PRT_MARKER_LOG("dma-submit stage=%u tensor=%u phase=submit-end rc=%d tok=%u done_pa=0x%llx rr_cfg=%u rr_mgr=%u rr_opc=%u",
                   stage_idx, tensor_id, rc, tok.id,
                   (unsigned long long)tok.debug_done_flag_pa,
                   tok.rr_cfg_id,
                   tok.rr_manager_id,
                   tok.rr_opcode_id);
  }
  if (rc != PRT_OK) {
    (void)prt_dma_token_cleanup(&tok);
    return rc;
  }
  if (trace_submit) {
    PRT_MARKER_LOG("dma-submit stage=%u tensor=%u phase=wait-begin tok=%u done_pa=0x%llx timeout_ms=%llu",
                   stage_idx, tensor_id, tok.id,
                   (unsigned long long)tok.debug_done_flag_pa,
                   (unsigned long long)(timeout_ns / 1000000ULL));
  }
  rc = prt_dma_wait(rt, &tok, timeout_ns);
  if (trace_submit) {
    PRT_MARKER_LOG("dma-submit stage=%u tensor=%u phase=wait-end rc=%d tok=%u hw_done=%d",
                   stage_idx, tensor_id, rc, tok.id, tok.hw_done_flag);
  }
  (void)prt_dma_token_cleanup(&tok);
  return rc;
}

static int dma_submit_wait_annotated_scoped(prt_runtime_t *rt, const prt_dma_req_t *req,
                                            uint32_t stage_idx, uint32_t tensor_id,
                                            uint64_t timeout_ns, prt_rr_scope_t *scope,
                                            int force_export_probe) {
  int rc;
  prt_dma_token_t tok;
  uint32_t cleanup_token_id = 0U;
  uint32_t cleanup_manager_id = PRT_BREADCRUMB_ANY_U32;
  uint32_t cleanup_flags = 0U;
  uint64_t cleanup_src_addr = 0ULL;
  uint64_t cleanup_dst_addr = 0ULL;
  uint64_t cleanup_done_flag_pa = 0ULL;
  const int trace_submit = dma_should_marker_trace_submit(req, tensor_id);
  if (!rt || !req) return PRT_ERR_INVAL;
  memset(&tok, 0, sizeof(tok));
  tok.stage_idx = stage_idx;
  tok.tensor_id = tensor_id;
  tok.debug_force_export_probe = force_export_probe ? 1 : 0;
  if (scope && scope->valid) {
    tok.rr_scope_valid = scope->valid;
    tok.rr_scope_external = 1;
    tok.rr_cfg_id = scope->cfg_id;
    tok.rr_manager_id = scope->manager_id;
    tok.rr_opcode_id = scope->opcode_id;
  }
  if (trace_submit) {
    PRT_MARKER_LOG("dma-submit stage=%u tensor=%u phase=submit-begin src=0x%llx dst=0x%llx bytes=%llu src_acc=%u dst_acc=%u timeout_ms=%llu",
                   stage_idx, tensor_id,
                   (unsigned long long)req->src_addr,
                   (unsigned long long)req->dst_addr,
                   (unsigned long long)req->bytes,
                   req->src_acc,
                   req->dst_acc,
                   (unsigned long long)(timeout_ns / 1000000ULL));
  }
  rc = prt_dma_submit(rt, req, &tok);
  if (trace_submit) {
    PRT_MARKER_LOG("dma-submit stage=%u tensor=%u phase=submit-end rc=%d tok=%u done_pa=0x%llx rr_cfg=%u rr_mgr=%u rr_opc=%u external=%u",
                   stage_idx, tensor_id, rc, tok.id,
                   (unsigned long long)tok.debug_done_flag_pa,
                   tok.rr_cfg_id,
                   tok.rr_manager_id,
                   tok.rr_opcode_id,
                   (uint32_t)tok.rr_scope_external);
  }
  if (rc != PRT_OK) {
    if (scope && scope->valid) {
      scope->valid = tok.rr_scope_valid;
      scope->cfg_id = tok.rr_cfg_id;
      scope->stage_id = stage_idx;
      scope->manager_id = tok.rr_manager_id;
      scope->opcode_id = tok.rr_opcode_id;
    }
    (void)prt_dma_token_cleanup(&tok);
    return rc;
  }
  if (trace_submit) {
    PRT_MARKER_LOG("dma-submit stage=%u tensor=%u phase=wait-begin tok=%u done_pa=0x%llx timeout_ms=%llu",
                   stage_idx, tensor_id, tok.id,
                   (unsigned long long)tok.debug_done_flag_pa,
                   (unsigned long long)(timeout_ns / 1000000ULL));
  }
  rc = prt_dma_wait(rt, &tok, timeout_ns);
  prt_breadcrumb_note(PRT_BREADCRUMB_KIND_DMA,
                      PRT_BREADCRUMB_PHASE_DMA_SUBMITWAIT_AFTER_WAIT,
                      tok.tensor_id,
                      tok.id,
                      tok.rr_manager_id,
                      PRT_BREADCRUMB_ANY_U32,
                      rc,
                      dma_breadcrumb_flags_from_token(&tok, 0U),
                      tok.debug_src_addr,
                      tok.debug_dst_addr,
                      tok.debug_done_flag_pa,
                      0ULL,
                      __LINE__);
  if (trace_submit) {
    PRT_MARKER_LOG("dma-submit stage=%u tensor=%u phase=wait-end rc=%d tok=%u hw_done=%d external=%u",
                   stage_idx, tensor_id, rc, tok.id, tok.hw_done_flag,
                   (uint32_t)tok.rr_scope_external);
  }
  if (scope && scope->valid) {
    scope->valid = tok.rr_scope_valid;
    scope->cfg_id = tok.rr_cfg_id;
    scope->stage_id = stage_idx;
    scope->manager_id = tok.rr_manager_id;
    scope->opcode_id = tok.rr_opcode_id;
  }
  cleanup_token_id = tok.id;
  cleanup_manager_id = tok.rr_manager_id;
  cleanup_flags = dma_breadcrumb_flags_from_token(&tok, 0U);
  cleanup_src_addr = tok.debug_src_addr;
  cleanup_dst_addr = tok.debug_dst_addr;
  cleanup_done_flag_pa = tok.debug_done_flag_pa;
  (void)prt_dma_token_cleanup(&tok);
  prt_breadcrumb_note(PRT_BREADCRUMB_KIND_DMA,
                      PRT_BREADCRUMB_PHASE_DMA_SUBMITWAIT_AFTER_CLEANUP,
                      tensor_id,
                      cleanup_token_id,
                      cleanup_manager_id,
                      PRT_BREADCRUMB_ANY_U32,
                      rc,
                      cleanup_flags,
                      cleanup_src_addr,
                      cleanup_dst_addr,
                      cleanup_done_flag_pa,
                      0ULL,
                      __LINE__);
  return rc;
}

static void dma_token_init(prt_dma_token_t *tok) {
  uint32_t stage_idx = 0;
  uint32_t tensor_id = 0;
  int rr_scope_valid = 0;
  int rr_scope_external = 0;
  uint32_t rr_cfg_id = 0;
  uint32_t rr_manager_id = 0;
  uint32_t rr_opcode_id = 0;
  int debug_force_export_probe = 0;
  if (tok) {
    stage_idx = tok->stage_idx;
    tensor_id = tok->tensor_id;
    rr_scope_valid = tok->rr_scope_valid;
    rr_scope_external = tok->rr_scope_external;
    rr_cfg_id = tok->rr_cfg_id;
    rr_manager_id = tok->rr_manager_id;
    rr_opcode_id = tok->rr_opcode_id;
    debug_force_export_probe = tok->debug_force_export_probe;
  }
  memset(tok, 0, sizeof(*tok));
  tok->stage_idx = stage_idx;
  tok->tensor_id = tensor_id;
  tok->owner_rt = NULL;
  tok->completion_flag = NULL;
  tok->completion_slot = UINT32_MAX;
  tok->rr_scope_valid = rr_scope_valid;
  tok->rr_scope_external = rr_scope_external;
  tok->rr_cfg_id = rr_cfg_id;
  tok->rr_manager_id = rr_manager_id;
  tok->rr_opcode_id = rr_opcode_id;
  tok->debug_force_export_probe = debug_force_export_probe;
  tok->hw_done_flag = 0;
  tok->hw_done_flag_pa_rc = PRT_ERR_NOT_READY;
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
  if (tok->rr_scope_external) {
    tok->rr_scope_valid = 0;
    tok->rr_scope_external = 0;
    return;
  }
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

static int dma_token_fence_scope(prt_dma_token_t *tok) {
#if defined(__riscv)
  prt_rr_scope_t scope;
#endif
  if (!tok || !tok->rr_scope_valid) return PRT_OK;
#if defined(__riscv)
  memset(&scope, 0, sizeof(scope));
  scope.valid = tok->rr_scope_valid;
  scope.cfg_id = tok->rr_cfg_id;
  scope.stage_id = tok->stage_idx;
  scope.manager_id = tok->rr_manager_id;
  scope.opcode_id = tok->rr_opcode_id;
  return prt_rr_fence_scope(&scope);
#else
  return PRT_OK;
#endif
}

static int dma_batch_scope_acquire(prt_runtime_t *rt, uint32_t stage_idx,
                                   uint32_t manager_id, uint32_t opcode_id,
                                   prt_rr_scope_t *scope) {
  int rc;
  if (!scope) return PRT_ERR_INVAL;
  memset(scope, 0, sizeof(*scope));
  rc = dma_validate_stage_manager(rt, stage_idx, manager_id, "dma_batch_scope_acquire");
  if (rc != PRT_OK) return rc;
#if defined(__riscv)
  return prt_rr_acquire_scope(rt, stage_idx, manager_id, opcode_id, scope);
#else
  (void)rt;
  (void)stage_idx;
  (void)manager_id;
  (void)opcode_id;
  return PRT_OK;
#endif
}

static void dma_batch_scope_release(prt_rr_scope_t *scope) {
#if defined(__riscv)
  if (scope && scope->valid) (void)prt_rr_release_scope(scope);
#else
  (void)scope;
#endif
}

static void dma_token_fini(prt_dma_token_t *tok) {
  if (!tok) return;
  if (!tok->initialized) return;
  dma_token_release_scope(tok, 0);
  dma_completion_flag_release(tok);
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
  if (dma_should_progress_log(tok)) {
    PRT_PROGRESS_LOG("dma-submit token=%u queued pending_count=%u done_va=0x%llx done_pa=0x%llx done_pa_rc=%d(%s)",
                     tok->id,
                     rt->dma_pending_count,
                     (unsigned long long)tok->debug_done_flag_va,
                     (unsigned long long)tok->debug_done_flag_pa,
                     tok->hw_done_flag_pa_rc,
                     prt_err_str(tok->hw_done_flag_pa_rc));
  }
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
static inline void hw_dma_set_dst(uint64_t addr, uint64_t done_flag_addr) {
  ROCC_INSTRUCTION_0_R_R(XCUSTOM_DMA, addr, done_flag_addr, 2);
}

static inline void hw_dma_set_src(uint64_t addr, uint64_t len) {
  ROCC_INSTRUCTION_0_R_R(XCUSTOM_DMA, addr, len, 1);
}

static inline void hw_dma_submit_fence(void) {
  __asm__ volatile("fence rw, rw" ::: "memory");
}

static inline uint64_t hw_dma_read_monitor(uint64_t stat_id) {
  uint64_t value = 0;
  ROCC_INSTRUCTION_R_R_R(XCUSTOM_DMA, value, stat_id, 0, 4);
  return value;
}

static inline uint64_t hw_dma_fence(void) {
  uint64_t status = 0;
  __asm__ volatile("fence" ::: "memory");
  ROCC_INSTRUCTION_R_R_R(XCUSTOM_DMA, status, 0, 0, 3);
  __asm__ volatile("fence" ::: "memory");
  return status;
}

static int dma_blocking_wait_poll_doneflag(prt_dma_token_t *tok, uint64_t timeout_ns,
                                           int emit_progress) {
  const uint64_t start_ns = prt_now_ns();
  const struct timespec sleep_ts = { .tv_sec = 0, .tv_nsec = 1000000L };
  uint32_t polls = 0U;

  if (!tok || !tok->completion_flag || timeout_ns == 0ULL) return PRT_ERR_INVAL;
  if (emit_progress) {
    PRT_PROGRESS_LOG("dma-wait-doneflag-poll phase=begin token=%u stage=%u tensor=%u timeout_ns=%llu done_pa=0x%llx",
                     tok->id,
                     tok->stage_idx,
                     tok->tensor_id,
                     (unsigned long long)timeout_ns,
                     (unsigned long long)tok->debug_done_flag_pa);
  }
  prt_breadcrumb_note(PRT_BREADCRUMB_KIND_DMA,
                      PRT_BREADCRUMB_PHASE_DMA_WAIT_DONEFLAG_POLL_BEGIN,
                      tok->tensor_id,
                      tok->id,
                      tok->rr_manager_id,
                      PRT_BREADCRUMB_ANY_U32,
                      PRT_OK,
                      dma_breadcrumb_flags_from_token(tok, 0U),
                      tok->debug_src_addr,
                      tok->debug_dst_addr,
                      tok->debug_done_flag_pa,
                      timeout_ns,
                      __LINE__);

  while (1) {
    const uint64_t now_ns = prt_now_ns();
    if (dma_completion_flag_refresh(tok)) {
      prt_breadcrumb_note(PRT_BREADCRUMB_KIND_DMA,
                          PRT_BREADCRUMB_PHASE_DMA_WAIT_DONEFLAG_POLL_DONE,
                          tok->tensor_id,
                          tok->id,
                          tok->rr_manager_id,
                          PRT_BREADCRUMB_ANY_U32,
                          PRT_OK,
                          dma_breadcrumb_flags_from_token(tok, 0U),
                          tok->debug_src_addr,
                          tok->debug_dst_addr,
                          tok->debug_done_flag_pa,
                          now_ns - start_ns,
                          __LINE__);
      if (emit_progress) {
        PRT_PROGRESS_LOG("dma-wait-doneflag-poll phase=done token=%u stage=%u tensor=%u polls=%u elapsed_ns=%llu",
                         tok->id,
                         tok->stage_idx,
                         tok->tensor_id,
                         polls,
                         (unsigned long long)(now_ns - start_ns));
      }
      return PRT_OK;
    }
    if (now_ns - start_ns >= timeout_ns) {
      prt_breadcrumb_note(PRT_BREADCRUMB_KIND_DMA,
                          PRT_BREADCRUMB_PHASE_DMA_WAIT_DONEFLAG_POLL_TIMEOUT,
                          tok->tensor_id,
                          tok->id,
                          tok->rr_manager_id,
                          PRT_BREADCRUMB_ANY_U32,
                          PRT_ERR_TIMEOUT,
                          dma_breadcrumb_flags_from_token(tok, 0U),
                          tok->debug_src_addr,
                          tok->debug_dst_addr,
                          tok->debug_done_flag_pa,
                          now_ns - start_ns,
                          __LINE__);
      PRT_PROGRESS_LOG("dma-wait-doneflag-poll phase=timeout token=%u stage=%u tensor=%u polls=%u elapsed_ns=%llu timeout_ns=%llu done_pa=0x%llx",
                       tok->id,
                       tok->stage_idx,
                       tok->tensor_id,
                       polls,
                       (unsigned long long)(now_ns - start_ns),
                       (unsigned long long)timeout_ns,
                       (unsigned long long)tok->debug_done_flag_pa);
      tok->status = PRT_ERR_TIMEOUT;
      tok->done = 1;
      return PRT_ERR_TIMEOUT;
    }
    polls += 1U;
    (void)nanosleep(&sleep_ts, NULL);
  }
}

static void dma_log_rr_snapshot_marker(const char *tag, const prt_dma_token_t *tok) {
  const char *snapshot_tag = tag ? tag : "dma-rr-snapshot";
  const uint64_t opc0 = dma_debug_rr_read_opc_map(0U);
  const uint64_t opc1 = dma_debug_rr_read_opc_map(1U);
  const uint64_t opc2 = dma_debug_rr_read_opc_map(2U);
  const uint64_t opc3 = dma_debug_rr_read_opc_map(3U);
  const uint64_t cfg0 = dma_debug_rr_read_cfg(0U);
  const uint64_t cfg1 = dma_debug_rr_read_cfg(1U);
  const uint64_t cfg2 = dma_debug_rr_read_cfg(2U);
  const uint64_t cfg3 = dma_debug_rr_read_cfg(3U);
  const uint32_t reserved_cfg_id = RR_MAX_CFGS - 1U;
  const uint64_t cfg_reserved = dma_debug_rr_read_cfg(reserved_cfg_id);
  uint64_t cfg_scope = UINT64_MAX;
  if (tok && tok->rr_cfg_id < RR_MAX_CFGS) {
    cfg_scope = dma_debug_rr_read_cfg(tok->rr_cfg_id);
  }
  PRT_MARKER_LOG("%s base token=%u stage=%u tensor=%u cfg=%u mgr=%u opc=%u scope_valid=%u hw_done=%d",
                 snapshot_tag,
                 tok ? tok->id : 0U,
                 tok ? tok->stage_idx : 0U,
                 tok ? tok->tensor_id : 0U,
                 tok ? tok->rr_cfg_id : 0U,
                 tok ? tok->rr_manager_id : 0U,
                 tok ? tok->rr_opcode_id : 0U,
                 tok ? (uint32_t)(tok->rr_scope_valid != 0) : 0U,
                 tok ? dma_completion_flag_value(tok) : 0);
  PRT_MARKER_LOG("%s opc token=%u opc0=0x%llx opc1=0x%llx opc2=0x%llx opc3=0x%llx",
                 snapshot_tag,
                 tok ? tok->id : 0U,
                 (unsigned long long)opc0,
                 (unsigned long long)opc1,
                 (unsigned long long)opc2,
                 (unsigned long long)opc3);
  PRT_MARKER_LOG("%s cfg token=%u cfg_scope=0x%llx cfg0=0x%llx cfg1=0x%llx cfg2=0x%llx cfg3=0x%llx cfg_reserved[%u]=0x%llx",
                 snapshot_tag,
                 tok ? tok->id : 0U,
                 (unsigned long long)cfg_scope,
                 (unsigned long long)cfg0,
                 (unsigned long long)cfg1,
                 (unsigned long long)cfg2,
                 (unsigned long long)cfg3,
                 reserved_cfg_id,
                 (unsigned long long)cfg_reserved);
}

#if defined(__linux__) && defined(__riscv)
static int dma_debug_mgrctrl_read_u64(uint32_t manager_id, uint64_t offset, uint64_t *value) {
  static int fd = -2;
  if (!value) return PRT_ERR_INVAL;

  if (fd == -2) {
    fd = open("/dev/mem", O_RDONLY | O_CLOEXEC);
  }
  if (fd < 0) return -errno;

  const off_t addr = (off_t)(0x20000ULL + ((uint64_t)manager_id * 0x1000ULL) + offset);
  uint64_t raw = 0ULL;
  const ssize_t got = pread(fd, &raw, sizeof(raw), addr);
  if (got != (ssize_t)sizeof(raw)) {
    return got < 0 ? -errno : PRT_ERR_IO;
  }

  *value = raw & 0xffULL;
  return PRT_OK;
}
#else
static int dma_debug_mgrctrl_read_u64(uint32_t manager_id, uint64_t offset, uint64_t *value) {
  (void)manager_id;
  (void)offset;
  if (value) *value = 0ULL;
  return PRT_ERR_NOT_IMPL;
}
#endif

static void dma_log_monitor_snapshot_marker(const char *tag, const prt_dma_token_t *tok) {
  const char *snapshot_tag = tag ? tag : "dma-monitor-snapshot";
  const uint64_t valid = hw_dma_read_monitor(DMA_MON_VALID);
  uint64_t src_cmds = 0;
  uint64_t dst_cmds = 0;
  uint64_t req_copy_bytes = 0;
  uint64_t cycles = 0;
  uint64_t effective_bytes = 0;
  uint64_t eff_bw_x1000_bpc = 0;
  if (valid != 0ULL) {
    src_cmds = hw_dma_read_monitor(DMA_MON_SRC_CMDS);
    dst_cmds = hw_dma_read_monitor(DMA_MON_DST_CMDS);
    req_copy_bytes = hw_dma_read_monitor(DMA_MON_REQ_COPY_BYTES);
    cycles = hw_dma_read_monitor(DMA_MON_CYCLES);
    effective_bytes = hw_dma_read_monitor(DMA_MON_EFFECTIVE_BYTES);
    eff_bw_x1000_bpc = hw_dma_read_monitor(DMA_MON_EFF_BW_X1000_BPC);
  }
  PRT_MARKER_LOG("%s mon token=%u stage=%u tensor=%u valid=%llu src_cmds=%llu dst_cmds=%llu req_bytes=%llu cycles=%llu effective_bytes=%llu eff_bw_x1000_bpc=%llu",
                 snapshot_tag,
                 tok ? tok->id : 0U,
                 tok ? tok->stage_idx : 0U,
                 tok ? tok->tensor_id : 0U,
                 (unsigned long long)valid,
                 (unsigned long long)src_cmds,
                 (unsigned long long)dst_cmds,
                 (unsigned long long)req_copy_bytes,
                 (unsigned long long)cycles,
                 (unsigned long long)effective_bytes,
                 (unsigned long long)eff_bw_x1000_bpc);
}
#endif

int prt_dma_backend_init(prt_runtime_t *rt) {
  int rc;
  if (!rt) return PRT_ERR_INVAL;

  rt->progress_thread_enabled = 0;
  rt->dma_pending_head = NULL;
  rt->dma_pending_tail = NULL;
  rt->dma_pending_count = 0;
  rc = dma_completion_pool_init(rt);
  if (rc != PRT_OK) return rc;

  if (rt->cfg.dma_backend == PRT_DMA_BACKEND_BLOCKING_FENCE) {
    rt->dma_ops.submit = dma_blocking_submit;
    rt->dma_ops.wait = dma_blocking_wait;
    rt->dma_ops.submit_and_wait = dma_blocking_submit_and_wait;
    rt->dma_ops.name = "blocking_fence";
    PRT_PROGRESS_LOG("dma-backend using blocking_fence completion via hw_dma_fence + rr_fence_scope");
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
  if (rt->progress_thread_enabled) {
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
  dma_completion_pool_destroy(rt);
}

int prt_dma_submit(prt_runtime_t *rt, const prt_dma_req_t *req, prt_dma_token_t *tok) {
  int rc;
  if (!rt || !req || !tok || !rt->dma_ops.submit) return PRT_ERR_INVAL;
  rc = dma_validate_stage_manager(rt, tok->stage_idx, req->src_acc, "prt_dma_submit/src");
  if (rc != PRT_OK) return rc;
  rc = dma_validate_stage_manager(rt, tok->stage_idx, req->dst_acc, "prt_dma_submit/dst");
  if (rc != PRT_OK) return rc;
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
    dma_completion_flag_release(tok);
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

int prt_dma_copy_spm_pages_prefix(prt_runtime_t *rt, const prt_page_list_t *dst_pages,
                                  const prt_page_list_t *src_pages, uint64_t bytes,
                                  uint32_t manager_id, uint32_t stage_idx,
                                  uint32_t tensor_id, uint64_t timeout_ns) {
  prt_dma_req_t req;
  prt_rr_scope_t scope;
  uint64_t page_bytes;
  uint64_t src_capacity;
  uint64_t dst_capacity;
  uint64_t remaining;
  int rc = PRT_OK;
  if (!rt || !dst_pages || !src_pages || !dst_pages->data || !src_pages->data || bytes == 0U) return PRT_ERR_INVAL;
  if (dst_pages->size == 0 || src_pages->size == 0) return PRT_ERR_INVAL;
  memset(&scope, 0, sizeof(scope));

  page_bytes = rt->cfg.page_size_bytes ? rt->cfg.page_size_bytes : PRT_PAGE_SIZE_BYTES;
  src_capacity = (uint64_t)src_pages->size * page_bytes;
  dst_capacity = (uint64_t)dst_pages->size * page_bytes;
  if (bytes > src_capacity || bytes > dst_capacity) return PRT_ERR_INVAL;

  req.src_acc = manager_id;
  req.dst_acc = manager_id;
  remaining = bytes;
  rc = dma_batch_scope_acquire(rt, stage_idx, manager_id, 2U, &scope);
  if (rc != PRT_OK) return rc;
  for (uint32_t i = 0; i < src_pages->size && i < dst_pages->size && remaining > 0U; ++i) {
    req.src_addr = page_base_addr(rt, &src_pages->data[i]);
    req.dst_addr = page_base_addr(rt, &dst_pages->data[i]);
    req.bytes = dma_min_u64(remaining, page_bytes);
    dma_breadcrumb_page_begin(tensor_id, manager_id, i, &req, 0U, timeout_ns);
    rc = dma_submit_wait_annotated_scoped(rt, &req, stage_idx, tensor_id, timeout_ns, &scope, 0);
    dma_breadcrumb_page_end(tensor_id, manager_id, i, &req, rc, 0U, timeout_ns);
    if (rc != PRT_OK) break;
    remaining -= req.bytes;
  }
  dma_batch_scope_release(&scope);
  if (rc != PRT_OK) return rc;
  return remaining == 0U ? PRT_OK : PRT_ERR_STATE;
}

int prt_dma_copy_spm_pages(prt_runtime_t *rt, const prt_page_list_t *dst_pages,
                           const prt_page_list_t *src_pages, uint32_t manager_id,
                           uint32_t stage_idx, uint32_t tensor_id, uint64_t timeout_ns) {
  uint64_t page_bytes;
  if (!rt || !dst_pages || !src_pages || !dst_pages->data || !src_pages->data) return PRT_ERR_INVAL;
  if (dst_pages->size == 0 || src_pages->size == 0 || dst_pages->size != src_pages->size) return PRT_ERR_INVAL;
  page_bytes = rt->cfg.page_size_bytes ? rt->cfg.page_size_bytes : PRT_PAGE_SIZE_BYTES;
  return prt_dma_copy_spm_pages_prefix(rt, dst_pages, src_pages,
                                       (uint64_t)dst_pages->size * page_bytes,
                                       manager_id, stage_idx, tensor_id, timeout_ns);
}

int prt_dma_copy_dram_to_spm_pages_prefix(prt_runtime_t *rt, const prt_page_list_t *dst_pages,
                                          uint64_t src_dram_addr, uint64_t bytes,
                                          uint32_t manager_id, uint32_t stage_idx,
                                          uint32_t tensor_id, uint64_t timeout_ns) {
  prt_dma_req_t req;
  prt_rr_scope_t scope;
  uint64_t page_bytes;
  uint64_t dst_capacity;
  uint64_t remaining;
  if (!rt || !dst_pages || !dst_pages->data || dst_pages->size == 0 || bytes == 0U) return PRT_ERR_INVAL;
  memset(&scope, 0, sizeof(scope));
  page_bytes = rt->cfg.page_size_bytes ? rt->cfg.page_size_bytes : PRT_PAGE_SIZE_BYTES;
  dst_capacity = (uint64_t)dst_pages->size * page_bytes;
  if (bytes > dst_capacity) return PRT_ERR_INVAL;
#if defined(__riscv) && defined(__linux__)
  return dma_copy_host_to_spm_pages_linux(rt, dst_pages,
                                          (const uint8_t *)(uintptr_t)src_dram_addr,
                                          manager_id, stage_idx, tensor_id, timeout_ns, bytes);
#endif
  req.src_acc = manager_id;
  req.dst_acc = manager_id;
  remaining = bytes;
  {
    int rc = dma_batch_scope_acquire(rt, stage_idx, manager_id, 2U, &scope);
    if (rc != PRT_OK) return rc;
  }
  for (uint32_t i = 0; i < dst_pages->size && remaining > 0U; ++i) {
    int rc;
    req.src_addr = src_dram_addr + (uint64_t)i * page_bytes;
    req.dst_addr = page_base_addr(rt, &dst_pages->data[i]);
    req.bytes = dma_min_u64(remaining, page_bytes);
    dma_breadcrumb_page_begin(tensor_id, manager_id, i, &req, 0U, timeout_ns);
    rc = dma_submit_wait_annotated_scoped(rt, &req, stage_idx, tensor_id, timeout_ns, &scope, 0);
    dma_breadcrumb_page_end(tensor_id, manager_id, i, &req, rc, 0U, timeout_ns);
    if (rc != PRT_OK) {
      dma_batch_scope_release(&scope);
      return rc;
    }
    remaining -= req.bytes;
  }
  dma_batch_scope_release(&scope);
  return remaining == 0U ? PRT_OK : PRT_ERR_STATE;
}

int prt_dma_copy_dram_to_spm_pages(prt_runtime_t *rt, const prt_page_list_t *dst_pages,
                                   uint64_t src_dram_addr, uint32_t manager_id,
                                   uint32_t stage_idx, uint32_t tensor_id, uint64_t timeout_ns) {
  uint64_t page_bytes;
  if (!rt || !dst_pages || !dst_pages->data || dst_pages->size == 0) return PRT_ERR_INVAL;
  page_bytes = rt->cfg.page_size_bytes ? rt->cfg.page_size_bytes : PRT_PAGE_SIZE_BYTES;
  return prt_dma_copy_dram_to_spm_pages_prefix(rt, dst_pages, src_dram_addr,
                                               (uint64_t)dst_pages->size * page_bytes,
                                               manager_id, stage_idx, tensor_id, timeout_ns);
}

int prt_dma_copy_spm_pages_to_dram_prefix(prt_runtime_t *rt, uint64_t dst_dram_addr,
                                          const prt_page_list_t *src_pages, uint64_t bytes,
                                          uint32_t manager_id, uint32_t stage_idx,
                                          uint32_t tensor_id, uint64_t timeout_ns) {
  prt_dma_req_t req;
  prt_rr_scope_t scope;
  uint64_t page_bytes;
  uint64_t src_capacity;
  uint64_t remaining;
  if (!rt || !src_pages || !src_pages->data || src_pages->size == 0 || bytes == 0U) return PRT_ERR_INVAL;
  memset(&scope, 0, sizeof(scope));
  page_bytes = rt->cfg.page_size_bytes ? rt->cfg.page_size_bytes : PRT_PAGE_SIZE_BYTES;
  src_capacity = (uint64_t)src_pages->size * page_bytes;
  if (bytes > src_capacity) return PRT_ERR_INVAL;
#if defined(__riscv) && defined(__linux__)
  return dma_copy_spm_pages_to_host_linux(rt, (uint8_t *)(uintptr_t)dst_dram_addr,
                                          src_pages, manager_id, stage_idx, tensor_id, timeout_ns, bytes);
#endif
  req.src_acc = manager_id;
  req.dst_acc = manager_id;
  remaining = bytes;
  {
    int rc = dma_batch_scope_acquire(rt, stage_idx, manager_id, 2U, &scope);
    if (rc != PRT_OK) return rc;
  }
  for (uint32_t i = 0; i < src_pages->size && remaining > 0U; ++i) {
    int rc;
    req.src_addr = page_base_addr(rt, &src_pages->data[i]);
    req.dst_addr = dst_dram_addr + (uint64_t)i * page_bytes;
    req.bytes = dma_min_u64(remaining, page_bytes);
    dma_breadcrumb_page_begin(tensor_id, manager_id, i, &req, 0U, timeout_ns);
    rc = dma_submit_wait_annotated_scoped(rt, &req, stage_idx, tensor_id, timeout_ns, &scope, 0);
    dma_breadcrumb_page_end(tensor_id, manager_id, i, &req, rc, 0U, timeout_ns);
    if (rc != PRT_OK) {
      dma_batch_scope_release(&scope);
      return rc;
    }
    remaining -= req.bytes;
  }
  dma_batch_scope_release(&scope);
  return remaining == 0U ? PRT_OK : PRT_ERR_STATE;
}

int prt_dma_copy_spm_pages_to_dram(prt_runtime_t *rt, uint64_t dst_dram_addr,
                                   const prt_page_list_t *src_pages, uint32_t manager_id,
                                   uint32_t stage_idx, uint32_t tensor_id, uint64_t timeout_ns) {
  uint64_t page_bytes;
  if (!rt || !src_pages || !src_pages->data || src_pages->size == 0) return PRT_ERR_INVAL;
  page_bytes = rt->cfg.page_size_bytes ? rt->cfg.page_size_bytes : PRT_PAGE_SIZE_BYTES;
  return prt_dma_copy_spm_pages_to_dram_prefix(rt, dst_dram_addr, src_pages,
                                               (uint64_t)src_pages->size * page_bytes,
                                               manager_id, stage_idx, tensor_id, timeout_ns);
}

int prt_dma_copy_spm_va(prt_runtime_t *rt, uint64_t dst_va, uint64_t src_va, uint64_t bytes,
                        uint32_t manager_id, uint32_t stage_idx, uint32_t tensor_id,
                        uint64_t timeout_ns) {
  prt_spm_xlate_seg_t *src_segs = NULL;
  prt_spm_xlate_seg_t *dst_segs = NULL;
  prt_rr_scope_t scope;
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
  memset(&scope, 0, sizeof(scope));
  rc = dma_validate_stage_manager(rt, stage_idx, manager_id, "prt_dma_copy_spm_va");
  if (rc != PRT_OK) return rc;
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
  rc = dma_batch_scope_acquire(rt, stage_idx, manager_id, 2U, &scope);
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
    rc = dma_submit_wait_annotated_scoped(rt, &req, stage_idx, tensor_id, timeout_ns, &scope, 0);
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
  dma_batch_scope_release(&scope);
  free(src_segs);
  free(dst_segs);
  return rc;
}

static int dma_blocking_submit(prt_runtime_t *rt, const prt_dma_req_t *req, prt_dma_token_t *tok) {
  uint64_t progress_src_addr;
  uint64_t progress_dst_addr;
  uint64_t progress_bytes;
  int trace_submit = 0;
  int sparse_submit_probe = 0;
  int fixed_submit_probe = 0;
  int export_submit_probe = 0;
  int checkpoint_submit = 0;

  if (!req || !tok) return PRT_ERR_INVAL;

  dma_token_init(tok);
  tok->owner_rt = rt;
  tok->id = __sync_add_and_fetch(&rt->next_dma_token_id, 1);
  tok->submit_ns = prt_now_ns();
  progress_src_addr = req->src_addr;
  progress_dst_addr = req->dst_addr;
  progress_bytes = req->bytes;
  prt_breadcrumb_note(PRT_BREADCRUMB_KIND_DMA,
                      PRT_BREADCRUMB_PHASE_DMA_SUBMIT_BEGIN,
                      tok->tensor_id,
                      tok->id,
                      req->dst_acc,
                      PRT_BREADCRUMB_ANY_U32,
                      PRT_OK,
                      dma_breadcrumb_flags_from_token(tok, 0U),
                      progress_src_addr,
                      progress_dst_addr,
                      progress_bytes,
                      0ULL,
                      __LINE__);
#if !PRT_ENABLE_PROGRESS_LOG || !defined(__riscv)
  (void)progress_src_addr;
  (void)progress_dst_addr;
  (void)progress_bytes;
#endif
#if !defined(__riscv)
  (void)trace_submit;
  (void)fixed_submit_probe;
  (void)export_submit_probe;
  (void)checkpoint_submit;
#endif
  trace_submit = dma_should_marker_trace_submit(req, tok->tensor_id);
  sparse_submit_probe = trace_submit &&
                        tok->stage_idx == 0U &&
                        prt_log_gate_allow_deep_logs() &&
                        progress_bytes > 0ULL &&
                        progress_bytes <= 4096ULL;
  checkpoint_submit = dma_should_checkpoint_submit_wait_tok(tok);
  prt_trace_on_dma_submit(rt);
  if (sparse_submit_probe) {
    PRT_PROGRESS_LOG("dma-submit-inner stage=%u tensor=%u phase=after-trace-count tok=%u src=0x%llx dst=0x%llx bytes=%llu",
                     tok->stage_idx, tok->tensor_id, tok->id,
                     (unsigned long long)progress_src_addr,
                     (unsigned long long)progress_dst_addr,
                     (unsigned long long)progress_bytes);
  }
  prt_trace_log_event(rt, tok->stage_idx, PRT_TRACE_EVT_DMA_SUBMIT,
                      tok->tensor_id,
                      req->bytes > UINT32_MAX ? UINT32_MAX : (uint32_t)req->bytes);
  if (sparse_submit_probe) {
    PRT_PROGRESS_LOG("dma-submit-inner stage=%u tensor=%u phase=after-trace-event tok=%u kind=%u",
                     tok->stage_idx, tok->tensor_id, tok->id, (uint32_t)PRT_TRACE_EVT_DMA_SUBMIT);
  }
  dma_debug_capture_req(tok, req);
  fixed_submit_probe = dma_should_sparse_fixed_load_submit_probe_tok(tok);
  export_submit_probe = dma_should_sparse_export_submit_probe_tok(tok);
  if (sparse_submit_probe) {
    PRT_PROGRESS_LOG("dma-submit-inner stage=%u tensor=%u phase=after-capture-req tok=%u done_va=0x%llx",
                     tok->stage_idx, tok->tensor_id, tok->id,
                     (unsigned long long)tok->debug_done_flag_va);
  }
  if (export_submit_probe) {
    PRT_PROGRESS_LOG("dma-export-submit stage=%u tensor=%u phase=enter tok=%u src=0x%llx dst=0x%llx bytes=%llu",
                     tok->stage_idx, tok->tensor_id, tok->id,
                     (unsigned long long)progress_src_addr,
                     (unsigned long long)progress_dst_addr,
                     (unsigned long long)progress_bytes);
  }

#if defined(__riscv)
  {
    prt_rr_scope_t scope;
    int have_scope = tok->rr_scope_valid != 0;
    int rrc = PRT_OK;
    memset(&scope, 0, sizeof(scope));
    if (have_scope) {
      scope.valid = tok->rr_scope_valid;
      scope.cfg_id = tok->rr_cfg_id;
      scope.stage_id = tok->stage_idx;
      scope.manager_id = tok->rr_manager_id;
      scope.opcode_id = tok->rr_opcode_id;
    }
    if (sparse_submit_probe) {
      PRT_PROGRESS_LOG("dma-submit-inner stage=%u tensor=%u phase=before-rr-marker tok=%u mgr=%u",
                       tok->stage_idx, tok->tensor_id, tok->id, req->dst_acc);
    }
    if (checkpoint_submit) {
      PRT_CHECKPOINT_LOG("dma stage=%u checkpoint=rr-acquire-begin tensor=%u src=0x%llx dst=0x%llx bytes=%llu dst_acc=%u",
                         tok->stage_idx, tok->tensor_id,
                         (unsigned long long)progress_src_addr,
                         (unsigned long long)progress_dst_addr,
                         (unsigned long long)progress_bytes,
                         req->dst_acc);
      PRT_CHECKPOINT_LOG("dma stage=%u checkpoint=rr-scope-state tensor=%u have_scope=%u scope_valid=%u scope_cfg=%u rr_mgr=%u rr_opc=%u",
                         tok->stage_idx, tok->tensor_id,
                         (uint32_t)have_scope,
                         (uint32_t)scope.valid,
                         scope.cfg_id,
                         scope.manager_id,
                         scope.opcode_id);
    }
    if (trace_submit) {
      PRT_MARKER_LOG("dma-submit stage=%u tensor=%u phase=rr-acquire-begin mgr=%u",
                     tok->stage_idx, tok->tensor_id, req->dst_acc);
    }
    if (sparse_submit_probe) {
      PRT_PROGRESS_LOG("dma-submit-inner stage=%u tensor=%u phase=after-rr-marker tok=%u mgr=%u",
                       tok->stage_idx, tok->tensor_id, tok->id, req->dst_acc);
    }
    if (!have_scope) {
      if (checkpoint_submit) {
        PRT_CHECKPOINT_LOG("dma stage=%u checkpoint=rr-acquire-call-begin tensor=%u dst_acc=%u",
                           tok->stage_idx, tok->tensor_id, req->dst_acc);
      }
      PRT_PROGRESS_RAW_LINE("[prt-raw] dma pre-acquire");
      rrc = prt_rr_acquire_scope(rt, tok->stage_idx, req->dst_acc, 2U, &scope);
      if (checkpoint_submit) {
        PRT_CHECKPOINT_LOG("dma stage=%u checkpoint=rr-acquire-call-end tensor=%u rc=%d cfg=%u rr_mgr=%u rr_opc=%u valid=%u",
                           tok->stage_idx, tok->tensor_id, rrc,
                           scope.cfg_id, scope.manager_id, scope.opcode_id,
                           (uint32_t)scope.valid);
      }
    } else if (checkpoint_submit) {
      PRT_CHECKPOINT_LOG("dma stage=%u checkpoint=rr-acquire-reuse tensor=%u cfg=%u rr_mgr=%u rr_opc=%u valid=%u",
                         tok->stage_idx, tok->tensor_id,
                         scope.cfg_id, scope.manager_id, scope.opcode_id,
                         (uint32_t)scope.valid);
    } else if (sparse_submit_probe) {
      PRT_PROGRESS_LOG("dma-submit-inner stage=%u tensor=%u phase=reuse-rr-scope tok=%u cfg=%u rr_mgr=%u rr_opc=%u",
                       tok->stage_idx, tok->tensor_id, tok->id,
                       scope.cfg_id, scope.manager_id, scope.opcode_id);
    }
    if (sparse_submit_probe) {
      PRT_PROGRESS_LOG("dma-submit-inner stage=%u tensor=%u phase=after-rr-acquire-call tok=%u rc=%d cfg=%u rr_mgr=%u rr_opc=%u valid=%u",
                       tok->stage_idx, tok->tensor_id, tok->id, rrc,
                       scope.cfg_id, scope.manager_id, scope.opcode_id,
                       (uint32_t)scope.valid);
    }
    if (checkpoint_submit) {
      PRT_CHECKPOINT_LOG("dma stage=%u checkpoint=rr-acquire-end tensor=%u rc=%d cfg=%u rr_mgr=%u rr_opc=%u",
                         tok->stage_idx, tok->tensor_id, rrc,
                         scope.cfg_id, scope.manager_id, scope.opcode_id);
    }
    if (trace_submit) {
      PRT_MARKER_LOG("dma-submit stage=%u tensor=%u phase=rr-acquire-end rc=%d cfg=%u rr_mgr=%u rr_opc=%u",
                     tok->stage_idx, tok->tensor_id, rrc,
                     scope.cfg_id, scope.manager_id, scope.opcode_id);
    }
    if (rrc != PRT_OK) {
      tok->done = 1;
      tok->status = rrc;
      return rrc;
    }
    if (!have_scope) PRT_PROGRESS_RAW_LINE("[prt-raw] dma acquire-ok");
    if (trace_submit) {
      PRT_MARKER_LOG("dma-submit stage=%u tensor=%u phase=rr-acquire-postcheck cfg=%u rr_mgr=%u rr_opc=%u",
                     tok->stage_idx, tok->tensor_id,
                     scope.cfg_id, scope.manager_id, scope.opcode_id);
    }
    if (sparse_submit_probe) {
      PRT_PROGRESS_LOG("dma-submit-inner stage=%u tensor=%u phase=after-rr-postcheck tok=%u cfg=%u rr_mgr=%u rr_opc=%u",
                       tok->stage_idx, tok->tensor_id, tok->id,
                       scope.cfg_id, scope.manager_id, scope.opcode_id);
    }
    if (export_submit_probe) {
      PRT_PROGRESS_LOG("dma-export-submit stage=%u tensor=%u phase=after-rr-postcheck tok=%u cfg=%u rr_mgr=%u rr_opc=%u",
                       tok->stage_idx, tok->tensor_id, tok->id,
                       scope.cfg_id, scope.manager_id, scope.opcode_id);
    }
    prt_breadcrumb_note(PRT_BREADCRUMB_KIND_DMA,
                        PRT_BREADCRUMB_PHASE_DMA_RR_POSTCHECK,
                        tok->tensor_id,
                        tok->id,
                        scope.manager_id,
                        PRT_BREADCRUMB_ANY_U32,
                        rrc,
                        dma_breadcrumb_flags_from_token(tok, 0U),
                        progress_src_addr,
                        progress_dst_addr,
                        ((uint64_t)scope.cfg_id << 32) | (uint64_t)scope.opcode_id,
                        progress_bytes,
                        __LINE__);
    tok->rr_scope_valid = scope.valid;
    tok->rr_cfg_id = scope.cfg_id;
    tok->rr_manager_id = scope.manager_id;
    tok->rr_opcode_id = scope.opcode_id;
    if (trace_submit) {
      PRT_MARKER_LOG("dma-submit stage=%u tensor=%u phase=rr-state-install-end valid=%d cfg=%u rr_mgr=%u rr_opc=%u",
                     tok->stage_idx, tok->tensor_id,
                     tok->rr_scope_valid, tok->rr_cfg_id,
                     tok->rr_manager_id, tok->rr_opcode_id);
    }
    if (sparse_submit_probe) {
      PRT_PROGRESS_LOG("dma-submit-inner stage=%u tensor=%u phase=after-rr-state-install tok=%u valid=%u cfg=%u rr_mgr=%u rr_opc=%u",
                       tok->stage_idx, tok->tensor_id, tok->id,
                       (uint32_t)tok->rr_scope_valid, tok->rr_cfg_id,
                       tok->rr_manager_id, tok->rr_opcode_id);
    }
    if (export_submit_probe) {
      PRT_PROGRESS_LOG("dma-export-submit stage=%u tensor=%u phase=after-rr-state-install tok=%u valid=%u cfg=%u rr_mgr=%u rr_opc=%u",
                       tok->stage_idx, tok->tensor_id, tok->id,
                       (uint32_t)tok->rr_scope_valid, tok->rr_cfg_id,
                       tok->rr_manager_id, tok->rr_opcode_id);
    }
  }
  tok->hw_done_flag = 0;
  if (trace_submit) {
    PRT_MARKER_LOG("dma-submit stage=%u tensor=%u phase=doneflag-clear value=%d",
                   tok->stage_idx, tok->tensor_id, tok->hw_done_flag);
  }
  if (sparse_submit_probe) {
    PRT_PROGRESS_LOG("dma-submit-inner stage=%u tensor=%u phase=after-doneflag-clear tok=%u value=%d",
                     tok->stage_idx, tok->tensor_id, tok->id, tok->hw_done_flag);
  }
  if (export_submit_probe) {
    PRT_PROGRESS_LOG("dma-export-submit stage=%u tensor=%u phase=after-doneflag-clear tok=%u value=%d",
                     tok->stage_idx, tok->tensor_id, tok->id, tok->hw_done_flag);
  }
  if (checkpoint_submit) {
    PRT_CHECKPOINT_LOG("dma stage=%u checkpoint=doneflag-begin tensor=%u", tok->stage_idx, tok->tensor_id);
  }
  if (trace_submit) {
    PRT_MARKER_LOG("dma-submit stage=%u tensor=%u phase=doneflag-begin",
                   tok->stage_idx, tok->tensor_id);
  }
  if (sparse_submit_probe) {
    PRT_PROGRESS_LOG("dma-submit-inner stage=%u tensor=%u phase=before-doneflag-acquire tok=%u",
                     tok->stage_idx, tok->tensor_id, tok->id);
  }
  dma_debug_capture_done_flag(rt, tok);
  if (sparse_submit_probe) {
    PRT_PROGRESS_LOG("dma-submit-inner stage=%u tensor=%u phase=after-doneflag-acquire tok=%u rc=%d slot=%u done_va=0x%llx done_pa=0x%llx",
                     tok->stage_idx, tok->tensor_id, tok->id,
                     tok->hw_done_flag_pa_rc, tok->completion_slot,
                     (unsigned long long)tok->debug_done_flag_va,
                     (unsigned long long)tok->debug_done_flag_pa);
  }
  if (export_submit_probe) {
    PRT_PROGRESS_LOG("dma-export-submit stage=%u tensor=%u phase=after-doneflag-acquire tok=%u rc=%d slot=%u done_pa=0x%llx",
                     tok->stage_idx, tok->tensor_id, tok->id,
                     tok->hw_done_flag_pa_rc, tok->completion_slot,
                     (unsigned long long)tok->debug_done_flag_pa);
  }
  if (checkpoint_submit) {
    PRT_CHECKPOINT_LOG("dma stage=%u checkpoint=doneflag-end tensor=%u rc=%d done_pa=0x%llx",
                       tok->stage_idx, tok->tensor_id, tok->hw_done_flag_pa_rc,
                       (unsigned long long)tok->debug_done_flag_pa);
  }
  if (fixed_submit_probe) {
    PRT_PROGRESS_LOG("dma-fixed-load-submit stage=%u tensor=%u phase=doneflag-end tok=%u src=0x%llx dst=0x%llx bytes=%llu rc=%d done_pa=0x%llx",
                     tok->stage_idx, tok->tensor_id, tok->id,
                     (unsigned long long)progress_src_addr,
                     (unsigned long long)progress_dst_addr,
                     (unsigned long long)progress_bytes,
                     tok->hw_done_flag_pa_rc,
                     (unsigned long long)tok->debug_done_flag_pa);
  }
  prt_breadcrumb_note(PRT_BREADCRUMB_KIND_DMA,
                      PRT_BREADCRUMB_PHASE_DMA_DONEFLAG_END,
                      tok->tensor_id,
                      tok->id,
                      tok->rr_manager_id ? tok->rr_manager_id : req->dst_acc,
                      PRT_BREADCRUMB_ANY_U32,
                      tok->hw_done_flag_pa_rc,
                      dma_breadcrumb_flags_from_token(tok, 0U),
                      progress_src_addr,
                      progress_dst_addr,
                      tok->debug_done_flag_pa,
                      progress_bytes,
                      __LINE__);
  if (export_submit_probe) {
    PRT_PROGRESS_LOG("dma-export-submit stage=%u tensor=%u phase=doneflag-end tok=%u src=0x%llx dst=0x%llx bytes=%llu rc=%d done_pa=0x%llx",
                     tok->stage_idx, tok->tensor_id, tok->id,
                     (unsigned long long)progress_src_addr,
                     (unsigned long long)progress_dst_addr,
                     (unsigned long long)progress_bytes,
                     tok->hw_done_flag_pa_rc,
                     (unsigned long long)tok->debug_done_flag_pa);
  }
  if (trace_submit) {
    PRT_MARKER_LOG("dma-submit stage=%u tensor=%u phase=doneflag-end rc=%d done_va=0x%llx done_pa=0x%llx",
                   tok->stage_idx, tok->tensor_id, tok->hw_done_flag_pa_rc,
                   (unsigned long long)tok->debug_done_flag_va,
                   (unsigned long long)tok->debug_done_flag_pa);
  }
  if (tok->hw_done_flag_pa_rc != PRT_OK) {
    dma_token_release_scope(tok, 0);
    tok->done = 1;
    tok->status = tok->hw_done_flag_pa_rc;
    return tok->hw_done_flag_pa_rc;
  }
  dma_tracerv_dma_window_marker_if_scope(tok->stage_idx, tok->tensor_id,
                                         PRT_DMA_TRACERV_MARKER_PROGRAM_BEGIN);
  PRT_PROGRESS_RAW_LINE("[prt-raw] dma pre-program");
  if (checkpoint_submit) {
    PRT_CHECKPOINT_LOG("dma stage=%u checkpoint=program-begin tensor=%u src=0x%llx dst=0x%llx bytes=%llu done_pa=0x%llx",
                       tok->stage_idx, tok->tensor_id,
                       (unsigned long long)progress_src_addr,
                       (unsigned long long)progress_dst_addr,
                       (unsigned long long)progress_bytes,
                       (unsigned long long)tok->debug_done_flag_pa);
  }
  if (trace_submit) {
    PRT_MARKER_LOG("dma-submit stage=%u tensor=%u phase=program-begin src=0x%llx dst=0x%llx bytes=%llu done_pa=0x%llx",
                   tok->stage_idx, tok->tensor_id,
                   (unsigned long long)progress_src_addr,
                   (unsigned long long)progress_dst_addr,
                   (unsigned long long)progress_bytes,
                   (unsigned long long)tok->debug_done_flag_pa);
  }
  if (fixed_submit_probe) {
    PRT_PROGRESS_LOG("dma-fixed-load-submit stage=%u tensor=%u phase=program-begin tok=%u src=0x%llx dst=0x%llx bytes=%llu done_pa=0x%llx",
                     tok->stage_idx, tok->tensor_id, tok->id,
                     (unsigned long long)progress_src_addr,
                     (unsigned long long)progress_dst_addr,
                     (unsigned long long)progress_bytes,
                     (unsigned long long)tok->debug_done_flag_pa);
  }
  prt_breadcrumb_note(PRT_BREADCRUMB_KIND_DMA,
                      PRT_BREADCRUMB_PHASE_DMA_PROGRAM_BEGIN,
                      tok->tensor_id,
                      tok->id,
                      tok->rr_manager_id ? tok->rr_manager_id : req->dst_acc,
                      PRT_BREADCRUMB_ANY_U32,
                      PRT_OK,
                      dma_breadcrumb_flags_from_token(tok, 0U),
                      progress_src_addr,
                      progress_dst_addr,
                      tok->debug_done_flag_pa,
                      progress_bytes,
                      __LINE__);
  if (fixed_submit_probe) {
    PRT_PROGRESS_LOG("dma-fixed-load-submit stage=%u tensor=%u phase=program-post-breadcrumb tok=%u src=0x%llx dst=0x%llx bytes=%llu done_pa=0x%llx",
                     tok->stage_idx, tok->tensor_id, tok->id,
                     (unsigned long long)progress_src_addr,
                     (unsigned long long)progress_dst_addr,
                     (unsigned long long)progress_bytes,
                     (unsigned long long)tok->debug_done_flag_pa);
  }
  if (export_submit_probe) {
    PRT_PROGRESS_LOG("dma-export-submit stage=%u tensor=%u phase=program-begin tok=%u src=0x%llx dst=0x%llx bytes=%llu done_pa=0x%llx",
                     tok->stage_idx, tok->tensor_id, tok->id,
                     (unsigned long long)progress_src_addr,
                     (unsigned long long)progress_dst_addr,
                     (unsigned long long)progress_bytes,
                     (unsigned long long)tok->debug_done_flag_pa);
  }
  if (sparse_submit_probe) {
    PRT_PROGRESS_LOG("dma-submit-inner stage=%u tensor=%u phase=before-program-fence tok=%u src=0x%llx dst=0x%llx bytes=%llu done_pa=0x%llx",
                     tok->stage_idx, tok->tensor_id, tok->id,
                     (unsigned long long)progress_src_addr,
                     (unsigned long long)progress_dst_addr,
                     (unsigned long long)progress_bytes,
                     (unsigned long long)tok->debug_done_flag_pa);
  }
  hw_dma_submit_fence();
  dma_tracerv_dma_window_marker_if_scope(tok->stage_idx, tok->tensor_id,
                                         PRT_DMA_TRACERV_MARKER_PROGRAM_POST_FENCE);
  if (checkpoint_submit) {
    PRT_CHECKPOINT_LOG("dma stage=%u checkpoint=program-post-fence tensor=%u", tok->stage_idx, tok->tensor_id);
  }
  if (trace_submit) {
    PRT_MARKER_LOG("dma-submit stage=%u tensor=%u phase=program-post-fence done_pa=0x%llx",
                   tok->stage_idx, tok->tensor_id,
                   (unsigned long long)tok->debug_done_flag_pa);
  }
  if (fixed_submit_probe) {
    PRT_PROGRESS_LOG("dma-fixed-load-submit stage=%u tensor=%u phase=program-post-fence tok=%u done_pa=0x%llx",
                     tok->stage_idx, tok->tensor_id, tok->id,
                     (unsigned long long)tok->debug_done_flag_pa);
  }
  prt_breadcrumb_note(PRT_BREADCRUMB_KIND_DMA,
                      PRT_BREADCRUMB_PHASE_DMA_PROGRAM_POST_FENCE,
                      tok->tensor_id,
                      tok->id,
                      tok->rr_manager_id ? tok->rr_manager_id : req->dst_acc,
                      PRT_BREADCRUMB_ANY_U32,
                      PRT_OK,
                      dma_breadcrumb_flags_from_token(tok, 0U),
                      progress_src_addr,
                      progress_dst_addr,
                      tok->debug_done_flag_pa,
                      progress_bytes,
                      __LINE__);
  if (export_submit_probe) {
    PRT_PROGRESS_LOG("dma-export-submit stage=%u tensor=%u phase=program-post-fence tok=%u done_pa=0x%llx",
                     tok->stage_idx, tok->tensor_id, tok->id,
                     (unsigned long long)tok->debug_done_flag_pa);
  }
  if (sparse_submit_probe) {
    PRT_PROGRESS_LOG("dma-submit-inner stage=%u tensor=%u phase=after-program-fence tok=%u done_pa=0x%llx",
                     tok->stage_idx, tok->tensor_id, tok->id,
                     (unsigned long long)tok->debug_done_flag_pa);
  }
  hw_dma_set_dst(progress_dst_addr, tok->debug_done_flag_pa);
  dma_tracerv_dma_window_marker_if_scope(tok->stage_idx, tok->tensor_id,
                                         PRT_DMA_TRACERV_MARKER_PROGRAM_POST_DST);
  if (checkpoint_submit) {
    PRT_CHECKPOINT_LOG("dma stage=%u checkpoint=program-post-dst tensor=%u dst=0x%llx done_pa=0x%llx",
                       tok->stage_idx, tok->tensor_id,
                       (unsigned long long)progress_dst_addr,
                       (unsigned long long)tok->debug_done_flag_pa);
  }
  if (trace_submit) {
    PRT_MARKER_LOG("dma-submit stage=%u tensor=%u phase=program-post-dst dst=0x%llx done_pa=0x%llx",
                   tok->stage_idx, tok->tensor_id,
                   (unsigned long long)progress_dst_addr,
                   (unsigned long long)tok->debug_done_flag_pa);
  }
  if (fixed_submit_probe) {
    PRT_PROGRESS_LOG("dma-fixed-load-submit stage=%u tensor=%u phase=program-post-dst tok=%u dst=0x%llx done_pa=0x%llx",
                     tok->stage_idx, tok->tensor_id, tok->id,
                     (unsigned long long)progress_dst_addr,
                     (unsigned long long)tok->debug_done_flag_pa);
  }
  prt_breadcrumb_note(PRT_BREADCRUMB_KIND_DMA,
                      PRT_BREADCRUMB_PHASE_DMA_PROGRAM_POST_DST,
                      tok->tensor_id,
                      tok->id,
                      tok->rr_manager_id ? tok->rr_manager_id : req->dst_acc,
                      PRT_BREADCRUMB_ANY_U32,
                      PRT_OK,
                      dma_breadcrumb_flags_from_token(tok, 0U),
                      progress_src_addr,
                      progress_dst_addr,
                      tok->debug_done_flag_pa,
                      progress_bytes,
                      __LINE__);
  if (export_submit_probe) {
    PRT_PROGRESS_LOG("dma-export-submit stage=%u tensor=%u phase=program-post-dst tok=%u dst=0x%llx done_pa=0x%llx",
                     tok->stage_idx, tok->tensor_id, tok->id,
                     (unsigned long long)progress_dst_addr,
                     (unsigned long long)tok->debug_done_flag_pa);
  }
  if (sparse_submit_probe) {
    PRT_PROGRESS_LOG("dma-submit-inner stage=%u tensor=%u phase=after-program-dst tok=%u dst=0x%llx done_pa=0x%llx",
                     tok->stage_idx, tok->tensor_id, tok->id,
                     (unsigned long long)progress_dst_addr,
                     (unsigned long long)tok->debug_done_flag_pa);
  }
  if (fixed_submit_probe) {
    const uint64_t opc_binding = dma_debug_rr_read_opc_map(tok->rr_opcode_id);
    const uint64_t cfg_state = dma_debug_rr_read_cfg(tok->rr_cfg_id);
    uint64_t mgr_busy = 0ULL;
    uint64_t rocc_busy = 0ULL;
    const int mgr_busy_rc = dma_debug_mgrctrl_read_u64(tok->rr_manager_id, 0x000ULL, &mgr_busy);
    const int rocc_busy_rc = dma_debug_mgrctrl_read_u64(tok->rr_manager_id, 0x008ULL, &rocc_busy);

    PRT_PROGRESS_LOG("dma-fixed-load-submit stage=%u tensor=%u phase=program-pre-src-rr tok=%u cfg=%u mgr=%u opc=%u opc_binding=%llu cfg_state=0x%llx mgr_busy_rc=%d mgr_busy=%llu rocc_busy_rc=%d rocc_busy=%llu",
                     tok->stage_idx, tok->tensor_id, tok->id,
                     tok->rr_cfg_id,
                     tok->rr_manager_id,
                     tok->rr_opcode_id,
                     (unsigned long long)opc_binding,
                     (unsigned long long)cfg_state,
                     mgr_busy_rc,
                     (unsigned long long)mgr_busy,
                     rocc_busy_rc,
                     (unsigned long long)rocc_busy);
  }
  if (fixed_submit_probe && dma_fixed_load_monitor_probe_enabled()) {
    uint64_t mon_valid = hw_dma_read_monitor(DMA_MON_VALID);
    uint64_t mon_src_cmds = 0ULL;
    uint64_t mon_dst_cmds = 0ULL;
    uint64_t mon_req_copy_bytes = 0ULL;
    uint64_t mon_effective_bytes = 0ULL;
    uint64_t outstanding_bytes = 0ULL;
    if (mon_valid != 0ULL) {
      mon_src_cmds = hw_dma_read_monitor(DMA_MON_SRC_CMDS);
      mon_dst_cmds = hw_dma_read_monitor(DMA_MON_DST_CMDS);
      mon_req_copy_bytes = hw_dma_read_monitor(DMA_MON_REQ_COPY_BYTES);
      mon_effective_bytes = hw_dma_read_monitor(DMA_MON_EFFECTIVE_BYTES);
      if (mon_req_copy_bytes >= mon_effective_bytes) {
        outstanding_bytes = mon_req_copy_bytes - mon_effective_bytes;
      }
    }
    PRT_PROGRESS_LOG("dma-fixed-load-submit stage=%u tensor=%u phase=program-pre-src-monitor tok=%u valid=%llu src_cmds=%llu dst_cmds=%llu req_bytes=%llu effective_bytes=%llu outstanding_bytes=%llu",
                     tok->stage_idx, tok->tensor_id, tok->id,
                     (unsigned long long)mon_valid,
                     (unsigned long long)mon_src_cmds,
                     (unsigned long long)mon_dst_cmds,
                     (unsigned long long)mon_req_copy_bytes,
                     (unsigned long long)mon_effective_bytes,
                     (unsigned long long)outstanding_bytes);
  }
  {
    const uint32_t pre_src_nops = dma_fixed_load_pre_src_nops();
    if (pre_src_nops != 0U) {
      if (fixed_submit_probe) {
        PRT_PROGRESS_LOG("dma-fixed-load-submit stage=%u tensor=%u phase=program-pre-src-cpu-delay tok=%u nops=%u",
                         tok->stage_idx, tok->tensor_id, tok->id, pre_src_nops);
      }
      dma_busy_wait_nops(pre_src_nops);
    }
  }
  hw_dma_set_src(progress_src_addr, progress_bytes);
  dma_tracerv_dma_window_marker_if_scope(tok->stage_idx, tok->tensor_id,
                                         PRT_DMA_TRACERV_MARKER_PROGRAM_POST_SRC);
  if (checkpoint_submit) {
    PRT_CHECKPOINT_LOG("dma stage=%u checkpoint=program-post-src tensor=%u src=0x%llx bytes=%llu",
                       tok->stage_idx, tok->tensor_id,
                       (unsigned long long)progress_src_addr,
                       (unsigned long long)progress_bytes);
  }
  if (trace_submit) {
    PRT_MARKER_LOG("dma-submit stage=%u tensor=%u phase=program-post-src src=0x%llx bytes=%llu",
                   tok->stage_idx, tok->tensor_id,
                   (unsigned long long)progress_src_addr,
                   (unsigned long long)progress_bytes);
  }
  if (fixed_submit_probe) {
    PRT_PROGRESS_LOG("dma-fixed-load-submit stage=%u tensor=%u phase=program-post-src tok=%u src=0x%llx bytes=%llu",
                     tok->stage_idx, tok->tensor_id, tok->id,
                     (unsigned long long)progress_src_addr,
                     (unsigned long long)progress_bytes);
  }
  prt_breadcrumb_note(PRT_BREADCRUMB_KIND_DMA,
                      PRT_BREADCRUMB_PHASE_DMA_PROGRAM_POST_SRC,
                      tok->tensor_id,
                      tok->id,
                      tok->rr_manager_id ? tok->rr_manager_id : req->dst_acc,
                      PRT_BREADCRUMB_ANY_U32,
                      PRT_OK,
                      dma_breadcrumb_flags_from_token(tok, 0U),
                      progress_src_addr,
                      progress_dst_addr,
                      tok->debug_done_flag_pa,
                      progress_bytes,
                      __LINE__);
  if (export_submit_probe) {
    PRT_PROGRESS_LOG("dma-export-submit stage=%u tensor=%u phase=program-post-src tok=%u src=0x%llx bytes=%llu",
                     tok->stage_idx, tok->tensor_id, tok->id,
                     (unsigned long long)progress_src_addr,
                     (unsigned long long)progress_bytes);
  }
  if (sparse_submit_probe) {
    PRT_PROGRESS_LOG("dma-submit-inner stage=%u tensor=%u phase=after-program-src tok=%u src=0x%llx bytes=%llu",
                     tok->stage_idx, tok->tensor_id, tok->id,
                     (unsigned long long)progress_src_addr,
                     (unsigned long long)progress_bytes);
  }
  PRT_PROGRESS_RAW_LINE("[prt-raw] dma post-src");
#else
  (void)req;
  // Host-mode smoke path: do not dereference device-like addresses.
  // Functional data movement is validated on target runtime.
#endif

  tok->done = 0;
  tok->status = PRT_OK;
  return PRT_OK;
}

static void dma_gdb_marker_wait_return(const prt_dma_token_t *tok, int rc) {
  if (!tok) return;
  prt_gdb_marker_note(PRT_GDB_MARKER_SITE_DMA_WAIT_RETURN,
                      PRT_DEBUG_U32_NONE, PRT_DEBUG_U32_NONE, tok->stage_idx,
                      PRT_DEBUG_U32_NONE, tok->rr_manager_id, tok->tensor_id,
                      PRT_DEBUG_U32_NONE, tok->id, rc,
                      tok->debug_src_addr, tok->debug_dst_addr, __LINE__);
}

static int dma_blocking_wait(prt_runtime_t *rt, prt_dma_token_t *tok, uint64_t timeout_ns) {
  int progress_log;
  uint32_t progress_token_id;
  uint32_t progress_stage_idx;
  uint32_t progress_tensor_id;
  int checkpoint_wait = 0;
  int used_doneflag_poll = 0;

  if (!tok) return PRT_ERR_INVAL;
  progress_log = dma_should_progress_log(tok);
  progress_token_id = tok->id;
  progress_stage_idx = tok->stage_idx;
  progress_tensor_id = tok->tensor_id;
  checkpoint_wait = dma_should_checkpoint_submit_wait_tok(tok);
  prt_gdb_marker_note(PRT_GDB_MARKER_SITE_DMA_WAIT_ENTER,
                      PRT_DEBUG_U32_NONE, PRT_DEBUG_U32_NONE, tok->stage_idx,
                      PRT_DEBUG_U32_NONE, tok->rr_manager_id, tok->tensor_id,
                      PRT_DEBUG_U32_NONE, tok->id, PRT_OK,
                      tok->debug_src_addr, tok->debug_dst_addr, __LINE__);
#if !defined(__riscv)
  (void)progress_log;
  (void)progress_token_id;
  (void)progress_stage_idx;
  (void)progress_tensor_id;
  (void)timeout_ns;
  (void)checkpoint_wait;
#endif

#if defined(__riscv)
  uint64_t fence_status = 0;
  const int sparse_wait_probe = dma_should_sparse_submit_probe_tok(tok);
  const int fixed_wait_probe = dma_should_sparse_fixed_load_wait_probe_tok(tok);
  const int export_wait_probe = dma_should_sparse_export_submit_probe_tok(tok);
  const prt_trigger_log_family_t wait_family =
    (tok && tok->tensor_id == 2U) ? PRT_TRIGGER_LOG_FAMILY_DMA_EXPORT
                                  : PRT_TRIGGER_LOG_FAMILY_DMA_FIXED_LOAD;
  if (checkpoint_wait) {
    PRT_CHECKPOINT_LOG("dma stage=%u checkpoint=wait-enter tensor=%u token=%u timeout_ns=%llu done_pa=0x%llx",
                       tok->stage_idx, tok->tensor_id, tok->id,
                       (unsigned long long)timeout_ns,
                       (unsigned long long)tok->debug_done_flag_pa);
  }
  dma_completion_flag_refresh(tok);
  if (sparse_wait_probe) {
    PRT_PROGRESS_LOG("dma-wait-inner phase=before-fence token=%u stage=%u tensor=%u hw_done=%d done_va=0x%llx done_pa=0x%llx src=0x%llx dst=0x%llx bytes=%llu",
                     tok->id,
                     tok->stage_idx,
                     tok->tensor_id,
                     tok->hw_done_flag,
                     (unsigned long long)tok->debug_done_flag_va,
                     (unsigned long long)tok->debug_done_flag_pa,
                     (unsigned long long)tok->debug_src_addr,
                     (unsigned long long)tok->debug_dst_addr,
                     (unsigned long long)tok->debug_bytes);
  }
  if (fixed_wait_probe) {
    PRT_PROGRESS_LOG("dma-fixed-load-wait phase=before-fence token=%u stage=%u tensor=%u hw_done=%d done_pa=0x%llx src=0x%llx dst=0x%llx bytes=%llu",
                     tok->id,
                     tok->stage_idx,
                     tok->tensor_id,
                     tok->hw_done_flag,
                     (unsigned long long)tok->debug_done_flag_pa,
                     (unsigned long long)tok->debug_src_addr,
                     (unsigned long long)tok->debug_dst_addr,
                     (unsigned long long)tok->debug_bytes);
  }
  prt_breadcrumb_note(PRT_BREADCRUMB_KIND_DMA,
                      PRT_BREADCRUMB_PHASE_DMA_WAIT_BEFORE_FENCE,
                      tok->tensor_id,
                      tok->id,
                      tok->rr_manager_id,
                      PRT_BREADCRUMB_ANY_U32,
                      PRT_OK,
                      dma_breadcrumb_flags_from_token(tok, 0U),
                      tok->debug_src_addr,
                      tok->debug_dst_addr,
                      tok->debug_done_flag_pa,
                      timeout_ns,
                      __LINE__);
  if (timeout_ns != 0ULL && dma_blocking_wait_poll_timeout_enabled()) {
    const int poll_rc = dma_blocking_wait_poll_doneflag(tok, timeout_ns,
                                                        progress_log || sparse_wait_probe ||
                                                        fixed_wait_probe || export_wait_probe);
    if (poll_rc == PRT_ERR_TIMEOUT) {
      dma_trigger_wait(tok, wait_family, "poll-to", poll_rc);
      dma_gdb_marker_wait_return(tok, poll_rc);
      return poll_rc;
    }
    if (poll_rc == PRT_OK) {
      used_doneflag_poll = 1;
    } else {
      PRT_PROGRESS_LOG("dma-wait-doneflag-poll phase=disabled token=%u stage=%u tensor=%u rc=%d",
                       tok->id,
                       tok->stage_idx,
                       tok->tensor_id,
                       poll_rc);
    }
  }
  if (progress_log) {
    PRT_PROGRESS_HOT_LOG("dma-wait idle-enter mode=%s token=%u stage=%u tensor=%u hw_done=%d src_mod64=0x%02llx dst_mod64=0x%02llx done_mod64=0x%02llx full_byte_mode_hint=%u",
                         used_doneflag_poll ? "doneflag-poll" : "hw-fence",
                         progress_token_id,
                         progress_stage_idx,
                         progress_tensor_id,
                         tok->hw_done_flag,
                         (unsigned long long)dma_debug_mod64(tok->debug_src_addr),
                         (unsigned long long)dma_debug_mod64(tok->debug_dst_addr),
                         (unsigned long long)dma_debug_mod64(tok->debug_done_flag_pa),
                         dma_debug_full_byte_mode_hint(tok->debug_src_addr, tok->debug_dst_addr, tok->debug_bytes));
  }
  if (!used_doneflag_poll) {
    dma_tracerv_dma_window_marker_if_scope(tok->stage_idx, tok->tensor_id,
                                           PRT_DMA_TRACERV_MARKER_WAIT_BEFORE_FENCE);
    fence_status = hw_dma_fence();
    dma_tracerv_dma_window_marker_if_scope(tok->stage_idx, tok->tensor_id,
                                           PRT_DMA_TRACERV_MARKER_WAIT_AFTER_FENCE);
  }
  dma_completion_flag_refresh(tok);
  if (checkpoint_wait) {
    PRT_CHECKPOINT_LOG("dma stage=%u checkpoint=wait-fence-done tensor=%u token=%u status=%llu hw_done=%d",
                       tok->stage_idx, tok->tensor_id, tok->id,
                       (unsigned long long)fence_status,
                       tok->hw_done_flag);
  }
  if (sparse_wait_probe) {
    PRT_PROGRESS_LOG("dma-wait-inner phase=%s token=%u stage=%u tensor=%u status=%llu hw_done=%d",
                     used_doneflag_poll ? "after-doneflag-poll" : "after-fence",
                     tok->id,
                     tok->stage_idx,
                     tok->tensor_id,
                     (unsigned long long)fence_status,
                     tok->hw_done_flag);
  }
  if (fixed_wait_probe) {
    PRT_PROGRESS_LOG("dma-fixed-load-wait phase=%s token=%u stage=%u tensor=%u status=%llu hw_done=%d done_pa=0x%llx",
                     used_doneflag_poll ? "after-doneflag-poll" : "after-fence",
                     tok->id,
                     tok->stage_idx,
                     tok->tensor_id,
                     (unsigned long long)fence_status,
                     tok->hw_done_flag,
                     (unsigned long long)tok->debug_done_flag_pa);
  }
  if (export_wait_probe) {
    PRT_PROGRESS_LOG("dma-export-wait phase=%s token=%u stage=%u tensor=%u status=%llu hw_done=%d done_pa=0x%llx",
                     used_doneflag_poll ? "after-doneflag-poll" : "after-fence",
                     tok->id,
                     tok->stage_idx,
                     tok->tensor_id,
                     (unsigned long long)fence_status,
                     tok->hw_done_flag,
                     (unsigned long long)tok->debug_done_flag_pa);
  }
  dma_trigger_wait(tok, wait_family, used_doneflag_poll ? "poll-e" : "wf-e", PRT_OK);
  prt_breadcrumb_note(PRT_BREADCRUMB_KIND_DMA,
                      used_doneflag_poll ? PRT_BREADCRUMB_PHASE_DMA_WAIT_DONEFLAG_POLL_DONE
                                         : PRT_BREADCRUMB_PHASE_DMA_WAIT_AFTER_FENCE,
                      tok->tensor_id,
                      tok->id,
                      tok->rr_manager_id,
                      PRT_BREADCRUMB_ANY_U32,
                      PRT_OK,
                      dma_breadcrumb_flags_from_token(tok, 0U),
                      tok->debug_src_addr,
                      tok->debug_dst_addr,
                      tok->debug_done_flag_pa,
                      fence_status,
                      __LINE__);
  if (progress_log) {
    PRT_PROGRESS_HOT_LOG("dma-wait fence-done token=%u stage=%u tensor=%u status=%llu hw_done=%d",
                         progress_token_id,
                         progress_stage_idx,
                         progress_tensor_id,
                         (unsigned long long)fence_status,
                         tok->hw_done_flag);
  }
  if (tok->rr_scope_external) {
    prt_breadcrumb_note(PRT_BREADCRUMB_KIND_DMA,
                        PRT_BREADCRUMB_PHASE_DMA_WAIT_BEFORE_SHARED_FENCE,
                        tok->tensor_id,
                        tok->id,
                        tok->rr_manager_id,
                        PRT_BREADCRUMB_ANY_U32,
                        PRT_OK,
                        dma_breadcrumb_flags_from_token(tok, 0U),
                        tok->debug_src_addr,
                        tok->debug_dst_addr,
                        tok->debug_done_flag_pa,
                        0ULL,
                        __LINE__);
    dma_tracerv_dma_window_marker_if_scope(tok->stage_idx, tok->tensor_id,
                                           PRT_DMA_TRACERV_MARKER_WAIT_BEFORE_SHARED_FENCE);
    (void)dma_token_fence_scope(tok);
    dma_tracerv_dma_window_marker_if_scope(tok->stage_idx, tok->tensor_id,
                                           PRT_DMA_TRACERV_MARKER_WAIT_AFTER_SHARED_FENCE);
    if (sparse_wait_probe) {
      PRT_PROGRESS_LOG("dma-wait-inner phase=after-shared-fence token=%u stage=%u tensor=%u valid=%u",
                       tok->id,
                       tok->stage_idx,
                       tok->tensor_id,
                       (uint32_t)tok->rr_scope_valid);
    }
    if (fixed_wait_probe) {
      PRT_PROGRESS_LOG("dma-fixed-load-wait phase=after-shared-fence token=%u stage=%u tensor=%u valid=%u",
                       tok->id,
                       tok->stage_idx,
                       tok->tensor_id,
                       (uint32_t)tok->rr_scope_valid);
    }
    if (export_wait_probe) {
      PRT_PROGRESS_LOG("dma-export-wait phase=after-shared-fence token=%u stage=%u tensor=%u valid=%u",
                       tok->id,
                       tok->stage_idx,
                       tok->tensor_id,
                       (uint32_t)tok->rr_scope_valid);
    }
    dma_trigger_wait(tok, wait_family, "sf-e", PRT_OK);
    prt_breadcrumb_note(PRT_BREADCRUMB_KIND_DMA,
                        PRT_BREADCRUMB_PHASE_DMA_WAIT_AFTER_SHARED_FENCE,
                        tok->tensor_id,
                        tok->id,
                        tok->rr_manager_id,
                        PRT_BREADCRUMB_ANY_U32,
                        PRT_OK,
                        dma_breadcrumb_flags_from_token(tok, 0U),
                        tok->debug_src_addr,
                        tok->debug_dst_addr,
                        tok->debug_done_flag_pa,
                        0ULL,
                        __LINE__);
  } else {
    dma_token_release_scope(tok, 1);
  }
  if (sparse_wait_probe) {
    PRT_PROGRESS_LOG("dma-wait-inner phase=after-release token=%u stage=%u tensor=%u valid=%u",
                     tok->id,
                     tok->stage_idx,
                     tok->tensor_id,
                     (uint32_t)tok->rr_scope_valid);
  }
  if (fixed_wait_probe) {
    PRT_PROGRESS_LOG("dma-fixed-load-wait phase=after-release token=%u stage=%u tensor=%u valid=%u",
                     tok->id,
                     tok->stage_idx,
                     tok->tensor_id,
                     (uint32_t)tok->rr_scope_valid);
  }
  if (export_wait_probe) {
    PRT_PROGRESS_LOG("dma-export-wait phase=after-release token=%u stage=%u tensor=%u valid=%u",
                     tok->id,
                     tok->stage_idx,
                     tok->tensor_id,
                     (uint32_t)tok->rr_scope_valid);
  }
  dma_trigger_wait(tok, wait_family, "rel-e", PRT_OK);
  prt_breadcrumb_note(PRT_BREADCRUMB_KIND_DMA,
                      PRT_BREADCRUMB_PHASE_DMA_WAIT_AFTER_RELEASE,
                      tok->tensor_id,
                      tok->id,
                      tok->rr_manager_id,
                      PRT_BREADCRUMB_ANY_U32,
                      PRT_OK,
                      dma_breadcrumb_flags_from_token(tok, 0U),
                      tok->debug_src_addr,
                      tok->debug_dst_addr,
                      tok->debug_done_flag_pa,
                      0ULL,
                      __LINE__);
#endif

  dma_token_complete(tok, PRT_OK);
  prt_breadcrumb_note(PRT_BREADCRUMB_KIND_DMA,
                      PRT_BREADCRUMB_PHASE_DMA_WAIT_AFTER_COMPLETE,
                      tok->tensor_id,
                      tok->id,
                      tok->rr_manager_id,
                      PRT_BREADCRUMB_ANY_U32,
                      PRT_OK,
                      dma_breadcrumb_flags_from_token(tok, 0U),
                      tok->debug_src_addr,
                      tok->debug_dst_addr,
                      tok->debug_done_flag_pa,
                      0ULL,
                      __LINE__);
  dma_trace_complete_once(rt, tok);
  prt_breadcrumb_note(PRT_BREADCRUMB_KIND_DMA,
                      PRT_BREADCRUMB_PHASE_DMA_WAIT_AFTER_TRACE_COMPLETE,
                      tok->tensor_id,
                      tok->id,
                      tok->rr_manager_id,
                      PRT_BREADCRUMB_ANY_U32,
                      PRT_OK,
                      dma_breadcrumb_flags_from_token(tok, 0U),
                      tok->debug_src_addr,
                      tok->debug_dst_addr,
                      tok->debug_done_flag_pa,
                      0ULL,
                      __LINE__);
  dma_gdb_marker_wait_return(tok, PRT_OK);
  return PRT_OK;
}

static int dma_blocking_submit_and_wait(prt_runtime_t *rt, const prt_dma_req_t *req, uint64_t timeout_ns) {
  prt_dma_token_t tok;
  int rc;
  memset(&tok, 0, sizeof(tok));
  rc = dma_blocking_submit(rt, req, &tok);
  if (rc != PRT_OK) return rc;
  rc = dma_blocking_wait(rt, &tok, timeout_ns);
  (void)prt_dma_token_cleanup(&tok);
  return rc;
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

  if (dma_should_progress_log(tok)) {
    PRT_PROGRESS_HOT_LOG("dma-wait begin token=%u stage=%u tensor=%u timeout_ns=%llu done_va=0x%llx done_pa=0x%llx done_pa_rc=%d(%s)",
                         tok->id,
                         tok->stage_idx,
                         tok->tensor_id,
                         (unsigned long long)timeout_ns,
                         (unsigned long long)tok->debug_done_flag_va,
                         (unsigned long long)tok->debug_done_flag_pa,
                         tok->hw_done_flag_pa_rc,
                         prt_err_str(tok->hw_done_flag_pa_rc));
  }

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
    PRT_PROGRESS_LOG("dma-wait timeout token=%u stage=%u tensor=%u timeout_ns=%llu",
                     tok->id, tok->stage_idx, tok->tensor_id,
                     (unsigned long long)timeout_ns);
    dma_token_release_scope(tok, 1);
    dma_trace_complete_once(rt, tok);
    return rc;
  }

  if (dma_should_progress_log(tok)) {
    PRT_PROGRESS_HOT_LOG("dma-wait done token=%u stage=%u tensor=%u status=%d",
                         tok->id, tok->stage_idx, tok->tensor_id, rc);
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
          if (dma_completion_flag_refresh(tok)) {
            if (dma_should_progress_log(tok)) {
              dma_log_pending_state("progress-complete", tok, prt_now_ns() - tok->submit_ns);
            }
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
          tok->debug_progress_polls += 1U;
          if (dma_should_progress_log(tok)) {
            uint64_t now_ns = prt_now_ns();
            if (tok->debug_last_pending_log_ns == 0 ||
                now_ns - tok->debug_last_pending_log_ns >= 1000000000ULL) {
              tok->debug_last_pending_log_ns = now_ns;
              dma_log_pending_state("progress-pending", tok, now_ns - tok->submit_ns);
            }
          }
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
