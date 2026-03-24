#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "encoding.h"
#include "util.h"
#include "include/gemmini.h"
#include "include/gemmini_nn.h"
#include "include/rerocc_gemmini_spm_xlate.h"
#include "rerocc-linux-tests/rerocc_control.h"

#ifndef NUM_CORES
#define NUM_CORES 1
#endif

#ifndef REROCC_NUM_GEMMINI
#define REROCC_NUM_GEMMINI 2
#endif

#ifndef REROCC_GEMMINI_BASE_ID
#define REROCC_GEMMINI_BASE_ID 0
#endif

#ifndef REROCC_TEST_LOCAL_GEMMINI_ID
#define REROCC_TEST_LOCAL_GEMMINI_ID 0
#endif

#ifndef REROCC_ACQUIRE_MAX_RETRIES
#define REROCC_ACQUIRE_MAX_RETRIES 1000000UL
#endif

#define GEMMINI_CFG_ID 0

#define SHARED_SPAD_GLOBAL_ADDR_BASE 0x40000000ULL
#define SHARED_SPAD_LOCAL_SIZE (1024 * 1024ULL)
#define SHARED_SPAD_LOCAL_ADDR_BASE(i) \
  (SHARED_SPAD_GLOBAL_ADDR_BASE + SHARED_SPAD_LOCAL_SIZE * (uint64_t)(i))

#define REROCC_SPM_PAGE_SHIFT 10U
#define REROCC_SPM_PAGE_BYTES (1ULL << REROCC_SPM_PAGE_SHIFT)
#define SHARED_SPAD_XLATE_RANGE_BASE 0xC0000000ULL
#define SHARED_SPAD_XLATE_RANGE_SIZE \
  ((uint64_t)REROCC_NUM_GEMMINI * SHARED_SPAD_LOCAL_SIZE)
#define SHARED_SPAD_XLATE_PTE_CAP \
  ((REROCC_NUM_GEMMINI * (SHARED_SPAD_LOCAL_SIZE / REROCC_SPM_PAGE_BYTES)) + 128U)

#define RESADD_I 24
#define RESADD_J 64
#define RESADD_STRIDE RESADD_J
#define RESADD_ELEM_COUNT ((size_t)RESADD_I * (size_t)RESADD_STRIDE)
#define RESADD_BYTES (RESADD_ELEM_COUNT * sizeof(elem_t))
#define ALIAS_REGION_MAX_PAGES 128U
#define RESADD_VADDR_PAGE_OFFSET 768ULL
#define CLEAN_ACC_ROW_OFFSET 256U

#define CONTIG_A_VPAGE 0x010U
#define CONTIG_B_VPAGE 0x018U
#define CONTIG_C_VPAGE 0x020U
#define INTERLEAVED_A_VPAGE 0x030U
#define INTERLEAVED_B_VPAGE 0x038U
#define INTERLEAVED_C_VPAGE 0x040U

#define CONTIG_A_LOCAL_PAGE 64U
#define CONTIG_B_LOCAL_PAGE 68U
#define CONTIG_C_LOCAL_PAGE 72U
#define INTERLEAVED_A_LOCAL_PAGE 96U
#define INTERLEAVED_B_LOCAL_PAGE 104U
#define INTERLEAVED_C_LOCAL_PAGE 112U

#define PW_I 256
#define PW_J 64
#define PW_K 256
#define PW_CHUNK_TILE_J 128
#define PW_CHUNK_OC 64
#define PW_A_STRIDE 256
#define PW_B_STRIDE 256
#define PW_C_STRIDE 256
#define PW_DIAG_I 8
#define PW_DIAG_J PW_CHUNK_TILE_J
#define PW_DIAG_K 8
#define PW_OS_SAFE_TILE_I 16U
#define PW_OS_SAFE_TILE_J MAX_BLOCK_LEN
#define PW_OS_SAFE_TILE_K (PW_K / DIM)
#define PW_ELEM_COUNT_A ((size_t)PW_I * (size_t)PW_A_STRIDE)
#define PW_ELEM_COUNT_B ((size_t)PW_K * (size_t)PW_B_STRIDE)
#define PW_ELEM_COUNT_C ((size_t)PW_I * (size_t)PW_C_STRIDE)
#define PW_BYTES_A (PW_ELEM_COUNT_A * sizeof(elem_t))
#define PW_BYTES_B (PW_ELEM_COUNT_B * sizeof(elem_t))
#define PW_BYTES_C (PW_ELEM_COUNT_C * sizeof(elem_t))
#define PW_BYTES_BIAS ((size_t)PW_J * sizeof(acc_t))
#define PW_CHUNK_BYTES_BIAS ((size_t)PW_CHUNK_TILE_J * sizeof(acc_t))
#define PW_DIAG_BYTES_A ((size_t)PW_DIAG_I * (size_t)PW_A_STRIDE * sizeof(elem_t))
#define PW_DIAG_BYTES_B ((size_t)PW_DIAG_K * (size_t)PW_B_STRIDE * sizeof(elem_t))
#define PW_DIAG_BYTES_C ((size_t)PW_DIAG_I * (size_t)PW_C_STRIDE * sizeof(elem_t))
#define PW_DIAG_BYTES_BIAS ((size_t)PW_DIAG_J * sizeof(acc_t))
#define PW_DIAG_ELEM_COUNT_C ((size_t)PW_DIAG_I * (size_t)PW_C_STRIDE)
#define PW_VADDR_PAGE_OFFSET 768ULL

#define PW_A_VPAGE 0x080U
#define PW_B_VPAGE 0x0d0U
#define PW_BIAS_VPAGE 0x120U
#define PW_C_VPAGE 0x130U
#define PW_CHUNK_BIAS_VPAGE 0x171U
#define PW_CHUNK_BIAS_CONTIG_VPAGE 0x174U

#define PW_CONTIG_A_LOCAL_PAGE 160U
#define PW_CONTIG_B_LOCAL_PAGE 240U
#define PW_CONTIG_BIAS_LOCAL_PAGE 320U
#define PW_CONTIG_C_LOCAL_PAGE 336U
#define PW_CHUNK_CONTIG_BIAS_LOCAL_PAGE 416U

#define PW_INTERLEAVED_A_LOCAL_PAGE 480U
#define PW_INTERLEAVED_B_LOCAL_PAGE 560U
#define PW_INTERLEAVED_BIAS_LOCAL_PAGE 640U
#define PW_INTERLEAVED_C_LOCAL_PAGE 656U
#define PW_CHUNK_INTERLEAVED_BIAS_LOCAL_PAGE 704U

#define PW_DIAG_A_VPAGE 0x180U
#define PW_DIAG_B_VPAGE 0x188U
#define PW_DIAG_BIAS_VPAGE 0x190U
#define PW_DIAG_C_VPAGE 0x198U

#define PW_DIAG_CONTIG_A_LOCAL_PAGE 800U
#define PW_DIAG_CONTIG_B_LOCAL_PAGE 804U
#define PW_DIAG_CONTIG_BIAS_LOCAL_PAGE 808U
#define PW_DIAG_CONTIG_C_LOCAL_PAGE 812U

#define PW_DIAG_INTERLEAVED_A_LOCAL_PAGE 840U
#define PW_DIAG_INTERLEAVED_B_LOCAL_PAGE 844U
#define PW_DIAG_INTERLEAVED_BIAS_LOCAL_PAGE 848U
#define PW_DIAG_INTERLEAVED_C_LOCAL_PAGE 852U

typedef struct {
  uint64_t vaddr;
  uint64_t page_paddrs[ALIAS_REGION_MAX_PAGES];
  size_t page_count;
  size_t bytes;
} spm_alias_region_t;

typedef enum {
  RESADD_MODE_EXPLICIT_NO_FENCE = 0,
  RESADD_MODE_EXPLICIT_SERIALIZED = 1,
  RESADD_MODE_STANDARD_WS = 2,
} resadd_mode_t;

static elem_t resadd_a_dram[RESADD_I][RESADD_STRIDE] __attribute__((aligned(64)));
static elem_t resadd_b_dram[RESADD_I][RESADD_STRIDE] __attribute__((aligned(64)));
static elem_t resadd_gold_dram[RESADD_I][RESADD_STRIDE] __attribute__((aligned(64)));
static elem_t resadd_out_shadow_dram[RESADD_I][RESADD_STRIDE] __attribute__((aligned(64)));
static elem_t pw_a_dram[PW_I][PW_A_STRIDE] __attribute__((aligned(64)));
static elem_t pw_b_dram[PW_K][PW_B_STRIDE] __attribute__((aligned(64)));
static acc_t pw_bias_dram[PW_J] __attribute__((aligned(64)));
static acc_t pw_chunk_bias_dram[PW_CHUNK_TILE_J] __attribute__((aligned(64)));
static elem_t pw_init_c_dram[PW_I][PW_C_STRIDE] __attribute__((aligned(64)));
static elem_t pw_gold_dram[PW_I][PW_C_STRIDE] __attribute__((aligned(64)));
static elem_t pw_j128_gold_dram[PW_I][PW_C_STRIDE] __attribute__((aligned(64)));
static elem_t pw_chunk_gold_dram[PW_I][PW_C_STRIDE] __attribute__((aligned(64)));
static elem_t pw_diag_a_dram[PW_DIAG_I][PW_A_STRIDE] __attribute__((aligned(64)));
static elem_t pw_diag_b_dram[PW_DIAG_K][PW_B_STRIDE] __attribute__((aligned(64)));
static acc_t pw_diag_bias_dram[PW_DIAG_J] __attribute__((aligned(64)));
static elem_t pw_diag_init_c_dram[PW_DIAG_I][PW_C_STRIDE] __attribute__((aligned(64)));
static elem_t pw_diag_gold_dram[PW_DIAG_I][PW_C_STRIDE] __attribute__((aligned(64)));
static elem_t pw_out_shadow_dram[PW_I][PW_C_STRIDE] __attribute__((aligned(64)));
static uint64_t spm_xlate_pte[SHARED_SPAD_XLATE_PTE_CAP] __attribute__((aligned(64)));

static bool rr_acquire_cfg_with_retry(uint32_t cfg_id, uint64_t manager_id) {
  unsigned long retries = 0;
  while (!rr_acquire_cfg(cfg_id, manager_id)) {
    retries++;
    if (REROCC_ACQUIRE_MAX_RETRIES != 0 && retries >= REROCC_ACQUIRE_MAX_RETRIES) {
      return false;
    }
    asm volatile("nop");
  }
  return true;
}

static inline uint32_t lcg_next(uint32_t *state) {
  *state = (*state) * 1664525u + 1013904223u;
  return *state;
}

static void init_random_elem(elem_t *buf, size_t n, uint32_t *state) {
  for (size_t i = 0; i < n; i++) {
    buf[i] = (elem_t)((int32_t)(lcg_next(state) % 31u) - 15);
  }
}

static void init_random_acc(acc_t *buf, size_t n, uint32_t *state) {
  for (size_t i = 0; i < n; i++) {
    buf[i] = (acc_t)((int32_t)(lcg_next(state) % 61u) - 30);
  }
}

static void resadd_reference(const elem_t *a, const elem_t *b, elem_t *out, size_t n) {
  for (size_t i = 0; i < n; i++) {
    acc_t sum = (acc_t)a[i] + (acc_t)b[i];
    if (sum > elem_t_max) {
      sum = elem_t_max;
    } else if (sum < elem_t_min) {
      sum = elem_t_min;
    }
    out[i] = (elem_t)sum;
  }
}

static void shared_byte_copy(volatile uint8_t *dst, const uint8_t *src, size_t n) {
  for (size_t i = 0; i < n; i++) {
    dst[i] = src[i];
  }
}

static void shared_byte_read(const volatile uint8_t *src, uint8_t *dst, size_t n) {
  for (size_t i = 0; i < n; i++) {
    dst[i] = src[i];
  }
}

static void shared_byte_zero(volatile uint8_t *dst, size_t n) {
  for (size_t i = 0; i < n; i++) {
    dst[i] = 0;
  }
}

static inline uint64_t shared_spad_vaddr(uint64_t paddr) {
  return SHARED_SPAD_XLATE_RANGE_BASE + (paddr - SHARED_SPAD_GLOBAL_ADDR_BASE);
}

static void spm_xlate_table_clear(void) {
  memset(spm_xlate_pte, 0, sizeof(spm_xlate_pte));
}

static void spm_xlate_map_page(uint64_t vaddr, uint64_t paddr) {
  const uint64_t vpage = (vaddr - SHARED_SPAD_XLATE_RANGE_BASE) >> REROCC_SPM_PAGE_SHIFT;
  if (vpage < (uint64_t)SHARED_SPAD_XLATE_PTE_CAP) {
    spm_xlate_pte[vpage] = ((paddr >> REROCC_SPM_PAGE_SHIFT) << 1) | 1ULL;
  }
}

static inline void spm_xlate_program(bool enable) {
  rerocc_gemmini_spm_xlate_cfg((uint64_t)(uintptr_t)spm_xlate_pte,
                               (uint32_t)SHARED_SPAD_XLATE_PTE_CAP,
                               REROCC_SPM_PAGE_SHIFT,
                               enable ? 1U : 0U);
  rerocc_gemmini_spm_xlate_range(enable ? SHARED_SPAD_XLATE_RANGE_BASE : 0ULL,
                                 enable ? SHARED_SPAD_XLATE_RANGE_SIZE : 0ULL);
}

static inline void spm_xlate_reset(void) {
  rerocc_gemmini_spm_xlate_cfg(0ULL, 0U, REROCC_SPM_PAGE_SHIFT, 0U);
  rerocc_gemmini_spm_xlate_range(0ULL, 0ULL);
  rerocc_gemmini_spm_xlate_flush();
}

static inline void gemmini_wait_managed(uint32_t cfg_id) {
  rr_fence(cfg_id);
  gemmini_flush(0);
  rr_fence(cfg_id);
}

static inline void gemmini_wait_for_resadd_acc_reuse(uint32_t cfg_id) {
  rr_fence(cfg_id);
}

static size_t region_page_count(uint64_t vaddr, size_t bytes) {
  const uint64_t first_page = vaddr >> REROCC_SPM_PAGE_SHIFT;
  const uint64_t last_page = (vaddr + bytes - 1ULL) >> REROCC_SPM_PAGE_SHIFT;
  return bytes == 0 ? 0 : (size_t)(last_page - first_page + 1ULL);
}

static bool alias_region_init(spm_alias_region_t *region, uint32_t vpage_base,
                              uint64_t page_offset, size_t bytes) {
  if (region == NULL) {
    return false;
  }

  region->vaddr = SHARED_SPAD_XLATE_RANGE_BASE +
                  (uint64_t)vpage_base * REROCC_SPM_PAGE_BYTES + page_offset;
  region->page_count = region_page_count(region->vaddr, bytes);
  region->bytes = bytes;
  return region->page_count <= ALIAS_REGION_MAX_PAGES;
}

static bool alias_region_build_contiguous(spm_alias_region_t *region, uint32_t vpage_base,
                                          uint64_t page_offset, size_t bytes,
                                          uint32_t local_tile, uint32_t local_page_base) {
  if (!alias_region_init(region, vpage_base, page_offset, bytes)) {
    return false;
  }

  for (size_t i = 0; i < region->page_count; i++) {
    region->page_paddrs[i] = SHARED_SPAD_LOCAL_ADDR_BASE(local_tile) +
                             (uint64_t)(local_page_base + (uint32_t)i) * REROCC_SPM_PAGE_BYTES;
  }
  return true;
}

static bool alias_region_build_interleaved(spm_alias_region_t *region, uint32_t vpage_base,
                                           uint64_t page_offset, size_t bytes,
                                           uint32_t local_page_base, uint32_t slot_base) {
  if (!alias_region_init(region, vpage_base, page_offset, bytes)) {
    return false;
  }

  for (size_t i = 0; i < region->page_count; i++) {
    const uint32_t slot = slot_base + (uint32_t)i;
    const uint32_t tile = slot % REROCC_NUM_GEMMINI;
    const uint32_t local_page = local_page_base + slot / REROCC_NUM_GEMMINI;
    region->page_paddrs[i] = SHARED_SPAD_LOCAL_ADDR_BASE(tile) +
                             (uint64_t)local_page * REROCC_SPM_PAGE_BYTES;
  }
  return true;
}

static void alias_region_write(const spm_alias_region_t *region, const uint8_t *src, size_t bytes) {
  size_t copied = 0;
  uint64_t page_offset = region->vaddr & (REROCC_SPM_PAGE_BYTES - 1ULL);

  for (size_t i = 0; i < region->page_count && copied < bytes; i++) {
    size_t chunk = (size_t)(REROCC_SPM_PAGE_BYTES - page_offset);
    if (chunk > bytes - copied) {
      chunk = bytes - copied;
    }
    shared_byte_copy((volatile uint8_t *)(uintptr_t)(region->page_paddrs[i] + page_offset),
                     src + copied, chunk);
    copied += chunk;
    page_offset = 0;
  }
}

static void alias_region_read(const spm_alias_region_t *region, uint8_t *dst, size_t bytes) {
  size_t copied = 0;
  uint64_t page_offset = region->vaddr & (REROCC_SPM_PAGE_BYTES - 1ULL);

  for (size_t i = 0; i < region->page_count && copied < bytes; i++) {
    size_t chunk = (size_t)(REROCC_SPM_PAGE_BYTES - page_offset);
    if (chunk > bytes - copied) {
      chunk = bytes - copied;
    }
    shared_byte_read((const volatile uint8_t *)(uintptr_t)(region->page_paddrs[i] + page_offset),
                     dst + copied, chunk);
    copied += chunk;
    page_offset = 0;
  }
}

static void alias_region_zero(const spm_alias_region_t *region, size_t bytes) {
  size_t cleared = 0;
  uint64_t page_offset = region->vaddr & (REROCC_SPM_PAGE_BYTES - 1ULL);

  for (size_t i = 0; i < region->page_count && cleared < bytes; i++) {
    size_t chunk = (size_t)(REROCC_SPM_PAGE_BYTES - page_offset);
    if (chunk > bytes - cleared) {
      chunk = bytes - cleared;
    }
    shared_byte_zero((volatile uint8_t *)(uintptr_t)(region->page_paddrs[i] + page_offset), chunk);
    cleared += chunk;
    page_offset = 0;
  }
}

static void spm_xlate_map_region(const spm_alias_region_t *region) {
  const uint64_t vpage_base = region->vaddr & ~(REROCC_SPM_PAGE_BYTES - 1ULL);
  for (size_t i = 0; i < region->page_count; i++) {
    spm_xlate_map_page(vpage_base + i * REROCC_SPM_PAGE_BYTES, region->page_paddrs[i]);
  }
}

static void print_region(const char *case_name, const char *region_name,
                         const spm_alias_region_t *region) {
  printf("CASE_MAP %s region=%s vaddr=0x%lx pages=%lu",
         case_name, region_name,
         (unsigned long)region->vaddr,
         (unsigned long)region->page_count);
  for (size_t i = 0; i < region->page_count; i++) {
    printf(" p%lu=0x%lx", (unsigned long)i, (unsigned long)region->page_paddrs[i]);
  }
  printf("\n");
}

static bool alias_regions_overlap(const spm_alias_region_t *lhs,
                                  const spm_alias_region_t *rhs) {
  size_t lhs_copied = 0;
  uint64_t lhs_page_offset = lhs->vaddr & (REROCC_SPM_PAGE_BYTES - 1ULL);

  for (size_t i = 0; i < lhs->page_count && lhs_copied < lhs->bytes; i++) {
    const uint64_t lhs_base = lhs->page_paddrs[i];
    const uint64_t lhs_begin = lhs_base + lhs_page_offset;
    size_t lhs_chunk = (size_t)(REROCC_SPM_PAGE_BYTES - lhs_page_offset);
    if (lhs_chunk > lhs->bytes - lhs_copied) {
      lhs_chunk = lhs->bytes - lhs_copied;
    }
    const uint64_t lhs_end = lhs_begin + lhs_chunk;
    size_t rhs_copied = 0;
    uint64_t rhs_page_offset = rhs->vaddr & (REROCC_SPM_PAGE_BYTES - 1ULL);

    for (size_t j = 0; j < rhs->page_count && rhs_copied < rhs->bytes; j++) {
      const uint64_t rhs_base = rhs->page_paddrs[j];
      const uint64_t rhs_begin = rhs_base + rhs_page_offset;
      size_t rhs_chunk = (size_t)(REROCC_SPM_PAGE_BYTES - rhs_page_offset);
      if (rhs_chunk > rhs->bytes - rhs_copied) {
        rhs_chunk = rhs->bytes - rhs_copied;
      }
      const uint64_t rhs_end = rhs_begin + rhs_chunk;

      if (!(lhs_end <= rhs_begin || rhs_end <= lhs_begin)) {
        return true;
      }

      rhs_copied += rhs_chunk;
      rhs_page_offset = 0;
    }

    lhs_copied += lhs_chunk;
    lhs_page_offset = 0;
  }

  return false;
}

static bool alias_regions_virtual_page_overlap(const spm_alias_region_t *lhs,
                                               const spm_alias_region_t *rhs) {
  const uint64_t lhs_begin = lhs->vaddr & ~(REROCC_SPM_PAGE_BYTES - 1ULL);
  const uint64_t rhs_begin = rhs->vaddr & ~(REROCC_SPM_PAGE_BYTES - 1ULL);
  const uint64_t lhs_end = lhs_begin + lhs->page_count * REROCC_SPM_PAGE_BYTES;
  const uint64_t rhs_end = rhs_begin + rhs->page_count * REROCC_SPM_PAGE_BYTES;

  return !(lhs_end <= rhs_begin || rhs_end <= lhs_begin);
}

static bool validate_alias_layout(const char **names, const spm_alias_region_t **regions,
                                  size_t count) {
  for (size_t i = 0; i < count; i++) {
    for (size_t j = i + 1; j < count; j++) {
      if (alias_regions_overlap(regions[i], regions[j])) {
        printf("ALL_TESTS_FAIL reason=region_overlap lhs=%s rhs=%s\n",
               names[i], names[j]);
        return false;
      }
    }
  }
  return true;
}

static bool validate_case_xlate_layout(const char *case_name, const char **names,
                                       const spm_alias_region_t **regions, size_t count) {
  for (size_t i = 0; i < count; i++) {
    for (size_t j = i + 1; j < count; j++) {
      if (alias_regions_virtual_page_overlap(regions[i], regions[j])) {
        printf("CASE_FAIL %s reason=region_vpage_overlap lhs=%s rhs=%s\n",
               case_name, names[i], names[j]);
        return false;
      }
    }
  }

  return true;
}

static bool elem_buffer_matches(const elem_t *expected, const elem_t *actual, size_t n,
                                const char *case_name) {
  for (size_t i = 0; i < n; i++) {
    if (expected[i] != actual[i]) {
      printf("CASE_MISMATCH %s idx=%lu exp=%d act=%d\n",
             case_name, (unsigned long)i, (int)expected[i], (int)actual[i]);
      return false;
    }
  }
  return true;
}

static bool copy_explicit_issue_no_fence(const elem_t *src, elem_t *dst,
                                         size_t I, size_t J, size_t stride,
                                         bool use_mvin2,
                                         uint32_t acc_row_offset) {
  size_t tile_I = I;
  size_t tile_J = J;
  size_t total_acc_rows;

  if (!src || !dst || I == 0 || J == 0 || stride == 0) {
    return false;
  }

  total_acc_rows = (tile_I / DIM + (tile_I % DIM != 0)) * DIM *
                   (tile_J / DIM + (tile_J % DIM != 0));

  while (total_acc_rows > ACC_ROWS / 2) {
    if (tile_I >= tile_J || tile_J <= DIM) {
      tile_I /= 2;
    } else {
      tile_J -= DIM;
    }
    if (tile_I == 0 || tile_J == 0) {
      return false;
    }
    total_acc_rows = (tile_I / DIM + (tile_I % DIM != 0)) * DIM *
                     (tile_J / DIM + (tile_J % DIM != 0));
  }

  gemmini_extended_config_st(stride * sizeof(elem_t),
                             NO_ACTIVATION,
                             ACC_SCALE_IDENTITY);
  gemmini_config_ex(WS, 0, 0);
  gemmini_extended4_config_ld(stride * sizeof(elem_t), MVIN_SCALE_IDENTITY, true, DIM, 0);
  gemmini_extended4_config_ld(stride * sizeof(elem_t), MVIN_SCALE_IDENTITY, true, DIM, 1);

  for (size_t i = 0; i < I; i += tile_I) {
    for (size_t j = 0; j < J; j += tile_J) {
      const size_t I_tile = i + tile_I <= I ? tile_I : I - i;
      const size_t J_tile = j + tile_J <= J ? tile_J : J - j;
      const elem_t *tile_src = src + i * stride + j;
      elem_t *tile_dst = dst + i * stride + j;
      const elem_t *tile_zero = (const elem_t *)resadd_out_shadow_dram + i * stride + j;
      const size_t rounded_up_J = (J_tile / DIM + (J_tile % DIM != 0)) * DIM;
      size_t blocks = rounded_up_J / DIM;
      const uint32_t acc_write_addr_start = 1U << (ADDR_LEN - 1);
      const uint32_t acc_accum_addr_start = 3U << (ADDR_LEN - 2);

      if (blocks == 0) {
        continue;
      }
      if (blocks > MAX_BLOCK_LEN) {
        blocks = MAX_BLOCK_LEN;
      }

      for (size_t ii = 0; ii < I_tile; ii += DIM) {
        for (size_t jj = 0; jj < J_tile; jj += blocks * DIM) {
          const size_t cols = jj + blocks * DIM <= J_tile ? blocks * DIM : J_tile - jj;
          const size_t rows = ii + DIM <= I_tile ? DIM : I_tile - ii;
          const elem_t *src_dram_addr = tile_src + ii * stride + jj;
          const elem_t *zero_dram_addr = tile_zero + ii * stride + jj;
          elem_t *dst_dram_addr = tile_dst + ii * stride + jj;
          const uint32_t acc_row_base = (uint32_t)(ii * (rounded_up_J / DIM) + jj);
          const uint32_t acc_write_addr = acc_write_addr_start + acc_row_offset + acc_row_base;
          const uint32_t acc_accum_addr = acc_accum_addr_start + acc_row_offset + acc_row_base;

          if (use_mvin2) {
            // mvin2 uses accumulator accumulate-on-write semantics, so the
            // backing accumulator rows must first be explicitly initialized.
            gemmini_extended_mvin(zero_dram_addr, acc_write_addr, cols, rows);
            gemmini_wait_for_resadd_acc_reuse(GEMMINI_CFG_ID);
            gemmini_extended_mvin2(src_dram_addr, acc_accum_addr, cols, rows);
            gemmini_wait_for_resadd_acc_reuse(GEMMINI_CFG_ID);
          } else {
            gemmini_extended_mvin(src_dram_addr, acc_write_addr, cols, rows);
          }
          gemmini_extended_mvout(dst_dram_addr, acc_write_addr, cols, rows);
        }
      }

      if (j + tile_J < J || i + tile_I < I) {
        gemmini_wait_for_resadd_acc_reuse(GEMMINI_CFG_ID);
      }
    }
  }

  return true;
}

static bool resadd_explicit_issue_no_fence(const elem_t *a, const elem_t *b, elem_t *out,
                                           size_t I, size_t J, size_t stride, bool relu) {
  size_t tile_I = I;
  size_t tile_J = J;
  size_t total_acc_rows;

  if (!a || !b || !out || I == 0 || J == 0 || stride == 0) {
    return false;
  }

  total_acc_rows = (tile_I / DIM + (tile_I % DIM != 0)) * DIM *
                   (tile_J / DIM + (tile_J % DIM != 0));

  while (total_acc_rows > ACC_ROWS / 2) {
    if (tile_I >= tile_J || tile_J <= DIM) {
      tile_I /= 2;
    } else {
      tile_J -= DIM;
    }
    if (tile_I == 0 || tile_J == 0) {
      return false;
    }
    total_acc_rows = (tile_I / DIM + (tile_I % DIM != 0)) * DIM *
                     (tile_J / DIM + (tile_J % DIM != 0));
  }

  gemmini_extended_config_st(stride * sizeof(elem_t),
                             relu ? RELU : NO_ACTIVATION,
                             ACC_SCALE_IDENTITY);
  gemmini_config_ex(WS, 0, 0);
  gemmini_extended4_config_ld(stride * sizeof(elem_t), MVIN_SCALE_IDENTITY, true, DIM, 0);
  gemmini_extended4_config_ld(stride * sizeof(elem_t), MVIN_SCALE_IDENTITY, true, DIM, 1);

  for (size_t i = 0; i < I; i += tile_I) {
    for (size_t j = 0; j < J; j += tile_J) {
      const size_t I_tile = i + tile_I <= I ? tile_I : I - i;
      const size_t J_tile = j + tile_J <= J ? tile_J : J - j;
      const elem_t *tile_a = a + i * stride + j;
      const elem_t *tile_b = b + i * stride + j;
      elem_t *tile_out = out + i * stride + j;
      const size_t rounded_up_J = (J_tile / DIM + (J_tile % DIM != 0)) * DIM;
      size_t blocks = rounded_up_J / DIM;
      const uint32_t A_acc_addr_start = 1U << (ADDR_LEN - 1);
      const uint32_t B_acc_addr_start = 3U << (ADDR_LEN - 2);

      if (blocks == 0) {
        continue;
      }
      if (blocks > MAX_BLOCK_LEN) {
        blocks = MAX_BLOCK_LEN;
      }

      for (size_t ii = 0; ii < I_tile; ii += DIM) {
        for (size_t jj = 0; jj < J_tile; jj += blocks * DIM) {
          const size_t cols = jj + blocks * DIM <= J_tile ? blocks * DIM : J_tile - jj;
          const size_t rows = ii + DIM <= I_tile ? DIM : I_tile - ii;
          const elem_t *A_dram_addr = tile_a + ii * stride + jj;
          const elem_t *B_dram_addr = tile_b + ii * stride + jj;
          elem_t *C_dram_addr = tile_out + ii * stride + jj;
          const uint32_t acc_row_base = (uint32_t)(ii * (rounded_up_J / DIM) + jj);
          const uint32_t A_acc_addr = A_acc_addr_start + acc_row_base;
          const uint32_t B_acc_addr = B_acc_addr_start + acc_row_base;

          gemmini_extended_mvin(A_dram_addr, A_acc_addr, cols, rows);
          // In ReRoCC manager mode the dependent mvin2 must not issue until
          // the accumulator rows have been updated with A.
          gemmini_wait_for_resadd_acc_reuse(GEMMINI_CFG_ID);
          gemmini_extended_mvin2(B_dram_addr, B_acc_addr, cols, rows);
          // The explicit resadd path then reads the same accumulator rows back
          // out through the non-accumulate address view.
          gemmini_wait_for_resadd_acc_reuse(GEMMINI_CFG_ID);
          gemmini_extended_mvout(C_dram_addr, A_acc_addr, cols, rows);
        }
      }

      if (j + tile_J < J || i + tile_I < I) {
        gemmini_wait_for_resadd_acc_reuse(GEMMINI_CFG_ID);
      }
    }
  }

  return true;
}

static bool resadd_explicit_issue_serialized(const elem_t *a, const elem_t *b, elem_t *out,
                                             size_t I, size_t J, size_t stride, bool relu) {
  size_t tile_I = I;
  size_t tile_J = J;
  size_t total_acc_rows;

  if (!a || !b || !out || I == 0 || J == 0 || stride == 0) {
    return false;
  }

  total_acc_rows = (tile_I / DIM + (tile_I % DIM != 0)) * DIM *
                   (tile_J / DIM + (tile_J % DIM != 0));

  while (total_acc_rows > ACC_ROWS / 2) {
    if (tile_I >= tile_J || tile_J <= DIM) {
      tile_I /= 2;
    } else {
      tile_J -= DIM;
    }
    if (tile_I == 0 || tile_J == 0) {
      return false;
    }
    total_acc_rows = (tile_I / DIM + (tile_I % DIM != 0)) * DIM *
                     (tile_J / DIM + (tile_J % DIM != 0));
  }

  gemmini_extended_config_st(stride * sizeof(elem_t),
                             relu ? RELU : NO_ACTIVATION,
                             ACC_SCALE_IDENTITY);
  gemmini_config_ex(WS, 0, 0);
  gemmini_extended4_config_ld(stride * sizeof(elem_t), MVIN_SCALE_IDENTITY, true, DIM, 0);
  gemmini_extended4_config_ld(stride * sizeof(elem_t), MVIN_SCALE_IDENTITY, true, DIM, 1);

  for (size_t i = 0; i < I; i += tile_I) {
    for (size_t j = 0; j < J; j += tile_J) {
      const size_t I_tile = i + tile_I <= I ? tile_I : I - i;
      const size_t J_tile = j + tile_J <= J ? tile_J : J - j;
      const elem_t *tile_a = a + i * stride + j;
      const elem_t *tile_b = b + i * stride + j;
      elem_t *tile_out = out + i * stride + j;
      const size_t rounded_up_J = (J_tile / DIM + (J_tile % DIM != 0)) * DIM;
      size_t blocks = rounded_up_J / DIM;
      const uint32_t A_acc_addr_start = 1U << (ADDR_LEN - 1);
      const uint32_t B_acc_addr_start = 3U << (ADDR_LEN - 2);

      if (blocks == 0) {
        continue;
      }
      if (blocks > MAX_BLOCK_LEN) {
        blocks = MAX_BLOCK_LEN;
      }

      for (size_t ii = 0; ii < I_tile; ii += DIM) {
        for (size_t jj = 0; jj < J_tile; jj += blocks * DIM) {
          const size_t cols = jj + blocks * DIM <= J_tile ? blocks * DIM : J_tile - jj;
          const size_t rows = ii + DIM <= I_tile ? DIM : I_tile - ii;
          const elem_t *A_dram_addr = tile_a + ii * stride + jj;
          const elem_t *B_dram_addr = tile_b + ii * stride + jj;
          elem_t *C_dram_addr = tile_out + ii * stride + jj;
          const uint32_t acc_row_base = (uint32_t)(ii * (rounded_up_J / DIM) + jj);
          const uint32_t A_acc_addr = A_acc_addr_start + acc_row_base;
          const uint32_t B_acc_addr = B_acc_addr_start + acc_row_base;

          gemmini_extended_mvin(A_dram_addr, A_acc_addr, cols, rows);
          gemmini_wait_for_resadd_acc_reuse(GEMMINI_CFG_ID);
          gemmini_extended_mvin2(B_dram_addr, B_acc_addr, cols, rows);
          gemmini_wait_for_resadd_acc_reuse(GEMMINI_CFG_ID);
          gemmini_extended_mvout(C_dram_addr, A_acc_addr, cols, rows);
          gemmini_wait_for_resadd_acc_reuse(GEMMINI_CFG_ID);
        }
      }
    }
  }

  return true;
}

static bool resadd_standard_issue(const elem_t *a, const elem_t *b, elem_t *out,
                                  size_t I, size_t J, size_t stride, bool relu) {
  if (!a || !b || !out || I == 0 || J == 0 || stride == 0) {
    return false;
  }

  tiled_resadd_stride_auto(I, J,
                           MVIN_SCALE_IDENTITY,
                           MVIN_SCALE_IDENTITY,
                           ACC_SCALE_IDENTITY,
                           stride,
                           a, b, out, relu, WS);
  return true;
}

static elem_t sat_i8_from_acc(acc_t value) {
  if (value > elem_t_max) value = elem_t_max;
  if (value < elem_t_min) value = elem_t_min;
  return (elem_t)value;
}

static void pointwise_matmul_reference(const elem_t *a, const elem_t *b, const acc_t *bias,
                                       elem_t *out) {
  for (size_t i = 0; i < PW_I; ++i) {
    for (size_t j = 0; j < PW_J; ++j) {
      acc_t acc = bias ? bias[j] : 0;
      for (size_t k = 0; k < PW_K; ++k) {
        acc += (acc_t)a[i * PW_A_STRIDE + k] * (acc_t)b[k * PW_B_STRIDE + j];
      }
      out[i * PW_C_STRIDE + j] = sat_i8_from_acc(acc);
    }
  }
}

static void pointwise_matmul_reference_dim_j(const elem_t *a, const elem_t *b,
                                             const acc_t *bias, const elem_t *init_c,
                                             elem_t *out, size_t dim_j) {
  memcpy(out, init_c, PW_BYTES_C);

  for (size_t i = 0; i < PW_I; ++i) {
    for (size_t j = 0; j < dim_j; ++j) {
      acc_t acc = bias ? bias[j] : 0;
      for (size_t k = 0; k < PW_K; ++k) {
        acc += (acc_t)a[i * PW_A_STRIDE + k] * (acc_t)b[k * PW_B_STRIDE + j];
      }
      out[i * PW_C_STRIDE + j] = sat_i8_from_acc(acc);
    }
  }
}

static void pointwise_matmul_reference_shape(const elem_t *a, const elem_t *b,
                                             const acc_t *bias, const elem_t *init_c,
                                             elem_t *out,
                                             size_t dim_i, size_t dim_j, size_t dim_k,
                                             size_t a_stride, size_t b_stride,
                                             size_t c_stride) {
  const size_t c_bytes = dim_i * c_stride * sizeof(elem_t);
  memcpy(out, init_c, c_bytes);

  for (size_t i = 0; i < dim_i; ++i) {
    for (size_t j = 0; j < dim_j; ++j) {
      acc_t acc = bias ? bias[j] : 0;
      for (size_t k = 0; k < dim_k; ++k) {
        acc += (acc_t)a[i * a_stride + k] * (acc_t)b[k * b_stride + j];
      }
      out[i * c_stride + j] = sat_i8_from_acc(acc);
    }
  }
}

static void pointwise_matmul_chunked_reference(const elem_t *a, const elem_t *b,
                                               const acc_t *bias, const elem_t *init_c,
                                               elem_t *out) {
  memcpy(out, init_c, PW_BYTES_C);

  for (size_t oc_beg = 0; oc_beg < PW_CHUNK_TILE_J; oc_beg += PW_CHUNK_OC) {
    const size_t oc_tile =
      oc_beg + PW_CHUNK_OC <= PW_CHUNK_TILE_J ? PW_CHUNK_OC : (PW_CHUNK_TILE_J - oc_beg);

    for (size_t i = 0; i < PW_I; ++i) {
      for (size_t j = 0; j < oc_tile; ++j) {
        acc_t acc = bias ? bias[oc_beg + j] : 0;
        for (size_t k = 0; k < PW_K; ++k) {
          acc += (acc_t)a[i * PW_A_STRIDE + k] *
                 (acc_t)b[k * PW_B_STRIDE + oc_beg + j];
        }
        out[i * PW_C_STRIDE + oc_beg + j] = sat_i8_from_acc(acc);
      }
    }
  }
}

static bool pointwise_matmul_issue(const elem_t *a, const elem_t *b, const acc_t *bias,
                                   elem_t *out, enum tiled_matmul_type_t tiled_type) {
  if (!a || !b || !out) return false;
  tiled_matmul_nn_stride_auto(PW_I, PW_J, PW_K,
                              PW_A_STRIDE, PW_B_STRIDE, PW_C_STRIDE,
                              a, b, bias, out,
                              NO_ACTIVATION, ACC_SCALE_IDENTITY, true,
                              tiled_type);
  return true;
}

static bool pointwise_matmul_issue_dim_j(const elem_t *a, const elem_t *b, const acc_t *bias,
                                         elem_t *out, size_t dim_j,
                                         enum tiled_matmul_type_t tiled_type) {
  if (!a || !b || !out || dim_j == 0) return false;
  tiled_matmul_nn_stride_auto(PW_I, dim_j, PW_K,
                              PW_A_STRIDE, PW_B_STRIDE, PW_C_STRIDE,
                              a, b, bias, out,
                              NO_ACTIVATION, ACC_SCALE_IDENTITY, true,
                              tiled_type);
  return true;
}

static bool pointwise_matmul_issue_shape(const elem_t *a, const elem_t *b, const acc_t *bias,
                                         elem_t *out,
                                         size_t dim_i, size_t dim_j, size_t dim_k,
                                         size_t a_stride, size_t b_stride, size_t c_stride,
                                         enum tiled_matmul_type_t tiled_type) {
  if (!a || !b || !out || dim_i == 0 || dim_j == 0 || dim_k == 0 ||
      a_stride == 0 || b_stride == 0 || c_stride == 0) {
    return false;
  }
  tiled_matmul_nn_stride_auto(dim_i, dim_j, dim_k,
                              a_stride, b_stride, c_stride,
                              a, b, bias, out,
                              NO_ACTIVATION, ACC_SCALE_IDENTITY, true,
                              tiled_type);
  return true;
}

static bool pointwise_matmul_issue_dim_j_explicit_tiles(const elem_t *a, const elem_t *b,
                                                        const acc_t *bias, elem_t *out,
                                                        size_t dim_j,
                                                        size_t tile_i, size_t tile_j,
                                                        size_t tile_k,
                                                        enum tiled_matmul_type_t tiled_type) {
  if (!a || !b || !out || dim_j == 0 || tile_i == 0 || tile_j == 0 || tile_k == 0) return false;
  tiled_matmul(PW_I, dim_j, PW_K,
               a, b, bias, out,
               PW_A_STRIDE, PW_B_STRIDE, PW_C_STRIDE, PW_C_STRIDE,
               MVIN_SCALE_IDENTITY, MVIN_SCALE_IDENTITY, MVIN_SCALE_IDENTITY,
               NO_ACTIVATION, ACC_SCALE_IDENTITY, 0, true,
               tile_i, tile_j, tile_k,
               false, false,
               false, false,
               0,
               tiled_type);
  return true;
}

static bool pointwise_matmul_issue_chunked(const elem_t *a, const elem_t *b, const acc_t *bias,
                                           elem_t *out, enum tiled_matmul_type_t tiled_type) {
  if (!a || !b || !out) return false;

  for (size_t oc_beg = 0; oc_beg < PW_CHUNK_TILE_J; oc_beg += PW_CHUNK_OC) {
    const size_t oc_tile =
      oc_beg + PW_CHUNK_OC <= PW_CHUNK_TILE_J ? PW_CHUNK_OC : (PW_CHUNK_TILE_J - oc_beg);
    tiled_matmul_nn_stride_auto(PW_I, oc_tile, PW_K,
                                PW_A_STRIDE, PW_B_STRIDE, PW_C_STRIDE,
                                a, b + oc_beg, bias ? (bias + oc_beg) : NULL, out + oc_beg,
                                NO_ACTIVATION, ACC_SCALE_IDENTITY, true,
                                tiled_type);
  }

  return true;
}

static bool run_pointwise_matmul_case_dim_j(const char *name, int gemmini_manager_id,
                                            const spm_alias_region_t *a_region,
                                            const spm_alias_region_t *b_region,
                                            const spm_alias_region_t *bias_region,
                                            const spm_alias_region_t *c_region,
                                            const acc_t *bias_src, size_t bias_bytes,
                                            const elem_t *expected, size_t dim_j,
                                            enum tiled_matmul_type_t tiled_type) {
  bool ok;
  const elem_t *a_req = (const elem_t *)(uintptr_t)a_region->vaddr;
  const elem_t *b_req = (const elem_t *)(uintptr_t)b_region->vaddr;
  const acc_t *bias_req = (const acc_t *)(uintptr_t)bias_region->vaddr;
  elem_t *c_req = (elem_t *)(uintptr_t)c_region->vaddr;
  const char *type_name = tiled_type == OS ? "OS" :
                          tiled_type == WS ? "WS" : "CPU";

  printf("CASE_START %s bytes_a=%lu bytes_b=%lu bytes_bias=%lu bytes_c=%lu page_bytes=%lu page_offset=%lu type=%s\n",
         name,
         (unsigned long)PW_BYTES_A,
         (unsigned long)PW_BYTES_B,
         (unsigned long)bias_bytes,
         (unsigned long)PW_BYTES_C,
         (unsigned long)REROCC_SPM_PAGE_BYTES,
         (unsigned long)PW_VADDR_PAGE_OFFSET,
         type_name);
  print_region(name, "A", a_region);
  print_region(name, "B", b_region);
  print_region(name, "BIAS", bias_region);
  print_region(name, "C", c_region);

  {
    const char *region_names[] = {"A", "B", "BIAS", "C"};
    const spm_alias_region_t *regions[] = {a_region, b_region, bias_region, c_region};
    if (!validate_case_xlate_layout(name, region_names, regions,
                                    sizeof(regions) / sizeof(regions[0]))) {
      return false;
    }
  }

  alias_region_write(a_region, (const uint8_t *)pw_a_dram, PW_BYTES_A);
  alias_region_write(b_region, (const uint8_t *)pw_b_dram, PW_BYTES_B);
  alias_region_write(bias_region, (const uint8_t *)bias_src, bias_bytes);
  alias_region_write(c_region, (const uint8_t *)pw_init_c_dram, PW_BYTES_C);
  memset(pw_out_shadow_dram, 0, sizeof(pw_out_shadow_dram));

  if (!rr_acquire_cfg_with_retry(GEMMINI_CFG_ID, (uint64_t)gemmini_manager_id)) {
    printf("CASE_FAIL %s reason=acquire\n", name);
    return false;
  }

  rr_set_opc(3, GEMMINI_CFG_ID);
  gemmini_flush(0);
  spm_xlate_table_clear();
  spm_xlate_map_region(a_region);
  spm_xlate_map_region(b_region);
  spm_xlate_map_region(bias_region);
  spm_xlate_map_region(c_region);
  spm_xlate_program(true);

  if (!pointwise_matmul_issue_dim_j(a_req, b_req, bias_req, c_req, dim_j, tiled_type)) {
    spm_xlate_reset();
    rr_fence(GEMMINI_CFG_ID);
    rr_release(GEMMINI_CFG_ID);
    printf("CASE_FAIL %s reason=issue\n", name);
    return false;
  }

  gemmini_wait_managed(GEMMINI_CFG_ID);
  spm_xlate_reset();
  rr_fence(GEMMINI_CFG_ID);
  rr_release(GEMMINI_CFG_ID);

  alias_region_read(c_region, (uint8_t *)pw_out_shadow_dram, PW_BYTES_C);
  ok = elem_buffer_matches(expected,
                           (const elem_t *)pw_out_shadow_dram,
                           PW_ELEM_COUNT_C, name);
  printf("CASE_RESULT %s %s\n", name, ok ? "PASS" : "FAIL");
  return ok;
}

static bool run_pointwise_matmul_case(const char *name, int gemmini_manager_id,
                                      const spm_alias_region_t *a_region,
                                      const spm_alias_region_t *b_region,
                                      const spm_alias_region_t *bias_region,
                                      const spm_alias_region_t *c_region,
                                      enum tiled_matmul_type_t tiled_type) {
  return run_pointwise_matmul_case_dim_j(name, gemmini_manager_id,
                                         a_region, b_region, bias_region, c_region,
                                         (const acc_t *)pw_bias_dram, PW_BYTES_BIAS,
                                         (const elem_t *)pw_gold_dram, PW_J,
                                         tiled_type);
}

static bool run_pointwise_matmul_case_shape(const char *name, int gemmini_manager_id,
                                            const spm_alias_region_t *a_region,
                                            const spm_alias_region_t *b_region,
                                            const spm_alias_region_t *bias_region,
                                            const spm_alias_region_t *c_region,
                                            const elem_t *a_src, size_t a_bytes,
                                            const elem_t *b_src, size_t b_bytes,
                                            const acc_t *bias_src, size_t bias_bytes,
                                            const elem_t *c_init, size_t c_bytes,
                                            const elem_t *expected, size_t expected_elems,
                                            size_t dim_i, size_t dim_j, size_t dim_k,
                                            size_t a_stride, size_t b_stride, size_t c_stride,
                                            enum tiled_matmul_type_t tiled_type) {
  bool ok;
  const elem_t *a_req = (const elem_t *)(uintptr_t)a_region->vaddr;
  const elem_t *b_req = (const elem_t *)(uintptr_t)b_region->vaddr;
  const acc_t *bias_req = (const acc_t *)(uintptr_t)bias_region->vaddr;
  elem_t *c_req = (elem_t *)(uintptr_t)c_region->vaddr;
  const char *type_name = tiled_type == OS ? "OS" :
                          tiled_type == WS ? "WS" : "CPU";

  printf("CASE_START %s bytes_a=%lu bytes_b=%lu bytes_bias=%lu bytes_c=%lu page_bytes=%lu page_offset=%lu type=%s I=%lu J=%lu K=%lu a_stride=%lu b_stride=%lu c_stride=%lu\n",
         name,
         (unsigned long)a_bytes,
         (unsigned long)b_bytes,
         (unsigned long)bias_bytes,
         (unsigned long)c_bytes,
         (unsigned long)REROCC_SPM_PAGE_BYTES,
         (unsigned long)PW_VADDR_PAGE_OFFSET,
         type_name,
         (unsigned long)dim_i,
         (unsigned long)dim_j,
         (unsigned long)dim_k,
         (unsigned long)a_stride,
         (unsigned long)b_stride,
         (unsigned long)c_stride);
  print_region(name, "A", a_region);
  print_region(name, "B", b_region);
  print_region(name, "BIAS", bias_region);
  print_region(name, "C", c_region);

  {
    const char *region_names[] = {"A", "B", "BIAS", "C"};
    const spm_alias_region_t *regions[] = {a_region, b_region, bias_region, c_region};
    if (!validate_case_xlate_layout(name, region_names, regions,
                                    sizeof(regions) / sizeof(regions[0]))) {
      return false;
    }
  }

  alias_region_write(a_region, (const uint8_t *)a_src, a_bytes);
  alias_region_write(b_region, (const uint8_t *)b_src, b_bytes);
  alias_region_write(bias_region, (const uint8_t *)bias_src, bias_bytes);
  alias_region_write(c_region, (const uint8_t *)c_init, c_bytes);
  memset(pw_out_shadow_dram, 0, sizeof(pw_out_shadow_dram));

  if (!rr_acquire_cfg_with_retry(GEMMINI_CFG_ID, (uint64_t)gemmini_manager_id)) {
    printf("CASE_FAIL %s reason=acquire\n", name);
    return false;
  }

  rr_set_opc(3, GEMMINI_CFG_ID);
  gemmini_flush(0);
  spm_xlate_table_clear();
  spm_xlate_map_region(a_region);
  spm_xlate_map_region(b_region);
  spm_xlate_map_region(bias_region);
  spm_xlate_map_region(c_region);
  spm_xlate_program(true);

  if (!pointwise_matmul_issue_shape(a_req, b_req, bias_req, c_req,
                                    dim_i, dim_j, dim_k,
                                    a_stride, b_stride, c_stride,
                                    tiled_type)) {
    spm_xlate_reset();
    rr_fence(GEMMINI_CFG_ID);
    rr_release(GEMMINI_CFG_ID);
    printf("CASE_FAIL %s reason=issue\n", name);
    return false;
  }

  gemmini_wait_managed(GEMMINI_CFG_ID);
  spm_xlate_reset();
  rr_fence(GEMMINI_CFG_ID);
  rr_release(GEMMINI_CFG_ID);

  alias_region_read(c_region, (uint8_t *)pw_out_shadow_dram, c_bytes);
  ok = elem_buffer_matches(expected,
                           (const elem_t *)pw_out_shadow_dram,
                           expected_elems, name);
  printf("CASE_RESULT %s %s\n", name, ok ? "PASS" : "FAIL");
  return ok;
}

static bool run_pointwise_matmul_case_dim_j_explicit_tiles(const char *name,
                                                           int gemmini_manager_id,
                                                           const spm_alias_region_t *a_region,
                                                           const spm_alias_region_t *b_region,
                                                           const spm_alias_region_t *bias_region,
                                                           const spm_alias_region_t *c_region,
                                                           const acc_t *bias_src,
                                                           size_t bias_bytes,
                                                           const elem_t *expected,
                                                           size_t dim_j,
                                                           size_t tile_i,
                                                           size_t tile_j,
                                                           size_t tile_k,
                                                           enum tiled_matmul_type_t tiled_type) {
  bool ok;
  const elem_t *a_req = (const elem_t *)(uintptr_t)a_region->vaddr;
  const elem_t *b_req = (const elem_t *)(uintptr_t)b_region->vaddr;
  const acc_t *bias_req = (const acc_t *)(uintptr_t)bias_region->vaddr;
  elem_t *c_req = (elem_t *)(uintptr_t)c_region->vaddr;
  const char *type_name = tiled_type == OS ? "OS" :
                          tiled_type == WS ? "WS" : "CPU";

  printf("CASE_START %s bytes_a=%lu bytes_b=%lu bytes_bias=%lu bytes_c=%lu page_bytes=%lu page_offset=%lu type=%s tile_i=%lu tile_j=%lu tile_k=%lu\n",
         name,
         (unsigned long)PW_BYTES_A,
         (unsigned long)PW_BYTES_B,
         (unsigned long)bias_bytes,
         (unsigned long)PW_BYTES_C,
         (unsigned long)REROCC_SPM_PAGE_BYTES,
         (unsigned long)PW_VADDR_PAGE_OFFSET,
         type_name,
         (unsigned long)tile_i,
         (unsigned long)tile_j,
         (unsigned long)tile_k);
  print_region(name, "A", a_region);
  print_region(name, "B", b_region);
  print_region(name, "BIAS", bias_region);
  print_region(name, "C", c_region);

  {
    const char *region_names[] = {"A", "B", "BIAS", "C"};
    const spm_alias_region_t *regions[] = {a_region, b_region, bias_region, c_region};
    if (!validate_case_xlate_layout(name, region_names, regions,
                                    sizeof(regions) / sizeof(regions[0]))) {
      return false;
    }
  }

  alias_region_write(a_region, (const uint8_t *)pw_a_dram, PW_BYTES_A);
  alias_region_write(b_region, (const uint8_t *)pw_b_dram, PW_BYTES_B);
  alias_region_write(bias_region, (const uint8_t *)bias_src, bias_bytes);
  alias_region_write(c_region, (const uint8_t *)pw_init_c_dram, PW_BYTES_C);
  memset(pw_out_shadow_dram, 0, sizeof(pw_out_shadow_dram));

  if (!rr_acquire_cfg_with_retry(GEMMINI_CFG_ID, (uint64_t)gemmini_manager_id)) {
    printf("CASE_FAIL %s reason=acquire\n", name);
    return false;
  }

  rr_set_opc(3, GEMMINI_CFG_ID);
  gemmini_flush(0);
  spm_xlate_table_clear();
  spm_xlate_map_region(a_region);
  spm_xlate_map_region(b_region);
  spm_xlate_map_region(bias_region);
  spm_xlate_map_region(c_region);
  spm_xlate_program(true);

  if (!pointwise_matmul_issue_dim_j_explicit_tiles(a_req, b_req, bias_req, c_req, dim_j,
                                                   tile_i, tile_j, tile_k, tiled_type)) {
    spm_xlate_reset();
    rr_fence(GEMMINI_CFG_ID);
    rr_release(GEMMINI_CFG_ID);
    printf("CASE_FAIL %s reason=issue\n", name);
    return false;
  }

  gemmini_wait_managed(GEMMINI_CFG_ID);
  spm_xlate_reset();
  rr_fence(GEMMINI_CFG_ID);
  rr_release(GEMMINI_CFG_ID);

  alias_region_read(c_region, (uint8_t *)pw_out_shadow_dram, PW_BYTES_C);
  ok = elem_buffer_matches(expected,
                           (const elem_t *)pw_out_shadow_dram,
                           PW_ELEM_COUNT_C, name);
  printf("CASE_RESULT %s %s\n", name, ok ? "PASS" : "FAIL");
  return ok;
}

static bool run_pointwise_matmul_chunked_case(const char *name, int gemmini_manager_id,
                                              const spm_alias_region_t *a_region,
                                              const spm_alias_region_t *b_region,
                                              const spm_alias_region_t *bias_region,
                                              const spm_alias_region_t *c_region,
                                              enum tiled_matmul_type_t tiled_type) {
  bool ok;
  const elem_t *a_req = (const elem_t *)(uintptr_t)a_region->vaddr;
  const elem_t *b_req = (const elem_t *)(uintptr_t)b_region->vaddr;
  const acc_t *bias_req = (const acc_t *)(uintptr_t)bias_region->vaddr;
  elem_t *c_req = (elem_t *)(uintptr_t)c_region->vaddr;
  const char *type_name = tiled_type == OS ? "OS" :
                          tiled_type == WS ? "WS" : "CPU";

  printf("CASE_START %s bytes_a=%lu bytes_b=%lu bytes_bias=%lu bytes_c=%lu page_bytes=%lu page_offset=%lu type=%s tile_j=%u oc_chunk=%u\n",
         name,
         (unsigned long)PW_BYTES_A,
         (unsigned long)PW_BYTES_B,
         (unsigned long)PW_CHUNK_BYTES_BIAS,
         (unsigned long)PW_BYTES_C,
         (unsigned long)REROCC_SPM_PAGE_BYTES,
         (unsigned long)PW_VADDR_PAGE_OFFSET,
         type_name,
         (unsigned)PW_CHUNK_TILE_J,
         (unsigned)PW_CHUNK_OC);
  print_region(name, "A", a_region);
  print_region(name, "B", b_region);
  print_region(name, "BIAS", bias_region);
  print_region(name, "C", c_region);

  {
    const char *region_names[] = {"A", "B", "BIAS", "C"};
    const spm_alias_region_t *regions[] = {a_region, b_region, bias_region, c_region};
    if (!validate_case_xlate_layout(name, region_names, regions,
                                    sizeof(regions) / sizeof(regions[0]))) {
      return false;
    }
  }

  alias_region_write(a_region, (const uint8_t *)pw_a_dram, PW_BYTES_A);
  alias_region_write(b_region, (const uint8_t *)pw_b_dram, PW_BYTES_B);
  alias_region_write(bias_region, (const uint8_t *)pw_chunk_bias_dram, PW_CHUNK_BYTES_BIAS);
  alias_region_write(c_region, (const uint8_t *)pw_init_c_dram, PW_BYTES_C);
  memset(pw_out_shadow_dram, 0, sizeof(pw_out_shadow_dram));

  if (!rr_acquire_cfg_with_retry(GEMMINI_CFG_ID, (uint64_t)gemmini_manager_id)) {
    printf("CASE_FAIL %s reason=acquire\n", name);
    return false;
  }

  rr_set_opc(3, GEMMINI_CFG_ID);
  gemmini_flush(0);
  spm_xlate_table_clear();
  spm_xlate_map_region(a_region);
  spm_xlate_map_region(b_region);
  spm_xlate_map_region(bias_region);
  spm_xlate_map_region(c_region);
  spm_xlate_program(true);

  if (!pointwise_matmul_issue_chunked(a_req, b_req, bias_req, c_req, tiled_type)) {
    spm_xlate_reset();
    rr_fence(GEMMINI_CFG_ID);
    rr_release(GEMMINI_CFG_ID);
    printf("CASE_FAIL %s reason=issue\n", name);
    return false;
  }

  gemmini_wait_managed(GEMMINI_CFG_ID);
  spm_xlate_reset();
  rr_fence(GEMMINI_CFG_ID);
  rr_release(GEMMINI_CFG_ID);

  alias_region_read(c_region, (uint8_t *)pw_out_shadow_dram, PW_BYTES_C);
  ok = elem_buffer_matches((const elem_t *)pw_chunk_gold_dram,
                           (const elem_t *)pw_out_shadow_dram,
                           PW_ELEM_COUNT_C, name);
  printf("CASE_RESULT %s %s\n", name, ok ? "PASS" : "FAIL");
  return ok;
}

static bool run_copy_case(const char *name, int gemmini_manager_id,
                          const spm_alias_region_t *src_region,
                          const spm_alias_region_t *dst_region,
                          const elem_t *expected,
                          bool use_mvin2,
                          uint32_t acc_row_offset) {
  bool ok;
  const elem_t *src_req = (const elem_t *)(uintptr_t)src_region->vaddr;
  elem_t *dst_req = (elem_t *)(uintptr_t)dst_region->vaddr;

  printf("CASE_START %s bytes=%lu page_bytes=%lu page_offset=%lu mode=%s\n",
         name,
         (unsigned long)RESADD_BYTES,
         (unsigned long)REROCC_SPM_PAGE_BYTES,
         (unsigned long)RESADD_VADDR_PAGE_OFFSET,
         use_mvin2 ? "mvin2" : "mvin");
  print_region(name, "SRC", src_region);
  print_region(name, "DST", dst_region);

  alias_region_write(src_region, (const uint8_t *)expected, RESADD_BYTES);
  alias_region_zero(dst_region, RESADD_BYTES);
  memset(resadd_out_shadow_dram, 0, sizeof(resadd_out_shadow_dram));

  if (!rr_acquire_cfg_with_retry(GEMMINI_CFG_ID, (uint64_t)gemmini_manager_id)) {
    printf("CASE_FAIL %s reason=acquire\n", name);
    return false;
  }

  rr_set_opc(3, GEMMINI_CFG_ID);
  gemmini_flush(0);
  spm_xlate_table_clear();
  spm_xlate_map_region(src_region);
  spm_xlate_map_region(dst_region);
  spm_xlate_program(true);

  if (!copy_explicit_issue_no_fence(src_req, dst_req,
                                    RESADD_I, RESADD_J, RESADD_STRIDE,
                                    use_mvin2, acc_row_offset)) {
    spm_xlate_reset();
    rr_fence(GEMMINI_CFG_ID);
    rr_release(GEMMINI_CFG_ID);
    printf("CASE_FAIL %s reason=issue\n", name);
    return false;
  }

  gemmini_wait_managed(GEMMINI_CFG_ID);
  spm_xlate_reset();
  rr_fence(GEMMINI_CFG_ID);
  rr_release(GEMMINI_CFG_ID);

  alias_region_read(dst_region, (uint8_t *)resadd_out_shadow_dram, RESADD_BYTES);
  ok = elem_buffer_matches(expected,
                           (const elem_t *)resadd_out_shadow_dram,
                           RESADD_ELEM_COUNT, name);
  printf("CASE_RESULT %s %s\n", name, ok ? "PASS" : "FAIL");
  return ok;
}

static bool run_resadd_case(const char *name, int gemmini_manager_id,
                            const spm_alias_region_t *a_region,
                            const spm_alias_region_t *b_region,
                            const spm_alias_region_t *c_region,
                            resadd_mode_t mode) {
  bool ok;
  const elem_t *a_req = (const elem_t *)(uintptr_t)a_region->vaddr;
  const elem_t *b_req = (const elem_t *)(uintptr_t)b_region->vaddr;
  elem_t *c_req = (elem_t *)(uintptr_t)c_region->vaddr;
  const char *mode_name = "explicit";

  switch (mode) {
    case RESADD_MODE_EXPLICIT_NO_FENCE:
      mode_name = "explicit";
      break;
    case RESADD_MODE_EXPLICIT_SERIALIZED:
      mode_name = "explicit_serialized";
      break;
    case RESADD_MODE_STANDARD_WS:
      mode_name = "standard_ws";
      break;
    default:
      mode_name = "unknown";
      break;
  }

  printf("CASE_START %s bytes=%lu page_bytes=%lu page_offset=%lu mode=%s\n",
         name,
         (unsigned long)RESADD_BYTES,
         (unsigned long)REROCC_SPM_PAGE_BYTES,
         (unsigned long)RESADD_VADDR_PAGE_OFFSET,
         mode_name);
  print_region(name, "A", a_region);
  print_region(name, "B", b_region);
  print_region(name, "C", c_region);

  alias_region_write(a_region, (const uint8_t *)resadd_a_dram, RESADD_BYTES);
  alias_region_write(b_region, (const uint8_t *)resadd_b_dram, RESADD_BYTES);
  alias_region_zero(c_region, RESADD_BYTES);
  memset(resadd_out_shadow_dram, 0, sizeof(resadd_out_shadow_dram));

  if (!rr_acquire_cfg_with_retry(GEMMINI_CFG_ID, (uint64_t)gemmini_manager_id)) {
    printf("CASE_FAIL %s reason=acquire\n", name);
    return false;
  }

  rr_set_opc(3, GEMMINI_CFG_ID);
  gemmini_flush(0);
  spm_xlate_table_clear();
  spm_xlate_map_region(a_region);
  spm_xlate_map_region(b_region);
  spm_xlate_map_region(c_region);
  spm_xlate_program(true);

  bool issued = false;
  switch (mode) {
    case RESADD_MODE_EXPLICIT_NO_FENCE:
      issued = resadd_explicit_issue_no_fence(a_req, b_req, c_req,
                                              RESADD_I, RESADD_J, RESADD_STRIDE, false);
      break;
    case RESADD_MODE_EXPLICIT_SERIALIZED:
      issued = resadd_explicit_issue_serialized(a_req, b_req, c_req,
                                                RESADD_I, RESADD_J, RESADD_STRIDE, false);
      break;
    case RESADD_MODE_STANDARD_WS:
      issued = resadd_standard_issue(a_req, b_req, c_req,
                                     RESADD_I, RESADD_J, RESADD_STRIDE, false);
      break;
    default:
      issued = false;
      break;
  }

  if (!issued) {
    spm_xlate_reset();
    rr_fence(GEMMINI_CFG_ID);
    rr_release(GEMMINI_CFG_ID);
    printf("CASE_FAIL %s reason=issue\n", name);
    return false;
  }

  gemmini_wait_managed(GEMMINI_CFG_ID);
  spm_xlate_reset();
  rr_fence(GEMMINI_CFG_ID);
  rr_release(GEMMINI_CFG_ID);

  alias_region_read(c_region, (uint8_t *)resadd_out_shadow_dram, RESADD_BYTES);
  ok = elem_buffer_matches((const elem_t *)resadd_gold_dram,
                           (const elem_t *)resadd_out_shadow_dram,
                           RESADD_ELEM_COUNT, name);
  printf("CASE_RESULT %s %s\n", name, ok ? "PASS" : "FAIL");
  return ok;
}

void thread_entry(int cid, int nc) {
  (void)nc;
  if (cid != 0) {
    while (1) {
      asm volatile("wfi");
    }
  }

  const uint32_t local_gemmini_id = REROCC_TEST_LOCAL_GEMMINI_ID;
  const int gemmini_manager_id = REROCC_GEMMINI_BASE_ID + (int)local_gemmini_id;
  spm_alias_region_t contig_a;
  spm_alias_region_t contig_b;
  spm_alias_region_t contig_c;
  spm_alias_region_t interleaved_a;
  spm_alias_region_t interleaved_b;
  spm_alias_region_t interleaved_c;
  spm_alias_region_t pw_contig_a;
  spm_alias_region_t pw_contig_b;
  spm_alias_region_t pw_contig_bias;
  spm_alias_region_t pw_contig_c;
  spm_alias_region_t pw_chunk_contig_bias;
  spm_alias_region_t pw_interleaved_a;
  spm_alias_region_t pw_interleaved_b;
  spm_alias_region_t pw_interleaved_bias;
  spm_alias_region_t pw_chunk_interleaved_bias;
  spm_alias_region_t pw_interleaved_c;
  spm_alias_region_t pw_diag_contig_a;
  spm_alias_region_t pw_diag_contig_b;
  spm_alias_region_t pw_diag_contig_bias;
  spm_alias_region_t pw_diag_contig_c;
  spm_alias_region_t pw_diag_interleaved_a;
  spm_alias_region_t pw_diag_interleaved_b;
  spm_alias_region_t pw_diag_interleaved_bias;
  spm_alias_region_t pw_diag_interleaved_c;
  uint32_t seed = 0x12345678u;
  bool case_copy_a_mvin;
  bool case_copy_a_interleaved_src_contig_dst_mvin;
  bool case_copy_a_contig_src_interleaved_dst_mvin;
  bool case_copy_b_mvin;
  bool case_copy_b_mvin2;
  bool case_copy_b_mvin2_clean;
  bool case_contig_explicit;
  bool case_interleaved_explicit;
  bool case_interleaved_explicit_serialized;
  bool case_contig_standard_ws;
  bool case_interleaved_standard_ws;
  bool case_pw_contig_os;
  bool case_pw_contig_ws;
  bool case_pw_contig_os_j128;
  bool case_pw_contig_os_j128_tjcap;
  bool case_pw_interleaved_os_j128;
  bool case_pw_interleaved_os_j128_tjcap;
  bool case_pw_interleaved_os;
  bool case_pw_interleaved_ws;
  bool case_pw_contig_os_chunked;
  bool case_pw_interleaved_os_chunked;
  bool case_pw_diag_contig_os_bias_only;
  bool case_pw_diag_interleaved_os_bias_only;

  printf("[resadd-explicit-baremetal] gemmini_mgr=%d local_gemmini=%lu num_gemmini=%d\n",
         gemmini_manager_id, (unsigned long)local_gemmini_id, REROCC_NUM_GEMMINI);

  init_random_elem((elem_t *)resadd_a_dram, RESADD_ELEM_COUNT, &seed);
  init_random_elem((elem_t *)resadd_b_dram, RESADD_ELEM_COUNT, &seed);
  resadd_reference((const elem_t *)resadd_a_dram,
                   (const elem_t *)resadd_b_dram,
                   (elem_t *)resadd_gold_dram,
                   RESADD_ELEM_COUNT);
  init_random_elem((elem_t *)pw_a_dram, PW_ELEM_COUNT_A, &seed);
  init_random_elem((elem_t *)pw_b_dram, PW_ELEM_COUNT_B, &seed);
  init_random_acc((acc_t *)pw_bias_dram, PW_J, &seed);
  init_random_acc((acc_t *)pw_chunk_bias_dram, PW_CHUNK_TILE_J, &seed);
  memset(pw_init_c_dram, 0xa5, sizeof(pw_init_c_dram));
  memcpy(pw_gold_dram, pw_init_c_dram, sizeof(pw_gold_dram));
  memcpy(pw_j128_gold_dram, pw_init_c_dram, sizeof(pw_j128_gold_dram));
  memcpy(pw_chunk_gold_dram, pw_init_c_dram, sizeof(pw_chunk_gold_dram));
  pointwise_matmul_reference((const elem_t *)pw_a_dram,
                             (const elem_t *)pw_b_dram,
                             (const acc_t *)pw_bias_dram,
                             (elem_t *)pw_gold_dram);
  pointwise_matmul_reference_dim_j((const elem_t *)pw_a_dram,
                                   (const elem_t *)pw_b_dram,
                                   (const acc_t *)pw_chunk_bias_dram,
                                   (const elem_t *)pw_init_c_dram,
                                   (elem_t *)pw_j128_gold_dram,
                                   PW_CHUNK_TILE_J);
  pointwise_matmul_chunked_reference((const elem_t *)pw_a_dram,
                                     (const elem_t *)pw_b_dram,
                                     (const acc_t *)pw_chunk_bias_dram,
                                     (const elem_t *)pw_init_c_dram,
                                     (elem_t *)pw_chunk_gold_dram);
  memset(pw_diag_a_dram, 0, sizeof(pw_diag_a_dram));
  memset(pw_diag_b_dram, 0, sizeof(pw_diag_b_dram));
  init_random_acc((acc_t *)pw_diag_bias_dram, PW_DIAG_J, &seed);
  memset(pw_diag_init_c_dram, 0xa5, sizeof(pw_diag_init_c_dram));
  pointwise_matmul_reference_shape((const elem_t *)pw_diag_a_dram,
                                   (const elem_t *)pw_diag_b_dram,
                                   (const acc_t *)pw_diag_bias_dram,
                                   (const elem_t *)pw_diag_init_c_dram,
                                   (elem_t *)pw_diag_gold_dram,
                                   PW_DIAG_I, PW_DIAG_J, PW_DIAG_K,
                                   PW_A_STRIDE, PW_B_STRIDE, PW_C_STRIDE);

  if (!alias_region_build_contiguous(&contig_a, CONTIG_A_VPAGE,
                                     RESADD_VADDR_PAGE_OFFSET, RESADD_BYTES,
                                     local_gemmini_id, CONTIG_A_LOCAL_PAGE) ||
      !alias_region_build_contiguous(&contig_b, CONTIG_B_VPAGE,
                                     RESADD_VADDR_PAGE_OFFSET, RESADD_BYTES,
                                     local_gemmini_id, CONTIG_B_LOCAL_PAGE) ||
      !alias_region_build_contiguous(&contig_c, CONTIG_C_VPAGE,
                                     RESADD_VADDR_PAGE_OFFSET, RESADD_BYTES,
                                     local_gemmini_id, CONTIG_C_LOCAL_PAGE) ||
      !alias_region_build_interleaved(&interleaved_a, INTERLEAVED_A_VPAGE,
                                      RESADD_VADDR_PAGE_OFFSET, RESADD_BYTES,
                                      INTERLEAVED_A_LOCAL_PAGE, local_gemmini_id) ||
      !alias_region_build_interleaved(&interleaved_b, INTERLEAVED_B_VPAGE,
                                      RESADD_VADDR_PAGE_OFFSET, RESADD_BYTES,
                                      INTERLEAVED_B_LOCAL_PAGE, local_gemmini_id + 1U) ||
      !alias_region_build_interleaved(&interleaved_c, INTERLEAVED_C_VPAGE,
                                      RESADD_VADDR_PAGE_OFFSET, RESADD_BYTES,
                                      INTERLEAVED_C_LOCAL_PAGE, local_gemmini_id + 2U) ||
      !alias_region_build_contiguous(&pw_contig_a, PW_A_VPAGE,
                                     PW_VADDR_PAGE_OFFSET, PW_BYTES_A,
                                     local_gemmini_id, PW_CONTIG_A_LOCAL_PAGE) ||
      !alias_region_build_contiguous(&pw_contig_b, PW_B_VPAGE,
                                     PW_VADDR_PAGE_OFFSET, PW_BYTES_B,
                                     local_gemmini_id, PW_CONTIG_B_LOCAL_PAGE) ||
      !alias_region_build_contiguous(&pw_contig_bias, PW_BIAS_VPAGE,
                                     PW_VADDR_PAGE_OFFSET, PW_BYTES_BIAS,
                                     local_gemmini_id, PW_CONTIG_BIAS_LOCAL_PAGE) ||
      !alias_region_build_contiguous(&pw_chunk_contig_bias, PW_CHUNK_BIAS_CONTIG_VPAGE,
                                     PW_VADDR_PAGE_OFFSET, PW_CHUNK_BYTES_BIAS,
                                     local_gemmini_id, PW_CHUNK_CONTIG_BIAS_LOCAL_PAGE) ||
      !alias_region_build_contiguous(&pw_contig_c, PW_C_VPAGE,
                                     PW_VADDR_PAGE_OFFSET, PW_BYTES_C,
                                     local_gemmini_id, PW_CONTIG_C_LOCAL_PAGE) ||
      !alias_region_build_interleaved(&pw_interleaved_a, PW_A_VPAGE,
                                      PW_VADDR_PAGE_OFFSET, PW_BYTES_A,
                                      PW_INTERLEAVED_A_LOCAL_PAGE, local_gemmini_id) ||
      !alias_region_build_interleaved(&pw_interleaved_b, PW_B_VPAGE,
                                      PW_VADDR_PAGE_OFFSET, PW_BYTES_B,
                                      PW_INTERLEAVED_B_LOCAL_PAGE, local_gemmini_id + 1U) ||
      !alias_region_build_interleaved(&pw_interleaved_bias, PW_BIAS_VPAGE,
                                      PW_VADDR_PAGE_OFFSET, PW_BYTES_BIAS,
                                      PW_INTERLEAVED_BIAS_LOCAL_PAGE, local_gemmini_id + 2U) ||
      !alias_region_build_interleaved(&pw_chunk_interleaved_bias, PW_CHUNK_BIAS_VPAGE,
                                      PW_VADDR_PAGE_OFFSET, PW_CHUNK_BYTES_BIAS,
                                      PW_CHUNK_INTERLEAVED_BIAS_LOCAL_PAGE, local_gemmini_id + 4U) ||
      !alias_region_build_interleaved(&pw_interleaved_c, PW_C_VPAGE,
                                      PW_VADDR_PAGE_OFFSET, PW_BYTES_C,
                                      PW_INTERLEAVED_C_LOCAL_PAGE, local_gemmini_id + 3U) ||
      !alias_region_build_contiguous(&pw_diag_contig_a, PW_DIAG_A_VPAGE,
                                     PW_VADDR_PAGE_OFFSET, PW_DIAG_BYTES_A,
                                     local_gemmini_id, PW_DIAG_CONTIG_A_LOCAL_PAGE) ||
      !alias_region_build_contiguous(&pw_diag_contig_b, PW_DIAG_B_VPAGE,
                                     PW_VADDR_PAGE_OFFSET, PW_DIAG_BYTES_B,
                                     local_gemmini_id, PW_DIAG_CONTIG_B_LOCAL_PAGE) ||
      !alias_region_build_contiguous(&pw_diag_contig_bias, PW_DIAG_BIAS_VPAGE,
                                     PW_VADDR_PAGE_OFFSET, PW_DIAG_BYTES_BIAS,
                                     local_gemmini_id, PW_DIAG_CONTIG_BIAS_LOCAL_PAGE) ||
      !alias_region_build_contiguous(&pw_diag_contig_c, PW_DIAG_C_VPAGE,
                                     PW_VADDR_PAGE_OFFSET, PW_DIAG_BYTES_C,
                                     local_gemmini_id, PW_DIAG_CONTIG_C_LOCAL_PAGE) ||
      !alias_region_build_interleaved(&pw_diag_interleaved_a, PW_DIAG_A_VPAGE,
                                      PW_VADDR_PAGE_OFFSET, PW_DIAG_BYTES_A,
                                      PW_DIAG_INTERLEAVED_A_LOCAL_PAGE, local_gemmini_id) ||
      !alias_region_build_interleaved(&pw_diag_interleaved_b, PW_DIAG_B_VPAGE,
                                      PW_VADDR_PAGE_OFFSET, PW_DIAG_BYTES_B,
                                      PW_DIAG_INTERLEAVED_B_LOCAL_PAGE, local_gemmini_id + 1U) ||
      !alias_region_build_interleaved(&pw_diag_interleaved_bias, PW_DIAG_BIAS_VPAGE,
                                      PW_VADDR_PAGE_OFFSET, PW_DIAG_BYTES_BIAS,
                                      PW_DIAG_INTERLEAVED_BIAS_LOCAL_PAGE, local_gemmini_id + 2U) ||
      !alias_region_build_interleaved(&pw_diag_interleaved_c, PW_DIAG_C_VPAGE,
                                      PW_VADDR_PAGE_OFFSET, PW_DIAG_BYTES_C,
                                      PW_DIAG_INTERLEAVED_C_LOCAL_PAGE, local_gemmini_id + 3U)) {
    printf("ALL_TESTS_FAIL reason=region_build\n");
    exit(1);
  }

  {
    const char *region_names[] = {
      "contig_a", "contig_b", "contig_c",
      "interleaved_a", "interleaved_b", "interleaved_c",
      "pw_contig_a", "pw_contig_b", "pw_contig_bias",
      "pw_chunk_contig_bias", "pw_contig_c",
      "pw_interleaved_a", "pw_interleaved_b", "pw_interleaved_bias",
      "pw_chunk_interleaved_bias", "pw_interleaved_c",
      "pw_diag_contig_a", "pw_diag_contig_b", "pw_diag_contig_bias", "pw_diag_contig_c",
      "pw_diag_interleaved_a", "pw_diag_interleaved_b",
      "pw_diag_interleaved_bias", "pw_diag_interleaved_c",
    };
    const spm_alias_region_t *regions[] = {
      &contig_a, &contig_b, &contig_c,
      &interleaved_a, &interleaved_b, &interleaved_c,
      &pw_contig_a, &pw_contig_b, &pw_contig_bias,
      &pw_chunk_contig_bias, &pw_contig_c,
      &pw_interleaved_a, &pw_interleaved_b, &pw_interleaved_bias,
      &pw_chunk_interleaved_bias, &pw_interleaved_c,
      &pw_diag_contig_a, &pw_diag_contig_b, &pw_diag_contig_bias, &pw_diag_contig_c,
      &pw_diag_interleaved_a, &pw_diag_interleaved_b,
      &pw_diag_interleaved_bias, &pw_diag_interleaved_c,
    };
    if (!validate_alias_layout(region_names, regions,
                               sizeof(regions) / sizeof(regions[0]))) {
      exit(1);
    }
  }

  case_contig_explicit = run_resadd_case("resadd_explicit_cross_1kb_contiguous",
                                         gemmini_manager_id,
                                         &contig_a, &contig_b, &contig_c,
                                         RESADD_MODE_EXPLICIT_NO_FENCE);
  case_contig_standard_ws = run_resadd_case("resadd_standard_ws_cross_1kb_contiguous",
                                            gemmini_manager_id,
                                            &contig_a, &contig_b, &contig_c,
                                            RESADD_MODE_STANDARD_WS);
  case_copy_a_mvin = run_copy_case("copy_explicit_cross_1kb_interleaved_a_mvin",
                                   gemmini_manager_id,
                                   &interleaved_a, &interleaved_c,
                                   (const elem_t *)resadd_a_dram,
                                   false, 0U);
  case_copy_a_interleaved_src_contig_dst_mvin =
      run_copy_case("copy_explicit_cross_1kb_interleaved_src_contig_dst_a_mvin",
                    gemmini_manager_id,
                    &interleaved_a, &contig_c,
                    (const elem_t *)resadd_a_dram,
                    false, 0U);
  case_copy_a_contig_src_interleaved_dst_mvin =
      run_copy_case("copy_explicit_cross_1kb_contig_src_interleaved_dst_a_mvin",
                    gemmini_manager_id,
                    &contig_a, &interleaved_c,
                    (const elem_t *)resadd_a_dram,
                    false, 0U);
  case_copy_b_mvin = run_copy_case("copy_explicit_cross_1kb_interleaved_b_mvin",
                                   gemmini_manager_id,
                                   &interleaved_b, &interleaved_c,
                                   (const elem_t *)resadd_b_dram,
                                   false, 0U);
  case_copy_b_mvin2 = run_copy_case("copy_explicit_cross_1kb_interleaved_b_mvin2",
                                    gemmini_manager_id,
                                    &interleaved_b, &interleaved_c,
                                    (const elem_t *)resadd_b_dram,
                                    true, 0U);
  case_copy_b_mvin2_clean = run_copy_case("copy_explicit_cross_1kb_interleaved_b_mvin2_clean",
                                          gemmini_manager_id,
                                          &interleaved_b, &interleaved_c,
                                          (const elem_t *)resadd_b_dram,
                                          true, CLEAN_ACC_ROW_OFFSET);
  case_interleaved_explicit = run_resadd_case("resadd_explicit_cross_1kb_interleaved",
                                              gemmini_manager_id,
                                              &interleaved_a, &interleaved_b, &interleaved_c,
                                              RESADD_MODE_EXPLICIT_NO_FENCE);
  case_interleaved_explicit_serialized =
      run_resadd_case("resadd_explicit_serialized_cross_1kb_interleaved",
                      gemmini_manager_id,
                      &interleaved_a, &interleaved_b, &interleaved_c,
                      RESADD_MODE_EXPLICIT_SERIALIZED);
  case_interleaved_standard_ws =
      run_resadd_case("resadd_standard_ws_cross_1kb_interleaved",
                      gemmini_manager_id,
                      &interleaved_a, &interleaved_b, &interleaved_c,
                      RESADD_MODE_STANDARD_WS);
  case_pw_diag_contig_os_bias_only =
      run_pointwise_matmul_case_shape("pointwise_matmul_os_biasonly_smallik8_128_cross_1kb_contiguous",
                                      gemmini_manager_id,
                                      &pw_diag_contig_a, &pw_diag_contig_b,
                                      &pw_diag_contig_bias, &pw_diag_contig_c,
                                      (const elem_t *)pw_diag_a_dram, PW_DIAG_BYTES_A,
                                      (const elem_t *)pw_diag_b_dram, PW_DIAG_BYTES_B,
                                      (const acc_t *)pw_diag_bias_dram, PW_DIAG_BYTES_BIAS,
                                      (const elem_t *)pw_diag_init_c_dram, PW_DIAG_BYTES_C,
                                      (const elem_t *)pw_diag_gold_dram, PW_DIAG_ELEM_COUNT_C,
                                      PW_DIAG_I, PW_DIAG_J, PW_DIAG_K,
                                      PW_A_STRIDE, PW_B_STRIDE, PW_C_STRIDE,
                                      OS);
  case_pw_diag_interleaved_os_bias_only =
      run_pointwise_matmul_case_shape("pointwise_matmul_os_biasonly_smallik8_128_cross_1kb_interleaved",
                                      gemmini_manager_id,
                                      &pw_diag_interleaved_a, &pw_diag_interleaved_b,
                                      &pw_diag_interleaved_bias, &pw_diag_interleaved_c,
                                      (const elem_t *)pw_diag_a_dram, PW_DIAG_BYTES_A,
                                      (const elem_t *)pw_diag_b_dram, PW_DIAG_BYTES_B,
                                      (const acc_t *)pw_diag_bias_dram, PW_DIAG_BYTES_BIAS,
                                      (const elem_t *)pw_diag_init_c_dram, PW_DIAG_BYTES_C,
                                      (const elem_t *)pw_diag_gold_dram, PW_DIAG_ELEM_COUNT_C,
                                      PW_DIAG_I, PW_DIAG_J, PW_DIAG_K,
                                      PW_A_STRIDE, PW_B_STRIDE, PW_C_STRIDE,
                                      OS);
  case_pw_contig_os =
      run_pointwise_matmul_case("pointwise_matmul_os_cross_1kb_contiguous",
                                gemmini_manager_id,
                                &pw_contig_a, &pw_contig_b, &pw_contig_bias, &pw_contig_c,
                                OS);
  case_pw_contig_ws =
      run_pointwise_matmul_case("pointwise_matmul_ws_cross_1kb_contiguous",
                                gemmini_manager_id,
                                &pw_contig_a, &pw_contig_b, &pw_contig_bias, &pw_contig_c,
                                WS);
  case_pw_interleaved_os =
      run_pointwise_matmul_case("pointwise_matmul_os_cross_1kb_interleaved",
                                gemmini_manager_id,
                                &pw_interleaved_a, &pw_interleaved_b,
                                &pw_interleaved_bias, &pw_interleaved_c,
                                OS);
  case_pw_contig_os_j128 =
      run_pointwise_matmul_case_dim_j("pointwise_matmul_os_128_cross_1kb_contiguous",
                                      gemmini_manager_id,
                                      &pw_contig_a, &pw_contig_b,
                                      &pw_chunk_contig_bias, &pw_contig_c,
                                      (const acc_t *)pw_chunk_bias_dram, PW_CHUNK_BYTES_BIAS,
                                      (const elem_t *)pw_j128_gold_dram, PW_CHUNK_TILE_J,
                                      OS);
  case_pw_interleaved_os_j128 =
      run_pointwise_matmul_case_dim_j("pointwise_matmul_os_128_cross_1kb_interleaved",
                                      gemmini_manager_id,
                                      &pw_interleaved_a, &pw_interleaved_b,
                                      &pw_chunk_interleaved_bias, &pw_interleaved_c,
                                      (const acc_t *)pw_chunk_bias_dram, PW_CHUNK_BYTES_BIAS,
                                      (const elem_t *)pw_j128_gold_dram, PW_CHUNK_TILE_J,
                                      OS);
  case_pw_contig_os_j128_tjcap =
      run_pointwise_matmul_case_dim_j_explicit_tiles(
          "pointwise_matmul_os_128_tjcap_cross_1kb_contiguous",
          gemmini_manager_id,
          &pw_contig_a, &pw_contig_b,
          &pw_chunk_contig_bias, &pw_contig_c,
          (const acc_t *)pw_chunk_bias_dram, PW_CHUNK_BYTES_BIAS,
          (const elem_t *)pw_j128_gold_dram, PW_CHUNK_TILE_J,
          PW_OS_SAFE_TILE_I, PW_OS_SAFE_TILE_J, PW_OS_SAFE_TILE_K,
          OS);
  case_pw_interleaved_os_j128_tjcap =
      run_pointwise_matmul_case_dim_j_explicit_tiles(
          "pointwise_matmul_os_128_tjcap_cross_1kb_interleaved",
          gemmini_manager_id,
          &pw_interleaved_a, &pw_interleaved_b,
          &pw_chunk_interleaved_bias, &pw_interleaved_c,
          (const acc_t *)pw_chunk_bias_dram, PW_CHUNK_BYTES_BIAS,
          (const elem_t *)pw_j128_gold_dram, PW_CHUNK_TILE_J,
          PW_OS_SAFE_TILE_I, PW_OS_SAFE_TILE_J, PW_OS_SAFE_TILE_K,
          OS);
  case_pw_interleaved_ws =
      run_pointwise_matmul_case("pointwise_matmul_ws_cross_1kb_interleaved",
                                gemmini_manager_id,
                                &pw_interleaved_a, &pw_interleaved_b,
                                &pw_interleaved_bias, &pw_interleaved_c,
                                WS);
  case_pw_contig_os_chunked =
      run_pointwise_matmul_chunked_case("pointwise_matmul_os_chunked_128_to_64_cross_1kb_contiguous",
                                        gemmini_manager_id,
                                        &pw_contig_a, &pw_contig_b,
                                        &pw_chunk_contig_bias, &pw_contig_c,
                                        OS);
  case_pw_interleaved_os_chunked =
      run_pointwise_matmul_chunked_case("pointwise_matmul_os_chunked_128_to_64_cross_1kb_interleaved",
                                        gemmini_manager_id,
                                        &pw_interleaved_a, &pw_interleaved_b,
                                        &pw_chunk_interleaved_bias, &pw_interleaved_c,
                                        OS);

  rr_release_all(RR_MAX_CFGS);

  if (case_contig_explicit && case_contig_standard_ws &&
      case_copy_a_mvin &&
      case_copy_a_interleaved_src_contig_dst_mvin &&
      case_copy_a_contig_src_interleaved_dst_mvin &&
      case_copy_b_mvin &&
      case_copy_b_mvin2 &&
      case_copy_b_mvin2_clean &&
      case_interleaved_explicit &&
      case_interleaved_explicit_serialized &&
      case_interleaved_standard_ws &&
      case_pw_diag_contig_os_bias_only &&
      case_pw_diag_interleaved_os_bias_only &&
      case_pw_contig_os &&
      case_pw_contig_ws &&
      case_pw_contig_os_j128 &&
      case_pw_contig_os_j128_tjcap &&
      case_pw_interleaved_os &&
      case_pw_interleaved_os_j128 &&
      case_pw_interleaved_os_j128_tjcap &&
      case_pw_interleaved_ws &&
      case_pw_contig_os_chunked &&
      case_pw_interleaved_os_chunked) {
    printf("ALL_TESTS_PASS\n");
    exit(0);
  }

  printf("ALL_TESTS_FAIL\n");
  exit(1);
}

int main(void) {
  return 1;
}
