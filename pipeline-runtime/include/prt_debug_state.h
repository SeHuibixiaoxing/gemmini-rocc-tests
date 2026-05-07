#ifndef PRT_DEBUG_STATE_H
#define PRT_DEBUG_STATE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PRT_DEBUG_U32_NONE UINT32_MAX

#if defined(__GNUC__) || defined(__clang__)
#define PRT_DEBUG_TLS __thread
#else
#define PRT_DEBUG_TLS _Thread_local
#endif

typedef enum {
  PRT_DEBUG_PHASE_NONE = 0,
  PRT_DEBUG_PHASE_SEGMENT_INIT = 1,
  PRT_DEBUG_PHASE_WORKER_LOOP = 2,
  PRT_DEBUG_PHASE_WAIT = 3,
  PRT_DEBUG_PHASE_GEMM_PREP = 4,
  PRT_DEBUG_PHASE_GEMM_RUN = 5,
  PRT_DEBUG_PHASE_GEMM_FENCE = 6,
  PRT_DEBUG_PHASE_EXPORT_SYNC = 7,
  PRT_DEBUG_PHASE_RR_ACQUIRE = 8,
  PRT_DEBUG_PHASE_WORKER_DONE = 9,
} prt_debug_phase_id_t;

typedef enum {
  PRT_GDB_MARKER_SITE_ANY = 0,
  PRT_GDB_MARKER_SITE_RUNTIME_READY = 1,
  PRT_GDB_MARKER_SITE_SEGMENT_BEGIN = 2,
  PRT_GDB_MARKER_SITE_WORKER_CREATE = 3,
  PRT_GDB_MARKER_SITE_WORKER_ENTRY = 4,
  PRT_GDB_MARKER_SITE_WORKER_GEMM_RUN = 5,
  PRT_GDB_MARKER_SITE_WORKER_EXPORT_SYNC = 6,
  PRT_GDB_MARKER_SITE_EXPORT_SYNC_TENSOR = 7,
  PRT_GDB_MARKER_SITE_EXPORT_ALIAS_TARGET_BEGIN = 8,
  PRT_GDB_MARKER_SITE_EXPORT_ALIAS_TARGET_END = 9,
  PRT_GDB_MARKER_SITE_DMA_EXPORT_PAGE_SUBMIT_BEGIN = 10,
  PRT_GDB_MARKER_SITE_DMA_EXPORT_PAGE_SUBMIT_END = 11,
  PRT_GDB_MARKER_SITE_DMA_WAIT_ENTER = 12,
  PRT_GDB_MARKER_SITE_DMA_WAIT_RETURN = 13,
  PRT_GDB_MARKER_SITE_ARTIFACT_MAPPING_PARSE_DONE = 14,
  PRT_GDB_MARKER_SITE_ARTIFACT_VALIDATE_DONE = 15,
  PRT_GDB_MARKER_SITE_SYNTHETIC_MODEL_PREFAULT_BEGIN = 16,
  PRT_GDB_MARKER_SITE_SYNTHETIC_MODEL_PREFAULT_END = 17,
  PRT_GDB_MARKER_SITE_SYNTHETIC_MODEL_READY = 18,
  PRT_GDB_MARKER_SITE_WORKER_ENTRY_PROCESS_RETURN = 19,
  PRT_GDB_MARKER_SITE_WORKER_ENTRY_FULL_RETURN = 20,
  PRT_GDB_MARKER_SITE_WORKER_BEFORE_EXPORTS_READY = 21,
  PRT_GDB_MARKER_SITE_WORKER_AFTER_EXPORTS_READY = 22,
  PRT_GDB_MARKER_SITE_WORKER_BEFORE_BUILD_STAGE_TASK = 23,
  PRT_GDB_MARKER_SITE_WORKER_AFTER_BUILD_STAGE_TASK = 24,
} prt_gdb_marker_site_t;

typedef enum {
  PRT_DEBUG_WAIT_NONE = 0,
  PRT_DEBUG_WAIT_ENTRY_C1_PROCESS = 101,
  PRT_DEBUG_WAIT_ENTRY_C5_PROCESS = 105,
  PRT_DEBUG_WAIT_ENTRY_C7_RING_READY = 107,
  PRT_DEBUG_WAIT_ENTRY_FULL = 110,
  PRT_DEBUG_WAIT_EXPORT_RING_IDLE = 201,
  PRT_DEBUG_WAIT_EXPORT_C4_DRAIN = 204,
  PRT_DEBUG_WAIT_EXPORT_DMA_RETIRE = 206,
  PRT_DEBUG_WAIT_EXPORT_EMPTY = 209,
} prt_debug_wait_phase_id_t;

typedef struct {
  volatile uint32_t segment_idx;
  volatile uint32_t global_stage_id;
  volatile uint32_t local_stage_id;
  volatile uint32_t subbatch_id;
  volatile uint32_t phase_id;
  volatile uint32_t wait_phase_id;
  volatile uint32_t tensor_id;
  volatile uint32_t pipebuf_idx;
  volatile uint32_t pipebuf_kind;
  volatile uint32_t pipebuf_in_use_idx;
  volatile uint32_t pipebuf_no_use_idx;
  volatile uintptr_t pipebuf_addr;
  volatile uintptr_t ring_addr;
  volatile uint32_t manager_id;
  volatile uint32_t opcode_id;
  volatile uint32_t rr_stage_id;
  volatile uint32_t rr_manager_id;
  volatile uint32_t rr_opcode_id;
  volatile uint32_t rr_cfg_id;
  volatile int32_t last_rc;
  volatile uint64_t event_seq;
} prt_debug_state_t;

typedef struct {
  volatile uint32_t segment_idx;
  volatile uint32_t global_stage_id;
  volatile uint32_t local_stage_id;
  volatile uint32_t subbatch_id;
  volatile uint32_t phase_id;
  volatile uint32_t wait_phase_id;
  volatile uint32_t tensor_id;
  volatile uint32_t manager_id;
  volatile uint32_t opcode_id;
  volatile uint32_t rr_stage_id;
  volatile uint32_t rr_manager_id;
  volatile uint32_t rr_opcode_id;
  volatile uint32_t rr_cfg_id;
} prt_debug_filter_t;

typedef struct {
  volatile uint32_t site_id;
  volatile uint32_t segment_idx;
  volatile uint32_t global_stage_id;
  volatile uint32_t local_stage_id;
  volatile uint32_t subbatch_id;
  volatile uint32_t manager_id;
  volatile uint32_t tensor_id;
  volatile uint32_t page_idx;
  volatile uint32_t token_id;
  volatile int32_t rc;
  volatile uint64_t aux0;
  volatile uint64_t aux1;
  volatile uint64_t hit_count;
  volatile uint32_t line;
} prt_gdb_marker_state_t;

extern volatile prt_debug_state_t g_prt_debug_state;
extern PRT_DEBUG_TLS volatile prt_debug_state_t g_prt_debug_tls_state;
extern volatile prt_debug_filter_t g_prt_debug_filter;
extern volatile prt_gdb_marker_state_t g_prt_gdb_marker_state;

void prt_debug_filter_init_from_env(void);
void prt_debug_state_reset_thread(void);
void prt_debug_state_set_segment(uint32_t segment_idx);
void prt_debug_state_set_worker(uint32_t segment_idx,
                                uint32_t global_stage_id,
                                uint32_t local_stage_id,
                                uint32_t subbatch_id,
                                uint32_t phase_id);
void prt_debug_state_set_wait(uint32_t local_stage_id,
                              uint32_t subbatch_id,
                              uint32_t wait_phase_id,
                              uint32_t tensor_id,
                              uint32_t pipebuf_idx,
                              uint32_t pipebuf_kind,
                              uint32_t pipebuf_in_use_idx,
                              uint32_t pipebuf_no_use_idx,
                              uintptr_t pipebuf_addr,
                              uintptr_t ring_addr,
                              int rc);
void prt_debug_state_set_rr(uint32_t stage_id,
                            uint32_t manager_id,
                            uint32_t opcode_id,
                            uint32_t cfg_id);
void prt_gdb_marker_init_from_env(void);
int prt_gdb_marker_enabled(void);
void prt_gdb_marker_note(uint32_t site_id,
                         uint32_t segment_idx,
                         uint32_t global_stage_id,
                         uint32_t local_stage_id,
                         uint32_t subbatch_id,
                         uint32_t manager_id,
                         uint32_t tensor_id,
                         uint32_t page_idx,
                         uint32_t token_id,
                         int rc,
                         uint64_t aux0,
                         uint64_t aux1,
                         uint32_t line);
void prt_gdb_marker_stop(void);

#ifdef __cplusplus
}
#endif

#endif
