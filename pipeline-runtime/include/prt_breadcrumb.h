#ifndef PRT_BREADCRUMB_H
#define PRT_BREADCRUMB_H

#include <stdint.h>

#define PRT_BREADCRUMB_MAGIC 0x505254424352554dULL
#define PRT_BREADCRUMB_VERSION 1U
#define PRT_BREADCRUMB_SLOT_COUNT 64U
#define PRT_BREADCRUMB_ANY_U32 UINT32_MAX

#define PRT_BREADCRUMB_FLAG_SCOPE_VALID    (1U << 0)
#define PRT_BREADCRUMB_FLAG_SCOPE_EXTERNAL (1U << 1)
#define PRT_BREADCRUMB_FLAG_HW_DONE        (1U << 2)
#define PRT_BREADCRUMB_FLAG_BOUNCE         (1U << 3)

typedef enum {
  PRT_BREADCRUMB_KIND_NONE = 0,
  PRT_BREADCRUMB_KIND_RUNTIME = 1,
  PRT_BREADCRUMB_KIND_DMA = 2,
  PRT_BREADCRUMB_KIND_SPM_XLATE = 3,
  PRT_BREADCRUMB_KIND_RR = 4,
  PRT_BREADCRUMB_KIND_GEMMINI = 5,
} prt_breadcrumb_kind_t;

typedef enum {
  PRT_BREADCRUMB_PHASE_NONE = 0,
  PRT_BREADCRUMB_PHASE_RUNTIME_INIT_BEGIN = 1,
  PRT_BREADCRUMB_PHASE_RUNTIME_INIT_DONE = 2,

  PRT_BREADCRUMB_PHASE_DMA_PAGE_BEGIN = 100,
  PRT_BREADCRUMB_PHASE_DMA_SUBMIT_BEGIN = 101,
  PRT_BREADCRUMB_PHASE_DMA_RR_POSTCHECK = 102,
  PRT_BREADCRUMB_PHASE_DMA_DONEFLAG_END = 103,
  PRT_BREADCRUMB_PHASE_DMA_PROGRAM_BEGIN = 104,
  PRT_BREADCRUMB_PHASE_DMA_PROGRAM_POST_FENCE = 105,
  PRT_BREADCRUMB_PHASE_DMA_PROGRAM_POST_DST = 106,
  PRT_BREADCRUMB_PHASE_DMA_PROGRAM_POST_SRC = 107,
  PRT_BREADCRUMB_PHASE_DMA_WAIT_BEFORE_FENCE = 108,
  PRT_BREADCRUMB_PHASE_DMA_WAIT_AFTER_FENCE = 109,
  PRT_BREADCRUMB_PHASE_DMA_WAIT_AFTER_SHARED_FENCE = 110,
  PRT_BREADCRUMB_PHASE_DMA_WAIT_AFTER_RELEASE = 111,
  PRT_BREADCRUMB_PHASE_DMA_PAGE_END = 112,
  PRT_BREADCRUMB_PHASE_DMA_WAIT_AFTER_COMPLETE = 113,
  PRT_BREADCRUMB_PHASE_DMA_WAIT_AFTER_TRACE_COMPLETE = 114,
  PRT_BREADCRUMB_PHASE_DMA_SUBMITWAIT_AFTER_WAIT = 115,
  PRT_BREADCRUMB_PHASE_DMA_SUBMITWAIT_AFTER_CLEANUP = 116,
  PRT_BREADCRUMB_PHASE_DMA_WAIT_BEFORE_SHARED_FENCE = 117,
  PRT_BREADCRUMB_PHASE_DMA_PAGE_AFTER_SUBMITWAIT_RETURN = 118,
  PRT_BREADCRUMB_PHASE_DMA_PAGE_AFTER_PAGE_END = 119,
  PRT_BREADCRUMB_PHASE_DMA_PAGE_AFTER_ACCOUNTING = 120,
  PRT_BREADCRUMB_PHASE_DMA_PAGE_BEFORE_V2P = 121,
  PRT_BREADCRUMB_PHASE_DMA_PAGE_AFTER_V2P = 122,
  PRT_BREADCRUMB_PHASE_DMA_PAGE_DIRECT_PATH_DECIDED = 123,
  PRT_BREADCRUMB_PHASE_DMA_WAIT_DONEFLAG_POLL_BEGIN = 124,
  PRT_BREADCRUMB_PHASE_DMA_WAIT_DONEFLAG_POLL_DONE = 125,
  PRT_BREADCRUMB_PHASE_DMA_WAIT_DONEFLAG_POLL_TIMEOUT = 126,

  PRT_BREADCRUMB_PHASE_SPM_XLATE_FLUSH_BEGIN = 200,
  PRT_BREADCRUMB_PHASE_SPM_XLATE_FLUSH_END = 201,
  PRT_BREADCRUMB_PHASE_SPM_XLATE_RELEASE_FENCE_BEGIN = 202,
  PRT_BREADCRUMB_PHASE_SPM_XLATE_RELEASE_FENCE_END = 203,
  PRT_BREADCRUMB_PHASE_SPM_XLATE_RELEASE_BEGIN = 204,
  PRT_BREADCRUMB_PHASE_SPM_XLATE_RELEASE_END = 205,
  PRT_BREADCRUMB_PHASE_SPM_XLATE_RESTORE_BEGIN = 206,
  PRT_BREADCRUMB_PHASE_SPM_XLATE_RESTORE_END = 207,

  PRT_BREADCRUMB_PHASE_RR_ACQUIRE_BEFORE_CSR_WRITE = 300,
  PRT_BREADCRUMB_PHASE_RR_ACQUIRE_AFTER_CSR_WRITE = 301,
  PRT_BREADCRUMB_PHASE_RR_ACQUIRE_AFTER_CSR_READ = 302,
  PRT_BREADCRUMB_PHASE_RR_ACQUIRE_BEFORE_SET_OPC = 303,
  PRT_BREADCRUMB_PHASE_RR_ACQUIRE_AFTER_SET_OPC = 304,
  PRT_BREADCRUMB_PHASE_RR_ACQUIRE_BEFORE_RETURN = 305,
  PRT_BREADCRUMB_PHASE_RR_ACQUIRE_AFTER_CALL = 306,
  PRT_BREADCRUMB_PHASE_RR_RELEASE_BEGIN = 307,
  PRT_BREADCRUMB_PHASE_RR_RELEASE_END = 308,
  PRT_BREADCRUMB_PHASE_RR_RELEASE_AFTER_CSR_WRITE = 309,

  PRT_BREADCRUMB_PHASE_GEMMINI_POINTWISE_CALL_BEGIN = 400,
  PRT_BREADCRUMB_PHASE_GEMMINI_POINTWISE_CALL_RETURN = 401,
  PRT_BREADCRUMB_PHASE_GEMMINI_POINTWISE_MATMUL_BEGIN = 402,
  PRT_BREADCRUMB_PHASE_GEMMINI_POINTWISE_MATMUL_RETURN = 403,
  PRT_BREADCRUMB_PHASE_GEMMINI_POINTWISE_POSTCALL_FENCE_BEGIN = 404,
  PRT_BREADCRUMB_PHASE_GEMMINI_POINTWISE_POSTCALL_RR_FENCE_RETURN = 405,
  PRT_BREADCRUMB_PHASE_GEMMINI_POINTWISE_POSTCALL_GEMMINI_FENCE_RETURN = 406,
  PRT_BREADCRUMB_PHASE_GEMMINI_POINTWISE_POSTCALL_DRAIN_RETURN = 407,
  PRT_BREADCRUMB_PHASE_GEMMINI_POINTWISE_POSTCALL_RELEASE_RETURN = 408,
  PRT_BREADCRUMB_PHASE_GEMMINI_POINTWISE_PRECALL_AFTER_SCOPE_MARKER = 409,
  PRT_BREADCRUMB_PHASE_GEMMINI_POINTWISE_PRECALL_AFTER_BINDING_SNAPSHOT = 410,
  PRT_BREADCRUMB_PHASE_GEMMINI_POINTWISE_PRECALL_DISPATCH_DECIDED = 411,
  PRT_BREADCRUMB_PHASE_GEMMINI_POINTWISE_PRECALL_AFTER_POSTFLUSH_SNAPSHOT = 412,
  PRT_BREADCRUMB_PHASE_GEMMINI_POINTWISE_PRECALL_READY = 413,
} prt_breadcrumb_phase_t;

typedef struct {
  uint64_t seq;
  uint64_t mono_ns;
  uint64_t src_addr;
  uint64_t dst_addr;
  uint64_t aux_u64_0;
  uint64_t aux_u64_1;
  uint32_t kind;
  uint32_t phase;
  uint32_t flags;
  int32_t rc;
  uint32_t line;
  uint32_t slot_idx;
  uint32_t segment_idx;
  uint32_t global_stage_id;
  uint32_t local_stage_id;
  uint32_t subbatch_id;
  uint32_t tensor_id;
  uint32_t token_id;
  uint32_t manager_id;
  uint32_t page_idx;
  uint32_t reserved0;
} prt_breadcrumb_slot_t;

typedef struct {
  uint64_t magic;
  uint32_t version;
  uint32_t header_bytes;
  uint32_t slot_count;
  uint32_t enabled;
  uint32_t filter_segment;
  uint32_t filter_global_stage;
  uint32_t filter_local_stage;
  uint32_t filter_subbatch;
  uint32_t filter_stage_radius;
  uint32_t filter_subbatch_radius;
  uint64_t last_update_ns;
  uint32_t update_count;
  uint32_t last_slot_idx;
  uint32_t last_kind;
  uint32_t last_phase;
  uint32_t reserved[8];
  prt_breadcrumb_slot_t slots[PRT_BREADCRUMB_SLOT_COUNT];
} prt_breadcrumb_file_t;

int prt_breadcrumb_init(void);
void prt_breadcrumb_destroy(void);
int prt_breadcrumb_enabled(void);

void prt_breadcrumb_set_dma_transfer_context(uint32_t tensor_id,
                                             uint32_t manager_id,
                                             uint32_t page_idx,
                                             uint64_t src_addr,
                                             uint64_t dst_addr,
                                             uint64_t bytes);
void prt_breadcrumb_clear_dma_transfer_context(void);
void prt_breadcrumb_set_export_target_token(uint32_t token_id);
void prt_breadcrumb_clear_export_target_token(void);
uint32_t prt_breadcrumb_get_export_target_token(void);

void prt_breadcrumb_note(prt_breadcrumb_kind_t kind,
                         uint32_t phase,
                         uint32_t tensor_id,
                         uint32_t token_id,
                         uint32_t manager_id,
                         uint32_t page_idx,
                         int rc,
                         uint32_t flags,
                         uint64_t src_addr,
                         uint64_t dst_addr,
                         uint64_t aux_u64_0,
                         uint64_t aux_u64_1,
                         uint32_t line);

#endif
