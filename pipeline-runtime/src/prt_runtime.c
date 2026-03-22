#if defined(__linux__) && !defined(_GNU_SOURCE)
#define _GNU_SOURCE
#endif

#include "prt_runtime.h"
#include "prt_rerocc.h"
#include "prt_gemmini_artifacts.h"
#include "prt_progress.h"

#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <limits.h>
#if defined(__linux__)
#include <sched.h>
#endif

#define PRT_TRACE_EVENT_CAP_DEFAULT 262144U

uint64_t prt_now_ns(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

uint64_t prt_now_cycle(void) {
#if defined(__riscv)
  uint64_t c = 0;
  asm volatile("rdcycle %0" : "=r"(c));
  return c;
#else
  return prt_now_ns();
#endif
}

static uint64_t monotonic_ms(void) {
  return prt_now_ns() / 1000000ULL;
}

static const char *progress_pipebuf_kind_name(prt_pipebuf_kind_t kind) {
  switch (kind) {
    case PRT_BUF_C1_ENTRY_DRAM_OR_DEPEN: return "c1-entry";
    case PRT_BUF_C2_EXPORT_DRAM_OR_DEPEN: return "c2-export";
    case PRT_BUF_C3_ISOLATE_NO_RING_PAIR: return "c3-isolate";
    case PRT_BUF_C4_SHARED_NO_RING_PAIR: return "c4-shared";
    case PRT_BUF_C5_ENTRY_ISOLATE_WITH_RING: return "c5-entry-ring";
    case PRT_BUF_C6_EXPORT_ISOLATE_WITH_RING: return "c6-export-ring";
    case PRT_BUF_C7_ENTRY_ALL_RING: return "c7-entry-allring";
    case PRT_BUF_C8_EXPORT_ALL_RING: return "c8-export-allring";
    default: return "unknown";
  }
}

static void progress_log_worker_wait(uint32_t stage_id, uint32_t subbatch, const char *phase,
                                     prt_pipebuf_t *buf, uint32_t idx, uint64_t wait_begin_ms,
                                     uint64_t *last_log_ms, int rc) {
  uint64_t now = monotonic_ms();
  uint64_t elapsed_ms = now >= wait_begin_ms ? (now - wait_begin_ms) : 0;
  if (last_log_ms && *last_log_ms != 0 && now - *last_log_ms < 1000ULL) return;
  if (last_log_ms) *last_log_ms = now;

  if (!buf) {
    PRT_PROGRESS_LOG("worker stage=%u subbatch=%u waiting phase=%s elapsed_ms=%llu rc=%d",
                     stage_id, subbatch, phase ? phase : "unknown",
                     (unsigned long long)elapsed_ms, rc);
    return;
  }

  int full0, full1;
  int cmd_running0, cmd_running1;
  uint32_t cmd_count0, cmd_count1;
  int dma_live0, dma_live1;
  uint32_t in_use_idx, no_use_idx, sbatch_offset, fanout_total, fanout_pending;
  uint64_t state_epoch;
  uint32_t ring_head = 0;
  uint32_t ring_tail = 0;
  uint32_t ring_size = 0;

  pthread_mutex_lock(&buf->lock);
  full0 = buf->full[0];
  full1 = buf->full[1];
  cmd_running0 = buf->cmd_running[0];
  cmd_running1 = buf->cmd_running[1];
  cmd_count0 = buf->cmd_count[0];
  cmd_count1 = buf->cmd_count[1];
  dma_live0 = buf->dma_token_live[0];
  dma_live1 = buf->dma_token_live[1];
  in_use_idx = buf->in_use_idx;
  no_use_idx = buf->no_use_idx;
  sbatch_offset = buf->subbatch_offset;
  fanout_total = buf->fanout_total;
  fanout_pending = buf->fanout_pending;
  state_epoch = buf->state_epoch;
  pthread_mutex_unlock(&buf->lock);

  if (buf->ring) {
    pthread_mutex_lock(&buf->ring->lock);
    ring_head = buf->ring->head;
    ring_tail = buf->ring->tail;
    ring_size = buf->ring->size;
    pthread_mutex_unlock(&buf->ring->lock);
  }

  PRT_PROGRESS_LOG(
    "worker stage=%u subbatch=%u waiting phase=%s kind=%s tensor=%u idx=%u in_use=%u no_use=%u "
    "full=%d/%d cmd=%d:%u,%d:%u dma=%d/%d sb=%u epoch=%llu fanout=%u/%u ring=%u/%u/%u elapsed_ms=%llu rc=%d",
    stage_id, subbatch, phase ? phase : "unknown", progress_pipebuf_kind_name(buf->kind),
    buf->tensor_id, idx, in_use_idx, no_use_idx,
    full0, full1, cmd_running0, cmd_count0, cmd_running1, cmd_count1,
    dma_live0, dma_live1, sbatch_offset, (unsigned long long)state_epoch,
    fanout_pending, fanout_total, ring_head, ring_tail, ring_size,
    (unsigned long long)elapsed_ms, rc);
}

static uint32_t progress_pipebuf_sbatch(prt_pipebuf_t *b) {
  uint32_t sb = 0;
  if (!b) return 0;
  pthread_mutex_lock(&b->lock);
  sb = b->subbatch_offset;
  pthread_mutex_unlock(&b->lock);
  return sb;
}

static uint32_t progress_stage_sbatch(prt_pipebuf_t **entry_bufs, uint32_t entry_count,
                                      prt_pipebuf_t **export_bufs, uint32_t export_count) {
  if (export_count > 0 && export_bufs && export_bufs[0]) {
    return progress_pipebuf_sbatch(export_bufs[0]);
  }
  if (entry_count > 0 && entry_bufs && entry_bufs[0]) {
    return progress_pipebuf_sbatch(entry_bufs[0]);
  }
  return 0;
}

static int stage_has_tensor(const prt_stage_map_t *stage, uint32_t tensor_id, int search_entry, int search_export);
static int stage_has_fixed_tensor(const prt_stage_map_t *stage, uint32_t tensor_id);
static uint32_t stage_tensor_lazy_fetch_flag(const prt_stage_map_t *stage, uint32_t tensor_id);
static const prt_stage_map_t *runtime_stage_map(const prt_runtime_t *rt, uint32_t stage_id);
static prt_pipebuf_t *find_stage_pipebuf(prt_runtime_t *rt, uint32_t stage_id, uint32_t tensor_id, int is_entry);
static const prt_page_list_t *runtime_find_weight_pages(const prt_runtime_t *rt, uint32_t stage_id,
                                                        uint32_t tensor_id);
#if defined(__riscv)
static int copy_tensor_pages_to_model_aliases(prt_runtime_t *rt, uint32_t tensor_id,
                                              const prt_page_list_t *pages, size_t src_size,
                                              uint32_t manager_id, uint32_t stage_id);
#endif

static int runtime_bootstrap_spm_xlate(prt_runtime_t *rt) {
  int rc;
  if (!rt) return PRT_ERR_INVAL;
  if (!rt->cfg.spm_xlate_enable) return PRT_OK;

  for (uint32_t i = 0; i < rt->cfg.num_gemmini_mgrs; ++i) {
    uint32_t manager_id = rt->cfg.gemmini_mgr_base_id + i;
    rc = prt_gemmini_spm_xlate_cfg(manager_id, prt_spm_ptbr_pa(rt),
                                   prt_spm_pte_count(rt), rt->cfg.spm_page_shift, 1U);
    if (rc != PRT_OK) return rc;
    rc = prt_gemmini_spm_xlate_range(manager_id, rt->cfg.spm_xlate_range_base,
                                     rt->cfg.spm_xlate_range_size);
    if (rc != PRT_OK) return rc;
    rc = prt_gemmini_spm_xlate_flush(manager_id);
    if (rc != PRT_OK) return rc;
  }
  return PRT_OK;
}

static int runtime_flush_spm_xlate(prt_runtime_t *rt) {
  int rc;
  if (!rt) return PRT_ERR_INVAL;
  if (!rt->cfg.spm_xlate_enable) return PRT_OK;

  for (uint32_t i = 0; i < rt->cfg.num_gemmini_mgrs; ++i) {
    uint32_t manager_id = rt->cfg.gemmini_mgr_base_id + i;
    rc = prt_gemmini_spm_xlate_flush(manager_id);
    if (rc != PRT_OK) return rc;
  }
  return PRT_OK;
}

void prt_trace_reset(prt_runtime_t *rt) {
  if (!rt) return;
  rt->trace_run_start_ns = 0;
  rt->trace_run_end_ns = 0;
  rt->trace_dma_submit_count = 0;
  rt->trace_dma_complete_count = 0;
  rt->trace_dma_inflight = 0;
  rt->trace_dma_inflight_peak = 0;
  rt->trace_dma_busy_ns = 0;
  rt->trace_gemm_issue_count = 0;
  rt->trace_gemm_fence_count = 0;
  rt->trace_gemm_busy_ns = 0;
  rt->trace_prefetch_attempt_count = 0;
  rt->trace_prefetch_success_count = 0;
  rt->trace_export_submit_ahead_count = 0;
  rt->trace_export_retire_count = 0;
  rt->trace_cycle_ref = 0;
  rt->trace_ns_ref = 0;
  rt->trace_event_count = 0;
  rt->trace_event_drop_count = 0;
}

void prt_trace_run_start(prt_runtime_t *rt) {
  if (!rt) return;
  rt->trace_run_start_ns = prt_now_ns();
  rt->trace_run_end_ns = 0;
  rt->trace_cycle_ref = prt_now_cycle();
  rt->trace_ns_ref = rt->trace_run_start_ns;
  prt_trace_log_event(rt, UINT32_MAX, PRT_TRACE_EVT_RUN_START, 0, 0);
}

void prt_trace_run_end(prt_runtime_t *rt) {
  if (!rt) return;
  rt->trace_run_end_ns = prt_now_ns();
  prt_trace_log_event(rt, UINT32_MAX, PRT_TRACE_EVT_RUN_END, 0, 0);
}

int prt_trace_calibrate_cycle(prt_runtime_t *rt) {
  uint64_t min_delta = ULLONG_MAX;
  if (!rt) return PRT_ERR_INVAL;
  if (!rt->cfg.trace_path || rt->cfg.trace_path[0] == '\0') {
    rt->trace_cycle_overhead = 0;
    return PRT_OK;
  }

  for (uint32_t i = 0; i < 1024; ++i) {
    uint64_t t0 = prt_now_cycle();
    uint64_t t1 = prt_now_cycle();
    uint64_t d = t1 >= t0 ? (t1 - t0) : 0;
    if (d < min_delta) min_delta = d;
  }

  rt->trace_cycle_overhead = (min_delta == ULLONG_MAX) ? 0 : min_delta;
  return PRT_OK;
}

void prt_trace_log_event(prt_runtime_t *rt, uint32_t stage_id, prt_trace_event_kind_t kind,
                         uint32_t aux0, uint32_t aux1) {
  uint32_t idx;
  prt_trace_event_t *evt;
  uint64_t cyc;
  if (!rt || !rt->trace_events || rt->trace_event_cap == 0) return;

  idx = __sync_fetch_and_add(&rt->trace_event_count, 1U);
  if (idx >= rt->trace_event_cap) {
    (void)__sync_add_and_fetch(&rt->trace_event_drop_count, 1U);
    return;
  }

  evt = &rt->trace_events[idx];
  cyc = prt_now_cycle();
  if (cyc >= rt->trace_cycle_overhead) cyc -= rt->trace_cycle_overhead;
  else cyc = 0;

  evt->cycle = cyc;
  evt->mono_ns = prt_now_ns();
  evt->stage_id = stage_id;
  evt->kind = (uint32_t)kind;
  evt->aux0 = aux0;
  evt->aux1 = aux1;
}

void prt_trace_on_dma_submit(prt_runtime_t *rt) {
  if (!rt) return;
  pthread_mutex_lock(&rt->state_lock);
  rt->trace_dma_submit_count += 1ULL;
  rt->trace_dma_inflight += 1ULL;
  if (rt->trace_dma_inflight > rt->trace_dma_inflight_peak) {
    rt->trace_dma_inflight_peak = rt->trace_dma_inflight;
  }
  pthread_mutex_unlock(&rt->state_lock);
}

void prt_trace_on_dma_complete(prt_runtime_t *rt, uint64_t elapsed_ns) {
  if (!rt) return;
  pthread_mutex_lock(&rt->state_lock);
  rt->trace_dma_complete_count += 1ULL;
  if (elapsed_ns > 0) rt->trace_dma_busy_ns += elapsed_ns;
  if (rt->trace_dma_inflight > 0) rt->trace_dma_inflight -= 1ULL;
  pthread_mutex_unlock(&rt->state_lock);
}

void prt_trace_on_gemm_issue(prt_runtime_t *rt) {
  if (!rt) return;
  pthread_mutex_lock(&rt->state_lock);
  rt->trace_gemm_issue_count += 1ULL;
  pthread_mutex_unlock(&rt->state_lock);
}

void prt_trace_on_gemm_fence(prt_runtime_t *rt) {
  if (!rt) return;
  pthread_mutex_lock(&rt->state_lock);
  rt->trace_gemm_fence_count += 1ULL;
  pthread_mutex_unlock(&rt->state_lock);
}

void prt_trace_on_gemm_busy(prt_runtime_t *rt, uint64_t elapsed_ns) {
  if (!rt || elapsed_ns == 0) return;
  pthread_mutex_lock(&rt->state_lock);
  rt->trace_gemm_busy_ns += elapsed_ns;
  pthread_mutex_unlock(&rt->state_lock);
}

void prt_trace_on_prefetch(prt_runtime_t *rt, int success) {
  if (!rt) return;
  pthread_mutex_lock(&rt->state_lock);
  rt->trace_prefetch_attempt_count += 1ULL;
  if (success) rt->trace_prefetch_success_count += 1ULL;
  pthread_mutex_unlock(&rt->state_lock);
}

void prt_trace_on_export_submit_ahead(prt_runtime_t *rt) {
  if (!rt) return;
  pthread_mutex_lock(&rt->state_lock);
  rt->trace_export_submit_ahead_count += 1ULL;
  pthread_mutex_unlock(&rt->state_lock);
}

void prt_trace_on_export_retire(prt_runtime_t *rt) {
  if (!rt) return;
  pthread_mutex_lock(&rt->state_lock);
  rt->trace_export_retire_count += 1ULL;
  pthread_mutex_unlock(&rt->state_lock);
}

int prt_trace_dump(prt_runtime_t *rt) {
  FILE *fp;
  uint64_t run_ns;
  uint64_t overlap_est_ns;
  double dma_util;
  double gemm_util;
  double overlap_ratio;

  if (!rt) return PRT_ERR_INVAL;
  if (!rt->cfg.trace_path || rt->cfg.trace_path[0] == '\0') return PRT_OK;

  fp = fopen(rt->cfg.trace_path, "w");
  if (!fp) return PRT_ERR_IO;

  run_ns = 0;
  if (rt->trace_run_end_ns > rt->trace_run_start_ns) {
    run_ns = rt->trace_run_end_ns - rt->trace_run_start_ns;
  }
  overlap_est_ns = rt->trace_dma_busy_ns < rt->trace_gemm_busy_ns ?
                   rt->trace_dma_busy_ns : rt->trace_gemm_busy_ns;
  dma_util = run_ns ? (100.0 * (double)rt->trace_dma_busy_ns / (double)run_ns) : 0.0;
  gemm_util = run_ns ? (100.0 * (double)rt->trace_gemm_busy_ns / (double)run_ns) : 0.0;
  overlap_ratio = run_ns ? (100.0 * (double)overlap_est_ns / (double)run_ns) : 0.0;

  fprintf(fp, "run_ns=%llu\n", (unsigned long long)run_ns);
  fprintf(fp, "dma_submit_count=%llu\n", (unsigned long long)rt->trace_dma_submit_count);
  fprintf(fp, "dma_complete_count=%llu\n", (unsigned long long)rt->trace_dma_complete_count);
  fprintf(fp, "dma_inflight_peak=%llu\n", (unsigned long long)rt->trace_dma_inflight_peak);
  fprintf(fp, "dma_busy_ns=%llu\n", (unsigned long long)rt->trace_dma_busy_ns);
  fprintf(fp, "gemm_issue_count=%llu\n", (unsigned long long)rt->trace_gemm_issue_count);
  fprintf(fp, "gemm_fence_count=%llu\n", (unsigned long long)rt->trace_gemm_fence_count);
  fprintf(fp, "gemm_busy_ns=%llu\n", (unsigned long long)rt->trace_gemm_busy_ns);
  fprintf(fp, "prefetch_attempt_count=%llu\n", (unsigned long long)rt->trace_prefetch_attempt_count);
  fprintf(fp, "prefetch_success_count=%llu\n", (unsigned long long)rt->trace_prefetch_success_count);
  fprintf(fp, "export_submit_ahead_count=%llu\n", (unsigned long long)rt->trace_export_submit_ahead_count);
  fprintf(fp, "export_retire_count=%llu\n", (unsigned long long)rt->trace_export_retire_count);
  fprintf(fp, "trace_cycle_overhead=%llu\n", (unsigned long long)rt->trace_cycle_overhead);
  fprintf(fp, "trace_cycle_ref=%llu\n", (unsigned long long)rt->trace_cycle_ref);
  fprintf(fp, "trace_ns_ref=%llu\n", (unsigned long long)rt->trace_ns_ref);
  fprintf(fp, "spm_ptbr_pa=0x%llx\n", (unsigned long long)prt_spm_ptbr_pa(rt));
  fprintf(fp, "spm_pte_count=%u\n", prt_spm_pte_count(rt));
  fprintf(fp, "spm_fault_count=%llu\n", (unsigned long long)prt_spm_fault_count(rt));
  fprintf(fp, "spm_last_fault_vaddr=0x%llx\n", (unsigned long long)prt_spm_last_fault_vaddr(rt));
  fprintf(fp, "spm_last_fault_cause=%u\n", prt_spm_last_fault_cause(rt));
  fprintf(fp, "trace_event_count=%u\n", rt->trace_event_count > rt->trace_event_cap ?
          rt->trace_event_cap : rt->trace_event_count);
  fprintf(fp, "trace_event_drop_count=%u\n", rt->trace_event_drop_count);
  fprintf(fp, "overlap_est_ns=%llu\n", (unsigned long long)overlap_est_ns);
  fprintf(fp, "dma_util_pct=%.3f\n", dma_util);
  fprintf(fp, "gemm_util_pct=%.3f\n", gemm_util);
  fprintf(fp, "overlap_est_pct=%.3f\n", overlap_ratio);
  fprintf(fp, "event_format=idx,stage,kind,cycle,ns,aux0,aux1\n");
  {
    uint32_t n = rt->trace_event_count > rt->trace_event_cap ? rt->trace_event_cap : rt->trace_event_count;
    for (uint32_t i = 0; i < n; ++i) {
      const prt_trace_event_t *e = &rt->trace_events[i];
      fprintf(fp, "event_%u=%u,%u,%llu,%llu,%u,%u\n",
              i, e->stage_id, e->kind,
              (unsigned long long)e->cycle,
              (unsigned long long)e->mono_ns,
              e->aux0, e->aux1);
    }
  }
  fclose(fp);
  return PRT_OK;
}

#if defined(__linux__)
static int pick_affinity_cpu(uint32_t preferred_idx, int *out_cpu) {
  cpu_set_t allowed;
  uint32_t allowed_count = 0;
  uint32_t target_rank;
  if (!out_cpu) return PRT_ERR_INVAL;
  if (sched_getaffinity(0, sizeof(allowed), &allowed) != 0) return PRT_ERR_STATE;

  for (int cpu = 0; cpu < CPU_SETSIZE; ++cpu) {
    if (CPU_ISSET(cpu, &allowed)) allowed_count += 1U;
  }
  if (allowed_count == 0) return PRT_ERR_NOT_READY;

  target_rank = preferred_idx % allowed_count;
  for (int cpu = 0; cpu < CPU_SETSIZE; ++cpu) {
    if (!CPU_ISSET(cpu, &allowed)) continue;
    if (target_rank == 0U) {
      *out_cpu = cpu;
      return PRT_OK;
    }
    target_rank -= 1U;
  }
  return PRT_ERR_STATE;
}

static void stage_bind_current_thread(prt_runtime_t *rt, prt_stage_thread_ctx_t *ctx) {
  int target_cpu = -1;
  cpu_set_t set;
  int rc;
  if (!rt || !ctx) return;
  if (ctx->stage_id >= rt->stage_thread_count) return;

  rc = pick_affinity_cpu(rt->stage_acc_ids[ctx->stage_id], &target_cpu);
  if (rc != PRT_OK || target_cpu < 0) {
    fprintf(stderr, "stage[%u] affinity pick failed: rc=%s(%d)\n",
            ctx->stage_id, prt_err_str(rc), rc);
    return;
  }

  CPU_ZERO(&set);
  CPU_SET(target_cpu, &set);
  if (sched_setaffinity(0, sizeof(set), &set) != 0) {
    fprintf(stderr, "stage[%u] affinity set failed: cpu=%d errno=%d\n",
            ctx->stage_id, target_cpu, errno);
    return;
  }
}
#endif

static void build_abs_timeout(uint64_t timeout_ns, struct timespec *out) {
  struct timespec now;
  uint64_t nsec;
  clock_gettime(CLOCK_REALTIME, &now);
  nsec = (uint64_t)now.tv_nsec + timeout_ns;
  out->tv_sec = now.tv_sec + (time_t)(nsec / 1000000000ULL);
  out->tv_nsec = (long)(nsec % 1000000000ULL);
}

static int wait_pipebuf_cv(prt_pipebuf_t *buf, uint64_t timeout_ns) {
  int rc;
  struct timespec abs;
  if (!buf) return PRT_ERR_INVAL;
  pthread_mutex_lock(&buf->lock);
  if (timeout_ns == 0) {
    rc = pthread_cond_wait(&buf->cv, &buf->lock);
    pthread_mutex_unlock(&buf->lock);
    return rc == 0 ? PRT_OK : PRT_ERR_STATE;
  }
  build_abs_timeout(timeout_ns, &abs);
  rc = pthread_cond_timedwait(&buf->cv, &buf->lock, &abs);
  pthread_mutex_unlock(&buf->lock);
  if (rc == ETIMEDOUT) return PRT_ERR_TIMEOUT;
  return rc == 0 ? PRT_OK : PRT_ERR_STATE;
}

static void ringbuf_destroy(prt_ringbuf_t *rb) {
  if (!rb) return;
  if (rb->slot_pages) {
    for (uint32_t i = 0; i < rb->size; ++i) {
      free(rb->slot_pages[i].data);
      rb->slot_pages[i].data = NULL;
      rb->slot_pages[i].size = 0;
      rb->slot_pages[i].cap = 0;
    }
    free(rb->slot_pages);
  }
  free(rb->use_count.data);
  rb->slot_pages = NULL;
  rb->use_count.data = NULL;
  rb->use_count.size = rb->use_count.cap = 0;
  pthread_cond_destroy(&rb->cv);
  pthread_mutex_destroy(&rb->lock);
}

static void pipebuf_init(prt_pipebuf_t *b, uint32_t tensor_id, uint32_t stage_idx, uint32_t segment_idx, int is_entry,
                         prt_pipebuf_kind_t kind, int dbl) {
  memset(b, 0, sizeof(*b));
  b->tensor_id = tensor_id;
  b->stage_idx = stage_idx;
  b->segment_idx = segment_idx;
  b->is_entry = is_entry ? 1 : 0;
  b->kind = kind;
  b->with_double_buffer = dbl ? 1 : 0;
  b->in_use_idx = 0;
  b->no_use_idx = b->with_double_buffer ? 1U : 0U;
  pthread_mutex_init(&b->lock, NULL);
  pthread_cond_init(&b->cv, NULL);
}

static void pipebuf_destroy(prt_pipebuf_t *b) {
  if (!b) return;
  for (int i = 0; i < 2; ++i) {
    if (b->dma_token_live[i]) {
      (void)prt_dma_token_cleanup(&b->dma_tokens[i]);
      b->dma_token_live[i] = 0;
    }
  }
  for (int i = 0; i < 2; ++i) {
    free(b->slot_pages[i].data);
    b->slot_pages[i].data = NULL;
    b->slot_pages[i].size = 0;
    b->slot_pages[i].cap = 0;
  }
  pthread_cond_destroy(&b->cv);
  pthread_mutex_destroy(&b->lock);
}

static int is_entry_kind(prt_pipebuf_kind_t kind) {
  switch (kind) {
    case PRT_BUF_C1_ENTRY_DRAM_OR_DEPEN:
    case PRT_BUF_C3_ISOLATE_NO_RING_PAIR:
    case PRT_BUF_C4_SHARED_NO_RING_PAIR:
    case PRT_BUF_C5_ENTRY_ISOLATE_WITH_RING:
    case PRT_BUF_C7_ENTRY_ALL_RING:
      return 1;
    default:
      return 0;
  }
}

static int is_export_kind(prt_pipebuf_kind_t kind) {
  switch (kind) {
    case PRT_BUF_C2_EXPORT_DRAM_OR_DEPEN:
    case PRT_BUF_C3_ISOLATE_NO_RING_PAIR:
    case PRT_BUF_C4_SHARED_NO_RING_PAIR:
    case PRT_BUF_C6_EXPORT_ISOLATE_WITH_RING:
    case PRT_BUF_C8_EXPORT_ALL_RING:
      return 1;
    default:
      return 0;
  }
}

static int classify_kind(const char *tensor_type, int is_entry, int has_ring,
                         prt_pipebuf_kind_t *out_kind) {
  if (!tensor_type || !out_kind) return PRT_ERR_INVAL;

  if (!strcmp(tensor_type, "DRAM") || !strcmp(tensor_type, "DRAM_DEPEN")) {
    *out_kind = is_entry ? PRT_BUF_C1_ENTRY_DRAM_OR_DEPEN : PRT_BUF_C2_EXPORT_DRAM_OR_DEPEN;
    return PRT_OK;
  }
  if (!strcmp(tensor_type, "ISOLATE_SPM")) {
    if (has_ring) *out_kind = is_entry ? PRT_BUF_C5_ENTRY_ISOLATE_WITH_RING : PRT_BUF_C6_EXPORT_ISOLATE_WITH_RING;
    else *out_kind = PRT_BUF_C3_ISOLATE_NO_RING_PAIR;
    return PRT_OK;
  }
  if (!strcmp(tensor_type, "SHARED_SPM")) {
    *out_kind = PRT_BUF_C4_SHARED_NO_RING_PAIR;
    return PRT_OK;
  }
  if (!strcmp(tensor_type, "ALL_RINGBUFFER")) {
    if (!has_ring) return PRT_ERR_PARSE;
    *out_kind = is_entry ? PRT_BUF_C7_ENTRY_ALL_RING : PRT_BUF_C8_EXPORT_ALL_RING;
    return PRT_OK;
  }
  return PRT_ERR_PARSE;
}

static uint64_t synthetic_dram_base(uint32_t stage_id, uint32_t tensor_id, int is_entry, uint32_t slot) {
  uint64_t base = is_entry ? 0x80000000ULL : 0x90000000ULL;
  base += ((uint64_t)(stage_id & 0x0FFFU) << 20);
  base += ((uint64_t)(tensor_id & 0xFFFFU) << 4);
  base += (uint64_t)slot * 0x1000ULL;
  return base;
}

static const prt_model_layer_t *find_model_layer(const prt_model_desc_t *model, uint32_t layer_id) {
  if (!model || !model->layers) return NULL;
  for (uint32_t i = 0; i < model->num_layers; ++i) {
    if (model->layers[i].index == layer_id) return &model->layers[i];
  }
  if (layer_id < model->num_layers) return &model->layers[layer_id];
  return NULL;
}

static int get_layer_tensor_addr(const prt_model_layer_t *layer, uint32_t tensor_id, uint32_t slot, uint64_t *out_addr) {
  if (!layer || !out_addr) return PRT_ERR_INVAL;
  for (uint32_t i = 0; i < layer->tensor_count; ++i) {
    if (layer->tensor_ids[i] != tensor_id) continue;
    if (slot == 1 && i < layer->address2_count) {
      *out_addr = layer->address2[i];
      return PRT_OK;
    }
    if (i < layer->address_count) {
      *out_addr = layer->address[i];
      return PRT_OK;
    }
    if (i < layer->address2_count) {
      *out_addr = layer->address2[i];
      return PRT_OK;
    }
    return PRT_ERR_INVAL;
  }
  return PRT_ERR_NOT_READY;
}

static int resolve_model_addr_to_ptr(const prt_runtime_t *rt, uint64_t addr, uint64_t *out_ptr) {
  uint64_t off;
  uint64_t avail;
  uint64_t base_off;
  if (!rt || !out_ptr) return PRT_ERR_INVAL;
  if (!rt->model_blob || rt->model_blob_size == 0) {
    *out_ptr = addr;
    return PRT_OK;
  }
  if (rt->model_blob_offset >= rt->model_blob_size) return PRT_ERR_INVAL;
  base_off = (uint64_t)rt->model_blob_offset;
  avail = (uint64_t)rt->model_blob_size - base_off;

  // Primary mode: HybridMapper layer address is an offset from model base.
  if (addr < avail) {
    off = addr;
    *out_ptr = (uint64_t)((uint8_t *)rt->model_blob + base_off + off);
    return PRT_OK;
  }

  // Compatibility mode: allow absolute-style address with model addr_base.
  if (rt->model.addr_base > 0 && addr >= rt->model.addr_base) {
    off = addr - rt->model.addr_base;
    if (off < avail) {
      *out_ptr = (uint64_t)((uint8_t *)rt->model_blob + base_off + off);
      return PRT_OK;
    }
  }

  return PRT_ERR_INVAL;
}

static int load_model_blob_file(const char *path, void **out_buf, size_t *out_size) {
  const size_t align_bytes = 64U;
  FILE *f;
  long sz;
  void *buf = NULL;
  if (!path || !out_buf || !out_size) return PRT_ERR_INVAL;
  f = fopen(path, "rb");
  if (!f) return PRT_ERR_IO;
  if (fseek(f, 0, SEEK_END) != 0) {
    fclose(f);
    return PRT_ERR_IO;
  }
  sz = ftell(f);
  if (sz < 0) {
    fclose(f);
    return PRT_ERR_IO;
  }
  if (fseek(f, 0, SEEK_SET) != 0) {
    fclose(f);
    return PRT_ERR_IO;
  }
  if (sz > 0) {
    if (posix_memalign(&buf, align_bytes, (size_t)sz) != 0) {
      fclose(f);
      return PRT_ERR_NOMEM;
    }
  }
  if (sz > 0 && fread(buf, 1, (size_t)sz, f) != (size_t)sz) {
    free(buf);
    fclose(f);
    return PRT_ERR_IO;
  }
  fclose(f);
  *out_buf = buf;
  *out_size = (size_t)sz;
  return PRT_OK;
}

static int has_u32(const uint32_t *arr, uint32_t n, uint32_t v) {
  if (!arr) return 0;
  for (uint32_t i = 0; i < n; ++i) {
    if (arr[i] == v) return 1;
  }
  return 0;
}

static int append_u32_unique(uint32_t **arr, uint32_t *n, uint32_t *cap, uint32_t v) {
  uint32_t *tmp;
  uint32_t new_cap;
  if (!arr || !n || !cap) return PRT_ERR_INVAL;
  if (has_u32(*arr, *n, v)) return PRT_OK;
  if (*n < *cap) {
    (*arr)[(*n)++] = v;
    return PRT_OK;
  }
  new_cap = *cap ? (*cap << 1) : 8U;
  tmp = (uint32_t *)realloc(*arr, sizeof(uint32_t) * new_cap);
  if (!tmp) return PRT_ERR_NOMEM;
  *arr = tmp;
  *cap = new_cap;
  (*arr)[(*n)++] = v;
  return PRT_OK;
}

static void sort_u32_asc(uint32_t *arr, uint32_t n) {
  if (!arr || n < 2) return;
  for (uint32_t i = 0; i + 1 < n; ++i) {
    for (uint32_t j = i + 1; j < n; ++j) {
      if (arr[j] < arr[i]) {
        uint32_t t = arr[i];
        arr[i] = arr[j];
        arr[j] = t;
      }
    }
  }
}

static int get_layer_io_ids(const prt_model_layer_t *layer,
                            uint32_t *inputs, uint32_t *num_inputs,
                            uint32_t *weights, uint32_t *num_weights,
                            uint32_t *outputs, uint32_t *num_outputs) {
  if (!layer || !inputs || !num_inputs || !weights || !num_weights || !outputs || !num_outputs) {
    return PRT_ERR_INVAL;
  }

  *num_inputs = 0;
  *num_weights = 0;
  *num_outputs = 0;

  if (!strcmp(layer->type, "conv")) {
    if (layer->tensor_count >= 4) {
      weights[(*num_weights)++] = layer->tensor_ids[1];
      inputs[(*num_inputs)++] = layer->tensor_ids[2];
      outputs[(*num_outputs)++] = layer->tensor_ids[3];
      return PRT_OK;
    }
    return PRT_ERR_PARSE;
  }

  if (!strcmp(layer->type, "resadd")) {
    if (layer->tensor_count >= 3) {
      inputs[(*num_inputs)++] = layer->tensor_ids[0];
      inputs[(*num_inputs)++] = layer->tensor_ids[1];
      outputs[(*num_outputs)++] = layer->tensor_ids[2];
      return PRT_OK;
    }
    return PRT_ERR_PARSE;
  }

  return PRT_ERR_NOT_IMPL;
}

static int collect_model_input_output_ids(const prt_model_desc_t *model,
                                          uint32_t **out_input_ids, uint32_t *out_input_count,
                                          uint32_t **out_output_ids, uint32_t *out_output_count) {
  uint32_t *produced = NULL;
  uint32_t produced_n = 0;
  uint32_t produced_cap = 0;
  uint32_t *consumed_or_weight = NULL;
  uint32_t consumed_n = 0;
  uint32_t consumed_cap = 0;
  uint32_t *inputs = NULL;
  uint32_t inputs_n = 0;
  uint32_t inputs_cap = 0;
  uint32_t *outputs = NULL;
  uint32_t outputs_n = 0;
  uint32_t outputs_cap = 0;
  int rc = PRT_OK;

  if (!model || !out_input_ids || !out_input_count || !out_output_ids || !out_output_count) {
    return PRT_ERR_INVAL;
  }

  for (uint32_t i = 0; i < model->num_layers; ++i) {
    uint32_t in[2];
    uint32_t wt[1];
    uint32_t out[1];
    uint32_t in_n, wt_n, out_n;
    if (get_layer_io_ids(&model->layers[i], in, &in_n, wt, &wt_n, out, &out_n) != PRT_OK) continue;
    for (uint32_t j = 0; j < out_n; ++j) {
      rc = append_u32_unique(&produced, &produced_n, &produced_cap, out[j]);
      if (rc != PRT_OK) goto out;
    }
    for (uint32_t j = 0; j < in_n; ++j) {
      rc = append_u32_unique(&consumed_or_weight, &consumed_n, &consumed_cap, in[j]);
      if (rc != PRT_OK) goto out;
    }
    for (uint32_t j = 0; j < wt_n; ++j) {
      rc = append_u32_unique(&consumed_or_weight, &consumed_n, &consumed_cap, wt[j]);
      if (rc != PRT_OK) goto out;
    }
  }

  for (uint32_t i = 0; i < model->num_layers; ++i) {
    uint32_t in[2];
    uint32_t wt[1];
    uint32_t out[1];
    uint32_t in_n, wt_n, out_n;
    (void)wt;
    (void)wt_n;
    if (get_layer_io_ids(&model->layers[i], in, &in_n, wt, &wt_n, out, &out_n) != PRT_OK) continue;
    for (uint32_t j = 0; j < in_n; ++j) {
      if (!has_u32(produced, produced_n, in[j])) {
        rc = append_u32_unique(&inputs, &inputs_n, &inputs_cap, in[j]);
        if (rc != PRT_OK) goto out;
      }
    }
    for (uint32_t j = 0; j < out_n; ++j) {
      if (!has_u32(consumed_or_weight, consumed_n, out[j])) {
        rc = append_u32_unique(&outputs, &outputs_n, &outputs_cap, out[j]);
        if (rc != PRT_OK) goto out;
      }
    }
  }

  sort_u32_asc(inputs, inputs_n);
  sort_u32_asc(outputs, outputs_n);
  *out_input_ids = inputs;
  *out_input_count = inputs_n;
  *out_output_ids = outputs;
  *out_output_count = outputs_n;
  inputs = NULL;
  outputs = NULL;

out:
  free(produced);
  free(consumed_or_weight);
  free(inputs);
  free(outputs);
  return rc;
}

static size_t get_model_tensor_size_bytes(const prt_model_desc_t *model, uint32_t tensor_id) {
  size_t size = 0;
  if (!model || !model->layers) return 0;
  for (uint32_t i = 0; i < model->num_layers; ++i) {
    const prt_model_layer_t *layer = &model->layers[i];
    for (uint32_t j = 0; j < layer->tensor_count; ++j) {
      if (layer->tensor_ids[j] != tensor_id) continue;
      if (j < layer->tensor_size_count && layer->tensor_size[j] > size) {
        size = layer->tensor_size[j];
      }
    }
  }
  return size;
}

static size_t get_layer_tensor_slot_size_bytes(const prt_model_layer_t *layer, uint32_t slot, size_t fallback) {
  if (!layer) return fallback;
  if (slot < layer->tensor_size_count && layer->tensor_size[slot] > 0) {
    return layer->tensor_size[slot];
  }
  return fallback;
}

static void normalize_copy_prefix_zero(uint8_t *dst, size_t dst_size,
                                       const uint8_t *src, size_t src_size) {
  size_t copy_n;
  if (!dst || dst_size == 0) return;
  copy_n = dst_size < src_size ? dst_size : src_size;
  if (copy_n > 0 && src) memmove(dst, src, copy_n);
  if (dst_size > copy_n) memset(dst + copy_n, 0, dst_size - copy_n);
}

static int resolve_model_addr_to_slice(const prt_runtime_t *rt, uint64_t addr,
                                       uint8_t **out_ptr, size_t *out_avail) {
  uint64_t p = 0;
  uint8_t *base;
  uint8_t *end;
  uint8_t *ptr;
  int rc;
  if (!rt || !out_ptr || !out_avail) return PRT_ERR_INVAL;
  if (!rt->model_blob || rt->model_blob_size == 0) return PRT_ERR_NOT_READY;
  rc = resolve_model_addr_to_ptr(rt, addr, &p);
  if (rc != PRT_OK) return rc;
  base = (uint8_t *)rt->model_blob + rt->model_blob_offset;
  end = (uint8_t *)rt->model_blob + rt->model_blob_size;
  ptr = (uint8_t *)(uintptr_t)p;
  if (ptr < base || ptr > end) return PRT_ERR_INVAL;
  *out_ptr = ptr;
  *out_avail = (size_t)(end - ptr);
  return PRT_OK;
}

static int copy_tensor_data_to_model_aliases(prt_runtime_t *rt, uint32_t tensor_id,
                                             const uint8_t *src, size_t src_size) {
  int copied_any = 0;
  if (!rt || !src || src_size == 0 || !rt->model.layers) return PRT_ERR_INVAL;
  for (uint32_t i = 0; i < rt->model.num_layers; ++i) {
    const prt_model_layer_t *layer = &rt->model.layers[i];
    for (uint32_t j = 0; j < layer->tensor_count; ++j) {
      size_t expect_size;
      if (layer->tensor_ids[j] != tensor_id) continue;
      expect_size = get_layer_tensor_slot_size_bytes(layer, j, src_size);

      if (j < layer->address_count) {
        uint8_t *dst;
        size_t avail;
        int rc = resolve_model_addr_to_slice(rt, layer->address[j], &dst, &avail);
        if (rc == PRT_OK && avail >= expect_size) {
          normalize_copy_prefix_zero(dst, expect_size, src, src_size);
          copied_any = 1;
        }
      }
      if (j < layer->address2_count) {
        uint8_t *dst;
        size_t avail;
        int rc = resolve_model_addr_to_slice(rt, layer->address2[j], &dst, &avail);
        if (rc == PRT_OK && avail >= expect_size) {
          normalize_copy_prefix_zero(dst, expect_size, src, src_size);
          copied_any = 1;
        }
      }
    }
  }
  return copied_any ? PRT_OK : PRT_ERR_NOT_READY;
}

static int load_tensor_alias_normalized(const prt_runtime_t *rt, uint64_t addr,
                                        size_t alias_size, uint8_t *dst, size_t want_size) {
  uint8_t *src = NULL;
  size_t avail = 0;
  size_t needed;
  int rc;
  if (!rt || !dst || want_size == 0) return PRT_ERR_INVAL;
  rc = resolve_model_addr_to_slice(rt, addr, &src, &avail);
  if (rc != PRT_OK) return rc;
  needed = alias_size < want_size ? alias_size : want_size;
  if (avail < needed) return PRT_ERR_INVAL;
  normalize_copy_prefix_zero(dst, want_size, src, alias_size);
  return PRT_OK;
}

static int find_layer_tensor_slot(const prt_model_layer_t *layer, uint32_t tensor_id, uint32_t *out_slot) {
  if (!layer || !out_slot) return PRT_ERR_INVAL;
  for (uint32_t i = 0; i < layer->tensor_count; ++i) {
    if (layer->tensor_ids[i] == tensor_id) {
      *out_slot = i;
      return PRT_OK;
    }
  }
  return PRT_ERR_NOT_READY;
}

static int get_layer_tensor_source_slice(const prt_runtime_t *rt, const prt_model_layer_t *layer,
                                         uint32_t tensor_id, const uint8_t **out_src,
                                         size_t *out_src_size) {
  uint32_t slot = 0;
  uint8_t *src = NULL;
  size_t avail = 0;
  size_t size;
  int rc;
  if (!rt || !layer || !out_src || !out_src_size) return PRT_ERR_INVAL;
  rc = find_layer_tensor_slot(layer, tensor_id, &slot);
  if (rc != PRT_OK) return rc;
  size = get_layer_tensor_slot_size_bytes(layer, slot, get_model_tensor_size_bytes(&rt->model, tensor_id));
  if (slot < layer->address_count) rc = resolve_model_addr_to_slice(rt, layer->address[slot], &src, &avail);
  else if (slot < layer->address2_count) rc = resolve_model_addr_to_slice(rt, layer->address2[slot], &src, &avail);
  else return PRT_ERR_NOT_READY;
  if (rc != PRT_OK) return rc;
  if (avail < size) return PRT_ERR_INVAL;
  *out_src = src;
  *out_src_size = size;
  return PRT_OK;
}

#if defined(__riscv)
static int copy_tensor_pages_to_model_aliases(prt_runtime_t *rt, uint32_t tensor_id,
                                              const prt_page_list_t *pages, size_t src_size,
                                              uint32_t manager_id, uint32_t stage_id) {
  int copied_any = 0;
  if (!rt || !pages || !pages->data || pages->size == 0 || src_size == 0) return PRT_ERR_INVAL;
  for (uint32_t i = 0; i < rt->model.num_layers; ++i) {
    const prt_model_layer_t *layer = &rt->model.layers[i];
    for (uint32_t j = 0; j < layer->tensor_count; ++j) {
      size_t expect_size;
      if (layer->tensor_ids[j] != tensor_id) continue;
      expect_size = get_layer_tensor_slot_size_bytes(layer, j, src_size);
      if (j < layer->address_count) {
        uint64_t dst_addr;
        if (resolve_model_addr_to_ptr(rt, layer->address[j], &dst_addr) == PRT_OK) {
          int rc = prt_dma_copy_spm_pages_to_dram(rt, dst_addr, pages, manager_id, stage_id, tensor_id, 0);
          if (rc != PRT_OK) return rc;
          copied_any = 1;
        }
      }
      if (j < layer->address2_count) {
        uint64_t dst_addr;
        if (resolve_model_addr_to_ptr(rt, layer->address2[j], &dst_addr) == PRT_OK) {
          int rc = prt_dma_copy_spm_pages_to_dram(rt, dst_addr, pages, manager_id, stage_id, tensor_id, 0);
          if (rc != PRT_OK) return rc;
          copied_any = 1;
        }
      }
    }
  }
  return copied_any ? PRT_OK : PRT_ERR_NOT_READY;
}
#endif

static int stage_tensor_current_pages(prt_runtime_t *rt, const prt_stage_map_t *stage,
                                      uint32_t stage_id, uint32_t tensor_id,
                                      const prt_page_list_t **out_pages) {
  prt_pipebuf_t *buf;
  if (!rt || !stage || !out_pages) return PRT_ERR_INVAL;
  if (stage_has_fixed_tensor(stage, tensor_id)) {
    const prt_page_list_t *pages = runtime_find_weight_pages(rt, stage_id, tensor_id);
    if (!pages) return PRT_ERR_NOT_READY;
    *out_pages = pages;
    return PRT_OK;
  }
  buf = find_stage_pipebuf(rt, stage_id, tensor_id, 1);
  if (buf) {
    *out_pages = &buf->slot_pages[buf->in_use_idx];
    return PRT_OK;
  }
  buf = find_stage_pipebuf(rt, stage_id, tensor_id, 0);
  if (buf) {
    *out_pages = &buf->slot_pages[buf->in_use_idx];
    return PRT_OK;
  }
  return PRT_ERR_NOT_READY;
}

static int stage_prepare_exec_views(prt_runtime_t *rt, uint32_t stage_id, const prt_model_layer_t *layer) {
  const prt_stage_map_t *stage;
  uint32_t page_bytes;
  int need_flush = 0;
  if (!rt || !layer) return PRT_ERR_INVAL;
  stage = runtime_stage_map(rt, stage_id);
  if (!stage) return PRT_ERR_NOT_READY;
  if (stage->tensor_id_count != layer->tensor_count) return PRT_ERR_PARSE;
  page_bytes = rt->cfg.page_size_bytes ? rt->cfg.page_size_bytes : PRT_PAGE_SIZE_BYTES;

  for (uint32_t slot = 0; slot < layer->tensor_count && slot < stage->tensor_id_count; ++slot) {
    uint32_t tensor_id = layer->tensor_ids[slot];
    uint32_t local_bytes = stage->local_spm_tensor_bytes[slot];
    int tensor_in_spm = (slot < stage->spm_bypass_count &&
                         stage->spm_bypass[slot] == 0U &&
                         stage->local_spm_page_count[slot] > 0U);
    if (stage->tensor_ids[slot] != tensor_id) return PRT_ERR_PARSE;
    if (!tensor_in_spm) continue;
    if (local_bytes == 0U) {
      local_bytes = stage->local_spm_page_count[slot] * page_bytes;
    }

#if !defined(__riscv)
    if (!rt->stage_spm_shadow[stage_id] ||
        rt->stage_spm_shadow_bytes[stage_id] <
          (size_t)stage->local_spm_tensor_addr[slot] + (size_t)local_bytes) {
      return PRT_ERR_NOMEM;
    }
    if (stage_has_fixed_tensor(stage, tensor_id) || stage_has_tensor(stage, tensor_id, 1, 0)) {
      uint32_t lazy_fetch = stage_tensor_lazy_fetch_flag(stage, tensor_id);
      int reuse_loaded = stage_has_fixed_tensor(stage, tensor_id) &&
                         lazy_fetch != 0U &&
                         rt->stage_fixed_lazy_loaded[stage_id][slot] != 0U;
      const uint8_t *src = NULL;
      size_t src_size = 0;
      if (!reuse_loaded) {
        int rc = get_layer_tensor_source_slice(rt, layer, tensor_id, &src, &src_size);
        if (rc != PRT_OK) return rc;
        normalize_copy_prefix_zero(rt->stage_spm_shadow[stage_id] + stage->local_spm_tensor_addr[slot],
                                   local_bytes, src, src_size);
        if (stage_has_fixed_tensor(stage, tensor_id) && lazy_fetch != 0U) {
          rt->stage_fixed_lazy_loaded[stage_id][slot] = 1U;
        }
      }
    } else {
      memset(rt->stage_spm_shadow[stage_id] + stage->local_spm_tensor_addr[slot], 0, local_bytes);
    }
#else
    if (!rt->cfg.spm_xlate_enable) return PRT_ERR_NOT_READY;
    if (stage_has_fixed_tensor(stage, tensor_id)) {
      int rc;
      uint32_t lazy_fetch = stage_tensor_lazy_fetch_flag(stage, tensor_id);
      int reuse_loaded = lazy_fetch != 0U && rt->stage_fixed_lazy_loaded[stage_id][slot] != 0U;
      const uint8_t *src = NULL;
      size_t src_size = 0;
      const prt_page_list_t *pages = NULL;
      uint64_t timeout_ns = (uint64_t)rt->cfg.watchdog_timeout_ms * 1000000ULL;
      rc = stage_tensor_current_pages(rt, stage, stage_id, tensor_id, &pages);
      if (rc != PRT_OK) return rc;
      if (!reuse_loaded) {
        rc = get_layer_tensor_source_slice(rt, layer, tensor_id, &src, &src_size);
        if (rc != PRT_OK) return rc;
        rc = prt_dma_copy_dram_to_spm_pages(rt, pages, (uint64_t)(uintptr_t)src,
                                            rt->stage_dma_ids[stage_id], stage_id, tensor_id, timeout_ns);
        if (rc != PRT_OK) return rc;
        if (lazy_fetch != 0U) rt->stage_fixed_lazy_loaded[stage_id][slot] = 1U;
      }
    }
#endif

    if (rt->cfg.spm_xlate_enable) {
      const prt_page_list_t *pages = NULL;
      prt_page_list_t view;
      int rc = stage_tensor_current_pages(rt, stage, stage_id, tensor_id, &pages);
      if (rc != PRT_OK) return rc;
      if (!pages || !pages->data || pages->size < stage->local_spm_page_count[slot]) return PRT_ERR_STATE;
      memset(&view, 0, sizeof(view));
      view.data = pages->data;
      view.size = stage->local_spm_page_count[slot];
      view.cap = stage->local_spm_page_count[slot];
      rc = prt_spm_bind_vpages(rt,
                               rt->stage_spm_rebase_vpage[stage_id] + stage->local_spm_first_vpage[slot],
                               &view, stage->local_spm_page_count[slot]);
      if (rc != PRT_OK) return rc;
      need_flush = 1;
    }
  }

  if (need_flush) {
    return runtime_flush_spm_xlate(rt);
  }
  return PRT_OK;
}

static int stage_tensor_exec_addr(prt_runtime_t *rt, const prt_model_layer_t *layer,
                                  uint32_t stage_id, uint32_t tensor_id, uint64_t *out_addr) {
  const prt_stage_map_t *stage;
  prt_pipebuf_t *buf;
  uint32_t slot;
  int rc;
  if (!rt || !layer || !out_addr) return PRT_ERR_INVAL;
  stage = runtime_stage_map(rt, stage_id);
  if (!stage) return PRT_ERR_NOT_READY;
  rc = find_layer_tensor_slot(layer, tensor_id, &slot);
  if (rc != PRT_OK) return rc;
  if (slot < stage->spm_bypass_count &&
      stage->spm_bypass[slot] == 0U &&
      stage->local_spm_page_count[slot] > 0U) {
#if !defined(__riscv)
    if (!rt->stage_spm_shadow[stage_id]) return PRT_ERR_NOT_READY;
    *out_addr = (uint64_t)(uintptr_t)(rt->stage_spm_shadow[stage_id] + stage->local_spm_tensor_addr[slot]);
#else
    uint32_t page_bytes = rt->cfg.page_size_bytes ? rt->cfg.page_size_bytes : PRT_PAGE_SIZE_BYTES;
    *out_addr = rt->cfg.spm_xlate_range_base +
                (uint64_t)rt->stage_spm_rebase_vpage[stage_id] * (uint64_t)page_bytes +
                (uint64_t)stage->local_spm_tensor_addr[slot];
#endif
    return PRT_OK;
  }
  buf = find_stage_pipebuf(rt, stage_id, tensor_id, 1);
  if (buf && buf->dram_base_addr[buf->in_use_idx] != 0) {
    *out_addr = buf->dram_base_addr[buf->in_use_idx];
    return PRT_OK;
  }
  buf = find_stage_pipebuf(rt, stage_id, tensor_id, 0);
  if (buf && buf->dram_base_addr[buf->in_use_idx] != 0) {
    *out_addr = buf->dram_base_addr[buf->in_use_idx];
    return PRT_OK;
  }
  if (get_layer_tensor_addr(layer, tensor_id, 0, out_addr) != PRT_OK) return PRT_ERR_NOT_READY;
  return resolve_model_addr_to_ptr(rt, *out_addr, out_addr);
}

static int stage_tensor_host_addr(prt_runtime_t *rt, const prt_model_layer_t *layer,
                                  uint32_t stage_id, uint32_t tensor_id, uint64_t *out_addr) {
  prt_pipebuf_t *buf;
  if (!rt || !layer || !out_addr) return PRT_ERR_INVAL;

  buf = find_stage_pipebuf(rt, stage_id, tensor_id, 1);
  if (buf && buf->dram_base_addr[buf->in_use_idx] != 0) {
    *out_addr = buf->dram_base_addr[buf->in_use_idx];
    return PRT_OK;
  }

  buf = find_stage_pipebuf(rt, stage_id, tensor_id, 0);
  if (buf && buf->dram_base_addr[buf->in_use_idx] != 0) {
    *out_addr = buf->dram_base_addr[buf->in_use_idx];
    return PRT_OK;
  }

  if (get_layer_tensor_addr(layer, tensor_id, 0, out_addr) != PRT_OK) return PRT_ERR_NOT_READY;
  return resolve_model_addr_to_ptr(rt, *out_addr, out_addr);
}

static int runtime_resadd_ptr_to_host_offset(const void *ptr,
                                             uint64_t exec_base, size_t exec_bytes,
                                             uint64_t host_base, size_t host_bytes,
                                             size_t *out_off, const char **out_space) {
  uintptr_t addr;
  if (!ptr || !out_off || !out_space) return PRT_ERR_INVAL;
  addr = (uintptr_t)ptr;

  if (exec_base != 0 &&
      addr >= (uintptr_t)exec_base &&
      addr < (uintptr_t)exec_base + (uintptr_t)exec_bytes) {
    *out_off = (size_t)(addr - (uintptr_t)exec_base);
    *out_space = "spm_exec";
    return PRT_OK;
  }

  if (host_base != 0 &&
      addr >= (uintptr_t)host_base &&
      addr < (uintptr_t)host_base + (uintptr_t)host_bytes) {
    *out_off = (size_t)(addr - (uintptr_t)host_base);
    *out_space = "host";
    return PRT_OK;
  }

  return PRT_ERR_INVAL;
}

int prt_runtime_prepare_resadd_cpu_fallback(prt_runtime_t *rt, uint32_t stage_id,
                                            const prt_gemmini_resadd_desc_t *src,
                                            prt_gemmini_resadd_desc_t *host_desc,
                                            uint64_t *out_output_host_base,
                                            size_t *out_output_tensor_bytes,
                                            const prt_page_list_t **out_output_pages,
                                            uint32_t *out_output_tensor_id) {
  const prt_model_layer_t *layer;
  const prt_stage_map_t *stage;
  uint32_t slot_a = 0;
  uint32_t slot_b = 0;
  uint32_t slot_c = 0;
  uint64_t exec_a = 0;
  uint64_t exec_b = 0;
  uint64_t exec_c = 0;
  uint64_t host_a = 0;
  uint64_t host_b = 0;
  uint64_t host_c = 0;
  size_t bytes_a = 0;
  size_t bytes_b = 0;
  size_t bytes_c = 0;
  size_t off_a = 0;
  size_t off_b = 0;
  size_t off_c = 0;
  const char *space_a = "unknown";
  const char *space_b = "unknown";
  const char *space_c = "unknown";
  const prt_page_list_t *pages = NULL;
  int rc;

  if (!rt || !src || !host_desc) return PRT_ERR_INVAL;
  if (stage_id >= rt->stage_thread_count) return PRT_ERR_INVAL;

  layer = find_model_layer(&rt->model, rt->stage_layer_ids[stage_id]);
  stage = runtime_stage_map(rt, stage_id);
  if (!layer || !stage) return PRT_ERR_NOT_READY;
  if (strcmp(layer->type, "resadd") != 0 || layer->tensor_count < 3) return PRT_ERR_NOT_READY;

  rc = find_layer_tensor_slot(layer, layer->tensor_ids[0], &slot_a);
  if (rc != PRT_OK) return rc;
  rc = find_layer_tensor_slot(layer, layer->tensor_ids[1], &slot_b);
  if (rc != PRT_OK) return rc;
  rc = find_layer_tensor_slot(layer, layer->tensor_ids[2], &slot_c);
  if (rc != PRT_OK) return rc;

  bytes_a = get_layer_tensor_slot_size_bytes(layer, slot_a, get_model_tensor_size_bytes(&rt->model, layer->tensor_ids[0]));
  bytes_b = get_layer_tensor_slot_size_bytes(layer, slot_b, get_model_tensor_size_bytes(&rt->model, layer->tensor_ids[1]));
  bytes_c = get_layer_tensor_slot_size_bytes(layer, slot_c, get_model_tensor_size_bytes(&rt->model, layer->tensor_ids[2]));

  rc = stage_tensor_exec_addr(rt, layer, stage_id, layer->tensor_ids[0], &exec_a);
  if (rc != PRT_OK) return rc;
  rc = stage_tensor_exec_addr(rt, layer, stage_id, layer->tensor_ids[1], &exec_b);
  if (rc != PRT_OK) return rc;
  rc = stage_tensor_exec_addr(rt, layer, stage_id, layer->tensor_ids[2], &exec_c);
  if (rc != PRT_OK) return rc;

  rc = stage_tensor_host_addr(rt, layer, stage_id, layer->tensor_ids[0], &host_a);
  if (rc != PRT_OK) return rc;
  rc = stage_tensor_host_addr(rt, layer, stage_id, layer->tensor_ids[1], &host_b);
  if (rc != PRT_OK) return rc;
  rc = stage_tensor_host_addr(rt, layer, stage_id, layer->tensor_ids[2], &host_c);
  if (rc != PRT_OK) return rc;

  rc = runtime_resadd_ptr_to_host_offset(src->A, exec_a, bytes_a, host_a, bytes_a, &off_a, &space_a);
  if (rc != PRT_OK) return rc;
  rc = runtime_resadd_ptr_to_host_offset(src->B, exec_b, bytes_b, host_b, bytes_b, &off_b, &space_b);
  if (rc != PRT_OK) return rc;
  rc = runtime_resadd_ptr_to_host_offset(src->C, exec_c, bytes_c, host_c, bytes_c, &off_c, &space_c);
  if (rc != PRT_OK) return rc;

  *host_desc = *src;
  host_desc->A = (const void *)(uintptr_t)(host_a + off_a);
  host_desc->B = (const void *)(uintptr_t)(host_b + off_b);
  host_desc->C = (void *)(uintptr_t)(host_c + off_c);

  if (out_output_host_base) *out_output_host_base = host_c;
  if (out_output_tensor_bytes) *out_output_tensor_bytes = bytes_c;
  if (out_output_tensor_id) *out_output_tensor_id = layer->tensor_ids[2];
  if (out_output_pages) {
    if (slot_c < stage->spm_bypass_count &&
        stage->spm_bypass[slot_c] == 0U &&
        stage->local_spm_page_count[slot_c] > 0U) {
      rc = stage_tensor_current_pages(rt, stage, stage_id, layer->tensor_ids[2], &pages);
      if (rc != PRT_OK) return rc;
    }
    *out_output_pages = pages;
  }

  PRT_PROGRESS_LOG(
    "resadd-fallback-map stage=%u A_src=0x%llx A_exec=0x%llx A_host=0x%llx A_off=%llu A_space=%s "
    "B_src=0x%llx B_exec=0x%llx B_host=0x%llx B_off=%llu B_space=%s "
    "C_src=0x%llx C_exec=0x%llx C_host=0x%llx C_off=%llu C_space=%s output_bytes=%llu output_pages=%u",
    stage_id,
    (unsigned long long)(uintptr_t)src->A,
    (unsigned long long)exec_a,
    (unsigned long long)host_a,
    (unsigned long long)off_a,
    space_a,
    (unsigned long long)(uintptr_t)src->B,
    (unsigned long long)exec_b,
    (unsigned long long)host_b,
    (unsigned long long)off_b,
    space_b,
    (unsigned long long)(uintptr_t)src->C,
    (unsigned long long)exec_c,
    (unsigned long long)host_c,
    (unsigned long long)off_c,
    space_c,
    (unsigned long long)bytes_c,
    pages ? pages->size : 0U);
  return PRT_OK;
}

static int sync_stage_export_aliases(prt_runtime_t *rt, uint32_t stage_id) {
  const prt_segment_desc_t *seg;
  const prt_stage_map_t *stage;
  const prt_model_layer_t *layer;
  if (!rt || rt->pipeline.num_segments == 0 || !rt->pipeline.segments) return PRT_ERR_INVAL;
  seg = &rt->pipeline.segments[0];
  if (stage_id >= seg->num_stages) return PRT_ERR_INVAL;
  stage = &seg->stages[stage_id];
  layer = find_model_layer(&rt->model, stage->layer_id);
  if (!layer) return PRT_ERR_NOT_READY;
  for (uint32_t i = 0; i < stage->num_export; ++i) {
    uint32_t slot = 0;
    int rc = find_layer_tensor_slot(layer, stage->exports[i].tensor_id, &slot);
    if (rc != PRT_OK) return rc;
    if (slot < stage->spm_bypass_count &&
        stage->spm_bypass[slot] == 0U &&
        stage->local_spm_page_count[slot] > 0U) {
      size_t src_size = get_layer_tensor_slot_size_bytes(
        layer, slot, get_model_tensor_size_bytes(&rt->model, stage->exports[i].tensor_id));
#if !defined(__riscv)
      const uint8_t *src;
      if (!rt->stage_spm_shadow[stage_id]) return PRT_ERR_NOT_READY;
      src = rt->stage_spm_shadow[stage_id] + stage->local_spm_tensor_addr[slot];
      rc = copy_tensor_data_to_model_aliases(rt, stage->exports[i].tensor_id, src, src_size);
      if (rc != PRT_OK) return rc;
#else
      const prt_page_list_t *pages = NULL;
      rc = stage_tensor_current_pages(rt, stage, stage_id, stage->exports[i].tensor_id, &pages);
      if (rc != PRT_OK) return rc;
      rc = copy_tensor_pages_to_model_aliases(rt, stage->exports[i].tensor_id, pages,
                                              src_size, rt->stage_dma_ids[stage_id], stage_id);
      if (rc != PRT_OK) return rc;
#endif
    } else {
      const uint8_t *src = NULL;
      size_t src_size = 0;
      rc = get_layer_tensor_source_slice(rt, layer, stage->exports[i].tensor_id, &src, &src_size);
      if (rc != PRT_OK) return rc;
      rc = copy_tensor_data_to_model_aliases(rt, stage->exports[i].tensor_id, src, src_size);
      if (rc != PRT_OK) return rc;
    }
  }
  return PRT_OK;
}

static void compare_bytes_exact(const uint8_t *actual, const uint8_t *golden, size_t n,
                                size_t *out_mismatch, size_t *out_first_idx,
                                uint8_t *out_first_actual, uint8_t *out_first_golden,
                                uint32_t *out_max_abs_diff) {
  size_t mismatch = 0;
  size_t first_idx = (size_t)-1;
  uint8_t first_actual = 0;
  uint8_t first_golden = 0;
  uint32_t max_abs = 0;

  for (size_t i = 0; i < n; ++i) {
    int a = (int)(int8_t)actual[i];
    int g = (int)(int8_t)golden[i];
    uint32_t d = (uint32_t)(a > g ? (a - g) : (g - a));
    if (d > max_abs) max_abs = d;
    if (actual[i] == golden[i]) continue;
    if (first_idx == (size_t)-1) {
      first_idx = i;
      first_actual = actual[i];
      first_golden = golden[i];
    }
    mismatch += 1;
  }

  *out_mismatch = mismatch;
  *out_first_idx = first_idx;
  *out_first_actual = first_actual;
  *out_first_golden = first_golden;
  *out_max_abs_diff = max_abs;
}

static int compare_one_tensor_output(prt_runtime_t *rt, uint32_t tensor_id,
                                     const uint8_t *golden, size_t nbytes,
                                     size_t *out_best_mismatch, size_t *out_best_first_idx,
                                     uint8_t *out_best_first_actual, uint8_t *out_best_first_golden,
                                     uint32_t *out_best_max_abs) {
  size_t best_mismatch = (size_t)-1;
  size_t best_first_idx = (size_t)-1;
  uint8_t best_first_actual = 0;
  uint8_t best_first_golden = 0;
  uint32_t best_max_abs = 0;
  int found = 0;
  uint32_t seen_alias = 0;
  uint8_t *norm = NULL;

  if (!rt || !golden || nbytes == 0) return PRT_ERR_INVAL;
  norm = (uint8_t *)malloc(nbytes);
  if (!norm) return PRT_ERR_NOMEM;

  for (uint32_t i = 0; i < rt->model.num_layers; ++i) {
    const prt_model_layer_t *layer = &rt->model.layers[i];
    for (uint32_t j = 0; j < layer->tensor_count; ++j) {
      if (layer->tensor_ids[j] != tensor_id) continue;

      if (j < layer->address_count) {
        size_t alias_size = get_layer_tensor_slot_size_bytes(layer, j, nbytes);
        seen_alias += 1;
        if (load_tensor_alias_normalized(rt, layer->address[j], alias_size, norm, nbytes) == PRT_OK) {
          size_t mismatch, first_idx;
          uint8_t first_actual, first_golden;
          uint32_t max_abs;
          compare_bytes_exact(norm, golden, nbytes, &mismatch, &first_idx, &first_actual, &first_golden, &max_abs);
          if (!found || mismatch < best_mismatch) {
            best_mismatch = mismatch;
            best_first_idx = first_idx;
            best_first_actual = first_actual;
            best_first_golden = first_golden;
            best_max_abs = max_abs;
          }
          found = 1;
          if (mismatch == 0) goto done;
        }
      }

      if (j < layer->address2_count) {
        size_t alias_size = get_layer_tensor_slot_size_bytes(layer, j, nbytes);
        seen_alias += 1;
        if (load_tensor_alias_normalized(rt, layer->address2[j], alias_size, norm, nbytes) == PRT_OK) {
          size_t mismatch, first_idx;
          uint8_t first_actual, first_golden;
          uint32_t max_abs;
          compare_bytes_exact(norm, golden, nbytes, &mismatch, &first_idx, &first_actual, &first_golden, &max_abs);
          if (!found || mismatch < best_mismatch) {
            best_mismatch = mismatch;
            best_first_idx = first_idx;
            best_first_actual = first_actual;
            best_first_golden = first_golden;
            best_max_abs = max_abs;
          }
          found = 1;
          if (mismatch == 0) goto done;
        }
      }
    }
  }

done:
  if (!found) {
    fprintf(stderr, "tensor=%u output aliases seen=%u, none readable for %zu bytes\n",
            tensor_id, seen_alias, nbytes);
    return PRT_ERR_NOT_READY;
  }
  *out_best_mismatch = best_mismatch;
  *out_best_first_idx = best_first_idx;
  *out_best_first_actual = best_first_actual;
  *out_best_first_golden = best_first_golden;
  *out_best_max_abs = best_max_abs;
  return best_mismatch == 0 ? PRT_OK : PRT_ERR_MISMATCH;
}

static int map_model_inputs_from_blob(prt_runtime_t *rt, const uint32_t *input_ids, uint32_t input_count,
                                      const uint8_t *blob, size_t blob_size) {
  size_t off = 0;
  if (!rt || !blob) return PRT_ERR_INVAL;
  if (input_count == 0) return PRT_OK;

  for (uint32_t i = 0; i < input_count; ++i) {
    uint32_t tid = input_ids[i];
    size_t need = get_model_tensor_size_bytes(&rt->model, tid);
    int rc;
    if (need == 0) return PRT_ERR_INVAL;
    if (blob_size < off + need) return PRT_ERR_INVAL;
    rc = copy_tensor_data_to_model_aliases(rt, tid, blob + off, need);
    if (rc != PRT_OK) return rc;
    off += need;
  }
  return PRT_OK;
}

static int compare_model_outputs_with_blob(prt_runtime_t *rt, const uint32_t *output_ids, uint32_t output_count,
                                           const uint8_t *golden, size_t golden_size) {
  size_t off = 0;
  size_t total_mismatch = 0;
  uint32_t mismatch_tensors = 0;
  size_t summary_first_idx = (size_t)-1;
  uint32_t summary_first_tid = 0;
  uint8_t summary_first_actual = 0;
  uint8_t summary_first_golden = 0;
  uint32_t summary_max_abs = 0;
  if (!rt || !golden) return PRT_ERR_INVAL;
  if (output_count == 0) return PRT_OK;

  for (uint32_t i = 0; i < output_count; ++i) {
    uint32_t tid = output_ids[i];
    size_t need = get_model_tensor_size_bytes(&rt->model, tid);
    size_t mismatch, first_idx;
    uint8_t first_actual, first_golden;
    uint32_t max_abs;
    int rc;
    if (need == 0) return PRT_ERR_INVAL;
    if (golden_size < off + need) return PRT_ERR_INVAL;
    rc = compare_one_tensor_output(rt, tid, golden + off, need,
                                   &mismatch, &first_idx, &first_actual, &first_golden, &max_abs);
    if (rc != PRT_OK && rc != PRT_ERR_MISMATCH) {
      fprintf(stderr, "golden compare failed: tensor=%u rc=%s(%d)\n", tid, prt_err_str(rc), rc);
      return rc;
    }
    if (max_abs > summary_max_abs) summary_max_abs = max_abs;
    if (rc == PRT_ERR_MISMATCH) {
      mismatch_tensors += 1;
      total_mismatch += mismatch;
      if (summary_first_idx == (size_t)-1) {
        summary_first_tid = tid;
        summary_first_idx = first_idx;
        summary_first_actual = first_actual;
        summary_first_golden = first_golden;
      }
      fprintf(stderr,
              "golden mismatch: tensor=%u bytes=%zu mismatch=%zu first_idx=%zu actual=%d golden=%d max_abs=%u\n",
              tid, need, mismatch, first_idx, (int)(int8_t)first_actual, (int)(int8_t)first_golden, max_abs);
    }
    off += need;
  }

  if (mismatch_tensors > 0) {
    fprintf(stderr,
            "golden mismatch summary: tensors=%u total_mismatch=%zu first_tensor=%u first_idx=%zu actual=%d golden=%d max_abs=%u\n",
            mismatch_tensors, total_mismatch, summary_first_tid, summary_first_idx,
            (int)(int8_t)summary_first_actual, (int)(int8_t)summary_first_golden, summary_max_abs);
    return PRT_ERR_MISMATCH;
  }
  return PRT_OK;
}

static int load_one_tensor_output(prt_runtime_t *rt, uint32_t tensor_id, uint8_t *dst, size_t nbytes) {
  if (!rt || !dst || nbytes == 0) return PRT_ERR_INVAL;
  for (uint32_t i = 0; i < rt->model.num_layers; ++i) {
    const prt_model_layer_t *layer = &rt->model.layers[i];
    for (uint32_t j = 0; j < layer->tensor_count; ++j) {
      size_t alias_size;
      if (layer->tensor_ids[j] != tensor_id) continue;
      alias_size = get_layer_tensor_slot_size_bytes(layer, j, nbytes);
      if (j < layer->address_count &&
          load_tensor_alias_normalized(rt, layer->address[j], alias_size, dst, nbytes) == PRT_OK) {
        return PRT_OK;
      }
      if (j < layer->address2_count &&
          load_tensor_alias_normalized(rt, layer->address2[j], alias_size, dst, nbytes) == PRT_OK) {
        return PRT_OK;
      }
    }
  }
  return PRT_ERR_NOT_READY;
}

static int dump_model_outputs_to_blob_file(prt_runtime_t *rt, const uint32_t *output_ids,
                                           uint32_t output_count, const char *path) {
  FILE *fp;
  uint8_t *buf = NULL;
  size_t cap = 0;
  int rc = PRT_OK;

  if (!rt || !output_ids || output_count == 0 || !path) return PRT_ERR_INVAL;
  fp = fopen(path, "wb");
  if (!fp) return PRT_ERR_IO;

  for (uint32_t i = 0; i < output_count; ++i) {
    uint32_t tid = output_ids[i];
    size_t need = get_model_tensor_size_bytes(&rt->model, tid);
    if (need == 0) {
      rc = PRT_ERR_INVAL;
      break;
    }
    if (need > cap) {
      uint8_t *tmp = (uint8_t *)realloc(buf, need);
      if (!tmp) {
        rc = PRT_ERR_NOMEM;
        break;
      }
      buf = tmp;
      cap = need;
    }
    rc = load_one_tensor_output(rt, tid, buf, need);
    if (rc != PRT_OK) break;
    if (fwrite(buf, 1, need, fp) != need) {
      rc = PRT_ERR_IO;
      break;
    }
  }

  free(buf);
  fclose(fp);
  return rc;
}

static prt_pipebuf_t *find_export_pipebuf_for_tensor(prt_runtime_t *rt, uint32_t tensor_id) {
  if (!rt || !rt->pipebufs) return NULL;
  for (uint32_t i = 0; i < rt->pipebuf_count; ++i) {
    prt_pipebuf_t *b = &rt->pipebufs[i];
    if (!b->is_entry && is_export_kind(b->kind) && b->tensor_id == tensor_id) return b;
  }
  return NULL;
}

static uint32_t min_sink_sbatch_offset(prt_pipebuf_t **sink_bufs, uint32_t sink_count) {
  uint32_t min_off = UINT32_MAX;
  if (!sink_bufs || sink_count == 0) return 0;
  for (uint32_t i = 0; i < sink_count; ++i) {
    uint32_t off = 0;
    prt_pipebuf_t *b = sink_bufs[i];
    uint64_t wait_begin_ms = 0;
    uint64_t last_wait_log_ms = 0;
    int lock_rc = 0;
    if (!b) continue;
    wait_begin_ms = monotonic_ms();
    while ((lock_rc = pthread_mutex_trylock(&b->lock)) != 0) {
      uint64_t now;
      struct timespec ts;
      if (lock_rc != EBUSY) {
        pthread_mutex_lock(&b->lock);
        break;
      }
      now = monotonic_ms();
      if (now - wait_begin_ms >= 1000ULL &&
          (last_wait_log_ms == 0 || now - last_wait_log_ms >= 1000ULL)) {
        PRT_PROGRESS_ERR_LOG("sink-lock-wait tensor=%u stage=%u kind=%s idx=%u elapsed_ms=%llu subbatch=%u state_epoch=%llu",
                             b->tensor_id, b->stage_idx, progress_pipebuf_kind_name(b->kind),
                             b->in_use_idx,
                             (unsigned long long)(now - wait_begin_ms),
                             b->subbatch_offset,
                             (unsigned long long)b->state_epoch);
        last_wait_log_ms = now;
      }
      ts.tv_sec = 0;
      ts.tv_nsec = 1000000L;
      nanosleep(&ts, NULL);
    }
    off = b->subbatch_offset;
    pthread_mutex_unlock(&b->lock);
    if (off < min_off) min_off = off;
  }
  return min_off == UINT32_MAX ? 0 : min_off;
}

static int append_pipebuf_unique(prt_pipebuf_t ***arr, uint32_t *n, uint32_t *cap, prt_pipebuf_t *buf) {
  prt_pipebuf_t **tmp;
  uint32_t new_cap;
  if (!arr || !n || !cap || !buf) return PRT_ERR_INVAL;
  for (uint32_t i = 0; i < *n; ++i) {
    if ((*arr)[i] == buf) return PRT_OK;
  }
  if (*n < *cap) {
    (*arr)[(*n)++] = buf;
    return PRT_OK;
  }
  new_cap = *cap ? (*cap << 1) : 4U;
  tmp = (prt_pipebuf_t **)realloc(*arr, sizeof(prt_pipebuf_t *) * new_cap);
  if (!tmp) return PRT_ERR_NOMEM;
  *arr = tmp;
  *cap = new_cap;
  (*arr)[(*n)++] = buf;
  return PRT_OK;
}

static int has_entry_consumer_for_tensor(const prt_runtime_t *rt, uint32_t tensor_id) {
  if (!rt || !rt->pipebufs) return 0;
  for (uint32_t i = 0; i < rt->pipebuf_count; ++i) {
    const prt_pipebuf_t *b = &rt->pipebufs[i];
    if (!b->is_entry) continue;
    if (!is_entry_kind(b->kind)) continue;
    if (b->tensor_id == tensor_id) return 1;
  }
  return 0;
}

static const prt_ring_cfg_t *find_ring_cfg(const prt_segment_desc_t *seg, uint32_t tensor_id) {
  if (!seg) return NULL;
  for (uint32_t i = 0; i < seg->num_ring_cfg; ++i) {
    if (seg->ring_cfgs[i].tensor_id == tensor_id) return &seg->ring_cfgs[i];
  }
  return NULL;
}

static prt_ringbuf_t *find_ringbuf(prt_runtime_t *rt, uint32_t segment_idx, uint32_t tensor_id) {
  if (!rt) return NULL;
  for (uint32_t i = 0; i < rt->ringbuf_count; ++i) {
    if (rt->ringbufs[i].segment_idx == segment_idx && rt->ringbufs[i].tensor_id == tensor_id) {
      return &rt->ringbufs[i];
    }
  }
  return NULL;
}

static uint32_t map_lookup(const prt_u32_map_t *m, uint32_t key) {
  if (!m || !m->data) return 0;
  for (uint32_t i = 0; i < m->size; ++i) {
    if (m->data[i].key == key) return m->data[i].value;
  }
  return 0;
}

static int append_acc_unique(uint32_t *arr, uint32_t *n, uint32_t cap, uint32_t acc) {
  if (!arr || !n || *n >= cap) return PRT_ERR_INVAL;
  for (uint32_t i = 0; i < *n; ++i) {
    if (arr[i] == acc) return PRT_OK;
  }
  arr[(*n)++] = acc;
  return PRT_OK;
}

static int stage_has_tensor(const prt_stage_map_t *stage, uint32_t tensor_id, int search_entry, int search_export) {
  if (!stage) return 0;
  if (search_entry) {
    for (uint32_t i = 0; i < stage->num_entry; ++i) {
      if (stage->entry[i].tensor_id == tensor_id) return 1;
    }
  }
  if (search_export) {
    for (uint32_t i = 0; i < stage->num_export; ++i) {
      if (stage->exports[i].tensor_id == tensor_id) return 1;
    }
  }
  return 0;
}

static int stage_has_fixed_tensor(const prt_stage_map_t *stage, uint32_t tensor_id) {
  if (!stage) return 0;
  for (uint32_t i = 0; i < stage->fix_tensor_count; ++i) {
    if (stage->fix_tensor_ids[i] == tensor_id) return 1;
  }
  return 0;
}

static int stage_find_tensor_slot(const prt_stage_map_t *stage, uint32_t tensor_id, uint32_t *out_slot) {
  if (!stage || !out_slot) return PRT_ERR_INVAL;
  for (uint32_t i = 0; i < stage->tensor_id_count; ++i) {
    if (stage->tensor_ids[i] == tensor_id) {
      *out_slot = i;
      return PRT_OK;
    }
  }
  return PRT_ERR_NOT_READY;
}

static uint32_t stage_tensor_lazy_fetch_flag(const prt_stage_map_t *stage, uint32_t tensor_id) {
  uint32_t slot = 0;
  if (!stage || !stage->tensor_lazy_fetch_present) return 0;
  if (stage_find_tensor_slot(stage, tensor_id, &slot) != PRT_OK) return 0;
  if (slot >= stage->tensor_lazy_fetch_count) return 0;
  return stage->tensor_lazy_fetch[slot] != 0U ? 1U : 0U;
}

static const prt_stage_map_t *runtime_stage_map(const prt_runtime_t *rt, uint32_t stage_id) {
  const prt_segment_desc_t *seg;
  if (!rt || rt->pipeline.num_segments == 0 || !rt->pipeline.segments) return NULL;
  seg = &rt->pipeline.segments[0];
  if (stage_id >= seg->num_stages) return NULL;
  return &seg->stages[stage_id];
}

static prt_pipebuf_t *find_stage_pipebuf(prt_runtime_t *rt, uint32_t stage_id, uint32_t tensor_id, int is_entry) {
  if (!rt) return NULL;
  for (uint32_t i = 0; i < rt->pipebuf_count; ++i) {
    prt_pipebuf_t *b = &rt->pipebufs[i];
    if (b->stage_idx != stage_id || b->tensor_id != tensor_id) continue;
    if ((b->is_entry ? 1 : 0) == (is_entry ? 1 : 0)) return b;
  }
  return NULL;
}

static uint32_t runtime_stage_gemmini_mgr(const prt_runtime_t *rt, uint32_t stage_idx) {
  if (!rt) return 0;
  if (rt->active_action &&
      stage_idx < rt->active_action->acc_source.stage_count &&
      rt->active_action->acc_source.stage_assign &&
      rt->active_action->acc_source.stage_assign[stage_idx].acc_util > 0 &&
      rt->active_action->acc_source.stage_assign[stage_idx].gemmini_mgr_ids) {
    return rt->active_action->acc_source.stage_assign[stage_idx].gemmini_mgr_ids[0];
  }
  if (rt->cfg.num_cores == 0) return 0;
  return stage_idx % rt->cfg.num_cores;
}

static uint32_t runtime_stage_dma_mgr(const prt_runtime_t *rt, uint32_t stage_idx) {
  if (!rt) return 0;
  if (rt->active_action &&
      stage_idx < rt->active_action->acc_source.stage_count &&
      rt->active_action->acc_source.stage_assign &&
      rt->active_action->acc_source.stage_assign[stage_idx].acc_util > 0 &&
      rt->active_action->acc_source.stage_assign[stage_idx].dma_mgr_ids) {
    return rt->active_action->acc_source.stage_assign[stage_idx].dma_mgr_ids[0];
  }
  if (rt->cfg.num_cores == 0) return 0;
  return stage_idx % rt->cfg.num_cores;
}

static uint32_t runtime_stage_local_accs(const prt_runtime_t *rt, uint32_t stage_idx,
                                         uint32_t *out, uint32_t out_cap) {
  uint32_t n = 0;
  if (!rt || !out || out_cap == 0) return 0;
  if (rt->active_action &&
      stage_idx < rt->active_action->acc_source.stage_count &&
      rt->active_action->acc_source.stage_assign &&
      rt->active_action->acc_source.stage_assign[stage_idx].acc_util > 0 &&
      rt->active_action->acc_source.stage_assign[stage_idx].gemmini_mgr_ids) {
    const prt_stage_acc_assign_t *assign = &rt->active_action->acc_source.stage_assign[stage_idx];
    for (uint32_t i = 0; i < assign->acc_util; ++i) {
      uint32_t mgr = assign->gemmini_mgr_ids[i];
      uint32_t local = mgr;
      if (mgr >= rt->cfg.gemmini_mgr_base_id) local = mgr - rt->cfg.gemmini_mgr_base_id;
      if (local >= rt->cfg.num_cores) continue;
      if (append_acc_unique(out, &n, out_cap, local) != PRT_OK) break;
    }
  } else if (stage_idx < rt->stage_thread_count) {
    uint32_t count = rt->stage_tile_counts[stage_idx];
    if (count == 0) count = 1;
    if (count > PRT_MAX_CORES) count = PRT_MAX_CORES;
    for (uint32_t i = 0; i < count; ++i) {
      uint32_t mgr = rt->stage_mgr_ids[stage_idx][i];
      uint32_t local = mgr;
      if (mgr >= rt->cfg.gemmini_mgr_base_id) local = mgr - rt->cfg.gemmini_mgr_base_id;
      if (local >= rt->cfg.num_cores) continue;
      if (append_acc_unique(out, &n, out_cap, local) != PRT_OK) break;
    }
  }
  if (n == 0 && rt->cfg.num_cores > 0) {
    out[0] = stage_idx % rt->cfg.num_cores;
    n = 1;
  }
  return n;
}

static int collect_tensor_owner_accs(const prt_runtime_t *rt, uint32_t target_seg_idx, uint32_t tensor_id,
                                     int search_entry, int search_export,
                                     uint32_t *out, uint32_t *out_n, uint32_t out_cap) {
  uint32_t flat_stage_idx = 0;
  if (!rt || !out || !out_n || out_cap == 0) return PRT_ERR_INVAL;
  *out_n = 0;
  if (rt->cfg.num_cores == 0) return PRT_ERR_INVAL;

  for (uint32_t seg_idx = 0; seg_idx < rt->pipeline.num_segments; ++seg_idx) {
    const prt_segment_desc_t *seg = &rt->pipeline.segments[seg_idx];
    for (uint32_t s = 0; s < seg->num_stages; ++s) {
      const prt_stage_map_t *stage = &seg->stages[s];
      if (seg_idx == target_seg_idx &&
          stage_has_tensor(stage, tensor_id, search_entry, search_export)) {
        uint32_t owners[PRT_MAX_CORES];
        uint32_t owner_n = runtime_stage_local_accs(rt, flat_stage_idx, owners, PRT_MAX_CORES);
        for (uint32_t oi = 0; oi < owner_n; ++oi) {
          int rc = append_acc_unique(out, out_n, out_cap, owners[oi]);
          if (rc != PRT_OK) return rc;
        }
      }
      flat_stage_idx += 1;
    }
  }
  return PRT_OK;
}

typedef struct {
  uint32_t segment_idx;
  uint32_t tensor_id;
  uint32_t pages_per_slot;
  uint32_t slots;
  uint32_t preferred_accs[PRT_MAX_CORES];
  uint32_t preferred_cnt;
  prt_page_list_t canonical[2];
} shared_alloc_plan_t;

static shared_alloc_plan_t *find_shared_plan(shared_alloc_plan_t *plans, uint32_t n,
                                             uint32_t segment_idx, uint32_t tensor_id) {
  if (!plans) return NULL;
  for (uint32_t i = 0; i < n; ++i) {
    if (plans[i].segment_idx == segment_idx && plans[i].tensor_id == tensor_id) return &plans[i];
  }
  return NULL;
}

static void free_shared_plan_canonical(shared_alloc_plan_t *plans, uint32_t n) {
  if (!plans) return;
  for (uint32_t i = 0; i < n; ++i) {
    for (uint32_t slot = 0; slot < 2; ++slot) {
      free(plans[i].canonical[slot].data);
      plans[i].canonical[slot].data = NULL;
      plans[i].canonical[slot].size = 0;
      plans[i].canonical[slot].cap = 0;
    }
  }
}

static int clone_page_list(prt_page_list_t *dst, const prt_page_list_t *src) {
  if (!dst || !src) return PRT_ERR_INVAL;
  if (src->size == 0) {
    free(dst->data);
    dst->data = NULL;
    dst->size = 0;
    dst->cap = 0;
    return PRT_OK;
  }

  dst->data = (prt_page_t *)calloc(src->size, sizeof(prt_page_t));
  if (!dst->data) return PRT_ERR_NOMEM;
  memcpy(dst->data, src->data, sizeof(prt_page_t) * src->size);
  dst->size = src->size;
  dst->cap = src->size;
  return PRT_OK;
}

static void free_page_list_storage(prt_page_list_t *pl) {
  if (!pl) return;
  free(pl->data);
  pl->data = NULL;
  pl->size = 0;
  pl->cap = 0;
}

static void free_runtime_weight_bindings(prt_runtime_t *rt) {
  if (!rt || !rt->topo_weight_pages) return;
  for (uint32_t i = 0; i < rt->topo_weight_count; ++i) {
    free_page_list_storage(&rt->topo_weight_pages[i].pages);
  }
  free(rt->topo_weight_pages);
  rt->topo_weight_pages = NULL;
  rt->topo_weight_count = 0;
  rt->topo_weight_cap = 0;
}

static int runtime_find_weight_binding_index(const prt_runtime_t *rt, uint32_t stage_id, uint32_t tensor_id) {
  if (!rt) return -1;
  for (uint32_t i = 0; i < rt->topo_weight_count; ++i) {
    const prt_spm_page_binding_t *wb = &rt->topo_weight_pages[i];
    if (wb->tensor_id == tensor_id && wb->stage_id == stage_id) return (int)i;
  }
  return -1;
}

static int runtime_append_weight_binding(prt_runtime_t *rt, uint32_t stage_id, uint32_t tensor_id,
                                         const prt_page_list_t *pages) {
  prt_spm_page_binding_t *tmp;
  uint32_t new_cap;
  int rc;
  if (!rt || !pages) return PRT_ERR_INVAL;
  if (runtime_find_weight_binding_index(rt, stage_id, tensor_id) >= 0) return PRT_OK;
  if (rt->topo_weight_count >= rt->topo_weight_cap) {
    new_cap = rt->topo_weight_cap ? (rt->topo_weight_cap << 1) : 16U;
    tmp = (prt_spm_page_binding_t *)realloc(rt->topo_weight_pages,
                                            sizeof(prt_spm_page_binding_t) * new_cap);
    if (!tmp) return PRT_ERR_NOMEM;
    memset(tmp + rt->topo_weight_cap, 0,
           sizeof(prt_spm_page_binding_t) * (new_cap - rt->topo_weight_cap));
    rt->topo_weight_pages = tmp;
    rt->topo_weight_cap = new_cap;
  }
  memset(&rt->topo_weight_pages[rt->topo_weight_count], 0, sizeof(prt_spm_page_binding_t));
  rt->topo_weight_pages[rt->topo_weight_count].tensor_id = tensor_id;
  rt->topo_weight_pages[rt->topo_weight_count].stage_id = stage_id;
  rt->topo_weight_pages[rt->topo_weight_count].slot_id = 0;
  rc = clone_page_list(&rt->topo_weight_pages[rt->topo_weight_count].pages, pages);
  if (rc != PRT_OK) return rc;
  rt->topo_weight_count += 1U;
  return PRT_OK;
}

static const prt_page_list_t *runtime_find_weight_pages(const prt_runtime_t *rt, uint32_t stage_id,
                                                        uint32_t tensor_id) {
  if (!rt) return NULL;
  for (uint32_t i = 0; i < rt->topo_weight_count; ++i) {
    const prt_spm_page_binding_t *wb = &rt->topo_weight_pages[i];
    if (wb->tensor_id == tensor_id && wb->stage_id == stage_id) return &wb->pages;
  }
  return NULL;
}

static int register_shared_plan(const prt_runtime_t *rt, const prt_segment_desc_t *seg, uint32_t seg_idx,
                                uint32_t tensor_id, uint32_t pages_per_slot, uint32_t slots,
                                shared_alloc_plan_t *plans, uint32_t *plan_count, uint32_t plan_cap) {
  shared_alloc_plan_t *p;
  int rc;
  if (!rt || !seg || !plans || !plan_count) return PRT_ERR_INVAL;
  if (slots == 0 || slots > 2) return PRT_ERR_INVAL;

  p = find_shared_plan(plans, *plan_count, seg_idx, tensor_id);
  if (!p) {
    uint32_t read_first;
    if (*plan_count >= plan_cap) return PRT_ERR_INVAL;
    p = &plans[(*plan_count)++];
    memset(p, 0, sizeof(*p));
    p->segment_idx = seg_idx;
    p->tensor_id = tensor_id;
    p->pages_per_slot = pages_per_slot ? pages_per_slot : 1U;
    p->slots = slots;

    read_first = map_lookup(&seg->shared_tensor_is_read_first, tensor_id);
    rc = collect_tensor_owner_accs(rt, seg_idx, tensor_id,
                                   read_first ? 1 : 0, read_first ? 0 : 1,
                                   p->preferred_accs, &p->preferred_cnt, PRT_MAX_CORES);
    if (rc != PRT_OK) return rc;
    if (p->preferred_cnt == 0) {
      rc = collect_tensor_owner_accs(rt, seg_idx, tensor_id, 1, 1,
                                     p->preferred_accs, &p->preferred_cnt, PRT_MAX_CORES);
      if (rc != PRT_OK) return rc;
    }
  } else {
    if (p->pages_per_slot < pages_per_slot) p->pages_per_slot = pages_per_slot;
    if (p->slots < slots) p->slots = slots;
  }
  return PRT_OK;
}

static int runtime_validate_shared_aliasing(const prt_runtime_t *rt) {
  if (!rt || !rt->pipebufs) return PRT_ERR_INVAL;
  for (uint32_t i = 0; i < rt->pipebuf_count; ++i) {
    const prt_pipebuf_t *a = &rt->pipebufs[i];
    if (a->kind != PRT_BUF_C4_SHARED_NO_RING_PAIR) continue;
    for (uint32_t j = i + 1; j < rt->pipebuf_count; ++j) {
      const prt_pipebuf_t *b = &rt->pipebufs[j];
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

static int runtime_track_alloc_key(prt_runtime_t *rt, uint32_t key) {
  if (rt && rt->active_action) {
    return prt_action_track_alloc_key(rt->active_action, key);
  }
  uint32_t *tmp;
  uint32_t new_cap;
  if (!rt) return PRT_ERR_INVAL;
  if (rt->topo_alloc_count < rt->topo_alloc_cap) {
    rt->topo_alloc_keys[rt->topo_alloc_count++] = key;
    return PRT_OK;
  }
  new_cap = rt->topo_alloc_cap ? (rt->topo_alloc_cap << 1) : 64U;
  tmp = (uint32_t *)realloc(rt->topo_alloc_keys, sizeof(uint32_t) * new_cap);
  if (!tmp) return PRT_ERR_NOMEM;
  rt->topo_alloc_keys = tmp;
  rt->topo_alloc_cap = new_cap;
  rt->topo_alloc_keys[rt->topo_alloc_count++] = key;
  return PRT_OK;
}

static uint32_t get_stage_tensor_pages_total(const prt_segment_desc_t *seg, uint32_t stage_idx,
                                             uint32_t tensor_id, const char *tensor_type) {
  uint32_t pages = 0;
  if (!seg) return 1;

  if (stage_idx < seg->num_stage_spm_util) {
    pages = map_lookup(&seg->tensor_spm_util_in_stage[stage_idx], tensor_id);
  }
  if (pages == 0 && tensor_type && strcmp(tensor_type, "SHARED_SPM") == 0) {
    pages = map_lookup(&seg->tensor_spm_util_shared, tensor_id);
  }
  if (pages == 0) pages = 1;
  return pages;
}

static uint32_t get_ring_pages_per_slot(const prt_segment_desc_t *seg, const prt_ring_cfg_t *cfg) {
  uint32_t pages;
  if (!seg || !cfg) return 1;

  pages = cfg->size_per;
  if (pages == 0) {
    uint32_t total = map_lookup(&seg->tensor_spm_util_in_ringbuffer, cfg->tensor_id);
    uint32_t slots = cfg->count ? cfg->count : 1U;
    if (total > 0) pages = (total + slots - 1U) / slots;
  }
  if (pages == 0) pages = 1;
  return pages;
}

static int alloc_slot_pages(prt_runtime_t *rt, uint32_t *alloc_key_cursor,
                            uint32_t pages_per_slot, const uint32_t *preferred_accs,
                            uint32_t preferred_cnt, prt_page_list_t *dst) {
  prt_page_list_t tmp;
  uint32_t key;
  int rc;
  if (!rt || !alloc_key_cursor || !dst || pages_per_slot == 0) return PRT_ERR_INVAL;

  memset(&tmp, 0, sizeof(tmp));
  key = (*alloc_key_cursor)++;
  rc = prt_alloc_tensor_pages(rt, key,
                              (size_t)pages_per_slot * (size_t)(rt->cfg.page_size_bytes ?
                              rt->cfg.page_size_bytes : PRT_PAGE_SIZE_BYTES),
                              preferred_accs, preferred_cnt, &tmp);
  if (rc != PRT_OK) return rc;

  dst->data = (prt_page_t *)calloc(tmp.size, sizeof(prt_page_t));
  if (!dst->data) {
    (void)prt_release_tensor_pages(rt, key);
    return PRT_ERR_NOMEM;
  }
  memcpy(dst->data, tmp.data, sizeof(prt_page_t) * tmp.size);
  dst->size = tmp.size;
  dst->cap = tmp.size;

  rc = runtime_track_alloc_key(rt, key);
  if (rc != PRT_OK) {
    free(dst->data);
    dst->data = NULL;
    dst->size = 0;
    dst->cap = 0;
    (void)prt_release_tensor_pages(rt, key);
    return rc;
  }
  return PRT_OK;
}

static int init_ringbuf(prt_runtime_t *rt, prt_ringbuf_t *rb, uint32_t segment_idx, uint32_t tensor_id,
                        uint32_t ring_size, uint32_t out_degree, uint32_t pages_per_slot,
                        uint32_t *alloc_key_cursor, const uint32_t *preferred_accs,
                        uint32_t preferred_cnt) {
  int rc;
  if (!rt || !rb || !alloc_key_cursor) return PRT_ERR_INVAL;

  memset(rb, 0, sizeof(*rb));
  rb->segment_idx = segment_idx;
  rb->tensor_id = tensor_id;
  rb->size = ring_size == 0 ? 1U : ring_size;
  if (rb->size > PRT_MAX_RING_SLOTS) rb->size = PRT_MAX_RING_SLOTS;
  rb->out_degree = out_degree == 0 ? 1U : out_degree;
  rb->slot_pages = (prt_page_list_t *)calloc(rb->size, sizeof(prt_page_list_t));
  if (!rb->slot_pages) return PRT_ERR_NOMEM;

  pthread_mutex_init(&rb->lock, NULL);
  pthread_cond_init(&rb->cv, NULL);

  for (uint32_t i = 0; i < rb->size; ++i) {
    rc = alloc_slot_pages(rt, alloc_key_cursor, pages_per_slot ? pages_per_slot : 1U,
                          preferred_accs, preferred_cnt, &rb->slot_pages[i]);
    if (rc != PRT_OK) return rc;
  }
  return PRT_OK;
}

static int build_topology_from_pipeline(prt_runtime_t *rt) {
  uint32_t alloc_key_cursor = 0x70000000U;
  uint32_t total_pipebuf = 0;
  uint32_t pb_idx = 0;
  uint32_t ring_idx = 0;
  uint32_t flat_stage_idx = 0;
  shared_alloc_plan_t shared_plans[PRT_MAX_TENSORS];
  uint32_t shared_plan_count = 0;
  int rc;

  if (!rt) return PRT_ERR_INVAL;
  if (rt->pipeline.num_segments == 0 || !rt->pipeline.segments) return PRT_ERR_PARSE;
  memset(shared_plans, 0, sizeof(shared_plans));
  rt->topo_weight_count = 0;

  rt->stage_thread_count = 0;
  rt->ringbuf_count = 0;
  for (uint32_t seg_idx = 0; seg_idx < rt->pipeline.num_segments; ++seg_idx) {
    const prt_segment_desc_t *seg = &rt->pipeline.segments[seg_idx];
    if (seg->num_stages == 0 || !seg->stages) continue;
    for (uint32_t i = 0; i < seg->num_ring_cfg; ++i) {
      if (seg->ring_cfgs[i].count > 0) rt->ringbuf_count += 1;
    }
    for (uint32_t s = 0; s < seg->num_stages; ++s) {
      if (rt->stage_thread_count >= PRT_MAX_STAGES) return PRT_ERR_INVAL;
      rt->stage_thread_count += 1;
      total_pipebuf += seg->stages[s].num_entry;
      total_pipebuf += seg->stages[s].num_export;
    }
  }
  if (rt->stage_thread_count == 0 || total_pipebuf == 0) return PRT_ERR_PARSE;

  if (rt->ringbuf_count > 0) {
    rt->ringbufs = (prt_ringbuf_t *)calloc(rt->ringbuf_count, sizeof(prt_ringbuf_t));
    if (!rt->ringbufs) return PRT_ERR_NOMEM;
  }

  for (uint32_t seg_idx = 0; seg_idx < rt->pipeline.num_segments; ++seg_idx) {
    const prt_segment_desc_t *seg = &rt->pipeline.segments[seg_idx];
    for (uint32_t i = 0; i < seg->num_ring_cfg; ++i) {
      const prt_ring_cfg_t *cfg = &seg->ring_cfgs[i];
      uint32_t preferred_accs[PRT_MAX_CORES];
      uint32_t preferred_cnt = 0;
      uint32_t pages_per_slot;
      if (cfg->count == 0) continue;
      pages_per_slot = get_ring_pages_per_slot(seg, cfg);
      rc = collect_tensor_owner_accs(rt, seg_idx, cfg->tensor_id, 1, 1,
                                     preferred_accs, &preferred_cnt, PRT_MAX_CORES);
      if (rc != PRT_OK) return rc;
      rc = init_ringbuf(rt, &rt->ringbufs[ring_idx], seg_idx, cfg->tensor_id, cfg->count,
                        cfg->use_count, pages_per_slot, &alloc_key_cursor,
                        preferred_accs, preferred_cnt);
      if (rc != PRT_OK) return rc;
      ring_idx += 1;
    }
  }

  rt->pipebuf_count = total_pipebuf;
  rt->pipebufs = (prt_pipebuf_t *)calloc(total_pipebuf, sizeof(prt_pipebuf_t));
  if (!rt->pipebufs) return PRT_ERR_NOMEM;

  for (uint32_t seg_idx = 0; seg_idx < rt->pipeline.num_segments; ++seg_idx) {
    const prt_segment_desc_t *seg = &rt->pipeline.segments[seg_idx];
    for (uint32_t s = 0; s < seg->num_stages; ++s) {
      const prt_stage_map_t *stage = &seg->stages[s];
      const prt_model_layer_t *model_layer = find_model_layer(&rt->model, stage->layer_id);
      uint32_t stage_acc = runtime_stage_gemmini_mgr(rt, flat_stage_idx);
      uint32_t stage_dma = runtime_stage_dma_mgr(rt, flat_stage_idx);

      rt->stage_layer_ids[flat_stage_idx] = stage->layer_id;
      rt->stage_acc_ids[flat_stage_idx] = stage_acc;
      rt->stage_dma_ids[flat_stage_idx] = stage_dma;
      rt->stage_tile_counts[flat_stage_idx] = stage->acc_util;
      rt->stage_split_kinds[flat_stage_idx] = stage->split_kind;
      memset(rt->stage_mgr_ids[flat_stage_idx], 0, sizeof(rt->stage_mgr_ids[flat_stage_idx]));
      if (rt->active_action &&
          flat_stage_idx < rt->active_action->acc_source.stage_count &&
          rt->active_action->acc_source.stage_assign &&
          rt->active_action->acc_source.stage_assign[flat_stage_idx].gemmini_mgr_ids) {
        uint32_t util = rt->active_action->acc_source.stage_assign[flat_stage_idx].acc_util;
        if (util > PRT_MAX_CORES) util = PRT_MAX_CORES;
        for (uint32_t m = 0; m < util; ++m) {
          rt->stage_mgr_ids[flat_stage_idx][m] =
            rt->active_action->acc_source.stage_assign[flat_stage_idx].gemmini_mgr_ids[m];
        }
      } else {
        rt->stage_mgr_ids[flat_stage_idx][0] = stage_acc;
      }

      for (uint32_t i = 0; i < stage->fix_tensor_count; ++i) {
        uint32_t tensor_id = stage->fix_tensor_ids[i];
        uint32_t tensor_slot = 0;
        uint32_t preferred_accs[PRT_MAX_CORES];
        uint32_t preferred_cnt = 0;
        prt_page_list_t weight_pages;
        uint32_t page_need;
        if (stage_find_tensor_slot(stage, tensor_id, &tensor_slot) != PRT_OK) return PRT_ERR_PARSE;
        if (tensor_slot >= stage->spm_bypass_count || stage->spm_bypass[tensor_slot] != 0U) continue;
        page_need = stage->local_spm_page_count[tensor_slot];
        if (page_need == 0U) continue;
        if (runtime_find_weight_binding_index(rt, flat_stage_idx, tensor_id) >= 0) continue;
        preferred_cnt = runtime_stage_local_accs(rt, flat_stage_idx, preferred_accs, PRT_MAX_CORES);
        if (preferred_cnt == 0U) return PRT_ERR_PARSE;
        memset(&weight_pages, 0, sizeof(weight_pages));
        rc = alloc_slot_pages(rt, &alloc_key_cursor, page_need, preferred_accs, preferred_cnt, &weight_pages);
        if (rc != PRT_OK) return rc;
        rc = runtime_append_weight_binding(rt, flat_stage_idx, tensor_id, &weight_pages);
        free_page_list_storage(&weight_pages);
        if (rc != PRT_OK) return rc;
      }

      for (uint32_t i = 0; i < stage->num_entry; ++i) {
        const prt_tensor_binding_t *tb = &stage->entry[i];
        const prt_ring_cfg_t *ring_cfg = find_ring_cfg(seg, tb->tensor_id);
        uint32_t preferred_accs[PRT_MAX_CORES];
        uint32_t preferred_cnt = 0;
        uint32_t pages_total;
        uint32_t pages_per_slot;
        int has_ring = ring_cfg && ring_cfg->count > 0;
        prt_pipebuf_t *b = &rt->pipebufs[pb_idx++];
        prt_pipebuf_kind_t kind;

        rc = classify_kind(tb->tensor_type, 1, has_ring, &kind);
        if (rc != PRT_OK) {
          fprintf(stderr,
                  "classify_kind failed(entry): seg=%u stage_local=%u stage_flat=%u tensor=%u type=%s has_ring=%d rc=%d\n",
                  seg_idx, s, flat_stage_idx, tb->tensor_id, tb->tensor_type, has_ring, rc);
          return rc;
        }
        pipebuf_init(b, tb->tensor_id, flat_stage_idx, seg_idx, 1, kind, tb->double_buffer != 0);
        b->cmd_acc[0] = stage_dma;
        b->cmd_acc[1] = stage_dma;
        b->dram_base_addr[0] = synthetic_dram_base(flat_stage_idx, tb->tensor_id, 1, 0);
        b->dram_base_addr[1] = synthetic_dram_base(flat_stage_idx, tb->tensor_id, 1, 1);
        if (model_layer) {
          uint64_t addr0;
          uint64_t addr1;
          if (get_layer_tensor_addr(model_layer, tb->tensor_id, 0, &addr0) == PRT_OK &&
              resolve_model_addr_to_ptr(rt, addr0, &b->dram_base_addr[0]) != PRT_OK) {
            return PRT_ERR_INVAL;
          }
          if (get_layer_tensor_addr(model_layer, tb->tensor_id, 1, &addr1) == PRT_OK &&
              resolve_model_addr_to_ptr(rt, addr1, &b->dram_base_addr[1]) != PRT_OK) {
            return PRT_ERR_INVAL;
          }
        }

        if (!strcmp(tb->tensor_type, "DRAM_DEPEN") && has_ring) {
          b->dram_base_addr[0] = 0;
          b->dram_base_addr[1] = 0;
        }
        if (has_ring) {
          b->ring = find_ringbuf(rt, seg_idx, tb->tensor_id);
          if (!b->ring) return PRT_ERR_NOT_READY;
        }

        if (kind != PRT_BUF_C7_ENTRY_ALL_RING) {
          uint32_t slots = b->with_double_buffer ? 2U : 1U;
          pages_total = get_stage_tensor_pages_total(seg, s, tb->tensor_id, tb->tensor_type);
          pages_per_slot = (pages_total + slots - 1U) / slots;
          if (pages_per_slot == 0) pages_per_slot = 1;

          if (kind == PRT_BUF_C4_SHARED_NO_RING_PAIR) {
            rc = register_shared_plan(rt, seg, seg_idx, tb->tensor_id, pages_per_slot, slots,
                                      shared_plans, &shared_plan_count, PRT_MAX_TENSORS);
            if (rc != PRT_OK) return rc;
          } else {
            preferred_accs[0] = stage_acc;
            preferred_cnt = 1;
            for (uint32_t slot = 0; slot < slots; ++slot) {
              rc = alloc_slot_pages(rt, &alloc_key_cursor, pages_per_slot,
                                    preferred_accs, preferred_cnt, &b->slot_pages[slot]);
              if (rc != PRT_OK) return rc;
            }
          }
        }
      }

      for (uint32_t i = 0; i < stage->num_export; ++i) {
        const prt_tensor_binding_t *tb = &stage->exports[i];
        const prt_ring_cfg_t *ring_cfg = find_ring_cfg(seg, tb->tensor_id);
        uint32_t preferred_accs[PRT_MAX_CORES];
        uint32_t preferred_cnt = 0;
        uint32_t pages_total;
        uint32_t pages_per_slot;
        int has_ring = ring_cfg && ring_cfg->count > 0;
        prt_pipebuf_t *b = &rt->pipebufs[pb_idx++];
        prt_pipebuf_kind_t kind;

        rc = classify_kind(tb->tensor_type, 0, has_ring, &kind);
        if (rc != PRT_OK) {
          fprintf(stderr,
                  "classify_kind failed(export): seg=%u stage_local=%u stage_flat=%u tensor=%u type=%s has_ring=%d rc=%d\n",
                  seg_idx, s, flat_stage_idx, tb->tensor_id, tb->tensor_type, has_ring, rc);
          return rc;
        }
        pipebuf_init(b, tb->tensor_id, flat_stage_idx, seg_idx, 0, kind, tb->double_buffer != 0);
        b->cmd_acc[0] = stage_dma;
        b->cmd_acc[1] = stage_dma;
        b->dram_base_addr[0] = synthetic_dram_base(flat_stage_idx, tb->tensor_id, 0, 0);
        b->dram_base_addr[1] = synthetic_dram_base(flat_stage_idx, tb->tensor_id, 0, 1);
        if (model_layer) {
          uint64_t addr0;
          uint64_t addr1;
          if (get_layer_tensor_addr(model_layer, tb->tensor_id, 0, &addr0) == PRT_OK &&
              resolve_model_addr_to_ptr(rt, addr0, &b->dram_base_addr[0]) != PRT_OK) {
            return PRT_ERR_INVAL;
          }
          if (get_layer_tensor_addr(model_layer, tb->tensor_id, 1, &addr1) == PRT_OK &&
              resolve_model_addr_to_ptr(rt, addr1, &b->dram_base_addr[1]) != PRT_OK) {
            return PRT_ERR_INVAL;
          }
        }
        if (!strcmp(tb->tensor_type, "DRAM_DEPEN") && has_ring) {
          b->dram_base_addr[0] = 0;
          b->dram_base_addr[1] = 0;
        }
        if (has_ring) {
          b->ring = find_ringbuf(rt, seg_idx, tb->tensor_id);
          if (!b->ring) return PRT_ERR_NOT_READY;
        }

        if (kind != PRT_BUF_C8_EXPORT_ALL_RING) {
          uint32_t slots = b->with_double_buffer ? 2U : 1U;
          pages_total = get_stage_tensor_pages_total(seg, s, tb->tensor_id, tb->tensor_type);
          pages_per_slot = (pages_total + slots - 1U) / slots;
          if (pages_per_slot == 0) pages_per_slot = 1;

          if (kind == PRT_BUF_C4_SHARED_NO_RING_PAIR) {
            rc = register_shared_plan(rt, seg, seg_idx, tb->tensor_id, pages_per_slot, slots,
                                      shared_plans, &shared_plan_count, PRT_MAX_TENSORS);
            if (rc != PRT_OK) return rc;
          } else {
            preferred_accs[0] = stage_acc;
            preferred_cnt = 1;
            for (uint32_t slot = 0; slot < slots; ++slot) {
              rc = alloc_slot_pages(rt, &alloc_key_cursor, pages_per_slot,
                                    preferred_accs, preferred_cnt, &b->slot_pages[slot]);
              if (rc != PRT_OK) return rc;
            }
          }
        }
      }

      flat_stage_idx += 1;
    }
  }

  if (flat_stage_idx != rt->stage_thread_count || pb_idx != rt->pipebuf_count) return PRT_ERR_STATE;

  if (shared_plan_count > 0) {
    for (uint32_t i = 0; i < shared_plan_count; ++i) {
      shared_alloc_plan_t *p = &shared_plans[i];
      uint32_t preferred_accs[PRT_MAX_CORES];
      uint32_t preferred_cnt = p->preferred_cnt;

      if (preferred_cnt > 0) memcpy(preferred_accs, p->preferred_accs, sizeof(uint32_t) * preferred_cnt);
      else {
        preferred_accs[0] = 0;
        preferred_cnt = 1;
      }

      for (uint32_t slot = 0; slot < p->slots; ++slot) {
        rc = alloc_slot_pages(rt, &alloc_key_cursor, p->pages_per_slot,
                              preferred_accs, preferred_cnt, &p->canonical[slot]);
        if (rc != PRT_OK) {
          free_shared_plan_canonical(shared_plans, shared_plan_count);
          return rc;
        }
      }
    }

    for (uint32_t i = 0; i < rt->pipebuf_count; ++i) {
      prt_pipebuf_t *b = &rt->pipebufs[i];
      uint32_t slots;
      shared_alloc_plan_t *p;
      if (b->kind != PRT_BUF_C4_SHARED_NO_RING_PAIR) continue;
      p = find_shared_plan(shared_plans, shared_plan_count, b->segment_idx, b->tensor_id);
      if (!p) {
        free_shared_plan_canonical(shared_plans, shared_plan_count);
        return PRT_ERR_STATE;
      }
      slots = b->with_double_buffer ? 2U : 1U;
      if (slots > p->slots) {
        free_shared_plan_canonical(shared_plans, shared_plan_count);
        return PRT_ERR_STATE;
      }
      for (uint32_t slot = 0; slot < slots; ++slot) {
        rc = clone_page_list(&b->slot_pages[slot], &p->canonical[slot]);
        if (rc != PRT_OK) {
          free_shared_plan_canonical(shared_plans, shared_plan_count);
          return rc;
        }
      }
    }
    free_shared_plan_canonical(shared_plans, shared_plan_count);
  }

  for (uint32_t i = 0; i < rt->pipebuf_count; ++i) {
    prt_pipebuf_t *pre = &rt->pipebufs[i];
    if (pre->kind == PRT_BUF_C3_ISOLATE_NO_RING_PAIR && !pre->is_entry) {
      for (uint32_t j = 0; j < rt->pipebuf_count; ++j) {
        prt_pipebuf_t *nxt = &rt->pipebufs[j];
        if (nxt->kind == PRT_BUF_C3_ISOLATE_NO_RING_PAIR && nxt->is_entry &&
            nxt->segment_idx == pre->segment_idx && nxt->tensor_id == pre->tensor_id) {
          rt->isolate_pair_count += 1;
        }
      }
    } else if (pre->kind == PRT_BUF_C4_SHARED_NO_RING_PAIR && !pre->is_entry) {
      for (uint32_t j = 0; j < rt->pipebuf_count; ++j) {
        prt_pipebuf_t *nxt = &rt->pipebufs[j];
        if (nxt->kind == PRT_BUF_C4_SHARED_NO_RING_PAIR && nxt->is_entry &&
            nxt->segment_idx == pre->segment_idx && nxt->tensor_id == pre->tensor_id) {
          rt->shared_pair_count += (pre->with_double_buffer || nxt->with_double_buffer) ? 2U : 1U;
        }
      }
    }
  }

  if (rt->isolate_pair_count > 0) {
    rt->isolate_pairs = (prt_isolate_pair_t *)calloc(rt->isolate_pair_count, sizeof(prt_isolate_pair_t));
    if (!rt->isolate_pairs) return PRT_ERR_NOMEM;
  }
  if (rt->shared_pair_count > 0) {
    rt->shared_pairs = (prt_shared_pair_t *)calloc(rt->shared_pair_count, sizeof(prt_shared_pair_t));
    if (!rt->shared_pairs) return PRT_ERR_NOMEM;
  }

  {
    uint32_t iso_idx = 0;
    uint32_t shared_idx = 0;
    for (uint32_t i = 0; i < rt->pipebuf_count; ++i) {
      prt_pipebuf_t *pre = &rt->pipebufs[i];
      if (pre->kind == PRT_BUF_C3_ISOLATE_NO_RING_PAIR && !pre->is_entry) {
        uint32_t fanout = 0;
        for (uint32_t j = 0; j < rt->pipebuf_count; ++j) {
          prt_pipebuf_t *nxt = &rt->pipebufs[j];
          if (nxt->kind == PRT_BUF_C3_ISOLATE_NO_RING_PAIR && nxt->is_entry &&
              nxt->segment_idx == pre->segment_idx && nxt->tensor_id == pre->tensor_id) {
            prt_isolate_pair_t *pair = &rt->isolate_pairs[iso_idx++];
            pair->pre_export = pre;
            pair->nxt_entry = nxt;
            pair->pre_idx = &pre->in_use_idx;
            pair->nxt_idx = &nxt->in_use_idx;
            fanout += 1;
          }
        }
        pre->fanout_total = fanout;
      } else if (pre->kind == PRT_BUF_C4_SHARED_NO_RING_PAIR && !pre->is_entry) {
        for (uint32_t j = 0; j < rt->pipebuf_count; ++j) {
          prt_pipebuf_t *nxt = &rt->pipebufs[j];
          if (nxt->kind == PRT_BUF_C4_SHARED_NO_RING_PAIR && nxt->is_entry &&
              nxt->segment_idx == pre->segment_idx && nxt->tensor_id == pre->tensor_id) {
            uint32_t slots = (pre->with_double_buffer || nxt->with_double_buffer) ? 2U : 1U;
            for (uint32_t slot = 0; slot < slots; ++slot) {
              prt_shared_pair_t *pair = &rt->shared_pairs[shared_idx++];
              pair->pre_export = pre;
              pair->nxt_entry = nxt;
              pair->buffer_idx = slot;
              pair->tag = &pre->shared_tag[slot];
            }
          }
        }
      }
    }
  }

  rc = runtime_validate_shared_aliasing(rt);
  if (rc != PRT_OK) return rc;

  return PRT_OK;
}

static int runtime_prepare_stage_spm_windows(prt_runtime_t *rt) {
  uint32_t page_bytes;
  if (!rt) return PRT_ERR_INVAL;
  page_bytes = rt->cfg.page_size_bytes ? rt->cfg.page_size_bytes : PRT_PAGE_SIZE_BYTES;
  for (uint32_t stage_id = 0; stage_id < rt->stage_thread_count; ++stage_id) {
    const prt_stage_map_t *stage = runtime_stage_map(rt, stage_id);
    uint32_t span;
    if (!stage) return PRT_ERR_STATE;
    span = stage->local_spm_page_span;
    memset(rt->stage_fixed_lazy_loaded[stage_id], 0, sizeof(rt->stage_fixed_lazy_loaded[stage_id]));
    rt->stage_spm_rebase_vpage[stage_id] = 0;
    rt->stage_spm_window_pages[stage_id] = span;
    if (rt->stage_spm_shadow[stage_id]) {
      free(rt->stage_spm_shadow[stage_id]);
      rt->stage_spm_shadow[stage_id] = NULL;
      rt->stage_spm_shadow_bytes[stage_id] = 0;
    }
    if (span == 0U) continue;
    if (prt_spm_reserve_vpages(rt, span, &rt->stage_spm_rebase_vpage[stage_id]) != PRT_OK) {
      return PRT_ERR_NOMEM;
    }
    rt->stage_spm_shadow_bytes[stage_id] = (size_t)span * (size_t)page_bytes;
    rt->stage_spm_shadow[stage_id] = (uint8_t *)calloc(1, rt->stage_spm_shadow_bytes[stage_id]);
    if (!rt->stage_spm_shadow[stage_id]) return PRT_ERR_NOMEM;
  }
  return PRT_OK;
}

static int stage_wait_exports_ready(prt_runtime_t *rt, uint32_t stage_id, uint32_t subbatch,
                                    prt_pipebuf_t **export_bufs, uint32_t export_count,
                                    prt_shared_pair_t **shared_pairs, uint32_t shared_pair_count,
                                    uint64_t timeout_ns, uint64_t *last_wait_log_ms) {
  for (uint32_t i = 0; i < export_count; ++i) {
    prt_pipebuf_t *b = export_bufs[i];
    int rc;
    uint64_t wait_begin_ms = monotonic_ms();

    if (b->kind == PRT_BUF_C8_EXPORT_ALL_RING) {
      rc = prt_ring_wait_idle(b->ring, timeout_ns);
      if (rc == PRT_ERR_TIMEOUT) {
        progress_log_worker_wait(stage_id, subbatch, "export-ring-idle", b, b->in_use_idx,
                                 wait_begin_ms, last_wait_log_ms, rc);
      }
      if (rc != PRT_OK) return rc;
      rc = prt_process_c8(rt, b);
      if (rc != PRT_OK) return rc;
      continue;
    }

    if (b->kind == PRT_BUF_C4_SHARED_NO_RING_PAIR) {
      uint64_t start = monotonic_ms();
      while (1) {
        uint32_t idx = b->in_use_idx;
        pthread_mutex_lock(&b->lock);
        int empty = (b->full[idx] == 0);
        pthread_mutex_unlock(&b->lock);
        if (empty) break;

        for (uint32_t p = 0; p < shared_pair_count; ++p) {
          if (shared_pairs[p]->pre_export == b) {
            rc = prt_process_c4(rt, shared_pairs[p]);
            if (rc != PRT_OK) return rc;
          }
        }

        rc = wait_pipebuf_cv(b, 1000000ULL);
        if (rc == PRT_ERR_TIMEOUT) {
          progress_log_worker_wait(stage_id, subbatch, "export-c4-drain", b, idx,
                                   start, last_wait_log_ms, rc);
        }
        if (rc != PRT_OK && rc != PRT_ERR_TIMEOUT) return rc;
        if (timeout_ns > 0 && (monotonic_ms() - start) * 1000000ULL > timeout_ns) {
          progress_log_worker_wait(stage_id, subbatch, "export-c4-drain", b, idx,
                                   start, last_wait_log_ms, PRT_ERR_TIMEOUT);
          return PRT_ERR_TIMEOUT;
        }
      }
      continue;
    }

    if (rt->cfg.dma_backend == PRT_DMA_BACKEND_POLL_PROGRESS_THREAD &&
        (b->kind == PRT_BUF_C2_EXPORT_DRAM_OR_DEPEN ||
         b->kind == PRT_BUF_C6_EXPORT_ISOLATE_WITH_RING)) {
      uint64_t start = monotonic_ms();
      while (1) {
        uint32_t idx = b->in_use_idx;
        int empty = 0;
        rc = prt_progress_export_dma(rt, b, timeout_ns, 1);
        if (rc != PRT_OK) return rc;

        pthread_mutex_lock(&b->lock);
        empty = (b->full[idx] == 0);
        pthread_mutex_unlock(&b->lock);
        if (empty) break;

        rc = wait_pipebuf_cv(b, 1000000ULL);
        if (rc == PRT_ERR_TIMEOUT) {
          progress_log_worker_wait(stage_id, subbatch, "export-dma-retire", b, idx,
                                   start, last_wait_log_ms, rc);
        }
        if (rc != PRT_OK && rc != PRT_ERR_TIMEOUT) return rc;
        if (timeout_ns > 0 && (monotonic_ms() - start) * 1000000ULL > timeout_ns) {
          progress_log_worker_wait(stage_id, subbatch, "export-dma-retire", b, idx,
                                   start, last_wait_log_ms, PRT_ERR_TIMEOUT);
          return PRT_ERR_TIMEOUT;
        }
      }
    } else {
      rc = prt_pipebuf_wait_empty(b, b->in_use_idx, timeout_ns);
      if (rc == PRT_ERR_TIMEOUT) {
        progress_log_worker_wait(stage_id, subbatch, "export-empty", b, b->in_use_idx,
                                 wait_begin_ms, last_wait_log_ms, rc);
      }
      if (rc != PRT_OK) return rc;
    }
  }
  return PRT_OK;
}

static void rotate_buf_if_needed(prt_pipebuf_t *b) {
  if (!b || !b->with_double_buffer) return;
  pthread_mutex_lock(&b->lock);
  b->in_use_idx = (b->in_use_idx + 1U) & 1U;
  b->no_use_idx = (b->no_use_idx + 1U) & 1U;
  pthread_mutex_unlock(&b->lock);
}

static int stage_overlap_prefetch_entries(prt_runtime_t *rt, prt_pipebuf_t **entry_bufs,
                                          uint32_t entry_count, uint64_t timeout_ns) {
  uint64_t prefetch_timeout_ns = timeout_ns;
  if (!rt || !entry_bufs) return PRT_ERR_INVAL;
  if (prefetch_timeout_ns == 0 || prefetch_timeout_ns > 1000000ULL) {
    prefetch_timeout_ns = 1000000ULL;
  }

  for (uint32_t i = 0; i < entry_count; ++i) {
    prt_pipebuf_t *b = entry_bufs[i];
    uint32_t idx;
    int slot_empty;
    int slot_idle;
    int rc;
    if (!b || !b->with_double_buffer) continue;
    if (b->kind != PRT_BUF_C1_ENTRY_DRAM_OR_DEPEN &&
        b->kind != PRT_BUF_C5_ENTRY_ISOLATE_WITH_RING) {
      continue;
    }

    idx = b->no_use_idx;
    pthread_mutex_lock(&b->lock);
    slot_empty = (b->full[idx] == 0);
    slot_idle = (b->cmd_running[idx] == 0 && b->cmd_count[idx] == 0);
    pthread_mutex_unlock(&b->lock);
    if (!slot_empty || !slot_idle) continue;

    if (b->kind == PRT_BUF_C1_ENTRY_DRAM_OR_DEPEN) {
      rc = prt_process_c1(rt, b, idx, prefetch_timeout_ns);
    } else {
      rc = prt_process_c5(rt, b, idx, prefetch_timeout_ns);
    }
    prt_trace_on_prefetch(rt, rc == PRT_OK);

    if (rc == PRT_OK || rc == PRT_ERR_BUSY || rc == PRT_ERR_EMPTY || rc == PRT_ERR_TIMEOUT) {
      continue;
    }
    return rc;
  }

  return PRT_OK;
}

static int build_stage_conv_desc(prt_runtime_t *rt, uint32_t stage_id, prt_gemmini_conv_desc_t *out) {
  const prt_model_layer_t *layer;
  uint64_t addr_in;
  uint64_t addr_w;
  uint64_t addr_b;
  uint64_t addr_out;
  uint32_t N, IC, OC, OH, OW, KH, KW, G, sH, sW;
  int use_host_addrs = 0;

  if (!rt || !out) return PRT_ERR_INVAL;
  if (stage_id >= rt->stage_thread_count) return PRT_ERR_NOT_READY;
  layer = find_model_layer(&rt->model, rt->stage_layer_ids[stage_id]);
  if (!layer) return PRT_ERR_NOT_READY;
  if (strcmp(layer->type, "conv") != 0) return PRT_ERR_NOT_READY;
  if (layer->param_len < 10 || layer->address_count < 4) return PRT_ERR_NOT_READY;
  if (!rt->model_blob || rt->model_blob_size == 0) return PRT_ERR_NOT_READY;

  N = layer->param[0];
  IC = layer->param[1];
  OC = layer->param[2];
  OH = layer->param[3];
  OW = layer->param[4];
  KH = layer->param[5];
  KW = layer->param[6];
  G = layer->param[7];
  sH = layer->param[8];
  sW = layer->param[9];
  if (G != 1 || sH == 0 || sW == 0 || KH != KW) return PRT_ERR_NOT_IMPL;

  if (stage_prepare_exec_views(rt, stage_id, layer) != PRT_OK) {
    return PRT_ERR_STATE;
  }
#if defined(__riscv)
  use_host_addrs =
    rt->cfg.backend == PRT_BACKEND_FPGA &&
    (rt->stage_split_kinds[stage_id] == PRT_LAYER_SPLIT_OC ||
     rt->stage_split_kinds[stage_id] == PRT_LAYER_SPLIT_SPATIAL);
#else
  use_host_addrs = 0;
#endif
  if (((use_host_addrs &&
        stage_tensor_host_addr(rt, layer, stage_id, layer->tensor_ids[0], &addr_b) != PRT_OK) ||
       (!use_host_addrs &&
        stage_tensor_exec_addr(rt, layer, stage_id, layer->tensor_ids[0], &addr_b) != PRT_OK)) ||
      ((use_host_addrs &&
        stage_tensor_host_addr(rt, layer, stage_id, layer->tensor_ids[1], &addr_w) != PRT_OK) ||
       (!use_host_addrs &&
        stage_tensor_exec_addr(rt, layer, stage_id, layer->tensor_ids[1], &addr_w) != PRT_OK)) ||
      ((use_host_addrs &&
        stage_tensor_host_addr(rt, layer, stage_id, layer->tensor_ids[2], &addr_in) != PRT_OK) ||
       (!use_host_addrs &&
        stage_tensor_exec_addr(rt, layer, stage_id, layer->tensor_ids[2], &addr_in) != PRT_OK)) ||
      ((use_host_addrs &&
        stage_tensor_host_addr(rt, layer, stage_id, layer->tensor_ids[3], &addr_out) != PRT_OK) ||
       (!use_host_addrs &&
        stage_tensor_exec_addr(rt, layer, stage_id, layer->tensor_ids[3], &addr_out) != PRT_OK))) {
    return PRT_ERR_INVAL;
  }

  memset(out, 0, sizeof(*out));
  out->batch_size = (int)N;
  out->in_row_dim = (int)((OH - 1U) * sH + KH);
  out->in_col_dim = (int)((OW - 1U) * sW + KW);
  out->in_channels = (int)IC;
  out->out_channels = (int)OC;
  out->out_row_dim = (int)OH;
  out->out_col_dim = (int)OW;
  out->stride = (int)sH;
  out->input_dilation = 1;
  out->kernel_dilation = 1;
  out->padding = 0;
  out->kernel_dim = (int)KH;
  out->input = (const void *)(uintptr_t)addr_in;
  out->weights = (const void *)(uintptr_t)addr_w;
  out->bias = (const void *)(uintptr_t)addr_b;
  out->output = (void *)(uintptr_t)addr_out;
  out->act = 1;
  out->output_scale = 1.0f;
  out->pool_size = 1;
  out->pool_stride = 0;
  out->pool_padding = 0;
  out->tiled_type = 1;
  return PRT_OK;
}

static int build_stage_resadd_desc(prt_runtime_t *rt, uint32_t stage_id, prt_gemmini_resadd_desc_t *out) {
  const prt_model_layer_t *layer;
  uint64_t addr_a;
  uint64_t addr_b;
  uint64_t addr_c;
  uint32_t N = 1;
  uint32_t C = 0;
  uint32_t HW = 0;
  size_t I = 0;
  size_t J = 0;
  size_t elems_out = 0;

  if (!rt || !out) return PRT_ERR_INVAL;
  if (stage_id >= rt->stage_thread_count) return PRT_ERR_NOT_READY;
  layer = find_model_layer(&rt->model, rt->stage_layer_ids[stage_id]);
  if (!layer) return PRT_ERR_NOT_READY;
  if (strcmp(layer->type, "resadd") != 0) return PRT_ERR_NOT_READY;
  if (layer->address_count < 3) return PRT_ERR_NOT_READY;
  if (!rt->model_blob || rt->model_blob_size == 0) return PRT_ERR_NOT_READY;

  if (stage_prepare_exec_views(rt, stage_id, layer) != PRT_OK) {
    return PRT_ERR_STATE;
  }
  if (stage_tensor_exec_addr(rt, layer, stage_id, layer->tensor_ids[0], &addr_a) != PRT_OK ||
      stage_tensor_exec_addr(rt, layer, stage_id, layer->tensor_ids[1], &addr_b) != PRT_OK ||
      stage_tensor_exec_addr(rt, layer, stage_id, layer->tensor_ids[2], &addr_c) != PRT_OK) {
    return PRT_ERR_INVAL;
  }

  if (layer->param_len > 0 && layer->param[0] > 0) N = layer->param[0];
  if (layer->param_len > 1 && layer->param[1] > 0) C = layer->param[1];
  if (layer->param_len > 2 && layer->param[2] > 0) HW = layer->param[2];
  if (layer->tensor_size_count > 2 && layer->tensor_size[2] > 0) elems_out = (size_t)layer->tensor_size[2];

  if (C > 0) J = (size_t)C;
  if (HW > 0) I = (size_t)N * (size_t)HW;

  if (J == 0 && elems_out > 0) J = elems_out;
  if (J == 0) J = 1;
  if (I == 0 && elems_out > 0) I = elems_out / J;
  if (I == 0) I = 1;

  if (elems_out > 0 && I * J > elems_out) {
    I = elems_out / J;
    if (I == 0) {
      I = 1;
      J = elems_out;
      if (J == 0) J = 1;
    }
  }

  memset(out, 0, sizeof(*out));
  out->I = I;
  out->J = J;
  out->A_scale = 1.0f;
  out->B_scale = 1.0f;
  out->C_scale = 1.0f;
  out->stride = J;
  out->A = (const void *)(uintptr_t)addr_a;
  out->B = (const void *)(uintptr_t)addr_b;
  out->C = (void *)(uintptr_t)addr_c;
  out->relu = 0;
  out->tiled_type = 1;
  return PRT_OK;
}

#if !defined(__riscv)
static void host_stage_assign_runtime_state(prt_runtime_t *rt, const prt_stage_map_t *stage, uint32_t stage_id) {
  if (!rt || !stage || stage_id >= PRT_MAX_STAGES) return;
  rt->stage_layer_ids[stage_id] = stage->layer_id;
  rt->stage_acc_ids[stage_id] = runtime_stage_gemmini_mgr(rt, stage_id);
  rt->stage_dma_ids[stage_id] = runtime_stage_dma_mgr(rt, stage_id);
  rt->stage_tile_counts[stage_id] = stage->acc_util > 0 ? stage->acc_util : 1U;
  rt->stage_split_kinds[stage_id] = stage->split_kind;
  memset(rt->stage_mgr_ids[stage_id], 0, sizeof(rt->stage_mgr_ids[stage_id]));
  if (rt->active_action &&
      stage_id < rt->active_action->acc_source.stage_count &&
      rt->active_action->acc_source.stage_assign &&
      rt->active_action->acc_source.stage_assign[stage_id].gemmini_mgr_ids) {
    uint32_t n = rt->active_action->acc_source.stage_assign[stage_id].acc_util;
    if (n > PRT_MAX_CORES) n = PRT_MAX_CORES;
    for (uint32_t i = 0; i < n; ++i) {
      rt->stage_mgr_ids[stage_id][i] =
        rt->active_action->acc_source.stage_assign[stage_id].gemmini_mgr_ids[i];
    }
  } else if (stage->num_physical_acc_ids > 0) {
    uint32_t n = stage->num_physical_acc_ids > PRT_MAX_CORES ? PRT_MAX_CORES : stage->num_physical_acc_ids;
    for (uint32_t i = 0; i < n; ++i) {
      rt->stage_mgr_ids[stage_id][i] = rt->cfg.gemmini_mgr_base_id + stage->physical_acc_ids[i];
    }
  } else {
    rt->stage_mgr_ids[stage_id][0] = rt->stage_acc_ids[stage_id];
  }
}

static int run_segment_host_serial(prt_runtime_t *rt, const prt_segment_desc_t *seg, uint32_t target_subbatch,
                                   uint64_t timeout_ns) {
  uint32_t segment_idx = 0;
  if (!rt || !seg) return PRT_ERR_INVAL;
  if (seg->num_stages > PRT_MAX_STAGES) return PRT_ERR_NOT_IMPL;
  if (rt->active_action) segment_idx = rt->active_action->segment_idx;

  rt->stage_thread_count = seg->num_stages;
  for (uint32_t stage_id = 0; stage_id < seg->num_stages; ++stage_id) {
    host_stage_assign_runtime_state(rt, &seg->stages[stage_id], stage_id);
  }
  PRT_PROGRESS_LOG("segment=%u host-serial begin stages=%u target_subbatch=%u timeout_ms=%u",
                   segment_idx, seg->num_stages, target_subbatch, rt->cfg.watchdog_timeout_ms);

  for (uint32_t sb = 0; sb < target_subbatch; ++sb) {
    for (uint32_t stage_id = 0; stage_id < seg->num_stages; ++stage_id) {
      prt_conv_task_t task;
      prt_gemmini_conv_desc_t conv_desc;
      prt_gemmini_resadd_desc_t resadd_desc;
      uint64_t gemm_begin_ns;
      uint64_t gemm_end_ns;
      int rc;

      memset(&task, 0, sizeof(task));
      memset(&conv_desc, 0, sizeof(conv_desc));
      memset(&resadd_desc, 0, sizeof(resadd_desc));

      task.stage_id = stage_id;
      task.acc_id = rt->stage_acc_ids[stage_id];
      task.tile_count = rt->stage_tile_counts[stage_id] > 0 ? rt->stage_tile_counts[stage_id] : 1U;
      task.split_kind = (prt_layer_split_t)rt->stage_split_kinds[stage_id];
      task.num_managers = task.tile_count;
      if (task.num_managers == 0) task.num_managers = 1;
      if (task.num_managers > PRT_MAX_CORES) task.num_managers = PRT_MAX_CORES;
      for (uint32_t i = 0; i < task.num_managers; ++i) {
        task.manager_ids[i] = rt->stage_mgr_ids[stage_id][i];
      }
      if (task.manager_ids[0] == 0) task.manager_ids[0] = task.acc_id;

      if (build_stage_conv_desc(rt, stage_id, &conv_desc) == PRT_OK) {
        task.op_kind = PRT_STAGE_OP_CONV;
        task.opaque_task = &conv_desc;
      } else if (build_stage_resadd_desc(rt, stage_id, &resadd_desc) == PRT_OK) {
        task.op_kind = PRT_STAGE_OP_RESADD;
        task.opaque_task = &resadd_desc;
      } else {
        task.op_kind = PRT_STAGE_OP_NONE;
        task.opaque_task = NULL;
      }

      PRT_PROGRESS_LOG("segment=%u host-serial subbatch=%u stage=%u begin op=%u acc=%u dma=%u tiles=%u",
                       segment_idx, sb, stage_id, (uint32_t)task.op_kind,
                       rt->stage_acc_ids[stage_id], rt->stage_dma_ids[stage_id], task.tile_count);

      gemm_begin_ns = prt_now_ns();
      prt_trace_on_gemm_issue(rt);
      prt_trace_log_event(rt, stage_id, PRT_TRACE_EVT_GEMM_ISSUE, (uint32_t)task.op_kind, 0);
      rc = prt_gemm_conv_run(rt, &task, timeout_ns);
      gemm_end_ns = prt_now_ns();
      if (gemm_end_ns > gemm_begin_ns) {
        prt_trace_on_gemm_busy(rt, gemm_end_ns - gemm_begin_ns);
      }
      if (rc != PRT_OK) return rc;

      prt_trace_on_gemm_fence(rt);
      prt_trace_log_event(rt, stage_id, PRT_TRACE_EVT_GEMM_FENCE_BEGIN, 0, 0);
      rc = prt_gemm_fence(rt, &task, timeout_ns);
      if (rc != PRT_OK) return rc;
      prt_trace_log_event(rt, stage_id, PRT_TRACE_EVT_GEMM_FENCE_END, 0, 0);

      rc = sync_stage_export_aliases(rt, stage_id);
      if (rc != PRT_OK) return rc;
      PRT_PROGRESS_LOG("segment=%u host-serial subbatch=%u stage=%u done",
                       segment_idx, sb, stage_id);
    }
  }

  PRT_PROGRESS_LOG("segment=%u host-serial done target_subbatch=%u", segment_idx, target_subbatch);
  return PRT_OK;
}
#endif

static void *stage_worker_main(void *arg) {
  prt_stage_thread_ctx_t *ctx = (prt_stage_thread_ctx_t *)arg;
  prt_runtime_t *rt = ctx->rt;
  uint64_t timeout_ns = (uint64_t)rt->cfg.watchdog_timeout_ms * 1000000ULL;
  uint64_t wait_timeout_ns = timeout_ns;
  prt_pipebuf_t *entry_bufs[PRT_MAX_TENSORS];
  prt_pipebuf_t *export_bufs[PRT_MAX_TENSORS];
  prt_isolate_pair_t *iso_pairs[PRT_MAX_TENSORS];
  prt_shared_pair_t *shared_pairs[PRT_MAX_TENSORS];
  uint32_t entry_count = 0;
  uint32_t export_count = 0;
  uint32_t iso_pair_count = 0;
  uint32_t shared_pair_count = 0;
  uint64_t last_wait_log_ms = 0;

#if defined(__linux__)
  stage_bind_current_thread(rt, ctx);
#endif

  if (wait_timeout_ns == 0 || wait_timeout_ns > 10000000ULL) {
    wait_timeout_ns = 10000000ULL;
  }

  for (uint32_t i = 0; i < rt->pipebuf_count; ++i) {
    prt_pipebuf_t *b = &rt->pipebufs[i];
    if (b->stage_idx != ctx->stage_id) continue;
    if (b->is_entry && is_entry_kind(b->kind)) entry_bufs[entry_count++] = b;
    if (!b->is_entry && is_export_kind(b->kind)) export_bufs[export_count++] = b;
  }
  for (uint32_t i = 0; i < rt->isolate_pair_count; ++i) {
    if (rt->isolate_pairs[i].pre_export &&
        rt->isolate_pairs[i].pre_export->stage_idx == ctx->stage_id) {
      iso_pairs[iso_pair_count++] = &rt->isolate_pairs[i];
    }
  }
  for (uint32_t i = 0; i < rt->shared_pair_count; ++i) {
    if (rt->shared_pairs[i].pre_export &&
        rt->shared_pairs[i].pre_export->stage_idx == ctx->stage_id) {
      shared_pairs[shared_pair_count++] = &rt->shared_pairs[i];
    }
  }
  PRT_PROGRESS_LOG("worker stage=%u ready entries=%u exports=%u isolate_pairs=%u shared_pairs=%u acc=%u dma=%u tiles=%u",
                   ctx->stage_id, entry_count, export_count, iso_pair_count, shared_pair_count,
                   (ctx->stage_id < rt->stage_thread_count) ? rt->stage_acc_ids[ctx->stage_id] : 0U,
                   (ctx->stage_id < rt->stage_thread_count) ? rt->stage_dma_ids[ctx->stage_id] : 0U,
                   (ctx->stage_id < rt->stage_thread_count) ? rt->stage_tile_counts[ctx->stage_id] : 0U);

  while (!ctx->stop && !rt->stop_requested && !rt->fatal_error) {
    int rc;
    int retry = 0;
    uint32_t progress_sbatch = progress_stage_sbatch(entry_bufs, entry_count, export_bufs, export_count);

    for (uint32_t i = 0; i < entry_count; ++i) {
      prt_pipebuf_t *b = entry_bufs[i];
      uint32_t idx = b->in_use_idx;
      uint64_t process_wait_begin_ms = monotonic_ms();
      switch (b->kind) {
        case PRT_BUF_C1_ENTRY_DRAM_OR_DEPEN:
          rc = prt_process_c1(rt, b, idx, wait_timeout_ns);
          if (rc == PRT_ERR_TIMEOUT) {
            progress_log_worker_wait(ctx->stage_id, progress_sbatch, "entry-c1-process", b, idx,
                                     process_wait_begin_ms, &last_wait_log_ms, rc);
            if (rt->stop_requested) break;
            retry = 1;
            break;
          }
          if (rc != PRT_OK && rc != PRT_ERR_BUSY) {
            if (rt->stop_requested) break;
            rt->fatal_error = rc;
          }
          break;
        case PRT_BUF_C5_ENTRY_ISOLATE_WITH_RING:
          rc = prt_process_c5(rt, b, idx, wait_timeout_ns);
          if (rc == PRT_ERR_TIMEOUT) {
            progress_log_worker_wait(ctx->stage_id, progress_sbatch, "entry-c5-process", b, idx,
                                     process_wait_begin_ms, &last_wait_log_ms, rc);
            if (rt->stop_requested) break;
            retry = 1;
            break;
          }
          if (rc != PRT_OK) rt->fatal_error = rc;
          break;
        case PRT_BUF_C7_ENTRY_ALL_RING:
          rc = prt_ring_wait_ready(b->ring, b->subbatch_offset, wait_timeout_ns);
          if (rc == PRT_ERR_TIMEOUT) {
            progress_log_worker_wait(ctx->stage_id, progress_sbatch, "entry-c7-ring-ready", b, idx,
                                     process_wait_begin_ms, &last_wait_log_ms, rc);
            if (rt->stop_requested) break;
            retry = 1;
            break;
          }
          if (rc != PRT_OK) rt->fatal_error = rc;
          if (!rt->fatal_error) {
            rc = prt_process_c7(rt, b);
            if (rc != PRT_OK) rt->fatal_error = rc;
          }
          break;
        default:
          break;
      }
      if (rt->fatal_error || rt->stop_requested) break;

      uint64_t full_wait_begin_ms = monotonic_ms();
      rc = prt_pipebuf_wait_full(b, idx, wait_timeout_ns);
      if (rc != PRT_OK) {
        if (rc == PRT_ERR_TIMEOUT) {
          progress_log_worker_wait(ctx->stage_id, progress_sbatch, "entry-full", b, idx,
                                   full_wait_begin_ms, &last_wait_log_ms, rc);
          if (rt->stop_requested) break;
          retry = 1;
          break;
        }
        rt->fatal_error = rc;
        break;
      }
    }
    if (rt->fatal_error || rt->stop_requested) break;
    if (retry) continue;

    rc = stage_wait_exports_ready(rt, ctx->stage_id, progress_sbatch, export_bufs, export_count,
                                  shared_pairs, shared_pair_count, wait_timeout_ns,
                                  &last_wait_log_ms);
    if (rc != PRT_OK) {
      if (rc == PRT_ERR_TIMEOUT) {
        if (rt->stop_requested) break;
        continue;
      }
      rt->fatal_error = rc;
      break;
    }

    {
      prt_conv_task_t task;
      prt_gemmini_conv_desc_t conv_desc;
      prt_gemmini_resadd_desc_t resadd_desc;
      int overlap_mode = (rt->cfg.gemmini_mode == PRT_GEMMINI_MODE_ASYNC_EXPERIMENTAL);
      uint64_t gemm_begin_ns;
      memset(&task, 0, sizeof(task));
      memset(&conv_desc, 0, sizeof(conv_desc));
      memset(&resadd_desc, 0, sizeof(resadd_desc));
      task.stage_id = ctx->stage_id;
      task.acc_id = (ctx->stage_id < rt->stage_thread_count) ? rt->stage_acc_ids[ctx->stage_id] : 0;
      task.tile_count = (ctx->stage_id < rt->stage_thread_count) ? rt->stage_tile_counts[ctx->stage_id] : 1;
      task.split_kind = (ctx->stage_id < rt->stage_thread_count) ?
                        (prt_layer_split_t)rt->stage_split_kinds[ctx->stage_id] :
                        PRT_LAYER_SPLIT_UNSPEC;
      task.num_managers = task.tile_count > PRT_MAX_CORES ? PRT_MAX_CORES : task.tile_count;
      if (task.num_managers == 0) task.num_managers = 1;
      if (ctx->stage_id < rt->stage_thread_count) {
        for (uint32_t mi = 0; mi < task.num_managers; ++mi) {
          task.manager_ids[mi] = rt->stage_mgr_ids[ctx->stage_id][mi];
        }
        if (task.manager_ids[0] == 0) task.manager_ids[0] = rt->stage_acc_ids[ctx->stage_id];
      } else {
        task.manager_ids[0] = task.acc_id;
      }
      task.opaque_task = NULL;
      if (build_stage_conv_desc(rt, ctx->stage_id, &conv_desc) == PRT_OK) {
        task.op_kind = PRT_STAGE_OP_CONV;
        task.opaque_task = (void *)&conv_desc;
      } else if (build_stage_resadd_desc(rt, ctx->stage_id, &resadd_desc) == PRT_OK) {
        task.op_kind = PRT_STAGE_OP_RESADD;
        task.opaque_task = (void *)&resadd_desc;
      } else {
        task.op_kind = PRT_STAGE_OP_NONE;
      }
      PRT_PROGRESS_LOG("worker stage=%u subbatch=%u begin op=%u acc=%u dma=%u tiles=%u",
                       ctx->stage_id, progress_sbatch, (uint32_t)task.op_kind,
                       (ctx->stage_id < rt->stage_thread_count) ? rt->stage_acc_ids[ctx->stage_id] : 0U,
                       (ctx->stage_id < rt->stage_thread_count) ? rt->stage_dma_ids[ctx->stage_id] : 0U,
                       task.tile_count);
      PRT_PROGRESS_HOT_ERR_LOG("worker stage=%u subbatch=%u dispatch-enter op=%u split=%u acc=%u dma=%u tiles=%u",
                               ctx->stage_id, progress_sbatch, (uint32_t)task.op_kind,
                               (uint32_t)task.split_kind,
                               (ctx->stage_id < rt->stage_thread_count) ? rt->stage_acc_ids[ctx->stage_id] : 0U,
                               (ctx->stage_id < rt->stage_thread_count) ? rt->stage_dma_ids[ctx->stage_id] : 0U,
                               task.tile_count);
      gemm_begin_ns = prt_now_ns();
      prt_trace_on_gemm_issue(rt);
      PRT_PROGRESS_HOT_ERR_LOG("worker stage=%u subbatch=%u trace-issue-done", ctx->stage_id, progress_sbatch);
      prt_trace_log_event(rt, ctx->stage_id, PRT_TRACE_EVT_GEMM_ISSUE, (uint32_t)task.op_kind, 0);
      PRT_PROGRESS_HOT_ERR_LOG("worker stage=%u subbatch=%u gemm-run-enter", ctx->stage_id, progress_sbatch);
      rc = prt_gemm_conv_run(rt, &task, timeout_ns);
      PRT_PROGRESS_HOT_ERR_LOG("worker stage=%u subbatch=%u gemm-run-exit rc=%d", ctx->stage_id, progress_sbatch, rc);
      if (rc != PRT_OK) {
        uint64_t gemm_end_ns = prt_now_ns();
        if (gemm_end_ns > gemm_begin_ns) prt_trace_on_gemm_busy(rt, gemm_end_ns - gemm_begin_ns);
        rt->fatal_error = rc;
        break;
      }

      if (overlap_mode) {
        rc = stage_overlap_prefetch_entries(rt, entry_bufs, entry_count, timeout_ns);
        if (rc != PRT_OK) {
          uint64_t gemm_end_ns = prt_now_ns();
          if (gemm_end_ns > gemm_begin_ns) prt_trace_on_gemm_busy(rt, gemm_end_ns - gemm_begin_ns);
          rt->fatal_error = rc;
          break;
        }
        prt_trace_on_gemm_fence(rt);
        prt_trace_log_event(rt, ctx->stage_id, PRT_TRACE_EVT_GEMM_FENCE_BEGIN, 0, 0);
        rc = prt_gemm_fence(rt, &task, timeout_ns);
        {
          uint64_t gemm_end_ns = prt_now_ns();
          if (gemm_end_ns > gemm_begin_ns) prt_trace_on_gemm_busy(rt, gemm_end_ns - gemm_begin_ns);
        }
        if (rc != PRT_OK) {
          rt->fatal_error = rc;
          break;
        }
        prt_trace_log_event(rt, ctx->stage_id, PRT_TRACE_EVT_GEMM_FENCE_END, 0, 0);
      } else {
        uint64_t gemm_end_ns = prt_now_ns();
        if (gemm_end_ns > gemm_begin_ns) prt_trace_on_gemm_busy(rt, gemm_end_ns - gemm_begin_ns);
      }
      rc = sync_stage_export_aliases(rt, ctx->stage_id);
      if (rc != PRT_OK) {
        rt->fatal_error = rc;
        break;
      }
      PRT_PROGRESS_LOG("worker stage=%u subbatch=%u compute-done", ctx->stage_id, progress_sbatch);
      last_wait_log_ms = 0;
    }
    if (rt->fatal_error || rt->stop_requested) break;

    for (uint32_t i = 0; i < entry_count; ++i) {
      prt_pipebuf_t *b = entry_bufs[i];
      uint32_t idx = b->in_use_idx;
      pthread_mutex_lock(&b->lock);
      b->full[idx] = 0;
      b->state_epoch += 1;
      pthread_cond_broadcast(&b->cv);
      pthread_mutex_unlock(&b->lock);

      if (b->kind == PRT_BUF_C7_ENTRY_ALL_RING) {
        rc = prt_process_c7(rt, b);
        if (rc != PRT_OK) {
          rt->fatal_error = rc;
          break;
        }
      }

      if (b->kind == PRT_BUF_C1_ENTRY_DRAM_OR_DEPEN ||
          b->kind == PRT_BUF_C5_ENTRY_ISOLATE_WITH_RING) {
        rotate_buf_if_needed(b);
      }
    }
    if (rt->fatal_error || rt->stop_requested) break;

    for (uint32_t i = 0; i < export_count; ++i) {
      prt_pipebuf_t *b = export_bufs[i];
      uint32_t idx = b->in_use_idx;
      pthread_mutex_lock(&b->lock);
      b->full[idx] = 1;
      if (b->kind == PRT_BUF_C3_ISOLATE_NO_RING_PAIR && b->fanout_total > 0) {
        b->fanout_pending = b->fanout_total;
      }
      b->state_epoch += 1;
      pthread_cond_broadcast(&b->cv);
      pthread_mutex_unlock(&b->lock);

      switch (b->kind) {
        case PRT_BUF_C2_EXPORT_DRAM_OR_DEPEN:
          rc = prt_process_c2(rt, b, idx, wait_timeout_ns);
          if (rc == PRT_OK) rotate_buf_if_needed(b);
          break;
        case PRT_BUF_C6_EXPORT_ISOLATE_WITH_RING:
          rc = prt_process_c6(rt, b, idx, wait_timeout_ns);
          if (rc == PRT_OK) rotate_buf_if_needed(b);
          break;
        case PRT_BUF_C8_EXPORT_ALL_RING:
          rc = prt_process_c8(rt, b);
          break;
        case PRT_BUF_C3_ISOLATE_NO_RING_PAIR:
          rc = PRT_OK;
          for (uint32_t p = 0; p < iso_pair_count; ++p) {
            if (iso_pairs[p]->pre_export == b) {
              rc = prt_process_c3(rt, iso_pairs[p], wait_timeout_ns);
              if (rc != PRT_OK) break;
            }
          }
          break;
        case PRT_BUF_C4_SHARED_NO_RING_PAIR:
          rc = PRT_OK;
          for (uint32_t p = 0; p < shared_pair_count; ++p) {
            if (shared_pairs[p]->pre_export == b) {
              rc = prt_process_c4(rt, shared_pairs[p]);
              if (rc != PRT_OK) break;
            }
          }
          break;
        default:
          rc = PRT_OK;
          break;
      }

      if (rc != PRT_OK && rc != PRT_ERR_EMPTY && rc != PRT_ERR_BUSY) {
        if (rc == PRT_ERR_TIMEOUT) {
          if (rt->stop_requested) break;
          retry = 1;
          break;
        }
        rt->fatal_error = rc;
        break;
      }
    }
    if (!retry && !rt->fatal_error && !rt->stop_requested) {
      PRT_PROGRESS_LOG("worker stage=%u subbatch=%u done", ctx->stage_id, progress_sbatch);
      last_wait_log_ms = 0;
    }
    if (retry) continue;
  }

  PRT_PROGRESS_LOG("worker stage=%u exit stop=%d fatal=%d requested=%d",
                   ctx->stage_id, ctx->stop, rt->fatal_error, rt->stop_requested);
  return NULL;
}

int prt_runtime_init(const prt_runtime_cfg_t *cfg, prt_runtime_t *rt) {
  if (!cfg || !rt) return PRT_ERR_INVAL;
  memset(rt, 0, sizeof(*rt));
  rt->cfg = *cfg;

  if (rt->cfg.num_cores == 0) rt->cfg.num_cores = PRT_MAX_CORES;
  if (rt->cfg.num_cores > PRT_MAX_CORES) rt->cfg.num_cores = PRT_MAX_CORES;
  if (rt->cfg.num_gemmini_mgrs == 0) rt->cfg.num_gemmini_mgrs = rt->cfg.num_cores;
  if (rt->cfg.num_gemmini_mgrs > PRT_MAX_CORES) rt->cfg.num_gemmini_mgrs = PRT_MAX_CORES;
  if (rt->cfg.num_dma_mgrs == 0) rt->cfg.num_dma_mgrs = rt->cfg.num_gemmini_mgrs;
  if (rt->cfg.num_dma_mgrs > PRT_MAX_CORES) rt->cfg.num_dma_mgrs = PRT_MAX_CORES;
  if (rt->cfg.num_cores < rt->cfg.num_gemmini_mgrs) {
    rt->cfg.num_cores = rt->cfg.num_gemmini_mgrs;
  }
  if (rt->cfg.page_size_bytes == 0) rt->cfg.page_size_bytes = PRT_PAGE_SIZE_BYTES;
  if (rt->cfg.pages_per_acc == 0) rt->cfg.pages_per_acc = 256;
  if (rt->cfg.spm_page_shift == 0) {
    uint32_t p = rt->cfg.page_size_bytes;
    uint32_t shift = 0;
    while ((p & 1U) == 0U && p > 1U) {
      p >>= 1;
      shift += 1U;
    }
    rt->cfg.spm_page_shift = (p == 1U) ? shift : 10U;
  }
  if (rt->cfg.spm_xlate_enable > 1U) rt->cfg.spm_xlate_enable = 1U;
  if (rt->cfg.spm_xlate_range_size == 0) {
    uint64_t max_pages = (uint64_t)rt->cfg.num_cores * (uint64_t)rt->cfg.pages_per_acc;
    rt->cfg.spm_xlate_range_size = max_pages * (uint64_t)rt->cfg.page_size_bytes * 8ULL;
  }
  if (rt->cfg.watchdog_timeout_ms == 0) rt->cfg.watchdog_timeout_ms = 5000;
  if (rt->cfg.sync_mode != PRT_SYNC_MODE_BLOCKING_DEBUG) {
    rt->cfg.sync_mode = PRT_SYNC_MODE_ASYNC;
  }
  // Page-granular DMA translation is currently retired synchronously; force blocking debug
  // until token-chained async paging is implemented.
  if (rt->cfg.sync_mode == PRT_SYNC_MODE_ASYNC && rt->cfg.spm_xlate_enable) {
    rt->cfg.sync_mode = PRT_SYNC_MODE_BLOCKING_DEBUG;
  }
  if (rt->cfg.sync_mode == PRT_SYNC_MODE_BLOCKING_DEBUG) {
    rt->cfg.dma_backend = PRT_DMA_BACKEND_BLOCKING_FENCE;
    rt->cfg.gemmini_mode = PRT_GEMMINI_MODE_BLOCKING_FENCE;
  }

  PRT_PROGRESS_LOG("init begin backend=%u cores=%u gemmini=%u dma=%u pages_per_acc=%u page_bytes=%u spm_xlate=%u range_base=0x%llx range_size=%llu",
                   (uint32_t)rt->cfg.backend,
                   rt->cfg.num_cores,
                   rt->cfg.num_gemmini_mgrs,
                   rt->cfg.num_dma_mgrs,
                   rt->cfg.pages_per_acc,
                   rt->cfg.page_size_bytes,
                   rt->cfg.spm_xlate_enable,
                   (unsigned long long)rt->cfg.spm_xlate_range_base,
                   (unsigned long long)rt->cfg.spm_xlate_range_size);

  pthread_mutex_init(&rt->state_lock, NULL);
  pthread_cond_init(&rt->state_cv, NULL);

  PRT_PROGRESS_LOG("init page-table begin");
  int rc = prt_page_table_init(rt);
  if (rc != PRT_OK) {
    fprintf(stderr, "runtime_init: page_table_init failed: %s (%d)\n", prt_err_str(rc), rc);
    return rc;
  }
  PRT_PROGRESS_LOG("init page-table end ptbr=0x%llx ptes=%u",
                   (unsigned long long)prt_spm_ptbr_pa(rt),
                   prt_spm_pte_count(rt));

  PRT_PROGRESS_LOG("init dma-backend begin backend=%u", (uint32_t)rt->cfg.dma_backend);
  rc = prt_dma_backend_init(rt);
  if (rc != PRT_OK) {
    fprintf(stderr, "runtime_init: dma_backend_init failed: %s (%d)\n", prt_err_str(rc), rc);
    return rc;
  }
  PRT_PROGRESS_LOG("init dma-backend end");

  PRT_PROGRESS_LOG("init gemmini-backend begin mode=%u", (uint32_t)rt->cfg.gemmini_mode);
  rc = prt_gemmini_backend_init(rt);
  if (rc != PRT_OK) {
    fprintf(stderr, "runtime_init: gemmini_backend_init failed: %s (%d)\n", prt_err_str(rc), rc);
    return rc;
  }
  PRT_PROGRESS_LOG("init gemmini-backend end");

  PRT_PROGRESS_LOG("init spm-xlate begin");
  rc = runtime_bootstrap_spm_xlate(rt);
  if (rc != PRT_OK) {
    fprintf(stderr, "runtime_init: bootstrap_spm_xlate failed: %s (%d)\n", prt_err_str(rc), rc);
    return rc;
  }
  PRT_PROGRESS_LOG("init spm-xlate end");

  if (rt->cfg.trace_path && rt->cfg.trace_path[0] != '\0') {
    rt->trace_event_cap = PRT_TRACE_EVENT_CAP_DEFAULT;
    rt->trace_events = (prt_trace_event_t *)calloc(rt->trace_event_cap, sizeof(prt_trace_event_t));
    if (!rt->trace_events) return PRT_ERR_NOMEM;
  }
  PRT_PROGRESS_LOG("init trace-calibrate begin");
  rc = prt_trace_calibrate_cycle(rt);
  if (rc != PRT_OK) {
    fprintf(stderr, "runtime_init: trace_calibrate failed: %s (%d)\n", prt_err_str(rc), rc);
    return rc;
  }
  PRT_PROGRESS_LOG("init trace-calibrate end");
  prt_trace_reset(rt);
  PRT_PROGRESS_LOG("init done");
  return PRT_OK;
}

static void runtime_release_topology(prt_runtime_t *rt) {
  if (!rt) return;

  if (rt->pipebufs && rt->cfg.dma_backend == PRT_DMA_BACKEND_POLL_PROGRESS_THREAD) {
    uint64_t timeout_ns = (uint64_t)rt->cfg.watchdog_timeout_ms * 1000000ULL;
    if (timeout_ns == 0) timeout_ns = 5000000000ULL;
    for (uint32_t i = 0; i < rt->pipebuf_count; ++i) {
      prt_pipebuf_t *b = &rt->pipebufs[i];
      if (b->kind != PRT_BUF_C2_EXPORT_DRAM_OR_DEPEN &&
          b->kind != PRT_BUF_C6_EXPORT_ISOLATE_WITH_RING) {
        continue;
      }
      if (!b->dma_token_live[0] && !b->dma_token_live[1]) continue;
      (void)prt_progress_export_dma(rt, b, timeout_ns, 0);
    }
  }

  if (rt->topo_alloc_keys) {
    for (uint32_t i = 0; i < rt->topo_alloc_count; ++i) {
      (void)prt_release_tensor_pages(rt, rt->topo_alloc_keys[i]);
    }
    free(rt->topo_alloc_keys);
  }
  rt->topo_alloc_keys = NULL;
  rt->topo_alloc_count = 0;
  rt->topo_alloc_cap = 0;
  free_runtime_weight_bindings(rt);

  for (uint32_t i = 0; i < PRT_MAX_STAGES; ++i) {
    memset(rt->stage_fixed_lazy_loaded[i], 0, sizeof(rt->stage_fixed_lazy_loaded[i]));
    free(rt->stage_spm_shadow[i]);
    rt->stage_spm_shadow[i] = NULL;
    rt->stage_spm_shadow_bytes[i] = 0;
    rt->stage_spm_rebase_vpage[i] = 0;
    rt->stage_spm_window_pages[i] = 0;
  }
  if (rt->spm_pte && rt->spm_pte_cap > 0) {
    memset(rt->spm_pte, 0, sizeof(uint64_t) * rt->spm_pte_cap);
  }
  rt->spm_next_vpage = 0;

  if (rt->pipebufs) {
    for (uint32_t i = 0; i < rt->pipebuf_count; ++i) pipebuf_destroy(&rt->pipebufs[i]);
    free(rt->pipebufs);
  }
  rt->pipebufs = NULL;
  rt->pipebuf_count = 0;

  if (rt->ringbufs) {
    for (uint32_t i = 0; i < rt->ringbuf_count; ++i) ringbuf_destroy(&rt->ringbufs[i]);
    free(rt->ringbufs);
  }
  rt->ringbufs = NULL;
  rt->ringbuf_count = 0;

  free(rt->isolate_pairs);
  free(rt->shared_pairs);
  rt->isolate_pairs = NULL;
  rt->shared_pairs = NULL;
  rt->isolate_pair_count = 0;
  rt->shared_pair_count = 0;
  rt->stage_thread_count = 0;
}

static int runtime_assert_page_allocator_idle(prt_runtime_t *rt, const char *where) {
  uint32_t total_pages;
  uint32_t used_pages = 0;
  if (!rt || !rt->page_used) return PRT_ERR_INVAL;

  total_pages = rt->cfg.num_cores * rt->cfg.pages_per_acc;
  pthread_mutex_lock(&rt->page_lock);
  for (uint32_t i = 0; i < total_pages; ++i) {
    if (rt->page_used[i]) used_pages += 1;
  }
  if (rt->tensor_alloc_count == 0 && used_pages == 0) {
    pthread_mutex_unlock(&rt->page_lock);
    return PRT_OK;
  }
  pthread_mutex_unlock(&rt->page_lock);

  fprintf(stderr,
          "page allocator leak detected at %s: tensor_alloc_count=%u used_pages=%u\n",
          where ? where : "unknown", rt->tensor_alloc_count, used_pages);
  return PRT_ERR_STATE;
}

int prt_runtime_run(prt_runtime_t *rt, const prt_run_args_t *args) {
  uint64_t start_ms;
  uint64_t last_watchdog_log_ms = 0;
  uint64_t init_step_start_ms = 0;
  uint32_t *model_input_ids = NULL;
  uint32_t model_input_count = 0;
  uint32_t *model_output_ids = NULL;
  uint32_t model_output_count = 0;
  prt_pipebuf_t **sink_bufs = NULL;
  uint32_t sink_count = 0;
  uint32_t sink_cap = 0;
  uint32_t subbatch_size = 1;
  uint32_t target_batch = 1;
  uint32_t target_subbatch = 1;
  uint8_t *input_blob = NULL;
  size_t input_blob_size = 0;
  uint8_t *golden_blob = NULL;
  size_t golden_blob_size = 0;
  uint32_t created_threads;
  const prt_segment_desc_t *all_segments = NULL;
  uint32_t all_num_segments = 0;
  uint32_t all_subbatch_size = 1;
  uint32_t seg_idx = 0;
  int pipeline_swizzled = 0;
  int run_rc = PRT_OK;
  prt_schedule_action_t *action = NULL;
#define PRT_GOTO_OUT_ON_ERR(tag) \
  do { \
    if (rc != PRT_OK) { \
      fprintf(stderr, "runtime_run: %s failed rc=%s(%d)\n", tag, prt_err_str(rc), rc); \
      goto out; \
    } \
  } while (0)
  if (!rt || !args) return PRT_ERR_INVAL;

  prt_trace_reset(rt);
  prt_trace_run_start(rt);
  rt->stop_requested = 0;
  rt->fatal_error = 0;
  PRT_PROGRESS_LOG("runtime begin backend=%u batch=%u watchdog_ms=%u model_yaml=%s pipeline_yaml=%s layer_mapping_yaml=%s",
                   (uint32_t)rt->cfg.backend, args->batch, rt->cfg.watchdog_timeout_ms,
                   args->model_yaml ? args->model_yaml : "(null)",
                   args->pipeline_yaml ? args->pipeline_yaml : "(null)",
                   args->layer_mapping_yaml ? args->layer_mapping_yaml : "(null)");

  init_step_start_ms = monotonic_ms();
  PRT_PROGRESS_LOG("init load-model-yaml begin path=%s",
                   args->model_yaml ? args->model_yaml : "(null)");
  int rc = prt_load_model_yaml(args->model_yaml, &rt->model);
  PRT_GOTO_OUT_ON_ERR("load_model_yaml");
  PRT_PROGRESS_LOG("init load-model-yaml end elapsed_ms=%llu layers=%u tensors=%u stages=%u addr=[0x%llx,0x%llx]",
                   (unsigned long long)(monotonic_ms() - init_step_start_ms),
                   rt->model.num_layers, rt->model.num_tensors, rt->model.num_stages,
                   (unsigned long long)rt->model.addr_base,
                   (unsigned long long)rt->model.addr_end);

  init_step_start_ms = monotonic_ms();
  PRT_PROGRESS_LOG("init collect-model-io begin layers=%u", rt->model.num_layers);
  rc = collect_model_input_output_ids(&rt->model, &model_input_ids, &model_input_count,
                                      &model_output_ids, &model_output_count);
  PRT_GOTO_OUT_ON_ERR("collect_model_io_ids");
  PRT_PROGRESS_LOG("init collect-model-io end elapsed_ms=%llu inputs=%u outputs=%u",
                   (unsigned long long)(monotonic_ms() - init_step_start_ms),
                   model_input_count, model_output_count);

  init_step_start_ms = monotonic_ms();
  PRT_PROGRESS_LOG("init load-pipeline-yaml begin path=%s",
                   args->pipeline_yaml ? args->pipeline_yaml : "(null)");
  rc = prt_load_pipeline_yaml(args->pipeline_yaml, &rt->pipeline);
  if (rc != PRT_OK) {
    prt_free_model_desc(&rt->model);
    goto out;
  }
  PRT_PROGRESS_LOG("init load-pipeline-yaml end elapsed_ms=%llu segments=%u subbatch_size=%u",
                   (unsigned long long)(monotonic_ms() - init_step_start_ms),
                   rt->pipeline.num_segments, rt->pipeline.subbatch_size);

  init_step_start_ms = monotonic_ms();
  PRT_PROGRESS_LOG("init validate-artifacts begin layer_mapping=%s segments=%u",
                   args->layer_mapping_yaml ? args->layer_mapping_yaml : "(null)",
                   rt->pipeline.num_segments);
  rc = prt_validate_gemmini_artifacts(args->model_yaml, args->layer_mapping_yaml, &rt->pipeline);
  if (rc != PRT_OK) {
    prt_free_model_desc(&rt->model);
    prt_free_pipeline_desc(&rt->pipeline);
    goto out;
  }
  PRT_PROGRESS_LOG("init validate-artifacts end elapsed_ms=%llu",
                   (unsigned long long)(monotonic_ms() - init_step_start_ms));

  if (args->model_bin) {
    init_step_start_ms = monotonic_ms();
    PRT_PROGRESS_LOG("init load-model-bin begin path=%s offset=%llu",
                     args->model_bin,
                     (unsigned long long)args->model_offset_bytes);
    rc = load_model_blob_file(args->model_bin, &rt->model_blob, &rt->model_blob_size);
    PRT_GOTO_OUT_ON_ERR("load_model_bin");
    if (args->model_offset_bytes >= rt->model_blob_size) {
      rc = PRT_ERR_INVAL;
      goto out;
    }
    rt->model_blob_offset = (size_t)args->model_offset_bytes;
    PRT_PROGRESS_LOG("init load-model-bin end elapsed_ms=%llu size=%zu offset=%zu",
                     (unsigned long long)(monotonic_ms() - init_step_start_ms),
                     rt->model_blob_size, rt->model_blob_offset);
  }

  if (args->input_path) {
    if (!rt->model_blob || rt->model_blob_size == 0) {
      rc = PRT_ERR_INVAL;
      goto out;
    }
    init_step_start_ms = monotonic_ms();
    PRT_PROGRESS_LOG("init load-input-blob begin path=%s", args->input_path);
    rc = load_model_blob_file(args->input_path, (void **)&input_blob, &input_blob_size);
    PRT_GOTO_OUT_ON_ERR("load_input_blob");
    PRT_PROGRESS_LOG("init load-input-blob end elapsed_ms=%llu size=%zu",
                     (unsigned long long)(monotonic_ms() - init_step_start_ms),
                     input_blob_size);
    init_step_start_ms = monotonic_ms();
    PRT_PROGRESS_LOG("init map-model-inputs begin inputs=%u blob_size=%zu",
                     model_input_count, input_blob_size);
    rc = map_model_inputs_from_blob(rt, model_input_ids, model_input_count, input_blob, input_blob_size);
    PRT_GOTO_OUT_ON_ERR("map_model_inputs");
    PRT_PROGRESS_LOG("init map-model-inputs end elapsed_ms=%llu",
                     (unsigned long long)(monotonic_ms() - init_step_start_ms));
  }

  all_segments = rt->pipeline.segments;
  all_num_segments = rt->pipeline.num_segments;
  all_subbatch_size = rt->pipeline.subbatch_size;
  pipeline_swizzled = 1;
  PRT_PROGRESS_LOG("init ready segments=%u pipeline_subbatch_size=%u batch=%u",
                   all_num_segments, all_subbatch_size, args->batch);

  for (seg_idx = 0; seg_idx < all_num_segments && run_rc == PRT_OK; ++seg_idx) {
    const prt_segment_desc_t *seg = &all_segments[seg_idx];
    uint32_t missing_model_sinks = 0;
    int is_last_segment = (seg_idx + 1U == all_num_segments);
    PRT_PROGRESS_LOG("segment=%u init begin stages=%u seg_subbatch_size=%u is_last=%u",
                     seg_idx, seg->num_stages, seg->subbatch_size, (uint32_t)is_last_segment);

    runtime_release_topology(rt);
    rc = runtime_assert_page_allocator_idle(rt, "segment_loop_start");
    PRT_GOTO_OUT_ON_ERR("assert_idle_segment_loop_start");
    rt->stop_requested = 0;
    rt->fatal_error = 0;
    created_threads = 0;
    sink_count = 0;

    rt->pipeline.num_segments = 1;
    rt->pipeline.segments = (prt_segment_desc_t *)seg;
    rt->pipeline.subbatch_size = seg->subbatch_size > 0 ? seg->subbatch_size : 1U;
    PRT_PROGRESS_LOG("segment=%u init swizzle done subbatch_size=%u",
                     seg_idx, rt->pipeline.subbatch_size);

    PRT_PROGRESS_LOG("segment=%u action-generate begin", seg_idx);
    rc = prt_action_generate(rt, seg_idx, seg, &rt->model, &action);
    PRT_GOTO_OUT_ON_ERR("action_generate");
    PRT_PROGRESS_LOG("segment=%u action-generate end action=%u", seg_idx, action->action_id);
    rc = prt_action_alloc_acc(rt, action);
    PRT_GOTO_OUT_ON_ERR("action_alloc_acc");
    PRT_PROGRESS_LOG("segment=%u action-alloc-spm begin action=%u", seg_idx, action->action_id);
    rc = prt_action_alloc_spm(rt, action);
    PRT_GOTO_OUT_ON_ERR("action_alloc_spm");
    PRT_PROGRESS_LOG("segment=%u action-alloc-spm end action=%u spm_pages=%u",
                     seg_idx, action->action_id, action->spm_source.num_spm_pages);
    rt->active_action = action;

    PRT_PROGRESS_LOG("segment=%u build-topology begin", seg_idx);
    rc = build_topology_from_pipeline(rt);
    PRT_GOTO_OUT_ON_ERR("build_topology");
    PRT_PROGRESS_LOG("segment=%u build-topology end pipebufs=%u ringbufs=%u stage_threads=%u",
                     seg_idx, rt->pipebuf_count, rt->ringbuf_count, rt->stage_thread_count);
    PRT_PROGRESS_LOG("segment=%u prepare-stage-spm begin", seg_idx);
    rc = runtime_prepare_stage_spm_windows(rt);
    PRT_GOTO_OUT_ON_ERR("prepare_stage_spm_windows");
    PRT_PROGRESS_LOG("segment=%u prepare-stage-spm end", seg_idx);
    PRT_PROGRESS_LOG("segment=%u bind-topology begin", seg_idx);
    rc = prt_action_bind_topology(rt, action);
    PRT_GOTO_OUT_ON_ERR("action_bind_topology");
    PRT_PROGRESS_LOG("segment=%u bind-topology end", seg_idx);
    PRT_PROGRESS_LOG("segment=%u flush-spm-xlate begin", seg_idx);
    rc = runtime_flush_spm_xlate(rt);
    PRT_GOTO_OUT_ON_ERR("flush_spm_xlate");
    PRT_PROGRESS_LOG("segment=%u flush-spm-xlate end", seg_idx);

    if (is_last_segment) {
      for (uint32_t i = 0; i < model_output_count; ++i) {
        prt_pipebuf_t *sink = find_export_pipebuf_for_tensor(rt, model_output_ids[i]);
        if (!sink) {
          missing_model_sinks += 1;
          continue;
        }
        rc = append_pipebuf_unique(&sink_bufs, &sink_count, &sink_cap, sink);
        PRT_GOTO_OUT_ON_ERR("append_sink_last_segment");
      }
    }

    if (is_last_segment) {
      if (sink_count == 0) {
        fprintf(stderr,
                "segment[%u] missing model output sinks: missing=%u outputs=%u\n",
                seg_idx, missing_model_sinks, model_output_count);
        rc = PRT_ERR_NOT_READY;
        goto out;
      }
    } else {
      if (sink_count == 0) {
        for (uint32_t i = 0; i < rt->pipebuf_count; ++i) {
          prt_pipebuf_t *b = &rt->pipebufs[i];
          if (b->is_entry || !is_export_kind(b->kind)) continue;
          if (has_entry_consumer_for_tensor(rt, b->tensor_id)) continue;
          rc = append_pipebuf_unique(&sink_bufs, &sink_count, &sink_cap, b);
          PRT_GOTO_OUT_ON_ERR("append_sink_intermediate");
        }
      }
      if (sink_count == 0) {
        rc = PRT_ERR_NOT_READY;
        goto out;
      }
    }

    subbatch_size = rt->pipeline.subbatch_size > 0 ? rt->pipeline.subbatch_size : 1U;
    target_batch = args->batch > 0 ? args->batch : subbatch_size;
    {
      uint64_t target_subbatch_u64 =
        ((uint64_t)target_batch + (uint64_t)subbatch_size - 1ULL) / (uint64_t)subbatch_size;
      if (target_subbatch_u64 == 0) target_subbatch_u64 = 1;
      target_subbatch = target_subbatch_u64 > UINT32_MAX ? UINT32_MAX : (uint32_t)target_subbatch_u64;
    }
    PRT_PROGRESS_LOG("segment=%u begin stages=%u sinks=%u subbatch_size=%u target_batch=%u target_subbatch=%u",
                     seg_idx, seg->num_stages, sink_count, subbatch_size, target_batch, target_subbatch);

#if !defined(__riscv)
    if (rt->cfg.backend == PRT_BACKEND_CPU || rt->cfg.backend == PRT_BACKEND_FPGA) {
      uint64_t timeout_ns = (uint64_t)rt->cfg.watchdog_timeout_ms * 1000000ULL;
      run_rc = run_segment_host_serial(rt, seg, target_subbatch, timeout_ns);
      if (run_rc != PRT_OK) {
        fprintf(stderr, "segment[%u] failed: %s (%d)\n", seg_idx, prt_err_str(run_rc), run_rc);
      } else {
        PRT_PROGRESS_LOG("segment=%u host backend complete target_subbatch=%u", seg_idx, target_subbatch);
      }
      runtime_release_topology(rt);
      (void)prt_action_release(rt, &action);
      rt->active_action = NULL;
      continue;
    }
#endif

    for (uint32_t i = 0; i < rt->stage_thread_count; ++i) {
      uint32_t stage_id = i;
      rt->stage_threads[i].stage_id = stage_id;
      rt->stage_threads[i].rt = rt;
      rt->stage_threads[i].op_kind = PRT_STAGE_OP_NONE;
      rt->stage_threads[i].has_task_desc = 0;
      rt->stage_threads[i].stop = 0;
      if (pthread_create(&rt->stage_threads[i].thread, NULL, stage_worker_main, &rt->stage_threads[i]) != 0) {
        rt->fatal_error = PRT_ERR_STATE;
        rt->stop_requested = 1;
        break;
      }
      created_threads += 1;
    }

    start_ms = monotonic_ms();
    last_watchdog_log_ms = start_ms;
    while (!rt->stop_requested && !rt->fatal_error) {
      uint32_t done_subbatch = min_sink_sbatch_offset(sink_bufs, sink_count);
      uint64_t now = monotonic_ms();
      if (done_subbatch >= target_subbatch) {
        PRT_PROGRESS_LOG("segment=%u sink-progress=%u/%u", seg_idx, done_subbatch, target_subbatch);
        rt->stop_requested = 1;
        break;
      }
      if (now - last_watchdog_log_ms >= 1000ULL) {
        PRT_PROGRESS_LOG("segment=%u sink-progress=%u/%u elapsed_ms=%llu fatal=%d stop=%d",
                         seg_idx, done_subbatch, target_subbatch,
                         (unsigned long long)(now - start_ms),
                         rt->fatal_error, rt->stop_requested);
        last_watchdog_log_ms = now;
      }
      if (now - start_ms > (uint64_t)rt->cfg.watchdog_timeout_ms) {
        rt->fatal_error = PRT_ERR_TIMEOUT;
        rt->stop_requested = 1;
        break;
      }
      struct timespec ts;
      ts.tv_sec = 0;
      ts.tv_nsec = 10000000L;
      nanosleep(&ts, NULL);
    }

    for (uint32_t i = 0; i < created_threads; ++i) {
      rt->stage_threads[i].stop = 1;
      pthread_join(rt->stage_threads[i].thread, NULL);
    }
    run_rc = rt->fatal_error ? rt->fatal_error : PRT_OK;
    if (run_rc != PRT_OK) {
      fprintf(stderr, "segment[%u] failed: %s (%d)\n", seg_idx, prt_err_str(run_rc), run_rc);
    } else {
      PRT_PROGRESS_LOG("segment=%u threaded backend complete target_subbatch=%u", seg_idx, target_subbatch);
    }

    runtime_release_topology(rt);
    (void)prt_action_release(rt, &action);
    rt->active_action = NULL;
  }

  if (pipeline_swizzled) {
    rt->pipeline.num_segments = all_num_segments;
    rt->pipeline.segments = (prt_segment_desc_t *)all_segments;
    rt->pipeline.subbatch_size = all_subbatch_size;
  }
  runtime_release_topology(rt);
  (void)prt_action_release(rt, &rt->active_action);
  rt->active_action = NULL;
  (void)prt_action_release(rt, &action);
  rt->active_action = NULL;
  rc = runtime_assert_page_allocator_idle(rt, "post_run_release");
  if (rc != PRT_OK && run_rc == PRT_OK) run_rc = rc;

  if (run_rc == PRT_OK && args->golden_path) {
    rc = load_model_blob_file(args->golden_path, (void **)&golden_blob, &golden_blob_size);
    if (rc != PRT_OK) {
      run_rc = rc;
    } else {
      run_rc = compare_model_outputs_with_blob(rt, model_output_ids, model_output_count,
                                               golden_blob, golden_blob_size);
      if (run_rc == PRT_OK) {
        PRT_PROGRESS_LOG("golden compare passed outputs=%u golden_path=%s",
                         model_output_count, args->golden_path);
      }
    }
  }
  if (run_rc == PRT_OK && args->golden_out_path) {
    rc = dump_model_outputs_to_blob_file(rt, model_output_ids, model_output_count, args->golden_out_path);
    if (rc != PRT_OK) run_rc = rc;
    else PRT_PROGRESS_LOG("golden dump wrote outputs=%u path=%s", model_output_count, args->golden_out_path);
  }

out:
#undef PRT_GOTO_OUT_ON_ERR
  prt_trace_run_end(rt);
  {
    int trc = prt_trace_dump(rt);
    if (trc != PRT_OK && run_rc == PRT_OK && rc == PRT_OK) rc = trc;
  }
  if (pipeline_swizzled) {
    rt->pipeline.num_segments = all_num_segments;
    rt->pipeline.segments = (prt_segment_desc_t *)all_segments;
    rt->pipeline.subbatch_size = all_subbatch_size;
  }
  runtime_release_topology(rt);
  (void)prt_action_release(rt, &action);
  rt->active_action = NULL;
  {
    int idle_rc = runtime_assert_page_allocator_idle(rt, "out_release");
    if (idle_rc != PRT_OK && run_rc == PRT_OK && rc == PRT_OK) rc = idle_rc;
  }
  free(model_input_ids);
  free(model_output_ids);
  free(sink_bufs);
  free(input_blob);
  free(golden_blob);
  PRT_PROGRESS_LOG("runtime end run_rc=%d rc=%d", run_rc, rc);
  if (rc != PRT_OK && run_rc == PRT_OK) return rc;
  return run_rc;
}

int prt_runtime_destroy(prt_runtime_t *rt) {
  if (!rt) return PRT_ERR_INVAL;

  rt->stop_requested = 1;

  runtime_release_topology(rt);

  prt_dma_backend_destroy(rt);
  prt_gemmini_backend_destroy(rt);

  free(rt->model_blob);
  rt->model_blob = NULL;
  rt->model_blob_size = 0;
  rt->model_blob_offset = 0;

  prt_free_model_desc(&rt->model);
  prt_free_pipeline_desc(&rt->pipeline);

  prt_page_table_destroy(rt);

  free(rt->trace_events);
  rt->trace_events = NULL;
  rt->trace_event_cap = 0;
  rt->trace_event_count = 0;
  rt->trace_event_drop_count = 0;

  pthread_cond_destroy(&rt->state_cv);
  pthread_mutex_destroy(&rt->state_lock);
  return PRT_OK;
}
