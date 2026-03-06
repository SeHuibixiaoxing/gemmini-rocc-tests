#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdalign.h>
#include <stdio.h>
#ifndef BAREMETAL
#include <sys/mman.h>
#endif
#include "include/gemmini_params.h"
#include "include/gemmini.h"
#include "include/gemmini_testutils.h"
#include "util.h"

volatile int dma_complete_flag __attribute__((aligned(64)));

#define XCUSTOM_DMA 2

#ifndef DMA_MONITOR_ENABLE
#define DMA_MONITOR_ENABLE 0
#endif

enum {
    DMA_MON_VALID = 0,
    DMA_MON_SRC_CMDS = 1,
    DMA_MON_DST_CMDS = 2,
    DMA_MON_REQ_COPY_BYTES = 3,
    DMA_MON_CYCLES = 4,
    DMA_MON_EFFECTIVE_BYTES = 5,
    DMA_MON_EFF_BW_X1000_BPC = 6,
};

static inline void dma_set_dst(uint64_t addr) {
    ROCC_INSTRUCTION_0_R_R(XCUSTOM_DMA, addr, (uint64_t)&dma_complete_flag, 2);
}

static inline void dma_set_src(uint64_t addr, uint64_t len) {
    ROCC_INSTRUCTION_0_R_R(XCUSTOM_DMA, addr, len, 1);
}

static inline uint64_t dma_monitor_read(uint64_t stat_id) {
    uint64_t value;
    ROCC_INSTRUCTION_R_R_R(XCUSTOM_DMA, value, stat_id, 0, 4);
    return value;
}

static inline void dma_fence(void);

typedef struct {
    uint64_t samples;
    uint64_t src_cmds;
    uint64_t dst_cmds;
    uint64_t req_copy_bytes;
    uint64_t cycles;
    uint64_t effective_bytes;
} dma_mon_accum_t;

static inline void dma_mon_accum_reset(dma_mon_accum_t* acc) {
    acc->samples = 0;
    acc->src_cmds = 0;
    acc->dst_cmds = 0;
    acc->req_copy_bytes = 0;
    acc->cycles = 0;
    acc->effective_bytes = 0;
}

static inline void dma_fence_collect(dma_mon_accum_t* acc) {
    dma_fence();

#if DMA_MONITOR_ENABLE
    if (acc == NULL) {
        return;
    }

    uint64_t valid = dma_monitor_read(DMA_MON_VALID);
    if (valid == 0) {
        return;
    }

    acc->samples += 1;
    acc->src_cmds += dma_monitor_read(DMA_MON_SRC_CMDS);
    acc->dst_cmds += dma_monitor_read(DMA_MON_DST_CMDS);
    acc->req_copy_bytes += dma_monitor_read(DMA_MON_REQ_COPY_BYTES);
    acc->cycles += dma_monitor_read(DMA_MON_CYCLES);
    acc->effective_bytes += dma_monitor_read(DMA_MON_EFFECTIVE_BYTES);
#else
    (void)acc;
#endif
}

static inline void dma_fence(void) {
    asm volatile("fence");
    uint64_t status;
    ROCC_INSTRUCTION_R_R_R(XCUSTOM_DMA, status, 0, 0, 3);
    (void)status;

    if (dma_complete_flag != 0) {
        dma_complete_flag = 0;
    }
}

#define ADDR_SIZE (1024 * 1024)

#define SHARED_SPAD_GLOBAL_ADDR_BASE 0x40000000U
#define SHARED_SPAD_LOCAL_SIZE ADDR_SIZE
#define SHARED_SPAD_LOCAL_ADDR_BASE(i) (SHARED_SPAD_GLOBAL_ADDR_BASE + SHARED_SPAD_LOCAL_SIZE * (i))

#define MEM_BUF_SIZE (ADDR_SIZE / sizeof(uint64_t))
static alignas(64) uint64_t mem_buf[MEM_BUF_SIZE];
static uint64_t mem_buf_head_addr = (uint64_t) mem_buf;

#define MEM_ADDR_BASE mem_buf_head_addr
#define MEM_ADDR_SIZE (MEM_BUF_SIZE * sizeof(uint64_t))

typedef struct {
    const char* name;
    uint64_t base;
    uint64_t size;
} mem_region_t;

typedef struct {
    size_t src_idx;
    size_t dst_idx;
} path_t;

static mem_region_t regions[] = {
    {"DRAM",           0,                              MEM_ADDR_SIZE},
    {"SHARED_SPAD_0",  SHARED_SPAD_LOCAL_ADDR_BASE(0), SHARED_SPAD_LOCAL_SIZE},
    {"SHARED_SPAD_2",  SHARED_SPAD_LOCAL_ADDR_BASE(2), SHARED_SPAD_LOCAL_SIZE},
    {"DRAM_1",         0,                              MEM_ADDR_SIZE / 2},
    {"SHARED_SPAD_1",  SHARED_SPAD_LOCAL_ADDR_BASE(1), SHARED_SPAD_LOCAL_SIZE},
};

static const path_t paths[] = {
    {0, 3},
    {1, 4},
};

#ifndef ABTEST_QUICK
#define ABTEST_QUICK 0
#endif

#if ABTEST_QUICK
static const uint64_t bytes_list[] = {
    256 * 1024,
};

static const uint64_t subreq_bytes_list[] = {
    256 * 1024,
};

static const uint64_t fence_every_cmds_list[] = {
    1,
};

static const int warmup_iterations = 1;
static const int test_iterations = 2;
static const int rounds_per_iter = 4;
#else
static const uint64_t bytes_list[] = {
    16 * 1024,
};

static const uint64_t subreq_bytes_list[] = {
    16 * 1024,
};

static const uint64_t fence_every_cmds_list[] = {
    1,
};

static const int warmup_iterations = 1;
static const int test_iterations = 2;
static const int rounds_per_iter = 4;
#endif

#ifndef ABTEST_HEARTBEAT_CYCLE_INTERVAL
#define ABTEST_HEARTBEAT_CYCLE_INTERVAL (5000000UL)
#endif

typedef struct {
    int enabled;
    const char* src_name;
    const char* dst_name;
    uint64_t bytes;
    uint64_t subreq_bytes;
    uint64_t fence_every_cmds;
    int iter_idx;
    const char* phase;
    uint64_t t0;
    uint64_t next_hb_cycle;
    uint64_t bytes_done;
} heartbeat_ctx_t;

static inline void heartbeat_maybe_print(heartbeat_ctx_t* hb) {
    if (!hb->enabled) {
        return;
    }

    uint64_t now = read_cycles();
    if (now < hb->next_hb_cycle) {
        return;
    }

    uint64_t elapsed = now - hb->t0;
    if (elapsed == 0) {
        elapsed = 1;
    }

    uint64_t bw_scaled = (1000UL * hb->bytes_done) / elapsed;
    printf("abtest_heartbeat,src=%s,dst=%s,bytes=%lu,subreq_bytes=%lu,fence_every_cmds=%lu,iter=%d,phase=%s,elapsed_cycles=%lu,progress_bytes=%lu,bw_x1000_bytes_per_cycle=%lu\n",
           hb->src_name, hb->dst_name, hb->bytes, hb->subreq_bytes, hb->fence_every_cmds,
           hb->iter_idx, hb->phase, elapsed, hb->bytes_done, bw_scaled);

    while (hb->next_hb_cycle <= now) {
        hb->next_hb_cycle += ABTEST_HEARTBEAT_CYCLE_INTERVAL;
    }
}

static inline void dma_copy_split(uint64_t src,
                                  uint64_t dst,
                                  uint64_t bytes,
                                  uint64_t subreq_bytes,
                                  uint64_t fence_every_cmds,
                                  heartbeat_ctx_t* hb,
                                  dma_mon_accum_t* mon_acc) {
    uint64_t min_chunk = sizeof(elem_t);
    uint64_t chunk = subreq_bytes;
    if (chunk < min_chunk) {
        chunk = min_chunk;
    }
    chunk = (chunk / min_chunk) * min_chunk;
    if (chunk == 0) {
        chunk = min_chunk;
    }

    if (fence_every_cmds == 0) {
        fence_every_cmds = 1;
    }

    uint64_t remaining = bytes;
    uint64_t submitted_since_fence = 0;
    while (remaining > 0) {
        uint64_t this_len = remaining > chunk ? chunk : remaining;
        dma_set_dst(dst);
        dma_set_src(src, this_len);
        submitted_since_fence++;
        if (submitted_since_fence >= fence_every_cmds) {
            dma_fence_collect(mon_acc);
            submitted_since_fence = 0;
        }

        if (hb != NULL) {
            hb->bytes_done += this_len * 2UL;
            heartbeat_maybe_print(hb);
        }

        src += this_len;
        dst += this_len;
        remaining -= this_len;
    }

    if (submitted_since_fence > 0) {
        dma_fence_collect(mon_acc);
    }
}

static void run_one_cfg(const mem_region_t* src_r,
                        const mem_region_t* dst_r,
                        uint64_t bytes,
                        uint64_t subreq_bytes,
                        uint64_t fence_every_cmds,
                        int cid,
                        int nc) {
    if (bytes > src_r->size || bytes > dst_r->size) {
        if (cid == 0) {
                 printf("%s,%s,%lu,%lu,%lu,%d,%d,%d,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%s\n",
                   src_r->name, dst_r->name, bytes, subreq_bytes, fence_every_cmds,
                   rounds_per_iter, warmup_iterations, test_iterations,
                   0UL, 0UL,
                   0UL, 0UL,
                   0UL, 0UL,
                     0UL,
                   0UL,
                   "SKIP_SIZE");
        }
        return;
    }

    uint64_t per_hart = bytes / (uint64_t)nc;
    uint64_t src = src_r->base + per_hart * (uint64_t)cid;
    uint64_t dst = dst_r->base + per_hart * (uint64_t)cid;

    uint64_t sum_cyc = 0;
    uint64_t sum_payload_bw_scaled = 0;
    uint64_t sum_link_bw_scaled = 0;
    uint64_t sum_mon_src_cmds = 0;
    uint64_t sum_mon_dst_cmds = 0;
    uint64_t sum_mon_req_copy_bytes = 0;
    uint64_t sum_mon_cycles = 0;
    uint64_t sum_mon_effective_bytes = 0;
    uint64_t sum_mon_samples = 0;
    int total_iters = warmup_iterations + test_iterations;

    barrier(nc);
    for (int i = 0; i < total_iters; i++) {
        barrier(nc);
        uint64_t t0 = read_cycles();

        heartbeat_ctx_t hb;
        hb.enabled = (cid == 0);
        hb.src_name = src_r->name;
        hb.dst_name = dst_r->name;
        hb.bytes = bytes;
        hb.subreq_bytes = subreq_bytes;
        hb.fence_every_cmds = fence_every_cmds;
        hb.iter_idx = i;
        hb.phase = (i < warmup_iterations) ? "warmup" : "test";
        hb.t0 = t0;
        hb.next_hb_cycle = t0 + ABTEST_HEARTBEAT_CYCLE_INTERVAL;
        hb.bytes_done = 0;

        dma_mon_accum_t mon_acc;
        dma_mon_accum_reset(&mon_acc);

        for (int r = 0; r < rounds_per_iter; r++) {
            dma_copy_split(src, dst, per_hart, subreq_bytes, fence_every_cmds, &hb,
                           (cid == 0) ? &mon_acc : NULL);
        }

        barrier(nc);
        uint64_t t1 = read_cycles();
        uint64_t cyc = t1 - t0;
        uint64_t payload_bw_scaled = (1000UL * (per_hart * (uint64_t)rounds_per_iter)) / cyc;
        uint64_t link_bw_scaled = payload_bw_scaled * 2UL;

        if (i >= warmup_iterations) {
            sum_cyc += cyc;
            sum_payload_bw_scaled += payload_bw_scaled;
            sum_link_bw_scaled += link_bw_scaled;
            sum_mon_src_cmds += mon_acc.src_cmds;
            sum_mon_dst_cmds += mon_acc.dst_cmds;
            sum_mon_req_copy_bytes += mon_acc.req_copy_bytes;
            sum_mon_cycles += mon_acc.cycles;
            sum_mon_effective_bytes += mon_acc.effective_bytes;
            sum_mon_samples += mon_acc.samples;
        }
    }

    if (cid == 0) {
        uint64_t avg_cyc = sum_cyc / (uint64_t)test_iterations;
        uint64_t avg_payload_bw_scaled = sum_payload_bw_scaled / (uint64_t)test_iterations;
        uint64_t avg_link_bw_scaled = sum_link_bw_scaled / (uint64_t)test_iterations;
        uint64_t avg_mon_src_cmds = sum_mon_src_cmds / (uint64_t)test_iterations;
        uint64_t avg_mon_dst_cmds = sum_mon_dst_cmds / (uint64_t)test_iterations;
        uint64_t avg_mon_req_copy_bytes = sum_mon_req_copy_bytes / (uint64_t)test_iterations;
        uint64_t avg_mon_cycles = sum_mon_cycles / (uint64_t)test_iterations;
        uint64_t avg_mon_payload_bw_scaled = 0;
        uint64_t avg_mon_link_bw_scaled = 0;
        if (sum_mon_cycles != 0) {
            avg_mon_payload_bw_scaled = (1000UL * sum_mon_effective_bytes) / sum_mon_cycles;
            avg_mon_link_bw_scaled = avg_mon_payload_bw_scaled * 2UL;
        }

        printf("%s,%s,%lu,%lu,%lu,%d,%d,%d,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%s\n",
               src_r->name, dst_r->name, bytes, subreq_bytes, fence_every_cmds,
               rounds_per_iter, warmup_iterations, test_iterations,
               avg_cyc, avg_payload_bw_scaled, avg_link_bw_scaled,
               avg_mon_src_cmds, avg_mon_dst_cmds,
               avg_mon_req_copy_bytes, avg_mon_cycles,
               avg_mon_payload_bw_scaled, avg_mon_link_bw_scaled,
               "OK");
    }
}

int hart_main(int cid, int nc) {
#ifndef BAREMETAL
    if (mlockall(MCL_CURRENT | MCL_FUTURE) != 0) {
        perror("mlockall failed");
        exit(1);
    }
#endif

    regions[0].base = MEM_ADDR_BASE;
    regions[3].base = MEM_ADDR_BASE + (MEM_ADDR_SIZE / 2);

#ifndef ABTEST_REQUESTED_NC
#define ABTEST_REQUESTED_NC 1
#endif

    int requested_nc = ABTEST_REQUESTED_NC;
    if (requested_nc <= 0 || requested_nc > nc) {
        requested_nc = nc;
    }

    if (cid >= requested_nc) {
        while (1) {}
    }

    if (cid == 0) {
        printf("abtest_csv_begin\n");
        printf("abtest_quick=%d\n", ABTEST_QUICK);
        printf("src,dst,bytes,subreq_bytes,fence_every_cmds,rounds_per_iter,warmup,test,avg_cycles,avg_payload_bw_x1000_bytes_per_cycle,avg_link_bw_x1000_bytes_per_cycle,avg_mon_src_cmds,avg_mon_dst_cmds,avg_mon_req_copy_bytes,avg_mon_cycles,avg_mon_payload_bw_x1000_bytes_per_cycle,avg_mon_link_bw_x1000_bytes_per_cycle,status\n");
    }

    for (size_t p = 0; p < sizeof(paths)/sizeof(paths[0]); p++) {
        const mem_region_t* src_r = &regions[paths[p].src_idx];
        const mem_region_t* dst_r = &regions[paths[p].dst_idx];

        for (size_t b = 0; b < sizeof(bytes_list)/sizeof(bytes_list[0]); b++) {
            for (size_t s = 0; s < sizeof(subreq_bytes_list)/sizeof(subreq_bytes_list[0]); s++) {
                for (size_t f = 0; f < sizeof(fence_every_cmds_list)/sizeof(fence_every_cmds_list[0]); f++) {
                    run_one_cfg(src_r, dst_r,
                                bytes_list[b],
                                subreq_bytes_list[s],
                                fence_every_cmds_list[f],
                                cid, requested_nc);
                }
            }
        }
    }

    if (cid == 0) {
        printf("abtest_csv_end\n");
    }

    return 0;
}

void thread_entry(int cid, int nc) {
    int ret = hart_main(cid, nc);
    if (cid != 0) { while (1) {} }
    exit(ret);
}

int main(void) {
    return 1;
}
