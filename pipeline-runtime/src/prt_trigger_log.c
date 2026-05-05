#include "prt_trigger_log.h"

#if !defined(BAREMETAL)

#include "prt_progress.h"

#include <fcntl.h>
#include <pthread.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define PRT_TRIGGER_LOG_MAX_LINE 256U
#define PRT_TRIGGER_LOG_MAX_PRE_RING 64U

typedef struct {
  int initialized;
  int enabled;
  int fd;
  prt_trigger_log_family_t family;
  uint32_t segment_idx;
  uint32_t global_stage_id;
  uint32_t local_stage_id;
  uint32_t subbatch_id;
  uint32_t manager_id;
  uint32_t tensor_id;
  uint32_t page_idx;
  uint32_t token_id;
  uint32_t pre_ring;
  uint32_t post_budget;
  int match_once;
  int matched_once;
  int active;
  uint32_t budget_remaining;
  unsigned long long seq;
  char path[256];
  pthread_mutex_t lock;
} prt_trigger_log_state_t;

typedef struct {
  char lines[PRT_TRIGGER_LOG_MAX_PRE_RING][PRT_TRIGGER_LOG_MAX_LINE];
  uint16_t lens[PRT_TRIGGER_LOG_MAX_PRE_RING];
  uint32_t count;
  uint32_t next;
} prt_trigger_log_ring_t;

static prt_trigger_log_state_t g_prt_trigger_log_state = {
  .initialized = 0,
  .enabled = 0,
  .fd = -1,
  .family = PRT_TRIGGER_LOG_FAMILY_ANY,
  .segment_idx = PRT_TRIGGER_LOG_ANY_U32,
  .global_stage_id = PRT_TRIGGER_LOG_ANY_U32,
  .local_stage_id = PRT_TRIGGER_LOG_ANY_U32,
  .subbatch_id = PRT_TRIGGER_LOG_ANY_U32,
  .manager_id = PRT_TRIGGER_LOG_ANY_U32,
  .tensor_id = PRT_TRIGGER_LOG_ANY_U32,
  .page_idx = PRT_TRIGGER_LOG_ANY_U32,
  .token_id = PRT_TRIGGER_LOG_ANY_U32,
  .pre_ring = 8U,
  .post_budget = 32U,
  .match_once = 1,
  .matched_once = 0,
  .active = 0,
  .budget_remaining = 0U,
  .seq = 0ULL,
  .path = "/root/pipeline-runtime-debug/bertmini-batch8.trigger.log",
  .lock = PTHREAD_MUTEX_INITIALIZER,
};

static __thread prt_trigger_log_ring_t t_prt_trigger_log_ring = {{{0}}, {0}, 0U, 0U};

static uint32_t prt_trigger_log_env_u32_any(const char *name, uint32_t default_value) {
  const char *value = getenv(name);
  if (!value || !*value) return default_value;
  if (!strcmp(value, "any") || !strcmp(value, "*")) return PRT_TRIGGER_LOG_ANY_U32;
  return (uint32_t)strtoul(value, NULL, 0);
}

static uint32_t prt_trigger_log_env_u32(const char *name, uint32_t default_value) {
  const char *value = getenv(name);
  if (!value || !*value) return default_value;
  return (uint32_t)strtoul(value, NULL, 0);
}

static prt_trigger_log_family_t prt_trigger_log_family_from_env(const char *value) {
  if (!value || !*value) return PRT_TRIGGER_LOG_FAMILY_ANY;
  if (!strcmp(value, "any") || !strcmp(value, "*")) return PRT_TRIGGER_LOG_FAMILY_ANY;
  if (!strcmp(value, "runtime") || !strcmp(value, "rt")) return PRT_TRIGGER_LOG_FAMILY_RUNTIME;
  if (!strcmp(value, "dma-fixed-load") || !strcmp(value, "dma-fixed") || !strcmp(value, "dfix")) {
    return PRT_TRIGGER_LOG_FAMILY_DMA_FIXED_LOAD;
  }
  if (!strcmp(value, "dma-export") || !strcmp(value, "dexp")) return PRT_TRIGGER_LOG_FAMILY_DMA_EXPORT;
  if (!strcmp(value, "rr")) return PRT_TRIGGER_LOG_FAMILY_RR;
  if (!strcmp(value, "spm-xlate") || !strcmp(value, "spm") || !strcmp(value, "sx")) {
    return PRT_TRIGGER_LOG_FAMILY_SPM_XLATE;
  }
  if (!strcmp(value, "gemmini-pointwise") || !strcmp(value, "pointwise") || !strcmp(value, "gpw")) {
    return PRT_TRIGGER_LOG_FAMILY_GEMMINI_POINTWISE;
  }
  return PRT_TRIGGER_LOG_FAMILY_ANY;
}

static const char *prt_trigger_log_family_code(prt_trigger_log_family_t family) {
  switch (family) {
    case PRT_TRIGGER_LOG_FAMILY_RUNTIME: return "rt";
    case PRT_TRIGGER_LOG_FAMILY_DMA_FIXED_LOAD: return "dfix";
    case PRT_TRIGGER_LOG_FAMILY_DMA_EXPORT: return "dexp";
    case PRT_TRIGGER_LOG_FAMILY_RR: return "rr";
    case PRT_TRIGGER_LOG_FAMILY_GEMMINI_POINTWISE: return "gpw";
    case PRT_TRIGGER_LOG_FAMILY_SPM_XLATE: return "sx";
    default: return "any";
  }
}

static const char *prt_trigger_log_fmt_u32(uint32_t value, char *buf, size_t buf_len) {
  if (!buf || buf_len == 0U) return "";
  if (value == PRT_TRIGGER_LOG_ANY_U32) {
    if (buf_len >= 2U) {
      buf[0] = '*';
      buf[1] = '\0';
    }
    return buf;
  }
  snprintf(buf, buf_len, "%u", value);
  return buf;
}

static int prt_trigger_log_match_u32(uint32_t want, uint32_t have) {
  return want == PRT_TRIGGER_LOG_ANY_U32 || have == want;
}

static void prt_trigger_log_init_once(void) {
  const char *path = NULL;
  const char *family_value = NULL;
  uint32_t pre_ring = 8U;
  uint32_t post_budget = 32U;
  g_prt_trigger_log_state.initialized = 1;
  g_prt_trigger_log_state.enabled =
    prt_env_flag_enabled_impl("PIPELINE_RUNTIME_DEBUG_TRIGGER_ENABLE", 0);
  if (!g_prt_trigger_log_state.enabled) return;

  family_value = getenv("PIPELINE_RUNTIME_DEBUG_TRIGGER_KIND");
  g_prt_trigger_log_state.family = prt_trigger_log_family_from_env(family_value);
  g_prt_trigger_log_state.segment_idx =
    prt_trigger_log_env_u32_any("PIPELINE_RUNTIME_DEBUG_TRIGGER_SEGMENT", PRT_TRIGGER_LOG_ANY_U32);
  g_prt_trigger_log_state.global_stage_id =
    prt_trigger_log_env_u32_any("PIPELINE_RUNTIME_DEBUG_TRIGGER_GLOBAL_STAGE", PRT_TRIGGER_LOG_ANY_U32);
  g_prt_trigger_log_state.local_stage_id =
    prt_trigger_log_env_u32_any("PIPELINE_RUNTIME_DEBUG_TRIGGER_LOCAL_STAGE", PRT_TRIGGER_LOG_ANY_U32);
  g_prt_trigger_log_state.subbatch_id =
    prt_trigger_log_env_u32_any("PIPELINE_RUNTIME_DEBUG_TRIGGER_SUBBATCH", PRT_TRIGGER_LOG_ANY_U32);
  g_prt_trigger_log_state.manager_id =
    prt_trigger_log_env_u32_any("PIPELINE_RUNTIME_DEBUG_TRIGGER_MANAGER", PRT_TRIGGER_LOG_ANY_U32);
  g_prt_trigger_log_state.tensor_id =
    prt_trigger_log_env_u32_any("PIPELINE_RUNTIME_DEBUG_TRIGGER_TENSOR_ID", PRT_TRIGGER_LOG_ANY_U32);
  g_prt_trigger_log_state.page_idx =
    prt_trigger_log_env_u32_any("PIPELINE_RUNTIME_DEBUG_TRIGGER_PAGE", PRT_TRIGGER_LOG_ANY_U32);
  g_prt_trigger_log_state.token_id =
    prt_trigger_log_env_u32_any("PIPELINE_RUNTIME_DEBUG_TRIGGER_TOKEN", PRT_TRIGGER_LOG_ANY_U32);

  pre_ring = prt_trigger_log_env_u32("PIPELINE_RUNTIME_DEBUG_TRIGGER_PRE_RING", 8U);
  if (pre_ring > PRT_TRIGGER_LOG_MAX_PRE_RING) pre_ring = PRT_TRIGGER_LOG_MAX_PRE_RING;
  g_prt_trigger_log_state.pre_ring = pre_ring;

  post_budget = prt_trigger_log_env_u32("PIPELINE_RUNTIME_DEBUG_TRIGGER_POST_BUDGET", 32U);
  g_prt_trigger_log_state.post_budget = post_budget;
  g_prt_trigger_log_state.match_once =
    prt_env_flag_enabled_impl("PIPELINE_RUNTIME_DEBUG_TRIGGER_MATCH_ONCE", 1);

  path = getenv("PIPELINE_RUNTIME_DEBUG_TRIGGER_LOG_PATH");
  if (path && *path) {
    snprintf(g_prt_trigger_log_state.path, sizeof(g_prt_trigger_log_state.path), "%s", path);
  }
}

static void prt_trigger_log_fill_context(prt_trigger_log_event_t *event) {
  prt_log_gate_ctx_snapshot_t ctx = {0, 0, 0, 0, 0};
  if (!event) return;
  prt_log_gate_get_context(&ctx);
  if (!ctx.valid) return;
  if (event->segment_idx == PRT_TRIGGER_LOG_ANY_U32) event->segment_idx = ctx.segment_idx;
  if (event->global_stage_id == PRT_TRIGGER_LOG_ANY_U32) event->global_stage_id = ctx.global_stage_id;
  if (event->local_stage_id == PRT_TRIGGER_LOG_ANY_U32) event->local_stage_id = ctx.local_stage_id;
  if (event->subbatch_id == PRT_TRIGGER_LOG_ANY_U32) event->subbatch_id = ctx.subbatch_id;
}

static int prt_trigger_log_matches(const prt_trigger_log_event_t *event) {
  if (!event) return 0;
  if (g_prt_trigger_log_state.family != PRT_TRIGGER_LOG_FAMILY_ANY &&
      event->family != g_prt_trigger_log_state.family) {
    return 0;
  }
  return prt_trigger_log_match_u32(g_prt_trigger_log_state.segment_idx, event->segment_idx) &&
         prt_trigger_log_match_u32(g_prt_trigger_log_state.global_stage_id, event->global_stage_id) &&
         prt_trigger_log_match_u32(g_prt_trigger_log_state.local_stage_id, event->local_stage_id) &&
         prt_trigger_log_match_u32(g_prt_trigger_log_state.subbatch_id, event->subbatch_id) &&
         prt_trigger_log_match_u32(g_prt_trigger_log_state.manager_id, event->manager_id) &&
         prt_trigger_log_match_u32(g_prt_trigger_log_state.tensor_id, event->tensor_id) &&
         prt_trigger_log_match_u32(g_prt_trigger_log_state.page_idx, event->page_idx) &&
         prt_trigger_log_match_u32(g_prt_trigger_log_state.token_id, event->token_id);
}

static uint16_t prt_trigger_log_format_line(char *out, size_t out_len,
                                            unsigned long long seq,
                                            const prt_trigger_log_event_t *event) {
  char seg_buf[16];
  char gs_buf[16];
  char ls_buf[16];
  char sb_buf[16];
  char mgr_buf[16];
  char tn_buf[16];
  char pg_buf[16];
  char tok_buf[16];
  int rc = 0;
  size_t used = 0U;
  if (!out || out_len == 0U || !event || !event->phase) return 0U;
  rc = snprintf(out, out_len,
                "seq=%llu fam=%s ph=%s seg=%s gs=%s ls=%s sb=%s mgr=%s tn=%s pg=%s tok=%s rc=%d\n",
                seq,
                prt_trigger_log_family_code(event->family),
                event->phase,
                prt_trigger_log_fmt_u32(event->segment_idx, seg_buf, sizeof(seg_buf)),
                prt_trigger_log_fmt_u32(event->global_stage_id, gs_buf, sizeof(gs_buf)),
                prt_trigger_log_fmt_u32(event->local_stage_id, ls_buf, sizeof(ls_buf)),
                prt_trigger_log_fmt_u32(event->subbatch_id, sb_buf, sizeof(sb_buf)),
                prt_trigger_log_fmt_u32(event->manager_id, mgr_buf, sizeof(mgr_buf)),
                prt_trigger_log_fmt_u32(event->tensor_id, tn_buf, sizeof(tn_buf)),
                prt_trigger_log_fmt_u32(event->page_idx, pg_buf, sizeof(pg_buf)),
                prt_trigger_log_fmt_u32(event->token_id, tok_buf, sizeof(tok_buf)),
                event->rc);
  if (rc <= 0) return 0U;
  used = (size_t)rc;
  if (used >= out_len) used = out_len - 1U;
  return (uint16_t)used;
}

static void prt_trigger_log_ring_push(const char *line, uint16_t len) {
  if (!line || len == 0U) return;
  if (g_prt_trigger_log_state.pre_ring == 0U) return;
  snprintf(t_prt_trigger_log_ring.lines[t_prt_trigger_log_ring.next],
           sizeof(t_prt_trigger_log_ring.lines[t_prt_trigger_log_ring.next]),
           "%s", line);
  t_prt_trigger_log_ring.lens[t_prt_trigger_log_ring.next] = len;
  t_prt_trigger_log_ring.next =
    (t_prt_trigger_log_ring.next + 1U) % PRT_TRIGGER_LOG_MAX_PRE_RING;
  if (t_prt_trigger_log_ring.count < PRT_TRIGGER_LOG_MAX_PRE_RING) {
    t_prt_trigger_log_ring.count += 1U;
  }
}

static int prt_trigger_log_open_locked(void) {
  if (g_prt_trigger_log_state.fd >= 0) return g_prt_trigger_log_state.fd;
  g_prt_trigger_log_state.fd =
    open(g_prt_trigger_log_state.path, O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0644);
  return g_prt_trigger_log_state.fd;
}

static void prt_trigger_log_write_locked(const char *buf, uint16_t len) {
  if (!buf || len == 0U) return;
  if (prt_trigger_log_open_locked() < 0) return;
  prt_write_fd_all_impl(g_prt_trigger_log_state.fd, buf, (size_t)len);
}

static void prt_trigger_log_flush_ring_locked(void) {
  uint32_t count = t_prt_trigger_log_ring.count;
  uint32_t keep = g_prt_trigger_log_state.pre_ring;
  uint32_t start = 0U;
  if (count == 0U || keep == 0U) return;
  if (count > keep) count = keep;
  start = (t_prt_trigger_log_ring.next + PRT_TRIGGER_LOG_MAX_PRE_RING - count) %
          PRT_TRIGGER_LOG_MAX_PRE_RING;
  for (uint32_t i = 0; i < count; ++i) {
    uint32_t idx = (start + i) % PRT_TRIGGER_LOG_MAX_PRE_RING;
    prt_trigger_log_write_locked(t_prt_trigger_log_ring.lines[idx],
                                 t_prt_trigger_log_ring.lens[idx]);
  }
}

int prt_trigger_log_enabled(void) {
  if (!g_prt_trigger_log_state.initialized) {
    prt_trigger_log_init_once();
  }
  return g_prt_trigger_log_state.enabled;
}

void prt_trigger_log_note(const prt_trigger_log_event_t *event) {
  prt_trigger_log_event_t resolved;
  char line[PRT_TRIGGER_LOG_MAX_LINE];
  uint16_t len = 0U;
  unsigned long long seq = 0ULL;
  int write_active_line = 0;
  int activate_now = 0;
  int eligible_for_capture = 0;

  if (!event || !event->phase || !*event->phase) return;
  if (!prt_trigger_log_enabled()) return;

  resolved = *event;
  prt_trigger_log_fill_context(&resolved);
  pthread_mutex_lock(&g_prt_trigger_log_state.lock);
  eligible_for_capture =
    g_prt_trigger_log_state.active ||
    (((!g_prt_trigger_log_state.match_once || !g_prt_trigger_log_state.matched_once) &&
      prt_trigger_log_matches(&resolved)));
  if (!eligible_for_capture) {
    pthread_mutex_unlock(&g_prt_trigger_log_state.lock);
    return;
  }

  seq = __sync_add_and_fetch(&g_prt_trigger_log_state.seq, 1ULL);
  len = prt_trigger_log_format_line(line, sizeof(line), seq, &resolved);
  if (len == 0U) {
    pthread_mutex_unlock(&g_prt_trigger_log_state.lock);
    return;
  }
  prt_trigger_log_ring_push(line, len);

  if (g_prt_trigger_log_state.active) {
    write_active_line = 1;
  } else {
    activate_now = 1;
    g_prt_trigger_log_state.active = g_prt_trigger_log_state.post_budget > 0U;
    g_prt_trigger_log_state.budget_remaining = g_prt_trigger_log_state.post_budget;
    if (g_prt_trigger_log_state.match_once) {
      g_prt_trigger_log_state.matched_once = 1;
    }
  }

  if (activate_now) {
    prt_trigger_log_flush_ring_locked();
  } else if (write_active_line) {
    prt_trigger_log_write_locked(line, len);
    if (g_prt_trigger_log_state.budget_remaining > 0U) {
      g_prt_trigger_log_state.budget_remaining -= 1U;
      if (g_prt_trigger_log_state.budget_remaining == 0U) {
        g_prt_trigger_log_state.active = 0;
      }
    }
  }
  pthread_mutex_unlock(&g_prt_trigger_log_state.lock);
}

#endif
