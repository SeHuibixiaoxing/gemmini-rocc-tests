#ifndef PRT_PAGE_TABLE_H
#define PRT_PAGE_TABLE_H

#include "prt_types.h"

#ifdef __cplusplus
extern "C" {
#endif

struct prt_runtime_s;
typedef struct prt_runtime_s prt_runtime_t;

typedef struct {
  uint32_t tensor_id;
  uint32_t refcnt;
  prt_page_list_t pages;
} prt_tensor_alloc_t;

typedef struct {
  uint32_t start;
  uint32_t count;
} prt_vmap_desc_t;

typedef struct {
  uint64_t vaddr;
  uint64_t paddr;
  uint32_t bytes;
} prt_spm_xlate_seg_t;

typedef struct {
  uint32_t tensor_id;
  uint32_t vpage_start;
  uint32_t page_count;
  uint32_t refcnt;
} prt_spm_tensor_map_t;

int prt_page_table_init(prt_runtime_t *rt);
void prt_page_table_destroy(prt_runtime_t *rt);

int prt_alloc_tensor_pages(prt_runtime_t *rt, uint32_t tensor_id, size_t bytes,
                           const uint32_t *preferred_accs, uint32_t preferred_cnt,
                           prt_page_list_t *out);
int prt_release_tensor_pages(prt_runtime_t *rt, uint32_t tensor_id);

int prt_map_tensor_for_acc(prt_runtime_t *rt, uint32_t tensor_id, uint32_t acc_id,
                           prt_vmap_desc_t *out);
int prt_unmap_tensor_for_acc(prt_runtime_t *rt, uint32_t acc_id,
                             const prt_vmap_desc_t *map);

int prt_spm_map_tensor(prt_runtime_t *rt, uint32_t tensor_id, const prt_page_list_t *pages,
                       uint64_t *out_va_base);
int prt_spm_unmap_tensor(prt_runtime_t *rt, uint32_t tensor_id);
int prt_spm_translate_range(prt_runtime_t *rt, uint64_t vaddr, uint64_t bytes,
                            prt_spm_xlate_seg_t *segs, uint32_t seg_cap, uint32_t *out_seg_count);
uint64_t prt_spm_ptbr_pa(const prt_runtime_t *rt);
uint32_t prt_spm_pte_count(const prt_runtime_t *rt);
uint64_t prt_spm_fault_count(const prt_runtime_t *rt);
uint64_t prt_spm_last_fault_vaddr(const prt_runtime_t *rt);
uint32_t prt_spm_last_fault_cause(const prt_runtime_t *rt);

#ifdef __cplusplus
}
#endif

#endif
