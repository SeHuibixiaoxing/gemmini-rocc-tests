#include <stdint.h>
#include <stddef.h>
#include <assert.h>
#include <stdlib.h>
#include <stdalign.h>
#include <stdio.h>
#include <string.h>
#ifndef BAREMETAL
#include <sys/mman.h>
#endif
#include "include/gemmini_params.h"
#include "include/gemmini.h"
#include "include/gemmini_testutils.h"
#include "util.h"

volatile int dma_complete_flag __attribute__((aligned(64)));

#define XCUSTOM_DMA 2

#define ROCC_INSTRUCTION_SS(x, rs1, rs2, funct) \
  ROCC_INSTRUCTION_0_R_R(x, rs1, rs2, funct)

static inline void dma_set_dst(uint64_t addr) {
    ROCC_INSTRUCTION_0_R_R(XCUSTOM_DMA, addr, (uint64_t)&dma_complete_flag, 2);
}

static inline void dma_set_src(uint64_t addr, uint64_t len) {
    ROCC_INSTRUCTION_0_R_R(XCUSTOM_DMA, addr, len, 1);
}

static inline void dma_fence() {
    asm volatile("fence");
    uint64_t status;
    ROCC_INSTRUCTION_R_R_R(XCUSTOM_DMA, status, 0, 0, 3);
    (void)status;

    if (dma_complete_flag != 0) {
        dma_complete_flag = 0;
    }
}

#ifndef NUM_CORES
#warning `NUM_CORES` is not set explicitly. Default to 1.
#define NUM_CORES 1
#endif

#ifndef SIMPLE_BW_MODE
#define SIMPLE_BW_MODE 0
#endif

#ifndef ENABLE_BATCHED_SUBREQ
#define ENABLE_BATCHED_SUBREQ 1
#endif

#ifndef SUBREQ_BYTES
#define SUBREQ_BYTES 4096
#endif

#define ADDR_SIZE (1024 * 1024)

#define SHARED_SPAD_GLOBAL_ADDR_BASE 0x40000000U
#define SHARED_SPAD_LOCAL_SIZE ADDR_SIZE
#define SHARED_SPAD_LOCAL_ADDR_BASE(i) (SHARED_SPAD_GLOBAL_ADDR_BASE + SHARED_SPAD_LOCAL_SIZE * i)

#define SBUS_SPAD_ADDR_BASE 0x70000000U
#define SBUS_SPAD_ADDR_SIZE ADDR_SIZE
#define SBUS_SPAD_ADDR_CEIL (SBUS_SPAD_ADDR_BASE + SBUS_SPAD_ADDR_SIZE)

#define MEM_BUF_SIZE (ADDR_SIZE / sizeof(uint64_t))
static alignas(64) uint64_t mem_buf[MEM_BUF_SIZE];
static uint64_t mem_buf_head_addr = (uint64_t) mem_buf;

#define MEM_ADDR_BASE mem_buf_head_addr
#define MEM_ADDR_SIZE (MEM_BUF_SIZE * sizeof(uint64_t))
#define MEM_ADDR_CEIL (MEM_ADDR_BASE + MEM_ADDR_SIZE)

#define DO_CHECK

static const uint64_t bytes = 64 * 1024;

static const uint64_t warmup_iterations = 1;
static const uint64_t test_iterations = 1;
static const uint64_t rounds_per_iter = 1;

typedef struct {
    const char* name;
    uint64_t base;
    uint64_t size;
} mem_region_t;

typedef struct {
    const char* src_name;
    const char* dst_name;
    uint64_t bytes;
    uint64_t avg_cycles;
    uint64_t avg_bw_scaled;
    uint64_t second_round_bw_scaled;
    int has_second_round;
    int skipped;
    int failed;
} case_result_t;

static mem_region_t regions[] = {
    {"DRAM",            0,                               MEM_ADDR_SIZE},
    {"SBUS_SPAD",       SBUS_SPAD_ADDR_BASE,             SBUS_SPAD_ADDR_SIZE},
    {"SHARED_SPAD_0",   SHARED_SPAD_LOCAL_ADDR_BASE(0),  SHARED_SPAD_LOCAL_SIZE},
    {"SHARED_SPAD_1",   SHARED_SPAD_LOCAL_ADDR_BASE(1),  SHARED_SPAD_LOCAL_SIZE},
    {"SHARED_SPAD_2",   SHARED_SPAD_LOCAL_ADDR_BASE(2),  SHARED_SPAD_LOCAL_SIZE},
    {"SHARED_SPAD_3",   SHARED_SPAD_LOCAL_ADDR_BASE(3),  SHARED_SPAD_LOCAL_SIZE},
};

#define NUM_REGIONS (sizeof(regions) / sizeof(regions[0]))
#define MAX_CASE_RESULTS (NUM_REGIONS * (NUM_REGIONS - 1))

static case_result_t case_results[MAX_CASE_RESULTS];
static size_t case_results_count = 0;
static uint64_t second_round_bw_matrix[NUM_REGIONS][NUM_REGIONS];
static int second_round_bw_valid[NUM_REGIONS][NUM_REGIONS];

static inline void direct_dma_copy(elem_t* in, elem_t* out, uint64_t len) {
    uint64_t src = (uint64_t)in;
    uint64_t dst = (uint64_t)out;

    dma_set_dst(dst);
    dma_set_src(src, len);
}

static inline void direct_dma_copy_batched(elem_t* in, elem_t* out, uint64_t len) {
#if ENABLE_BATCHED_SUBREQ
    const uint64_t min_chunk = sizeof(elem_t);
    uint64_t chunk = SUBREQ_BYTES;
    if (chunk < min_chunk) {
        chunk = min_chunk;
    }
    chunk = (chunk / min_chunk) * min_chunk;
    if (chunk == 0) {
        chunk = min_chunk;
    }

    uint64_t src = (uint64_t)in;
    uint64_t dst = (uint64_t)out;
    uint64_t remaining = len;

    while (remaining > 0) {
        uint64_t this_len = remaining > chunk ? chunk : remaining;
        dma_set_dst(dst);
        dma_set_src(src, this_len);
        dma_fence();
        src += this_len;
        dst += this_len;
        remaining -= this_len;
    }
#else
    direct_dma_copy(in, out, len);
#endif
}

void mem_reset(elem_t* addr, uint64_t bytes) {
    size_t size = bytes / sizeof(elem_t);
    for (size_t i = 0; i < size; i++) {
        addr[i] = (elem_t) 0;
    }
}

void mem_init(elem_t* addr, uint64_t bytes, int x) {
    size_t size = bytes / sizeof(elem_t);
    for (size_t i = 0; i < size; i++) {
        addr[i] = (elem_t) (((uint32_t) 0x5A5A5A5A) ^ (uint32_t) i ^ (uint32_t) x);
    }
}

bool mem_cmp(elem_t* addr, elem_t* addr2, uint64_t bytes) {
    size_t size = bytes / sizeof(elem_t);
    for (size_t i = 0; i < size; i++) {
        if (addr[i] != addr2[i]) {
            printf("mem_cmp failure: %d!=%d at index %lu\n", addr[i], addr2[i], i);
            return false;
        }
    }
    return true;
}

static void print_csv_summary(void) {
    printf("csv_begin\n");
    printf("src,dst,bytes,avg_cycles,avg_bw_x1000_bytes_per_cycle,second_round_bw_x1000_bytes_per_cycle,skipped,failed\n");
    for (size_t i = 0; i < case_results_count; i++) {
        const case_result_t* r = &case_results[i];
        printf("%s,%s,%lu,%lu,%lu,%lu,%d,%d\n",
               r->src_name, r->dst_name, r->bytes,
               r->avg_cycles, r->avg_bw_scaled, r->second_round_bw_scaled,
               r->skipped, r->failed);
    }
    printf("csv_end\n");
}

static void print_ratio_fixed3(uint64_t numer, uint64_t denom) {
    uint64_t int_part = numer / denom;
    uint64_t rem = numer % denom;
    uint64_t frac3 = (rem * 1000) / denom;
    printf("%lu.%03lu", int_part, frac3);
}

static void print_second_round_bw_matrix_csv(void) {
    uint64_t min_bw = 0;

    for (size_t i = 0; i < NUM_REGIONS; i++) {
        for (size_t j = 0; j < NUM_REGIONS; j++) {
            if (i == j) {
                continue;
            }
            if (second_round_bw_valid[i][j]) {
                uint64_t bw = second_round_bw_matrix[i][j];
                if (bw > 0 && (min_bw == 0 || bw < min_bw)) {
                    min_bw = bw;
                }
            }
        }
    }

    printf("csv_matrix_second_round_raw_begin\n");
    printf("src\\dst");
    for (size_t j = 0; j < NUM_REGIONS; j++) {
        printf(",%s", regions[j].name);
    }
    printf("\n");

    for (size_t i = 0; i < NUM_REGIONS; i++) {
        printf("%s", regions[i].name);
        for (size_t j = 0; j < NUM_REGIONS; j++) {
            if (i == j) {
                printf(",");
            } else if (second_round_bw_valid[i][j]) {
                printf(",%lu", second_round_bw_matrix[i][j]);
            } else {
                printf(",");
            }
        }
        printf("\n");
    }
    printf("csv_matrix_second_round_raw_end\n");

    printf("csv_matrix_second_round_norm_begin\n");
    printf("src\\dst");
    for (size_t j = 0; j < NUM_REGIONS; j++) {
        printf(",%s", regions[j].name);
    }
    printf("\n");

    for (size_t i = 0; i < NUM_REGIONS; i++) {
        printf("%s", regions[i].name);
        for (size_t j = 0; j < NUM_REGIONS; j++) {
            if (i == j) {
                printf(",");
            } else if (second_round_bw_valid[i][j] && min_bw > 0) {
                printf(",");
                print_ratio_fixed3(second_round_bw_matrix[i][j], min_bw);
            } else {
                printf(",");
            }
        }
        printf("\n");
    }
    printf("csv_matrix_second_round_norm_end\n");
}

static int test_pair(int cid, int nc, const mem_region_t* src_r, const mem_region_t* dst_r, case_result_t* result) {
#ifndef BAREMETAL
    if (mlockall(MCL_CURRENT | MCL_FUTURE) != 0) {
      perror("mlockall failed");
      exit(1);
    }
#endif

    if (result != NULL) {
        result->src_name = src_r->name;
        result->dst_name = dst_r->name;
        result->bytes = bytes;
        result->avg_cycles = 0;
        result->avg_bw_scaled = 0;
        result->second_round_bw_scaled = 0;
        result->has_second_round = 0;
        result->skipped = 0;
        result->failed = 0;
    }

    if (bytes % nc != 0) {
        if (result != NULL) {
            result->skipped = 1;
        }
        if (cid == 0) {
            printf("SKIP %s->%s: bytes(%lu) %% ncores(%d) != 0\n", src_r->name, dst_r->name, bytes, nc);
        }
        return 0;
    }

    const uint64_t hart_bytes = bytes / nc;

    if (bytes > src_r->size || bytes > dst_r->size) {
        if (result != NULL) {
            result->skipped = 1;
        }
        if (cid == 0) {
            printf("SKIP %s->%s: need bytes=%lu, src_size=%lu, dst_size=%lu\n",
                src_r->name, dst_r->name, bytes, src_r->size, dst_r->size);
        }
        return 0;
    }

    elem_t* buf_in  = (elem_t*) (src_r->base + hart_bytes * cid);
    elem_t* buf_out = (elem_t*) (dst_r->base + hart_bytes * cid);

    barrier(nc);

    int pair_failed = 0;

    uint64_t sum_bw_scaled = 0;
    uint64_t sum_cycles = 0;
    const uint64_t num_iters = warmup_iterations + test_iterations;

    for (uint64_t i = 0; i < num_iters; i++) {
#if defined(DO_CHECK) && !SIMPLE_BW_MODE
        mem_init(buf_in, hart_bytes, (cid + 1) * (i + 2));
        mem_reset(buf_out, hart_bytes);
#endif

        barrier(nc);
        uint64_t t_start = read_cycles();

        for (int round = 0; round < rounds_per_iter; round++) {
            direct_dma_copy_batched(buf_in, buf_out, hart_bytes);
        }

        if (pair_failed) {
            barrier(nc);
            break;
        }

        barrier(nc);
        uint64_t t_end = read_cycles();

        uint64_t cyc = t_end - t_start;
        uint64_t bw_scaled = 1000 * (hart_bytes * 2 * rounds_per_iter) / cyc;

        if (i == 1 && result != NULL) {
            result->second_round_bw_scaled = bw_scaled;
            result->has_second_round = 1;
        }

#if defined(DO_CHECK) && !SIMPLE_BW_MODE
        int eq = mem_cmp(buf_in, buf_out, hart_bytes);
        if (!eq) {
            pair_failed = 1;
            printf("    hart %d: verification FAILED\n", cid);
        }
        barrier(nc);
#endif
        if (i >= warmup_iterations) {
            sum_bw_scaled += bw_scaled;
            sum_cycles += cyc;
        }
    }

    if (test_iterations > 0) {
        uint64_t avg_bw_scaled = sum_bw_scaled / test_iterations;
        uint64_t avg_cycles = sum_cycles / test_iterations;
        if (result != NULL) {
            result->avg_bw_scaled = avg_bw_scaled;
            result->avg_cycles = avg_cycles;
        }
    }

    if (result != NULL) {
        result->failed = pair_failed;
    }

    return pair_failed;
}

int hart_main(int cid, int nc) {
    regions[0].base = MEM_ADDR_BASE;

    const int requested_nc = 1;
    if (cid >= requested_nc) {
        while (1) {}
    }

    int any_failed = 0;

#if SIMPLE_BW_MODE
    const size_t simple_src_indices[] = {0, 2};
    const size_t simple_dst_indices[] = {2, 3};
    const size_t simple_cases = sizeof(simple_src_indices) / sizeof(simple_src_indices[0]);

    for (size_t case_idx = 0; case_idx < simple_cases; case_idx++) {
        size_t src_idx = simple_src_indices[case_idx];
        size_t dst_idx = simple_dst_indices[case_idx];

        case_result_t* result = NULL;
        if (cid == 0 && case_results_count < MAX_CASE_RESULTS) {
            result = &case_results[case_results_count++];
        }

        barrier(requested_nc);
        int failed = test_pair(cid, requested_nc, &regions[src_idx], &regions[dst_idx], result);
        barrier(requested_nc);

        if (cid == 0 && result != NULL) {
            if (result->has_second_round && !result->skipped && !result->failed) {
                second_round_bw_matrix[src_idx][dst_idx] = result->second_round_bw_scaled;
                second_round_bw_valid[src_idx][dst_idx] = 1;
            } else {
                second_round_bw_valid[src_idx][dst_idx] = 0;
            }
        }

        if (failed) {
            any_failed = 1;
        }

        if (cid == 0) {
            printf("\n");
        }
    }
#else
    for (size_t src_idx = 0; src_idx < NUM_REGIONS; src_idx++) {
        for (size_t dst_idx = 0; dst_idx < NUM_REGIONS; dst_idx++) {
            if (src_idx == dst_idx) {
                continue;
            }

            case_result_t* result = NULL;
            if (cid == 0 && case_results_count < MAX_CASE_RESULTS) {
                result = &case_results[case_results_count++];
            }

            barrier(requested_nc);
            int failed = test_pair(cid, requested_nc, &regions[src_idx], &regions[dst_idx], result);
            barrier(requested_nc);

            if (cid == 0 && result != NULL) {
                if (result->has_second_round && !result->skipped && !result->failed) {
                    second_round_bw_matrix[src_idx][dst_idx] = result->second_round_bw_scaled;
                    second_round_bw_valid[src_idx][dst_idx] = 1;
                } else {
                    second_round_bw_valid[src_idx][dst_idx] = 0;
                }
            }

            if (failed) {
                any_failed = 1;
            }

            if (cid == 0) {
                printf("\n");
            }
        }
    }
#endif

    if (cid == 0) {
        printf("matrix test %s\n", any_failed ? "FAILED" : "PASSED");
        print_csv_summary();
        print_second_round_bw_matrix_csv();
    }

    return any_failed;
}

void thread_entry(int cid, int nc) {
    hart_main(cid, nc);
    int ret = 0;
    if (cid != 0) { while (1) {} }
    exit(ret);
}

int main() {
    return 1;
}
