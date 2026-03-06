#ifndef REROCC_COUPLED_DMA_H
#define REROCC_COUPLED_DMA_H

#include <stdint.h>

#include "encoding.h"

#define REROCC_COUPLED_DMA_XCUSTOM 2

static inline void rerocc_coupleddma_set_dst(uint64_t dst_addr, uint64_t completion_addr) {
  ROCC_INSTRUCTION_0_R_R(REROCC_COUPLED_DMA_XCUSTOM, dst_addr, completion_addr, 2);
}

static inline void rerocc_coupleddma_set_src(uint64_t src_addr, uint64_t num_bytes) {
  ROCC_INSTRUCTION_0_R_R(REROCC_COUPLED_DMA_XCUSTOM, src_addr, num_bytes, 1);
}

static inline uint64_t rerocc_coupleddma_wait(void) {
  uint64_t status = 0;
  ROCC_INSTRUCTION_R_R_R(REROCC_COUPLED_DMA_XCUSTOM, status, 0, 0, 3);
  return status;
}

static inline uint64_t rerocc_coupleddma_read_monitor(uint64_t stat_id) {
  uint64_t value = 0;
  ROCC_INSTRUCTION_R_R_R(REROCC_COUPLED_DMA_XCUSTOM, value, stat_id, 0, 4);
  return value;
}

#endif // REROCC_COUPLED_DMA_H
