#include "prt_breadcrumb.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#if defined(__linux__)
#include <sys/mman.h>
#endif

#include "include/prt_log_gate.h"
#include "prt_error.h"
#include "prt_runtime.h"

typedef struct {
  uint32_t initialized;
  uint32_t enabled;
  uint32_t filter_segment;
  uint32_t filter_global_stage;
  uint32_t filter_local_stage;
  uint32_t filter_subbatch;
  uint32_t filter_stage_radius;
  uint32_t filter_subbatch_radius;
  int fd;
  size_t mapped_bytes;
  prt_breadcrumb_file_t *mapped;
} prt_breadcrumb_state_t;

typedef struct {
  uint32_t valid;
  uint32_t tensor_id;
  uint32_t manager_id;
  uint32_t page_idx;
  uint64_t src_addr;
  uint64_t dst_addr;
  uint64_t bytes;
} prt_breadcrumb_dma_transfer_ctx_t;

static prt_breadcrumb_state_t g_prt_breadcrumb_state = {0, 0, PRT_BREADCRUMB_ANY_U32,
                                                        PRT_BREADCRUMB_ANY_U32,
                                                        PRT_BREADCRUMB_ANY_U32,
                                                        PRT_BREADCRUMB_ANY_U32,
                                                        0U, 0U, -1, 0U, NULL};
static __thread prt_breadcrumb_dma_transfer_ctx_t t_prt_breadcrumb_dma_ctx = {0, 0, 0, 0, 0, 0, 0};
static __thread uint32_t t_prt_breadcrumb_export_target_token = 0U;

static int prt_breadcrumb_env_flag(const char *name, int default_value) {
  const char *value = getenv(name);
  if (!value || !*value) return default_value;
  switch (value[0]) {
    case '0':
    case 'n':
    case 'N':
    case 'f':
    case 'F':
      return 0;
    default:
      return 1;
  }
}

static uint32_t prt_breadcrumb_env_u32_any(const char *name, uint32_t default_value) {
  const char *value = getenv(name);
  char *endptr = NULL;
  unsigned long parsed = 0UL;
  if (!value || !*value) return default_value;
  if (!strcmp(value, "any") || !strcmp(value, "ANY")) return PRT_BREADCRUMB_ANY_U32;
  parsed = strtoul(value, &endptr, 0);
  if (!endptr || *endptr != '\0' || parsed > UINT32_MAX) return default_value;
  return (uint32_t)parsed;
}

static int prt_breadcrumb_match_u32(uint32_t want, uint32_t have, uint32_t radius) {
  uint64_t lo;
  uint64_t hi;
  if (want == PRT_BREADCRUMB_ANY_U32) return 1;
  if (have == PRT_BREADCRUMB_ANY_U32) return 0;
  lo = want > radius ? (uint64_t)(want - radius) : 0ULL;
  hi = (uint64_t)want + (uint64_t)radius;
  return (uint64_t)have >= lo && (uint64_t)have <= hi;
}

static int prt_breadcrumb_context_allowed(const prt_log_gate_ctx_snapshot_t *ctx) {
  if (g_prt_breadcrumb_state.enabled == 0U) return 0;
  return prt_breadcrumb_match_u32(g_prt_breadcrumb_state.filter_segment,
                                  ctx ? ctx->segment_idx : PRT_BREADCRUMB_ANY_U32, 0U) &&
         prt_breadcrumb_match_u32(g_prt_breadcrumb_state.filter_global_stage,
                                  ctx ? ctx->global_stage_id : PRT_BREADCRUMB_ANY_U32,
                                  g_prt_breadcrumb_state.filter_stage_radius) &&
         prt_breadcrumb_match_u32(g_prt_breadcrumb_state.filter_local_stage,
                                  ctx ? ctx->local_stage_id : PRT_BREADCRUMB_ANY_U32,
                                  g_prt_breadcrumb_state.filter_stage_radius) &&
         prt_breadcrumb_match_u32(g_prt_breadcrumb_state.filter_subbatch,
                                  ctx ? ctx->subbatch_id : PRT_BREADCRUMB_ANY_U32,
                                  g_prt_breadcrumb_state.filter_subbatch_radius);
}

static uint32_t prt_breadcrumb_mix_u32(uint32_t acc, uint32_t value) {
  // Use a lightweight stable hash so concurrent stage-0 probes do not all fight
  // over one slot. This keeps breadcrumb low-disturbance while recovering
  // manager/subbatch locality.
  acc ^= value + 0x9e3779b9U + (acc << 6) + (acc >> 2);
  return acc;
}

static uint32_t prt_breadcrumb_slot_for_note(const prt_log_gate_ctx_snapshot_t *ctx,
                                             prt_breadcrumb_kind_t kind,
                                             uint32_t tensor_id,
                                             uint32_t token_id,
                                             uint32_t manager_id,
                                             uint32_t page_idx) {
  uint32_t key = 2166136261U;
  const int have_ctx = ctx && ctx->valid;
  const uint32_t segment_idx =
    have_ctx ? ctx->segment_idx : PRT_BREADCRUMB_ANY_U32;
  const uint32_t global_stage_id =
    have_ctx ? ctx->global_stage_id : PRT_BREADCRUMB_ANY_U32;
  const uint32_t local_stage_id =
    have_ctx ? ctx->local_stage_id : PRT_BREADCRUMB_ANY_U32;
  const uint32_t subbatch_id =
    have_ctx ? ctx->subbatch_id : PRT_BREADCRUMB_ANY_U32;

  if (!have_ctx &&
      tensor_id == PRT_BREADCRUMB_ANY_U32 &&
      manager_id == PRT_BREADCRUMB_ANY_U32 &&
      page_idx == PRT_BREADCRUMB_ANY_U32 &&
      token_id == 0U) {
    return 0U;
  }

  key = prt_breadcrumb_mix_u32(key, (uint32_t)kind);
  key = prt_breadcrumb_mix_u32(key, segment_idx);
  key = prt_breadcrumb_mix_u32(key, global_stage_id);
  key = prt_breadcrumb_mix_u32(key, local_stage_id);
  key = prt_breadcrumb_mix_u32(key, subbatch_id);
  key = prt_breadcrumb_mix_u32(key, tensor_id);
  key = prt_breadcrumb_mix_u32(key, token_id);
  key = prt_breadcrumb_mix_u32(key, manager_id);
  key = prt_breadcrumb_mix_u32(key, page_idx);
  return 1U + (key % (PRT_BREADCRUMB_SLOT_COUNT - 1U));
}

static uint32_t prt_breadcrumb_resolve_page(uint32_t page_idx) {
  if (page_idx != PRT_BREADCRUMB_ANY_U32) return page_idx;
  if (t_prt_breadcrumb_dma_ctx.valid) return t_prt_breadcrumb_dma_ctx.page_idx;
  return PRT_BREADCRUMB_ANY_U32;
}

static uint32_t prt_breadcrumb_resolve_tensor(uint32_t tensor_id) {
  if (tensor_id != PRT_BREADCRUMB_ANY_U32) return tensor_id;
  if (t_prt_breadcrumb_dma_ctx.valid) return t_prt_breadcrumb_dma_ctx.tensor_id;
  return PRT_BREADCRUMB_ANY_U32;
}

static uint32_t prt_breadcrumb_resolve_manager(uint32_t manager_id) {
  if (manager_id != PRT_BREADCRUMB_ANY_U32) return manager_id;
  if (t_prt_breadcrumb_dma_ctx.valid) return t_prt_breadcrumb_dma_ctx.manager_id;
  return PRT_BREADCRUMB_ANY_U32;
}

static uint64_t prt_breadcrumb_resolve_src(uint64_t src_addr) {
  if (src_addr != 0ULL) return src_addr;
  if (t_prt_breadcrumb_dma_ctx.valid) return t_prt_breadcrumb_dma_ctx.src_addr;
  return 0ULL;
}

static uint64_t prt_breadcrumb_resolve_dst(uint64_t dst_addr) {
  if (dst_addr != 0ULL) return dst_addr;
  if (t_prt_breadcrumb_dma_ctx.valid) return t_prt_breadcrumb_dma_ctx.dst_addr;
  return 0ULL;
}

int prt_breadcrumb_init(void) {
  const char *path = NULL;
  size_t mapped_bytes = sizeof(prt_breadcrumb_file_t);
  int fd = -1;

  if (g_prt_breadcrumb_state.initialized) return PRT_OK;
  g_prt_breadcrumb_state.initialized = 1U;
  g_prt_breadcrumb_state.enabled = 0U;
  g_prt_breadcrumb_state.fd = -1;

  if (!prt_breadcrumb_env_flag("PIPELINE_RUNTIME_BREADCRUMB_ENABLE", 0)) return PRT_OK;
  path = getenv("PIPELINE_RUNTIME_BREADCRUMB_PATH");
  if (!path || !*path) path = "/root/pipeline-runtime-debug/bertmini-batch8.breadcrumb.bin";

#if defined(__linux__)
  fd = open(path, O_RDWR | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
  if (fd < 0) return PRT_ERR_IO;
  if (ftruncate(fd, (off_t)mapped_bytes) != 0) {
    close(fd);
    return PRT_ERR_IO;
  }
  g_prt_breadcrumb_state.mapped =
    (prt_breadcrumb_file_t *)mmap(NULL, mapped_bytes, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
  if (g_prt_breadcrumb_state.mapped == MAP_FAILED) {
    g_prt_breadcrumb_state.mapped = NULL;
    close(fd);
    return PRT_ERR_IO;
  }
  memset(g_prt_breadcrumb_state.mapped, 0, mapped_bytes);
  g_prt_breadcrumb_state.filter_segment =
    prt_breadcrumb_env_u32_any("PIPELINE_RUNTIME_BREADCRUMB_SEGMENT", PRT_BREADCRUMB_ANY_U32);
  g_prt_breadcrumb_state.filter_global_stage =
    prt_breadcrumb_env_u32_any("PIPELINE_RUNTIME_BREADCRUMB_GLOBAL_STAGE", PRT_BREADCRUMB_ANY_U32);
  g_prt_breadcrumb_state.filter_local_stage =
    prt_breadcrumb_env_u32_any("PIPELINE_RUNTIME_BREADCRUMB_LOCAL_STAGE", PRT_BREADCRUMB_ANY_U32);
  g_prt_breadcrumb_state.filter_subbatch =
    prt_breadcrumb_env_u32_any("PIPELINE_RUNTIME_BREADCRUMB_SUBBATCH", PRT_BREADCRUMB_ANY_U32);
  g_prt_breadcrumb_state.filter_stage_radius =
    prt_breadcrumb_env_u32_any("PIPELINE_RUNTIME_BREADCRUMB_STAGE_RADIUS", 0U);
  g_prt_breadcrumb_state.filter_subbatch_radius =
    prt_breadcrumb_env_u32_any("PIPELINE_RUNTIME_BREADCRUMB_SUBBATCH_RADIUS", 0U);

  g_prt_breadcrumb_state.mapped->magic = PRT_BREADCRUMB_MAGIC;
  g_prt_breadcrumb_state.mapped->version = PRT_BREADCRUMB_VERSION;
  g_prt_breadcrumb_state.mapped->header_bytes = (uint32_t)sizeof(prt_breadcrumb_file_t);
  g_prt_breadcrumb_state.mapped->slot_count = PRT_BREADCRUMB_SLOT_COUNT;
  g_prt_breadcrumb_state.mapped->enabled = 1U;
  g_prt_breadcrumb_state.mapped->filter_segment = g_prt_breadcrumb_state.filter_segment;
  g_prt_breadcrumb_state.mapped->filter_global_stage = g_prt_breadcrumb_state.filter_global_stage;
  g_prt_breadcrumb_state.mapped->filter_local_stage = g_prt_breadcrumb_state.filter_local_stage;
  g_prt_breadcrumb_state.mapped->filter_subbatch = g_prt_breadcrumb_state.filter_subbatch;
  g_prt_breadcrumb_state.mapped->filter_stage_radius = g_prt_breadcrumb_state.filter_stage_radius;
  g_prt_breadcrumb_state.mapped->filter_subbatch_radius = g_prt_breadcrumb_state.filter_subbatch_radius;
  g_prt_breadcrumb_state.mapped->last_update_ns = prt_now_ns();
  (void)msync(g_prt_breadcrumb_state.mapped, mapped_bytes, MS_SYNC);

  g_prt_breadcrumb_state.fd = fd;
  g_prt_breadcrumb_state.mapped_bytes = mapped_bytes;
  g_prt_breadcrumb_state.enabled = 1U;
  return PRT_OK;
#else
  (void)path;
  (void)mapped_bytes;
  (void)fd;
  return PRT_OK;
#endif
}

void prt_breadcrumb_destroy(void) {
#if defined(__linux__)
  if (g_prt_breadcrumb_state.mapped) {
    (void)msync(g_prt_breadcrumb_state.mapped, g_prt_breadcrumb_state.mapped_bytes, MS_SYNC);
    (void)munmap(g_prt_breadcrumb_state.mapped, g_prt_breadcrumb_state.mapped_bytes);
  }
#endif
  if (g_prt_breadcrumb_state.fd >= 0) (void)close(g_prt_breadcrumb_state.fd);
  memset(&g_prt_breadcrumb_state, 0, sizeof(g_prt_breadcrumb_state));
  g_prt_breadcrumb_state.fd = -1;
  memset(&t_prt_breadcrumb_dma_ctx, 0, sizeof(t_prt_breadcrumb_dma_ctx));
  t_prt_breadcrumb_export_target_token = 0U;
}

int prt_breadcrumb_enabled(void) {
  return g_prt_breadcrumb_state.enabled != 0U && g_prt_breadcrumb_state.mapped != NULL;
}

void prt_breadcrumb_set_dma_transfer_context(uint32_t tensor_id,
                                             uint32_t manager_id,
                                             uint32_t page_idx,
                                             uint64_t src_addr,
                                             uint64_t dst_addr,
                                             uint64_t bytes) {
  t_prt_breadcrumb_dma_ctx.valid = 1U;
  t_prt_breadcrumb_dma_ctx.tensor_id = tensor_id;
  t_prt_breadcrumb_dma_ctx.manager_id = manager_id;
  t_prt_breadcrumb_dma_ctx.page_idx = page_idx;
  t_prt_breadcrumb_dma_ctx.src_addr = src_addr;
  t_prt_breadcrumb_dma_ctx.dst_addr = dst_addr;
  t_prt_breadcrumb_dma_ctx.bytes = bytes;
}

void prt_breadcrumb_clear_dma_transfer_context(void) {
  memset(&t_prt_breadcrumb_dma_ctx, 0, sizeof(t_prt_breadcrumb_dma_ctx));
}

void prt_breadcrumb_set_export_target_token(uint32_t token_id) {
  t_prt_breadcrumb_export_target_token = token_id;
}

void prt_breadcrumb_clear_export_target_token(void) {
  t_prt_breadcrumb_export_target_token = 0U;
}

uint32_t prt_breadcrumb_get_export_target_token(void) {
  return t_prt_breadcrumb_export_target_token;
}

void prt_breadcrumb_note(prt_breadcrumb_kind_t kind,
                         uint32_t phase,
                         uint32_t tensor_id,
                         uint32_t token_id,
                         uint32_t manager_id,
                         uint32_t page_idx,
                         int rc,
                         uint32_t flags,
                         uint64_t src_addr,
                         uint64_t dst_addr,
                         uint64_t aux_u64_0,
                         uint64_t aux_u64_1,
                         uint32_t line) {
  prt_log_gate_ctx_snapshot_t ctx;
  prt_breadcrumb_slot_t *slot = NULL;
  uint32_t slot_idx = 0U;
  uint32_t resolved_tensor_id = PRT_BREADCRUMB_ANY_U32;
  uint32_t resolved_manager_id = PRT_BREADCRUMB_ANY_U32;
  uint32_t resolved_page_idx = PRT_BREADCRUMB_ANY_U32;
  uint64_t resolved_src_addr = 0ULL;
  uint64_t resolved_dst_addr = 0ULL;
  uint64_t now_ns = 0ULL;

  if (!prt_breadcrumb_enabled()) return;

  prt_log_gate_get_context(&ctx);
  if (!prt_breadcrumb_context_allowed(&ctx)) return;

  resolved_tensor_id = prt_breadcrumb_resolve_tensor(tensor_id);
  resolved_manager_id = prt_breadcrumb_resolve_manager(manager_id);
  resolved_page_idx = prt_breadcrumb_resolve_page(page_idx);
  resolved_src_addr = prt_breadcrumb_resolve_src(src_addr);
  resolved_dst_addr = prt_breadcrumb_resolve_dst(dst_addr);

  slot_idx = prt_breadcrumb_slot_for_note(&ctx, kind,
                                          resolved_tensor_id, token_id,
                                          resolved_manager_id, resolved_page_idx);
  if (slot_idx >= PRT_BREADCRUMB_SLOT_COUNT) slot_idx = 0U;
  slot = &g_prt_breadcrumb_state.mapped->slots[slot_idx];
  now_ns = prt_now_ns();

  (void)__sync_add_and_fetch(&slot->seq, 1ULL);
  __sync_synchronize();
  slot->mono_ns = now_ns;
  slot->src_addr = resolved_src_addr;
  slot->dst_addr = resolved_dst_addr;
  slot->aux_u64_0 = aux_u64_0;
  slot->aux_u64_1 = aux_u64_1;
  slot->kind = (uint32_t)kind;
  slot->phase = phase;
  slot->flags = flags;
  slot->rc = rc;
  slot->line = line;
  slot->slot_idx = slot_idx;
  slot->segment_idx = ctx.valid ? ctx.segment_idx : PRT_BREADCRUMB_ANY_U32;
  slot->global_stage_id = ctx.valid ? ctx.global_stage_id : PRT_BREADCRUMB_ANY_U32;
  slot->local_stage_id = ctx.valid ? ctx.local_stage_id : PRT_BREADCRUMB_ANY_U32;
  slot->subbatch_id = ctx.valid ? ctx.subbatch_id : PRT_BREADCRUMB_ANY_U32;
  slot->tensor_id = resolved_tensor_id;
  slot->token_id = token_id;
  slot->manager_id = resolved_manager_id;
  slot->page_idx = resolved_page_idx;
  __sync_synchronize();
  (void)__sync_add_and_fetch(&slot->seq, 1ULL);

  g_prt_breadcrumb_state.mapped->last_update_ns = now_ns;
  g_prt_breadcrumb_state.mapped->last_slot_idx = slot_idx;
  g_prt_breadcrumb_state.mapped->last_kind = (uint32_t)kind;
  g_prt_breadcrumb_state.mapped->last_phase = phase;
  (void)__sync_add_and_fetch(&g_prt_breadcrumb_state.mapped->update_count, 1U);
}
