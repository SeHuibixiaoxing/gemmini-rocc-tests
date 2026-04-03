#ifndef PRT_LOG_GATE_H
#define PRT_LOG_GATE_H

#include <stdint.h>

#ifndef PIPELINE_RUNTIME_DEEP_LOG_GATE
#define PIPELINE_RUNTIME_DEEP_LOG_GATE 0
#endif

#ifndef PRT_LOG_GATE_DEEP_LOG_BUDGET_DEFAULT
// Keep deep gated Linux/F2 runs observable without letting hot inner-loop
// `gcrit` traffic saturate the UART path and erase the later boundary.
#define PRT_LOG_GATE_DEEP_LOG_BUDGET_DEFAULT 160U
#endif

#define PRT_LOG_GATE_ANY_U32 UINT32_MAX

typedef struct {
  uint32_t enabled;
  uint32_t segment_idx;
  uint32_t global_stage_id;
  uint32_t local_stage_id;
  uint32_t subbatch_id;
  uint32_t stage_radius;
  uint32_t subbatch_radius;
} prt_log_gate_cfg_t;

#if PIPELINE_RUNTIME_DEEP_LOG_GATE
void prt_log_gate_init(const prt_log_gate_cfg_t *cfg);
void prt_log_gate_set_context(uint32_t segment_idx, uint32_t global_stage_id,
                              uint32_t local_stage_id, uint32_t subbatch_id);
void prt_log_gate_clear_context(void);
int prt_log_gate_is_enabled(void);
int prt_log_gate_allow_deep_logs(void);
int prt_log_gate_allow_deep_logs_budgeted(void);
#else
static inline void prt_log_gate_init(const prt_log_gate_cfg_t *cfg) {
  (void)cfg;
}
static inline void prt_log_gate_set_context(uint32_t segment_idx, uint32_t global_stage_id,
                                            uint32_t local_stage_id, uint32_t subbatch_id) {
  (void)segment_idx;
  (void)global_stage_id;
  (void)local_stage_id;
  (void)subbatch_id;
}
static inline void prt_log_gate_clear_context(void) {
}
static inline int prt_log_gate_is_enabled(void) {
  return 0;
}
static inline int prt_log_gate_allow_deep_logs(void) {
  return 0;
}
static inline int prt_log_gate_allow_deep_logs_budgeted(void) {
  return 0;
}
#endif

#endif
