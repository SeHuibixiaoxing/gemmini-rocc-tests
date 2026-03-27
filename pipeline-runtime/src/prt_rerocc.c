#include "prt_rerocc.h"

#include <string.h>

#include "prt_error.h"
#include "prt_progress.h"

#if defined(__riscv)
#include "rerocc-linux-tests/rerocc_control.h"
#include "include/rerocc_gemmini_spm_xlate.h"
#endif

#if defined(__riscv) && !defined(PRT_ENABLE_GEMMINI_SPM_XLATE_INSN)
#define PRT_ENABLE_GEMMINI_SPM_XLATE_INSN 1
#endif

#define PRT_RR_ACQUIRE_MAX_RETRIES 1000000UL
#if !defined(RR_MAX_CFGS)
#define RR_MAX_CFGS 16U
#endif
#define PRT_RR_SPM_XLATE_CFG_ID (RR_MAX_CFGS - 1U)

static uint32_t rr_cfg_id_for_stage(uint32_t stage_id, uint32_t opcode_id) {
  uint32_t lane = opcode_id == 2U ? 0U : 1U;
  return ((stage_id * 2U) + lane) % RR_MAX_CFGS;
}

static int prt_rr_acquire_scope_cfg(prt_runtime_t *rt, uint32_t cfg_id,
                                    uint32_t stage_id, uint32_t manager_id,
                                    uint32_t opcode_id, prt_rr_scope_t *scope);

#if defined(__riscv)
static uint64_t rr_read_opcode_binding(uint32_t opcode_id) {
  return rr_read_csr(CSR_RROPC0 + opcode_id);
}

static void rr_restore_opcode_binding(uint32_t opcode_id, uint64_t binding) {
  rr_write_csr(CSR_RROPC0 + opcode_id, binding);
}

static int prt_spm_xlate_acquire_scope(uint32_t manager_id, prt_rr_scope_t *scope,
                                       uint64_t *prev_binding) {
  int rc;
  if (!scope) return PRT_ERR_INVAL;
  if (prev_binding) *prev_binding = rr_read_opcode_binding(3U);
  rc = prt_rr_acquire_scope_cfg(NULL, PRT_RR_SPM_XLATE_CFG_ID, UINT32_MAX,
                                manager_id, 3U, scope);
  return rc;
}

static void prt_spm_xlate_release_scope(prt_rr_scope_t *scope, uint64_t prev_binding) {
  if (!scope || !scope->valid) return;
  rr_fence(scope->cfg_id);
  (void)prt_rr_release_scope(scope);
  rr_restore_opcode_binding(3U, prev_binding);
}
#endif

static int prt_rr_acquire_scope_cfg(prt_runtime_t *rt, uint32_t cfg_id,
                                    uint32_t stage_id, uint32_t manager_id,
                                    uint32_t opcode_id, prt_rr_scope_t *scope) {
  (void)rt;
  if (!scope) return PRT_ERR_INVAL;
  memset(scope, 0, sizeof(*scope));
  scope->cfg_id = cfg_id;
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

int prt_rr_acquire_scope(prt_runtime_t *rt, uint32_t stage_id,
                         uint32_t manager_id, uint32_t opcode_id,
                         prt_rr_scope_t *scope) {
  return prt_rr_acquire_scope_cfg(rt, rr_cfg_id_for_stage(stage_id, opcode_id),
                                  stage_id, manager_id, opcode_id, scope);
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

int prt_gemmini_spm_xlate_program(uint32_t manager_id, uint64_t ptbr_pa,
                                  uint32_t pte_count, uint32_t page_shift,
                                  uint64_t range_base, uint64_t range_size,
                                  uint32_t enable) {
#if defined(__riscv) && PRT_ENABLE_GEMMINI_SPM_XLATE_INSN
  prt_rr_scope_t scope;
  uint64_t prev_binding = 0;
  int rc = prt_spm_xlate_acquire_scope(manager_id, &scope, &prev_binding);
  if (rc != PRT_OK) return rc;
  PRT_PROGRESS_LOG("spm-xlate-program mgr=%u cfg=%u prev_opc3=0x%llx ptbr=0x%llx ptes=%u page_shift=%u enable=%u base=0x%llx bytes=%llu",
                   manager_id, scope.cfg_id,
                   (unsigned long long)prev_binding,
                   (unsigned long long)ptbr_pa,
                   pte_count, page_shift, enable,
                   (unsigned long long)range_base,
                   (unsigned long long)range_size);
  rerocc_gemmini_spm_xlate_cfg(ptbr_pa, pte_count, page_shift, enable);
  rerocc_gemmini_spm_xlate_range(range_base, range_size);
  rerocc_gemmini_spm_xlate_flush();
  prt_spm_xlate_release_scope(&scope, prev_binding);
  PRT_PROGRESS_LOG("spm-xlate-program-restore mgr=%u opc3=0x%llx",
                   manager_id, (unsigned long long)prev_binding);
#else
  (void)manager_id;
  (void)ptbr_pa;
  (void)pte_count;
  (void)page_shift;
  (void)range_base;
  (void)range_size;
  (void)enable;
#endif
  return PRT_OK;
}

int prt_gemmini_spm_xlate_reset(uint32_t manager_id, uint32_t page_shift) {
  return prt_gemmini_spm_xlate_program(manager_id, 0ULL, 0U, page_shift, 0ULL, 0ULL, 0U);
}

int prt_gemmini_spm_xlate_cfg(uint32_t manager_id, uint64_t ptbr_pa,
                              uint32_t pte_count, uint32_t page_shift, uint32_t enable) {
#if defined(__riscv) && PRT_ENABLE_GEMMINI_SPM_XLATE_INSN
  prt_rr_scope_t scope;
  uint64_t prev_binding = 0;
  int rc = prt_spm_xlate_acquire_scope(manager_id, &scope, &prev_binding);
  if (rc != PRT_OK) return rc;
  PRT_PROGRESS_LOG("spm-xlate-cfg mgr=%u cfg=%u prev_opc3=0x%llx ptbr=0x%llx ptes=%u page_shift=%u enable=%u",
                   manager_id, scope.cfg_id,
                   (unsigned long long)prev_binding,
                   (unsigned long long)ptbr_pa,
                   pte_count, page_shift, enable);
  rerocc_gemmini_spm_xlate_cfg(ptbr_pa, pte_count, page_shift, enable);
  prt_spm_xlate_release_scope(&scope, prev_binding);
  PRT_PROGRESS_LOG("spm-xlate-cfg-restore mgr=%u opc3=0x%llx",
                   manager_id, (unsigned long long)prev_binding);
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
#if defined(__riscv) && PRT_ENABLE_GEMMINI_SPM_XLATE_INSN
  prt_rr_scope_t scope;
  uint64_t prev_binding = 0;
  int rc = prt_spm_xlate_acquire_scope(manager_id, &scope, &prev_binding);
  if (rc != PRT_OK) return rc;
  PRT_PROGRESS_LOG("spm-xlate-range mgr=%u cfg=%u prev_opc3=0x%llx base=0x%llx bytes=%llu",
                   manager_id, scope.cfg_id,
                   (unsigned long long)prev_binding,
                   (unsigned long long)range_base,
                   (unsigned long long)range_size);
  rerocc_gemmini_spm_xlate_range(range_base, range_size);
  prt_spm_xlate_release_scope(&scope, prev_binding);
  PRT_PROGRESS_LOG("spm-xlate-range-restore mgr=%u opc3=0x%llx",
                   manager_id, (unsigned long long)prev_binding);
#else
  (void)manager_id;
  (void)range_base;
  (void)range_size;
#endif
  return PRT_OK;
}

int prt_gemmini_spm_xlate_flush(uint32_t manager_id) {
#if defined(__riscv) && PRT_ENABLE_GEMMINI_SPM_XLATE_INSN
  prt_rr_scope_t scope;
  uint64_t prev_binding = 0;
  int rc = prt_spm_xlate_acquire_scope(manager_id, &scope, &prev_binding);
  if (rc != PRT_OK) return rc;
  PRT_PROGRESS_LOG("spm-xlate-flush mgr=%u cfg=%u prev_opc3=0x%llx",
                   manager_id, scope.cfg_id, (unsigned long long)prev_binding);
  rerocc_gemmini_spm_xlate_flush();
  prt_spm_xlate_release_scope(&scope, prev_binding);
  PRT_PROGRESS_LOG("spm-xlate-flush-restore mgr=%u opc3=0x%llx",
                   manager_id, (unsigned long long)prev_binding);
#else
  (void)manager_id;
#endif
  return PRT_OK;
}

int prt_gemmini_spm_xlate_fault_read(uint32_t manager_id, uint64_t *fault_vaddr,
                                     uint32_t *fault_cause) {
#if defined(__riscv) && PRT_ENABLE_GEMMINI_SPM_XLATE_INSN
  uint64_t raw;
  prt_rr_scope_t scope;
  uint64_t prev_binding = 0;
  int rc = prt_spm_xlate_acquire_scope(manager_id, &scope, &prev_binding);
  if (rc != PRT_OK) return rc;
  raw = rerocc_gemmini_spm_xlate_fault();
  prt_spm_xlate_release_scope(&scope, prev_binding);
  if (fault_vaddr) *fault_vaddr = raw & ~0xffULL;
  if (fault_cause) *fault_cause = (uint32_t)(raw & 0xffULL);
#else
  (void)manager_id;
  if (fault_vaddr) *fault_vaddr = 0;
  if (fault_cause) *fault_cause = 0;
#endif
  return PRT_OK;
}

int prt_gemmini_spm_xlate_fault_read_scoped(const prt_rr_scope_t *scope,
                                            uint64_t *fault_vaddr,
                                            uint32_t *fault_cause) {
#if defined(__riscv) && PRT_ENABLE_GEMMINI_SPM_XLATE_INSN
  uint64_t raw;
  if (!scope || !scope->valid || scope->opcode_id != 3U) return PRT_ERR_INVAL;
  raw = rerocc_gemmini_spm_xlate_fault();
  if (fault_vaddr) *fault_vaddr = raw & ~0xffULL;
  if (fault_cause) *fault_cause = (uint32_t)(raw & 0xffULL);
#else
  (void)scope;
  if (fault_vaddr) *fault_vaddr = 0;
  if (fault_cause) *fault_cause = 0;
#endif
  return PRT_OK;
}
