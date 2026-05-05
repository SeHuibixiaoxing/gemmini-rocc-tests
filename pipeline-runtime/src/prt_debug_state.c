#include "prt_debug_state.h"

#include <errno.h>
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

volatile prt_debug_state_t g_prt_debug_state = PRT_DEBUG_STATE_INIT;
PRT_DEBUG_TLS volatile prt_debug_state_t g_prt_debug_tls_state = PRT_DEBUG_STATE_INIT;
volatile prt_debug_filter_t g_prt_debug_filter = PRT_DEBUG_FILTER_INIT;

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
