#ifndef PRT_REROCC_H
#define PRT_REROCC_H

#include <stdint.h>

#include "prt_types.h"

#if defined(__riscv)
#include "rerocc-linux-tests/rerocc_control.h"
#endif

#ifdef __cplusplus
extern "C" {
#endif

struct prt_runtime_s;
typedef struct prt_runtime_s prt_runtime_t;

typedef struct {
  int valid;
  uint32_t cfg_id;
  uint32_t stage_id;
  uint32_t manager_id;
  uint32_t opcode_id;
} prt_rr_scope_t;

int prt_rr_acquire_scope(prt_runtime_t *rt, uint32_t stage_id,
                         uint32_t manager_id, uint32_t opcode_id,
                         prt_rr_scope_t *scope);
int prt_rr_fence_scope(prt_rr_scope_t *scope);
/* Pipeline-runtime callers must use the scoped wrapper instead of raw
 * rr_release(): this helper waits on a same-cfg RRCFG readback before the
 * scope can be invalidated/reused. */
int prt_rr_release_scope(prt_rr_scope_t *scope);
int prt_gemmini_spm_xlate_program(uint32_t manager_id, uint64_t ptbr_pa,
                                  uint32_t pte_count, uint32_t page_shift,
                                  uint64_t range_base, uint64_t range_size,
                                  uint32_t enable);
int prt_gemmini_spm_xlate_reset(uint32_t manager_id, uint32_t page_shift);
int prt_gemmini_spm_xlate_cfg(uint32_t manager_id, uint64_t ptbr_pa,
                              uint32_t pte_count, uint32_t page_shift, uint32_t enable);
int prt_gemmini_spm_xlate_range(uint32_t manager_id, uint64_t range_base,
                                uint64_t range_size);
int prt_gemmini_spm_xlate_flush(uint32_t manager_id);
int prt_gemmini_spm_xlate_fault_read(uint32_t manager_id, uint64_t *fault_vaddr,
                                     uint32_t *fault_cause);
int prt_gemmini_spm_xlate_fault_read_scoped(const prt_rr_scope_t *scope,
                                            uint64_t *fault_vaddr,
                                            uint32_t *fault_cause);

#ifdef __cplusplus
}
#endif

#endif
