#include "prt_scheduler.h"
#include "prt_runtime.h"
#include "prt_dma.h"
#include <pthread.h>

int prt_process_c1_async(prt_runtime_t *rt, prt_pipebuf_t *buf, uint32_t idx, uint64_t timeout_ns) {
  if (!rt || !buf || idx > 1) return PRT_ERR_INVAL;
  if (rt->cfg.dma_backend != PRT_DMA_BACKEND_POLL_PROGRESS_THREAD) {
    return prt_process_c1(rt, buf, idx, timeout_ns);
  }

  pthread_mutex_lock(&buf->lock);
  if (buf->full[idx]) {
    pthread_mutex_unlock(&buf->lock);
    return PRT_ERR_BUSY;
  }
  if (buf->dma_token_live[idx]) {
    pthread_mutex_unlock(&buf->lock);
    return PRT_ERR_BUSY;
  }
  pthread_mutex_unlock(&buf->lock);

  return prt_process_c1(rt, buf, idx, timeout_ns);
}

int prt_process_c3_async(prt_runtime_t *rt, prt_isolate_pair_t *pair, uint64_t timeout_ns) {
  if (!rt || !pair) return PRT_ERR_INVAL;
  return prt_process_c3(rt, pair, timeout_ns);
}
