#include "prt_gemmini_adapter.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "prt_rerocc.h"
#include "prt_runtime.h"

#if defined(__riscv)
#include "include/gemmini.h"
#endif

static int gemm_blocking_conv_run(prt_runtime_t *rt, const prt_conv_task_t *task, uint64_t timeout_ns);
static int gemm_blocking_fence(prt_runtime_t *rt, const prt_conv_task_t *task, uint64_t timeout_ns);

static int gemm_async_conv_run(prt_runtime_t *rt, const prt_conv_task_t *task, uint64_t timeout_ns);
static int gemm_async_fence(prt_runtime_t *rt, const prt_conv_task_t *task, uint64_t timeout_ns);
static int gemm_issue_task(const prt_conv_task_t *task);

#if defined(__riscv)
typedef struct {
  uint32_t r0;
  uint32_t r1;
  uint32_t c0;
  uint32_t c1;
} prt_rect2d_t;

static uint32_t rect2d_area(const prt_rect2d_t *r) {
  if (!r || r->r1 <= r->r0 || r->c1 <= r->c0) return 0;
  return (r->r1 - r->r0) * (r->c1 - r->c0);
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
    (void)prt_rr_release_scope(&scope);
    if (rc != PRT_OK) return rc;
  }

  gemmini_fence();
  return PRT_OK;
}

static int conv_call_for_manager_nb(uint32_t stage_id, uint32_t manager_id,
                                    const prt_gemmini_conv_desc_t *conv) {
  prt_rr_scope_t scope;
  enum tiled_matmul_type_t tiled_type = WS;
  int input_dilation;
  int kernel_dilation;
  float output_scale;
  int rc;
  if (!conv) return PRT_ERR_INVAL;

  rc = prt_rr_acquire_scope(NULL, stage_id, manager_id, 3U, &scope);
  if (rc != PRT_OK) return rc;

  if (conv->tiled_type == 0) tiled_type = OS;
  else if (conv->tiled_type == 2) tiled_type = CPU;
  else tiled_type = WS;

  input_dilation = conv->input_dilation > 0 ? conv->input_dilation : 1;
  kernel_dilation = conv->kernel_dilation > 0 ? conv->kernel_dilation : 1;
  output_scale = conv->output_scale != 0.0f ? conv->output_scale : 1.0f;

  tiled_conv_auto(
    conv->batch_size, conv->in_row_dim, conv->in_col_dim, conv->in_channels,
    conv->out_channels, conv->out_row_dim, conv->out_col_dim,
    conv->stride, input_dilation, kernel_dilation, conv->padding, conv->kernel_dim,
    conv->wrot180 != 0, conv->trans_output_1203 != 0, conv->trans_input_3120 != 0,
    conv->trans_weight_1203 != 0, conv->trans_weight_0132 != 0,
    (const elem_t *)conv->input, (const elem_t *)conv->weights,
    (const acc_t *)conv->bias, (elem_t *)conv->output,
    conv->act, (acc_scale_t)output_scale,
    conv->pool_size, conv->pool_stride, conv->pool_padding,
    tiled_type);

  (void)prt_rr_release_scope(&scope);
  return PRT_OK;
}

static int resadd_issue_no_fence(const prt_gemmini_resadd_desc_t *resadd,
                                 enum tiled_matmul_type_t matadd_type) {
  size_t tile_I;
  size_t tile_J;
  size_t total_acc_rows;
  if (!resadd) return PRT_ERR_INVAL;

  if (matadd_type == CPU) {
    resadd_cpu(resadd->I, resadd->J, resadd->stride,
               (scale_t)resadd->A_scale, (scale_t)resadd->B_scale,
               (acc_scale_t)resadd->C_scale,
               (const elem_t *)resadd->A, (const elem_t *)resadd->B, (elem_t *)resadd->C,
               resadd->relu != 0);
    return PRT_OK;
  }
  if (matadd_type != WS) return PRT_ERR_NOT_IMPL;

  tile_I = resadd->I;
  tile_J = resadd->J;
  total_acc_rows = (tile_I / DIM + (tile_I % DIM != 0)) * DIM *
                   (tile_J / DIM + (tile_J % DIM != 0));

  while (total_acc_rows > ACC_ROWS / 2) {
    if (tile_I >= tile_J || tile_J <= DIM) {
      tile_I /= 2;
    } else {
      tile_J -= DIM;
    }
    if (tile_I == 0 || tile_J == 0) return PRT_ERR_NOT_IMPL;
    total_acc_rows = (tile_I / DIM + (tile_I % DIM != 0)) * DIM *
                     (tile_J / DIM + (tile_J % DIM != 0));
  }

  gemmini_extended_config_st(resadd->stride * sizeof(elem_t),
                             resadd->relu ? RELU : NO_ACTIVATION,
                             (acc_scale_t)resadd->C_scale);
  gemmini_config_ex(WS, 0, 0);
  gemmini_extended4_config_ld(resadd->stride * sizeof(elem_t), (scale_t)resadd->A_scale, true, DIM, 0);
  gemmini_extended4_config_ld(resadd->stride * sizeof(elem_t), (scale_t)resadd->B_scale, true, DIM, 1);

  for (size_t i = 0; i < resadd->I; i += tile_I) {
    for (size_t j = 0; j < resadd->J; j += tile_J) {
      const size_t I_tile = i + tile_I <= resadd->I ? tile_I : resadd->I - i;
      const size_t J_tile = j + tile_J <= resadd->J ? tile_J : resadd->J - j;
      const elem_t *a = (const elem_t *)resadd->A + i * resadd->stride + j;
      const elem_t *b = (const elem_t *)resadd->B + i * resadd->stride + j;
      elem_t *c = (elem_t *)resadd->C + i * resadd->stride + j;

      sp_tiled_resadd(I_tile, J_tile,
                      (scale_t)resadd->A_scale, (scale_t)resadd->B_scale, a, b, c,
                      resadd->stride, resadd->stride, resadd->stride,
                      resadd->relu != 0);
    }
  }

  return PRT_OK;
}

static int resadd_call_for_manager_nb(uint32_t stage_id, uint32_t manager_id,
                                      const prt_gemmini_resadd_desc_t *resadd) {
  prt_rr_scope_t scope;
  enum tiled_matmul_type_t matadd_type = WS;
  int rc;
  if (!resadd) return PRT_ERR_INVAL;
  rc = prt_rr_acquire_scope(NULL, stage_id, manager_id, 3U, &scope);
  if (rc != PRT_OK) return rc;

  if (resadd->tiled_type == 2) matadd_type = CPU;
  else matadd_type = WS;

  rc = resadd_issue_no_fence(resadd, matadd_type);
  (void)prt_rr_release_scope(&scope);
  return rc;
}

static int run_conv_oc_split(const prt_conv_task_t *task, const prt_gemmini_conv_desc_t *conv) {
  uint32_t tiles;
  uint32_t k_rows;
  const elem_t *weights;
  const acc_t *bias;
  elem_t *output;
  if (!task || !conv) return PRT_ERR_INVAL;

  tiles = task->tile_count > 0 ? task->tile_count : 1U;
  if (tiles <= 1U) {
    return conv_call_for_manager_nb(task->stage_id, task->acc_id, conv);
  }
  if ((uint32_t)conv->out_channels < tiles) return PRT_ERR_NOT_IMPL;

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

    w_pack = (elem_t *)malloc(sizeof(elem_t) * (size_t)k_rows * (size_t)oc_tile);
    b_pack = (acc_t *)malloc(sizeof(acc_t) * (size_t)oc_tile);
    o_pack = (elem_t *)malloc(sizeof(elem_t) * (size_t)conv->batch_size *
                              (size_t)conv->out_row_dim * (size_t)conv->out_col_dim *
                              (size_t)oc_tile);
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
    memset(o_pack, 0, sizeof(elem_t) * (size_t)conv->batch_size *
                    (size_t)conv->out_row_dim * (size_t)conv->out_col_dim * (size_t)oc_tile);

    sub.out_channels = (int)oc_tile;
    sub.weights = w_pack;
    sub.bias = b_pack;
    sub.output = o_pack;

    {
      int rc = conv_call_for_manager_nb(task->stage_id, mgr, &sub);
      if (rc != PRT_OK) {
        free(w_pack);
        free(b_pack);
        free(o_pack);
        return rc;
      }
    }

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

    free(w_pack);
    free(b_pack);
    free(o_pack);
  }
  return PRT_OK;
}

static int run_conv_spatial_split(const prt_conv_task_t *task, const prt_gemmini_conv_desc_t *conv) {
  uint32_t tiles;
  const elem_t *input;
  const elem_t *weights;
  const acc_t *bias;
  elem_t *output;
  prt_rect2d_t rects[PRT_MAX_TILE_SPLITS];
  int rc;
  if (!task || !conv) return PRT_ERR_INVAL;
  tiles = task->tile_count > 0 ? task->tile_count : 1U;
  if (tiles <= 1U) return conv_call_for_manager_nb(task->stage_id, task->acc_id, conv);
  if (tiles > PRT_MAX_TILE_SPLITS) return PRT_ERR_INVAL;

  rc = partition_2d_rects((uint32_t)conv->out_row_dim, (uint32_t)conv->out_col_dim, tiles, rects);
  if (rc != PRT_OK) return rc;

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

    in_pack = (elem_t *)calloc((size_t)conv->batch_size * (size_t)in_h * (size_t)in_w *
                               (size_t)conv->in_channels, sizeof(elem_t));
    out_pack = (elem_t *)calloc((size_t)conv->batch_size * (size_t)tile_oh *
                                (size_t)tile_ow * (size_t)conv->out_channels,
                                sizeof(elem_t));
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

    rc = conv_call_for_manager_nb(task->stage_id, mgr, &sub);
    if (rc != PRT_OK) {
      free(in_pack);
      free(out_pack);
      return rc;
    }

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

    free(in_pack);
    free(out_pack);
  }

  return PRT_OK;
}

static int run_resadd_split(const prt_conv_task_t *task, const prt_gemmini_resadd_desc_t *resadd) {
  uint32_t tiles;
  prt_rect2d_t rects[PRT_MAX_TILE_SPLITS];
  int rc;
  if (!task || !resadd) return PRT_ERR_INVAL;
  tiles = task->tile_count > 0 ? task->tile_count : 1U;
  if (tiles <= 1U) {
    return resadd_call_for_manager_nb(task->stage_id, task->acc_id, resadd);
  }
  if (tiles > PRT_MAX_TILE_SPLITS) return PRT_ERR_INVAL;

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
    rc = resadd_call_for_manager_nb(task->stage_id, mgr, &sub);
    if (rc != PRT_OK) return rc;
  }

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
  (void)rt;
  rc = gemm_issue_task(task);
  if (rc != PRT_OK) return rc;
  return gemm_blocking_fence(rt, task, timeout_ns);
}

static int gemm_issue_task(const prt_conv_task_t *task) {
#if !defined(__riscv)
  (void)task;
  return PRT_OK;
#else
  const prt_gemmini_conv_desc_t *conv = NULL;
  const prt_gemmini_resadd_desc_t *resadd = NULL;
  if (!task || !task->opaque_task) return PRT_OK;
  if (task->num_managers == 0) return PRT_ERR_INVAL;

  if (task->op_kind == PRT_STAGE_OP_CONV) {
    conv = (const prt_gemmini_conv_desc_t *)task->opaque_task;
    if (!conv->input || !conv->weights || !conv->output) return PRT_ERR_INVAL;
    if (conv->batch_size <= 0 || conv->in_row_dim <= 0 || conv->in_col_dim <= 0 ||
        conv->in_channels <= 0 || conv->out_channels <= 0 || conv->out_row_dim <= 0 ||
        conv->out_col_dim <= 0 || conv->kernel_dim <= 0) {
      return PRT_ERR_INVAL;
    }
    if (task->tile_count <= 1U) {
      return conv_call_for_manager_nb(task->stage_id, task->manager_ids[0], conv);
    }
    if ((uint32_t)conv->out_channels >= task->tile_count) {
      return run_conv_oc_split(task, conv);
    }
    return run_conv_spatial_split(task, conv);
  } else if (task->op_kind == PRT_STAGE_OP_RESADD) {
    resadd = (const prt_gemmini_resadd_desc_t *)task->opaque_task;
    if (!resadd->A || !resadd->B || !resadd->C) return PRT_ERR_INVAL;
    if (resadd->I == 0 || resadd->J == 0 || resadd->stride == 0) return PRT_ERR_INVAL;
    return run_resadd_split(task, resadd);
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
  (void)rt;
  (void)timeout_ns;
  return gemm_issue_task(task);
}

static int gemm_async_fence(prt_runtime_t *rt, const prt_conv_task_t *task, uint64_t timeout_ns) {
  return gemm_blocking_fence(rt, task, timeout_ns);
}
