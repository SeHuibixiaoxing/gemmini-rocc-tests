#include "include/prt_log_gate.h"

#if PIPELINE_RUNTIME_DEEP_LOG_GATE

#include <stddef.h>

typedef struct {
  uint32_t valid;
  uint32_t segment_idx;
  uint32_t global_stage_id;
  uint32_t local_stage_id;
  uint32_t subbatch_id;
} prt_log_gate_ctx_t;

static prt_log_gate_cfg_t g_prt_log_gate_cfg = {
  .enabled = 0,
  .segment_idx = PRT_LOG_GATE_ANY_U32,
  .global_stage_id = PRT_LOG_GATE_ANY_U32,
  .local_stage_id = PRT_LOG_GATE_ANY_U32,
  .subbatch_id = PRT_LOG_GATE_ANY_U32,
  .stage_radius = 0,
  .subbatch_radius = 0,
};

static __thread prt_log_gate_ctx_t t_prt_log_gate_ctx = {0, 0, 0, 0, 0};
static __thread uint32_t t_prt_log_gate_budget_remaining = 0U;

static int prt_log_gate_match_u32(uint32_t want, uint32_t have, uint32_t radius) {
  uint64_t lo;
  uint64_t hi;
  if (want == PRT_LOG_GATE_ANY_U32) return 1;
  lo = want > radius ? (uint64_t)(want - radius) : 0ULL;
  hi = (uint64_t)want + (uint64_t)radius;
  return (uint64_t)have >= lo && (uint64_t)have <= hi;
}

void prt_log_gate_init(const prt_log_gate_cfg_t *cfg) {
  g_prt_log_gate_cfg.enabled = 0;
  g_prt_log_gate_cfg.segment_idx = PRT_LOG_GATE_ANY_U32;
  g_prt_log_gate_cfg.global_stage_id = PRT_LOG_GATE_ANY_U32;
  g_prt_log_gate_cfg.local_stage_id = PRT_LOG_GATE_ANY_U32;
  g_prt_log_gate_cfg.subbatch_id = PRT_LOG_GATE_ANY_U32;
  g_prt_log_gate_cfg.stage_radius = 0;
  g_prt_log_gate_cfg.subbatch_radius = 0;

  if (cfg != NULL) {
    g_prt_log_gate_cfg = *cfg;
  }

  t_prt_log_gate_ctx.valid = 0;
  t_prt_log_gate_budget_remaining = 0U;
}

void prt_log_gate_set_context(uint32_t segment_idx, uint32_t global_stage_id,
                              uint32_t local_stage_id, uint32_t subbatch_id) {
  t_prt_log_gate_ctx.valid = 1U;
  t_prt_log_gate_ctx.segment_idx = segment_idx;
  t_prt_log_gate_ctx.global_stage_id = global_stage_id;
  t_prt_log_gate_ctx.local_stage_id = local_stage_id;
  t_prt_log_gate_ctx.subbatch_id = subbatch_id;
  t_prt_log_gate_budget_remaining = PRT_LOG_GATE_DEEP_LOG_BUDGET_DEFAULT;
}

void prt_log_gate_clear_context(void) {
  t_prt_log_gate_ctx.valid = 0U;
  t_prt_log_gate_ctx.segment_idx = PRT_LOG_GATE_ANY_U32;
  t_prt_log_gate_ctx.global_stage_id = PRT_LOG_GATE_ANY_U32;
  t_prt_log_gate_ctx.local_stage_id = PRT_LOG_GATE_ANY_U32;
  t_prt_log_gate_ctx.subbatch_id = PRT_LOG_GATE_ANY_U32;
  t_prt_log_gate_budget_remaining = 0U;
}

int prt_log_gate_is_enabled(void) {
  return g_prt_log_gate_cfg.enabled != 0U;
}

int prt_log_gate_allow_deep_logs(void) {
  if (g_prt_log_gate_cfg.enabled == 0U) return 0;
  if (t_prt_log_gate_ctx.valid == 0U) return 0;

  return prt_log_gate_match_u32(g_prt_log_gate_cfg.segment_idx,
                                t_prt_log_gate_ctx.segment_idx, 0U) &&
         prt_log_gate_match_u32(g_prt_log_gate_cfg.global_stage_id,
                                t_prt_log_gate_ctx.global_stage_id,
                                g_prt_log_gate_cfg.stage_radius) &&
         prt_log_gate_match_u32(g_prt_log_gate_cfg.local_stage_id,
                                t_prt_log_gate_ctx.local_stage_id,
                                g_prt_log_gate_cfg.stage_radius) &&
         prt_log_gate_match_u32(g_prt_log_gate_cfg.subbatch_id,
                                t_prt_log_gate_ctx.subbatch_id,
                                g_prt_log_gate_cfg.subbatch_radius);
}

int prt_log_gate_allow_deep_logs_budgeted(void) {
  if (!prt_log_gate_allow_deep_logs()) return 0;
  if (PRT_LOG_GATE_DEEP_LOG_BUDGET_DEFAULT == 0U) return 1;
  if (t_prt_log_gate_budget_remaining == 0U) return 0;
  t_prt_log_gate_budget_remaining -= 1U;
  return 1;
}

void prt_log_gate_get_context(prt_log_gate_ctx_snapshot_t *out) {
  if (!out) return;
  out->valid = t_prt_log_gate_ctx.valid;
  out->segment_idx = t_prt_log_gate_ctx.segment_idx;
  out->global_stage_id = t_prt_log_gate_ctx.global_stage_id;
  out->local_stage_id = t_prt_log_gate_ctx.local_stage_id;
  out->subbatch_id = t_prt_log_gate_ctx.subbatch_id;
}

#endif
