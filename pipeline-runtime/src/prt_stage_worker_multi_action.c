#if defined(__linux__) && !defined(_GNU_SOURCE)
#define _GNU_SOURCE
#endif

#include "prt_runtime.h"
#include "prt_action_queue.h"
#include "prt_schedule_action.h"

#if defined(__linux__)
#include <sched.h>
#include <pthread.h>
#endif

int prt_stage_worker_get_current_hart_id(void) {
#if defined(__linux__)
  return sched_getcpu();
#else
  return 0;
#endif
}

prt_schedule_action_t *prt_stage_worker_get_action(prt_runtime_t *rt, prt_schedule_action_t *preferred) {
  prt_schedule_action_t *action = preferred;
  if (!rt) return NULL;

  if (!action) {
    uint32_t hart_id = (uint32_t)prt_stage_worker_get_current_hart_id();
    action = prt_action_queue_get_for_hart(rt, hart_id);
  }
  if (!action) {
    action = prt_runtime_current_action(rt);
  }

  if (action) {
    prt_runtime_set_thread_action(rt, action);
  }

  return action;
}
