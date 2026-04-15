#include "prt_rerocc.h"

#include <string.h>

#include "prt_breadcrumb.h"
#include "prt_error.h"
#include "prt_progress.h"
#include "prt_trigger_log.h"

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

static void prt_rr_breadcrumb_note_acquire(uint32_t phase,
                                           const prt_rr_scope_t *scope,
                                           uint32_t csr_id,
                                           uint64_t wdata,
                                           uint64_t cfg_state,
                                           uint64_t retries,
                                           int rc,
                                           uint32_t extra_flags,
                                           uint32_t line) {
  uint32_t flags = extra_flags;
  uint32_t token_id = 0U;
  uint32_t manager = PRT_BREADCRUMB_ANY_U32;
  if (scope) {
    token_id = scope->cfg_id;
    manager = scope->manager_id;
    if (scope->valid) flags |= PRT_BREADCRUMB_FLAG_SCOPE_VALID;
  }
  prt_breadcrumb_note(PRT_BREADCRUMB_KIND_RR, phase,
                      PRT_BREADCRUMB_ANY_U32, token_id, manager,
                      PRT_BREADCRUMB_ANY_U32, rc, flags,
                      (uint64_t)csr_id, wdata, cfg_state, retries, line);
}

static void prt_rr_breadcrumb_note_release(uint32_t phase,
                                           const prt_rr_scope_t *scope,
                                           uint32_t line) {
  uint32_t flags = 0U;
  uint32_t token_id = 0U;
  uint32_t manager = PRT_BREADCRUMB_ANY_U32;
  uint32_t csr_id = 0U;
  if (scope) {
    token_id = scope->cfg_id;
    manager = scope->manager_id;
#if defined(__riscv)
    csr_id = CSR_RRCFG0 + scope->cfg_id;
#endif
    if (scope->valid) flags |= PRT_BREADCRUMB_FLAG_SCOPE_VALID;
  }
  prt_breadcrumb_note(PRT_BREADCRUMB_KIND_RR, phase,
                      PRT_BREADCRUMB_ANY_U32, token_id, manager,
                      PRT_BREADCRUMB_ANY_U32, PRT_OK, flags,
                      (uint64_t)csr_id, 0ULL, 0ULL, 0ULL, line);
}

static void prt_rr_trigger_note(const char *phase,
                                uint32_t stage_id,
                                uint32_t manager_id,
                                uint32_t opcode_id,
                                uint32_t cfg_id,
                                int rc) {
  prt_trigger_log_note(&(const prt_trigger_log_event_t){
    .family = PRT_TRIGGER_LOG_FAMILY_RR,
    .phase = phase,
    .segment_idx = PRT_TRIGGER_LOG_ANY_U32,
    .global_stage_id = stage_id,
    .local_stage_id = stage_id,
    .subbatch_id = PRT_TRIGGER_LOG_ANY_U32,
    .manager_id = manager_id,
    .tensor_id = PRT_TRIGGER_LOG_ANY_U32,
    .page_idx = PRT_TRIGGER_LOG_ANY_U32,
    .token_id = cfg_id != UINT32_MAX ? cfg_id : opcode_id,
    .rc = rc,
  });
}

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
  prt_breadcrumb_note(PRT_BREADCRUMB_KIND_SPM_XLATE,
                      PRT_BREADCRUMB_PHASE_SPM_XLATE_RELEASE_FENCE_BEGIN,
                      PRT_BREADCRUMB_ANY_U32, 0U, scope->manager_id,
                      PRT_BREADCRUMB_ANY_U32, PRT_OK,
                      PRT_BREADCRUMB_FLAG_SCOPE_VALID,
                      0ULL, 0ULL, prev_binding, scope->cfg_id, __LINE__);
  PRT_PROGRESS_LOG("spm-xlate-release mgr=%u cfg=%u phase=fence-begin prev_opc3=0x%llx",
                   scope->manager_id, scope->cfg_id,
                   (unsigned long long)prev_binding);
  PRT_PROGRESS_RAW_LINE("[prt-raw] sxr-b");
  rr_fence(scope->cfg_id);
  prt_breadcrumb_note(PRT_BREADCRUMB_KIND_SPM_XLATE,
                      PRT_BREADCRUMB_PHASE_SPM_XLATE_RELEASE_FENCE_END,
                      PRT_BREADCRUMB_ANY_U32, 0U, scope->manager_id,
                      PRT_BREADCRUMB_ANY_U32, PRT_OK,
                      PRT_BREADCRUMB_FLAG_SCOPE_VALID,
                      0ULL, 0ULL, prev_binding, scope->cfg_id, __LINE__);
  PRT_PROGRESS_LOG("spm-xlate-release mgr=%u cfg=%u phase=fence-end prev_opc3=0x%llx",
                   scope->manager_id, scope->cfg_id,
                   (unsigned long long)prev_binding);
  PRT_PROGRESS_RAW_LINE("[prt-raw] sxr-f");
  prt_breadcrumb_note(PRT_BREADCRUMB_KIND_SPM_XLATE,
                      PRT_BREADCRUMB_PHASE_SPM_XLATE_RELEASE_BEGIN,
                      PRT_BREADCRUMB_ANY_U32, 0U, scope->manager_id,
                      PRT_BREADCRUMB_ANY_U32, PRT_OK,
                      PRT_BREADCRUMB_FLAG_SCOPE_VALID,
                      0ULL, 0ULL, prev_binding, scope->cfg_id, __LINE__);
  PRT_PROGRESS_LOG("spm-xlate-release mgr=%u cfg=%u phase=release-begin prev_opc3=0x%llx",
                   scope->manager_id, scope->cfg_id,
                   (unsigned long long)prev_binding);
  (void)prt_rr_release_scope(scope);
  prt_breadcrumb_note(PRT_BREADCRUMB_KIND_SPM_XLATE,
                      PRT_BREADCRUMB_PHASE_SPM_XLATE_RELEASE_END,
                      PRT_BREADCRUMB_ANY_U32, 0U, scope->manager_id,
                      PRT_BREADCRUMB_ANY_U32, PRT_OK,
                      0U,
                      0ULL, 0ULL, prev_binding, scope->cfg_id, __LINE__);
  PRT_PROGRESS_LOG("spm-xlate-release mgr=%u cfg=%u phase=release-end prev_opc3=0x%llx",
                   scope->manager_id, scope->cfg_id,
                   (unsigned long long)prev_binding);
  PRT_PROGRESS_RAW_LINE("[prt-raw] sxr-r");
  prt_breadcrumb_note(PRT_BREADCRUMB_KIND_SPM_XLATE,
                      PRT_BREADCRUMB_PHASE_SPM_XLATE_RESTORE_BEGIN,
                      PRT_BREADCRUMB_ANY_U32, 0U, scope->manager_id,
                      PRT_BREADCRUMB_ANY_U32, PRT_OK,
                      0U,
                      0ULL, 0ULL, prev_binding, scope->cfg_id, __LINE__);
  PRT_PROGRESS_LOG("spm-xlate-release mgr=%u cfg=%u phase=restore-begin prev_opc3=0x%llx",
                   scope->manager_id, scope->cfg_id,
                   (unsigned long long)prev_binding);
  rr_restore_opcode_binding(3U, prev_binding);
  prt_breadcrumb_note(PRT_BREADCRUMB_KIND_SPM_XLATE,
                      PRT_BREADCRUMB_PHASE_SPM_XLATE_RESTORE_END,
                      PRT_BREADCRUMB_ANY_U32, 0U, scope->manager_id,
                      PRT_BREADCRUMB_ANY_U32, PRT_OK,
                      0U,
                      0ULL, 0ULL, prev_binding, scope->cfg_id, __LINE__);
  PRT_PROGRESS_LOG("spm-xlate-release mgr=%u cfg=%u phase=restore-end prev_opc3=0x%llx",
                   scope->manager_id, scope->cfg_id,
                   (unsigned long long)prev_binding);
  PRT_PROGRESS_RAW_LINE("[prt-raw] sxr-e");
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
    const int sparse_probe = (stage_id == 0U && opcode_id == 2U);
    const int checkpoint_probe = (stage_id == 0U && opcode_id == 2U);
    const uint32_t csr_id = CSR_RRCFG0 + scope->cfg_id;
    const uint64_t wdata = RR_CFG_ACQ_MASK | (manager_id & RR_CFG_MGR_MASK);
    unsigned long retries = 0;
    uint64_t cfg_state = 0;
    int acquired = 0;

    while (!acquired) {
      prt_rr_breadcrumb_note_acquire(PRT_BREADCRUMB_PHASE_RR_ACQUIRE_BEFORE_CSR_WRITE,
                                     scope, csr_id, wdata, cfg_state, retries,
                                     PRT_OK, 0U, __LINE__);
      if (sparse_probe && retries == 0UL) {
        PRT_PROGRESS_LOG("rr-acquire-inner phase=before-csr-write stage=%u manager=%u opcode=%u cfg=%u csr=0x%x wdata=0x%llx",
                         stage_id, manager_id, opcode_id, scope->cfg_id, csr_id,
                         (unsigned long long)wdata);
      }
      if (checkpoint_probe && retries == 0UL) {
        PRT_CHECKPOINT_LOG("rr-acquire-inner checkpoint=before-csr-write stage=%u manager=%u opcode=%u cfg=%u csr=0x%x wdata=0x%llx",
                           stage_id, manager_id, opcode_id, scope->cfg_id, csr_id,
                           (unsigned long long)wdata);
      }
      rr_write_csr(csr_id, wdata);
      prt_rr_breadcrumb_note_acquire(PRT_BREADCRUMB_PHASE_RR_ACQUIRE_AFTER_CSR_WRITE,
                                     scope, csr_id, wdata, cfg_state, retries,
                                     PRT_OK, 0U, __LINE__);
      if (sparse_probe && retries == 0UL) {
        PRT_PROGRESS_LOG("rr-acquire-inner phase=after-csr-write stage=%u manager=%u opcode=%u cfg=%u csr=0x%x",
                         stage_id, manager_id, opcode_id, scope->cfg_id, csr_id);
      }
      if (checkpoint_probe && retries == 0UL) {
        PRT_CHECKPOINT_LOG("rr-acquire-inner checkpoint=after-csr-write stage=%u manager=%u opcode=%u cfg=%u csr=0x%x",
                           stage_id, manager_id, opcode_id, scope->cfg_id, csr_id);
      }
      cfg_state = rr_read_csr(csr_id);
      acquired = (cfg_state & RR_CFG_ACQ_MASK) != 0U;
      prt_rr_breadcrumb_note_acquire(PRT_BREADCRUMB_PHASE_RR_ACQUIRE_AFTER_CSR_READ,
                                     scope, csr_id, wdata, cfg_state, retries,
                                     PRT_OK, 0U, __LINE__);
      if (sparse_probe && retries == 0UL) {
        PRT_PROGRESS_LOG("rr-acquire-inner phase=after-csr-read stage=%u manager=%u opcode=%u cfg=%u csr=0x%x cfg_state=0x%llx acquired=%u",
                         stage_id, manager_id, opcode_id, scope->cfg_id, csr_id,
                         (unsigned long long)cfg_state, (uint32_t)acquired);
      }
      if (checkpoint_probe && retries == 0UL) {
        PRT_CHECKPOINT_LOG("rr-acquire-inner checkpoint=after-csr-read stage=%u manager=%u opcode=%u cfg=%u csr=0x%x cfg_state=0x%llx acquired=%u",
                           stage_id, manager_id, opcode_id, scope->cfg_id, csr_id,
                           (unsigned long long)cfg_state, (uint32_t)acquired);
      }
      if (acquired) break;
      retries += 1;
      if (checkpoint_probe &&
          (retries == 1UL || retries == 1000UL || retries == 10000UL || (retries % 100000UL) == 0UL)) {
        PRT_CHECKPOINT_LOG("rr-acquire-inner checkpoint=retry stage=%u manager=%u opcode=%u cfg=%u retries=%lu cfg_state=0x%llx",
                           stage_id, manager_id, opcode_id, scope->cfg_id, retries,
                           (unsigned long long)cfg_state);
      }
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
    if (sparse_probe) {
      PRT_PROGRESS_LOG("rr-acquire-inner phase=before-set-opc stage=%u manager=%u opcode=%u cfg=%u acquired_state=0x%llx retries=%lu",
                       stage_id, manager_id, opcode_id, scope->cfg_id,
                       (unsigned long long)cfg_state, retries);
    }
    prt_rr_breadcrumb_note_acquire(PRT_BREADCRUMB_PHASE_RR_ACQUIRE_BEFORE_SET_OPC,
                                   scope, csr_id, wdata, cfg_state, retries,
                                   PRT_OK, 0U, __LINE__);
    if (checkpoint_probe) {
      PRT_CHECKPOINT_LOG("rr-acquire-inner checkpoint=before-set-opc stage=%u manager=%u opcode=%u cfg=%u acquired_state=0x%llx retries=%lu",
                         stage_id, manager_id, opcode_id, scope->cfg_id,
                         (unsigned long long)cfg_state, retries);
    }
  rr_set_opc((uint8_t)opcode_id, scope->cfg_id);
  prt_rr_trigger_note("acq-opc", stage_id, manager_id, opcode_id, scope->cfg_id, PRT_OK);
  prt_rr_breadcrumb_note_acquire(PRT_BREADCRUMB_PHASE_RR_ACQUIRE_AFTER_SET_OPC,
                                 scope, CSR_RROPC0 + opcode_id, scope->cfg_id,
                                 0ULL, 0ULL, PRT_OK, 0U, __LINE__);
  if (stage_id == 0U && opcode_id == 2U) {
    PRT_CHECKPOINT_LOG("rr-acquire-inner checkpoint=after-set-opc stage=%u manager=%u opcode=%u cfg=%u",
                       stage_id, manager_id, opcode_id, scope->cfg_id);
    PRT_PROGRESS_LOG("rr-acquire-inner phase=after-set-opc stage=%u manager=%u opcode=%u cfg=%u",
                     stage_id, manager_id, opcode_id, scope->cfg_id);
    PRT_CHECKPOINT_LOG("rr-acquire-inner checkpoint=before-scope-valid stage=%u manager=%u opcode=%u cfg=%u",
                       stage_id, manager_id, opcode_id, scope->cfg_id);
    PRT_PROGRESS_LOG("rr-acquire-inner phase=before-scope-valid stage=%u manager=%u opcode=%u cfg=%u",
                     stage_id, manager_id, opcode_id, scope->cfg_id);
  }
  scope->valid = 1;
  prt_rr_breadcrumb_note_acquire(PRT_BREADCRUMB_PHASE_RR_ACQUIRE_BEFORE_RETURN,
                                 scope, csr_id, wdata, cfg_state, retries,
                                 PRT_OK, PRT_BREADCRUMB_FLAG_SCOPE_VALID, __LINE__);
  if (stage_id == 0U && opcode_id == 2U) {
    PRT_CHECKPOINT_LOG("rr-acquire-inner checkpoint=after-scope-valid stage=%u manager=%u opcode=%u cfg=%u valid=%u",
                       stage_id, manager_id, opcode_id, scope->cfg_id, (uint32_t)scope->valid);
    PRT_PROGRESS_LOG("rr-acquire-inner phase=after-scope-valid stage=%u manager=%u opcode=%u cfg=%u valid=%u",
                     stage_id, manager_id, opcode_id, scope->cfg_id, (uint32_t)scope->valid);
    PRT_CHECKPOINT_LOG("rr-acquire-inner checkpoint=before-return stage=%u manager=%u opcode=%u cfg=%u",
                       stage_id, manager_id, opcode_id, scope->cfg_id);
    PRT_PROGRESS_LOG("rr-acquire-inner phase=before-return stage=%u manager=%u opcode=%u cfg=%u",
                     stage_id, manager_id, opcode_id, scope->cfg_id);
  }
#else
  scope->valid = 0;
#endif
  return PRT_OK;
}

__attribute__((noinline))
int prt_rr_acquire_scope(prt_runtime_t *rt, uint32_t stage_id,
                         uint32_t manager_id, uint32_t opcode_id,
                         prt_rr_scope_t *scope) {
  int rc = prt_rr_acquire_scope_cfg(rt, rr_cfg_id_for_stage(stage_id, opcode_id),
                                    stage_id, manager_id, opcode_id, scope);
#if defined(__riscv)
  const uint32_t acquire_csr_id = scope ? (CSR_RRCFG0 + scope->cfg_id) : 0U;
#else
  const uint32_t acquire_csr_id = 0U;
#endif
  prt_rr_breadcrumb_note_acquire(PRT_BREADCRUMB_PHASE_RR_ACQUIRE_AFTER_CALL,
                                 scope, acquire_csr_id,
                                 0ULL, 0ULL, 0ULL, rc,
                                 (scope && scope->valid) ? PRT_BREADCRUMB_FLAG_SCOPE_VALID : 0U,
                                 __LINE__);
  prt_rr_trigger_note("acq-ret", stage_id, manager_id, opcode_id,
                      scope ? scope->cfg_id : UINT32_MAX, rc);
  if (stage_id == 0U && opcode_id == 2U) {
    PRT_PROGRESS_LOG("rr-acquire-wrap phase=after-call stage=%u manager=%u opcode=%u rc=%d cfg=%u valid=%u",
                     stage_id, manager_id, opcode_id, rc,
                     scope ? scope->cfg_id : UINT32_MAX,
                     scope ? (uint32_t)scope->valid : 0U);
  }
  return rc;
}

int prt_rr_fence_scope(prt_rr_scope_t *scope) {
  if (!scope) return PRT_ERR_INVAL;
#if defined(__riscv)
  if (scope->valid) {
    PRT_MARKER_LOG("rr-fence-scope stage=%u mgr=%u opcode=%u cfg=%u valid=%u begin",
                   scope->stage_id, scope->manager_id, scope->opcode_id,
                   scope->cfg_id, scope->valid);
    PRT_PROGRESS_RAW_LINE("[prt-raw] rrf-b");
    rr_fence(scope->cfg_id);
    PRT_PROGRESS_RAW_LINE("[prt-raw] rrf-e");
    PRT_MARKER_LOG("rr-fence-scope stage=%u mgr=%u opcode=%u cfg=%u valid=%u end",
                   scope->stage_id, scope->manager_id, scope->opcode_id,
                   scope->cfg_id, scope->valid);
  }
#endif
  return PRT_OK;
}

int prt_rr_release_scope(prt_rr_scope_t *scope) {
  if (!scope) return PRT_ERR_INVAL;
#if defined(__riscv)
  if (scope->valid) {
    prt_rr_trigger_note("rel-b", scope->stage_id, scope->manager_id,
                        scope->opcode_id, scope->cfg_id, PRT_OK);
    prt_rr_breadcrumb_note_release(PRT_BREADCRUMB_PHASE_RR_RELEASE_BEGIN, scope, __LINE__);
    rr_release(scope->cfg_id);
    scope->valid = 0;
    prt_rr_breadcrumb_note_release(PRT_BREADCRUMB_PHASE_RR_RELEASE_END, scope, __LINE__);
    prt_rr_trigger_note("rel-e", scope->stage_id, scope->manager_id,
                        scope->opcode_id, scope->cfg_id, PRT_OK);
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
  PRT_PROGRESS_RAW_LINE("[prt-raw] sxf-ab");
  int rc = prt_spm_xlate_acquire_scope(manager_id, &scope, &prev_binding);
  PRT_PROGRESS_RAW_LINE("[prt-raw] sxf-ae");
  if (rc != PRT_OK) return rc;
  prt_breadcrumb_note(PRT_BREADCRUMB_KIND_SPM_XLATE,
                      PRT_BREADCRUMB_PHASE_SPM_XLATE_FLUSH_BEGIN,
                      PRT_BREADCRUMB_ANY_U32, 0U, manager_id,
                      PRT_BREADCRUMB_ANY_U32, rc,
                      scope.valid ? PRT_BREADCRUMB_FLAG_SCOPE_VALID : 0U,
                      0ULL, 0ULL, prev_binding, scope.cfg_id, __LINE__);
  PRT_PROGRESS_LOG("spm-xlate-flush mgr=%u cfg=%u prev_opc3=0x%llx",
                   manager_id, scope.cfg_id, (unsigned long long)prev_binding);
  PRT_PROGRESS_LOG("spm-xlate-flush-call mgr=%u cfg=%u phase=issue-begin prev_opc3=0x%llx",
                   manager_id, scope.cfg_id, (unsigned long long)prev_binding);
  PRT_PROGRESS_RAW_LINE("[prt-raw] sxf-fb");
  rerocc_gemmini_spm_xlate_flush();
  prt_breadcrumb_note(PRT_BREADCRUMB_KIND_SPM_XLATE,
                      PRT_BREADCRUMB_PHASE_SPM_XLATE_FLUSH_END,
                      PRT_BREADCRUMB_ANY_U32, 0U, manager_id,
                      PRT_BREADCRUMB_ANY_U32, PRT_OK,
                      scope.valid ? PRT_BREADCRUMB_FLAG_SCOPE_VALID : 0U,
                      0ULL, 0ULL, prev_binding, scope.cfg_id, __LINE__);
  PRT_PROGRESS_LOG("spm-xlate-flush-call mgr=%u cfg=%u phase=issue-end prev_opc3=0x%llx",
                   manager_id, scope.cfg_id, (unsigned long long)prev_binding);
  PRT_PROGRESS_RAW_LINE("[prt-raw] sxf-fe");
  prt_spm_xlate_release_scope(&scope, prev_binding);
  PRT_PROGRESS_RAW_LINE("[prt-raw] sxf-re");
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
