#define REROCC_FOCUSED_POINTWISE_INTERLEAVED 1
#define REROCC_FOCUSED_POINTWISE_INTERLEAVED_NAME "pointwise_stage0_runtime_highva_vpage0_focus"
#define REROCC_SHARED_SPAD_XLATE_RANGE_BASE 0x3f9ce22000ULL
#define PW_VADDR_PAGE_OFFSET 0ULL
#define PW_B_VPAGE 1U
#define PW_A_VPAGE 65U
#define PW_C_VPAGE 129U
#define PW_CHUNK_BIAS_VPAGE 0U
#include "rerocc_lc_resadd_explicit_interleaved.c"
