#ifndef PRT_DMA_H
#define PRT_DMA_H

#include "prt_types.h"
#include "prt_page_table.h"

#ifdef __cplusplus
extern "C" {
#endif

struct prt_runtime_s;
typedef struct prt_runtime_s prt_runtime_t;

typedef struct {
  int (*submit)(prt_runtime_t *rt, const prt_dma_req_t *req, prt_dma_token_t *tok);
  int (*wait)(prt_runtime_t *rt, prt_dma_token_t *tok, uint64_t timeout_ns);
  int (*submit_and_wait)(prt_runtime_t *rt, const prt_dma_req_t *req, uint64_t timeout_ns);
  const char *name;
} prt_dma_backend_ops_t;

int prt_dma_backend_init(prt_runtime_t *rt);
void prt_dma_backend_destroy(prt_runtime_t *rt);

int prt_dma_submit(prt_runtime_t *rt, const prt_dma_req_t *req, prt_dma_token_t *tok);
int prt_dma_wait(prt_runtime_t *rt, prt_dma_token_t *tok, uint64_t timeout_ns);
int prt_dma_try_wait(prt_runtime_t *rt, prt_dma_token_t *tok);
int prt_dma_token_cleanup(prt_dma_token_t *tok);
int prt_dma_submit_and_wait(prt_runtime_t *rt, const prt_dma_req_t *req, uint64_t timeout_ns);
int prt_dma_copy_spm_va(prt_runtime_t *rt, uint64_t dst_va, uint64_t src_va, uint64_t bytes,
                        uint32_t manager_id, uint32_t stage_idx, uint32_t tensor_id,
                        uint64_t timeout_ns);
int prt_dma_copy_spm_pages(prt_runtime_t *rt, const prt_page_list_t *dst_pages,
                           const prt_page_list_t *src_pages, uint32_t manager_id,
                           uint32_t stage_idx, uint32_t tensor_id, uint64_t timeout_ns);
int prt_dma_copy_spm_pages_prefix(prt_runtime_t *rt, const prt_page_list_t *dst_pages,
                                  const prt_page_list_t *src_pages, uint64_t bytes,
                                  uint32_t manager_id, uint32_t stage_idx,
                                  uint32_t tensor_id, uint64_t timeout_ns);
int prt_dma_copy_dram_to_spm_pages(prt_runtime_t *rt, const prt_page_list_t *dst_pages,
                                   uint64_t src_dram_addr, uint32_t manager_id,
                                   uint32_t stage_idx, uint32_t tensor_id, uint64_t timeout_ns);
int prt_dma_copy_dram_to_spm_pages_prefix(prt_runtime_t *rt, const prt_page_list_t *dst_pages,
                                          uint64_t src_dram_addr, uint64_t bytes,
                                          uint32_t manager_id, uint32_t stage_idx,
                                          uint32_t tensor_id, uint64_t timeout_ns);
int prt_dma_copy_spm_pages_to_dram(prt_runtime_t *rt, uint64_t dst_dram_addr,
                                   const prt_page_list_t *src_pages, uint32_t manager_id,
                                   uint32_t stage_idx, uint32_t tensor_id, uint64_t timeout_ns);
int prt_dma_copy_spm_pages_to_dram_prefix(prt_runtime_t *rt, uint64_t dst_dram_addr,
                                          const prt_page_list_t *src_pages, uint64_t bytes,
                                          uint32_t manager_id, uint32_t stage_idx,
                                          uint32_t tensor_id, uint64_t timeout_ns);

#ifdef __cplusplus
}
#endif

#endif
