#ifndef PRT_GEMMINI_ADAPTER_H
#define PRT_GEMMINI_ADAPTER_H

#include "prt_types.h"

#ifdef __cplusplus
extern "C" {
#endif

struct prt_runtime_s;
typedef struct prt_runtime_s prt_runtime_t;

typedef struct {
  int batch_size;
  int in_row_dim;
  int in_col_dim;
  int in_channels;
  int out_channels;
  int out_row_dim;
  int out_col_dim;

  int stride;
  int input_dilation;
  int kernel_dilation;
  int padding;
  int kernel_dim;

  int wrot180;
  int trans_output_1203;
  int trans_input_3120;
  int trans_weight_1203;
  int trans_weight_0132;

  const void *input;
  const void *weights;
  const void *bias;
  void *output;

  int act;
  float output_scale;
  int pool_size;
  int pool_stride;
  int pool_padding;

  int tiled_type; // 0=OS, 1=WS, 2=CPU
} prt_gemmini_conv_desc_t;

typedef struct {
  size_t I;
  size_t J;
  float A_scale;
  float B_scale;
  float C_scale;
  size_t stride;
  const void *A;
  const void *B;
  void *C;
  int relu;
  int tiled_type; // 1=WS, 2=CPU
} prt_gemmini_resadd_desc_t;

typedef struct {
  int (*conv_run)(prt_runtime_t *rt, const prt_conv_task_t *task, uint64_t timeout_ns);
  int (*fence)(prt_runtime_t *rt, const prt_conv_task_t *task, uint64_t timeout_ns);
  const char *name;
} prt_gemmini_ops_t;

int prt_gemmini_backend_init(prt_runtime_t *rt);
void prt_gemmini_backend_destroy(prt_runtime_t *rt);

int prt_gemm_conv_run(prt_runtime_t *rt, const prt_conv_task_t *task, uint64_t timeout_ns);
int prt_gemm_fence(prt_runtime_t *rt, const prt_conv_task_t *task, uint64_t timeout_ns);

#ifdef __cplusplus
}
#endif

#endif
