#ifndef SRC_MAIN_C_REROCC_CONTROL_H
#define SRC_MAIN_C_REROCC_CONTROL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#define RR_CFG_ACQ_MASK 0x100
#define RR_CFG_MGR_MASK 0x0ff
#define RR_MAX_CFGS 32

#define CSR_RROPC0 0x800
#define CSR_RROPC1 0x801
#define CSR_RROPC2 0x802
#define CSR_RROPC3 0x803
#define CSR_RRBAR 0x804

#define CSR_RRCFG0 0x810
#define CSR_RRCFG1 0x811
#define CSR_RRCFG2 0x812
#define CSR_RRCFG3 0x813
#define CSR_RRCFG4 0x814
#define CSR_RRCFG5 0x815
#define CSR_RRCFG6 0x816
#define CSR_RRCFG7 0x817
#define CSR_RRCFG8 0x818
#define CSR_RRCFG9 0x819
#define CSR_RRCFG10 0x81a
#define CSR_RRCFG11 0x81b
#define CSR_RRCFG12 0x81c
#define CSR_RRCFG13 0x81d
#define CSR_RRCFG14 0x81e
#define CSR_RRCFG15 0x81f
#define CSR_RRCFG16 0x820
#define CSR_RRCFG17 0x821
#define CSR_RRCFG18 0x822
#define CSR_RRCFG19 0x823
#define CSR_RRCFG20 0x824
#define CSR_RRCFG21 0x825
#define CSR_RRCFG22 0x826
#define CSR_RRCFG23 0x827
#define CSR_RRCFG24 0x828
#define CSR_RRCFG25 0x829
#define CSR_RRCFG26 0x82a
#define CSR_RRCFG27 0x82b
#define CSR_RRCFG28 0x82c
#define CSR_RRCFG29 0x82d
#define CSR_RRCFG30 0x82e
#define CSR_RRCFG31 0x82f

#define RR_CSR_LIST \
  F(CSR_RRBAR) \
  F(CSR_RROPC0) \
  F(CSR_RROPC1) \
  F(CSR_RROPC2) \
  F(CSR_RROPC3) \
  F(CSR_RRCFG0) \
  F(CSR_RRCFG1) \
  F(CSR_RRCFG2) \
  F(CSR_RRCFG3) \
  F(CSR_RRCFG4) \
  F(CSR_RRCFG5) \
  F(CSR_RRCFG6) \
  F(CSR_RRCFG7) \
  F(CSR_RRCFG8) \
  F(CSR_RRCFG9) \
  F(CSR_RRCFG10) \
  F(CSR_RRCFG11) \
  F(CSR_RRCFG12) \
  F(CSR_RRCFG13) \
  F(CSR_RRCFG14) \
  F(CSR_RRCFG15) \
  F(CSR_RRCFG16) \
  F(CSR_RRCFG17) \
  F(CSR_RRCFG18) \
  F(CSR_RRCFG19) \
  F(CSR_RRCFG20) \
  F(CSR_RRCFG21) \
  F(CSR_RRCFG22) \
  F(CSR_RRCFG23) \
  F(CSR_RRCFG24) \
  F(CSR_RRCFG25) \
  F(CSR_RRCFG26) \
  F(CSR_RRCFG27) \
  F(CSR_RRCFG28) \
  F(CSR_RRCFG29) \
  F(CSR_RRCFG30) \
  F(CSR_RRCFG31)

#define read_csr(reg) ({ unsigned long __tmp; \
  asm volatile ("csrr %0, " #reg : "=r"(__tmp)); \
  __tmp; })

#define swap_csr(reg, val) ({ unsigned long __tmp; \
  asm volatile ("csrrw %0, " #reg ", %1" : "=r"(__tmp) : "rK"(val)); \
  __tmp; })

static inline uint64_t rr_swap_csr(uint64_t csr_id, uint64_t wdata) {
  uint64_t ret = 0;
  switch (csr_id) {
#define F(c) case c: { ret = swap_csr(c, wdata); break; }
    RR_CSR_LIST
#undef F
  default:
    printf("rr_swap_csr illegal csr id 0x%lx\n", csr_id);
    abort();
  }
  return ret;
}

static inline uint64_t rr_read_csr(uint64_t csr_id) {
  uint64_t ret = 0;
  switch (csr_id) {
#define F(c) case c: { ret = read_csr(c); break; }
    RR_CSR_LIST
#undef F
  default:
    printf("rr_read_csr illegal csr id 0x%lx\n", csr_id);
    abort();
  }
  return ret;
}

static inline void rr_write_csr(uint64_t csr_id, uint64_t wdata) {
  (void)rr_swap_csr(csr_id, wdata);
}

static inline bool rr_acquire_cfg(uint32_t cfg_id, uint64_t manager_id) {
  const uint32_t csr_id = CSR_RRCFG0 + cfg_id;
  const uint64_t wdata = RR_CFG_ACQ_MASK | (manager_id & RR_CFG_MGR_MASK);
  rr_write_csr(csr_id, wdata);
  return (rr_read_csr(csr_id) & RR_CFG_ACQ_MASK) != 0;
}

static inline void rr_release(uint32_t cfg_id) {
  const uint32_t csr_id = CSR_RRCFG0 + cfg_id;
  rr_write_csr(csr_id, 0);
}

static inline void rr_release_all(size_t n_cfgs) {
  const size_t max_cfg = n_cfgs > RR_MAX_CFGS ? RR_MAX_CFGS : n_cfgs;
  for (size_t i = 0; i < max_cfg; i++) {
    rr_release((uint32_t)i);
  }
}

static inline bool rr_acquire_multi(uint32_t cfg_id, const uint64_t *manager_ids, size_t n) {
  for (size_t i = 0; i < n; i++) {
    if (rr_acquire_cfg(cfg_id, manager_ids[i])) {
      return true;
    }
  }
  return false;
}

static inline void rr_set_opc(uint8_t opcode_id, uint32_t cfg_id) {
  rr_write_csr(CSR_RROPC0 + opcode_id, cfg_id);
}

static inline void rr_fence(uint32_t cfg_id) {
  rr_write_csr(CSR_RRBAR, cfg_id);
  asm volatile("fence");
}

#endif  // SRC_MAIN_C_REROCC_CONTROL_H
