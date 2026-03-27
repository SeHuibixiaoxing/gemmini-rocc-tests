#include "prt_runtime.h"
#include <string.h>

static _Thread_local prt_runtime_t *g_prt_tls_runtime = NULL;
static _Thread_local prt_schedule_action_t *g_prt_tls_action = NULL;

prt_schedule_action_t *prt_runtime_current_action(const prt_runtime_t *rt) {
  if (rt && g_prt_tls_runtime == rt && g_prt_tls_action) {
    return g_prt_tls_action;
  }
  return rt ? rt->active_action : NULL;
}

void prt_runtime_set_thread_action(prt_runtime_t *rt, prt_schedule_action_t *action) {
  if (!rt) return;
  g_prt_tls_runtime = rt;
  g_prt_tls_action = action ? action : rt->active_action;
}

void prt_runtime_clear_thread_action(prt_runtime_t *rt) {
  if (!rt) return;
  if (g_prt_tls_runtime == rt) {
    g_prt_tls_runtime = NULL;
    g_prt_tls_action = NULL;
  }
}

int prt_action_queue_push(prt_runtime_t *rt, prt_schedule_action_t *action, uint32_t hart_id) {
  if (!rt || !action) return PRT_ERR_INVAL;

  pthread_mutex_lock(&rt->action_queue_lock);

  if (rt->action_count >= PRT_MAX_ACTIONS) {
    pthread_mutex_unlock(&rt->action_queue_lock);
    return PRT_ERR_BUSY;
  }

  rt->action_queue[rt->action_count] = action;
  rt->action_to_hart[rt->action_count] = hart_id;
  action->assigned_hart_id = hart_id;
  rt->action_count++;

  pthread_mutex_unlock(&rt->action_queue_lock);
  return PRT_OK;
}

int prt_action_queue_remove(prt_runtime_t *rt, prt_schedule_action_t *action) {
  if (!rt || !action) return PRT_ERR_INVAL;

  pthread_mutex_lock(&rt->action_queue_lock);

  for (uint32_t i = 0; i < rt->action_count; i++) {
    if (rt->action_queue[i] == action) {
      for (uint32_t j = i; j < rt->action_count - 1; j++) {
        rt->action_queue[j] = rt->action_queue[j + 1];
        rt->action_to_hart[j] = rt->action_to_hart[j + 1];
      }
      if (rt->action_count > 0) {
        rt->action_queue[rt->action_count - 1] = NULL;
        rt->action_to_hart[rt->action_count - 1] = 0;
      }
      action->assigned_hart_id = UINT32_MAX;
      rt->action_count--;
      pthread_mutex_unlock(&rt->action_queue_lock);
      return PRT_OK;
    }
  }

  pthread_mutex_unlock(&rt->action_queue_lock);
  return PRT_ERR_NOT_READY;
}

prt_schedule_action_t* prt_action_queue_get_for_hart(prt_runtime_t *rt, uint32_t hart_id) {
  if (!rt) return NULL;

  pthread_mutex_lock(&rt->action_queue_lock);

  for (uint32_t i = 0; i < rt->action_count; i++) {
    if (rt->action_to_hart[i] == hart_id) {
      prt_schedule_action_t *action = rt->action_queue[i];
      pthread_mutex_unlock(&rt->action_queue_lock);
      return action;
    }
  }

  pthread_mutex_unlock(&rt->action_queue_lock);
  return NULL;
}
