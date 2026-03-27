#ifndef REROCC_GEMMINI_SPM_XLATE_H
#define REROCC_GEMMINI_SPM_XLATE_H

#include <stdint.h>

#include "encoding.h"
#include "rocc-software/src/xcustom.h"

#ifndef XCUSTOM_ACC
#define XCUSTOM_ACC 3
#endif

#define REROCC_GEMMINI_SPM_XLATE_FUNCT_CFG 23
#define REROCC_GEMMINI_SPM_XLATE_FUNCT_RANGE 24
#define REROCC_GEMMINI_SPM_XLATE_FUNCT_FLUSH 25
#define REROCC_GEMMINI_SPM_XLATE_FUNCT_FAULT 26

static inline void rerocc_gemmini_spm_xlate_cfg(uint64_t ptbr_pa,
                                                 uint32_t pte_count,
                                                 uint32_t page_shift,
                                                 uint32_t enable) {
  uint64_t rs1 = ptbr_pa;
  uint64_t rs2 = ((uint64_t)pte_count << 16) |
                 ((uint64_t)(page_shift & 0xffU) << 8) |
                 (uint64_t)(enable & 0x1U);
  ROCC_INSTRUCTION_0_R_R(XCUSTOM_ACC, rs1, rs2, REROCC_GEMMINI_SPM_XLATE_FUNCT_CFG);
}

static inline void rerocc_gemmini_spm_xlate_range(uint64_t range_base,
                                                   uint64_t range_size) {
  ROCC_INSTRUCTION_0_R_R(XCUSTOM_ACC, range_base, range_size,
                         REROCC_GEMMINI_SPM_XLATE_FUNCT_RANGE);
}

static inline void rerocc_gemmini_spm_xlate_flush(void) {
  ROCC_INSTRUCTION_0_R_R(XCUSTOM_ACC, 0, 0, REROCC_GEMMINI_SPM_XLATE_FUNCT_FLUSH);
}

static inline uint64_t rerocc_gemmini_spm_xlate_fault(void) {
  uint64_t value = 0;
  ROCC_INSTRUCTION_R_R_R(XCUSTOM_ACC, value, 0, 0, REROCC_GEMMINI_SPM_XLATE_FUNCT_FAULT);
  return value;
}

#endif  // REROCC_GEMMINI_SPM_XLATE_H
