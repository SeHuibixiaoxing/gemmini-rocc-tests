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

// ----------------------------------------------------------------------------
// Direct DMA Extension for Gemmini System
// ----------------------------------------------------------------------------
// This program benchmarks the bandwidth of the "Direct DMA" engine, a custom RoCC
// accelerator designed to copy data directly between memory regions (SharedSpad, DRAM)
// without passing through the Gemmini private scratchpad or requiring complex tiling.
//
// ISA Specification (Custom2 Opcode):
// -----------------------------------
// The accelerator uses the RISC-V Custom2 opcode. It works as a streaming COPY engine.
// It consists of a Source (Loader) and a Destination (Writer).
//
// 1. SET_DST (Funct 2): Configure the Write destination.
//    - rs1: Destination Address (byte aligned)
//    - rs2: Flags (0 for normal copy)
//    - C: ROCC_INSTRUCTION_SS(2, dst_addr, 0, 2);
//
// 2. SET_SRC (Funct 1): Configure the Read source and trigger the copy.
//    - rs1: Source Address (byte aligned)
//    - rs2: Length in bytes
//    - C: ROCC_INSTRUCTION_SS(2, src_addr, len, 1);
//
// 3. SFENCE (Funct 0): Setup a memory barrier / wait for completion.
//    - C: ROCC_INSTRUCTION_SS(2, 0, 0, 0);
// ----------------------------------------------------------------------------

volatile int dma_complete_flag __attribute__((aligned(64)));

#define XCUSTOM_DMA 2

#define ROCC_INSTRUCTION_SS(x, rs1, rs2, funct) \
  ROCC_INSTRUCTION_0_R_R(x, rs1, rs2, funct)

// Setup Destination Address
static inline void dma_set_dst(uint64_t addr) {
    ROCC_INSTRUCTION_SS(XCUSTOM_DMA, addr, (uint64_t)&dma_complete_flag, 2);
}

// Setup Source Address and Length, then Start
static inline void dma_set_src(uint64_t addr, uint64_t len) {
    ROCC_INSTRUCTION_SS(XCUSTOM_DMA, addr, len, 1);
}

// Wait for all outstanding operations
static inline void dma_fence() {
    printf("DEBUG: dma_fence start (polling)\n");
    asm volatile("fence"); // Fence before checking

    // Hardware "Check Completion" (Funct 3): Blocks until all issued commands are completed.
    // Returns the number of completed commands.
    // If the hardware is stuck, this instruction will never return.
    uint64_t status;
    printf("DEBUG: invoking hardware completion check (opcode 3)...\n");
    ROCC_INSTRUCTION_R_R_R(XCUSTOM_DMA, status, 0, 0, 3);
    printf("DEBUG: hardware completion waiting returned. status=%lu\n", status);

    // Fallback Verification: Check if the flag was actually written
    if (dma_complete_flag == 0) {
        printf("WARNING: Hardware reported completion, but memory flag is still 0! (Coherence Issue?)\n");
        // Force reload attempt
        asm volatile("fence");
        if (dma_complete_flag == 0) {
             printf("ERROR: Flag remains 0.\n");
        }
    } else {
        dma_complete_flag = 0;
        printf("DEBUG: dma_fence done (flag observed)\n");
    }
}


// >>>> Configuration Region >>>>

#ifndef NUM_CORES
#warning `NUM_CORES` is not set explicitly. Default to 1.
#define NUM_CORES 1
#endif 

#define ADDR_SIZE (1024 * 1024)

#define SHARED_SPAD_GLOBAL_ADDR_BASE 0x40000000U
#define SHARED_SPAD_LOCAL_SIZE ADDR_SIZE
#define SHARED_SPAD_LOCAL_ADDR_BASE(i) (SHARED_SPAD_GLOBAL_ADDR_BASE + SHARED_SPAD_LOCAL_SIZE * i)

#define SBUS_SPAD_ADDR_BASE 0x70000000U
#define SBUS_SPAD_ADDR_SIZE ADDR_SIZE
#define SBUS_SPAD_ADDR_CEIL (SBUS_SPAD_ADDR_BASE + SBUS_SPAD_ADDR_SIZE)

#define MBUS_SPAD_ADDR_BASE 0x60000000U
#define MBUS_SPAD_ADDR_SIZE ADDR_SIZE
#define MBUS_SPAD_ADDR_CEIL (MBUS_SPAD_ADDR_BASE + MBUS_SPAD_ADDR_SIZE)

#define MEM_BUF_SIZE (ADDR_SIZE / sizeof(uint64_t))
static alignas(64) uint64_t mem_buf[MEM_BUF_SIZE];
static uint64_t mem_buf_head_addr = (uint64_t) mem_buf;

#define MEM_ADDR_BASE mem_buf_head_addr
#define MEM_ADDR_SIZE (MEM_BUF_SIZE * sizeof(uint64_t))
#define MEM_ADDR_CEIL (MEM_ADDR_BASE + MEM_ADDR_SIZE)


// Whether to run a single test or multiple tests
#define DO_MULTIPLE_TEST

// Whether to init buffers and check results
#define DO_CHECK

// Default Address Base (can be overridden)
#define ADDR_BASE MEM_ADDR_BASE

static uint64_t buf_base = 0;  // This will be overridden below.

// Default bytes to move
static uint64_t bytes = 512 * 1024;

static const uint64_t warmup_iterations = 1;
static const uint64_t test_iterations = 1;
static const uint64_t rounds_per_iter = 1;

// <<<< Configuration Region <<<<


// Direct DMA Copy Function
// Replaces mvin_mvout with a single hardware acceleration call
static inline void direct_dma_copy(elem_t* in, elem_t* out, uint64_t len) {
    uint64_t src = (uint64_t)in;
    uint64_t dst = (uint64_t)out;
    printf("DEBUG: direct_dma_copy src=%lx dst=%lx len=%lu\n", src, dst, len);
    
    // 1. Configure Destination
    dma_set_dst(dst);
    
    // 2. Configure Source and Trigger
    dma_set_src(src, len);
    
    // 3. No fence here, done by caller
}


void mem_reset(elem_t* addr, uint64_t bytes) {
    printf("DEBUG: mem_reset addr=%lx bytes=%lu\n", (uint64_t)addr, bytes);
    size_t size = bytes / sizeof(elem_t);
    for (size_t i = 0; i < size; i++) {
        addr[i] = (elem_t) 0;
    }
    printf("DEBUG: mem_reset done\n");
}

void mem_init(elem_t* addr, uint64_t bytes, int x) {
    printf("DEBUG: mem_init addr=%lx bytes=%lu\n", (uint64_t)addr, bytes);
    size_t size = bytes / sizeof(elem_t);
    for (size_t i = 0; i < size; i++) {
        addr[i] = (elem_t) (((uint32_t) 0x5A5A5A5A) ^ (uint32_t) i ^ (uint32_t) x);
    }
    printf("DEBUG: mem_init done\n");
}

bool mem_cmp(elem_t* addr, elem_t* addr2, uint64_t bytes) {
    printf("DEBUG: mem_cmp addr=%lx addr2=%lx bytes=%lu\n", (uint64_t)addr, (uint64_t)addr2, bytes);
    size_t size = bytes / sizeof(elem_t);
    for (size_t i = 0; i < size; i++) {
        if (addr[i] != addr2[i]) {
            printf("mem_cmp failure: %d!=%d at index %lu\n", addr[i], addr2[i], i);
            return false;
        }
    }
    printf("DEBUG: mem_cmp done\n");
    return true;
}


int test(int cid, int nc) {
#ifndef BAREMETAL
    if (mlockall(MCL_CURRENT | MCL_FUTURE) != 0) {
      perror("mlockall failed");
      exit(1);
    }
#endif

    uint64_t hart_bytes = bytes / nc;

    // Calculate buffer partitions
    elem_t* buf_in = (elem_t*) (buf_base + hart_bytes * cid);
    elem_t* buf_out = (elem_t*) (buf_base + bytes + hart_bytes * cid);

    for (int i = 0; i < nc; i++) {
        if (i == cid) {
            printf("hart %d: Direct DMA copy %lu bytes from %lx to %lx\n", 
                    cid, hart_bytes, (uint64_t) buf_in, (uint64_t) buf_out);
        }
        barrier(nc);
    }

    // Ensure pipeline is clear
    // dma_fence();

    uint64_t sum_bw_scaled = 0;
    uint64_t num_iters = warmup_iterations + test_iterations;
    
    for (uint64_t i = 0; i < num_iters; i++) {
        char warmup_sign[] = "(warmup)";
        if (i >= warmup_iterations) {
            warmup_sign[0] = '\0';
        }
        if (cid == 0) {
            printf("Iteration %lu/%lu %s\n", i + 1, num_iters, warmup_sign);
        }

#ifdef DO_CHECK
        if (cid == 0) printf("\tmem_init ...\n");
        mem_init(buf_in, hart_bytes, (cid + 1) * (i + 2));
        mem_reset(buf_out, hart_bytes);
#endif

        barrier(nc);
        uint64_t t_start = read_cycles();
        
        for (int round = 0; round < rounds_per_iter; round++) {
            // Using the Direct DMA engine
            direct_dma_copy(buf_in, buf_out, hart_bytes);
            
            // Wait for completion of this round
            // Note: If we had a queue depth > rounds, we could pipeline multiple requests
            // But here we wait to match the blocking semantics of the original test for validation
            dma_fence(); 
        }
        
        barrier(nc);
        uint64_t t_end = read_cycles();

        uint64_t cyc = t_end - t_start;
        uint64_t bw_scaled = 1000 * (bytes * 2 * rounds_per_iter) / cyc;

#ifdef DO_CHECK
        if (cid == 0) printf("\tmem_cmp ...\n");
        int eq = mem_cmp(buf_in, buf_out, hart_bytes);
        if (!eq) {
             printf("\thart %d: Verification FAILED!\n", cid);
        } else {
             if (cid == 0) printf("\tVerification PASSED.\n");
        }
        barrier(nc);
#endif

        if (cid == 0) {
            printf("\t%lu cycles\n", cyc);
            printf("\t%lu*0.001 bytes/cyc\n", bw_scaled);
        }
        if (i >= warmup_iterations) {
            sum_bw_scaled += bw_scaled;
        }
    }

    if (test_iterations > 0) {
        uint64_t avg_bw_scaled = sum_bw_scaled / test_iterations;
        if (cid == 0) {
            printf("avg bandwidth: %lu*0.001 bytes/cyc\n", avg_bw_scaled);
        }
    }

    return 0;
}

int hart_main(int cid, int nc) {
#ifndef DO_MULTIPLE_TEST
    // Call single test.
    buf_base = ADDR_BASE;
    test(cid, nc);
#else
    // Call multiple tests with different params.
    printf("version: 10\n");
    uint64_t nc_list[] = {1};
    uint64_t bytes_list[] = {256 * 1024};
    uint64_t addr_list[] = {MEM_ADDR_BASE, MBUS_SPAD_ADDR_BASE, 
                            SBUS_SPAD_ADDR_BASE, 
                            SHARED_SPAD_LOCAL_ADDR_BASE(0), 
                            SHARED_SPAD_LOCAL_ADDR_BASE(1)};

    for (int k = 0; k < (sizeof(nc_list) / sizeof(uint64_t)); k++) {
        uint64_t nc_ = nc_list[k];
        if (cid >= nc_) { while (true) {} }
        for (int i = 0; i < (sizeof(bytes_list) / sizeof(uint64_t)); i++) {
            bytes = bytes_list[i];
            for (int j = 0; j < (sizeof(addr_list) / sizeof(uint64_t)); j++) {
                buf_base = addr_list[j];
                if (cid == 0) {
                    printf("\n(%d, %d, %d), ncores=%lu, bytes=%lu, addr=%lx\n", 
                        k, i, j, nc_, bytes, buf_base);
                }
                test(cid, nc_);
            }
        }
    }
#endif
    return 0; 
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
