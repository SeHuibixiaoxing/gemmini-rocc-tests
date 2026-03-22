#ifndef PRT_RUNTIME_H
#define PRT_RUNTIME_H

#include "prt_dma.h"
#include "prt_gemmini_adapter.h"
#include "prt_page_table.h"
#include "prt_schedule_action.h"
#include "prt_scheduler.h"
#include "prt_types.h"
#include "prt_yaml_loader.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  pthread_t thread;
  uint32_t stage_id;
  struct prt_runtime_s *rt;
  prt_stage_op_t op_kind;
  int has_task_desc;
  prt_gemmini_conv_desc_t conv_desc;
  prt_gemmini_resadd_desc_t resadd_desc;
  volatile int stop;
} prt_stage_thread_ctx_t;

typedef struct {
  pthread_t thread;
  struct prt_runtime_s *rt;
  volatile int stop;
} prt_progress_thread_ctx_t;

typedef struct prt_runtime_s {
  prt_runtime_cfg_t cfg;

  prt_dma_backend_ops_t dma_ops;
  prt_gemmini_ops_t gemm_ops;

  pthread_mutex_t state_lock;
  pthread_cond_t state_cv;
  volatile int stop_requested;
  volatile int fatal_error;

  uint32_t next_dma_token_id;

  prt_model_desc_t model;
  prt_pipeline_desc_t pipeline;
  void *model_blob;
  size_t model_blob_size;
  size_t model_blob_offset;

  prt_stage_thread_ctx_t stage_threads[PRT_MAX_STAGES];
  uint32_t stage_thread_count;
  uint32_t stage_layer_ids[PRT_MAX_STAGES];
  uint32_t stage_acc_ids[PRT_MAX_STAGES];
  uint32_t stage_dma_ids[PRT_MAX_STAGES];
  uint32_t stage_tile_counts[PRT_MAX_STAGES];
  uint32_t stage_split_kinds[PRT_MAX_STAGES];
  uint32_t stage_mgr_ids[PRT_MAX_STAGES][PRT_MAX_CORES];
  prt_schedule_action_t *active_action;
  uint32_t stage_spm_rebase_vpage[PRT_MAX_STAGES];
  uint32_t stage_spm_window_pages[PRT_MAX_STAGES];
  uint8_t *stage_spm_shadow[PRT_MAX_STAGES];
  size_t stage_spm_shadow_bytes[PRT_MAX_STAGES];
  uint8_t *stage_dma_bounce[PRT_MAX_STAGES];
  size_t stage_dma_bounce_bytes[PRT_MAX_STAGES];
  uint8_t stage_fixed_lazy_loaded[PRT_MAX_STAGES][PRT_MAX_LAYER_TENSORS];
  prt_progress_thread_ctx_t progress_thread;
  int progress_thread_enabled;
  pthread_mutex_t dma_pending_lock;
  pthread_cond_t dma_pending_cv;
  void *dma_pending_head;
  void *dma_pending_tail;
  uint32_t dma_pending_count;

  pthread_mutex_t page_lock;
  uint8_t *page_used; // bitmap-like array [num_cores * pages_per_acc]

  prt_tensor_alloc_t *tensor_allocs;
  uint32_t tensor_alloc_count;
  uint32_t tensor_alloc_cap;

  uint64_t *spm_pte;
  uint32_t spm_pte_cap;
  uint32_t spm_next_vpage;
  uint64_t spm_ptbr_pa;
  void *spm_pte_alloc;
  size_t spm_pte_alloc_bytes;
  void *spm_alias_map;
  size_t spm_alias_map_bytes;
  uint64_t spm_fault_count;
  uint64_t spm_last_fault_vaddr;
  uint32_t spm_last_fault_cause;
  prt_spm_tensor_map_t *spm_tensor_maps;
  uint32_t spm_tensor_map_count;
  uint32_t spm_tensor_map_cap;

  prt_pipebuf_t *pipebufs;
  uint32_t pipebuf_count;
  prt_spm_page_binding_t *topo_weight_pages;
  uint32_t topo_weight_count;
  uint32_t topo_weight_cap;

  prt_ringbuf_t *ringbufs;
  uint32_t ringbuf_count;

  prt_isolate_pair_t *isolate_pairs;
  uint32_t isolate_pair_count;

  prt_shared_pair_t *shared_pairs;
  uint32_t shared_pair_count;

  uint32_t *topo_alloc_keys;
  uint32_t topo_alloc_count;
  uint32_t topo_alloc_cap;

  volatile uint64_t trace_run_start_ns;
  volatile uint64_t trace_run_end_ns;
  volatile uint64_t trace_dma_submit_count;
  volatile uint64_t trace_dma_complete_count;
  volatile uint64_t trace_dma_inflight;
  volatile uint64_t trace_dma_inflight_peak;
  volatile uint64_t trace_dma_busy_ns;
  volatile uint64_t trace_gemm_issue_count;
  volatile uint64_t trace_gemm_fence_count;
  volatile uint64_t trace_gemm_busy_ns;
  volatile uint64_t trace_prefetch_attempt_count;
  volatile uint64_t trace_prefetch_success_count;
  volatile uint64_t trace_export_submit_ahead_count;
  volatile uint64_t trace_export_retire_count;
  uint64_t trace_cycle_overhead;
  uint64_t trace_cycle_ref;
  uint64_t trace_ns_ref;
  prt_trace_event_t *trace_events;
  uint32_t trace_event_cap;
  volatile uint32_t trace_event_count;
  volatile uint32_t trace_event_drop_count;
} prt_runtime_t;

int prt_runtime_init(const prt_runtime_cfg_t *cfg, prt_runtime_t *rt);
int prt_runtime_run(prt_runtime_t *rt, const prt_run_args_t *args);
int prt_runtime_destroy(prt_runtime_t *rt);
int prt_runtime_prepare_resadd_cpu_fallback(prt_runtime_t *rt, uint32_t stage_id,
                                            const prt_gemmini_resadd_desc_t *src,
                                            prt_gemmini_resadd_desc_t *host_desc,
                                            uint64_t *out_output_host_base,
                                            size_t *out_output_tensor_bytes,
                                            const prt_page_list_t **out_output_pages,
                                            uint32_t *out_output_tensor_id);

uint64_t prt_now_ns(void);
uint64_t prt_now_cycle(void);
void prt_trace_reset(prt_runtime_t *rt);
void prt_trace_run_start(prt_runtime_t *rt);
void prt_trace_run_end(prt_runtime_t *rt);
int prt_trace_calibrate_cycle(prt_runtime_t *rt);
void prt_trace_log_event(prt_runtime_t *rt, uint32_t stage_id, prt_trace_event_kind_t kind,
                         uint32_t aux0, uint32_t aux1);
void prt_trace_on_dma_submit(prt_runtime_t *rt);
void prt_trace_on_dma_complete(prt_runtime_t *rt, uint64_t elapsed_ns);
void prt_trace_on_gemm_issue(prt_runtime_t *rt);
void prt_trace_on_gemm_fence(prt_runtime_t *rt);
void prt_trace_on_gemm_busy(prt_runtime_t *rt, uint64_t elapsed_ns);
void prt_trace_on_prefetch(prt_runtime_t *rt, int success);
void prt_trace_on_export_submit_ahead(prt_runtime_t *rt);
void prt_trace_on_export_retire(prt_runtime_t *rt);
int prt_trace_dump(prt_runtime_t *rt);

#ifdef __cplusplus
}
#endif

#endif
