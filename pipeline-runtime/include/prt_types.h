#ifndef PRT_TYPES_H
#define PRT_TYPES_H

#include <pthread.h>
#include <stdint.h>
#include <stddef.h>

#include "prt_error.h"

#ifdef __cplusplus
extern "C" {
#endif

struct prt_runtime_s;

#define PRT_MAX_CORES 64
#define PRT_MAX_ACTIONS 6
#define PRT_MAX_TILE_SPLITS 32
#define PRT_MAX_STAGES 128
#define PRT_MAX_TENSORS 1024
#define PRT_MAX_RING_SLOTS 64
#define PRT_MAX_LAYER_TENSORS 8
#define PRT_PAGE_SIZE_BYTES 1024U
#define PRT_MAX_PAGES_PER_TENSOR 8192U
#define PRT_SHARED_SPAD_GLOBAL_ADDR_BASE 0x40000000ULL

typedef enum {
  PRT_BUF_C1_ENTRY_DRAM_OR_DEPEN = 0,
  PRT_BUF_C2_EXPORT_DRAM_OR_DEPEN,
  PRT_BUF_C3_ISOLATE_NO_RING_PAIR,
  PRT_BUF_C4_SHARED_NO_RING_PAIR,
  PRT_BUF_C5_ENTRY_ISOLATE_WITH_RING,
  PRT_BUF_C6_EXPORT_ISOLATE_WITH_RING,
  PRT_BUF_C7_ENTRY_ALL_RING,
  PRT_BUF_C8_EXPORT_ALL_RING
} prt_pipebuf_kind_t;

typedef enum {
  PRT_DMA_BACKEND_BLOCKING_FENCE = 0,
  PRT_DMA_BACKEND_POLL_PROGRESS_THREAD = 1
} prt_dma_backend_t;

typedef enum {
  PRT_GEMMINI_MODE_BLOCKING_FENCE = 0,
  PRT_GEMMINI_MODE_ASYNC_EXPERIMENTAL = 1
} prt_gemmini_mode_t;

typedef enum {
  PRT_SYNC_MODE_ASYNC = 0,
  PRT_SYNC_MODE_BLOCKING_DEBUG = 1
} prt_sync_mode_t;

typedef enum {
  PRT_STAGE_OP_NONE = 0,
  PRT_STAGE_OP_CONV = 1,
  PRT_STAGE_OP_RESADD = 2
} prt_stage_op_t;

typedef enum {
  PRT_BACKEND_FPGA = 0,
  PRT_BACKEND_CPU = 1
} prt_backend_t;

typedef enum {
  PRT_LAYER_SPLIT_UNSPEC = 0,
  PRT_LAYER_SPLIT_SINGLE = 1,
  PRT_LAYER_SPLIT_OC = 2,
  PRT_LAYER_SPLIT_SPATIAL = 3,
  PRT_LAYER_SPLIT_RESADD_SPATIAL = 4
} prt_layer_split_t;

typedef struct {
  uint32_t start;
  uint32_t end;
} prt_vpage_range_t;

typedef struct {
  uint32_t ppn;
  uint32_t acc_id;
  uint32_t local_page_idx;
} prt_page_t;

typedef struct {
  prt_page_t *data;
  uint32_t size;
  uint32_t cap;
} prt_page_list_t;

typedef struct {
  uint32_t key;
  uint32_t value;
} prt_u32_kv_t;

typedef struct {
  prt_u32_kv_t *data;
  uint32_t size;
  uint32_t cap;
} prt_u32_map_t;

typedef struct {
  uint32_t id;
  uint32_t stage_idx;
  uint32_t tensor_id;
  struct prt_runtime_s *owner_rt;
  volatile uint32_t *completion_flag;
  uint32_t completion_slot;
  volatile int done;
  volatile int hw_done_flag;
  int hw_done_flag_pa_rc;
  int initialized;
  uint64_t submit_ns;
  uint64_t debug_src_addr;
  uint64_t debug_dst_addr;
  uint64_t debug_bytes;
  uint64_t debug_done_flag_va;
  uint64_t debug_done_flag_pa;
  uint64_t debug_last_pending_log_ns;
  uint32_t debug_src_acc;
  uint32_t debug_dst_acc;
  uint32_t debug_page_idx;
  uint32_t debug_progress_polls;
  int debug_force_export_probe;
  int traced_complete;
  int status;
  int rr_scope_valid;
  int rr_scope_external;
  uint32_t rr_cfg_id;
  uint32_t rr_manager_id;
  uint32_t rr_opcode_id;
  pthread_mutex_t lock;
  pthread_cond_t cv;
} prt_dma_token_t;

typedef struct {
  uint64_t src_addr;
  uint64_t dst_addr;
  uint64_t bytes;
  uint32_t src_acc;
  uint32_t dst_acc;
} prt_dma_req_t;

typedef struct {
  uint32_t tensor_id;
  uint32_t stage_idx;
  uint32_t kind;
  uint64_t src_addr;
  uint64_t dst_addr;
  uint64_t bytes;
} prt_trace_evt_t;

typedef enum {
  PRT_TRACE_EVT_NONE = 0,
  PRT_TRACE_EVT_RUN_START = 1,
  PRT_TRACE_EVT_RUN_END = 2,
  PRT_TRACE_EVT_DMA_SUBMIT = 3,
  PRT_TRACE_EVT_DMA_COMPLETE = 4,
  PRT_TRACE_EVT_GEMM_ISSUE = 5,
  PRT_TRACE_EVT_GEMM_FENCE_BEGIN = 6,
  PRT_TRACE_EVT_GEMM_FENCE_END = 7,
  PRT_TRACE_EVT_EXPORT_SUBMIT_AHEAD = 8,
  PRT_TRACE_EVT_EXPORT_RETIRE = 9
} prt_trace_event_kind_t;

typedef struct {
  uint64_t cycle;
  uint64_t mono_ns;
  uint32_t stage_id;
  uint32_t kind;
  uint32_t aux0;
  uint32_t aux1;
} prt_trace_event_t;

typedef struct {
  uint32_t segment_idx;
  uint32_t buffer_id;
  uint32_t tensor_id;
  uint32_t size;
  uint32_t head;
  uint32_t tail;
  uint32_t out_degree;
  prt_u32_map_t use_count;
  prt_page_list_t *slot_pages;

  pthread_mutex_t lock;
  pthread_cond_t cv;
} prt_ringbuf_t;

typedef struct prt_pipebuf_s {
  uint32_t buffer_id;
  uint32_t tensor_id;
  uint32_t stage_idx;
  uint32_t segment_idx;
  int is_entry;
  prt_pipebuf_kind_t kind;
  int with_double_buffer;
  uint32_t in_use_idx;
  uint32_t no_use_idx;

  prt_page_list_t slot_pages[2];
  uint64_t dram_base_addr[2];

  int full[2];
  int cmd_running[2];
  uint32_t cmd_count[2];
  prt_dma_token_t dma_tokens[2];
  int dma_token_live[2];
  uint32_t dma_submit_sbatch[2];
  uint32_t cmd_acc[2];
  prt_vpage_range_t cmd_vrange[2];
  prt_vpage_range_t ring_cmd_vrange[2];

  uint32_t subbatch_offset;
  int tag;
  int shared_tag[2];
  uint32_t fanout_total;
  uint32_t fanout_pending;
  uint64_t state_epoch;

  prt_ringbuf_t *ring;
  pthread_mutex_t lock;
  pthread_cond_t cv;
} prt_pipebuf_t;

typedef struct {
  prt_pipebuf_t *pre_export;
  prt_pipebuf_t *nxt_entry;
  uint32_t *pre_idx;
  uint32_t *nxt_idx;
} prt_isolate_pair_t;

typedef struct {
  prt_pipebuf_t *pre_export;
  prt_pipebuf_t *nxt_entry;
  uint32_t buffer_idx;
  int *tag;
} prt_shared_pair_t;

typedef struct {
  uint32_t tensor_id;
  uint32_t pages_per_buf;
  uint32_t ring_size;
  uint32_t ring_out_degree;
} prt_tensor_desc_t;

typedef struct {
  uint32_t tensor_id;
  char tensor_type[32];
  uint32_t double_buffer;
  uint32_t buffer_id;
} prt_tensor_binding_t;

typedef enum {
  PRT_BUFFER_BINDING_UNKNOWN = 0,
  PRT_BUFFER_BINDING_WEIGHT = 1,
  PRT_BUFFER_BINDING_PIPE = 2,
  PRT_BUFFER_BINDING_RING = 3
} prt_buffer_binding_kind_t;

typedef struct {
  uint32_t buffer_id;
  uint32_t tensor_id;
  uint32_t stage_local_id;
  uint32_t is_entry;
  uint32_t kind;
  uint32_t slot_count;
  uint32_t pages_per_slot;
  uint32_t alias_group_id;
} prt_buffer_binding_t;

typedef struct {
  uint32_t stage_id;
  uint32_t layer_id;
  uint32_t layer_count;
  uint32_t acc_util;
  uint32_t acc_util_present;
  uint32_t num_virtual_acc_ids;
  uint32_t virtual_acc_ids[PRT_MAX_CORES];
  uint32_t virtual_acc_ids_present;
  uint32_t num_physical_acc_ids;
  uint32_t physical_acc_ids[PRT_MAX_CORES];
  uint32_t physical_acc_ids_present;
  uint32_t split_kind;
  uint32_t num_entry;
  prt_tensor_binding_t *entry;
  uint32_t num_export;
  prt_tensor_binding_t *exports;
  uint32_t tensor_id_count;
  uint32_t tensor_ids[PRT_MAX_LAYER_TENSORS];
  uint32_t fix_tensor_count;
  uint32_t fix_tensor_ids[PRT_MAX_LAYER_TENSORS];
  uint32_t inner_isolate_count;
  uint32_t inner_isolate_ids[PRT_MAX_LAYER_TENSORS];
  uint32_t inner_shared_count;
  uint32_t inner_shared_ids[PRT_MAX_LAYER_TENSORS];
  uint32_t dram_bypass_count;
  uint32_t dram_bypass[PRT_MAX_LAYER_TENSORS];
  uint32_t spm_bypass_count;
  uint32_t spm_bypass[PRT_MAX_LAYER_TENSORS];
  uint32_t tensor_usage_count_present;
  uint32_t tensor_usage_count_count;
  uint32_t tensor_usage_count[PRT_MAX_LAYER_TENSORS];
  uint32_t tensor_lazy_fetch_present;
  uint32_t tensor_lazy_fetch_count;
  uint32_t tensor_lazy_fetch[PRT_MAX_LAYER_TENSORS];
  uint32_t local_spm_tensor_count;
  uint32_t local_spm_tensor_addr[PRT_MAX_LAYER_TENSORS];
  uint32_t local_spm_first_vpage[PRT_MAX_LAYER_TENSORS];
  uint32_t local_spm_page_count[PRT_MAX_LAYER_TENSORS];
  uint32_t local_spm_tensor_bytes[PRT_MAX_LAYER_TENSORS];
  uint32_t local_spm_page_span;
  uint32_t exec_base_vpage;
} prt_stage_map_t;

typedef struct {
  uint32_t tensor_id;
  uint32_t count;
  uint32_t size_per;
  uint32_t use_count;
} prt_ring_cfg_t;

typedef struct {
  uint32_t segment_idx;
  uint32_t subbatch_size;
  uint32_t num_stages;
  prt_stage_map_t *stages;
  uint32_t num_ring_cfg;
  prt_ring_cfg_t *ring_cfgs;
  prt_u32_map_t transport_effective_bytes;
  prt_u32_map_t ring_slot_effective_bytes;
  uint32_t segment_spm_page_span;
  uint32_t buffer_binding_count;
  prt_buffer_binding_t *buffer_bindings;
  uint32_t num_stage_spm_util;
  prt_u32_map_t *tensor_spm_util_in_stage;
  prt_u32_map_t shared_tensor_is_read_first;
  prt_u32_map_t tensor_spm_util_shared;
  prt_u32_map_t tensor_spm_util_in_ringbuffer;
  prt_u32_map_t tensor_spm_util_weight;
} prt_segment_desc_t;

typedef struct {
  uint32_t stage_id;
  uint32_t acc_id;
  uint32_t num_entry_tensors;
  uint32_t num_export_tensors;
  uint32_t *entry_tensor_ids;
  uint32_t *export_tensor_ids;
} prt_stage_desc_t;

typedef struct {
  uint32_t index;
  char type[16];
  uint32_t param[16];
  uint32_t param_len;
  uint32_t tensor_ids[8];
  uint32_t tensor_count;
  uint32_t tensor_stride[8];
  uint32_t tensor_stride_count;
  uint32_t tensor_size[8];
  uint32_t tensor_size_count;
  uint64_t address[8];
  uint32_t address_count;
  uint64_t address2[8];
  uint32_t address2_count;
} prt_model_layer_t;

typedef struct {
  uint64_t addr_base;
  uint64_t addr_end;
  uint32_t num_layers;
  prt_model_layer_t *layers;

  uint32_t num_tensors;
  prt_tensor_desc_t *tensors;
  uint32_t num_stages;
  prt_stage_desc_t *stages;
} prt_model_desc_t;

typedef struct {
  uint32_t num_segments;
  uint32_t subbatch_size;
  prt_segment_desc_t *segments;
} prt_pipeline_desc_t;

typedef struct {
  uint32_t num_cores;
  uint32_t num_gemmini_mgrs;
  uint32_t num_dma_mgrs;
  uint32_t gemmini_mgr_base_id;
  uint32_t dma_mgr_base_id;
  uint32_t pair_manager_mode;
  prt_backend_t backend;
  uint32_t page_size_bytes;
  uint32_t spm_xlate_enable;
  uint32_t spm_page_shift;
  uint32_t pages_per_acc;
  uint64_t spm_xlate_range_base;
  uint64_t spm_xlate_range_size;
  uint32_t spm_pt_pool_prealloc_hugepages;
  uint32_t spm_pt_pool_max_hugepages;
  uint32_t spm_pt_require_hugetlb;
  prt_dma_backend_t dma_backend;
  prt_gemmini_mode_t gemmini_mode;
  prt_sync_mode_t sync_mode;
  uint32_t watchdog_timeout_ms;
  uint32_t export_dma_timeout_ms;
  uint32_t hw_validate_only;
  uint32_t deep_log_gate_enable;
  uint32_t deep_log_segment;
  uint32_t deep_log_global_stage;
  uint32_t deep_log_local_stage;
  uint32_t deep_log_subbatch;
  uint32_t deep_log_stage_radius;
  uint32_t deep_log_subbatch_radius;
  const char *trace_path;
} prt_runtime_cfg_t;

typedef struct {
  const char *model_yaml;
  const char *layer_mapping_yaml;
  const char *model_bin;
  uint64_t model_offset_bytes;
  uint32_t skip_model_bin_load;
  const char *pipeline_yaml;
  const char *input_path;
  uint32_t skip_input_load;
  const char *golden_path;
  uint32_t skip_golden_check;
  const char *golden_out_path;
  uint32_t batch;
} prt_run_args_t;

typedef struct {
  uint32_t stage_id;
  uint32_t acc_id;
  uint32_t num_managers;
  uint32_t manager_ids[PRT_MAX_CORES];
  uint32_t tile_count;
  prt_layer_split_t split_kind;
  prt_stage_op_t op_kind;
  void *opaque_task;
} prt_conv_task_t;

static inline int prt_cfg_pair_manager_mode_enabled(const prt_runtime_cfg_t *cfg) {
  return cfg && cfg->pair_manager_mode != 0U;
}

static inline uint32_t prt_cfg_gemmini_mgr_count(const prt_runtime_cfg_t *cfg) {
  return cfg ? cfg->num_gemmini_mgrs : 0U;
}

static inline uint32_t prt_cfg_dma_mgr_count(const prt_runtime_cfg_t *cfg) {
  if (!cfg) return 0U;
  if (prt_cfg_pair_manager_mode_enabled(cfg)) return cfg->num_gemmini_mgrs;
  return cfg->num_dma_mgrs;
}

static inline uint32_t prt_cfg_spm_manager_count(const prt_runtime_cfg_t *cfg) {
  if (!cfg) return 0U;
  if (cfg->num_gemmini_mgrs != 0U) return cfg->num_gemmini_mgrs;
  return cfg->num_cores;
}

static inline uint64_t prt_cfg_spm_total_pages(const prt_runtime_cfg_t *cfg) {
  if (!cfg) return 0ULL;
  return (uint64_t)prt_cfg_spm_manager_count(cfg) * (uint64_t)cfg->pages_per_acc;
}

static inline uint32_t prt_cfg_gemmini_manager_id(const prt_runtime_cfg_t *cfg,
                                                  uint32_t local_idx) {
  if (!cfg) return 0U;
  return cfg->gemmini_mgr_base_id + local_idx;
}

static inline uint32_t prt_cfg_dma_manager_id(const prt_runtime_cfg_t *cfg,
                                              uint32_t local_idx) {
  if (!cfg) return 0U;
  if (prt_cfg_pair_manager_mode_enabled(cfg)) return cfg->gemmini_mgr_base_id + local_idx;
  return cfg->dma_mgr_base_id + local_idx;
}

#ifdef __cplusplus
}
#endif

#endif
