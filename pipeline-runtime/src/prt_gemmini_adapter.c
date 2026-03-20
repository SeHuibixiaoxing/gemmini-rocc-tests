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
#endif

static int gemm_blocking_conv_run(prt_runtime_t *rt, const prt_conv_task_t *task, uint64_t timeout_ns);
static int gemm_blocking_fence(prt_runtime_t *rt, const prt_conv_task_t *task, uint64_t timeout_ns);

static int gemm_async_conv_run(prt_runtime_t *rt, const prt_conv_task_t *task, uint64_t timeout_ns);
static int gemm_async_fence(prt_runtime_t *rt, const prt_conv_task_t *task, uint64_t timeout_ns);
static int gemm_issue_task(prt_runtime_t *rt, const prt_conv_task_t *task);

static int task_issue_already_fenced(const prt_conv_task_t *task);

static void *prt_gemmini_alloc_aligned(size_t bytes, int zero_fill) {
  const size_t align_bytes = 64U;
  void *ptr = NULL;
  if (bytes == 0) return NULL;
  if (posix_memalign(&ptr, align_bytes, bytes) != 0) return NULL;
  if (zero_fill) memset(ptr, 0, bytes);
  return ptr;
}

#if defined(__riscv)
static int flush_scope_after_drain(prt_rr_scope_t *scope) {
  int rc;
  if (!scope) return PRT_ERR_INVAL;
  gemmini_flush(0);
  rc = prt_rr_fence_scope(scope);
  gemmini_fence();
  return rc;
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
  int in_stride = conv->in_channels;
  int weight_stride = conv->out_channels;
  int out_stride = conv->out_channels;
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
  uint32_t k_rows;
  const int8_t *weights;
  const int32_t *bias;
  int8_t *output;
  if (!task || !conv) return PRT_ERR_INVAL;

  tiles = task->tile_count > 0 ? task->tile_count : 1U;
  if (tiles <= 1U) {
    conv_cpu_ref(conv);
    return PRT_OK;
  }
  if ((uint32_t)conv->out_channels < tiles) return PRT_ERR_NOT_IMPL;

  weights = (const int8_t *)conv->weights;
  bias = (const int32_t *)conv->bias;
  output = (int8_t *)conv->output;
  k_rows = (uint32_t)(conv->kernel_dim * conv->kernel_dim * conv->in_channels);

  for (uint32_t t = 0; t < tiles; ++t) {
    uint32_t oc_beg;
    uint32_t oc_end;
    uint32_t oc_tile;
    int8_t *w_pack;
    int32_t *b_pack;
    int8_t *o_pack;
    prt_gemmini_conv_desc_t sub = *conv;
    host_split_1d_range((uint32_t)conv->out_channels, tiles, t, &oc_beg, &oc_end);
    if (oc_end <= oc_beg) continue;
    oc_tile = oc_end - oc_beg;

    w_pack = (int8_t *)prt_gemmini_alloc_aligned(sizeof(int8_t) * (size_t)k_rows * (size_t)oc_tile, 0);
    b_pack = (int32_t *)prt_gemmini_alloc_aligned(sizeof(int32_t) * (size_t)oc_tile, 0);
    o_pack = (int8_t *)prt_gemmini_alloc_aligned(sizeof(int8_t) *
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
             sizeof(int8_t) * oc_tile);
    }
    if (bias) memcpy(b_pack, bias + oc_beg, sizeof(int32_t) * oc_tile);
    else memset(b_pack, 0, sizeof(int32_t) * oc_tile);

    sub.out_channels = (int)oc_tile;
    sub.weights = w_pack;
    sub.bias = b_pack;
    sub.output = o_pack;
    conv_cpu_ref(&sub);

    for (int b = 0; b < conv->batch_size; ++b) {
      for (int oh = 0; oh < conv->out_row_dim; ++oh) {
        for (int ow = 0; ow < conv->out_col_dim; ++ow) {
          size_t dst_off = ((size_t)b * conv->out_row_dim * conv->out_col_dim +
                            (size_t)oh * conv->out_col_dim + (size_t)ow) * (size_t)conv->out_channels + oc_beg;
          size_t src_off = ((size_t)b * conv->out_row_dim * conv->out_col_dim +
                            (size_t)oh * conv->out_col_dim + (size_t)ow) * (size_t)oc_tile;
          memcpy(output + dst_off, o_pack + src_off, sizeof(int8_t) * oc_tile);
        }
      }
    }

    free(w_pack);
    free(b_pack);
    free(o_pack);
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
  gemmini_flush(0);

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

static int conv_call_for_manager_sync(uint32_t stage_id, uint32_t manager_id,
                                      const prt_gemmini_conv_desc_t *conv) {
  prt_rr_scope_t scope;
  enum tiled_matmul_type_t tiled_type = WS;
  int input_dilation;
  int kernel_dilation;
  float output_scale;
  int rc;
  if (!conv) return PRT_ERR_INVAL;

  PRT_PROGRESS_LOG("conv-sync stage=%u mgr=%u acquire-begin out_ch=%d out_dim=%dx%d in_dim=%dx%d",
                   stage_id, manager_id, conv->out_channels,
                   conv->out_row_dim, conv->out_col_dim,
                   conv->in_row_dim, conv->in_col_dim);

  rc = prt_rr_acquire_scope(NULL, stage_id, manager_id, 3U, &scope);
  if (rc != PRT_OK) return rc;
  PRT_PROGRESS_LOG("conv-sync stage=%u mgr=%u acquire-end", stage_id, manager_id);

  if (conv->tiled_type == 0) tiled_type = OS;
  else if (conv->tiled_type == 2) tiled_type = CPU;
  else tiled_type = WS;

  input_dilation = conv->input_dilation > 0 ? conv->input_dilation : 1;
  kernel_dilation = conv->kernel_dilation > 0 ? conv->kernel_dilation : 1;
  output_scale = conv->output_scale != 0.0f ? conv->output_scale : 1.0f;
  gemmini_flush(0);
  PRT_PROGRESS_LOG("conv-sync stage=%u mgr=%u tiled-conv-begin", stage_id, manager_id);

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
  PRT_PROGRESS_LOG("conv-sync stage=%u mgr=%u tiled-conv-end", stage_id, manager_id);

  PRT_PROGRESS_LOG("conv-sync stage=%u mgr=%u fence-begin", stage_id, manager_id);
  rc = prt_rr_fence_scope(&scope);
  gemmini_fence();
  if (rc == PRT_OK) rc = flush_scope_after_drain(&scope);
  PRT_PROGRESS_LOG("conv-sync stage=%u mgr=%u fence-end rc=%d", stage_id, manager_id, rc);
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
  float output_scale;
  int rc;
  if (!conv) return PRT_ERR_INVAL;

  PRT_PROGRESS_LOG("conv-sync stage=%u mgr=%u acquire-begin out_ch=%d out_dim=%dx%d in_dim=%dx%d",
                   stage_id, manager_id, conv->out_channels,
                   conv->out_row_dim, conv->out_col_dim,
                   conv->in_row_dim, conv->in_col_dim);

  rc = prt_rr_acquire_scope(NULL, stage_id, manager_id, 3U, &scope);
  if (rc != PRT_OK) return rc;
  PRT_PROGRESS_LOG("conv-sync stage=%u mgr=%u acquire-end", stage_id, manager_id);

  if (conv->tiled_type == 0) tiled_type = OS;
  else if (conv->tiled_type == 2) tiled_type = CPU;
  else tiled_type = WS;

  input_dilation = conv->input_dilation > 0 ? conv->input_dilation : 1;
  kernel_dilation = conv->kernel_dilation > 0 ? conv->kernel_dilation : 1;
  output_scale = conv->output_scale != 0.0f ? conv->output_scale : 1.0f;
  gemmini_flush(0);
  PRT_PROGRESS_LOG("conv-sync stage=%u mgr=%u tiled-conv-begin in_stride=%d weight_stride=%d out_stride=%d",
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
  PRT_PROGRESS_LOG("conv-sync stage=%u mgr=%u tiled-conv-end", stage_id, manager_id);

  PRT_PROGRESS_LOG("conv-sync stage=%u mgr=%u fence-begin", stage_id, manager_id);
  rc = prt_rr_fence_scope(&scope);
  gemmini_fence();
  if (rc == PRT_OK) rc = flush_scope_after_drain(&scope);
  PRT_PROGRESS_LOG("conv-sync stage=%u mgr=%u fence-end rc=%d", stage_id, manager_id, rc);
  (void)prt_rr_release_scope(&scope);
  return rc;
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

  gemmini_flush(0);
  rc = resadd_issue_no_fence(resadd, matadd_type);
  (void)prt_rr_release_scope(&scope);
  return rc;
}

static int resadd_call_for_manager_sync(uint32_t stage_id, uint32_t manager_id,
                                        const prt_gemmini_resadd_desc_t *resadd) {
  prt_rr_scope_t scope;
  enum tiled_matmul_type_t matadd_type = WS;
  int rc;
  if (!resadd) return PRT_ERR_INVAL;
  rc = prt_rr_acquire_scope(NULL, stage_id, manager_id, 3U, &scope);
  if (rc != PRT_OK) return rc;

  if (resadd->tiled_type == 2) matadd_type = CPU;
  else matadd_type = WS;

  gemmini_flush(0);
  rc = resadd_issue_no_fence(resadd, matadd_type);
  if (rc == PRT_OK) {
    rc = prt_rr_fence_scope(&scope);
    gemmini_fence();
    if (rc == PRT_OK) rc = flush_scope_after_drain(&scope);
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

  PRT_PROGRESS_LOG("oc-split stage=%u tiles=%u out_ch=%d out_dim=%dx%d begin",
                   task->stage_id, tiles, conv->out_channels,
                   conv->out_row_dim, conv->out_col_dim);

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
    const int full_out_channels = conv->out_channels;

    PRT_PROGRESS_LOG("oc-split stage=%u mode=direct-strided full_out_ch=%d",
                     task->stage_id, full_out_channels);

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

      PRT_PROGRESS_LOG("oc-split stage=%u tile=%u/%u mgr=%u oc_beg=%u oc_tile=%u launch",
                       task->stage_id, t, tiles, mgr, oc_beg, oc_tile);
      rc = conv_call_for_manager_sync_strided(task->stage_id, mgr, &sub,
                                              conv->in_channels,
                                              full_out_channels,
                                              full_out_channels);
      if (rc != PRT_OK) return rc;
      PRT_PROGRESS_LOG("oc-split stage=%u tile=%u/%u mgr=%u done",
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
      PRT_PROGRESS_LOG("oc-split stage=%u tile=%u/%u mgr=%u oc_beg=%u oc_tile=%u launch",
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
      PRT_PROGRESS_LOG("oc-split stage=%u tile=%u/%u mgr=%u done",
                       task->stage_id, t, tiles, mgr);
      tile_ctx[t].oc_beg = oc_beg;
      tile_ctx[t].oc_tile = oc_tile;
      tile_ctx[t].w_pack = w_pack;
      tile_ctx[t].b_pack = b_pack;
      tile_ctx[t].o_pack = o_pack;
    }

    PRT_PROGRESS_LOG("oc-split stage=%u repack-begin tiles=%u", task->stage_id, tiles);
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

    PRT_PROGRESS_LOG("oc-split stage=%u repack-end tiles=%u", task->stage_id, tiles);
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
    rc = resadd_call_for_manager_sync(task->stage_id, mgr, &sub);
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

static int gemm_issue_task(prt_runtime_t *rt, const prt_conv_task_t *task) {
  const prt_gemmini_conv_desc_t *conv = NULL;
  const prt_gemmini_resadd_desc_t *resadd = NULL;
  if (!rt || !task || !task->opaque_task) return PRT_OK;
  if (task->num_managers == 0) return PRT_ERR_INVAL;

  if (rt->cfg.backend == PRT_BACKEND_CPU) {
    if (task->op_kind == PRT_STAGE_OP_CONV) {
      conv = (const prt_gemmini_conv_desc_t *)task->opaque_task;
      conv_cpu_ref(conv);
      return PRT_OK;
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
    if (!conv->input || !conv->weights || !conv->output) return PRT_ERR_INVAL;
    if (conv->batch_size <= 0 || conv->in_row_dim <= 0 || conv->in_col_dim <= 0 ||
        conv->in_channels <= 0 || conv->out_channels <= 0 || conv->out_row_dim <= 0 ||
        conv->out_col_dim <= 0 || conv->kernel_dim <= 0) {
      return PRT_ERR_INVAL;
    }
    switch (task->split_kind) {
      case PRT_LAYER_SPLIT_SINGLE:
        return conv_call_for_manager_nb(task->stage_id, task->manager_ids[0], conv);
      case PRT_LAYER_SPLIT_OC:
        return run_conv_oc_split(task, conv);
      case PRT_LAYER_SPLIT_SPATIAL:
        return run_conv_spatial_split(task, conv);
      case PRT_LAYER_SPLIT_UNSPEC:
        if (task->tile_count <= 1U) {
          return conv_call_for_manager_nb(task->stage_id, task->manager_ids[0], conv);
        }
        if ((uint32_t)conv->out_channels >= task->tile_count) return run_conv_oc_split(task, conv);
        return run_conv_spatial_split(task, conv);
      default:
        return PRT_ERR_NOT_IMPL;
    }
  } else if (task->op_kind == PRT_STAGE_OP_RESADD) {
    resadd = (const prt_gemmini_resadd_desc_t *)task->opaque_task;
    if (!resadd->A || !resadd->B || !resadd->C) return PRT_ERR_INVAL;
    if (resadd->I == 0 || resadd->J == 0 || resadd->stride == 0) return PRT_ERR_INVAL;
    switch (task->split_kind) {
      case PRT_LAYER_SPLIT_SINGLE:
        return resadd_call_for_manager_nb(task->stage_id, task->manager_ids[0], resadd);
      case PRT_LAYER_SPLIT_RESADD_SPATIAL:
      case PRT_LAYER_SPLIT_SPATIAL:
        return run_resadd_split(task, resadd);
      case PRT_LAYER_SPLIT_UNSPEC:
        return run_resadd_split(task, resadd);
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
