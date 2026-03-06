#ifndef PRT_TYPES_H
#define PRT_TYPES_H

#include <pthread.h>
#include <stdint.h>
#include <stddef.h>

#include "prt_error.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PRT_MAX_CORES 32
#define PRT_MAX_TILE_SPLITS 32
#define PRT_MAX_STAGES 128
#define PRT_MAX_TENSORS 1024
#define PRT_MAX_RING_SLOTS 64
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
  volatile int done;
  volatile int hw_done_flag;
  int initialized;
  uint64_t submit_ns;
  int traced_complete;
  int status;
  int rr_scope_valid;
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
} prt_tensor_binding_t;

typedef struct {
  uint32_t stage_id;
  uint32_t layer_id;
  uint32_t acc_util;
  uint32_t acc_util_present;
  uint32_t num_entry;
  prt_tensor_binding_t *entry;
  uint32_t num_export;
  prt_tensor_binding_t *exports;
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
  uint32_t num_stage_spm_util;
  prt_u32_map_t *tensor_spm_util_in_stage;
  prt_u32_map_t shared_tensor_is_read_first;
  prt_u32_map_t tensor_spm_util_shared;
  prt_u32_map_t tensor_spm_util_in_ringbuffer;
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
  uint32_t page_size_bytes;
  uint32_t spm_xlate_enable;
  uint32_t spm_page_shift;
  uint32_t pages_per_acc;
  uint64_t spm_xlate_range_base;
  uint64_t spm_xlate_range_size;
  prt_dma_backend_t dma_backend;
  prt_gemmini_mode_t gemmini_mode;
  prt_sync_mode_t sync_mode;
  uint32_t watchdog_timeout_ms;
  uint32_t hw_validate_only;
  const char *trace_path;
} prt_runtime_cfg_t;

typedef struct {
  const char *model_yaml;
  const char *model_bin;
  uint64_t model_offset_bytes;
  const char *pipeline_yaml;
  const char *input_path;
  const char *golden_path;
  uint32_t batch;
} prt_run_args_t;

typedef struct {
  uint32_t stage_id;
  uint32_t acc_id;
  uint32_t num_managers;
  uint32_t manager_ids[PRT_MAX_CORES];
  uint32_t tile_count;
  prt_stage_op_t op_kind;
  void *opaque_task;
} prt_conv_task_t;

#ifdef __cplusplus
}
#endif

#endif
