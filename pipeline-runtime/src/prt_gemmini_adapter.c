#include "prt_gemmini_adapter.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "prt_progress.h"
#include "prt_rerocc.h"
#include "prt_runtime.h"

#if defined(__riscv)
#include "include/gemmini.h"
#include "include/gemmini_nn.h"
#endif

#if defined(__riscv)
#ifndef PRT_GEMMINI_WS_SAFE_POINTWISE_KCHS_CAP
#define PRT_GEMMINI_WS_SAFE_POINTWISE_KCHS_CAP 16
#endif

#ifndef PRT_GEMMINI_SAFE_POINTWISE_OC_CHUNK
// FPGA runs showed direct OS fallback can still stall at J=128 after repeated
// split-OC iterations, so keep canonical pointwise fallbacks below that size.
#define PRT_GEMMINI_SAFE_POINTWISE_OC_CHUNK 64
#endif
#endif

static int gemm_blocking_conv_run(prt_runtime_t *rt, const prt_conv_task_t *task, uint64_t timeout_ns);
static int gemm_blocking_fence(prt_runtime_t *rt, const prt_conv_task_t *task, uint64_t timeout_ns);

static int gemm_async_conv_run(prt_runtime_t *rt, const prt_conv_task_t *task, uint64_t timeout_ns);
static int gemm_async_fence(prt_runtime_t *rt, const prt_conv_task_t *task, uint64_t timeout_ns);
static int gemm_issue_task(prt_runtime_t *rt, const prt_conv_task_t *task);
static int gemm_issue_conv_task(prt_runtime_t *rt, const prt_conv_task_t *task,
                                const prt_gemmini_conv_desc_t *conv);
static int gemm_issue_grouped_conv_task(prt_runtime_t *rt, const prt_conv_task_t *task,
                                        const prt_gemmini_conv_desc_t *conv);

static int task_issue_already_fenced(const prt_conv_task_t *task);
static void resadd_cpu_ref(const prt_gemmini_resadd_desc_t *resadd);

static void *prt_gemmini_alloc_aligned(size_t bytes, int zero_fill) {
  const size_t align_bytes = 64U;
  void *ptr = NULL;
  if (bytes == 0) return NULL;
  if (posix_memalign(&ptr, align_bytes, bytes) != 0) return NULL;
  if (zero_fill) memset(ptr, 0, bytes);
  return ptr;
}

static int prt_conv_input_stride(const prt_gemmini_conv_desc_t *conv) {
  if (!conv) return 0;
  return conv->in_stride > 0 ? conv->in_stride : conv->in_channels;
}

static int prt_conv_weight_stride(const prt_gemmini_conv_desc_t *conv) {
  if (!conv) return 0;
  return conv->weight_stride > 0 ? conv->weight_stride : conv->out_channels;
}

static int prt_conv_output_stride(const prt_gemmini_conv_desc_t *conv) {
  if (!conv) return 0;
  return conv->out_stride > 0 ? conv->out_stride : conv->out_channels;
}

static int prt_conv_groups(const prt_gemmini_conv_desc_t *conv) {
  if (!conv) return 1;
  return conv->groups > 0 ? conv->groups : 1;
}

static int prt_grouped_conv_uses_standard_layout(const prt_gemmini_conv_desc_t *conv) {
  if (!conv) return 0;
  return conv->wrot180 == 0 &&
         conv->trans_output_1203 == 0 &&
         conv->trans_input_3120 == 0 &&
         conv->trans_weight_1203 == 0 &&
         conv->trans_weight_0132 == 0;
}

static size_t prt_grouped_conv_weight_group_rows(const prt_gemmini_conv_desc_t *conv) {
  if (!conv) return 0U;
  return (size_t)conv->kernel_dim * (size_t)conv->kernel_dim * (size_t)conv->in_channels;
}

static const void *prt_grouped_conv_input_ptr(const prt_gemmini_conv_desc_t *conv, int group_idx) {
  const int8_t *input;
  if (!conv || !conv->input || group_idx < 0) return NULL;
  input = (const int8_t *)conv->input;
  return input + (size_t)group_idx * (size_t)conv->in_channels;
}

static const void *prt_grouped_conv_weight_ptr(const prt_gemmini_conv_desc_t *conv, int group_idx) {
  const int8_t *weights;
  size_t group_rows;
  size_t weight_stride;
  if (!conv || !conv->weights || group_idx < 0) return NULL;
  weights = (const int8_t *)conv->weights;
  group_rows = prt_grouped_conv_weight_group_rows(conv);
  weight_stride = (size_t)prt_conv_weight_stride(conv);
  return weights + (size_t)group_idx * group_rows * weight_stride;
}

static const void *prt_grouped_conv_bias_ptr(const prt_gemmini_conv_desc_t *conv, int group_idx) {
  const int32_t *bias;
  if (!conv || !conv->bias || group_idx < 0) return NULL;
  bias = (const int32_t *)conv->bias;
  return bias + (size_t)group_idx * (size_t)conv->out_channels;
}

static void *prt_grouped_conv_output_ptr(const prt_gemmini_conv_desc_t *conv, int group_idx) {
  int8_t *output;
  if (!conv || !conv->output || group_idx < 0) return NULL;
  output = (int8_t *)conv->output;
  return output + (size_t)group_idx * (size_t)conv->out_channels;
}

#if defined(__riscv)
static uint64_t prt_spm_xlate_range_base_cache = 0;
static uint64_t prt_spm_xlate_range_size_cache = 0;
static int prt_spm_xlate_enabled_cache = 0;

static void prt_cache_spm_xlate_cfg(const prt_runtime_t *rt) {
  if (!rt) return;
  prt_spm_xlate_range_base_cache = rt->cfg.spm_xlate_range_base;
  prt_spm_xlate_range_size_cache = rt->cfg.spm_xlate_range_size;
  prt_spm_xlate_enabled_cache = rt->cfg.spm_xlate_enable != 0;
}

static int prt_addr_uses_spm_xlate_alias(const void *ptr) {
  const uint64_t addr = (uint64_t)(uintptr_t)ptr;
  if (!ptr || !prt_spm_xlate_enabled_cache || prt_spm_xlate_range_size_cache == 0) return 0;
  if (prt_spm_xlate_range_base_cache == 0) {
    return addr < prt_spm_xlate_range_size_cache;
  }
  return addr >= prt_spm_xlate_range_base_cache &&
         addr < prt_spm_xlate_range_base_cache + prt_spm_xlate_range_size_cache;
}

static int prt_conv_uses_spm_xlate_alias(const prt_gemmini_conv_desc_t *conv) {
  if (!conv) return 0;
  return prt_addr_uses_spm_xlate_alias(conv->input) ||
         prt_addr_uses_spm_xlate_alias(conv->weights) ||
         prt_addr_uses_spm_xlate_alias(conv->bias) ||
         prt_addr_uses_spm_xlate_alias(conv->output);
}

static int flush_scope_after_drain(prt_rr_scope_t *scope) {
  int rc;
  if (!scope) return PRT_ERR_INVAL;
  gemmini_flush(0);
  rc = prt_rr_fence_scope(scope);
  gemmini_fence();
  return rc;
}

static int prt_floor_dim_or_one(int value) {
  if (value <= 0) return 1;
  if (value <= DIM) return value;
  value = (value / DIM) * DIM;
  return value > 0 ? value : 1;
}

static int prt_pick_safe_loop_conv_kchs_cap(const prt_gemmini_conv_desc_t *conv,
                                            enum tiled_matmul_type_t tiled_type) {
  if (!conv) return 0;
  if (tiled_type != WS) return 0;
  if (conv->kernel_dim != 1 || conv->stride != 1 || conv->padding != 0) return 0;
  if (conv->in_channels <= PRT_GEMMINI_WS_SAFE_POINTWISE_KCHS_CAP) return 0;
  return prt_floor_dim_or_one(PRT_GEMMINI_WS_SAFE_POINTWISE_KCHS_CAP);
}

static int prt_is_canonical_pointwise_matmul_conv(const prt_gemmini_conv_desc_t *conv,
                                                  enum tiled_matmul_type_t tiled_type) {
  if (!conv) return 0;
  if (tiled_type != WS) return 0;
  if (conv->kernel_dim != 1 || conv->stride != 1 || conv->padding != 0) return 0;
  if (conv->pool_stride != 0 || conv->pool_size > 1 || conv->pool_padding != 0) return 0;
  if (conv->input_dilation > 1 || conv->kernel_dilation > 1) return 0;
  if (conv->wrot180 || conv->trans_output_1203 || conv->trans_input_3120 ||
      conv->trans_weight_1203 || conv->trans_weight_0132) {
    return 0;
  }
  if (conv->batch_size <= 0 || conv->in_channels <= 0 || conv->out_channels <= 0 ||
      conv->out_row_dim <= 0 || conv->out_col_dim <= 0) {
    return 0;
  }
  if (conv->in_row_dim != conv->out_row_dim || conv->in_col_dim != conv->out_col_dim) return 0;
  return 1;
}

static void prt_pointwise_matmul_dims(const prt_gemmini_conv_desc_t *conv,
                                      size_t *dim_i, size_t *dim_j, size_t *dim_k) {
  if (!conv || !dim_i || !dim_j || !dim_k) return;
  *dim_i = (size_t)conv->batch_size * (size_t)conv->out_row_dim * (size_t)conv->out_col_dim;
  *dim_j = (size_t)conv->out_channels;
  *dim_k = (size_t)conv->in_channels;
}

static int prt_pointwise_matmul_strides_supported(const prt_gemmini_conv_desc_t *conv,
                                                  int in_stride, int weight_stride,
                                                  int out_stride) {
  size_t dim_i = 0;
  size_t dim_j = 0;
  size_t dim_k = 0;
  (void)dim_i;
  if (!conv) return 0;
  if (in_stride <= 0 || weight_stride <= 0 || out_stride <= 0) return 0;
  prt_pointwise_matmul_dims(conv, &dim_i, &dim_j, &dim_k);
  return (size_t)in_stride >= dim_k &&
         (size_t)weight_stride >= dim_j &&
         (size_t)out_stride >= dim_j;
}

static int prt_pick_safe_pointwise_oc_chunk(const prt_gemmini_conv_desc_t *conv,
                                            enum tiled_matmul_type_t tiled_type) {
  if (!conv) return 0;
  if (!prt_is_canonical_pointwise_matmul_conv(conv, tiled_type)) return 0;
  if (conv->out_channels <= PRT_GEMMINI_SAFE_POINTWISE_OC_CHUNK) return 0;
  return prt_floor_dim_or_one(PRT_GEMMINI_SAFE_POINTWISE_OC_CHUNK);
}

static const char *prt_tiled_matmul_type_name(enum tiled_matmul_type_t tiled_type) {
  switch (tiled_type) {
    case OS: return "OS";
    case WS: return "WS";
    case CPU: return "CPU";
    default: return "UNKNOWN";
  }
}

static const char *prt_split_kind_name(prt_layer_split_t split_kind) {
  switch (split_kind) {
    case PRT_LAYER_SPLIT_UNSPEC: return "unspec";
    case PRT_LAYER_SPLIT_SINGLE: return "single";
    case PRT_LAYER_SPLIT_OC: return "oc";
    case PRT_LAYER_SPLIT_SPATIAL: return "spatial";
    case PRT_LAYER_SPLIT_RESADD_SPATIAL: return "resadd_spatial";
    default: return "unknown";
  }
}

static enum tiled_matmul_type_t prt_conv_tiled_type(const prt_gemmini_conv_desc_t *conv) {
  if (!conv) return WS;
  if (conv->tiled_type == 0) return OS;
  if (conv->tiled_type == 2) return CPU;
  return WS;
}

static enum tiled_matmul_type_t prt_pick_resadd_type(const prt_gemmini_resadd_desc_t *resadd) {
  if (!resadd) return WS;
#if defined(__riscv)
  // User hard constraint: live bertmini runs must stay on Gemmini. Ignore any
  // historical CPU resadd artifact hints in the on-device runtime.
  return WS;
#else
  return resadd->tiled_type == 2 ? CPU : WS;
#endif
}

static int prt_writeback_resadd_cpu_output(prt_runtime_t *rt, uint32_t stage_id, uint32_t manager_id,
                                           uint32_t tensor_id, uint64_t host_output_base,
                                           size_t host_output_bytes,
                                           const prt_page_list_t *output_pages) {
  const uint32_t page_bytes =
    rt && rt->cfg.page_size_bytes ? rt->cfg.page_size_bytes : PRT_PAGE_SIZE_BYTES;
  const size_t spm_bytes =
    output_pages ? (size_t)output_pages->size * (size_t)page_bytes : 0U;
  uint64_t timeout_ns;
  const uint8_t *src = (const uint8_t *)(uintptr_t)host_output_base;
  uint8_t *padded = NULL;
  int rc;

  if (!rt || !output_pages || !output_pages->data || output_pages->size == 0 || !src) return PRT_OK;
  timeout_ns = (uint64_t)rt->cfg.watchdog_timeout_ms * 1000000ULL;

  if (host_output_bytes < spm_bytes) {
    padded = (uint8_t *)prt_gemmini_alloc_aligned(spm_bytes, 1);
    if (!padded) return PRT_ERR_NOMEM;
    memcpy(padded, src, host_output_bytes);
    src = padded;
  }

  PRT_PROGRESS_LOG("resadd-fallback-writeback stage=%u mgr=%u tensor=%u host_base=0x%llx tensor_bytes=%llu spm_bytes=%llu pages=%u begin",
                   stage_id, manager_id, tensor_id,
                   (unsigned long long)host_output_base,
                   (unsigned long long)host_output_bytes,
                   (unsigned long long)spm_bytes,
                   output_pages->size);
  rc = prt_dma_copy_dram_to_spm_pages(rt, output_pages, (uint64_t)(uintptr_t)src,
                                      rt->stage_dma_ids[stage_id], stage_id, tensor_id, timeout_ns);
  PRT_PROGRESS_LOG("resadd-fallback-writeback stage=%u mgr=%u tensor=%u end rc=%d",
                   stage_id, manager_id, tensor_id, rc);

  free(padded);
  return rc;
}

static int prt_should_force_resadd_cpu_fallback(const prt_conv_task_t *task,
                                                const prt_gemmini_resadd_desc_t *resadd,
                                                enum tiled_matmul_type_t requested_type) {
  (void)task;
  (void)resadd;
  (void)requested_type;

  // User hard constraint: bertmini debugging must stay on Gemmini. Keep the
  // old CPU fallback helpers available only as historical reference, but do not
  // route live resadd traffic to CPU.
  return 0;
}

static int prt_run_resadd_cpu_fallback(prt_runtime_t *rt, uint32_t stage_id, uint32_t manager_id,
                                       const prt_conv_task_t *task,
                                       const prt_gemmini_resadd_desc_t *resadd,
                                       enum tiled_matmul_type_t requested_type) {
  const prt_layer_split_t split_kind =
    task ? task->split_kind : PRT_LAYER_SPLIT_UNSPEC;
  const uint32_t tiles = (task && task->tile_count > 0) ? task->tile_count : 1U;
  prt_gemmini_resadd_desc_t host_desc;
  uint64_t host_output_base = 0;
  size_t host_output_bytes = 0;
  const prt_page_list_t *output_pages = NULL;
  uint32_t output_tensor_id = 0;
  int rc;
  if (!resadd) return PRT_ERR_INVAL;

  PRT_PROGRESS_LOG("resadd-fallback stage=%u mgr=%u begin I=%lu J=%lu stride=%lu split=%s tiles=%u requested_type=%s fallback_type=CPU reason=avoid-loop-ws-resadd",
                   stage_id, manager_id,
                   (unsigned long)resadd->I, (unsigned long)resadd->J,
                   (unsigned long)resadd->stride,
                   prt_split_kind_name(split_kind), tiles,
                   prt_tiled_matmul_type_name(requested_type));
  rc = prt_runtime_prepare_resadd_cpu_fallback(rt, stage_id, resadd, &host_desc,
                                               &host_output_base, &host_output_bytes,
                                               &output_pages, &output_tensor_id);
  if (rc != PRT_OK) {
    PRT_PROGRESS_LOG("resadd-fallback stage=%u mgr=%u map-failed rc=%d", stage_id, manager_id, rc);
    return rc;
  }
  resadd_cpu_ref(&host_desc);
  rc = prt_writeback_resadd_cpu_output(rt, stage_id, manager_id, output_tensor_id,
                                       host_output_base, host_output_bytes, output_pages);
  if (rc != PRT_OK) return rc;
  PRT_PROGRESS_LOG("resadd-fallback stage=%u mgr=%u end I=%lu J=%lu stride=%lu split=%s tiles=%u requested_type=%s fallback_type=CPU",
                   stage_id, manager_id,
                   (unsigned long)resadd->I, (unsigned long)resadd->J,
                   (unsigned long)resadd->stride,
                   prt_split_kind_name(split_kind), tiles,
                   prt_tiled_matmul_type_name(requested_type));
  return PRT_OK;
}

static enum tiled_matmul_type_t prt_pick_pointwise_matmul_fallback_type(
    enum tiled_matmul_type_t tiled_type, size_t dim_j) {
  (void)dim_j;
  // Live FireSim UART now proves the canonical 1x1 conv fallback enters
  // gemmini_loop_ws and stalls on the LOOP_WS run command itself. Stay on the
  // Gemmini path by switching just this narrow canonical pointwise fallback to
  // OS instead of WS; CPU fallback is not allowed on bertmini.
  return tiled_type == WS ? OS : tiled_type;
}

static int prt_run_pointwise_matmul_fallback_chunked_oc(uint32_t stage_id, uint32_t manager_id,
                                                        const prt_gemmini_conv_desc_t *conv,
                                                        enum tiled_matmul_type_t tiled_type,
                                                        int oc_chunk, int in_stride,
                                                        int weight_stride, int out_stride,
                                                        int emit_logs,
                                                        prt_rr_scope_t *scope);
static int prt_run_pointwise_matmul_fallback_impl(uint32_t stage_id, uint32_t manager_id,
                                                  const prt_gemmini_conv_desc_t *conv,
                                                  enum tiled_matmul_type_t tiled_type,
                                                  int emit_logs,
                                                  prt_rr_scope_t *scope);
static int prt_run_pointwise_matmul_fallback_strided_impl(uint32_t stage_id, uint32_t manager_id,
                                                          const prt_gemmini_conv_desc_t *conv,
                                                          enum tiled_matmul_type_t tiled_type,
                                                          int in_stride, int weight_stride,
                                                          int out_stride, int emit_logs,
                                                          prt_rr_scope_t *scope);

static int prt_run_pointwise_matmul_fallback_scoped(uint32_t stage_id, uint32_t manager_id,
                                                    const prt_gemmini_conv_desc_t *conv,
                                                    enum tiled_matmul_type_t tiled_type,
                                                    prt_rr_scope_t *scope);
static int prt_run_pointwise_matmul_fallback_strided_scoped(uint32_t stage_id,
                                                            uint32_t manager_id,
                                                            const prt_gemmini_conv_desc_t *conv,
                                                            enum tiled_matmul_type_t tiled_type,
                                                            int in_stride, int weight_stride,
                                                            int out_stride,
                                                            prt_rr_scope_t *scope);

// Prefer the stable non-CISC matmul path for canonical pointwise convs.
static int prt_run_pointwise_matmul_fallback(uint32_t stage_id, uint32_t manager_id,
                                             const prt_gemmini_conv_desc_t *conv,
                                             enum tiled_matmul_type_t tiled_type) {
  return prt_run_pointwise_matmul_fallback_impl(stage_id, manager_id, conv, tiled_type, 1, NULL);
}

static int prt_run_pointwise_matmul_fallback_scoped(uint32_t stage_id, uint32_t manager_id,
                                                    const prt_gemmini_conv_desc_t *conv,
                                                    enum tiled_matmul_type_t tiled_type,
                                                    prt_rr_scope_t *scope) {
  return prt_run_pointwise_matmul_fallback_impl(stage_id, manager_id, conv, tiled_type, 1, scope);
}

static int prt_run_pointwise_matmul_fallback_impl(uint32_t stage_id, uint32_t manager_id,
                                                  const prt_gemmini_conv_desc_t *conv,
                                                  enum tiled_matmul_type_t tiled_type,
                                                  int emit_logs,
                                                  prt_rr_scope_t *scope) {
  return prt_run_pointwise_matmul_fallback_strided_impl(stage_id, manager_id, conv, tiled_type,
                                                        prt_conv_input_stride(conv),
                                                        prt_conv_weight_stride(conv),
                                                        prt_conv_output_stride(conv),
                                                        emit_logs,
                                                        scope);
}

static int prt_run_pointwise_matmul_fallback_strided(uint32_t stage_id, uint32_t manager_id,
                                                     const prt_gemmini_conv_desc_t *conv,
                                                     enum tiled_matmul_type_t tiled_type,
                                                     int in_stride, int weight_stride,
                                                     int out_stride) {
  return prt_run_pointwise_matmul_fallback_strided_impl(stage_id, manager_id, conv, tiled_type,
                                                        in_stride, weight_stride, out_stride, 1,
                                                        NULL);
}

static int prt_run_pointwise_matmul_fallback_strided_scoped(uint32_t stage_id,
                                                            uint32_t manager_id,
                                                            const prt_gemmini_conv_desc_t *conv,
                                                            enum tiled_matmul_type_t tiled_type,
                                                            int in_stride, int weight_stride,
                                                            int out_stride,
                                                            prt_rr_scope_t *scope) {
  return prt_run_pointwise_matmul_fallback_strided_impl(stage_id, manager_id, conv, tiled_type,
                                                        in_stride, weight_stride, out_stride, 1,
                                                        scope);
}

static int prt_run_pointwise_matmul_fallback_strided_impl(uint32_t stage_id, uint32_t manager_id,
                                                          const prt_gemmini_conv_desc_t *conv,
                                                          enum tiled_matmul_type_t tiled_type,
                                                          int in_stride, int weight_stride,
                                                          int out_stride, int emit_logs,
                                                          prt_rr_scope_t *scope) {
  size_t dim_i = 0;
  size_t dim_j = 0;
  size_t dim_k = 0;
  int safe_oc_chunk = 0;
  enum tiled_matmul_type_t fallback_type;
  prt_pointwise_matmul_dims(conv, &dim_i, &dim_j, &dim_k);
  fallback_type = prt_pick_pointwise_matmul_fallback_type(tiled_type, dim_j);
  safe_oc_chunk = prt_pick_safe_pointwise_oc_chunk(conv, tiled_type);
  if (safe_oc_chunk > 0) {
    if (emit_logs) {
      PRT_PROGRESS_LOG("pointwise-matmul-fallback stage=%u mgr=%u chunked I=%llu J=%llu K=%llu oc_chunk=%d in_stride=%d weight_stride=%d out_stride=%d requested_type=%s fallback_type=%s",
                       stage_id, manager_id,
                       (unsigned long long)dim_i, (unsigned long long)dim_j, (unsigned long long)dim_k,
                       safe_oc_chunk, in_stride, weight_stride, out_stride,
                       prt_tiled_matmul_type_name(tiled_type),
                       prt_tiled_matmul_type_name(fallback_type));
    }
    return prt_run_pointwise_matmul_fallback_chunked_oc(stage_id, manager_id, conv, tiled_type,
                                                        safe_oc_chunk, in_stride, weight_stride,
                                                        out_stride, emit_logs, scope);
  }
  if (emit_logs) {
    PRT_PROGRESS_LOG("pointwise-matmul-fallback stage=%u mgr=%u begin I=%llu J=%llu K=%llu in_stride=%d weight_stride=%d out_stride=%d requested_type=%s fallback_type=%s",
                     stage_id, manager_id,
                     (unsigned long long)dim_i, (unsigned long long)dim_j, (unsigned long long)dim_k,
                     in_stride, weight_stride, out_stride,
                     prt_tiled_matmul_type_name(tiled_type),
                     prt_tiled_matmul_type_name(fallback_type));
    PRT_PROGRESS_HOT_LOG("pointwise-matmul-fallback stage=%u mgr=%u begin I=%lu J=%lu K=%lu in_stride=%d weight_stride=%d out_stride=%d requested_type=%s fallback_type=%s reason=avoid-loop-ws",
                         stage_id, manager_id,
                         (unsigned long)dim_i, (unsigned long)dim_j, (unsigned long)dim_k,
                         in_stride, weight_stride, out_stride,
                         prt_tiled_matmul_type_name(tiled_type),
                         prt_tiled_matmul_type_name(fallback_type));
  }
  tiled_matmul_nn_stride_auto(
    dim_i, dim_j, dim_k,
    (size_t)in_stride, (size_t)weight_stride, (size_t)out_stride,
    (const elem_t *)conv->input, (const elem_t *)conv->weights,
    conv->bias ? (const acc_t *)conv->bias : NULL, (const elem_t *)conv->output,
    conv->act, (acc_scale_t)(conv->output_scale != 0.0f ? conv->output_scale : 1.0f),
    conv->bias != NULL, fallback_type);
  if (emit_logs) {
    PRT_PROGRESS_LOG("pointwise-matmul-fallback stage=%u mgr=%u end I=%llu J=%llu K=%llu in_stride=%d weight_stride=%d out_stride=%d requested_type=%s fallback_type=%s",
                     stage_id, manager_id,
                     (unsigned long long)dim_i, (unsigned long long)dim_j, (unsigned long long)dim_k,
                     in_stride, weight_stride, out_stride,
                     prt_tiled_matmul_type_name(tiled_type),
                     prt_tiled_matmul_type_name(fallback_type));
    PRT_PROGRESS_HOT_LOG("pointwise-matmul-fallback stage=%u mgr=%u end I=%lu J=%lu K=%lu in_stride=%d weight_stride=%d out_stride=%d requested_type=%s fallback_type=%s",
                         stage_id, manager_id,
                         (unsigned long)dim_i, (unsigned long)dim_j, (unsigned long)dim_k,
                         in_stride, weight_stride, out_stride,
                         prt_tiled_matmul_type_name(tiled_type),
                         prt_tiled_matmul_type_name(fallback_type));
  }
  return PRT_OK;
}

static int prt_run_pointwise_matmul_fallback_chunked_oc(uint32_t stage_id, uint32_t manager_id,
                                                        const prt_gemmini_conv_desc_t *conv,
                                                        enum tiled_matmul_type_t tiled_type,
                                                        int oc_chunk, int in_stride,
                                                        int weight_stride, int out_stride,
                                                        int emit_logs,
                                                        prt_rr_scope_t *scope) {
  size_t dim_i = 0;
  size_t dim_j = 0;
  size_t dim_k = 0;
  const elem_t *weights;
  const acc_t *bias;
  elem_t *output;
  int rc = PRT_OK;

  if (!conv) return PRT_ERR_INVAL;
  if (oc_chunk <= 0 || conv->out_channels <= oc_chunk) {
    return prt_run_pointwise_matmul_fallback_strided_impl(stage_id, manager_id, conv, tiled_type,
                                                          in_stride, weight_stride, out_stride,
                                                          emit_logs, scope);
  }

  prt_pointwise_matmul_dims(conv, &dim_i, &dim_j, &dim_k);
  weights = (const elem_t *)conv->weights;
  bias = (const acc_t *)conv->bias;
  output = (elem_t *)conv->output;
  PRT_PROGRESS_RAW_LINE("[prt-raw] pw-chunked-enter");
  if (emit_logs) {
    PRT_PROGRESS_LOG("pointwise-matmul-fallback-chunked stage=%u mgr=%u begin I=%llu J=%llu K=%llu oc_chunk=%d in_stride=%d weight_stride=%d out_stride=%d requested_type=%s fallback_type=%s",
                     stage_id, manager_id,
                     (unsigned long long)dim_i, (unsigned long long)dim_j, (unsigned long long)dim_k,
                     oc_chunk, in_stride, weight_stride, out_stride,
                     prt_tiled_matmul_type_name(tiled_type),
                     prt_tiled_matmul_type_name(
                       prt_pick_pointwise_matmul_fallback_type(
                         tiled_type,
                         (size_t)(oc_chunk > 0 && oc_chunk < conv->out_channels ? oc_chunk
                                                                                : conv->out_channels))));
    PRT_PROGRESS_HOT_LOG("pointwise-matmul-fallback-chunked stage=%u mgr=%u begin I=%lu J=%lu K=%lu oc_chunk=%d requested_type=%s fallback_type=%s mode=direct-strided",
                         stage_id, manager_id,
                         (unsigned long)dim_i, (unsigned long)dim_j, (unsigned long)dim_k,
                         oc_chunk,
                         prt_tiled_matmul_type_name(tiled_type),
                         prt_tiled_matmul_type_name(
                           prt_pick_pointwise_matmul_fallback_type(
                             tiled_type,
                             (size_t)(oc_chunk > 0 && oc_chunk < conv->out_channels ? oc_chunk
                                                                                    : conv->out_channels))));
  }
  PRT_PROGRESS_RAW_LINE("[prt-raw] pw-chunked-after-begin-log");

  for (int oc_beg = 0; oc_beg < conv->out_channels; oc_beg += oc_chunk) {
    const int oc_tile =
      (oc_beg + oc_chunk <= conv->out_channels) ? oc_chunk : (conv->out_channels - oc_beg);
    prt_gemmini_conv_desc_t sub = *conv;

    sub.out_channels = oc_tile;
    sub.weights = weights + oc_beg;
    sub.bias = bias ? (bias + oc_beg) : NULL;
    sub.output = output + oc_beg;

    PRT_PROGRESS_RAW_LINE("[prt-raw] pw-chunked-before-chunk-log");
    if (emit_logs) {
      PRT_PROGRESS_HOT_LOG("pointwise-matmul-fallback-chunked stage=%u mgr=%u chunk-begin oc_beg=%d oc_tile=%d mode=direct-strided",
                           stage_id, manager_id, oc_beg, oc_tile);
    }
    PRT_PROGRESS_RAW_LINE("[prt-raw] pw-chunked-before-inner");
    rc = prt_run_pointwise_matmul_fallback_strided_impl(stage_id, manager_id, &sub, tiled_type,
                                                        in_stride, weight_stride, out_stride, 0,
                                                        scope);
    if (rc != PRT_OK) goto cleanup;
    PRT_PROGRESS_RAW_LINE("[prt-raw] pw-chunked-after-inner");
    if (scope && scope->valid && oc_beg + oc_chunk < conv->out_channels) {
      PRT_PROGRESS_LOG("pointwise-matmul-fallback-chunked stage=%u mgr=%u chunk-drain oc_beg=%d oc_tile=%d",
                       stage_id, manager_id, oc_beg, oc_tile);
      rc = flush_scope_after_drain(scope);
      if (rc != PRT_OK) goto cleanup;
    }
    if (emit_logs) {
      PRT_PROGRESS_HOT_LOG("pointwise-matmul-fallback-chunked stage=%u mgr=%u chunk-end oc_beg=%d oc_tile=%d",
                           stage_id, manager_id, oc_beg, oc_tile);
    }
  }

  if (emit_logs) {
    PRT_PROGRESS_LOG("pointwise-matmul-fallback-chunked stage=%u mgr=%u end I=%llu J=%llu K=%llu oc_chunk=%d requested_type=%s fallback_type=%s",
                     stage_id, manager_id,
                     (unsigned long long)dim_i, (unsigned long long)dim_j, (unsigned long long)dim_k,
                     oc_chunk,
                     prt_tiled_matmul_type_name(tiled_type),
                     prt_tiled_matmul_type_name(
                       prt_pick_pointwise_matmul_fallback_type(
                         tiled_type,
                         (size_t)(oc_chunk > 0 && oc_chunk < conv->out_channels ? oc_chunk
                                                                                : conv->out_channels))));
    PRT_PROGRESS_HOT_LOG("pointwise-matmul-fallback-chunked stage=%u mgr=%u end I=%lu J=%lu K=%lu oc_chunk=%d requested_type=%s fallback_type=%s mode=direct-strided",
                         stage_id, manager_id,
                         (unsigned long)dim_i, (unsigned long)dim_j, (unsigned long)dim_k,
                         oc_chunk,
                         prt_tiled_matmul_type_name(tiled_type),
                         prt_tiled_matmul_type_name(
                           prt_pick_pointwise_matmul_fallback_type(
                             tiled_type,
                             (size_t)(oc_chunk > 0 && oc_chunk < conv->out_channels ? oc_chunk
                                                                                    : conv->out_channels))));
  }
cleanup:
  return rc;
}

static void tiled_conv_stride_auto_capped_kchs(
    int batch_size, int in_row_dim, int in_col_dim, int in_channels,
    int out_channels, int out_row_dim, int out_col_dim,
    int stride, int input_dilation, int kernel_dilation, int padding, int kernel_dim,
    int in_stride, int weight_stride, int out_stride,
    bool wrot180, bool trans_output_1203, bool trans_input_3120,
    bool trans_weight_1203, bool trans_weight_0132,
    const elem_t *input, const elem_t *weights, const acc_t *bias, elem_t *output,
    int act, acc_scale_t scale,
    int pool_size, int pool_stride, int pool_padding,
    enum tiled_matmul_type_t tiled_conv_type,
    int max_kchs,
    int chosen_args[7]) {
  const bool no_pool = pool_stride == 0;
  const int orows_idx = 1;
  const int ocols_idx = 2;
  const int out_channels_idx = 3;
  const int in_channels_idx = 6;
  const int max_spad_rows = (BANK_NUM * BANK_ROWS / 2);
  const int max_acc_rows = (ACC_ROWS / 2);
  int pool_out_row_dim;
  int pool_out_col_dim;
  bool downsample;
  int args[7];
  int max_args[7];
  int capped_kchs;
  int spad_rows;
  int acc_rows;

  if (no_pool) {
    pool_size = 1;
    pool_stride = 1;
    pool_padding = 0;
  }

  capped_kchs = max_kchs > 0 ? max_kchs : in_channels;
  if (capped_kchs > in_channels) capped_kchs = in_channels;
  capped_kchs = prt_floor_dim_or_one(capped_kchs);
  if (capped_kchs > in_channels) capped_kchs = in_channels;

  pool_out_row_dim = (out_row_dim + 2 * pool_padding - pool_size) / pool_stride + 1;
  pool_out_col_dim = (out_col_dim + 2 * pool_padding - pool_size) / pool_stride + 1;

  downsample = stride == 2 && kernel_dim == 1 && padding == 0 && no_pool &&
               in_row_dim % 2 == 0 && in_col_dim % 2 == 0;

  args[0] = batch_size;
  args[1] = pool_out_row_dim;
  args[2] = pool_out_col_dim;
  args[3] = out_channels;
  args[4] = kernel_dim;
  args[5] = kernel_dim;
  args[6] = capped_kchs;

  memcpy(max_args, args, sizeof(args));

  spad_rows = tiled_conv_total_spad_rows(false,
      stride, input_dilation, kernel_dilation, downsample, trans_weight_0132, trans_input_3120,
      args[0], args[1], args[2], args[3], args[4], args[5], args[6], pool_size, pool_stride);
  acc_rows = tiled_conv_total_spad_rows(true,
      stride, input_dilation, kernel_dilation, downsample, trans_weight_0132, trans_input_3120,
      args[0], args[1], args[2], args[3], args[4], args[5], args[6], pool_size, pool_stride);

  while (spad_rows > max_spad_rows || acc_rows > max_acc_rows) {
    int max_val = -1;
    int max_idx = -1;

    for (size_t i = 0; i < sizeof(args) / sizeof(args[0]); ++i) {
      if (!(i == ocols_idx && args[i] <= DIM && args[orows_idx] > 1) &&
          args[i] > max_val) {
        max_val = args[i];
        max_idx = (int)i;
      }
    }

    if (max_idx == out_channels_idx || max_idx == in_channels_idx) {
      if (args[max_idx] % DIM != 0) args[max_idx] = (args[max_idx] / DIM) * DIM;
      else args[max_idx] -= DIM;
      args[max_idx] = args[max_idx] == 0 ? 1 : args[max_idx];
    } else {
      args[max_idx] -= 1;
    }

    spad_rows = tiled_conv_total_spad_rows(false,
        stride, input_dilation, kernel_dilation, downsample, trans_weight_0132, trans_input_3120,
        args[0], args[1], args[2], args[3], args[4], args[5], args[6], pool_size, pool_stride);
    acc_rows = tiled_conv_total_spad_rows(true,
        stride, input_dilation, kernel_dilation, downsample, trans_weight_0132, trans_input_3120,
        args[0], args[1], args[2], args[3], args[4], args[5], args[6], pool_size, pool_stride);
  }

  {
    bool not_increased = false;
    while (!not_increased) {
      int args_candidate[7];
      not_increased = true;
      memcpy(args_candidate, args, sizeof(args));
      args_candidate[ocols_idx] += 1;

      if (args_candidate[ocols_idx] > max_args[ocols_idx]) continue;

      spad_rows = tiled_conv_total_spad_rows(false,
          stride, input_dilation, kernel_dilation, downsample, trans_weight_0132, trans_input_3120,
          args_candidate[0], args_candidate[1], args_candidate[2], args_candidate[3],
          args_candidate[4], args_candidate[5], args_candidate[6], pool_size, pool_stride);
      acc_rows = tiled_conv_total_spad_rows(true,
          stride, input_dilation, kernel_dilation, downsample, trans_weight_0132, trans_input_3120,
          args_candidate[0], args_candidate[1], args_candidate[2], args_candidate[3],
          args_candidate[4], args_candidate[5], args_candidate[6], pool_size, pool_stride);

      if (spad_rows <= max_spad_rows && acc_rows <= max_acc_rows) {
        args[ocols_idx] = args_candidate[ocols_idx];
        not_increased = false;
      }
    }
  }

  {
    bool nothing_increased = false;
    while (!nothing_increased) {
      nothing_increased = true;

      for (size_t i = 0; i < sizeof(args) / sizeof(args[0]); ++i) {
        int args_candidate[7];
        memcpy(args_candidate, args, sizeof(args));
        if ((int)i == out_channels_idx || (int)i == in_channels_idx) args_candidate[i] += DIM;
        else args_candidate[i] += 1;

        if (args_candidate[i] > max_args[i]) continue;

        spad_rows = tiled_conv_total_spad_rows(false,
            stride, input_dilation, kernel_dilation, downsample, trans_weight_0132, trans_input_3120,
            args_candidate[0], args_candidate[1], args_candidate[2], args_candidate[3],
            args_candidate[4], args_candidate[5], args_candidate[6], pool_size, pool_stride);
        acc_rows = tiled_conv_total_spad_rows(true,
            stride, input_dilation, kernel_dilation, downsample, trans_weight_0132, trans_input_3120,
            args_candidate[0], args_candidate[1], args_candidate[2], args_candidate[3],
            args_candidate[4], args_candidate[5], args_candidate[6], pool_size, pool_stride);

        if (spad_rows <= max_spad_rows && acc_rows <= max_acc_rows) {
          args[i] = args_candidate[i];
          nothing_increased = false;
        }
      }
    }
  }

  if (chosen_args) memcpy(chosen_args, args, sizeof(args));

  PRT_PROGRESS_LOG("tiled-conv-safe-args batches=%d porows=%d pocols=%d pochs=%d krows=%d kcols=%d kchs=%d cap=%d",
                   args[0], args[1], args[2], args[3], args[4], args[5], args[6], capped_kchs);

  tiled_conv(
      batch_size, in_row_dim, in_col_dim, in_channels,
      out_channels, out_row_dim, out_col_dim,
      stride, input_dilation, kernel_dilation, padding, kernel_dim,
      in_stride, weight_stride, out_stride,
      wrot180, trans_output_1203, trans_input_3120,
      trans_weight_1203, trans_weight_0132,
      args[0], args[1], args[2], args[3], args[4], args[5], args[6],
      input, weights, bias, output,
      act, scale,
      pool_size, pool_stride, pool_padding,
      tiled_conv_type);
}
#endif

#if !defined(__riscv)
typedef struct {
  uint32_t r0;
  uint32_t r1;
  uint32_t c0;
  uint32_t c1;
} prt_rect2d_host_t;
#endif

static int8_t scale_and_sat_i8(int64_t value, int act, float scale) {
  double scaled = (double)value * (double)scale;
  int64_t out = scaled >= 0.0 ? (int64_t)(scaled + 0.5) : (int64_t)(scaled - 0.5);
  int64_t minimum = act ? 0 : -128;
  if (out > 127) out = 127;
  if (out < minimum) out = minimum;
  return (int8_t)out;
}

static int32_t scale_i32_from_i8(int8_t value, float scale) {
  double scaled = (double)value * (double)scale;
  return scaled >= 0.0 ? (int32_t)(scaled + 0.5) : (int32_t)(scaled - 0.5);
}

static void conv_cpu_without_pool_ref(const prt_gemmini_conv_desc_t *conv) {
  const int8_t *input = (const int8_t *)conv->input;
  const int8_t *weights = (const int8_t *)conv->weights;
  const int32_t *bias = (const int32_t *)conv->bias;
  int8_t *output = (int8_t *)conv->output;
  int in_stride = prt_conv_input_stride(conv);
  int weight_stride = prt_conv_weight_stride(conv);
  int out_stride = prt_conv_output_stride(conv);
  int no_bias = bias == NULL;

  for (int b = 0; b < conv->batch_size; ++b) {
    for (int orow = 0; orow < conv->out_row_dim; ++orow) {
      for (int ocol = 0; ocol < conv->out_col_dim; ++ocol) {
        for (int och = 0; och < conv->out_channels; ++och) {
          int64_t acc = no_bias ? 0 : bias[och];
          for (int krow = 0; krow < conv->kernel_dim; ++krow) {
            int in_row_nom = orow * conv->stride + krow * conv->kernel_dilation - conv->padding;
            if (in_row_nom % conv->input_dilation != 0) continue;
            for (int kcol = 0; kcol < conv->kernel_dim; ++kcol) {
              int in_col_nom = ocol * conv->stride + kcol * conv->kernel_dilation - conv->padding;
              int irow = in_row_nom / conv->input_dilation;
              int icol;
              if (in_col_nom % conv->input_dilation != 0) continue;
              icol = in_col_nom / conv->input_dilation;
              for (int kch = 0; kch < conv->in_channels; ++kch) {
                int8_t ipixel = 0;
                int krow_idx = conv->wrot180 ? (conv->kernel_dim - krow - 1) : krow;
                int kcol_idx = conv->wrot180 ? (conv->kernel_dim - kcol - 1) : kcol;
                int8_t weight;
                if (irow >= 0 && irow < conv->in_row_dim && icol >= 0 && icol < conv->in_col_dim) {
                  const int8_t *in = input +
                    ((b * conv->in_row_dim * conv->in_col_dim + irow * conv->in_col_dim + icol) * in_stride + kch);
                  if (conv->trans_input_3120) {
                    in = input + ((kch * conv->in_row_dim * conv->in_col_dim + irow * conv->in_col_dim + icol) *
                                  conv->batch_size + b);
                  }
                  ipixel = *in;
                }
                weight = *(weights + (krow_idx * conv->kernel_dim * conv->in_channels +
                                      kcol_idx * conv->in_channels + kch) * weight_stride + och);
                if (conv->trans_weight_1203) {
                  weight = *(weights + (kch * conv->kernel_dim * conv->kernel_dim +
                                        krow_idx * conv->kernel_dim + kcol_idx) * conv->out_channels + och);
                } else if (conv->trans_weight_0132) {
                  weight = *(weights + (krow_idx * conv->kernel_dim * conv->out_channels +
                                        kcol_idx * conv->out_channels + och) * conv->in_channels + kch);
                }
                acc += (int64_t)weight * (int64_t)ipixel;
              }
            }
          }
          {
            int8_t *out = output +
              ((b * conv->out_row_dim * conv->out_col_dim + orow * conv->out_col_dim + ocol) * out_stride + och);
            if (conv->trans_output_1203) {
              out = output + ((orow * conv->out_col_dim * conv->batch_size + ocol * conv->batch_size + b) *
                              conv->out_channels + och);
            }
            *out = scale_and_sat_i8(acc, conv->act, conv->output_scale != 0.0f ? conv->output_scale : 1.0f);
          }
        }
      }
    }
  }
}

static void conv_cpu_ref(const prt_gemmini_conv_desc_t *conv) {
  if (!conv) return;
  if (conv->pool_stride == 0 || conv->pool_size <= 1) {
    conv_cpu_without_pool_ref(conv);
    return;
  }

  {
    size_t tmp_size = (size_t)conv->batch_size * (size_t)conv->out_row_dim * (size_t)conv->out_col_dim *
                      (size_t)conv->out_channels;
    int8_t *tmp = (int8_t *)calloc(tmp_size, sizeof(int8_t));
    if (!tmp) return;
    prt_gemmini_conv_desc_t base = *conv;
    base.output = tmp;
    base.pool_size = 1;
    base.pool_stride = 0;
    base.pool_padding = 0;
    conv_cpu_without_pool_ref(&base);

    {
      int pool_out_row_dim = (conv->out_row_dim + 2 * conv->pool_padding - conv->pool_size) / conv->pool_stride + 1;
      int pool_out_col_dim = (conv->out_col_dim + 2 * conv->pool_padding - conv->pool_size) / conv->pool_stride + 1;
      int8_t *dst = (int8_t *)conv->output;
      for (int b = 0; b < conv->batch_size; ++b) {
        for (int porow = 0; porow < pool_out_row_dim; ++porow) {
          for (int pocol = 0; pocol < pool_out_col_dim; ++pocol) {
            for (int och = 0; och < conv->out_channels; ++och) {
              int have = 0;
              int8_t best = 0;
              for (int pwrow = 0; pwrow < conv->pool_size; ++pwrow) {
                int orow = porow * conv->pool_stride + pwrow - conv->pool_padding;
                for (int pwcol = 0; pwcol < conv->pool_size; ++pwcol) {
                  int ocol = pocol * conv->pool_stride + pwcol - conv->pool_padding;
                  int8_t val;
                  if (orow < 0 || orow >= conv->out_row_dim || ocol < 0 || ocol >= conv->out_col_dim) continue;
                  val = tmp[((b * conv->out_row_dim * conv->out_col_dim + orow * conv->out_col_dim + ocol) *
                             conv->out_channels) + och];
                  if (!have || val > best) {
                    best = val;
                    have = 1;
                  }
                }
              }
              if (have) {
                dst[((b * pool_out_row_dim * pool_out_col_dim + porow * pool_out_col_dim + pocol) *
                     conv->out_channels) + och] = best;
              }
            }
          }
        }
      }
    }
    free(tmp);
  }
}

static void resadd_cpu_ref(const prt_gemmini_resadd_desc_t *resadd) {
  const int8_t *a;
  const int8_t *b;
  int8_t *c;
  int minimum;
  if (!resadd) return;
  minimum = resadd->relu ? 0 : -128;
  a = (const int8_t *)resadd->A;
  b = (const int8_t *)resadd->B;
  c = (int8_t *)resadd->C;
  if (!a || !b || !c) return;

  for (size_t i = 0; i < resadd->I; ++i) {
    for (size_t j = 0; j < resadd->J; ++j) {
      int32_t acc = scale_i32_from_i8(a[i * resadd->stride + j], resadd->A_scale) +
                    scale_i32_from_i8(b[i * resadd->stride + j], resadd->B_scale);
      double scaled = (double)acc * (double)(resadd->C_scale != 0.0f ? resadd->C_scale : 1.0f);
      int32_t out = scaled >= 0.0 ? (int32_t)(scaled + 0.5) : (int32_t)(scaled - 0.5);
      if (out > 127) out = 127;
      if (out < minimum) out = minimum;
      c[i * resadd->stride + j] = (int8_t)out;
    }
  }
}

#if !defined(__riscv)
static void host_split_1d_range(uint32_t total, uint32_t parts, uint32_t idx,
                                uint32_t *out_begin, uint32_t *out_end) {
  uint32_t base;
  uint32_t rem;
  uint32_t begin;
  uint32_t span;
  if (!out_begin || !out_end || parts == 0) return;
  base = total / parts;
  rem = total % parts;
  begin = idx * base + (idx < rem ? idx : rem);
  span = base + (idx < rem ? 1U : 0U);
  if (begin > total) begin = total;
  if (begin + span > total) span = total - begin;
  *out_begin = begin;
  *out_end = begin + span;
}

static uint32_t host_rect2d_area(const prt_rect2d_host_t *r) {
  if (!r || r->r1 <= r->r0 || r->c1 <= r->c0) return 0;
  return (r->r1 - r->r0) * (r->c1 - r->c0);
}

static int host_partition_2d_rects(uint32_t rows, uint32_t cols, uint32_t parts,
                                   prt_rect2d_host_t *out_rects) {
  uint32_t count = 1;
  if (!out_rects || rows == 0 || cols == 0 || parts == 0) return PRT_ERR_INVAL;
  if ((uint64_t)parts > (uint64_t)rows * (uint64_t)cols) return PRT_ERR_NOT_IMPL;

  out_rects[0].r0 = 0;
  out_rects[0].r1 = rows;
  out_rects[0].c0 = 0;
  out_rects[0].c1 = cols;

  while (count < parts) {
    uint32_t best_idx = UINT32_MAX;
    uint32_t best_area = 0;
    prt_rect2d_host_t cur;
    uint32_t h;
    uint32_t w;
    prt_rect2d_host_t split_new;

    for (uint32_t i = 0; i < count; ++i) {
      uint32_t a = host_rect2d_area(&out_rects[i]);
      if (a > best_area && a > 1U) {
        best_area = a;
        best_idx = i;
      }
    }
    if (best_idx == UINT32_MAX) return PRT_ERR_NOT_IMPL;

    cur = out_rects[best_idx];
    h = cur.r1 - cur.r0;
    w = cur.c1 - cur.c0;
    if ((h >= w && h > 1U) || w == 1U) {
      uint32_t mid = cur.r0 + h / 2U;
      if (mid <= cur.r0 || mid >= cur.r1) return PRT_ERR_NOT_IMPL;
      out_rects[best_idx].r1 = mid;
      split_new.r0 = mid;
      split_new.r1 = cur.r1;
      split_new.c0 = cur.c0;
      split_new.c1 = cur.c1;
    } else {
      uint32_t mid = cur.c0 + w / 2U;
      if (mid <= cur.c0 || mid >= cur.c1) return PRT_ERR_NOT_IMPL;
      out_rects[best_idx].c1 = mid;
      split_new.r0 = cur.r0;
      split_new.r1 = cur.r1;
      split_new.c0 = mid;
      split_new.c1 = cur.c1;
    }
    out_rects[count++] = split_new;
  }
  return PRT_OK;
}

static int run_conv_oc_split_host(const prt_conv_task_t *task, const prt_gemmini_conv_desc_t *conv) {
  uint32_t tiles;
  if (!task || !conv) return PRT_ERR_INVAL;

  tiles = task->tile_count > 0 ? task->tile_count : 1U;
  if (tiles <= 1U) {
    conv_cpu_ref(conv);
    return PRT_OK;
  }
  if ((uint32_t)conv->out_channels < tiles) return PRT_ERR_NOT_IMPL;

  for (uint32_t t = 0; t < tiles; ++t) {
    uint32_t oc_beg;
    uint32_t oc_end;
    uint32_t oc_tile;
    prt_gemmini_conv_desc_t sub = *conv;
    host_split_1d_range((uint32_t)conv->out_channels, tiles, t, &oc_beg, &oc_end);
    if (oc_end <= oc_beg) continue;
    oc_tile = oc_end - oc_beg;

    sub.out_channels = (int)oc_tile;
    sub.weights = ((const int8_t *)conv->weights) + oc_beg;
    sub.bias = conv->bias ? (((const int32_t *)conv->bias) + oc_beg) : NULL;
    sub.output = ((int8_t *)conv->output) + oc_beg;
    conv_cpu_ref(&sub);
  }
  return PRT_OK;
}

static int run_conv_spatial_split_host(const prt_conv_task_t *task, const prt_gemmini_conv_desc_t *conv) {
  uint32_t tiles;
  const int8_t *input;
  const int8_t *weights;
  const int32_t *bias;
  int8_t *output;
  prt_rect2d_host_t rects[PRT_MAX_TILE_SPLITS];
  int rc;
  if (!task || !conv) return PRT_ERR_INVAL;
  tiles = task->tile_count > 0 ? task->tile_count : 1U;
  if (tiles <= 1U) {
    conv_cpu_ref(conv);
    return PRT_OK;
  }
  if (tiles > PRT_MAX_TILE_SPLITS) return PRT_ERR_INVAL;

  rc = host_partition_2d_rects((uint32_t)conv->out_row_dim, (uint32_t)conv->out_col_dim, tiles, rects);
  if (rc != PRT_OK) return rc;

  input = (const int8_t *)conv->input;
  weights = (const int8_t *)conv->weights;
  bias = (const int32_t *)conv->bias;
  output = (int8_t *)conv->output;

  for (uint32_t t = 0; t < tiles; ++t) {
    uint32_t oh_beg = rects[t].r0;
    uint32_t oh_end = rects[t].r1;
    uint32_t ow_beg = rects[t].c0;
    uint32_t ow_end = rects[t].c1;
    int tile_oh = (int)(oh_end - oh_beg);
    int tile_ow = (int)(ow_end - ow_beg);
    int in_h;
    int in_w;
    int in_row0;
    int in_col0;
    int8_t *in_pack;
    int8_t *out_pack;
    prt_gemmini_conv_desc_t sub = *conv;

    if (tile_oh <= 0 || tile_ow <= 0) return PRT_ERR_NOT_IMPL;

    in_row0 = (int)oh_beg * conv->stride - conv->padding;
    in_col0 = (int)ow_beg * conv->stride - conv->padding;
    in_h = (tile_oh - 1) * conv->stride + conv->kernel_dim;
    in_w = (tile_ow - 1) * conv->stride + conv->kernel_dim;
    if (in_h <= 0 || in_w <= 0) return PRT_ERR_INVAL;

    in_pack = (int8_t *)prt_gemmini_alloc_aligned(sizeof(int8_t) *
                                                  (size_t)conv->batch_size *
                                                  (size_t)in_h *
                                                  (size_t)in_w *
                                                  (size_t)conv->in_channels,
                                                  1);
    out_pack = (int8_t *)prt_gemmini_alloc_aligned(sizeof(int8_t) *
                                                   (size_t)conv->batch_size *
                                                   (size_t)tile_oh *
                                                   (size_t)tile_ow *
                                                   (size_t)conv->out_channels,
                                                   1);
    if (!in_pack || !out_pack) {
      free(in_pack);
      free(out_pack);
      return PRT_ERR_NOMEM;
    }

    for (int b = 0; b < conv->batch_size; ++b) {
      for (int ih = 0; ih < in_h; ++ih) {
        for (int iw = 0; iw < in_w; ++iw) {
          int src_h = in_row0 + ih;
          int src_w = in_col0 + iw;
          if (src_h < 0 || src_h >= conv->in_row_dim || src_w < 0 || src_w >= conv->in_col_dim) continue;
          memcpy(
            in_pack + (((size_t)b * in_h * in_w) + (size_t)ih * in_w + (size_t)iw) * conv->in_channels,
            input + (((size_t)b * conv->in_row_dim * conv->in_col_dim) +
                     (size_t)src_h * conv->in_col_dim + (size_t)src_w) * conv->in_channels,
            sizeof(int8_t) * conv->in_channels);
        }
      }
    }

    sub.in_row_dim = in_h;
    sub.in_col_dim = in_w;
    sub.out_row_dim = tile_oh;
    sub.out_col_dim = tile_ow;
    sub.padding = 0;
    sub.in_stride = conv->in_channels;
    sub.out_stride = conv->out_channels;
    sub.input = in_pack;
    sub.weights = weights;
    sub.bias = bias;
    sub.output = out_pack;
    conv_cpu_ref(&sub);

    for (int b = 0; b < conv->batch_size; ++b) {
      for (int oh = 0; oh < tile_oh; ++oh) {
        int dst_oh = (int)oh_beg + oh;
        for (int ow = 0; ow < tile_ow; ++ow) {
          int dst_ow = (int)ow_beg + ow;
          size_t dst_off = ((size_t)b * conv->out_row_dim * conv->out_col_dim +
                            (size_t)dst_oh * conv->out_col_dim + (size_t)dst_ow) * (size_t)conv->out_channels;
          size_t src_off = ((size_t)b * tile_oh * tile_ow +
                            (size_t)oh * tile_ow + (size_t)ow) * (size_t)conv->out_channels;
          memcpy(output + dst_off, out_pack + src_off, sizeof(int8_t) * conv->out_channels);
        }
      }
    }

    free(in_pack);
    free(out_pack);
  }
  return PRT_OK;
}

static int run_resadd_split_host(const prt_conv_task_t *task, const prt_gemmini_resadd_desc_t *resadd) {
  uint32_t tiles;
  prt_rect2d_host_t rects[PRT_MAX_TILE_SPLITS];
  int rc;
  if (!task || !resadd) return PRT_ERR_INVAL;
  tiles = task->tile_count > 0 ? task->tile_count : 1U;
  if (tiles <= 1U) {
    resadd_cpu_ref(resadd);
    return PRT_OK;
  }
  if (tiles > PRT_MAX_TILE_SPLITS) return PRT_ERR_INVAL;

  rc = host_partition_2d_rects((uint32_t)resadd->I, (uint32_t)resadd->J, tiles, rects);
  if (rc != PRT_OK) return rc;

  for (uint32_t t = 0; t < tiles; ++t) {
    uint32_t i_beg = rects[t].r0;
    uint32_t i_end = rects[t].r1;
    uint32_t j_beg = rects[t].c0;
    uint32_t j_end = rects[t].c1;
    prt_gemmini_resadd_desc_t sub = *resadd;
    if (i_end <= i_beg || j_end <= j_beg) return PRT_ERR_NOT_IMPL;
    sub.I = (size_t)(i_end - i_beg);
    sub.J = (size_t)(j_end - j_beg);
    sub.A = (const int8_t *)resadd->A + (size_t)i_beg * resadd->stride + (size_t)j_beg;
    sub.B = (const int8_t *)resadd->B + (size_t)i_beg * resadd->stride + (size_t)j_beg;
    sub.C = (int8_t *)resadd->C + (size_t)i_beg * resadd->stride + (size_t)j_beg;
    resadd_cpu_ref(&sub);
  }
  return PRT_OK;
}
#endif

#if defined(__riscv)
typedef struct {
  uint32_t r0;
  uint32_t r1;
  uint32_t c0;
  uint32_t c1;
} prt_rect2d_t;

typedef struct {
  uint32_t oc_beg;
  uint32_t oc_tile;
  elem_t *w_pack;
  acc_t *b_pack;
  elem_t *o_pack;
} prt_conv_oc_split_tile_t;

typedef struct {
  uint32_t oh_beg;
  uint32_t ow_beg;
  int tile_oh;
  int tile_ow;
  elem_t *in_pack;
  elem_t *out_pack;
} prt_conv_spatial_split_tile_t;

static void cleanup_conv_oc_split_tiles(prt_conv_oc_split_tile_t *tiles, uint32_t count) {
  if (!tiles) return;
  for (uint32_t i = 0; i < count; ++i) {
    free(tiles[i].w_pack);
    free(tiles[i].b_pack);
    free(tiles[i].o_pack);
    tiles[i].w_pack = NULL;
    tiles[i].b_pack = NULL;
    tiles[i].o_pack = NULL;
  }
}

static void cleanup_conv_spatial_split_tiles(prt_conv_spatial_split_tile_t *tiles, uint32_t count) {
  if (!tiles) return;
  for (uint32_t i = 0; i < count; ++i) {
    free(tiles[i].in_pack);
    free(tiles[i].out_pack);
    tiles[i].in_pack = NULL;
    tiles[i].out_pack = NULL;
  }
}

static uint32_t rect2d_area(const prt_rect2d_t *r) {
  if (!r || r->r1 <= r->r0 || r->c1 <= r->c0) return 0;
  return (r->r1 - r->r0) * (r->c1 - r->c0);
}

static void split_1d_range(uint32_t total, uint32_t parts, uint32_t idx,
                           uint32_t *out_begin, uint32_t *out_end) {
  uint32_t base;
  uint32_t rem;
  uint32_t begin;
  uint32_t span;
  if (!out_begin || !out_end || parts == 0) return;

  base = total / parts;
  rem = total % parts;
  begin = idx * base + (idx < rem ? idx : rem);
  span = base + (idx < rem ? 1U : 0U);
  if (begin > total) begin = total;
  if (begin + span > total) span = total - begin;
  *out_begin = begin;
  *out_end = begin + span;
}

static int partition_2d_rects(uint32_t rows, uint32_t cols, uint32_t parts, prt_rect2d_t *out_rects) {
  uint32_t count = 1;
  if (!out_rects || rows == 0 || cols == 0 || parts == 0) return PRT_ERR_INVAL;
  if ((uint64_t)parts > (uint64_t)rows * (uint64_t)cols) return PRT_ERR_NOT_IMPL;

  out_rects[0].r0 = 0;
  out_rects[0].r1 = rows;
  out_rects[0].c0 = 0;
  out_rects[0].c1 = cols;

  while (count < parts) {
    uint32_t best_idx = UINT32_MAX;
    uint32_t best_area = 0;
    prt_rect2d_t cur;
    uint32_t h;
    uint32_t w;
    prt_rect2d_t split_new;

    for (uint32_t i = 0; i < count; ++i) {
      uint32_t a = rect2d_area(&out_rects[i]);
      if (a > best_area && a > 1U) {
        best_area = a;
        best_idx = i;
      }
    }
    if (best_idx == UINT32_MAX) return PRT_ERR_NOT_IMPL;

    cur = out_rects[best_idx];
    h = cur.r1 - cur.r0;
    w = cur.c1 - cur.c0;

    if ((h >= w && h > 1U) || w == 1U) {
      uint32_t mid = cur.r0 + h / 2U;
      if (mid <= cur.r0 || mid >= cur.r1) return PRT_ERR_NOT_IMPL;
      out_rects[best_idx].r1 = mid;
      split_new.r0 = mid;
      split_new.r1 = cur.r1;
      split_new.c0 = cur.c0;
      split_new.c1 = cur.c1;
    } else {
      uint32_t mid = cur.c0 + w / 2U;
      if (mid <= cur.c0 || mid >= cur.c1) return PRT_ERR_NOT_IMPL;
      out_rects[best_idx].c1 = mid;
      split_new.r0 = cur.r0;
      split_new.r1 = cur.r1;
      split_new.c0 = mid;
      split_new.c1 = cur.c1;
    }

    out_rects[count++] = split_new;
  }

  return PRT_OK;
}

static int fence_task_managers(const prt_conv_task_t *task) {
  uint32_t unique_mgrs[PRT_MAX_CORES];
  uint32_t unique_count = 0;
  uint32_t mgr_count;
  if (!task) return PRT_ERR_INVAL;

  mgr_count = task->num_managers > PRT_MAX_CORES ? PRT_MAX_CORES : task->num_managers;
  if (mgr_count == 0) mgr_count = 1;

  for (uint32_t i = 0; i < mgr_count; ++i) {
    uint32_t id = (i < task->num_managers) ? task->manager_ids[i] : task->acc_id;
    int seen = 0;
    for (uint32_t j = 0; j < unique_count; ++j) {
      if (unique_mgrs[j] == id) {
        seen = 1;
        break;
      }
    }
    if (!seen && unique_count < PRT_MAX_CORES) {
      unique_mgrs[unique_count++] = id;
    }
  }

  for (uint32_t i = 0; i < unique_count; ++i) {
    prt_rr_scope_t scope;
    int rc = prt_rr_acquire_scope(NULL, task->stage_id, unique_mgrs[i], 3U, &scope);
    if (rc != PRT_OK) return rc;
    rc = prt_rr_fence_scope(&scope);
    gemmini_fence();
    if (rc == PRT_OK) rc = flush_scope_after_drain(&scope);
    (void)prt_rr_release_scope(&scope);
    if (rc != PRT_OK) return rc;
  }

  return PRT_OK;
}

static int conv_call_for_manager_nb(uint32_t stage_id, uint32_t manager_id,
                                    const prt_gemmini_conv_desc_t *conv) {
  prt_rr_scope_t scope;
  enum tiled_matmul_type_t tiled_type = WS;
  int input_dilation;
  int kernel_dilation;
  int in_stride;
  int weight_stride;
  int out_stride;
  int use_pointwise_matmul_fallback = 0;
  int safe_kchs_cap = 0;
  int chosen_args[7] = {0};
  float output_scale;
  int rc;
  if (!conv) return PRT_ERR_INVAL;

  PRT_PROGRESS_HOT_LOG("conv-nb stage=%u mgr=%u acquire-begin out_ch=%d out_dim=%dx%d in_dim=%dx%d",
                       stage_id, manager_id, conv->out_channels,
                       conv->out_row_dim, conv->out_col_dim,
                       conv->in_row_dim, conv->in_col_dim);
  rc = prt_rr_acquire_scope(NULL, stage_id, manager_id, 3U, &scope);
  if (rc != PRT_OK) return rc;
  PRT_PROGRESS_HOT_LOG("conv-nb stage=%u mgr=%u acquire-end", stage_id, manager_id);

  tiled_type = prt_conv_tiled_type(conv);

  input_dilation = conv->input_dilation > 0 ? conv->input_dilation : 1;
  kernel_dilation = conv->kernel_dilation > 0 ? conv->kernel_dilation : 1;
  in_stride = prt_conv_input_stride(conv);
  weight_stride = prt_conv_weight_stride(conv);
  out_stride = prt_conv_output_stride(conv);
  if (in_stride < conv->in_channels || weight_stride < conv->out_channels || out_stride < conv->out_channels) {
    (void)prt_rr_release_scope(&scope);
    return PRT_ERR_INVAL;
  }
  output_scale = conv->output_scale != 0.0f ? conv->output_scale : 1.0f;
  use_pointwise_matmul_fallback =
    prt_is_canonical_pointwise_matmul_conv(conv, tiled_type) &&
    prt_pointwise_matmul_strides_supported(conv, in_stride, weight_stride, out_stride);
  gemmini_flush(0);
  if (use_pointwise_matmul_fallback) {
    rc = prt_run_pointwise_matmul_fallback_scoped(stage_id, manager_id, conv, tiled_type, &scope);
  } else if ((safe_kchs_cap = prt_pick_safe_loop_conv_kchs_cap(conv, tiled_type)) > 0) {
    PRT_PROGRESS_HOT_LOG("conv-nb stage=%u mgr=%u tiled-conv-begin in_stride=%d weight_stride=%d out_stride=%d safe-kchs-cap=%d",
                         stage_id, manager_id, in_stride, weight_stride, out_stride, safe_kchs_cap);
    tiled_conv_stride_auto_capped_kchs(
      conv->batch_size, conv->in_row_dim, conv->in_col_dim, conv->in_channels,
      conv->out_channels, conv->out_row_dim, conv->out_col_dim,
      conv->stride, input_dilation, kernel_dilation, conv->padding, conv->kernel_dim,
      in_stride, weight_stride, out_stride,
      conv->wrot180 != 0, conv->trans_output_1203 != 0, conv->trans_input_3120 != 0,
      conv->trans_weight_1203 != 0, conv->trans_weight_0132 != 0,
      (const elem_t *)conv->input, (const elem_t *)conv->weights,
      (const acc_t *)conv->bias, (elem_t *)conv->output,
      conv->act, (acc_scale_t)output_scale,
      conv->pool_size, conv->pool_stride, conv->pool_padding,
      tiled_type, safe_kchs_cap, chosen_args);
    PRT_PROGRESS_HOT_LOG("conv-nb stage=%u mgr=%u tiled-conv-safe-args batches=%d porows=%d pocols=%d pochs=%d krows=%d kcols=%d kchs=%d",
                         stage_id, manager_id,
                         chosen_args[0], chosen_args[1], chosen_args[2], chosen_args[3],
                         chosen_args[4], chosen_args[5], chosen_args[6]);
    rc = PRT_OK;
  } else {
    PRT_PROGRESS_HOT_LOG("conv-nb stage=%u mgr=%u tiled-conv-begin in_stride=%d weight_stride=%d out_stride=%d",
                         stage_id, manager_id, in_stride, weight_stride, out_stride);
    tiled_conv_stride_auto(
      conv->batch_size, conv->in_row_dim, conv->in_col_dim, conv->in_channels,
      conv->out_channels, conv->out_row_dim, conv->out_col_dim,
      conv->stride, input_dilation, kernel_dilation, conv->padding, conv->kernel_dim,
      in_stride, weight_stride, out_stride,
      conv->wrot180 != 0, conv->trans_output_1203 != 0, conv->trans_input_3120 != 0,
      conv->trans_weight_1203 != 0, conv->trans_weight_0132 != 0,
      (const elem_t *)conv->input, (const elem_t *)conv->weights,
      (const acc_t *)conv->bias, (elem_t *)conv->output,
      conv->act, (acc_scale_t)output_scale,
      conv->pool_size, conv->pool_stride, conv->pool_padding,
      tiled_type);
    PRT_PROGRESS_HOT_LOG("conv-nb stage=%u mgr=%u tiled-conv-end", stage_id, manager_id);
    rc = PRT_OK;
  }
  if (rc != PRT_OK) {
    (void)prt_rr_release_scope(&scope);
    return rc;
  }

  (void)prt_rr_release_scope(&scope);
  return PRT_OK;
}

static int conv_call_for_manager_sync(uint32_t stage_id, uint32_t manager_id,
                                      const prt_gemmini_conv_desc_t *conv) {
  prt_rr_scope_t scope;
  enum tiled_matmul_type_t tiled_type = WS;
  int input_dilation;
  int kernel_dilation;
  int in_stride;
  int weight_stride;
  int out_stride;
  int use_pointwise_matmul_fallback = 0;
  int safe_kchs_cap = 0;
  int chosen_args[7] = {0};
  float output_scale;
  int rc;
  if (!conv) return PRT_ERR_INVAL;

  PRT_PROGRESS_HOT_LOG("conv-sync stage=%u mgr=%u acquire-begin out_ch=%d out_dim=%dx%d in_dim=%dx%d",
                       stage_id, manager_id, conv->out_channels,
                       conv->out_row_dim, conv->out_col_dim,
                       conv->in_row_dim, conv->in_col_dim);

  rc = prt_rr_acquire_scope(NULL, stage_id, manager_id, 3U, &scope);
  if (rc != PRT_OK) return rc;
  PRT_PROGRESS_HOT_LOG("conv-sync stage=%u mgr=%u acquire-end", stage_id, manager_id);

  tiled_type = prt_conv_tiled_type(conv);

  input_dilation = conv->input_dilation > 0 ? conv->input_dilation : 1;
  kernel_dilation = conv->kernel_dilation > 0 ? conv->kernel_dilation : 1;
  in_stride = prt_conv_input_stride(conv);
  weight_stride = prt_conv_weight_stride(conv);
  out_stride = prt_conv_output_stride(conv);
  if (in_stride < conv->in_channels || weight_stride < conv->out_channels || out_stride < conv->out_channels) {
    (void)prt_rr_release_scope(&scope);
    return PRT_ERR_INVAL;
  }
  output_scale = conv->output_scale != 0.0f ? conv->output_scale : 1.0f;
  use_pointwise_matmul_fallback =
    prt_is_canonical_pointwise_matmul_conv(conv, tiled_type) &&
    prt_pointwise_matmul_strides_supported(conv, in_stride, weight_stride, out_stride);
  gemmini_flush(0);
  if (use_pointwise_matmul_fallback) {
    rc = prt_run_pointwise_matmul_fallback_scoped(stage_id, manager_id, conv, tiled_type, &scope);
  } else if ((safe_kchs_cap = prt_pick_safe_loop_conv_kchs_cap(conv, tiled_type)) > 0) {
    PRT_PROGRESS_HOT_LOG("conv-sync stage=%u mgr=%u tiled-conv-begin in_stride=%d weight_stride=%d out_stride=%d safe-kchs-cap=%d",
                         stage_id, manager_id, in_stride, weight_stride, out_stride, safe_kchs_cap);
    tiled_conv_stride_auto_capped_kchs(
      conv->batch_size, conv->in_row_dim, conv->in_col_dim, conv->in_channels,
      conv->out_channels, conv->out_row_dim, conv->out_col_dim,
      conv->stride, input_dilation, kernel_dilation, conv->padding, conv->kernel_dim,
      in_stride, weight_stride, out_stride,
      conv->wrot180 != 0, conv->trans_output_1203 != 0, conv->trans_input_3120 != 0,
      conv->trans_weight_1203 != 0, conv->trans_weight_0132 != 0,
      (const elem_t *)conv->input, (const elem_t *)conv->weights,
      (const acc_t *)conv->bias, (elem_t *)conv->output,
      conv->act, (acc_scale_t)output_scale,
      conv->pool_size, conv->pool_stride, conv->pool_padding,
      tiled_type, safe_kchs_cap, chosen_args);
    PRT_PROGRESS_HOT_LOG("conv-sync stage=%u mgr=%u tiled-conv-safe-args batches=%d porows=%d pocols=%d pochs=%d krows=%d kcols=%d kchs=%d",
                         stage_id, manager_id,
                         chosen_args[0], chosen_args[1], chosen_args[2], chosen_args[3],
                         chosen_args[4], chosen_args[5], chosen_args[6]);
    rc = PRT_OK;
  } else {
    PRT_PROGRESS_HOT_LOG("conv-sync stage=%u mgr=%u tiled-conv-begin in_stride=%d weight_stride=%d out_stride=%d",
                         stage_id, manager_id, in_stride, weight_stride, out_stride);
    tiled_conv_stride_auto(
      conv->batch_size, conv->in_row_dim, conv->in_col_dim, conv->in_channels,
      conv->out_channels, conv->out_row_dim, conv->out_col_dim,
      conv->stride, input_dilation, kernel_dilation, conv->padding, conv->kernel_dim,
      in_stride, weight_stride, out_stride,
      conv->wrot180 != 0, conv->trans_output_1203 != 0, conv->trans_input_3120 != 0,
      conv->trans_weight_1203 != 0, conv->trans_weight_0132 != 0,
      (const elem_t *)conv->input, (const elem_t *)conv->weights,
      (const acc_t *)conv->bias, (elem_t *)conv->output,
      conv->act, (acc_scale_t)output_scale,
      conv->pool_size, conv->pool_stride, conv->pool_padding,
      tiled_type);
    rc = PRT_OK;
  }
  if (rc != PRT_OK) {
    (void)prt_rr_release_scope(&scope);
    return rc;
  }
  if (!use_pointwise_matmul_fallback) {
    PRT_PROGRESS_HOT_LOG("conv-sync stage=%u mgr=%u tiled-conv-end", stage_id, manager_id);
  }

  PRT_PROGRESS_HOT_LOG("conv-sync stage=%u mgr=%u fence-begin", stage_id, manager_id);
  rc = prt_rr_fence_scope(&scope);
  gemmini_fence();
  if (rc == PRT_OK) rc = flush_scope_after_drain(&scope);
  PRT_PROGRESS_HOT_LOG("conv-sync stage=%u mgr=%u fence-end rc=%d", stage_id, manager_id, rc);
  (void)prt_rr_release_scope(&scope);
  return rc;
}

static int conv_call_for_manager_sync_strided(uint32_t stage_id, uint32_t manager_id,
                                              const prt_gemmini_conv_desc_t *conv,
                                              int in_stride, int weight_stride,
                                              int out_stride) {
  prt_rr_scope_t scope;
  enum tiled_matmul_type_t tiled_type = WS;
  int input_dilation;
  int kernel_dilation;
  int use_pointwise_matmul_fallback = 0;
  int safe_kchs_cap = 0;
  int chosen_args[7] = {0};
  float output_scale;
  int rc;
  if (!conv) return PRT_ERR_INVAL;

  PRT_PROGRESS_LOG("conv-sync-strided stage=%u mgr=%u begin oc=%d out_dim=%dx%d in_dim=%dx%d in_stride=%d weight_stride=%d out_stride=%d",
                   stage_id, manager_id, conv->out_channels,
                   conv->out_row_dim, conv->out_col_dim,
                   conv->in_row_dim, conv->in_col_dim,
                   in_stride, weight_stride, out_stride);

  PRT_PROGRESS_HOT_LOG("conv-sync stage=%u mgr=%u acquire-begin out_ch=%d out_dim=%dx%d in_dim=%dx%d",
                       stage_id, manager_id, conv->out_channels,
                       conv->out_row_dim, conv->out_col_dim,
                       conv->in_row_dim, conv->in_col_dim);

  rc = prt_rr_acquire_scope(NULL, stage_id, manager_id, 3U, &scope);
  if (rc != PRT_OK) return rc;
  PRT_PROGRESS_LOG("conv-sync-strided stage=%u mgr=%u acquired", stage_id, manager_id);
  PRT_PROGRESS_HOT_LOG("conv-sync stage=%u mgr=%u acquire-end", stage_id, manager_id);

  tiled_type = prt_conv_tiled_type(conv);

  input_dilation = conv->input_dilation > 0 ? conv->input_dilation : 1;
  kernel_dilation = conv->kernel_dilation > 0 ? conv->kernel_dilation : 1;
  output_scale = conv->output_scale != 0.0f ? conv->output_scale : 1.0f;
  use_pointwise_matmul_fallback =
    prt_is_canonical_pointwise_matmul_conv(conv, tiled_type) &&
    prt_pointwise_matmul_strides_supported(conv, in_stride, weight_stride, out_stride);
  gemmini_flush(0);
  PRT_PROGRESS_LOG("conv-sync-strided stage=%u mgr=%u flushed use_pointwise=%d", stage_id, manager_id,
                   use_pointwise_matmul_fallback);
  if (use_pointwise_matmul_fallback) {
    PRT_PROGRESS_LOG("conv-sync-strided stage=%u mgr=%u dispatch=pointwise", stage_id, manager_id);
    rc = prt_run_pointwise_matmul_fallback_strided_scoped(stage_id, manager_id, conv, tiled_type,
                                                          in_stride, weight_stride, out_stride,
                                                          &scope);
    PRT_PROGRESS_LOG("conv-sync-strided stage=%u mgr=%u dispatch=pointwise-return rc=%d",
                     stage_id, manager_id, rc);
  } else if ((safe_kchs_cap = prt_pick_safe_loop_conv_kchs_cap(conv, tiled_type)) > 0) {
    PRT_PROGRESS_LOG("conv-sync-strided stage=%u mgr=%u dispatch=conv-loop-capped cap=%d",
                     stage_id, manager_id, safe_kchs_cap);
    PRT_PROGRESS_HOT_LOG("conv-sync stage=%u mgr=%u tiled-conv-begin in_stride=%d weight_stride=%d out_stride=%d safe-kchs-cap=%d",
                         stage_id, manager_id, in_stride, weight_stride, out_stride, safe_kchs_cap);
    tiled_conv_stride_auto_capped_kchs(
      conv->batch_size, conv->in_row_dim, conv->in_col_dim, conv->in_channels,
      conv->out_channels, conv->out_row_dim, conv->out_col_dim,
      conv->stride, input_dilation, kernel_dilation, conv->padding, conv->kernel_dim,
      in_stride, weight_stride, out_stride,
      conv->wrot180 != 0, conv->trans_output_1203 != 0, conv->trans_input_3120 != 0,
      conv->trans_weight_1203 != 0, conv->trans_weight_0132 != 0,
      (const elem_t *)conv->input, (const elem_t *)conv->weights,
      (const acc_t *)conv->bias, (elem_t *)conv->output,
      conv->act, (acc_scale_t)output_scale,
      conv->pool_size, conv->pool_stride, conv->pool_padding,
      tiled_type, safe_kchs_cap, chosen_args);
    PRT_PROGRESS_HOT_LOG("conv-sync stage=%u mgr=%u tiled-conv-safe-args batches=%d porows=%d pocols=%d pochs=%d krows=%d kcols=%d kchs=%d",
                         stage_id, manager_id,
                         chosen_args[0], chosen_args[1], chosen_args[2], chosen_args[3],
                         chosen_args[4], chosen_args[5], chosen_args[6]);
    rc = PRT_OK;
  } else {
    PRT_PROGRESS_LOG("conv-sync-strided stage=%u mgr=%u dispatch=conv-loop", stage_id, manager_id);
    PRT_PROGRESS_HOT_LOG("conv-sync stage=%u mgr=%u tiled-conv-begin in_stride=%d weight_stride=%d out_stride=%d",
                         stage_id, manager_id, in_stride, weight_stride, out_stride);
    tiled_conv_stride_auto(
      conv->batch_size, conv->in_row_dim, conv->in_col_dim, conv->in_channels,
      conv->out_channels, conv->out_row_dim, conv->out_col_dim,
      conv->stride, input_dilation, kernel_dilation, conv->padding, conv->kernel_dim,
      in_stride, weight_stride, out_stride,
      conv->wrot180 != 0, conv->trans_output_1203 != 0, conv->trans_input_3120 != 0,
      conv->trans_weight_1203 != 0, conv->trans_weight_0132 != 0,
      (const elem_t *)conv->input, (const elem_t *)conv->weights,
      (const acc_t *)conv->bias, (elem_t *)conv->output,
      conv->act, (acc_scale_t)output_scale,
      conv->pool_size, conv->pool_stride, conv->pool_padding,
      tiled_type);
    rc = PRT_OK;
  }
  if (rc != PRT_OK) {
    (void)prt_rr_release_scope(&scope);
    return rc;
  }
  PRT_PROGRESS_LOG("conv-sync-strided stage=%u mgr=%u issue-done use_pointwise=%d", stage_id,
                   manager_id, use_pointwise_matmul_fallback);
  if (!use_pointwise_matmul_fallback) {
    PRT_PROGRESS_HOT_LOG("conv-sync stage=%u mgr=%u tiled-conv-end", stage_id, manager_id);
  }

  PRT_PROGRESS_LOG("conv-sync-strided stage=%u mgr=%u fence-begin", stage_id, manager_id);
  PRT_PROGRESS_HOT_LOG("conv-sync stage=%u mgr=%u fence-begin", stage_id, manager_id);
  rc = prt_rr_fence_scope(&scope);
  gemmini_fence();
  if (rc == PRT_OK) rc = flush_scope_after_drain(&scope);
  PRT_PROGRESS_LOG("conv-sync-strided stage=%u mgr=%u fence-end rc=%d", stage_id, manager_id, rc);
  PRT_PROGRESS_HOT_LOG("conv-sync stage=%u mgr=%u fence-end rc=%d", stage_id, manager_id, rc);
  (void)prt_rr_release_scope(&scope);
  return rc;
}

static int resadd_issue_scoped(const prt_gemmini_resadd_desc_t *resadd,
                               enum tiled_matmul_type_t matadd_type,
                               prt_rr_scope_t *scope) {
  if (!resadd) return PRT_ERR_INVAL;
  (void)scope;

  if (matadd_type == CPU) {
    resadd_cpu(resadd->I, resadd->J, resadd->stride,
               (scale_t)resadd->A_scale, (scale_t)resadd->B_scale,
               (acc_scale_t)resadd->C_scale,
               (const elem_t *)resadd->A, (const elem_t *)resadd->B, (elem_t *)resadd->C,
               resadd->relu != 0);
    return PRT_OK;
  }
  if (matadd_type != WS) return PRT_ERR_NOT_IMPL;

  // 2026-03-23 FPGA baremetal differential testing showed the handwritten
  // explicit mvin/mvin2/mvout workaround is the bug: it fails on interleaved
  // shared-spad aliases, while both the canonical WS resadd path and a fully
  // serialized explicit sequence pass on the same mappings. Keep the runtime on
  // the standard Gemmini interface here.
  tiled_resadd_stride_auto(resadd->I, resadd->J,
                           (scale_t)resadd->A_scale,
                           (scale_t)resadd->B_scale,
                           (acc_scale_t)resadd->C_scale,
                           resadd->stride,
                           (const elem_t *)resadd->A,
                           (const elem_t *)resadd->B,
                           (elem_t *)resadd->C,
                           resadd->relu != 0,
                           WS);
  return PRT_OK;
}

static int resadd_call_for_manager_nb(uint32_t stage_id, uint32_t manager_id,
                                      const prt_gemmini_resadd_desc_t *resadd) {
  prt_rr_scope_t scope;
  enum tiled_matmul_type_t matadd_type;
  int rc;
  if (!resadd) return PRT_ERR_INVAL;
  matadd_type = prt_pick_resadd_type(resadd);

  PRT_PROGRESS_HOT_LOG("resadd-nb stage=%u mgr=%u acquire-begin I=%lu J=%lu stride=%lu requested_type=%s",
                       stage_id, manager_id,
                       (unsigned long)resadd->I, (unsigned long)resadd->J,
                       (unsigned long)resadd->stride,
                       prt_tiled_matmul_type_name(matadd_type));
  rc = prt_rr_acquire_scope(NULL, stage_id, manager_id, 3U, &scope);
  if (rc != PRT_OK) return rc;
  PRT_PROGRESS_HOT_LOG("resadd-nb stage=%u mgr=%u acquire-end", stage_id, manager_id);

  gemmini_flush(0);
  PRT_PROGRESS_HOT_LOG("resadd-nb stage=%u mgr=%u issue-begin I=%lu J=%lu stride=%lu requested_type=%s",
                       stage_id, manager_id,
                       (unsigned long)resadd->I, (unsigned long)resadd->J,
                       (unsigned long)resadd->stride,
                       prt_tiled_matmul_type_name(matadd_type));
  rc = resadd_issue_scoped(resadd, matadd_type, &scope);
  PRT_PROGRESS_HOT_LOG("resadd-nb stage=%u mgr=%u issue-end rc=%d", stage_id, manager_id, rc);
  (void)prt_rr_release_scope(&scope);
  return rc;
}

static int resadd_call_for_manager_sync(uint32_t stage_id, uint32_t manager_id,
                                        const prt_gemmini_resadd_desc_t *resadd) {
  prt_rr_scope_t scope;
  enum tiled_matmul_type_t matadd_type;
  int rc;
  if (!resadd) return PRT_ERR_INVAL;
  matadd_type = prt_pick_resadd_type(resadd);

  PRT_PROGRESS_HOT_LOG("resadd-sync stage=%u mgr=%u acquire-begin I=%lu J=%lu stride=%lu requested_type=%s",
                       stage_id, manager_id,
                       (unsigned long)resadd->I, (unsigned long)resadd->J,
                       (unsigned long)resadd->stride,
                       prt_tiled_matmul_type_name(matadd_type));
  rc = prt_rr_acquire_scope(NULL, stage_id, manager_id, 3U, &scope);
  if (rc != PRT_OK) return rc;
  PRT_PROGRESS_HOT_LOG("resadd-sync stage=%u mgr=%u acquire-end", stage_id, manager_id);

  gemmini_flush(0);
  PRT_PROGRESS_HOT_LOG("resadd-sync stage=%u mgr=%u issue-begin I=%lu J=%lu stride=%lu requested_type=%s",
                       stage_id, manager_id,
                       (unsigned long)resadd->I, (unsigned long)resadd->J,
                       (unsigned long)resadd->stride,
                       prt_tiled_matmul_type_name(matadd_type));
  rc = resadd_issue_scoped(resadd, matadd_type, &scope);
  PRT_PROGRESS_HOT_LOG("resadd-sync stage=%u mgr=%u issue-end rc=%d", stage_id, manager_id, rc);
  if (rc == PRT_OK) {
    PRT_PROGRESS_HOT_LOG("resadd-sync stage=%u mgr=%u fence-begin", stage_id, manager_id);
    rc = prt_rr_fence_scope(&scope);
    gemmini_fence();
    if (rc == PRT_OK) rc = flush_scope_after_drain(&scope);
    PRT_PROGRESS_HOT_LOG("resadd-sync stage=%u mgr=%u fence-end rc=%d", stage_id, manager_id, rc);
  }
  (void)prt_rr_release_scope(&scope);
  return rc;
}

static int run_conv_oc_split(const prt_conv_task_t *task, const prt_gemmini_conv_desc_t *conv) {
  uint32_t tiles;
  if (!task || !conv) return PRT_ERR_INVAL;

  tiles = task->tile_count > 0 ? task->tile_count : 1U;
  if (tiles <= 1U) {
    return conv_call_for_manager_nb(task->stage_id, task->acc_id, conv);
  }
  if (tiles > PRT_MAX_TILE_SPLITS) return PRT_ERR_INVAL;
  if ((uint32_t)conv->out_channels < tiles) return PRT_ERR_NOT_IMPL;

  PRT_PROGRESS_HOT_ERR_LOG("oc-split-enter stage=%u tiles=%u out_ch=%d out_dim=%dx%d in_dim=%dx%d",
                           task->stage_id, tiles, conv->out_channels,
                           conv->out_row_dim, conv->out_col_dim,
                           conv->in_row_dim, conv->in_col_dim);
  PRT_PROGRESS_HOT_LOG("oc-split stage=%u tiles=%u out_ch=%d out_dim=%dx%d begin",
                       task->stage_id, tiles, conv->out_channels,
                       conv->out_row_dim, conv->out_col_dim);

#if defined(__riscv)
  {
    enum tiled_matmul_type_t tiled_type = prt_conv_tiled_type(conv);
    if (prt_is_canonical_pointwise_matmul_conv(conv, tiled_type) &&
        prt_pointwise_matmul_strides_supported(conv, prt_conv_input_stride(conv),
                                               prt_conv_weight_stride(conv),
                                               prt_conv_output_stride(conv))) {
      const elem_t *weights = (const elem_t *)conv->weights;
      const acc_t *bias = (const acc_t *)conv->bias;
      elem_t *output = (elem_t *)conv->output;
      const int full_weight_stride = prt_conv_weight_stride(conv);
      const int full_out_stride = prt_conv_output_stride(conv);

      PRT_PROGRESS_HOT_LOG("oc-split stage=%u mode=pointwise-direct-strided weight_stride=%d out_stride=%d",
                           task->stage_id, full_weight_stride, full_out_stride);

      for (uint32_t t = 0; t < tiles; ++t) {
        uint32_t oc_beg;
        uint32_t oc_end;
        uint32_t oc_tile;
        prt_gemmini_conv_desc_t sub = *conv;
        uint32_t mgr;
        int rc;

        split_1d_range((uint32_t)conv->out_channels, tiles, t, &oc_beg, &oc_end);
        if (oc_end <= oc_beg) continue;
        oc_tile = oc_end - oc_beg;
        mgr = task->manager_ids[t % task->num_managers];

        sub.out_channels = (int)oc_tile;
        sub.weights = weights + oc_beg;
        sub.bias = bias ? (bias + oc_beg) : NULL;
        sub.output = output + oc_beg;

        PRT_PROGRESS_LOG("oc-split-pointwise stage=%u tile=%u/%u mgr=%u oc_beg=%u oc_tile=%u in_stride=%d weight_stride=%d out_stride=%d begin",
                         task->stage_id, t, tiles, mgr, oc_beg, oc_tile,
                         prt_conv_input_stride(conv), full_weight_stride, full_out_stride);
        PRT_PROGRESS_HOT_LOG("oc-split stage=%u tile=%u/%u mgr=%u oc_beg=%u oc_tile=%u launch mode=pointwise-direct-strided",
                             task->stage_id, t, tiles, mgr, oc_beg, oc_tile);
        rc = conv_call_for_manager_sync_strided(task->stage_id, mgr, &sub,
                                                prt_conv_input_stride(conv),
                                                full_weight_stride,
                                                full_out_stride);
        if (rc != PRT_OK) return rc;
        PRT_PROGRESS_LOG("oc-split-pointwise stage=%u tile=%u/%u mgr=%u oc_beg=%u oc_tile=%u end",
                         task->stage_id, t, tiles, mgr, oc_beg, oc_tile);
        PRT_PROGRESS_HOT_LOG("oc-split stage=%u tile=%u/%u mgr=%u done mode=pointwise-direct-strided",
                             task->stage_id, t, tiles, mgr);
      }

      return PRT_OK;
    }
  }
#endif

#if defined(__riscv) && defined(PRT_ENABLE_GEMMINI_OCSPLIT_DIRECT_STRIDED)
  // The direct-strided split-OC path is useful for experimentation, but on the
  // current FPGA stack it can stall inside tiled_conv_stride_auto(). Keep the
  // older contiguous pack/repack flow as the default until that path is proven.
  if (conv->trans_output_1203 == 0 &&
      conv->trans_input_3120 == 0 &&
      conv->trans_weight_1203 == 0 &&
      conv->trans_weight_0132 == 0) {
    const elem_t *weights = (const elem_t *)conv->weights;
    const acc_t *bias = (const acc_t *)conv->bias;
    elem_t *output = (elem_t *)conv->output;
    const int full_weight_stride = prt_conv_weight_stride(conv);
    const int full_out_stride = prt_conv_output_stride(conv);

    PRT_PROGRESS_HOT_LOG("oc-split stage=%u mode=direct-strided weight_stride=%d out_stride=%d",
                         task->stage_id, full_weight_stride, full_out_stride);

    for (uint32_t t = 0; t < tiles; ++t) {
      uint32_t oc_beg;
      uint32_t oc_end;
      uint32_t oc_tile;
      prt_gemmini_conv_desc_t sub = *conv;
      uint32_t mgr;
      int rc;

      split_1d_range((uint32_t)conv->out_channels, tiles, t, &oc_beg, &oc_end);
      if (oc_end <= oc_beg) continue;
      oc_tile = oc_end - oc_beg;
      mgr = task->manager_ids[t % task->num_managers];

      sub.out_channels = (int)oc_tile;
      sub.weights = weights + oc_beg;
      sub.bias = bias ? (bias + oc_beg) : NULL;
      sub.output = output + oc_beg;

      PRT_PROGRESS_HOT_LOG("oc-split stage=%u tile=%u/%u mgr=%u oc_beg=%u oc_tile=%u launch",
                           task->stage_id, t, tiles, mgr, oc_beg, oc_tile);
      rc = conv_call_for_manager_sync_strided(task->stage_id, mgr, &sub,
                                              prt_conv_input_stride(conv),
                                              full_weight_stride,
                                              full_out_stride);
      if (rc != PRT_OK) return rc;
      PRT_PROGRESS_HOT_LOG("oc-split stage=%u tile=%u/%u mgr=%u done",
                           task->stage_id, t, tiles, mgr);
    }

    return PRT_OK;
  }
#endif

  {
    uint32_t k_rows;
    const elem_t *weights;
    const acc_t *bias;
    elem_t *output;
    prt_conv_oc_split_tile_t tile_ctx[PRT_MAX_TILE_SPLITS];

    memset(tile_ctx, 0, sizeof(tile_ctx));
    weights = (const elem_t *)conv->weights;
    bias = (const acc_t *)conv->bias;
    output = (elem_t *)conv->output;
    k_rows = (uint32_t)(conv->kernel_dim * conv->kernel_dim * conv->in_channels);

    for (uint32_t t = 0; t < tiles; ++t) {
      uint32_t oc_beg;
      uint32_t oc_end;
      uint32_t oc_tile;
      elem_t *w_pack;
      acc_t *b_pack;
      elem_t *o_pack;
      prt_gemmini_conv_desc_t sub = *conv;
      uint32_t mgr;
      split_1d_range((uint32_t)conv->out_channels, tiles, t, &oc_beg, &oc_end);
      if (oc_end <= oc_beg) continue;
      oc_tile = oc_end - oc_beg;
      mgr = task->manager_ids[t % task->num_managers];

      w_pack = (elem_t *)prt_gemmini_alloc_aligned(sizeof(elem_t) * (size_t)k_rows * (size_t)oc_tile, 0);
      b_pack = (acc_t *)prt_gemmini_alloc_aligned(sizeof(acc_t) * (size_t)oc_tile, 0);
      o_pack = (elem_t *)prt_gemmini_alloc_aligned(sizeof(elem_t) *
                                                   (size_t)conv->batch_size *
                                                   (size_t)conv->out_row_dim *
                                                   (size_t)conv->out_col_dim *
                                                   (size_t)oc_tile,
                                                   1);
      if (!w_pack || !b_pack || !o_pack) {
        free(w_pack);
        free(b_pack);
        free(o_pack);
        return PRT_ERR_NOMEM;
      }

      for (uint32_t r = 0; r < k_rows; ++r) {
        memcpy(w_pack + (size_t)r * oc_tile,
               weights + (size_t)r * conv->out_channels + oc_beg,
               sizeof(elem_t) * oc_tile);
      }
      if (bias) memcpy(b_pack, bias + oc_beg, sizeof(acc_t) * oc_tile);
      else memset(b_pack, 0, sizeof(acc_t) * oc_tile);
      sub.out_channels = (int)oc_tile;
      sub.weights = w_pack;
      sub.bias = b_pack;
      sub.output = o_pack;
      PRT_PROGRESS_HOT_LOG("oc-split stage=%u tile=%u/%u mgr=%u oc_beg=%u oc_tile=%u launch",
                           task->stage_id, t, tiles, mgr, oc_beg, oc_tile);

      {
        int rc = conv_call_for_manager_sync(task->stage_id, mgr, &sub);
        if (rc != PRT_OK) {
          free(w_pack);
          free(b_pack);
          free(o_pack);
          cleanup_conv_oc_split_tiles(tile_ctx, tiles);
          return rc;
        }
      }
      PRT_PROGRESS_HOT_LOG("oc-split stage=%u tile=%u/%u mgr=%u done",
                           task->stage_id, t, tiles, mgr);
      tile_ctx[t].oc_beg = oc_beg;
      tile_ctx[t].oc_tile = oc_tile;
      tile_ctx[t].w_pack = w_pack;
      tile_ctx[t].b_pack = b_pack;
      tile_ctx[t].o_pack = o_pack;
    }

    PRT_PROGRESS_HOT_LOG("oc-split stage=%u repack-begin tiles=%u", task->stage_id, tiles);
    for (uint32_t t = 0; t < tiles; ++t) {
      uint32_t oc_beg = tile_ctx[t].oc_beg;
      uint32_t oc_tile = tile_ctx[t].oc_tile;
      elem_t *o_pack = tile_ctx[t].o_pack;
      if (!o_pack || oc_tile == 0) continue;
      for (int b = 0; b < conv->batch_size; ++b) {
        for (int oh = 0; oh < conv->out_row_dim; ++oh) {
          for (int ow = 0; ow < conv->out_col_dim; ++ow) {
            size_t dst_off = ((size_t)b * conv->out_row_dim * conv->out_col_dim +
                              (size_t)oh * conv->out_col_dim + (size_t)ow) * (size_t)conv->out_channels + oc_beg;
            size_t src_off = ((size_t)b * conv->out_row_dim * conv->out_col_dim +
                              (size_t)oh * conv->out_col_dim + (size_t)ow) * (size_t)oc_tile;
            memcpy(output + dst_off, o_pack + src_off, sizeof(elem_t) * oc_tile);
          }
        }
      }
    }

    PRT_PROGRESS_HOT_LOG("oc-split stage=%u repack-end tiles=%u", task->stage_id, tiles);
    cleanup_conv_oc_split_tiles(tile_ctx, tiles);
    return PRT_OK;
  }
}

static int run_conv_spatial_split(const prt_conv_task_t *task, const prt_gemmini_conv_desc_t *conv) {
  uint32_t tiles;
  const elem_t *input;
  const elem_t *weights;
  const acc_t *bias;
  elem_t *output;
  prt_rect2d_t rects[PRT_MAX_TILE_SPLITS];
  prt_conv_spatial_split_tile_t tile_ctx[PRT_MAX_TILE_SPLITS];
  int rc;
  if (!task || !conv) return PRT_ERR_INVAL;
  tiles = task->tile_count > 0 ? task->tile_count : 1U;
  if (tiles <= 1U) return conv_call_for_manager_nb(task->stage_id, task->acc_id, conv);
  if (tiles > PRT_MAX_TILE_SPLITS) return PRT_ERR_INVAL;

  rc = partition_2d_rects((uint32_t)conv->out_row_dim, (uint32_t)conv->out_col_dim, tiles, rects);
  if (rc != PRT_OK) return rc;

  memset(tile_ctx, 0, sizeof(tile_ctx));
  input = (const elem_t *)conv->input;
  weights = (const elem_t *)conv->weights;
  bias = (const acc_t *)conv->bias;
  output = (elem_t *)conv->output;

  for (uint32_t t = 0; t < tiles; ++t) {
    uint32_t oh_beg = rects[t].r0;
    uint32_t oh_end = rects[t].r1;
    uint32_t ow_beg = rects[t].c0;
    uint32_t ow_end = rects[t].c1;
    int tile_oh = (int)(oh_end - oh_beg);
    int tile_ow = (int)(ow_end - ow_beg);
    int in_h;
    int in_w;
    int in_row0;
    int in_col0;
    elem_t *in_pack;
    elem_t *out_pack;
    prt_gemmini_conv_desc_t sub = *conv;
    uint32_t mgr = task->manager_ids[t % task->num_managers];

    if (tile_oh <= 0 || tile_ow <= 0) return PRT_ERR_NOT_IMPL;

    in_row0 = (int)oh_beg * conv->stride - conv->padding;
    in_col0 = (int)ow_beg * conv->stride - conv->padding;
    in_h = (tile_oh - 1) * conv->stride + conv->kernel_dim;
    in_w = (tile_ow - 1) * conv->stride + conv->kernel_dim;
    if (in_h <= 0 || in_w <= 0) return PRT_ERR_INVAL;

    in_pack = (elem_t *)prt_gemmini_alloc_aligned(sizeof(elem_t) *
                                                  (size_t)conv->batch_size *
                                                  (size_t)in_h *
                                                  (size_t)in_w *
                                                  (size_t)conv->in_channels,
                                                  1);
    out_pack = (elem_t *)prt_gemmini_alloc_aligned(sizeof(elem_t) *
                                                   (size_t)conv->batch_size *
                                                   (size_t)tile_oh *
                                                   (size_t)tile_ow *
                                                   (size_t)conv->out_channels,
                                                   1);
    if (!in_pack || !out_pack) {
      free(in_pack);
      free(out_pack);
      return PRT_ERR_NOMEM;
    }

    for (int b = 0; b < conv->batch_size; ++b) {
      for (int ih = 0; ih < in_h; ++ih) {
        for (int iw = 0; iw < in_w; ++iw) {
          int src_h = in_row0 + ih;
          int src_w = in_col0 + iw;
          if (src_h < 0 || src_h >= conv->in_row_dim || src_w < 0 || src_w >= conv->in_col_dim) continue;
          memcpy(
            in_pack + (((size_t)b * in_h * in_w) + (size_t)ih * in_w + (size_t)iw) * conv->in_channels,
            input + (((size_t)b * conv->in_row_dim * conv->in_col_dim) +
                     (size_t)src_h * conv->in_col_dim + (size_t)src_w) * conv->in_channels,
            sizeof(elem_t) * conv->in_channels);
        }
      }
    }

    sub.in_row_dim = in_h;
    sub.in_col_dim = in_w;
    sub.out_row_dim = tile_oh;
    sub.out_col_dim = tile_ow;
    sub.padding = 0;
    sub.input = in_pack;
    sub.weights = weights;
    sub.bias = bias;
    sub.output = out_pack;

    rc = conv_call_for_manager_sync(task->stage_id, mgr, &sub);
    if (rc != PRT_OK) {
      free(in_pack);
      free(out_pack);
      cleanup_conv_spatial_split_tiles(tile_ctx, tiles);
      return rc;
    }
    tile_ctx[t].oh_beg = oh_beg;
    tile_ctx[t].ow_beg = ow_beg;
    tile_ctx[t].tile_oh = tile_oh;
    tile_ctx[t].tile_ow = tile_ow;
    tile_ctx[t].in_pack = in_pack;
    tile_ctx[t].out_pack = out_pack;
  }

  for (uint32_t t = 0; t < tiles; ++t) {
    uint32_t oh_beg = tile_ctx[t].oh_beg;
    uint32_t ow_beg = tile_ctx[t].ow_beg;
    int tile_oh = tile_ctx[t].tile_oh;
    int tile_ow = tile_ctx[t].tile_ow;
    elem_t *out_pack = tile_ctx[t].out_pack;
    if (!out_pack || tile_oh <= 0 || tile_ow <= 0) continue;
    for (int b = 0; b < conv->batch_size; ++b) {
      for (int oh = 0; oh < tile_oh; ++oh) {
        int dst_oh = (int)oh_beg + oh;
        for (int ow = 0; ow < tile_ow; ++ow) {
          int dst_ow = (int)ow_beg + ow;
          size_t dst_off = ((size_t)b * conv->out_row_dim * conv->out_col_dim +
                            (size_t)dst_oh * conv->out_col_dim + (size_t)dst_ow) * (size_t)conv->out_channels;
          size_t src_off = ((size_t)b * tile_oh * tile_ow +
                            (size_t)oh * tile_ow + (size_t)ow) * (size_t)conv->out_channels;
          memcpy(output + dst_off, out_pack + src_off, sizeof(elem_t) * conv->out_channels);
        }
      }
    }
  }

  cleanup_conv_spatial_split_tiles(tile_ctx, tiles);
  return PRT_OK;
}

static int run_resadd_split(prt_runtime_t *rt, const prt_conv_task_t *task,
                            const prt_gemmini_resadd_desc_t *resadd) {
  uint32_t tiles;
  prt_rect2d_t rects[PRT_MAX_TILE_SPLITS];
  const enum tiled_matmul_type_t requested_type = prt_pick_resadd_type(resadd);
  const int force_cpu_fallback =
    prt_should_force_resadd_cpu_fallback(task, resadd, requested_type);
  int rc;
  if (!task || !resadd) return PRT_ERR_INVAL;
  tiles = task->tile_count > 0 ? task->tile_count : 1U;
  if (tiles <= 1U) {
    if (force_cpu_fallback) {
      return prt_run_resadd_cpu_fallback(rt, task->stage_id, task->manager_ids[0], task, resadd,
                                         requested_type);
    }
    return resadd_call_for_manager_nb(task->stage_id, task->acc_id, resadd);
  }
  if (tiles > PRT_MAX_TILE_SPLITS) return PRT_ERR_INVAL;

  PRT_PROGRESS_LOG("resadd-split stage=%u split=%s tiles=%u acc=%u mgr0=%u mgr1=%u A=0x%llx B=0x%llx C=0x%llx I=%llu J=%llu stride=%llu requested_type=%s fallback=%s begin",
                   task->stage_id,
                   prt_split_kind_name(task->split_kind),
                   tiles,
                   task->acc_id,
                   task->manager_ids[0],
                   task->num_managers > 1 ? task->manager_ids[1] : 0U,
                   (unsigned long long)(uintptr_t)resadd->A,
                   (unsigned long long)(uintptr_t)resadd->B,
                   (unsigned long long)(uintptr_t)resadd->C,
                   (unsigned long long)resadd->I,
                   (unsigned long long)resadd->J,
                   (unsigned long long)resadd->stride,
                   prt_tiled_matmul_type_name(requested_type),
                   force_cpu_fallback ? "CPU" : prt_tiled_matmul_type_name(requested_type));
  PRT_PROGRESS_HOT_LOG("resadd-split stage=%u tiles=%u I=%lu J=%lu stride=%lu split=%s requested_type=%s fallback=%s begin",
                       task->stage_id, tiles,
                       (unsigned long)resadd->I, (unsigned long)resadd->J,
                       (unsigned long)resadd->stride,
                       prt_split_kind_name(task->split_kind),
                       prt_tiled_matmul_type_name(requested_type),
                       force_cpu_fallback ? "CPU" : prt_tiled_matmul_type_name(requested_type));
  rc = partition_2d_rects((uint32_t)resadd->I, (uint32_t)resadd->J, tiles, rects);
  if (rc != PRT_OK) return rc;

  for (uint32_t t = 0; t < tiles; ++t) {
    uint32_t i_beg = rects[t].r0;
    uint32_t i_end = rects[t].r1;
    uint32_t j_beg = rects[t].c0;
    uint32_t j_end = rects[t].c1;
    prt_gemmini_resadd_desc_t sub = *resadd;
    uint32_t mgr = task->manager_ids[t % task->num_managers];

    if (i_end <= i_beg || j_end <= j_beg) return PRT_ERR_NOT_IMPL;

    sub.I = (size_t)(i_end - i_beg);
    sub.J = (size_t)(j_end - j_beg);
    sub.A = (const elem_t *)resadd->A + (size_t)i_beg * resadd->stride + (size_t)j_beg;
    sub.B = (const elem_t *)resadd->B + (size_t)i_beg * resadd->stride + (size_t)j_beg;
    sub.C = (elem_t *)resadd->C + (size_t)i_beg * resadd->stride + (size_t)j_beg;
    PRT_PROGRESS_LOG("resadd-split stage=%u tile=%u/%u mgr=%u rect_r=[%u,%u) rect_c=[%u,%u) subA=0x%llx subB=0x%llx subC=0x%llx I=%llu J=%llu stride=%llu issue",
                     task->stage_id,
                     t,
                     tiles,
                     mgr,
                     i_beg,
                     i_end,
                     j_beg,
                     j_end,
                     (unsigned long long)(uintptr_t)sub.A,
                     (unsigned long long)(uintptr_t)sub.B,
                     (unsigned long long)(uintptr_t)sub.C,
                     (unsigned long long)sub.I,
                     (unsigned long long)sub.J,
                     (unsigned long long)sub.stride);
    PRT_PROGRESS_HOT_LOG("resadd-split stage=%u tile=%u/%u mgr=%u i_beg=%u i_end=%u j_beg=%u j_end=%u launch fallback=%s",
                         task->stage_id, t, tiles, mgr,
                         i_beg, i_end, j_beg, j_end,
                         force_cpu_fallback ? "CPU" : prt_tiled_matmul_type_name(requested_type));
    rc = force_cpu_fallback ?
      prt_run_resadd_cpu_fallback(rt, task->stage_id, mgr, task, &sub, requested_type) :
      resadd_call_for_manager_sync(task->stage_id, mgr, &sub);
    if (rc != PRT_OK) return rc;
    PRT_PROGRESS_HOT_LOG("resadd-split stage=%u tile=%u/%u mgr=%u done",
                         task->stage_id, t, tiles, mgr);
  }

  PRT_PROGRESS_LOG("resadd-split stage=%u split=%s tiles=%u end fallback=%s",
                   task->stage_id,
                   prt_split_kind_name(task->split_kind),
                   tiles,
                   force_cpu_fallback ? "CPU" : prt_tiled_matmul_type_name(requested_type));
  PRT_PROGRESS_HOT_LOG("resadd-split stage=%u tiles=%u end fallback=%s",
                       task->stage_id, tiles,
                       force_cpu_fallback ? "CPU" : prt_tiled_matmul_type_name(requested_type));
  return PRT_OK;
}
#endif

int prt_gemmini_backend_init(prt_runtime_t *rt) {
  if (!rt) return PRT_ERR_INVAL;

  if (rt->cfg.gemmini_mode == PRT_GEMMINI_MODE_BLOCKING_FENCE) {
    rt->gemm_ops.conv_run = gemm_blocking_conv_run;
    rt->gemm_ops.fence = gemm_blocking_fence;
    rt->gemm_ops.name = "blocking_fence";
  } else {
    rt->gemm_ops.conv_run = gemm_async_conv_run;
    rt->gemm_ops.fence = gemm_async_fence;
    rt->gemm_ops.name = "async_experimental";
  }
  return PRT_OK;
}

void prt_gemmini_backend_destroy(prt_runtime_t *rt) {
  (void)rt;
}

int prt_gemm_conv_run(prt_runtime_t *rt, const prt_conv_task_t *task, uint64_t timeout_ns) {
  if (!rt || !task || !rt->gemm_ops.conv_run) return PRT_ERR_INVAL;
  return rt->gemm_ops.conv_run(rt, task, timeout_ns);
}

int prt_gemm_fence(prt_runtime_t *rt, const prt_conv_task_t *task, uint64_t timeout_ns) {
  if (!rt || !rt->gemm_ops.fence) return PRT_ERR_INVAL;
  return rt->gemm_ops.fence(rt, task, timeout_ns);
}

static int gemm_blocking_conv_run(prt_runtime_t *rt, const prt_conv_task_t *task, uint64_t timeout_ns) {
  int rc;
  rc = gemm_issue_task(rt, task);
  if (rc != PRT_OK) return rc;
  if (task_issue_already_fenced(task)) return PRT_OK;
  return gemm_blocking_fence(rt, task, timeout_ns);
}

static int task_issue_already_fenced(const prt_conv_task_t *task) {
  if (!task) return 0;
#if defined(__riscv)
  if (task->op_kind == PRT_STAGE_OP_CONV) {
    return task->split_kind == PRT_LAYER_SPLIT_OC ||
           task->split_kind == PRT_LAYER_SPLIT_SPATIAL;
  }
  if (task->op_kind == PRT_STAGE_OP_RESADD) {
    return task->split_kind == PRT_LAYER_SPLIT_RESADD_SPATIAL ||
           task->split_kind == PRT_LAYER_SPLIT_SPATIAL;
  }
#endif
  return 0;
}

static int gemm_issue_conv_task(prt_runtime_t *rt, const prt_conv_task_t *task,
                                const prt_gemmini_conv_desc_t *conv) {
  if (!rt || !task || !conv) return PRT_ERR_INVAL;

  if (rt->cfg.backend == PRT_BACKEND_CPU) {
    conv_cpu_ref(conv);
    return PRT_OK;
  }

#if !defined(__riscv)
  switch (task->split_kind) {
    case PRT_LAYER_SPLIT_SINGLE:
      conv_cpu_ref(conv);
      return PRT_OK;
    case PRT_LAYER_SPLIT_OC:
      return run_conv_oc_split_host(task, conv);
    case PRT_LAYER_SPLIT_SPATIAL:
      return run_conv_spatial_split_host(task, conv);
    case PRT_LAYER_SPLIT_UNSPEC:
      if (task->tile_count <= 1U) {
        conv_cpu_ref(conv);
        return PRT_OK;
      }
      if ((uint32_t)conv->out_channels >= task->tile_count) return run_conv_oc_split_host(task, conv);
      return run_conv_spatial_split_host(task, conv);
    default:
      return PRT_ERR_NOT_IMPL;
  }
#else
  prt_cache_spm_xlate_cfg(rt);
  if (!conv->input || !conv->weights || !conv->output) return PRT_ERR_INVAL;
  if (conv->batch_size <= 0 || conv->in_row_dim <= 0 || conv->in_col_dim <= 0 ||
      conv->in_channels <= 0 || conv->out_channels <= 0 || conv->out_row_dim <= 0 ||
      conv->out_col_dim <= 0 || conv->kernel_dim <= 0) {
    return PRT_ERR_INVAL;
  }
  PRT_PROGRESS_HOT_ERR_LOG("gemm-issue-conv stage=%u split=%u tiles=%u batch=%d in_dim=%dx%d ic=%d out_dim=%dx%d oc=%d kernel=%d groups=%d",
                           task->stage_id, (uint32_t)task->split_kind, task->tile_count,
                           conv->batch_size, conv->in_row_dim, conv->in_col_dim, conv->in_channels,
                           conv->out_row_dim, conv->out_col_dim, conv->out_channels, conv->kernel_dim,
                           prt_conv_groups(conv));
  switch (task->split_kind) {
    case PRT_LAYER_SPLIT_SINGLE:
      PRT_PROGRESS_HOT_ERR_LOG("gemm-issue-conv stage=%u dispatch=single mgr=%u",
                               task->stage_id, task->manager_ids[0]);
      return conv_call_for_manager_nb(task->stage_id, task->manager_ids[0], conv);
    case PRT_LAYER_SPLIT_OC:
      PRT_PROGRESS_HOT_ERR_LOG("gemm-issue-conv stage=%u dispatch=oc-split", task->stage_id);
      return run_conv_oc_split(task, conv);
    case PRT_LAYER_SPLIT_SPATIAL:
      PRT_PROGRESS_HOT_ERR_LOG("gemm-issue-conv stage=%u dispatch=spatial-split", task->stage_id);
      return run_conv_spatial_split(task, conv);
    case PRT_LAYER_SPLIT_UNSPEC:
      if (task->tile_count <= 1U) {
        PRT_PROGRESS_HOT_ERR_LOG("gemm-issue-conv stage=%u dispatch=unspec-single mgr=%u",
                                 task->stage_id, task->manager_ids[0]);
        return conv_call_for_manager_nb(task->stage_id, task->manager_ids[0], conv);
      }
      if ((uint32_t)conv->out_channels >= task->tile_count) {
        PRT_PROGRESS_HOT_ERR_LOG("gemm-issue-conv stage=%u dispatch=unspec-oc-split", task->stage_id);
        return run_conv_oc_split(task, conv);
      }
      PRT_PROGRESS_HOT_ERR_LOG("gemm-issue-conv stage=%u dispatch=unspec-spatial-split", task->stage_id);
      return run_conv_spatial_split(task, conv);
    default:
      return PRT_ERR_NOT_IMPL;
  }
#endif
}

static int gemm_issue_grouped_conv_task(prt_runtime_t *rt, const prt_conv_task_t *task,
                                        const prt_gemmini_conv_desc_t *conv) {
  const int groups = prt_conv_groups(conv);
  const int in_stride = prt_conv_input_stride(conv);
  const int out_stride = prt_conv_output_stride(conv);
  if (!rt || !task || !conv) return PRT_ERR_INVAL;
  if (groups <= 1) return gemm_issue_conv_task(rt, task, conv);
  if (!prt_grouped_conv_uses_standard_layout(conv)) return PRT_ERR_NOT_IMPL;
  if (in_stride < groups * conv->in_channels || out_stride < groups * conv->out_channels) {
    return PRT_ERR_INVAL;
  }

  PRT_PROGRESS_HOT_ERR_LOG("gemm-issue-grouped-conv stage=%u split=%u groups=%d in_stride=%d out_stride=%d",
                           task->stage_id, (uint32_t)task->split_kind, groups, in_stride, out_stride);
  for (int group_idx = 0; group_idx < groups; ++group_idx) {
    prt_gemmini_conv_desc_t sub = *conv;
    int rc;

    sub.groups = 1;
    sub.input = prt_grouped_conv_input_ptr(conv, group_idx);
    sub.weights = prt_grouped_conv_weight_ptr(conv, group_idx);
    sub.bias = prt_grouped_conv_bias_ptr(conv, group_idx);
    sub.output = prt_grouped_conv_output_ptr(conv, group_idx);

    PRT_PROGRESS_HOT_ERR_LOG("gemm-issue-grouped-conv stage=%u group=%d/%d mgr0=%u",
                             task->stage_id, group_idx, groups,
                             task->num_managers > 0 ? task->manager_ids[0] : 0U);
    rc = gemm_issue_conv_task(rt, task, &sub);
    if (rc != PRT_OK) return rc;
  }
  return PRT_OK;
}

static int gemm_issue_task(prt_runtime_t *rt, const prt_conv_task_t *task) {
  const prt_gemmini_conv_desc_t *conv = NULL;
  const prt_gemmini_resadd_desc_t *resadd = NULL;
  if (!rt || !task || !task->opaque_task) return PRT_OK;
  if (task->num_managers == 0) return PRT_ERR_INVAL;
  PRT_PROGRESS_HOT_ERR_LOG("gemm-issue-enter stage=%u op=%u split=%u tiles=%u managers=%u mgr0=%u backend=%u",
                           task->stage_id, (uint32_t)task->op_kind, (uint32_t)task->split_kind,
                           task->tile_count, task->num_managers,
                           task->num_managers > 0 ? task->manager_ids[0] : 0U,
                           rt->cfg.backend);

  if (rt->cfg.backend == PRT_BACKEND_CPU) {
    if (task->op_kind == PRT_STAGE_OP_CONV) {
      conv = (const prt_gemmini_conv_desc_t *)task->opaque_task;
      return gemm_issue_grouped_conv_task(rt, task, conv);
    } else if (task->op_kind == PRT_STAGE_OP_RESADD) {
      resadd = (const prt_gemmini_resadd_desc_t *)task->opaque_task;
      resadd_cpu_ref(resadd);
      return PRT_OK;
    }
    return PRT_OK;
  }

#if !defined(__riscv)
  if (task->op_kind == PRT_STAGE_OP_CONV) {
    conv = (const prt_gemmini_conv_desc_t *)task->opaque_task;
    return gemm_issue_grouped_conv_task(rt, task, conv);
  } else if (task->op_kind == PRT_STAGE_OP_RESADD) {
    resadd = (const prt_gemmini_resadd_desc_t *)task->opaque_task;
    switch (task->split_kind) {
      case PRT_LAYER_SPLIT_SINGLE:
        resadd_cpu_ref(resadd);
        return PRT_OK;
      case PRT_LAYER_SPLIT_RESADD_SPATIAL:
      case PRT_LAYER_SPLIT_SPATIAL:
        return run_resadd_split_host(task, resadd);
      case PRT_LAYER_SPLIT_UNSPEC:
        return run_resadd_split_host(task, resadd);
      default:
        return PRT_ERR_NOT_IMPL;
    }
  }
  return PRT_OK;
#else
  if (task->op_kind == PRT_STAGE_OP_CONV) {
    conv = (const prt_gemmini_conv_desc_t *)task->opaque_task;
    return gemm_issue_grouped_conv_task(rt, task, conv);
  } else if (task->op_kind == PRT_STAGE_OP_RESADD) {
    resadd = (const prt_gemmini_resadd_desc_t *)task->opaque_task;
    if (!resadd->A || !resadd->B || !resadd->C) return PRT_ERR_INVAL;
    if (resadd->I == 0 || resadd->J == 0 || resadd->stride == 0) return PRT_ERR_INVAL;
    PRT_PROGRESS_HOT_ERR_LOG("gemm-issue-resadd stage=%u split=%u tiles=%u I=%lu J=%lu stride=%lu",
                             task->stage_id, (uint32_t)task->split_kind, task->tile_count,
                             (unsigned long)resadd->I, (unsigned long)resadd->J,
                             (unsigned long)resadd->stride);
    switch (task->split_kind) {
      case PRT_LAYER_SPLIT_SINGLE:
        if (prt_should_force_resadd_cpu_fallback(task, resadd, prt_pick_resadd_type(resadd))) {
          PRT_PROGRESS_HOT_ERR_LOG("gemm-issue-resadd stage=%u dispatch=cpu-fallback", task->stage_id);
          return prt_run_resadd_cpu_fallback(rt, task->stage_id, task->manager_ids[0], task, resadd,
                                             prt_pick_resadd_type(resadd));
        }
        PRT_PROGRESS_HOT_ERR_LOG("gemm-issue-resadd stage=%u dispatch=single mgr=%u",
                                 task->stage_id, task->manager_ids[0]);
        return resadd_call_for_manager_nb(task->stage_id, task->manager_ids[0], resadd);
      case PRT_LAYER_SPLIT_RESADD_SPATIAL:
      case PRT_LAYER_SPLIT_SPATIAL:
        PRT_PROGRESS_HOT_ERR_LOG("gemm-issue-resadd stage=%u dispatch=split", task->stage_id);
        return run_resadd_split(rt, task, resadd);
      case PRT_LAYER_SPLIT_UNSPEC:
        PRT_PROGRESS_HOT_ERR_LOG("gemm-issue-resadd stage=%u dispatch=unspec-split", task->stage_id);
        return run_resadd_split(rt, task, resadd);
      default:
        return PRT_ERR_NOT_IMPL;
    }
  }
  return PRT_OK;
#endif
}

static int gemm_blocking_fence(prt_runtime_t *rt, const prt_conv_task_t *task, uint64_t timeout_ns) {
  (void)rt;
  (void)timeout_ns;
#if defined(__riscv)
  return fence_task_managers(task);
#else
  (void)task;
  return PRT_OK;
#endif
}

static int gemm_async_conv_run(prt_runtime_t *rt, const prt_conv_task_t *task, uint64_t timeout_ns) {
  (void)timeout_ns;
  return gemm_issue_task(rt, task);
}

static int gemm_async_fence(prt_runtime_t *rt, const prt_conv_task_t *task, uint64_t timeout_ns) {
  return gemm_blocking_fence(rt, task, timeout_ns);
}
