#include "prt_dma.h"

#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#if defined(__linux__) && defined(__riscv)
#include <sys/syscall.h>
#include <unistd.h>
#endif

#include "prt_progress.h"
#include "prt_runtime.h"
#include "prt_rerocc.h"

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

static int dma_blocking_submit(prt_runtime_t *rt, const prt_dma_req_t *req, prt_dma_token_t *tok);
static int dma_blocking_wait(prt_runtime_t *rt, prt_dma_token_t *tok, uint64_t timeout_ns);
static int dma_blocking_submit_and_wait(prt_runtime_t *rt, const prt_dma_req_t *req, uint64_t timeout_ns);

static int dma_poll_submit(prt_runtime_t *rt, const prt_dma_req_t *req, prt_dma_token_t *tok);
static int dma_poll_wait(prt_runtime_t *rt, prt_dma_token_t *tok, uint64_t timeout_ns);
static int dma_poll_submit_and_wait(prt_runtime_t *rt, const prt_dma_req_t *req, uint64_t timeout_ns);
static void *dma_progress_thread_main(void *arg);
static int dma_should_marker_trace_submit(const prt_dma_req_t *req, uint32_t tensor_id);
static int dma_should_log_export_chunk_marker(uint32_t page_idx, uint32_t page_count,
                                              uint64_t copied_bytes);
static int dma_submit_wait_annotated(prt_runtime_t *rt, const prt_dma_req_t *req,
                                     uint32_t stage_idx, uint32_t tensor_id,
                                     uint64_t timeout_ns);
static uint64_t page_base_addr(const prt_runtime_t *rt, const prt_page_t *page);
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
                                            uint64_t timeout_ns);
static int dma_copy_spm_pages_to_host_linux(prt_runtime_t *rt, uint8_t *dst_host,
                                            const prt_page_list_t *src_pages, uint32_t manager_id,
                                            uint32_t stage_idx, uint32_t tensor_id,
                                            uint64_t timeout_ns);
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
static int dma_debug_virt_to_phys(const void *vaddr, uint64_t *paddr) {
  return prt_host_virt_to_phys(vaddr, paddr);
}

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

#if defined(__linux__) && defined(__riscv)
static uint64_t dma_min_u64(uint64_t a, uint64_t b) {
  return a < b ? a : b;
}

static int dma_copy_host_to_spm_pages_linux(prt_runtime_t *rt, const prt_page_list_t *dst_pages,
                                            const uint8_t *src_host, uint32_t manager_id,
                                            uint32_t stage_idx, uint32_t tensor_id,
                                            uint64_t timeout_ns) {
  prt_dma_req_t req;
  const size_t host_page_bytes = prt_host_page_size_bytes();
  const uint64_t page_bytes = rt->cfg.page_size_bytes ? rt->cfg.page_size_bytes : PRT_PAGE_SIZE_BYTES;

  if (!rt || !dst_pages || !dst_pages->data || dst_pages->size == 0 || !src_host) return PRT_ERR_INVAL;
  if (host_page_bytes == 0) return PRT_ERR_STATE;

  req.src_acc = manager_id;
  req.dst_acc = manager_id;

  for (uint32_t i = 0; i < dst_pages->size; ++i) {
    const uint64_t dst_page_base = page_base_addr(rt, &dst_pages->data[i]);
    uint64_t copied = 0;

    while (copied < page_bytes) {
      const uint64_t src_off = (uint64_t)i * page_bytes + copied;
      const uint8_t *src_ptr = src_host + (size_t)src_off;
      const size_t host_page_off = (size_t)((uintptr_t)src_ptr % (uintptr_t)host_page_bytes);
      const uint64_t host_page_room = (uint64_t)(host_page_bytes - host_page_off);
      const uint64_t dst_addr = dst_page_base + copied;
      uint64_t chunk = dma_min_u64(page_bytes - copied, host_page_room);
      uint64_t src_pa = 0;
      int used_bounce = 0;
      int rc;
      if (dma_chunk_needs_bounce((uint64_t)(uintptr_t)src_ptr, dst_addr, chunk)) {
        uint8_t *bounce_ptr = NULL;
        uint64_t bounce_room = 0;
        used_bounce = 1;
        rc = dma_stage_bounce_region(rt, stage_idx, dma_debug_mod64(dst_addr), &bounce_ptr, &bounce_room);
        if (rc != PRT_OK) return rc;
        chunk = dma_min_u64(chunk, bounce_room);
        memcpy(bounce_ptr, src_ptr, (size_t)chunk);
        rc = prt_host_virt_to_phys((const void *)bounce_ptr, &src_pa);
      } else {
        rc = prt_host_virt_to_phys((const void *)src_ptr, &src_pa);
      }
      if (rc != PRT_OK) return rc;

      req.src_addr = src_pa;
      req.dst_addr = dst_addr;
      req.bytes = chunk;
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
      rc = dma_submit_wait_annotated(rt, &req, stage_idx, tensor_id, timeout_ns);
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
      if (rc != PRT_OK) return rc;
      copied += chunk;
    }
  }

  return PRT_OK;
}

static int dma_copy_spm_pages_to_host_linux(prt_runtime_t *rt, uint8_t *dst_host,
                                            const prt_page_list_t *src_pages, uint32_t manager_id,
                                            uint32_t stage_idx, uint32_t tensor_id,
                                            uint64_t timeout_ns) {
  prt_dma_req_t req;
  const size_t host_page_bytes = prt_host_page_size_bytes();
  const uint64_t page_bytes = rt->cfg.page_size_bytes ? rt->cfg.page_size_bytes : PRT_PAGE_SIZE_BYTES;

  if (!rt || !src_pages || !src_pages->data || src_pages->size == 0 || !dst_host) return PRT_ERR_INVAL;
  if (host_page_bytes == 0) return PRT_ERR_STATE;

  req.src_acc = manager_id;
  req.dst_acc = manager_id;

  for (uint32_t i = 0; i < src_pages->size; ++i) {
    const uint64_t src_page_base = page_base_addr(rt, &src_pages->data[i]);
    uint64_t copied = 0;

    while (copied < page_bytes) {
      const uint64_t dst_off = (uint64_t)i * page_bytes + copied;
      uint8_t *dst_ptr = dst_host + (size_t)dst_off;
      const size_t host_page_off = (size_t)((uintptr_t)dst_ptr % (uintptr_t)host_page_bytes);
      const uint64_t host_page_room = (uint64_t)(host_page_bytes - host_page_off);
      const uint64_t src_addr = src_page_base + copied;
      uint64_t chunk = dma_min_u64(page_bytes - copied, host_page_room);
      uint64_t dst_pa = 0;
      int rc;
      int used_bounce = 0;
      const int log_chunk_marker =
        dma_should_log_export_chunk_marker(i, src_pages->size, copied);
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
        if (rc != PRT_OK) return rc;
        chunk = dma_min_u64(chunk, bounce_room);
        rc = prt_host_virt_to_phys((const void *)bounce_ptr, &dst_pa);
        if (rc != PRT_OK) return rc;
        req.src_addr = src_addr;
        req.dst_addr = dst_pa;
        req.bytes = chunk;
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
        rc = dma_submit_wait_annotated(rt, &req, stage_idx, tensor_id, timeout_ns);
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
          return rc;
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
        continue;
      }
      rc = prt_host_virt_to_phys((const void *)dst_ptr, &dst_pa);
      if (rc != PRT_OK) return rc;

      req.src_addr = src_addr;
      req.dst_addr = dst_pa;
      req.bytes = chunk;
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
      rc = dma_submit_wait_annotated(rt, &req, stage_idx, tensor_id, timeout_ns);
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
        return rc;
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
    }
  }

  return PRT_OK;
}
#endif

static int dma_should_progress_log(const prt_dma_token_t *tok) {
  if (!tok) return 0;
  return tok->id > 0 && tok->id <= 16U;
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
#endif

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
      memset(buf, 0, host_page_bytes);
      exec->stage_dma_bounce[stage_idx] = (uint8_t *)buf;
      exec->stage_dma_bounce_bytes[stage_idx] = host_page_bytes;
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
static void dma_debug_capture_done_flag(prt_dma_token_t *tok) {
  uint64_t pa = 0;
  int rc;
  if (!tok) return;
  tok->debug_done_flag_va = (uint64_t)(uintptr_t)&tok->hw_done_flag;
  rc = dma_debug_virt_to_phys((const void *)&tok->hw_done_flag, &pa);
  tok->hw_done_flag_pa_rc = rc;
  tok->debug_done_flag_pa = rc == PRT_OK ? pa : 0;
}
#endif

static void dma_log_pending_state(const char *tag, const prt_dma_token_t *tok, uint64_t elapsed_ns) {
#if !PRT_ENABLE_PROGRESS_LOG
  (void)tag;
  (void)elapsed_ns;
#endif
  if (!dma_should_progress_log(tok)) return;
  PRT_PROGRESS_LOG(
    "dma-%s token=%u stage=%u tensor=%u polls=%u elapsed_ms=%llu hw_done=%d done=%d status=%d src=0x%llx dst=0x%llx bytes=%llu src_acc=%u dst_acc=%u done_va=0x%llx done_pa=0x%llx done_pa_rc=%d(%s)",
    tag,
    tok->id,
    tok->stage_idx,
    tok->tensor_id,
    tok->debug_progress_polls,
    (unsigned long long)(elapsed_ns / 1000000ULL),
    tok->hw_done_flag,
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
}

static int dma_should_marker_trace_submit(const prt_dma_req_t *req, uint32_t tensor_id) {
  if (!req) return 0;
  if (!dma_submit_marker_trace_enabled()) return 0;
  if (tensor_id >= 1000000U) return 0;
  return req->bytes > 0ULL && req->bytes <= 4096ULL;
}

static int dma_should_log_export_chunk_marker(uint32_t page_idx, uint32_t page_count,
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
  asm volatile("fence rw, rw" ::: "memory");
}

static inline uint64_t hw_dma_read_monitor(uint64_t stat_id) {
  uint64_t value = 0;
  ROCC_INSTRUCTION_R_R_R(XCUSTOM_DMA, value, stat_id, 0, 4);
  return value;
}

static inline uint64_t hw_dma_fence(void) {
  uint64_t status = 0;
  asm volatile("fence" ::: "memory");
  ROCC_INSTRUCTION_R_R_R(XCUSTOM_DMA, status, 0, 0, 3);
  asm volatile("fence" ::: "memory");
  return status;
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
  const uint64_t cfg15 = dma_debug_rr_read_cfg(15U);
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
                 tok ? tok->hw_done_flag : 0);
  PRT_MARKER_LOG("%s opc token=%u opc0=0x%llx opc1=0x%llx opc2=0x%llx opc3=0x%llx",
                 snapshot_tag,
                 tok ? tok->id : 0U,
                 (unsigned long long)opc0,
                 (unsigned long long)opc1,
                 (unsigned long long)opc2,
                 (unsigned long long)opc3);
  PRT_MARKER_LOG("%s cfg token=%u cfg_scope=0x%llx cfg0=0x%llx cfg1=0x%llx cfg2=0x%llx cfg3=0x%llx cfg15=0x%llx",
                 snapshot_tag,
                 tok ? tok->id : 0U,
                 (unsigned long long)cfg_scope,
                 (unsigned long long)cfg0,
                 (unsigned long long)cfg1,
                 (unsigned long long)cfg2,
                 (unsigned long long)cfg3,
                 (unsigned long long)cfg15);
}

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
#elif defined(__linux__)
  return dma_copy_host_to_spm_pages_linux(rt, dst_pages,
                                          (const uint8_t *)(uintptr_t)src_dram_addr,
                                          manager_id, stage_idx, tensor_id, timeout_ns);
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
#elif defined(__linux__)
  return dma_copy_spm_pages_to_host_linux(rt, (uint8_t *)(uintptr_t)dst_dram_addr,
                                          src_pages, manager_id, stage_idx, tensor_id, timeout_ns);
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
  uint64_t progress_src_addr;
  uint64_t progress_dst_addr;
  uint64_t progress_bytes;
  int trace_submit = 0;

  if (!req || !tok) return PRT_ERR_INVAL;

  dma_token_init(tok);
  tok->id = __sync_add_and_fetch(&rt->next_dma_token_id, 1);
  tok->submit_ns = prt_now_ns();
  progress_src_addr = req->src_addr;
  progress_dst_addr = req->dst_addr;
  progress_bytes = req->bytes;
#if !PRT_ENABLE_PROGRESS_LOG || !defined(__riscv)
  (void)progress_src_addr;
  (void)progress_dst_addr;
  (void)progress_bytes;
#endif
  trace_submit = dma_should_marker_trace_submit(req, tok->tensor_id);
  prt_trace_on_dma_submit(rt);
  prt_trace_log_event(rt, tok->stage_idx, PRT_TRACE_EVT_DMA_SUBMIT,
                      tok->tensor_id,
                      req->bytes > UINT32_MAX ? UINT32_MAX : (uint32_t)req->bytes);
  dma_debug_capture_req(tok, req);

#if defined(__riscv)
  {
    prt_rr_scope_t scope;
    if (trace_submit) {
      PRT_MARKER_LOG("dma-submit stage=%u tensor=%u phase=rr-acquire-begin mgr=%u",
                     tok->stage_idx, tok->tensor_id, req->dst_acc);
    }
    PRT_PROGRESS_RAW_LINE("[prt-raw] dma pre-acquire");
    int rrc = prt_rr_acquire_scope(rt, tok->stage_idx, req->dst_acc, 2U, &scope);
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
    PRT_PROGRESS_RAW_LINE("[prt-raw] dma acquire-ok");
    tok->rr_scope_valid = scope.valid;
    tok->rr_cfg_id = scope.cfg_id;
    tok->rr_manager_id = scope.manager_id;
    tok->rr_opcode_id = scope.opcode_id;
  }
  tok->hw_done_flag = 0;
  if (trace_submit) {
    PRT_MARKER_LOG("dma-submit stage=%u tensor=%u phase=doneflag-begin",
                   tok->stage_idx, tok->tensor_id);
  }
  dma_debug_capture_done_flag(tok);
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
  PRT_PROGRESS_RAW_LINE("[prt-raw] dma pre-program");
  if (trace_submit) {
    PRT_MARKER_LOG("dma-submit stage=%u tensor=%u phase=program-begin src=0x%llx dst=0x%llx bytes=%llu done_pa=0x%llx",
                   tok->stage_idx, tok->tensor_id,
                   (unsigned long long)progress_src_addr,
                   (unsigned long long)progress_dst_addr,
                   (unsigned long long)progress_bytes,
                   (unsigned long long)tok->debug_done_flag_pa);
  }
  hw_dma_submit_fence();
  if (trace_submit) {
    PRT_MARKER_LOG("dma-submit stage=%u tensor=%u phase=program-post-fence done_pa=0x%llx",
                   tok->stage_idx, tok->tensor_id,
                   (unsigned long long)tok->debug_done_flag_pa);
  }
  hw_dma_set_dst(progress_dst_addr, tok->debug_done_flag_pa);
  if (trace_submit) {
    PRT_MARKER_LOG("dma-submit stage=%u tensor=%u phase=program-post-dst dst=0x%llx done_pa=0x%llx",
                   tok->stage_idx, tok->tensor_id,
                   (unsigned long long)progress_dst_addr,
                   (unsigned long long)tok->debug_done_flag_pa);
  }
  hw_dma_set_src(progress_src_addr, progress_bytes);
  if (trace_submit) {
    PRT_MARKER_LOG("dma-submit stage=%u tensor=%u phase=program-post-src src=0x%llx bytes=%llu",
                   tok->stage_idx, tok->tensor_id,
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

static int dma_blocking_wait(prt_runtime_t *rt, prt_dma_token_t *tok, uint64_t timeout_ns) {
  int progress_log;
  uint32_t progress_token_id;
  uint32_t progress_stage_idx;
  uint32_t progress_tensor_id;
#if defined(__riscv)
  int pending_snapshot_logged = 0;
#endif

  if (!tok) return PRT_ERR_INVAL;
  progress_log = dma_should_progress_log(tok);
  progress_token_id = tok->id;
  progress_stage_idx = tok->stage_idx;
  progress_tensor_id = tok->tensor_id;
#if !defined(__riscv)
  (void)progress_log;
  (void)progress_token_id;
  (void)progress_stage_idx;
  (void)progress_tensor_id;
  (void)timeout_ns;
#endif

#if defined(__riscv)
  if (timeout_ns > 0) {
    const uint64_t start_ns = prt_now_ns();
    uint64_t last_log_ns = start_ns;

    if (progress_log) {
      PRT_PROGRESS_HOT_LOG("dma-wait doneflag-enter token=%u stage=%u tensor=%u timeout_ns=%llu hw_done=%d src_mod64=0x%02llx dst_mod64=0x%02llx done_mod64=0x%02llx",
                           progress_token_id,
                           progress_stage_idx,
                           progress_tensor_id,
                           (unsigned long long)timeout_ns,
                           tok->hw_done_flag,
                           (unsigned long long)dma_debug_mod64(tok->debug_src_addr),
                           (unsigned long long)dma_debug_mod64(tok->debug_dst_addr),
                           (unsigned long long)dma_debug_mod64(tok->debug_done_flag_pa));
    }

    while (1) {
      uint64_t now_ns;
      asm volatile("fence r, rw" ::: "memory");
      if (tok->hw_done_flag) {
        tok->hw_done_flag = 0;
        asm volatile("fence" ::: "memory");
        if (progress_log) {
          PRT_PROGRESS_HOT_LOG("dma-wait doneflag-done token=%u stage=%u tensor=%u elapsed_ms=%llu",
                               progress_token_id,
                               progress_stage_idx,
                               progress_tensor_id,
                               (unsigned long long)((prt_now_ns() - start_ns) / 1000000ULL));
        }
        dma_token_release_scope(tok, 1);
        dma_token_complete(tok, PRT_OK);
        dma_trace_complete_once(rt, tok);
        return PRT_OK;
      }

      now_ns = prt_now_ns();
      if (now_ns - start_ns >= timeout_ns) {
        PRT_MARKER_LOG("dma-wait timeout token=%u stage=%u tensor=%u waited_ms=%llu timeout_ms=%llu src=0x%llx dst=0x%llx bytes=%llu src_acc=%u dst_acc=%u done_va=0x%llx done_pa=0x%llx done_pa_rc=%d hw_done=%d rr_cfg=%u rr_mgr=%u rr_opc=%u",
                       tok->id,
                       tok->stage_idx,
                       tok->tensor_id,
                       (unsigned long long)((now_ns - start_ns) / 1000000ULL),
                       (unsigned long long)(timeout_ns / 1000000ULL),
                       (unsigned long long)tok->debug_src_addr,
                       (unsigned long long)tok->debug_dst_addr,
                       (unsigned long long)tok->debug_bytes,
                       tok->debug_src_acc,
                       tok->debug_dst_acc,
                       (unsigned long long)tok->debug_done_flag_va,
                       (unsigned long long)tok->debug_done_flag_pa,
                       tok->hw_done_flag_pa_rc,
                       tok->hw_done_flag,
                       tok->rr_cfg_id,
                       tok->rr_manager_id,
                       tok->rr_opcode_id);
        dma_log_rr_snapshot_marker("dma-wait timeout rr", tok);
        dma_log_monitor_snapshot_marker("dma-wait timeout", tok);
        dma_token_release_scope(tok, 0);
        dma_token_complete(tok, PRT_ERR_TIMEOUT);
        dma_trace_complete_once(rt, tok);
        return PRT_ERR_TIMEOUT;
      }

      if (now_ns - last_log_ns >= 1000000000ULL) {
        last_log_ns = now_ns;
        PRT_MARKER_LOG("dma-wait pending token=%u stage=%u tensor=%u elapsed_ms=%llu timeout_ms=%llu src=0x%llx dst=0x%llx bytes=%llu done_pa=0x%llx hw_done=%d",
                       tok->id,
                       tok->stage_idx,
                       tok->tensor_id,
                       (unsigned long long)((now_ns - start_ns) / 1000000ULL),
                       (unsigned long long)(timeout_ns / 1000000ULL),
                       (unsigned long long)tok->debug_src_addr,
                       (unsigned long long)tok->debug_dst_addr,
                       (unsigned long long)tok->debug_bytes,
                       (unsigned long long)tok->debug_done_flag_pa,
                       tok->hw_done_flag);
        if (!pending_snapshot_logged) {
          pending_snapshot_logged = 1;
          dma_log_rr_snapshot_marker("dma-wait pending rr", tok);
          dma_log_monitor_snapshot_marker("dma-wait pending", tok);
        }
      }
      asm volatile("nop");
    }
  }

  uint64_t fence_status = 0;
  if (progress_log) {
    PRT_PROGRESS_HOT_LOG("dma-wait fence-enter token=%u stage=%u tensor=%u hw_done=%d src_mod64=0x%02llx dst_mod64=0x%02llx done_mod64=0x%02llx full_byte_mode_hint=%u",
                         progress_token_id,
                         progress_stage_idx,
                         progress_tensor_id,
                         tok->hw_done_flag,
                         (unsigned long long)dma_debug_mod64(tok->debug_src_addr),
                         (unsigned long long)dma_debug_mod64(tok->debug_dst_addr),
                         (unsigned long long)dma_debug_mod64(tok->debug_done_flag_pa),
                         dma_debug_full_byte_mode_hint(tok->debug_src_addr, tok->debug_dst_addr, tok->debug_bytes));
  }
  fence_status = hw_dma_fence();
  if (progress_log) {
    PRT_PROGRESS_HOT_LOG("dma-wait fence-done token=%u stage=%u tensor=%u status=%llu hw_done=%d",
                         progress_token_id,
                         progress_stage_idx,
                         progress_tensor_id,
                         (unsigned long long)fence_status,
                         tok->hw_done_flag);
  }
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
          if (tok->hw_done_flag) {
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
