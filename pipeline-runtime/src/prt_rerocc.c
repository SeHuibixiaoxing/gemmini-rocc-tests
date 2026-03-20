#include "prt_rerocc.h"

#include <string.h>

#include "prt_error.h"
#include "prt_progress.h"

#if defined(__riscv)
#include "rerocc-linux-tests/rerocc_control.h"
#include "include/rerocc_gemmini_spm_xlate.h"
#endif

#define PRT_RR_ACQUIRE_MAX_RETRIES 1000000UL
#if !defined(RR_MAX_CFGS)
#define RR_MAX_CFGS 16U
#endif

static uint32_t rr_cfg_id_for_stage(uint32_t stage_id, uint32_t opcode_id) {
  uint32_t lane = opcode_id == 2U ? 0U : 1U;
  return ((stage_id * 2U) + lane) % RR_MAX_CFGS;
}

int prt_rr_acquire_scope(prt_runtime_t *rt, uint32_t stage_id,
                         uint32_t manager_id, uint32_t opcode_id,
                         prt_rr_scope_t *scope) {
  (void)rt;
  if (!scope) return PRT_ERR_INVAL;
  memset(scope, 0, sizeof(*scope));
  scope->cfg_id = rr_cfg_id_for_stage(stage_id, opcode_id);
  scope->stage_id = stage_id;
  scope->manager_id = manager_id;
  scope->opcode_id = opcode_id;

#if defined(__riscv)
  {
    unsigned long retries = 0;
    while (!rr_acquire_cfg(scope->cfg_id, manager_id)) {
      retries += 1;
      if (retries == 1UL || retries == 1000UL || retries == 10000UL || (retries % 100000UL) == 0UL) {
        PRT_PROGRESS_LOG("rr-acquire wait stage=%u manager=%u opcode=%u cfg=%u retries=%lu",
                         stage_id, manager_id, opcode_id, scope->cfg_id, retries);
      }
      if (PRT_RR_ACQUIRE_MAX_RETRIES != 0 &&
          retries >= PRT_RR_ACQUIRE_MAX_RETRIES) {
        PRT_PROGRESS_LOG("rr-acquire timeout stage=%u manager=%u opcode=%u cfg=%u retries=%lu",
                         stage_id, manager_id, opcode_id, scope->cfg_id, retries);
        return PRT_ERR_TIMEOUT;
      }
      asm volatile("nop");
    }
    if (retries > 0UL) {
      PRT_PROGRESS_LOG("rr-acquire done stage=%u manager=%u opcode=%u cfg=%u retries=%lu",
                       stage_id, manager_id, opcode_id, scope->cfg_id, retries);
    }
  }
  rr_set_opc((uint8_t)opcode_id, scope->cfg_id);
  scope->valid = 1;
#else
  scope->valid = 0;
#endif
  return PRT_OK;
}

int prt_rr_fence_scope(prt_rr_scope_t *scope) {
  if (!scope) return PRT_ERR_INVAL;
#if defined(__riscv)
  if (scope->valid) {
    rr_fence(scope->cfg_id);
  }
#endif
  return PRT_OK;
}

int prt_rr_release_scope(prt_rr_scope_t *scope) {
  if (!scope) return PRT_ERR_INVAL;
#if defined(__riscv)
  if (scope->valid) {
    rr_release(scope->cfg_id);
    scope->valid = 0;
  }
#endif
  return PRT_OK;
}

int prt_gemmini_spm_xlate_cfg(uint32_t manager_id, uint64_t ptbr_pa,
                              uint32_t pte_count, uint32_t page_shift, uint32_t enable) {
#if defined(__riscv) && defined(PRT_ENABLE_GEMMINI_SPM_XLATE_INSN)
  prt_rr_scope_t scope;
  int rc = prt_rr_acquire_scope(NULL, 0, manager_id, 3U, &scope);
  if (rc != PRT_OK) return rc;
  rerocc_gemmini_spm_xlate_cfg(ptbr_pa, pte_count, page_shift, enable);
  (void)prt_rr_release_scope(&scope);
#else
  (void)manager_id;
  (void)ptbr_pa;
  (void)pte_count;
  (void)page_shift;
  (void)enable;
#endif
  return PRT_OK;
}

int prt_gemmini_spm_xlate_range(uint32_t manager_id, uint64_t range_base,
                                uint64_t range_size) {
#if defined(__riscv) && defined(PRT_ENABLE_GEMMINI_SPM_XLATE_INSN)
  prt_rr_scope_t scope;
  int rc = prt_rr_acquire_scope(NULL, 0, manager_id, 3U, &scope);
  if (rc != PRT_OK) return rc;
  rerocc_gemmini_spm_xlate_range(range_base, range_size);
  (void)prt_rr_release_scope(&scope);
#else
  (void)manager_id;
  (void)range_base;
  (void)range_size;
#endif
  return PRT_OK;
}

int prt_gemmini_spm_xlate_flush(uint32_t manager_id) {
#if defined(__riscv) && defined(PRT_ENABLE_GEMMINI_SPM_XLATE_INSN)
  prt_rr_scope_t scope;
  int rc = prt_rr_acquire_scope(NULL, 0, manager_id, 3U, &scope);
  if (rc != PRT_OK) return rc;
  rerocc_gemmini_spm_xlate_flush();
  (void)prt_rr_release_scope(&scope);
#else
  (void)manager_id;
#endif
  return PRT_OK;
}

int prt_gemmini_spm_xlate_fault_read(uint32_t manager_id, uint64_t *fault_vaddr,
                                     uint32_t *fault_cause) {
#if defined(__riscv) && defined(PRT_ENABLE_GEMMINI_SPM_XLATE_INSN)
  uint64_t raw;
  prt_rr_scope_t scope;
  int rc = prt_rr_acquire_scope(NULL, 0, manager_id, 3U, &scope);
  if (rc != PRT_OK) return rc;
  raw = rerocc_gemmini_spm_xlate_fault();
  (void)prt_rr_release_scope(&scope);
  if (fault_vaddr) *fault_vaddr = raw & ~0xffULL;
  if (fault_cause) *fault_cause = (uint32_t)(raw & 0xffULL);
#else
  (void)manager_id;
  if (fault_vaddr) *fault_vaddr = 0;
  if (fault_cause) *fault_cause = 0;
#endif
  return PRT_OK;
}
