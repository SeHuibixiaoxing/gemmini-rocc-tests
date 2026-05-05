#ifndef PRT_TRIGGER_LOG_H
#define PRT_TRIGGER_LOG_H

#include <stdint.h>

#define PRT_TRIGGER_LOG_ANY_U32 UINT32_MAX

typedef enum {
  PRT_TRIGGER_LOG_FAMILY_ANY = 0,
  PRT_TRIGGER_LOG_FAMILY_RUNTIME = 1,
  PRT_TRIGGER_LOG_FAMILY_DMA_FIXED_LOAD = 2,
  PRT_TRIGGER_LOG_FAMILY_DMA_EXPORT = 3,
  PRT_TRIGGER_LOG_FAMILY_RR = 4,
  PRT_TRIGGER_LOG_FAMILY_GEMMINI_POINTWISE = 5,
  PRT_TRIGGER_LOG_FAMILY_SPM_XLATE = 6,
} prt_trigger_log_family_t;

typedef struct {
  prt_trigger_log_family_t family;
  const char *phase;
  uint32_t segment_idx;
  uint32_t global_stage_id;
  uint32_t local_stage_id;
  uint32_t subbatch_id;
  uint32_t manager_id;
  uint32_t tensor_id;
  uint32_t page_idx;
  uint32_t token_id;
  int rc;
} prt_trigger_log_event_t;

#if !defined(BAREMETAL)
int prt_trigger_log_enabled(void);
void prt_trigger_log_note(const prt_trigger_log_event_t *event);
#else
static inline int prt_trigger_log_enabled(void) {
  return 0;
}
static inline void prt_trigger_log_note(const prt_trigger_log_event_t *event) {
  (void)event;
}
#endif

#endif
