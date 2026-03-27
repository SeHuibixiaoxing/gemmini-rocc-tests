#ifndef PRT_SCHEDULE_ACTION_H
#define PRT_SCHEDULE_ACTION_H

#include <stddef.h>

#include "prt_page_table.h"
#include "prt_types.h"

#ifdef __cplusplus
extern "C" {
#endif

struct prt_runtime_s;
typedef struct prt_runtime_s prt_runtime_t;
struct prt_action_exec_s;
typedef struct prt_action_exec_s prt_action_exec_t;

typedef enum {
  PRT_ACTION_CREATED = 0,
  PRT_ACTION_ALLOCATED = 1,
  PRT_ACTION_RUNNING = 2,
  PRT_ACTION_RELEASED = 3
} prt_action_state_t;

typedef struct {
  uint32_t stage_id;
  uint32_t acc_util;
  uint32_t *gemmini_mgr_ids;
  uint32_t *dma_mgr_ids;
} prt_stage_acc_assign_t;

typedef struct {
  uint32_t num_acc;
  uint32_t stage_count;
  prt_stage_acc_assign_t *stage_assign;
  uint32_t all_count;
  uint32_t *all_gemmini_mgr_ids;
  uint32_t *all_dma_mgr_ids;
} prt_acc_source_t;

typedef struct {
  uint32_t buffer_id;
  uint32_t tensor_id;
  uint32_t stage_id;
  uint32_t slot_id;
  prt_page_list_t pages;
} prt_spm_page_binding_t;

typedef struct {
  uint32_t num_spm_pages;
  uint32_t *alloc_keys;
  uint32_t alloc_count;
  uint32_t alloc_cap;
  prt_spm_page_binding_t *weight_pages;
  uint32_t weight_count;
  uint32_t weight_cap;
  prt_spm_page_binding_t *in_stage_pages;
  uint32_t in_stage_count;
  uint32_t in_stage_cap;
  prt_spm_page_binding_t *ring_pages;
  uint32_t ring_count;
  uint32_t ring_cap;
} prt_spm_source_t;

typedef struct prt_schedule_action_s {
  uint32_t action_id;
  uint32_t segment_idx;
  const prt_segment_desc_t *pipeline_segment_ref;
  const prt_model_desc_t *model_ref;
  prt_action_exec_t *exec;
  prt_acc_source_t acc_source;
  prt_spm_source_t spm_source;
  prt_spm_xlate_ctx_t spm_xlate;
  uint64_t alias_base_va;
  uint64_t alias_bytes;
  uint32_t alias_vpage_start;
  uint32_t alias_page_count;
  void *alias_alloc;
  size_t alias_alloc_bytes;
  uint64_t spm_ptbr_pa;
  uint32_t spm_pte_count;
  uint64_t spm_fault_count;
  uint64_t spm_last_fault_vaddr;
  uint32_t spm_last_fault_cause;
  prt_action_state_t state;

  // Per-action SPM context
  uint64_t *spm_pte_private;
  uint32_t spm_pte_private_cap;
  void *spm_pte_private_alloc;
  size_t spm_pte_private_alloc_bytes;
  uint32_t assigned_hart_id;
} prt_schedule_action_t;

int prt_action_generate(prt_runtime_t *rt, uint32_t segment_idx,
                        const prt_segment_desc_t *segment,
                        const prt_model_desc_t *model,
                        prt_schedule_action_t **out_action);
int prt_action_alloc_acc(prt_runtime_t *rt, prt_schedule_action_t *action);
int prt_action_alloc_spm(prt_runtime_t *rt, prt_schedule_action_t *action);
int prt_action_bind_topology(prt_runtime_t *rt, prt_schedule_action_t *action);
int prt_action_track_alloc_key(prt_schedule_action_t *action, uint32_t key);
int prt_action_release(prt_runtime_t *rt, prt_schedule_action_t **action_ptr);
int prt_action_to_json(const prt_schedule_action_t *action, char *buf, size_t buf_size);

int prt_action_alloc_private_spm_context(prt_runtime_t *rt, prt_schedule_action_t *action);
int prt_action_install_spm_context(prt_runtime_t *rt, prt_schedule_action_t *action);
void prt_action_release_private_spm_context(prt_schedule_action_t *action);

#ifdef __cplusplus
}
#endif

#endif
