#define _GNU_SOURCE

#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#include "rocc-software/src/xcustom.h"
#include "include/rerocc_coupleddma.h"
#include "rerocc-linux-tests/rerocc_control.h"
#include "rerocc_linux_pagemap.h"

#ifndef REROCC_ACQUIRE_MAX_RETRIES
#define REROCC_ACQUIRE_MAX_RETRIES 1000000UL
#endif

#ifndef DMA_WAIT_SPINS
#define DMA_WAIT_SPINS 20000000UL
#endif

#define DMA_CFG_ID 1U
#define DMA_OPCODE_ID 2U

#define SHARED_SPAD_GLOBAL_ADDR_BASE 0x40000000ULL
#define UARTPROBE_DEFAULT_BYTES 65536ULL
#define UARTPROBE_DEFAULT_PAGE_BYTES 1024ULL
#define UARTPROBE_DEFAULT_DST_OFFSET 3072ULL
#define UARTPROBE_DEFAULT_REPEAT_COUNT 8U
#define UARTPROBE_DEFAULT_BURST_LINES 1U
#define UARTPROBE_DEFAULT_BURST_BYTES 96U
#define UARTPROBE_DEFAULT_SRC_LOCAL_ADDR 132096ULL
#define UARTPROBE_DEFAULT_LOG_PATH "/root/pipeline-runtime-debug/uartprobe.deep.log"
#define UARTPROBE_DEFAULT_BINARY_STAGE_PATH "/root/pipeline-runtime-debug/uartprobe.binary.stage"
#define UARTPROBE_MAX_LOG_LINE 2048U

enum {
  DMA_MON_VALID = 0,
  DMA_MON_SRC_CMDS = 1,
  DMA_MON_DST_CMDS = 2,
  DMA_MON_REQ_COPY_BYTES = 3,
  DMA_MON_CYCLES = 4,
  DMA_MON_EFFECTIVE_BYTES = 5,
  DMA_MON_EFF_BW_X1000_BPC = 6,
};

typedef enum {
  LOG_MODE_NONE = 0,
  LOG_MODE_STDOUT_STDIO,
  LOG_MODE_STDOUT_WRITE,
  LOG_MODE_FILE_WRITE,
  LOG_MODE_DUAL,
} log_mode_t;

typedef enum {
  LOG_PHASE_PRE_SUBMIT = 0,
  LOG_PHASE_POST_SUBMIT_PRE_WAIT,
  LOG_PHASE_POST_WAIT,
} log_phase_t;

typedef enum {
  WAIT_MODE_FENCE = 0,
  WAIT_MODE_DONEFLAG,
} wait_mode_t;

typedef enum {
  ALIAS_MODE_SINGLE = 0,
  ALIAS_MODE_AB,
  ALIAS_MODE_ABAB,
} alias_mode_t;

typedef enum {
  STDIO_SINK_INHERIT = 0,
  STDIO_SINK_DEVNULL,
} stdio_sink_t;

typedef enum {
  OBSERVABILITY_SILENT = 0,
  OBSERVABILITY_BINARY_STAGE,
  OBSERVABILITY_FULL,
} observability_t;

typedef struct {
  observability_t observability;
  log_mode_t log_mode;
  log_phase_t log_phase;
  wait_mode_t wait_mode;
  wait_mode_t seed_wait_mode;
  alias_mode_t alias_mode;
  stdio_sink_t stdio_sink;
  uint64_t bytes;
  uint64_t page_bytes;
  uint64_t dst_offset;
  uint64_t src_local_addr;
  uint32_t repeat_count;
  uint32_t burst_lines;
  uint32_t burst_bytes;
  uint32_t dma_manager_id;
  int dump_records;
  const char *log_path;
} uartprobe_cfg_t;

typedef struct {
  void *base;
  uint8_t *ptr;
  size_t alloc_bytes;
} host_region_t;

typedef struct {
  volatile uint32_t *va;
  uint64_t pa;
  size_t alloc_bytes;
} completion_flag_t;

typedef struct {
  uint64_t valid;
  uint64_t src_cmds;
  uint64_t dst_cmds;
  uint64_t req_copy_bytes;
  uint64_t cycles;
  uint64_t effective_bytes;
  uint64_t eff_bw_x1000_bpc;
} dma_monitor_sample_t;

typedef struct {
  uint32_t repeat_idx;
  uint32_t alias_copy_idx;
  uint32_t alias_slot;
  uint32_t page_idx;
  uint32_t chunk_idx;
  uint64_t src_pa;
  uint64_t dst_pa;
  uint64_t bytes;
  wait_mode_t wait_mode;
  uint64_t fence_status;
  int post_sample_valid;
  dma_monitor_sample_t pre_sample;
  dma_monitor_sample_t post_sample;
} dma_record_t;

typedef struct {
  dma_record_t *data;
  size_t size;
  size_t cap;
} dma_record_vec_t;

typedef struct {
  uint32_t repeat_idx;
  uint32_t alias_copy_idx;
  uint32_t alias_slot;
  uint32_t page_idx;
  uint32_t chunk_idx;
  uint64_t chunk_bytes;
} log_context_t;

static observability_t g_observability = OBSERVABILITY_FULL;

static bool parse_observability_env(const char *value, observability_t *out);
static void runtime_log_printf(const char *fmt, ...);

static void maybe_lock_memory(void) {
  if (mlockall(MCL_CURRENT | MCL_FUTURE) != 0) {
    runtime_log_printf("[uartprobe] warning: mlockall failed: %s\n", strerror(errno));
  }
}

static uint64_t align_up_u64(uint64_t value, uint64_t align) {
  return ((value + align - 1ULL) / align) * align;
}

static const char *log_mode_name(log_mode_t mode) {
  switch (mode) {
    case LOG_MODE_NONE: return "none";
    case LOG_MODE_STDOUT_STDIO: return "stdout_stdio";
    case LOG_MODE_STDOUT_WRITE: return "stdout_write";
    case LOG_MODE_FILE_WRITE: return "file_write";
    case LOG_MODE_DUAL: return "dual";
    default: return "unknown";
  }
}

static const char *log_phase_name(log_phase_t phase) {
  switch (phase) {
    case LOG_PHASE_PRE_SUBMIT: return "pre_submit";
    case LOG_PHASE_POST_SUBMIT_PRE_WAIT: return "post_submit_pre_wait";
    case LOG_PHASE_POST_WAIT: return "post_wait";
    default: return "unknown";
  }
}

static const char *wait_mode_name(wait_mode_t mode) {
  switch (mode) {
    case WAIT_MODE_FENCE: return "fence";
    case WAIT_MODE_DONEFLAG: return "doneflag";
    default: return "unknown";
  }
}

static const char *alias_mode_name(alias_mode_t mode) {
  switch (mode) {
    case ALIAS_MODE_SINGLE: return "single";
    case ALIAS_MODE_AB: return "ab";
    case ALIAS_MODE_ABAB: return "abab";
    default: return "unknown";
  }
}

static const char *observability_name(observability_t mode) {
  switch (mode) {
    case OBSERVABILITY_SILENT: return "silent";
    case OBSERVABILITY_BINARY_STAGE: return "binary_stage";
    case OBSERVABILITY_FULL: return "full";
    default: return "unknown";
  }
}

static bool observability_is_full(observability_t mode) {
  return mode == OBSERVABILITY_FULL;
}

static bool observability_allows_binary_stage(observability_t mode) {
  return mode == OBSERVABILITY_BINARY_STAGE || mode == OBSERVABILITY_FULL;
}

static bool current_observability_is_full(void) {
  return observability_is_full(g_observability);
}

static bool current_observability_allows_binary_stage(void) {
  return observability_allows_binary_stage(g_observability);
}

static void runtime_log_printf(const char *fmt, ...) {
  va_list ap;
  if (!fmt || !current_observability_is_full()) {
    return;
  }
  va_start(ap, fmt);
  vprintf(fmt, ap);
  va_end(ap);
}

static void write_all_fd(int fd, const char *buf, size_t len) {
  const char *ptr = buf;
  size_t remaining = len;
  while (remaining > 0U) {
    ssize_t written = write(fd, ptr, remaining);
    if (written > 0) {
      ptr += (size_t)written;
      remaining -= (size_t)written;
      continue;
    }
    if (written < 0 && errno == EINTR) {
      continue;
    }
    break;
  }
}

static void emit_diag_marker(const char *marker) {
  char line[160];
  int rc;
  size_t len;
  if (!marker || !current_observability_is_full()) {
    return;
  }
  rc = snprintf(line, sizeof(line), "[uartprobe-diag] %s\n", marker);
  if (rc <= 0) {
    return;
  }
  len = (size_t)rc;
  if (len >= sizeof(line)) {
    len = sizeof(line) - 1U;
  }
  write_all_fd(STDERR_FILENO, line, len);
}

static void emit_seed_marker(uint64_t page_idx,
                             uint64_t copied,
                             uint64_t chunk,
                             uint64_t src_pa,
                             uint64_t dst_pa,
                             const char *state) {
  char line[256];
  int rc;
  size_t len;
  if (!state || !current_observability_is_full()) {
    return;
  }
  rc = snprintf(line, sizeof(line),
                "[uartprobe-seed] page=%llu copied=%llu chunk=%llu state=%s src=0x%llx dst=0x%llx\n",
                (unsigned long long)page_idx,
                (unsigned long long)copied,
                (unsigned long long)chunk,
                state,
                (unsigned long long)src_pa,
                (unsigned long long)dst_pa);
  if (rc <= 0) {
    return;
  }
  len = (size_t)rc;
  if (len >= sizeof(line)) {
    len = sizeof(line) - 1U;
  }
  write_all_fd(STDERR_FILENO, line, len);
}

static void emit_issue_step_marker(const log_context_t *ctx,
                                   uint64_t src_pa,
                                   uint64_t dst_pa,
                                   uint64_t bytes,
                                   const char *state) {
  char line[256];
  int rc;
  size_t len;
  if (!ctx || !state || !current_observability_is_full()) {
    return;
  }
  rc = snprintf(line, sizeof(line),
                "[uartprobe-issue] repeat=%u alias=%u slot=%u page=%u chunk=%u bytes=%llu state=%s src=0x%llx dst=0x%llx\n",
                ctx->repeat_idx,
                ctx->alias_copy_idx,
                ctx->alias_slot,
                ctx->page_idx,
                ctx->chunk_idx,
                (unsigned long long)bytes,
                state,
                (unsigned long long)src_pa,
                (unsigned long long)dst_pa);
  if (rc <= 0) {
    return;
  }
  len = (size_t)rc;
  if (len >= sizeof(line)) {
    len = sizeof(line) - 1U;
  }
  write_all_fd(STDERR_FILENO, line, len);
}

static const char *binary_stage_path(void) {
  const char *value = getenv("UARTPROBE_BINARY_STAGE_PATH");
  if (value && value[0] != '\0') {
    return value;
  }
  return UARTPROBE_DEFAULT_BINARY_STAGE_PATH;
}

static void write_binary_stage(const char *stage) {
  const char *path = binary_stage_path();
  int fd;
  if (!path || path[0] == '\0' || !stage || !current_observability_allows_binary_stage()) {
    return;
  }
  fd = open(path, O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0644);
  if (fd < 0) {
    return;
  }
  write_all_fd(fd, stage, strlen(stage));
  write_all_fd(fd, "\n", 1U);
  close(fd);
}

static bool should_trace_issue_binary_stage(const log_context_t *ctx) {
  if (!ctx) {
    return false;
  }
  return ctx->repeat_idx == 0U &&
         ctx->alias_copy_idx == 0U &&
         ctx->alias_slot == 0U &&
         ctx->page_idx == 0U &&
         ctx->chunk_idx == 0U;
}

static void write_issue_binary_stage(const log_context_t *ctx, const char *state) {
  char stage[128];
  int rc;
  size_t len;
  if (!state || !should_trace_issue_binary_stage(ctx) || !current_observability_is_full()) {
    return;
  }
  rc = snprintf(stage, sizeof(stage),
                "issue-seed-r%u-a%u-s%u-p%u-c%u-%s",
                ctx->repeat_idx,
                ctx->alias_copy_idx,
                ctx->alias_slot,
                ctx->page_idx,
                ctx->chunk_idx,
                state);
  if (rc <= 0) {
    return;
  }
  len = (size_t)rc;
  if (len >= sizeof(stage)) {
    len = sizeof(stage) - 1U;
    stage[len] = '\0';
  }
  write_binary_stage(stage);
}

static int file_log_fd(const uartprobe_cfg_t *cfg) {
  static int fd = -2;
  if (fd != -2) {
    return fd;
  }
  if (!cfg || !cfg->log_path || cfg->log_path[0] == '\0') {
    fd = -1;
    return fd;
  }
  fd = open(cfg->log_path, O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0644);
  return fd;
}

static void redirect_stdio_to_file_log(const uartprobe_cfg_t *cfg) {
  int fd;
  if (!cfg || cfg->log_mode != LOG_MODE_FILE_WRITE) {
    return;
  }
  fd = file_log_fd(cfg);
  if (fd < 0) {
    return;
  }
  if (fd != STDOUT_FILENO) {
    (void)dup2(fd, STDOUT_FILENO);
  }
  if (fd != STDERR_FILENO) {
    (void)dup2(fd, STDERR_FILENO);
  }
  (void)setvbuf(stdout, NULL, _IONBF, 0);
  (void)setvbuf(stderr, NULL, _IONBF, 0);
  write_binary_stage("after-redirect");
}

static void redirect_stdio_to_devnull(const uartprobe_cfg_t *cfg) {
  int fd;
  if (!cfg || cfg->stdio_sink != STDIO_SINK_DEVNULL) {
    return;
  }
  fd = open("/dev/null", O_WRONLY | O_CLOEXEC);
  if (fd < 0) {
    return;
  }
  if (fd != STDOUT_FILENO) {
    (void)dup2(fd, STDOUT_FILENO);
  }
  if (fd != STDERR_FILENO) {
    (void)dup2(fd, STDERR_FILENO);
  }
  if (fd != STDOUT_FILENO && fd != STDERR_FILENO) {
    (void)close(fd);
  }
  (void)setvbuf(stdout, NULL, _IONBF, 0);
  (void)setvbuf(stderr, NULL, _IONBF, 0);
  write_binary_stage("after-devnull-redirect");
}

static void apply_stdio_redirection(const uartprobe_cfg_t *cfg) {
  if (!cfg) {
    return;
  }
  if (!observability_is_full(cfg->observability)) {
    return;
  }
  if (cfg->stdio_sink == STDIO_SINK_DEVNULL) {
    redirect_stdio_to_devnull(cfg);
    return;
  }
  redirect_stdio_to_file_log(cfg);
}

static void emit_log_line_stdout_stdio(const char *line) {
  fprintf(stdout, "%s", line);
  fflush(stdout);
}

static void emit_log_line_stdout_write(const char *line, size_t len) {
  write_all_fd(STDOUT_FILENO, line, len);
}

static void emit_log_line_file(const uartprobe_cfg_t *cfg, const char *line, size_t len) {
  const int fd = file_log_fd(cfg);
  if (fd >= 0) {
    write_all_fd(fd, line, len);
  }
}

static void emit_log_burst(const uartprobe_cfg_t *cfg, log_phase_t phase,
                           const log_context_t *ctx) {
  char line[UARTPROBE_MAX_LOG_LINE];
  size_t target_len;
  uint32_t i;

  if (!cfg || !ctx || !observability_is_full(cfg->observability) ||
      cfg->log_mode == LOG_MODE_NONE || cfg->log_phase != phase) {
    return;
  }

  target_len = cfg->burst_bytes > 0U ? cfg->burst_bytes : UARTPROBE_DEFAULT_BURST_BYTES;
  if (target_len >= sizeof(line)) {
    target_len = sizeof(line) - 2U;
  }

  for (i = 0; i < cfg->burst_lines; ++i) {
    int body_rc = snprintf(
      line, sizeof(line),
      "[uartprobe] phase=%s wait=%s repeat=%u alias_copy=%u alias_slot=%u page=%u chunk=%u bytes=%llu line=%u ",
      log_phase_name(phase),
      wait_mode_name(cfg->wait_mode),
      ctx->repeat_idx,
      ctx->alias_copy_idx,
      ctx->alias_slot,
      ctx->page_idx,
      ctx->chunk_idx,
      (unsigned long long)ctx->chunk_bytes,
      i);
    size_t used;
    if (body_rc < 0) {
      continue;
    }
    used = (size_t)body_rc;
    if (used > target_len) {
      used = target_len;
    }
    while (used < target_len) {
      static const char pad[] = "0123456789abcdef";
      line[used] = pad[(used + i + ctx->page_idx + ctx->chunk_idx) & 15U];
      used += 1U;
    }
    line[used++] = '\n';
    line[used] = '\0';

    switch (cfg->log_mode) {
      case LOG_MODE_STDOUT_STDIO:
        emit_log_line_stdout_stdio(line);
        break;
      case LOG_MODE_STDOUT_WRITE:
        emit_log_line_stdout_write(line, used);
        break;
      case LOG_MODE_FILE_WRITE:
        emit_log_line_file(cfg, line, used);
        break;
      case LOG_MODE_DUAL:
        emit_log_line_stdout_write(line, used);
        emit_log_line_file(cfg, line, used);
        break;
      default:
        break;
    }
  }
}

static void dma_monitor_sample_read(dma_monitor_sample_t *sample) {
  if (!sample) {
    return;
  }
  sample->valid = rerocc_coupleddma_read_monitor(DMA_MON_VALID);
  sample->src_cmds = rerocc_coupleddma_read_monitor(DMA_MON_SRC_CMDS);
  sample->dst_cmds = rerocc_coupleddma_read_monitor(DMA_MON_DST_CMDS);
  sample->req_copy_bytes = rerocc_coupleddma_read_monitor(DMA_MON_REQ_COPY_BYTES);
  sample->cycles = rerocc_coupleddma_read_monitor(DMA_MON_CYCLES);
  sample->effective_bytes = rerocc_coupleddma_read_monitor(DMA_MON_EFFECTIVE_BYTES);
  sample->eff_bw_x1000_bpc = rerocc_coupleddma_read_monitor(DMA_MON_EFF_BW_X1000_BPC);
}

static bool rr_acquire_cfg_with_retry(uint32_t cfg_id, uint64_t manager_id) {
  unsigned long retries = 0;
  while (!rr_acquire_cfg(cfg_id, manager_id)) {
    retries += 1UL;
    if (REROCC_ACQUIRE_MAX_RETRIES != 0UL && retries >= REROCC_ACQUIRE_MAX_RETRIES) {
      return false;
    }
    asm volatile("nop");
  }
  return true;
}

static bool dma_wait_done(volatile uint32_t *done_flag) {
  unsigned long spin;
  for (spin = 0UL; spin < DMA_WAIT_SPINS; ++spin) {
    asm volatile("fence r, rw");
    if (*done_flag != 0U) {
      *done_flag = 0U;
      asm volatile("fence" ::: "memory");
      return true;
    }
  }
  return false;
}

static bool alloc_host_region(const rerocc_linux_pagemap_t *pm, uint64_t offset,
                              uint64_t payload_bytes, host_region_t *region) {
  void *base;
  size_t alloc_bytes;
  if (!pm || !region || pm->page_size <= 0) {
    return false;
  }

  memset(region, 0, sizeof(*region));
  alloc_bytes = (size_t)align_up_u64(offset + payload_bytes, (uint64_t)pm->page_size);
  if (alloc_bytes == 0U) {
    return false;
  }

  base = mmap(NULL,
              alloc_bytes,
              PROT_READ | PROT_WRITE,
              MAP_PRIVATE | MAP_ANONYMOUS
#ifdef MAP_POPULATE
                  | MAP_POPULATE
#endif
              ,
              -1,
              0);
  if (base == MAP_FAILED) {
    runtime_log_printf("[uartprobe] mmap failed bytes=%zu: %s\n", alloc_bytes, strerror(errno));
    return false;
  }

  (void)mlock(base, alloc_bytes);
  memset(base, 0, alloc_bytes);
  region->base = base;
  region->ptr = (uint8_t *)base + offset;
  region->alloc_bytes = alloc_bytes;
  return true;
}

static void free_host_region(host_region_t *region) {
  if (!region || !region->base || region->alloc_bytes == 0U) {
    return;
  }
  munmap(region->base, region->alloc_bytes);
  region->base = NULL;
  region->ptr = NULL;
  region->alloc_bytes = 0U;
}

static bool alloc_completion_flag(const rerocc_linux_pagemap_t *pm, completion_flag_t *flag) {
  host_region_t tmp;
  uint64_t flag_pa = 0;
  if (!pm || !flag) {
    return false;
  }
  if (!alloc_host_region(pm, 0U, sizeof(uint32_t), &tmp)) {
    return false;
  }
  if (!rerocc_linux_virt_to_phys(pm, tmp.base, &flag_pa)) {
    runtime_log_printf("[uartprobe] virt_to_phys failed for completion flag\n");
    free_host_region(&tmp);
    return false;
  }
  flag->va = (volatile uint32_t *)tmp.base;
  flag->pa = flag_pa;
  flag->alloc_bytes = tmp.alloc_bytes;
  return true;
}

static void free_completion_flag(completion_flag_t *flag) {
  if (!flag || !flag->va || flag->alloc_bytes == 0U) {
    return;
  }
  munmap((void *)flag->va, flag->alloc_bytes);
  flag->va = NULL;
  flag->pa = 0U;
  flag->alloc_bytes = 0U;
}

static void fill_pattern(uint8_t *buf, uint64_t bytes) {
  uint64_t i;
  for (i = 0; i < bytes; ++i) {
    buf[i] = (uint8_t)((i * 17ULL + 29ULL) & 0xffU);
  }
}

static bool record_vec_push(dma_record_vec_t *vec, const dma_record_t *record) {
  dma_record_t *new_data;
  size_t new_cap;
  if (!vec || !record) {
    return false;
  }
  if (vec->size == vec->cap) {
    new_cap = vec->cap == 0U ? 256U : vec->cap * 2U;
    new_data = (dma_record_t *)realloc(vec->data, new_cap * sizeof(*new_data));
    if (!new_data) {
      return false;
    }
    vec->data = new_data;
    vec->cap = new_cap;
  }
  vec->data[vec->size++] = *record;
  return true;
}

static void record_vec_free(dma_record_vec_t *vec) {
  if (!vec) {
    return;
  }
  free(vec->data);
  vec->data = NULL;
  vec->size = 0U;
  vec->cap = 0U;
}

static bool parse_u64_env(const char *name, uint64_t default_value, uint64_t *out) {
  const char *value = getenv(name);
  char *end = NULL;
  unsigned long long parsed;
  if (!out) {
    return false;
  }
  if (!value || value[0] == '\0') {
    *out = default_value;
    return true;
  }
  errno = 0;
  parsed = strtoull(value, &end, 0);
  if (errno != 0 || end == value || *end != '\0') {
    fprintf(stderr, "[uartprobe] invalid integer for %s: %s\n", name, value);
    return false;
  }
  *out = (uint64_t)parsed;
  return true;
}

static bool parse_bool_env(const char *name, int default_value, int *out) {
  const char *value = getenv(name);
  if (!out) {
    return false;
  }
  if (!value || value[0] == '\0') {
    *out = default_value;
    return true;
  }
  if (!strcmp(value, "1") || !strcmp(value, "true") || !strcmp(value, "TRUE") ||
      !strcmp(value, "yes") || !strcmp(value, "YES")) {
    *out = 1;
    return true;
  }
  if (!strcmp(value, "0") || !strcmp(value, "false") || !strcmp(value, "FALSE") ||
      !strcmp(value, "no") || !strcmp(value, "NO")) {
    *out = 0;
    return true;
  }
  fprintf(stderr, "[uartprobe] invalid boolean for %s: %s\n", name, value);
  return false;
}

static bool parse_observability_env(const char *value, observability_t *out) {
  if (!value || !out) {
    return false;
  }
  if (!strcmp(value, "silent")) *out = OBSERVABILITY_SILENT;
  else if (!strcmp(value, "binary_stage")) *out = OBSERVABILITY_BINARY_STAGE;
  else if (!strcmp(value, "full")) *out = OBSERVABILITY_FULL;
  else return false;
  return true;
}

static bool parse_log_mode_env(const char *value, log_mode_t *out) {
  if (!value || !out) {
    return false;
  }
  if (!strcmp(value, "none")) *out = LOG_MODE_NONE;
  else if (!strcmp(value, "stdout_stdio")) *out = LOG_MODE_STDOUT_STDIO;
  else if (!strcmp(value, "stdout_write")) *out = LOG_MODE_STDOUT_WRITE;
  else if (!strcmp(value, "file_write")) *out = LOG_MODE_FILE_WRITE;
  else if (!strcmp(value, "dual")) *out = LOG_MODE_DUAL;
  else return false;
  return true;
}

static bool parse_log_phase_env(const char *value, log_phase_t *out) {
  if (!value || !out) {
    return false;
  }
  if (!strcmp(value, "pre_submit")) *out = LOG_PHASE_PRE_SUBMIT;
  else if (!strcmp(value, "post_submit_pre_wait")) *out = LOG_PHASE_POST_SUBMIT_PRE_WAIT;
  else if (!strcmp(value, "post_wait")) *out = LOG_PHASE_POST_WAIT;
  else return false;
  return true;
}

static bool parse_wait_mode_env(const char *value, wait_mode_t *out) {
  if (!value || !out) {
    return false;
  }
  if (!strcmp(value, "fence")) *out = WAIT_MODE_FENCE;
  else if (!strcmp(value, "doneflag")) *out = WAIT_MODE_DONEFLAG;
  else return false;
  return true;
}

static bool parse_alias_mode_env(const char *value, alias_mode_t *out) {
  if (!value || !out) {
    return false;
  }
  if (!strcmp(value, "single")) *out = ALIAS_MODE_SINGLE;
  else if (!strcmp(value, "ab")) *out = ALIAS_MODE_AB;
  else if (!strcmp(value, "abab")) *out = ALIAS_MODE_ABAB;
  else return false;
  return true;
}

static bool parse_stdio_sink_env(const char *value, stdio_sink_t *out) {
  if (!value || !out) {
    return false;
  }
  if (!strcmp(value, "inherit")) {
    *out = STDIO_SINK_INHERIT;
  } else if (!strcmp(value, "devnull")) {
    *out = STDIO_SINK_DEVNULL;
  } else {
    return false;
  }
  return true;
}

static bool parse_cfg(uartprobe_cfg_t *cfg) {
  const char *value;
  uint64_t tmp = 0ULL;
  int dump_records = 0;

  if (!cfg) {
    return false;
  }
  memset(cfg, 0, sizeof(*cfg));

  cfg->observability = OBSERVABILITY_FULL;
  value = getenv("UARTPROBE_OBSERVABILITY");
  if (value && value[0] != '\0' && !parse_observability_env(value, &cfg->observability)) {
    fprintf(stderr, "[uartprobe] invalid UARTPROBE_OBSERVABILITY: %s\n", value);
    return false;
  }

  cfg->log_mode = LOG_MODE_STDOUT_WRITE;
  value = getenv("UARTPROBE_LOG_MODE");
  if (value && value[0] != '\0' && !parse_log_mode_env(value, &cfg->log_mode)) {
    fprintf(stderr, "[uartprobe] invalid UARTPROBE_LOG_MODE: %s\n", value);
    return false;
  }

  cfg->log_phase = LOG_PHASE_POST_SUBMIT_PRE_WAIT;
  value = getenv("UARTPROBE_LOG_PHASE");
  if (value && value[0] != '\0' && !parse_log_phase_env(value, &cfg->log_phase)) {
    fprintf(stderr, "[uartprobe] invalid UARTPROBE_LOG_PHASE: %s\n", value);
    return false;
  }

  cfg->wait_mode = WAIT_MODE_FENCE;
  value = getenv("UARTPROBE_WAIT_MODE");
  if (value && value[0] != '\0' && !parse_wait_mode_env(value, &cfg->wait_mode)) {
    fprintf(stderr, "[uartprobe] invalid UARTPROBE_WAIT_MODE: %s\n", value);
    return false;
  }

  cfg->seed_wait_mode = WAIT_MODE_FENCE;
  value = getenv("UARTPROBE_SEED_WAIT_MODE");
  if (value && value[0] != '\0' && !parse_wait_mode_env(value, &cfg->seed_wait_mode)) {
    fprintf(stderr, "[uartprobe] invalid UARTPROBE_SEED_WAIT_MODE: %s\n", value);
    return false;
  }

  cfg->alias_mode = ALIAS_MODE_ABAB;
  value = getenv("UARTPROBE_ALIAS_MODE");
  if (value && value[0] != '\0' && !parse_alias_mode_env(value, &cfg->alias_mode)) {
    fprintf(stderr, "[uartprobe] invalid UARTPROBE_ALIAS_MODE: %s\n", value);
    return false;
  }

  cfg->stdio_sink = STDIO_SINK_INHERIT;
  value = getenv("UARTPROBE_STDIO_SINK");
  if (value && value[0] != '\0' && !parse_stdio_sink_env(value, &cfg->stdio_sink)) {
    fprintf(stderr, "[uartprobe] invalid UARTPROBE_STDIO_SINK: %s\n", value);
    return false;
  }

  if (!parse_u64_env("UARTPROBE_BYTES", UARTPROBE_DEFAULT_BYTES, &cfg->bytes)) {
    return false;
  }
  if (!parse_u64_env("UARTPROBE_PAGE_BYTES", UARTPROBE_DEFAULT_PAGE_BYTES, &cfg->page_bytes)) {
    return false;
  }
  if (!parse_u64_env("UARTPROBE_DST_OFFSET", UARTPROBE_DEFAULT_DST_OFFSET, &cfg->dst_offset)) {
    return false;
  }
  if (!parse_u64_env("UARTPROBE_SRC_LOCAL_ADDR", UARTPROBE_DEFAULT_SRC_LOCAL_ADDR,
                     &cfg->src_local_addr)) {
    return false;
  }
  if (!parse_u64_env("UARTPROBE_REPEAT_COUNT", UARTPROBE_DEFAULT_REPEAT_COUNT, &tmp)) {
    return false;
  }
  cfg->repeat_count = (uint32_t)tmp;
  if (!parse_u64_env("UARTPROBE_BURST_LINES", UARTPROBE_DEFAULT_BURST_LINES, &tmp)) {
    return false;
  }
  cfg->burst_lines = (uint32_t)tmp;
  if (!parse_u64_env("UARTPROBE_BURST_BYTES", UARTPROBE_DEFAULT_BURST_BYTES, &tmp)) {
    return false;
  }
  cfg->burst_bytes = (uint32_t)tmp;
  if (!parse_u64_env("UARTPROBE_DMA_MANAGER_ID", 0U, &tmp)) {
    return false;
  }
  cfg->dma_manager_id = (uint32_t)tmp;
  if (!parse_bool_env("UARTPROBE_DUMP_RECORDS", 0, &dump_records)) {
    return false;
  }
  cfg->dump_records = dump_records;

  value = getenv("UARTPROBE_LOG_PATH");
  cfg->log_path = (value && value[0] != '\0') ? value : UARTPROBE_DEFAULT_LOG_PATH;

  if (cfg->bytes == 0ULL || cfg->page_bytes == 0ULL) {
    fprintf(stderr, "[uartprobe] bytes and page_bytes must be non-zero\n");
    return false;
  }
  if ((cfg->bytes % cfg->page_bytes) != 0ULL) {
    fprintf(stderr, "[uartprobe] bytes=%llu must be a multiple of page_bytes=%llu\n",
            (unsigned long long)cfg->bytes,
            (unsigned long long)cfg->page_bytes);
    return false;
  }
  if (cfg->repeat_count == 0U) {
    fprintf(stderr, "[uartprobe] repeat_count must be > 0\n");
    return false;
  }
  if (cfg->burst_lines == 0U) {
    fprintf(stderr, "[uartprobe] burst_lines must be > 0\n");
    return false;
  }
  if (cfg->burst_bytes < 32U) {
    fprintf(stderr, "[uartprobe] burst_bytes must be >= 32\n");
    return false;
  }
  return true;
}

static bool issue_dma_copy(const uartprobe_cfg_t *cfg,
                           const log_context_t *ctx,
                           wait_mode_t wait_mode,
                           uint64_t src_pa,
                           uint64_t dst_pa,
                           uint64_t bytes,
                           completion_flag_t *flag,
                           dma_record_t *record,
                           int emit_hot_logs,
                           int emit_issue_steps) {
  bool ok = false;
  uint64_t fence_status = 0ULL;

  if (!cfg || !flag || !record) {
    return false;
  }
  memset(record, 0, sizeof(*record));
  record->repeat_idx = ctx ? ctx->repeat_idx : 0U;
  record->alias_copy_idx = ctx ? ctx->alias_copy_idx : 0U;
  record->alias_slot = ctx ? ctx->alias_slot : 0U;
  record->page_idx = ctx ? ctx->page_idx : 0U;
  record->chunk_idx = ctx ? ctx->chunk_idx : 0U;
  record->src_pa = src_pa;
  record->dst_pa = dst_pa;
  record->bytes = bytes;
  record->wait_mode = wait_mode;

  dma_monitor_sample_read(&record->pre_sample);

  if (emit_issue_steps) {
    emit_issue_step_marker(ctx, src_pa, dst_pa, bytes, "before-acquire");
    write_issue_binary_stage(ctx, "before-acquire");
  }
  if (!rr_acquire_cfg_with_retry(DMA_CFG_ID, (uint64_t)cfg->dma_manager_id)) {
    runtime_log_printf("[uartprobe] acquire timeout cfg=%u dma_mgr=%u\n",
                       DMA_CFG_ID, cfg->dma_manager_id);
    return false;
  }
  if (emit_issue_steps) {
    emit_issue_step_marker(ctx, src_pa, dst_pa, bytes, "after-acquire");
    write_issue_binary_stage(ctx, "after-acquire");
  }

  rr_set_opc(DMA_OPCODE_ID, DMA_CFG_ID);
  *flag->va = 0U;
  asm volatile("fence rw, rw" ::: "memory");

  if (emit_hot_logs) {
    emit_log_burst(cfg, LOG_PHASE_PRE_SUBMIT, ctx);
  }
  rerocc_coupleddma_set_dst(dst_pa, flag->pa);
  if (emit_issue_steps) {
    emit_issue_step_marker(ctx, src_pa, dst_pa, bytes, "after-set-dst");
    write_issue_binary_stage(ctx, "after-set-dst");
  }
  rerocc_coupleddma_set_src(src_pa, bytes);
  if (emit_issue_steps) {
    emit_issue_step_marker(ctx, src_pa, dst_pa, bytes, "after-set-src");
    write_issue_binary_stage(ctx, "after-set-src");
  }
  if (emit_hot_logs) {
    emit_log_burst(cfg, LOG_PHASE_POST_SUBMIT_PRE_WAIT, ctx);
  }
  if (emit_issue_steps) {
    write_issue_binary_stage(ctx, "before-wait");
  }

  if (wait_mode == WAIT_MODE_FENCE) {
    fence_status = rerocc_coupleddma_wait();
    record->fence_status = fence_status;
    dma_monitor_sample_read(&record->post_sample);
    record->post_sample_valid = 1;
    ok = true;
    if (emit_issue_steps) {
      emit_issue_step_marker(ctx, src_pa, dst_pa, bytes, "after-wait");
      write_issue_binary_stage(ctx, "after-wait");
    }
  } else {
    ok = dma_wait_done(flag->va);
    if (!ok) {
      runtime_log_printf("[uartprobe] doneflag timeout dma_mgr=%u src=0x%llx dst=0x%llx bytes=%llu repeat=%u alias=%u page=%u chunk=%u\n",
                         cfg->dma_manager_id,
                         (unsigned long long)src_pa,
                         (unsigned long long)dst_pa,
                         (unsigned long long)bytes,
                         record->repeat_idx,
                         record->alias_copy_idx,
                         record->page_idx,
                         record->chunk_idx);
      if (emit_issue_steps) {
        emit_issue_step_marker(ctx, src_pa, dst_pa, bytes, "doneflag-timeout");
        write_issue_binary_stage(ctx, "doneflag-timeout");
      }
    } else if (emit_issue_steps) {
      emit_issue_step_marker(ctx, src_pa, dst_pa, bytes, "after-doneflag");
      write_issue_binary_stage(ctx, "after-doneflag");
    }
  }

  if (ok && emit_hot_logs) {
    emit_log_burst(cfg, LOG_PHASE_POST_WAIT, ctx);
  }

  if (ok) {
    rr_fence(DMA_CFG_ID);
    if (emit_issue_steps) {
      emit_issue_step_marker(ctx, src_pa, dst_pa, bytes, "after-rr-fence");
    }
  }
  rr_release(DMA_CFG_ID);
  if (emit_issue_steps) {
    emit_issue_step_marker(ctx, src_pa, dst_pa, bytes, "after-release");
  }
  return ok;
}

static bool seed_shared_from_host(const uartprobe_cfg_t *cfg,
                                  const rerocc_linux_pagemap_t *pm,
                                  const uint8_t *src_host,
                                  completion_flag_t *flag) {
  uint64_t page_idx;
  if (!cfg || !pm || !src_host || !flag) {
    return false;
  }

  emit_diag_marker("enter seed");
  for (page_idx = 0ULL; page_idx < (cfg->bytes / cfg->page_bytes); ++page_idx) {
    uint64_t copied = 0ULL;
    const uint64_t dst_page_base =
      SHARED_SPAD_GLOBAL_ADDR_BASE + cfg->src_local_addr + page_idx * cfg->page_bytes;
    while (copied < cfg->page_bytes) {
      uint64_t src_off = page_idx * cfg->page_bytes + copied;
      const uint8_t *src_ptr = src_host + src_off;
      size_t host_page_off = (size_t)((uintptr_t)src_ptr % (uintptr_t)pm->page_size);
      uint64_t room = (uint64_t)((size_t)pm->page_size - host_page_off);
      uint64_t chunk = cfg->page_bytes - copied;
      uint64_t src_pa = 0ULL;
      log_context_t ctx;
      dma_record_t record;
      if (chunk > room) {
        chunk = room;
      }
      if (!rerocc_linux_virt_to_phys(pm, src_ptr, &src_pa)) {
        runtime_log_printf("[uartprobe] virt_to_phys failed for seed host ptr offset=%llu\n",
                           (unsigned long long)src_off);
        return false;
      }
      emit_seed_marker(page_idx, copied, chunk, src_pa, dst_page_base + copied, "before-dma");
      memset(&ctx, 0, sizeof(ctx));
      ctx.page_idx = (uint32_t)page_idx;
      ctx.chunk_idx = (uint32_t)(copied / chunk);
      ctx.chunk_bytes = chunk;
      if (!issue_dma_copy(cfg, &ctx, cfg->seed_wait_mode,
                          src_pa, dst_page_base + copied, chunk, flag, &record, 0, 1)) {
        runtime_log_printf("[uartprobe] seed copy failed page=%llu copied=%llu\n",
                           (unsigned long long)page_idx,
                           (unsigned long long)copied);
        return false;
      }
      emit_seed_marker(page_idx, copied, chunk, src_pa, dst_page_base + copied, "after-dma");
      copied += chunk;
    }
  }
  emit_diag_marker("after seed");
  return true;
}

static size_t alias_sequence(alias_mode_t mode, uint32_t seq[4]) {
  switch (mode) {
    case ALIAS_MODE_SINGLE:
      seq[0] = 0U;
      return 1U;
    case ALIAS_MODE_AB:
      seq[0] = 0U;
      seq[1] = 1U;
      return 2U;
    case ALIAS_MODE_ABAB:
    default:
      seq[0] = 0U;
      seq[1] = 1U;
      seq[2] = 0U;
      seq[3] = 1U;
      return 4U;
  }
}

static const char *alias_slot_name(uint32_t slot) {
  return slot == 0U ? "A" : "B";
}

static bool export_shared_to_alias(const uartprobe_cfg_t *cfg,
                                   const rerocc_linux_pagemap_t *pm,
                                   uint8_t *dst_host,
                                   completion_flag_t *flag,
                                   uint32_t repeat_idx,
                                   uint32_t alias_copy_idx,
                                   uint32_t alias_slot,
                                   dma_record_vec_t *records) {
  uint64_t page_idx;
  if (!cfg || !pm || !dst_host || !flag || !records) {
    return false;
  }

  memset(dst_host, 0, (size_t)cfg->bytes);
  for (page_idx = 0ULL; page_idx < (cfg->bytes / cfg->page_bytes); ++page_idx) {
    uint64_t copied = 0ULL;
    const uint64_t src_page_base =
      SHARED_SPAD_GLOBAL_ADDR_BASE + cfg->src_local_addr + page_idx * cfg->page_bytes;
    uint32_t chunk_idx = 0U;
    while (copied < cfg->page_bytes) {
      uint64_t dst_off = page_idx * cfg->page_bytes + copied;
      uint8_t *dst_ptr = dst_host + dst_off;
      size_t host_page_off = (size_t)((uintptr_t)dst_ptr % (uintptr_t)pm->page_size);
      uint64_t room = (uint64_t)((size_t)pm->page_size - host_page_off);
      uint64_t chunk = cfg->page_bytes - copied;
      uint64_t dst_pa = 0ULL;
      log_context_t ctx;
      dma_record_t record;
      if (chunk > room) {
        chunk = room;
      }
      if (!rerocc_linux_virt_to_phys(pm, dst_ptr, &dst_pa)) {
        runtime_log_printf("[uartprobe] virt_to_phys failed for alias=%s repeat=%u page=%llu dst_off=%llu\n",
                           alias_slot_name(alias_slot),
                           repeat_idx,
                           (unsigned long long)page_idx,
                           (unsigned long long)dst_off);
        return false;
      }
      memset(&ctx, 0, sizeof(ctx));
      ctx.repeat_idx = repeat_idx;
      ctx.alias_copy_idx = alias_copy_idx;
      ctx.alias_slot = alias_slot;
      ctx.page_idx = (uint32_t)page_idx;
      ctx.chunk_idx = chunk_idx;
      ctx.chunk_bytes = chunk;
      if (!issue_dma_copy(cfg, &ctx, cfg->wait_mode,
                          src_page_base + copied, dst_pa, chunk, flag, &record, 1, 0)) {
        runtime_log_printf("[uartprobe] export copy failed repeat=%u alias=%s page=%llu chunk=%u src=0x%llx dst=0x%llx\n",
                           repeat_idx,
                           alias_slot_name(alias_slot),
                           (unsigned long long)page_idx,
                           chunk_idx,
                           (unsigned long long)(src_page_base + copied),
                           (unsigned long long)dst_pa);
        return false;
      }
      if (!record_vec_push(records, &record)) {
        runtime_log_printf("[uartprobe] record buffer exhausted\n");
        return false;
      }
      copied += chunk;
      chunk_idx += 1U;
    }
  }
  return true;
}

static void dump_record_summary(const uartprobe_cfg_t *cfg, const dma_record_vec_t *records) {
  size_t i;
  uint64_t post_valid_records = 0ULL;
  uint64_t sum_src_cmds = 0ULL;
  uint64_t sum_dst_cmds = 0ULL;
  uint64_t sum_req_bytes = 0ULL;
  uint64_t sum_cycles = 0ULL;
  uint64_t sum_effective_bytes = 0ULL;
  uint64_t last_eff_bw = 0ULL;

  if (!cfg || !records || !observability_is_full(cfg->observability)) {
    return;
  }

  for (i = 0U; i < records->size; ++i) {
    if (records->data[i].post_sample_valid) {
      post_valid_records += 1ULL;
      sum_src_cmds += records->data[i].post_sample.src_cmds;
      sum_dst_cmds += records->data[i].post_sample.dst_cmds;
      sum_req_bytes += records->data[i].post_sample.req_copy_bytes;
      sum_cycles += records->data[i].post_sample.cycles;
      sum_effective_bytes += records->data[i].post_sample.effective_bytes;
      last_eff_bw = records->data[i].post_sample.eff_bw_x1000_bpc;
    }
  }

  runtime_log_printf("[uartprobe] records total=%llu post_valid=%llu sum_src_cmds=%llu sum_dst_cmds=%llu sum_req_bytes=%llu sum_cycles=%llu sum_effective_bytes=%llu last_eff_bw_x1000_bpc=%llu\n",
                     (unsigned long long)records->size,
                     (unsigned long long)post_valid_records,
                     (unsigned long long)sum_src_cmds,
                     (unsigned long long)sum_dst_cmds,
                     (unsigned long long)sum_req_bytes,
                     (unsigned long long)sum_cycles,
                     (unsigned long long)sum_effective_bytes,
                     (unsigned long long)last_eff_bw);

  if (!cfg->dump_records) {
    return;
  }
  for (i = 0U; i < records->size; ++i) {
    const dma_record_t *record = &records->data[i];
    runtime_log_printf("[uartprobe-record] idx=%llu repeat=%u alias_copy=%u alias_slot=%s page=%u chunk=%u bytes=%llu wait=%s fence_status=%llu pre_valid=%llu post_valid=%d post_mon_valid=%llu post_src_cmds=%llu post_dst_cmds=%llu post_req_bytes=%llu post_cycles=%llu post_effective_bytes=%llu post_eff_bw_x1000_bpc=%llu\n",
                       (unsigned long long)i,
                       record->repeat_idx,
                       record->alias_copy_idx,
                       alias_slot_name(record->alias_slot),
                       record->page_idx,
                       record->chunk_idx,
                       (unsigned long long)record->bytes,
                       wait_mode_name(record->wait_mode),
                       (unsigned long long)record->fence_status,
                       (unsigned long long)record->pre_sample.valid,
                       record->post_sample_valid,
                       (unsigned long long)record->post_sample.valid,
                       (unsigned long long)record->post_sample.src_cmds,
                       (unsigned long long)record->post_sample.dst_cmds,
                       (unsigned long long)record->post_sample.req_copy_bytes,
                       (unsigned long long)record->post_sample.cycles,
                       (unsigned long long)record->post_sample.effective_bytes,
                       (unsigned long long)record->post_sample.eff_bw_x1000_bpc);
    }
}

int main(void) {
  uartprobe_cfg_t cfg;
  rerocc_linux_pagemap_t pm;
  host_region_t src_host;
  host_region_t alias_a;
  host_region_t alias_b;
  completion_flag_t flag;
  dma_record_vec_t records;
  uint32_t alias_seq[4];
  size_t alias_count;
  uint32_t repeat_idx;
  int rc = 1;
  const char *observability_env = getenv("UARTPROBE_OBSERVABILITY");

  if (observability_env && observability_env[0] != '\0') {
    if (!parse_observability_env(observability_env, &g_observability)) {
      fprintf(stderr, "[uartprobe] invalid UARTPROBE_OBSERVABILITY: %s\n", observability_env);
      return 2;
    }
  }

  (void)setvbuf(stdout, NULL, _IONBF, 0);
  (void)setvbuf(stderr, NULL, _IONBF, 0);
  write_binary_stage("enter-main");
  maybe_lock_memory();
  write_binary_stage("after-mlock");

  if (!parse_cfg(&cfg)) {
    write_binary_stage("parse-cfg-failed");
    return 2;
  }

  write_binary_stage("after-parse");
  g_observability = cfg.observability;
  apply_stdio_redirection(&cfg);
  rerocc_linux_pagemap_reset(&pm);
  memset(&src_host, 0, sizeof(src_host));
  memset(&alias_a, 0, sizeof(alias_a));
  memset(&alias_b, 0, sizeof(alias_b));
  memset(&flag, 0, sizeof(flag));
  memset(&records, 0, sizeof(records));

  if (!rerocc_linux_pagemap_init(&pm)) {
    write_binary_stage("pagemap-init-failed");
    runtime_log_printf("[uartprobe] pagemap init failed\n");
    return 1;
  }
  write_binary_stage("after-pagemap-init");
  emit_diag_marker("after-stage after-pagemap-init");

  emit_diag_marker("before-stage before-alloc-src");
  write_binary_stage("before-alloc-src");
  emit_diag_marker("after-stage before-alloc-src");
  emit_diag_marker("enter alloc-src");
  if (!alloc_host_region(&pm, 0U, cfg.bytes, &src_host)) {
    write_binary_stage("alloc-src-failed");
    runtime_log_printf("[uartprobe] src allocation failed\n");
    goto cleanup;
  }
  write_binary_stage("after-alloc-src");
  emit_diag_marker("after alloc-src");

  emit_diag_marker("before-stage before-alloc-alias-a");
  write_binary_stage("before-alloc-alias-a");
  emit_diag_marker("after-stage before-alloc-alias-a");
  emit_diag_marker("enter alloc-alias-a");
  if (!alloc_host_region(&pm, cfg.dst_offset, cfg.bytes, &alias_a)) {
    write_binary_stage("alloc-alias-a-failed");
    runtime_log_printf("[uartprobe] alias_a allocation failed\n");
    goto cleanup;
  }
  write_binary_stage("after-alloc-alias-a");
  emit_diag_marker("after alloc-alias-a");
  write_binary_stage("after-alloc-alias-a-post-diag");

  emit_diag_marker("before-stage before-alloc-alias-b");
  write_binary_stage("before-alloc-alias-b");
  emit_diag_marker("after-stage before-alloc-alias-b");
  write_binary_stage("before-alloc-alias-b-post-diag");
  emit_diag_marker("enter alloc-alias-b");
  if (!alloc_host_region(&pm, cfg.dst_offset, cfg.bytes, &alias_b)) {
    write_binary_stage("alloc-alias-b-failed");
    runtime_log_printf("[uartprobe] alias_b allocation failed\n");
    goto cleanup;
  }
  write_binary_stage("after-alloc-alias-b");
  emit_diag_marker("after alloc-alias-b");

  emit_diag_marker("before-stage before-alloc-flag");
  write_binary_stage("before-alloc-flag");
  emit_diag_marker("after-stage before-alloc-flag");
  emit_diag_marker("enter alloc-flag");
  if (!alloc_completion_flag(&pm, &flag)) {
    write_binary_stage("alloc-flag-failed");
    runtime_log_printf("[uartprobe] completion flag allocation failed\n");
    goto cleanup;
  }
  write_binary_stage("after-alloc-flag");
  emit_diag_marker("after alloc-flag");

  if (!src_host.base || !alias_a.base || !alias_b.base || !flag.va) {
    write_binary_stage("alloc-failed");
    runtime_log_printf("[uartprobe] buffer allocation failed\n");
    goto cleanup;
  }

  fill_pattern(src_host.ptr, cfg.bytes);
  alias_count = alias_sequence(cfg.alias_mode, alias_seq);
  write_binary_stage("before-begin");

  runtime_log_printf("[uartprobe] begin observability=%s log_mode=%s log_phase=%s wait_mode=%s seed_wait_mode=%s alias_mode=%s bytes=%llu page_bytes=%llu dst_offset=%llu repeats=%u dma_mgr=%u src_local_addr=%llu\n",
                     observability_name(cfg.observability),
                     log_mode_name(cfg.log_mode),
                     log_phase_name(cfg.log_phase),
                     wait_mode_name(cfg.wait_mode),
                     wait_mode_name(cfg.seed_wait_mode),
                     alias_mode_name(cfg.alias_mode),
                     (unsigned long long)cfg.bytes,
                     (unsigned long long)cfg.page_bytes,
                     (unsigned long long)cfg.dst_offset,
                     cfg.repeat_count,
                     cfg.dma_manager_id,
                     (unsigned long long)cfg.src_local_addr);
  write_binary_stage("after-begin");

  if (!seed_shared_from_host(&cfg, &pm, src_host.ptr, &flag)) {
    write_binary_stage("seed-failed");
    runtime_log_printf("UARTPROBE_FAIL stage=seed\n");
    goto cleanup;
  }
  write_binary_stage("after-seed");
  runtime_log_printf("[uartprobe] seed-complete pages=%llu\n",
                     (unsigned long long)(cfg.bytes / cfg.page_bytes));

  for (repeat_idx = 0U; repeat_idx < cfg.repeat_count; ++repeat_idx) {
    size_t alias_idx;
    for (alias_idx = 0U; alias_idx < alias_count; ++alias_idx) {
      uint32_t slot = alias_seq[alias_idx];
      uint8_t *dst_ptr = slot == 0U ? alias_a.ptr : alias_b.ptr;
      if (!export_shared_to_alias(&cfg, &pm, dst_ptr, &flag,
                                  repeat_idx, (uint32_t)alias_idx, slot, &records)) {
        write_binary_stage("export-failed");
        runtime_log_printf("UARTPROBE_FAIL stage=export repeat=%u alias_copy=%u alias_slot=%s\n",
                           repeat_idx,
                           (unsigned)alias_idx,
                           alias_slot_name(slot));
        goto cleanup;
      }
      if (memcmp(src_host.ptr, dst_ptr, (size_t)cfg.bytes) != 0) {
        write_binary_stage("verify-failed");
        runtime_log_printf("[uartprobe] data mismatch repeat=%u alias_copy=%u alias_slot=%s\n",
                           repeat_idx,
                           (unsigned)alias_idx,
                           alias_slot_name(slot));
        runtime_log_printf("UARTPROBE_FAIL stage=verify repeat=%u alias_copy=%u alias_slot=%s\n",
                           repeat_idx,
                           (unsigned)alias_idx,
                           alias_slot_name(slot));
        goto cleanup;
      }
    }
  }

  dump_record_summary(&cfg, &records);
  write_binary_stage("pass");
  runtime_log_printf("UARTPROBE_PASS total_records=%llu\n",
                     (unsigned long long)records.size);
  rc = 0;

cleanup:
  record_vec_free(&records);
  free_completion_flag(&flag);
  free_host_region(&alias_b);
  free_host_region(&alias_a);
  free_host_region(&src_host);
  return rc;
}
