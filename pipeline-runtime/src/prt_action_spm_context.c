#include "prt_schedule_action.h"
#include "prt_runtime.h"
#include "prt_page_table.h"
#include "prt_rerocc.h"
#include <stdlib.h>
#include <string.h>

int prt_action_alloc_private_spm_context(prt_runtime_t *rt, prt_schedule_action_t *action) {
  if (!rt || !action) return PRT_ERR_INVAL;
  if (!action->spm_xlate.pte || action->spm_xlate.pte_count == 0) return PRT_OK;
  if (action->spm_pte_private_alloc) {
    prt_action_release_private_spm_context(action);
  }

  uint32_t pte_count = action->spm_xlate.pte_count;
  size_t alloc_bytes = (size_t)pte_count * sizeof(uint64_t) + 4096;

  action->spm_pte_private_alloc = malloc(alloc_bytes);
  if (!action->spm_pte_private_alloc) return PRT_ERR_NOMEM;

  uintptr_t base = (uintptr_t)action->spm_pte_private_alloc;
  uintptr_t aligned = (base + 4095) & ~4095UL;
  action->spm_pte_private = (uint64_t *)aligned;
  action->spm_pte_private_cap = pte_count;
  action->spm_pte_private_alloc_bytes = alloc_bytes;

  memcpy(action->spm_pte_private, action->spm_xlate.pte, pte_count * sizeof(uint64_t));
  return PRT_OK;
}

int prt_action_install_spm_context(prt_runtime_t *rt, prt_schedule_action_t *action) {
  if (!rt || !action) return PRT_ERR_INVAL;
  if (!rt->cfg.spm_xlate_enable) return PRT_OK;
  if (!action->spm_xlate.pte || action->spm_xlate.pte_count == 0U) return PRT_OK;

  if (action->spm_pte_private) {
    uint32_t copy_count = action->spm_pte_private_cap;
    if (copy_count > action->spm_xlate.pte_count) copy_count = action->spm_xlate.pte_count;
    memcpy(action->spm_xlate.pte, action->spm_pte_private, (size_t)copy_count * sizeof(uint64_t));
  }
  prt_spm_xlate_ctx_publish(&action->spm_xlate);

  for (uint32_t i = 0; i < action->acc_source.all_count; i++) {
    uint32_t mgr_id = action->acc_source.all_gemmini_mgr_ids[i];
    int rc = prt_gemmini_spm_xlate_program(mgr_id, action->spm_xlate.ptbr_pa,
                                           action->spm_xlate.pte_count,
                                           rt->cfg.spm_page_shift,
                                           action->alias_base_va,
                                           action->alias_bytes,
                                           1U);
    if (rc != PRT_OK) return rc;
  }

  return PRT_OK;
}

void prt_action_release_private_spm_context(prt_schedule_action_t *action) {
  if (!action) return;
  free(action->spm_pte_private_alloc);
  action->spm_pte_private_alloc = NULL;
  action->spm_pte_private = NULL;
  action->spm_pte_private_cap = 0;
  action->spm_pte_private_alloc_bytes = 0;
}
