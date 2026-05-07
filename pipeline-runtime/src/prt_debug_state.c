#include "prt_debug_state.h"

#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

#define PRT_DEBUG_STATE_INIT \
  { \
    .segment_idx = PRT_DEBUG_U32_NONE, \
    .global_stage_id = PRT_DEBUG_U32_NONE, \
    .local_stage_id = PRT_DEBUG_U32_NONE, \
    .subbatch_id = PRT_DEBUG_U32_NONE, \
    .phase_id = PRT_DEBUG_PHASE_NONE, \
    .wait_phase_id = PRT_DEBUG_WAIT_NONE, \
    .tensor_id = PRT_DEBUG_U32_NONE, \
    .pipebuf_idx = PRT_DEBUG_U32_NONE, \
    .pipebuf_kind = PRT_DEBUG_U32_NONE, \
    .pipebuf_in_use_idx = PRT_DEBUG_U32_NONE, \
    .pipebuf_no_use_idx = PRT_DEBUG_U32_NONE, \
    .pipebuf_addr = 0, \
    .ring_addr = 0, \
    .manager_id = PRT_DEBUG_U32_NONE, \
    .opcode_id = PRT_DEBUG_U32_NONE, \
    .rr_stage_id = PRT_DEBUG_U32_NONE, \
    .rr_manager_id = PRT_DEBUG_U32_NONE, \
    .rr_opcode_id = PRT_DEBUG_U32_NONE, \
    .rr_cfg_id = PRT_DEBUG_U32_NONE, \
    .last_rc = 0, \
    .event_seq = 0, \
  }

#define PRT_DEBUG_FILTER_INIT \
  { \
    .segment_idx = PRT_DEBUG_U32_NONE, \
    .global_stage_id = PRT_DEBUG_U32_NONE, \
    .local_stage_id = PRT_DEBUG_U32_NONE, \
    .subbatch_id = PRT_DEBUG_U32_NONE, \
    .phase_id = PRT_DEBUG_U32_NONE, \
    .wait_phase_id = PRT_DEBUG_U32_NONE, \
    .tensor_id = PRT_DEBUG_U32_NONE, \
    .manager_id = PRT_DEBUG_U32_NONE, \
    .opcode_id = PRT_DEBUG_U32_NONE, \
    .rr_stage_id = PRT_DEBUG_U32_NONE, \
    .rr_manager_id = PRT_DEBUG_U32_NONE, \
    .rr_opcode_id = PRT_DEBUG_U32_NONE, \
    .rr_cfg_id = PRT_DEBUG_U32_NONE, \
  }

#define PRT_GDB_MARKER_STATE_INIT \
  { \
    .site_id = PRT_GDB_MARKER_SITE_ANY, \
    .segment_idx = PRT_DEBUG_U32_NONE, \
    .global_stage_id = PRT_DEBUG_U32_NONE, \
    .local_stage_id = PRT_DEBUG_U32_NONE, \
    .subbatch_id = PRT_DEBUG_U32_NONE, \
    .manager_id = PRT_DEBUG_U32_NONE, \
    .tensor_id = PRT_DEBUG_U32_NONE, \
    .page_idx = PRT_DEBUG_U32_NONE, \
    .token_id = PRT_DEBUG_U32_NONE, \
    .rc = 0, \
    .aux0 = 0, \
    .aux1 = 0, \
    .hit_count = 0, \
    .line = 0, \
  }

volatile prt_debug_state_t g_prt_debug_state = PRT_DEBUG_STATE_INIT;
PRT_DEBUG_TLS volatile prt_debug_state_t g_prt_debug_tls_state = PRT_DEBUG_STATE_INIT;
volatile prt_debug_filter_t g_prt_debug_filter = PRT_DEBUG_FILTER_INIT;
volatile prt_gdb_marker_state_t g_prt_gdb_marker_state = PRT_GDB_MARKER_STATE_INIT;

typedef struct {
  int initialized;
  int enabled;
  uint32_t site_id;
  uint32_t segment_idx;
  uint32_t global_stage_id;
  uint32_t local_stage_id;
  uint32_t subbatch_id;
  uint32_t manager_id;
  uint32_t tensor_id;
  uint32_t page_idx;
  uint32_t token_id;
} prt_gdb_marker_filter_t;

static prt_gdb_marker_filter_t g_prt_gdb_marker_filter = {
  .initialized = 0,
  .enabled = 0,
  .site_id = PRT_GDB_MARKER_SITE_ANY,
  .segment_idx = PRT_DEBUG_U32_NONE,
  .global_stage_id = PRT_DEBUG_U32_NONE,
  .local_stage_id = PRT_DEBUG_U32_NONE,
  .subbatch_id = PRT_DEBUG_U32_NONE,
  .manager_id = PRT_DEBUG_U32_NONE,
  .tensor_id = PRT_DEBUG_U32_NONE,
  .page_idx = PRT_DEBUG_U32_NONE,
  .token_id = PRT_DEBUG_U32_NONE,
};

static uint32_t prt_debug_env_u32_any(const char *name) {
  const char *value = getenv(name);
  char *end = NULL;
  unsigned long parsed;
  if (!value || !*value) return PRT_DEBUG_U32_NONE;
  if (strcmp(value, "any") == 0 || strcmp(value, "none") == 0 ||
      strcmp(value, "*") == 0) {
    return PRT_DEBUG_U32_NONE;
  }
  errno = 0;
  parsed = strtoul(value, &end, 0);
  if (errno != 0 || end == value || (end && *end != '\0') || parsed > UINT32_MAX) {
    return PRT_DEBUG_U32_NONE;
  }
  return (uint32_t)parsed;
}

static int prt_debug_env_flag(const char *name, int default_value) {
  const char *value = getenv(name);
  if (!value || !*value) return default_value;
  if (strcmp(value, "0") == 0 || strcmp(value, "false") == 0 ||
      strcmp(value, "off") == 0 || strcmp(value, "no") == 0) {
    return 0;
  }
  return 1;
}

static uint32_t prt_gdb_marker_site_from_env(void) {
  const char *value = getenv("PIPELINE_RUNTIME_GDB_MARKER_SITE");
  char *end = NULL;
  unsigned long parsed;
  if (!value || !*value || strcmp(value, "any") == 0 || strcmp(value, "*") == 0) {
    return PRT_GDB_MARKER_SITE_ANY;
  }
  if (strcmp(value, "runtime-ready") == 0 || strcmp(value, "ready") == 0) {
    return PRT_GDB_MARKER_SITE_RUNTIME_READY;
  }
  if (strcmp(value, "segment-begin") == 0 || strcmp(value, "segment") == 0) {
    return PRT_GDB_MARKER_SITE_SEGMENT_BEGIN;
  }
  if (strcmp(value, "worker-create") == 0) return PRT_GDB_MARKER_SITE_WORKER_CREATE;
  if (strcmp(value, "worker-entry") == 0) return PRT_GDB_MARKER_SITE_WORKER_ENTRY;
  if (strcmp(value, "worker-gemm-run") == 0 || strcmp(value, "gemm-run") == 0) {
    return PRT_GDB_MARKER_SITE_WORKER_GEMM_RUN;
  }
  if (strcmp(value, "worker-export-sync") == 0 || strcmp(value, "export-sync") == 0) {
    return PRT_GDB_MARKER_SITE_WORKER_EXPORT_SYNC;
  }
  if (strcmp(value, "export-sync-tensor") == 0) return PRT_GDB_MARKER_SITE_EXPORT_SYNC_TENSOR;
  if (strcmp(value, "export-alias-target-begin") == 0) {
    return PRT_GDB_MARKER_SITE_EXPORT_ALIAS_TARGET_BEGIN;
  }
  if (strcmp(value, "export-alias-target-end") == 0) {
    return PRT_GDB_MARKER_SITE_EXPORT_ALIAS_TARGET_END;
  }
  if (strcmp(value, "dma-export-page-submit-begin") == 0) {
    return PRT_GDB_MARKER_SITE_DMA_EXPORT_PAGE_SUBMIT_BEGIN;
  }
  if (strcmp(value, "dma-export-page-submit-end") == 0) {
    return PRT_GDB_MARKER_SITE_DMA_EXPORT_PAGE_SUBMIT_END;
  }
  if (strcmp(value, "dma-wait-enter") == 0) return PRT_GDB_MARKER_SITE_DMA_WAIT_ENTER;
  if (strcmp(value, "dma-wait-return") == 0) return PRT_GDB_MARKER_SITE_DMA_WAIT_RETURN;
  if (strcmp(value, "worker-entry-process-return") == 0 ||
      strcmp(value, "entry-process-return") == 0) {
    return PRT_GDB_MARKER_SITE_WORKER_ENTRY_PROCESS_RETURN;
  }
  if (strcmp(value, "worker-entry-full-return") == 0 ||
      strcmp(value, "entry-full-return") == 0) {
    return PRT_GDB_MARKER_SITE_WORKER_ENTRY_FULL_RETURN;
  }
  if (strcmp(value, "worker-before-exports-ready") == 0 ||
      strcmp(value, "before-exports-ready") == 0) {
    return PRT_GDB_MARKER_SITE_WORKER_BEFORE_EXPORTS_READY;
  }
  if (strcmp(value, "worker-after-exports-ready") == 0 ||
      strcmp(value, "after-exports-ready") == 0) {
    return PRT_GDB_MARKER_SITE_WORKER_AFTER_EXPORTS_READY;
  }
  if (strcmp(value, "worker-before-build-stage-task") == 0 ||
      strcmp(value, "before-build-stage-task") == 0) {
    return PRT_GDB_MARKER_SITE_WORKER_BEFORE_BUILD_STAGE_TASK;
  }
  if (strcmp(value, "worker-after-build-stage-task") == 0 ||
      strcmp(value, "after-build-stage-task") == 0) {
    return PRT_GDB_MARKER_SITE_WORKER_AFTER_BUILD_STAGE_TASK;
  }
  if (strcmp(value, "artifact-mapping-parse-done") == 0 ||
      strcmp(value, "mapping-parse-done") == 0) {
    return PRT_GDB_MARKER_SITE_ARTIFACT_MAPPING_PARSE_DONE;
  }
  if (strcmp(value, "artifact-validate-done") == 0 ||
      strcmp(value, "validate-artifacts-done") == 0) {
    return PRT_GDB_MARKER_SITE_ARTIFACT_VALIDATE_DONE;
  }
  if (strcmp(value, "synthetic-model-prefault-begin") == 0 ||
      strcmp(value, "synthetic-prefault-begin") == 0) {
    return PRT_GDB_MARKER_SITE_SYNTHETIC_MODEL_PREFAULT_BEGIN;
  }
  if (strcmp(value, "synthetic-model-prefault-end") == 0 ||
      strcmp(value, "synthetic-prefault-end") == 0) {
    return PRT_GDB_MARKER_SITE_SYNTHETIC_MODEL_PREFAULT_END;
  }
  if (strcmp(value, "synthetic-model-ready") == 0 ||
      strcmp(value, "synthetic-ready") == 0) {
    return PRT_GDB_MARKER_SITE_SYNTHETIC_MODEL_READY;
  }
  errno = 0;
  parsed = strtoul(value, &end, 0);
  if (errno != 0 || end == value || (end && *end != '\0') || parsed > UINT32_MAX) {
    return PRT_GDB_MARKER_SITE_ANY;
  }
  return (uint32_t)parsed;
}

static void prt_debug_state_mirror_tls_to_global(void) {
  g_prt_debug_state.segment_idx = g_prt_debug_tls_state.segment_idx;
  g_prt_debug_state.global_stage_id = g_prt_debug_tls_state.global_stage_id;
  g_prt_debug_state.local_stage_id = g_prt_debug_tls_state.local_stage_id;
  g_prt_debug_state.subbatch_id = g_prt_debug_tls_state.subbatch_id;
  g_prt_debug_state.phase_id = g_prt_debug_tls_state.phase_id;
  g_prt_debug_state.wait_phase_id = g_prt_debug_tls_state.wait_phase_id;
  g_prt_debug_state.tensor_id = g_prt_debug_tls_state.tensor_id;
  g_prt_debug_state.pipebuf_idx = g_prt_debug_tls_state.pipebuf_idx;
  g_prt_debug_state.pipebuf_kind = g_prt_debug_tls_state.pipebuf_kind;
  g_prt_debug_state.pipebuf_in_use_idx = g_prt_debug_tls_state.pipebuf_in_use_idx;
  g_prt_debug_state.pipebuf_no_use_idx = g_prt_debug_tls_state.pipebuf_no_use_idx;
  g_prt_debug_state.pipebuf_addr = g_prt_debug_tls_state.pipebuf_addr;
  g_prt_debug_state.ring_addr = g_prt_debug_tls_state.ring_addr;
  g_prt_debug_state.manager_id = g_prt_debug_tls_state.manager_id;
  g_prt_debug_state.opcode_id = g_prt_debug_tls_state.opcode_id;
  g_prt_debug_state.rr_stage_id = g_prt_debug_tls_state.rr_stage_id;
  g_prt_debug_state.rr_manager_id = g_prt_debug_tls_state.rr_manager_id;
  g_prt_debug_state.rr_opcode_id = g_prt_debug_tls_state.rr_opcode_id;
  g_prt_debug_state.rr_cfg_id = g_prt_debug_tls_state.rr_cfg_id;
  g_prt_debug_state.last_rc = g_prt_debug_tls_state.last_rc;
  g_prt_debug_state.event_seq = g_prt_debug_tls_state.event_seq;
}

static void prt_debug_state_touch(void) {
  g_prt_debug_tls_state.event_seq = g_prt_debug_tls_state.event_seq + 1ULL;
}

void prt_debug_filter_init_from_env(void) {
  g_prt_debug_filter.segment_idx =
    prt_debug_env_u32_any("PIPELINE_RUNTIME_DEBUG_FILTER_SEGMENT");
  g_prt_debug_filter.global_stage_id =
    prt_debug_env_u32_any("PIPELINE_RUNTIME_DEBUG_FILTER_GLOBAL_STAGE");
  g_prt_debug_filter.local_stage_id =
    prt_debug_env_u32_any("PIPELINE_RUNTIME_DEBUG_FILTER_LOCAL_STAGE");
  g_prt_debug_filter.subbatch_id =
    prt_debug_env_u32_any("PIPELINE_RUNTIME_DEBUG_FILTER_SUBBATCH");
  g_prt_debug_filter.phase_id =
    prt_debug_env_u32_any("PIPELINE_RUNTIME_DEBUG_FILTER_PHASE");
  g_prt_debug_filter.wait_phase_id =
    prt_debug_env_u32_any("PIPELINE_RUNTIME_DEBUG_FILTER_WAIT_PHASE");
  g_prt_debug_filter.tensor_id =
    prt_debug_env_u32_any("PIPELINE_RUNTIME_DEBUG_FILTER_TENSOR");
  g_prt_debug_filter.manager_id =
    prt_debug_env_u32_any("PIPELINE_RUNTIME_DEBUG_FILTER_MANAGER");
  g_prt_debug_filter.opcode_id =
    prt_debug_env_u32_any("PIPELINE_RUNTIME_DEBUG_FILTER_OPCODE");
  g_prt_debug_filter.rr_stage_id =
    prt_debug_env_u32_any("PIPELINE_RUNTIME_DEBUG_FILTER_RR_STAGE");
  g_prt_debug_filter.rr_manager_id =
    prt_debug_env_u32_any("PIPELINE_RUNTIME_DEBUG_FILTER_RR_MANAGER");
  g_prt_debug_filter.rr_opcode_id =
    prt_debug_env_u32_any("PIPELINE_RUNTIME_DEBUG_FILTER_RR_OPCODE");
  g_prt_debug_filter.rr_cfg_id =
    prt_debug_env_u32_any("PIPELINE_RUNTIME_DEBUG_FILTER_RR_CFG");
}

void prt_gdb_marker_init_from_env(void) {
  g_prt_gdb_marker_filter.initialized = 1;
  g_prt_gdb_marker_filter.enabled =
    prt_debug_env_flag("PIPELINE_RUNTIME_GDB_MARKER_ENABLE", 0);
  g_prt_gdb_marker_filter.site_id = prt_gdb_marker_site_from_env();
  g_prt_gdb_marker_filter.segment_idx =
    prt_debug_env_u32_any("PIPELINE_RUNTIME_GDB_MARKER_SEGMENT");
  g_prt_gdb_marker_filter.global_stage_id =
    prt_debug_env_u32_any("PIPELINE_RUNTIME_GDB_MARKER_GLOBAL_STAGE");
  g_prt_gdb_marker_filter.local_stage_id =
    prt_debug_env_u32_any("PIPELINE_RUNTIME_GDB_MARKER_LOCAL_STAGE");
  g_prt_gdb_marker_filter.subbatch_id =
    prt_debug_env_u32_any("PIPELINE_RUNTIME_GDB_MARKER_SUBBATCH");
  g_prt_gdb_marker_filter.manager_id =
    prt_debug_env_u32_any("PIPELINE_RUNTIME_GDB_MARKER_MANAGER");
  g_prt_gdb_marker_filter.tensor_id =
    prt_debug_env_u32_any("PIPELINE_RUNTIME_GDB_MARKER_TENSOR");
  g_prt_gdb_marker_filter.page_idx =
    prt_debug_env_u32_any("PIPELINE_RUNTIME_GDB_MARKER_PAGE");
  g_prt_gdb_marker_filter.token_id =
    prt_debug_env_u32_any("PIPELINE_RUNTIME_GDB_MARKER_TOKEN");
}

int prt_gdb_marker_enabled(void) {
  if (!g_prt_gdb_marker_filter.initialized) prt_gdb_marker_init_from_env();
  return g_prt_gdb_marker_filter.enabled;
}

static int prt_gdb_marker_match_u32(uint32_t want, uint32_t have) {
  return want == PRT_DEBUG_U32_NONE || have == want;
}

static int prt_gdb_marker_matches(uint32_t site_id,
                                  uint32_t segment_idx,
                                  uint32_t global_stage_id,
                                  uint32_t local_stage_id,
                                  uint32_t subbatch_id,
                                  uint32_t manager_id,
                                  uint32_t tensor_id,
                                  uint32_t page_idx,
                                  uint32_t token_id) {
  if (g_prt_gdb_marker_filter.site_id != PRT_GDB_MARKER_SITE_ANY &&
      g_prt_gdb_marker_filter.site_id != site_id) {
    return 0;
  }
  return prt_gdb_marker_match_u32(g_prt_gdb_marker_filter.segment_idx, segment_idx) &&
         prt_gdb_marker_match_u32(g_prt_gdb_marker_filter.global_stage_id, global_stage_id) &&
         prt_gdb_marker_match_u32(g_prt_gdb_marker_filter.local_stage_id, local_stage_id) &&
         prt_gdb_marker_match_u32(g_prt_gdb_marker_filter.subbatch_id, subbatch_id) &&
         prt_gdb_marker_match_u32(g_prt_gdb_marker_filter.manager_id, manager_id) &&
         prt_gdb_marker_match_u32(g_prt_gdb_marker_filter.tensor_id, tensor_id) &&
         prt_gdb_marker_match_u32(g_prt_gdb_marker_filter.page_idx, page_idx) &&
         prt_gdb_marker_match_u32(g_prt_gdb_marker_filter.token_id, token_id);
}

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
                         uint32_t line) {
  if (!prt_gdb_marker_enabled()) return;
  if (segment_idx == PRT_DEBUG_U32_NONE) {
    segment_idx = g_prt_debug_tls_state.segment_idx;
  }
  if (global_stage_id == PRT_DEBUG_U32_NONE) {
    global_stage_id = g_prt_debug_tls_state.global_stage_id;
  }
  if (local_stage_id == PRT_DEBUG_U32_NONE) {
    local_stage_id = g_prt_debug_tls_state.local_stage_id;
  }
  if (subbatch_id == PRT_DEBUG_U32_NONE) {
    subbatch_id = g_prt_debug_tls_state.subbatch_id;
  }
  if (!prt_gdb_marker_matches(site_id, segment_idx, global_stage_id,
                              local_stage_id, subbatch_id, manager_id,
                              tensor_id, page_idx, token_id)) {
    return;
  }
  g_prt_gdb_marker_state.site_id = site_id;
  g_prt_gdb_marker_state.segment_idx = segment_idx;
  g_prt_gdb_marker_state.global_stage_id = global_stage_id;
  g_prt_gdb_marker_state.local_stage_id = local_stage_id;
  g_prt_gdb_marker_state.subbatch_id = subbatch_id;
  g_prt_gdb_marker_state.manager_id = manager_id;
  g_prt_gdb_marker_state.tensor_id = tensor_id;
  g_prt_gdb_marker_state.page_idx = page_idx;
  g_prt_gdb_marker_state.token_id = token_id;
  g_prt_gdb_marker_state.rc = rc;
  g_prt_gdb_marker_state.aux0 = aux0;
  g_prt_gdb_marker_state.aux1 = aux1;
  g_prt_gdb_marker_state.line = line;
  prt_gdb_marker_stop();
}

void __attribute__((noinline)) prt_gdb_marker_stop(void) {
  g_prt_gdb_marker_state.hit_count = g_prt_gdb_marker_state.hit_count + 1ULL;
  __asm__ volatile("" ::: "memory");
}

void prt_debug_state_reset_thread(void) {
  g_prt_debug_tls_state.segment_idx = PRT_DEBUG_U32_NONE;
  g_prt_debug_tls_state.global_stage_id = PRT_DEBUG_U32_NONE;
  g_prt_debug_tls_state.local_stage_id = PRT_DEBUG_U32_NONE;
  g_prt_debug_tls_state.subbatch_id = PRT_DEBUG_U32_NONE;
  g_prt_debug_tls_state.phase_id = PRT_DEBUG_PHASE_NONE;
  g_prt_debug_tls_state.wait_phase_id = PRT_DEBUG_WAIT_NONE;
  g_prt_debug_tls_state.tensor_id = PRT_DEBUG_U32_NONE;
  g_prt_debug_tls_state.pipebuf_idx = PRT_DEBUG_U32_NONE;
  g_prt_debug_tls_state.pipebuf_kind = PRT_DEBUG_U32_NONE;
  g_prt_debug_tls_state.pipebuf_in_use_idx = PRT_DEBUG_U32_NONE;
  g_prt_debug_tls_state.pipebuf_no_use_idx = PRT_DEBUG_U32_NONE;
  g_prt_debug_tls_state.pipebuf_addr = 0;
  g_prt_debug_tls_state.ring_addr = 0;
  g_prt_debug_tls_state.manager_id = PRT_DEBUG_U32_NONE;
  g_prt_debug_tls_state.opcode_id = PRT_DEBUG_U32_NONE;
  g_prt_debug_tls_state.rr_stage_id = PRT_DEBUG_U32_NONE;
  g_prt_debug_tls_state.rr_manager_id = PRT_DEBUG_U32_NONE;
  g_prt_debug_tls_state.rr_opcode_id = PRT_DEBUG_U32_NONE;
  g_prt_debug_tls_state.rr_cfg_id = PRT_DEBUG_U32_NONE;
  g_prt_debug_tls_state.last_rc = 0;
  prt_debug_state_touch();
  prt_debug_state_mirror_tls_to_global();
}

void prt_debug_state_set_segment(uint32_t segment_idx) {
  g_prt_debug_tls_state.segment_idx = segment_idx;
  g_prt_debug_tls_state.phase_id = PRT_DEBUG_PHASE_SEGMENT_INIT;
  g_prt_debug_tls_state.wait_phase_id = PRT_DEBUG_WAIT_NONE;
  g_prt_debug_tls_state.tensor_id = PRT_DEBUG_U32_NONE;
  g_prt_debug_tls_state.pipebuf_idx = PRT_DEBUG_U32_NONE;
  g_prt_debug_tls_state.pipebuf_kind = PRT_DEBUG_U32_NONE;
  g_prt_debug_tls_state.pipebuf_in_use_idx = PRT_DEBUG_U32_NONE;
  g_prt_debug_tls_state.pipebuf_no_use_idx = PRT_DEBUG_U32_NONE;
  g_prt_debug_tls_state.pipebuf_addr = 0;
  g_prt_debug_tls_state.ring_addr = 0;
  g_prt_debug_tls_state.last_rc = 0;
  prt_debug_state_touch();
  prt_debug_state_mirror_tls_to_global();
}

void prt_debug_state_set_worker(uint32_t segment_idx,
                                uint32_t global_stage_id,
                                uint32_t local_stage_id,
                                uint32_t subbatch_id,
                                uint32_t phase_id) {
  g_prt_debug_tls_state.segment_idx = segment_idx;
  g_prt_debug_tls_state.global_stage_id = global_stage_id;
  g_prt_debug_tls_state.local_stage_id = local_stage_id;
  g_prt_debug_tls_state.subbatch_id = subbatch_id;
  g_prt_debug_tls_state.phase_id = phase_id;
  g_prt_debug_tls_state.wait_phase_id = PRT_DEBUG_WAIT_NONE;
  g_prt_debug_tls_state.tensor_id = PRT_DEBUG_U32_NONE;
  g_prt_debug_tls_state.pipebuf_idx = PRT_DEBUG_U32_NONE;
  g_prt_debug_tls_state.pipebuf_kind = PRT_DEBUG_U32_NONE;
  g_prt_debug_tls_state.pipebuf_in_use_idx = PRT_DEBUG_U32_NONE;
  g_prt_debug_tls_state.pipebuf_no_use_idx = PRT_DEBUG_U32_NONE;
  g_prt_debug_tls_state.pipebuf_addr = 0;
  g_prt_debug_tls_state.ring_addr = 0;
  g_prt_debug_tls_state.last_rc = 0;
  prt_debug_state_touch();
  prt_debug_state_mirror_tls_to_global();
}

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
                              int rc) {
  if (g_prt_debug_tls_state.global_stage_id == PRT_DEBUG_U32_NONE) {
    g_prt_debug_tls_state.global_stage_id = local_stage_id;
  }
  g_prt_debug_tls_state.local_stage_id = local_stage_id;
  g_prt_debug_tls_state.subbatch_id = subbatch_id;
  g_prt_debug_tls_state.phase_id = PRT_DEBUG_PHASE_WAIT;
  g_prt_debug_tls_state.wait_phase_id = wait_phase_id;
  g_prt_debug_tls_state.tensor_id = tensor_id;
  g_prt_debug_tls_state.pipebuf_idx = pipebuf_idx;
  g_prt_debug_tls_state.pipebuf_kind = pipebuf_kind;
  g_prt_debug_tls_state.pipebuf_in_use_idx = pipebuf_in_use_idx;
  g_prt_debug_tls_state.pipebuf_no_use_idx = pipebuf_no_use_idx;
  g_prt_debug_tls_state.pipebuf_addr = pipebuf_addr;
  g_prt_debug_tls_state.ring_addr = ring_addr;
  g_prt_debug_tls_state.last_rc = rc;
  prt_debug_state_touch();
  prt_debug_state_mirror_tls_to_global();
}

void prt_debug_state_set_rr(uint32_t stage_id,
                            uint32_t manager_id,
                            uint32_t opcode_id,
                            uint32_t cfg_id) {
  if (g_prt_debug_tls_state.global_stage_id == PRT_DEBUG_U32_NONE) {
    g_prt_debug_tls_state.global_stage_id = stage_id;
  }
  g_prt_debug_tls_state.local_stage_id = stage_id;
  g_prt_debug_tls_state.phase_id = PRT_DEBUG_PHASE_RR_ACQUIRE;
  g_prt_debug_tls_state.manager_id = manager_id;
  g_prt_debug_tls_state.opcode_id = opcode_id;
  g_prt_debug_tls_state.rr_stage_id = stage_id;
  g_prt_debug_tls_state.rr_manager_id = manager_id;
  g_prt_debug_tls_state.rr_opcode_id = opcode_id;
  g_prt_debug_tls_state.rr_cfg_id = cfg_id;
  g_prt_debug_tls_state.last_rc = 0;
  prt_debug_state_touch();
  prt_debug_state_mirror_tls_to_global();
}
