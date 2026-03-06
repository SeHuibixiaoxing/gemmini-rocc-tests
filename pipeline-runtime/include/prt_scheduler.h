#ifndef PRT_SCHEDULER_H
#define PRT_SCHEDULER_H

#include "prt_types.h"

#ifdef __cplusplus
extern "C" {
#endif

struct prt_runtime_s;
typedef struct prt_runtime_s prt_runtime_t;

int prt_pipebuf_wait_full(prt_pipebuf_t *buf, uint32_t idx, uint64_t timeout_ns);
int prt_pipebuf_wait_empty(prt_pipebuf_t *buf, uint32_t idx, uint64_t timeout_ns);
int prt_ring_wait_ready(prt_ringbuf_t *rb, uint32_t offset, uint64_t timeout_ns);
int prt_ring_wait_idle(prt_ringbuf_t *rb, uint64_t timeout_ns);

int prt_process_c1(prt_runtime_t *rt, prt_pipebuf_t *buf, uint32_t idx, uint64_t timeout_ns);
int prt_process_c2(prt_runtime_t *rt, prt_pipebuf_t *buf, uint32_t idx, uint64_t timeout_ns);
int prt_process_c3(prt_runtime_t *rt, prt_isolate_pair_t *pair, uint64_t timeout_ns);
int prt_process_c4(prt_runtime_t *rt, prt_shared_pair_t *pair);
int prt_process_c5(prt_runtime_t *rt, prt_pipebuf_t *buf, uint32_t idx, uint64_t timeout_ns);
int prt_process_c6(prt_runtime_t *rt, prt_pipebuf_t *buf, uint32_t idx, uint64_t timeout_ns);
int prt_process_c7(prt_runtime_t *rt, prt_pipebuf_t *buf);
int prt_process_c8(prt_runtime_t *rt, prt_pipebuf_t *buf);
int prt_progress_export_dma(prt_runtime_t *rt, prt_pipebuf_t *buf, uint64_t timeout_ns, int nonblocking);

#ifdef __cplusplus
}
#endif

#endif
