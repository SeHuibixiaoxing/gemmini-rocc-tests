#define REROCC_FOCUSED_POINTWISE_INTERLEAVED 1
#define REROCC_FOCUSED_POINTWISE_INTERLEAVED_NAME "pointwise_stage0_runtime_linuxphys_focus"
#define REROCC_SHARED_SPAD_XLATE_RANGE_BASE 0x3faf751000ULL

#define PW_VADDR_PAGE_OFFSET 0ULL
#define PW_B_VPAGE 1U
#define PW_A_VPAGE 65U
#define PW_C_VPAGE 129U
#define PW_CHUNK_BIAS_VPAGE 0U

// Mirror the Linux/F2 stage0 physical shared-spad placement exactly:
// input  -> local pages 0..31 interleaved across a0/a1
// output -> local pages 32..63 interleaved across a0/a1
// bias   -> a0 local page 64
// weight -> starts at a1 local page 64, then alternates a0/a1
#define PW_FOCUSED_STAGE0_A_LOCAL_PAGE 0U
#define PW_FOCUSED_STAGE0_B_LOCAL_PAGE 64U
#define PW_FOCUSED_STAGE0_BIAS_LOCAL_PAGE 64U
#define PW_FOCUSED_STAGE0_C_LOCAL_PAGE 32U

#define PW_FOCUSED_STAGE0_A_SLOT_OFFSET 0U
#define PW_FOCUSED_STAGE0_B_SLOT_OFFSET 1U
#define PW_FOCUSED_STAGE0_BIAS_SLOT_OFFSET 0U
#define PW_FOCUSED_STAGE0_C_SLOT_OFFSET 0U

#include "rerocc_lc_resadd_explicit_interleaved.c"
