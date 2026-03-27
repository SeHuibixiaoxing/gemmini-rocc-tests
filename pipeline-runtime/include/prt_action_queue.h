#ifndef PRT_ACTION_QUEUE_H
#define PRT_ACTION_QUEUE_H

#include "prt_types.h"

#ifdef __cplusplus
extern "C" {
#endif

struct prt_runtime_s;
typedef struct prt_runtime_s prt_runtime_t;
struct prt_schedule_action_s;
typedef struct prt_schedule_action_s prt_schedule_action_t;

int prt_action_queue_push(prt_runtime_t *rt, prt_schedule_action_t *action, uint32_t hart_id);
int prt_action_queue_remove(prt_runtime_t *rt, prt_schedule_action_t *action);
prt_schedule_action_t* prt_action_queue_get_for_hart(prt_runtime_t *rt, uint32_t hart_id);
int prt_stage_worker_get_current_hart_id(void);
prt_schedule_action_t *prt_stage_worker_get_action(prt_runtime_t *rt, prt_schedule_action_t *preferred);

#ifdef __cplusplus
}
#endif

#endif
