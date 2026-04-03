#ifndef PRT_PROGRESS_H
#define PRT_PROGRESS_H

#ifndef PRT_ENABLE_PROGRESS_LOG
#define PRT_ENABLE_PROGRESS_LOG 0
#endif

#ifndef PRT_ENABLE_PROGRESS_RAW_LOG
#define PRT_ENABLE_PROGRESS_RAW_LOG 0
#endif

#ifndef PRT_ENABLE_PROGRESS_HOT_LOG
#define PRT_ENABLE_PROGRESS_HOT_LOG 0
#endif

#ifndef PRT_ENABLE_ONLY_MARKER
#define PRT_ENABLE_ONLY_MARKER 0
#endif

#ifndef PRT_ENABLE_CRITICAL_UART_PROBE
#define PRT_ENABLE_CRITICAL_UART_PROBE 1
#endif

#ifndef PRT_ENABLE_CRITICAL_UART_PAD_BURST
#define PRT_ENABLE_CRITICAL_UART_PAD_BURST 0
#endif

// Keep marker logs deterministic by default; pad bursts are optional because
// they can dominate Linux UART bandwidth during long FireSim runs.
#ifndef PRT_ENABLE_PROGRESS_PAD_BURST
#define PRT_ENABLE_PROGRESS_PAD_BURST 0
#endif

#include "include/prt_log_gate.h"
#include <stdio.h>
#include <stdarg.h>
#include <string.h>

static inline void prt_stream_prefixed_vlog_impl(FILE *stream, const char *prefix, const char *fmt, va_list ap) {
  if (stream == NULL) return;
  fprintf(stream, "%s", prefix ? prefix : "");
  vfprintf(stream, fmt, ap);
  fputc('\n', stream);
  fflush(stream);
}

#ifndef BAREMETAL
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>
#endif
#if !defined(BAREMETAL)
static inline int prt_env_flag_enabled_impl(const char *name, int default_value) {
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

static inline int prt_uart_log_enabled_impl(void) {
  static int enabled = -1;
  if (enabled < 0) {
    enabled = prt_env_flag_enabled_impl("PIPELINE_RUNTIME_UART_LOG_ENABLE", 1);
  }
  return enabled;
}

static inline int prt_guest_log_enabled_impl(void) {
  static int enabled = -1;
  if (enabled < 0) {
    enabled = prt_env_flag_enabled_impl("PIPELINE_RUNTIME_GUEST_LOG_ENABLE", 1);
  }
  return enabled;
}

static inline const char *prt_guest_log_path_impl(void) {
  static int initialized = 0;
  static const char *path = NULL;
  if (!initialized) {
    path = getenv("PIPELINE_RUNTIME_GUEST_LOG_PATH");
    initialized = 1;
  }
  return path;
}

static inline int prt_guest_deep_log_enabled_impl(void) {
  static int enabled = -1;
  if (enabled < 0) {
    enabled = prt_env_flag_enabled_impl("PIPELINE_RUNTIME_GUEST_DEEP_LOG_ENABLE", 0);
  }
  return enabled;
}

static inline const char *prt_guest_deep_log_path_impl(void) {
  static int initialized = 0;
  static const char *path = NULL;
  if (!initialized) {
    path = getenv("PIPELINE_RUNTIME_GUEST_DEEP_LOG_PATH");
    initialized = 1;
  }
  return path;
}

static inline int prt_audit_log_enabled_impl(void) {
  static int enabled = -1;
  if (enabled < 0) {
    enabled = prt_env_flag_enabled_impl("PIPELINE_RUNTIME_AUDIT_LOG_ENABLE", 0);
  }
  return enabled;
}

static inline const char *prt_audit_log_path_impl(void) {
  static int initialized = 0;
  static const char *path = NULL;
  if (!initialized) {
    path = getenv("PIPELINE_RUNTIME_AUDIT_LOG_PATH");
    initialized = 1;
  }
  return path;
}

static inline int prt_log_fd_impl(void) {
  static int log_fd = -2;
  if (log_fd == -2) {
    int fd = open("/proc/self/fd/2", O_WRONLY | O_NONBLOCK | O_CLOEXEC);
    log_fd = fd >= 0 ? fd : STDERR_FILENO;
  }
  return log_fd;
}

static inline void prt_write_fd_all_impl(int fd, const char *buf, size_t len) {
  if (!buf || len == 0U) return;
  const char *ptr = buf;
  size_t remaining = len;
  while (remaining > 0U) {
    ssize_t written = write(fd, ptr, remaining);
    if (written > 0) {
      ptr += (size_t)written;
      remaining -= (size_t)written;
      continue;
    }
    if (written < 0 && errno == EINTR) continue;
    break;
  }
}

static inline void prt_write_all_impl(const char *buf, size_t len) {
  prt_write_fd_all_impl(prt_log_fd_impl(), buf, len);
}

static inline int prt_guest_log_fd_impl(void) {
  static int log_fd = -2;
  if (log_fd == -2) {
    const char *path = prt_guest_log_path_impl();
    if (prt_guest_log_enabled_impl() && path && *path) {
      log_fd = open(path, O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0644);
    } else {
      log_fd = -1;
    }
  }
  return log_fd;
}

static inline int prt_guest_deep_log_fd_impl(void) {
  static int log_fd = -2;
  if (log_fd == -2) {
    const char *path = prt_guest_deep_log_path_impl();
    if (prt_guest_deep_log_enabled_impl() && path && *path) {
      log_fd = open(path, O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0644);
    } else {
      log_fd = -1;
    }
  }
  return log_fd;
}

static inline int prt_audit_log_fd_impl(void) {
  static int log_fd = -2;
  if (log_fd == -2) {
    const char *path = prt_audit_log_path_impl();
    if (prt_audit_log_enabled_impl() && path && *path) {
      log_fd = open(path, O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0644);
    } else {
      log_fd = -1;
    }
  }
  return log_fd;
}

static inline void prt_guest_write_all_impl(const char *buf, size_t len) {
  const int guest_log_fd = prt_guest_log_fd_impl();
  if (!buf || len == 0U) return;
  if (guest_log_fd < 0) return;
  prt_write_fd_all_impl(guest_log_fd, buf, len);
}

static inline void prt_uart_write_all_impl(const char *buf, size_t len) {
  if (!buf || len == 0U) return;
  if (!prt_uart_log_enabled_impl()) return;
  prt_write_all_impl(buf, len);
}

static inline int prt_should_emit_deep_logs_impl(void) {
  if (!prt_guest_deep_log_enabled_impl()) return 0;
  if (!prt_log_gate_is_enabled()) return 1;
  return prt_log_gate_allow_deep_logs();
}

static inline void prt_deep_write_all_impl(const char *buf, size_t len) {
  const int deep_log_fd = prt_guest_deep_log_fd_impl();
  if (!buf || len == 0U) return;
  if (!prt_should_emit_deep_logs_impl()) return;
  if (deep_log_fd < 0) return;
  prt_write_fd_all_impl(deep_log_fd, buf, len);
}

static inline void prt_audit_write_all_impl(const char *buf, size_t len) {
  const int audit_log_fd = prt_audit_log_fd_impl();
  if (!buf || len == 0U) return;
  if (audit_log_fd < 0) return;
  prt_write_fd_all_impl(audit_log_fd, buf, len);
}

static inline void prt_guest_prefixed_vlog_impl(const char *prefix, const char *fmt, va_list ap) {
  char line[2048];
  int prefix_rc;
  int body_rc;
  size_t used = 0U;

  prefix_rc = snprintf(line, sizeof(line), "%s", prefix ? prefix : "");
  if (prefix_rc < 0) return;
  if ((size_t)prefix_rc >= sizeof(line)) {
    line[sizeof(line) - 2U] = '\n';
    line[sizeof(line) - 1U] = '\0';
    prt_guest_write_all_impl(line, sizeof(line) - 1U);
    return;
  }

  used = (size_t)prefix_rc;
  body_rc = vsnprintf(line + used, sizeof(line) - used, fmt, ap);
  if (body_rc < 0) return;

  used += (size_t)body_rc;
  if (used >= sizeof(line) - 1U) {
    used = sizeof(line) - 2U;
  }
  line[used++] = '\n';
  line[used] = '\0';

  prt_guest_write_all_impl(line, used);
}

static inline void prt_progress_prefixed_vlog_impl(FILE *stream, const char *prefix, const char *fmt, va_list ap) {
  if (prt_guest_log_fd_impl() >= 0) {
    va_list ap_copy;
    va_copy(ap_copy, ap);
    prt_guest_prefixed_vlog_impl(prefix, fmt, ap_copy);
    va_end(ap_copy);
    return;
  }

  if (!prt_uart_log_enabled_impl()) return;
  prt_stream_prefixed_vlog_impl(stream, prefix, fmt, ap);
}

static inline void prt_deep_prefixed_vlog_impl(const char *prefix, const char *fmt, va_list ap) {
  char line[2048];
  int prefix_rc;
  int body_rc;
  size_t used = 0U;

  prefix_rc = snprintf(line, sizeof(line), "%s", prefix ? prefix : "");
  if (prefix_rc < 0) return;
  if ((size_t)prefix_rc >= sizeof(line)) {
    line[sizeof(line) - 2U] = '\n';
    line[sizeof(line) - 1U] = '\0';
    prt_deep_write_all_impl(line, sizeof(line) - 1U);
    return;
  }

  used = (size_t)prefix_rc;
  body_rc = vsnprintf(line + used, sizeof(line) - used, fmt, ap);
  if (body_rc < 0) return;

  used += (size_t)body_rc;
  if (used >= sizeof(line) - 1U) {
    used = sizeof(line) - 2U;
  }
  line[used++] = '\n';
  line[used] = '\0';

  prt_deep_write_all_impl(line, used);
}

static inline void prt_deep_prefixed_log_impl(const char *prefix, const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  prt_deep_prefixed_vlog_impl(prefix, fmt, ap);
  va_end(ap);
}

static inline void prt_audit_prefixed_vlog_impl(const char *prefix, const char *fmt, va_list ap) {
  char line[2048];
  int prefix_rc;
  int body_rc;
  size_t used = 0U;

  prefix_rc = snprintf(line, sizeof(line), "%s", prefix ? prefix : "");
  if (prefix_rc < 0) return;
  if ((size_t)prefix_rc >= sizeof(line)) {
    line[sizeof(line) - 2U] = '\n';
    line[sizeof(line) - 1U] = '\0';
    prt_audit_write_all_impl(line, sizeof(line) - 1U);
    return;
  }

  used = (size_t)prefix_rc;
  body_rc = vsnprintf(line + used, sizeof(line) - used, fmt, ap);
  if (body_rc < 0) return;

  used += (size_t)body_rc;
  if (used >= sizeof(line) - 1U) {
    used = sizeof(line) - 2U;
  }
  line[used++] = '\n';
  line[used] = '\0';

  prt_audit_write_all_impl(line, used);
}
#else
static inline void prt_deep_write_all_impl(const char *buf, size_t len) {
  if (!buf || len == 0U) return;
  fwrite(buf, 1U, len, stderr);
  fflush(stderr);
}

static inline void prt_audit_write_all_impl(const char *buf, size_t len) {
  if (!buf || len == 0U) return;
  fwrite(buf, 1U, len, stderr);
  fflush(stderr);
}
#endif

static inline void prt_audit_log_impl(const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
#ifdef BAREMETAL
  prt_stream_prefixed_vlog_impl(stderr, "[prt-audit] ", fmt, ap);
#else
  prt_audit_prefixed_vlog_impl("[prt-audit] ", fmt, ap);
#endif
  va_end(ap);
}

#define PRT_AUDIT_LOG(fmt, ...)                                     \
  do {                                                              \
    prt_audit_log_impl(fmt, ##__VA_ARGS__);                         \
  } while (0)

#if PRT_ENABLE_CRITICAL_UART_PROBE
static inline void prt_crit_write_all_impl(const char *buf, size_t len) {
  if (!buf || len == 0U) return;
#ifdef BAREMETAL
  fwrite(buf, 1U, len, stderr);
  fflush(stderr);
#else
  prt_deep_write_all_impl(buf, len);
#endif
}
static inline void prt_crit_emit_pad_burst_impl(void) {
#if PRT_ENABLE_CRITICAL_UART_PAD_BURST
  static const char pad0[] =
    "[prt-crit-pad] burst-0-0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef\n";
  static const char pad1[] =
    "[prt-crit-pad] burst-1-fedcba9876543210fedcba9876543210fedcba9876543210fedcba9876543210\n";
  static const char pad2[] =
    "[prt-crit-pad] burst-2-00112233445566778899aabbccddeeff00112233445566778899aabbccddeeff\n";
  prt_crit_write_all_impl(pad0, sizeof(pad0) - 1U);
  prt_crit_write_all_impl(pad1, sizeof(pad1) - 1U);
  prt_crit_write_all_impl(pad2, sizeof(pad2) - 1U);
#endif
}
static inline void prt_crit_log_impl(const char *fmt, ...) {
  char line[2048];
  int prefix_rc;
  int body_rc;
  size_t used = 0U;
  va_list ap;

  prefix_rc = snprintf(line, sizeof(line), "[prt-crit] ");
  if (prefix_rc < 0) return;
  if ((size_t)prefix_rc >= sizeof(line)) {
    line[sizeof(line) - 2U] = '\n';
    line[sizeof(line) - 1U] = '\0';
    prt_crit_write_all_impl(line, sizeof(line) - 1U);
    prt_crit_emit_pad_burst_impl();
    return;
  }

  used = (size_t)prefix_rc;
  va_start(ap, fmt);
  body_rc = vsnprintf(line + used, sizeof(line) - used, fmt, ap);
  va_end(ap);
  if (body_rc < 0) return;

  used += (size_t)body_rc;
  if (used >= sizeof(line) - 1U) {
    used = sizeof(line) - 2U;
  }
  line[used++] = '\n';
  line[used] = '\0';

  prt_crit_write_all_impl(line, used);
  prt_crit_emit_pad_burst_impl();
}
#define PRT_CRIT_LINE(msg_literal)                                  \
  do {                                                              \
    prt_crit_log_impl("%s", msg_literal);                           \
  } while (0)
#define PRT_CRIT_LOG(fmt, ...)                                      \
  do {                                                              \
    prt_crit_log_impl(fmt, ##__VA_ARGS__);                          \
  } while (0)
#else
#define PRT_CRIT_LINE(...) do { } while (0)
#define PRT_CRIT_LOG(...) do { } while (0)
#endif

#if PRT_ENABLE_ONLY_MARKER
static inline void prt_progress_write_all_impl(const char *buf, size_t len) {
  if (!buf || len == 0U) return;
#ifdef BAREMETAL
  fwrite(buf, 1U, len, stderr);
  fflush(stderr);
#else
  prt_deep_write_all_impl(buf, len);
#endif
}
static inline void prt_progress_emit_pad_burst_impl(const char *prefix) {
#if PRT_ENABLE_PROGRESS_PAD_BURST
  static const char pad0[] =
    "[prt-pad] burst-0-0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef\n";
  static const char pad1[] =
    "[prt-pad] burst-1-fedcba9876543210fedcba9876543210fedcba9876543210fedcba9876543210\n";
  static const char pad2[] =
    "[prt-pad] burst-2-00112233445566778899aabbccddeeff00112233445566778899aabbccddeeff\n";
  if (prefix && *prefix) {
    prt_progress_write_all_impl(prefix, strlen(prefix));
  }
  prt_progress_write_all_impl(pad0, sizeof(pad0) - 1U);
  prt_progress_write_all_impl(pad1, sizeof(pad1) - 1U);
  prt_progress_write_all_impl(pad2, sizeof(pad2) - 1U);
#else
  (void)prefix;
#endif
}
// Keep ordinary marker logs on the same direct-write path as critical markers.
// Deep-log F2 runs can emit many marker lines inside hot windows, and stdio
// fflush(stderr) can wedge there even though the compute path itself is fine.
static inline void prt_marker_vlog_impl(const char *fmt, va_list ap) {
  char line[2048];
  int prefix_rc;
  int body_rc;
  size_t used = 0U;

  prefix_rc = snprintf(line, sizeof(line), "[prt-marker] ");
  if (prefix_rc < 0) return;
  if ((size_t)prefix_rc >= sizeof(line)) {
    line[sizeof(line) - 2U] = '\n';
    line[sizeof(line) - 1U] = '\0';
    prt_progress_write_all_impl(line, sizeof(line) - 1U);
    prt_progress_emit_pad_burst_impl(NULL);
    return;
  }

  used = (size_t)prefix_rc;
  body_rc = vsnprintf(line + used, sizeof(line) - used, fmt, ap);
  if (body_rc < 0) return;

  used += (size_t)body_rc;
  if (used >= sizeof(line) - 1U) {
    used = sizeof(line) - 2U;
  }
  line[used++] = '\n';
  line[used] = '\0';

  prt_progress_write_all_impl(line, used);
  prt_progress_emit_pad_burst_impl(NULL);
}
static inline void prt_marker_log_impl(const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  prt_marker_vlog_impl(fmt, ap);
  va_end(ap);
}
static inline void prt_marker_crit_log_impl(const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  prt_marker_vlog_impl(fmt, ap);
  va_end(ap);
}
#define PRT_MARKER_LOG(fmt, ...)                                    \
  do {                                                              \
    prt_marker_log_impl(fmt, ##__VA_ARGS__);                        \
  } while (0)
#define PRT_MARKER_CRIT_LOG(fmt, ...)                               \
  do {                                                              \
    prt_marker_crit_log_impl(fmt, ##__VA_ARGS__);                   \
  } while (0)
#else
#define PRT_MARKER_LOG(...) do { } while (0)
#define PRT_MARKER_CRIT_LOG(...) do { } while (0)
#endif

#if PRT_ENABLE_PROGRESS_LOG
#include <stdio.h>
#include <stdarg.h>
#include <unistd.h>
static inline void prt_progress_log_impl(const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
#ifdef BAREMETAL
  prt_stream_prefixed_vlog_impl(stdout, "[prt-progress] ", fmt, ap);
#else
  prt_progress_prefixed_vlog_impl(stdout, "[prt-progress] ", fmt, ap);
#endif
  va_end(ap);
}
static inline void prt_progress_err_log_impl(const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
#ifdef BAREMETAL
  prt_stream_prefixed_vlog_impl(stderr, "[prt-progress-err] ", fmt, ap);
#else
  prt_progress_prefixed_vlog_impl(stderr, "[prt-progress-err] ", fmt, ap);
#endif
  va_end(ap);
}
#define PRT_PROGRESS_LOG(fmt, ...)                                   \
  do {                                                               \
    prt_progress_log_impl(fmt, ##__VA_ARGS__);                       \
  } while (0)
#define PRT_PROGRESS_ERR_LOG(fmt, ...)                               \
  do {                                                               \
    prt_progress_err_log_impl(fmt, ##__VA_ARGS__);                   \
  } while (0)
#if PRT_ENABLE_PROGRESS_HOT_LOG
static inline void prt_progress_hot_log_impl(const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
#ifdef BAREMETAL
  fprintf(stdout, "[prt-progress] ");
  vfprintf(stdout, fmt, ap);
  fputc('\n', stdout);
  fflush(stdout);
#else
  prt_deep_prefixed_vlog_impl("[prt-progress] ", fmt, ap);
#endif
  va_end(ap);
}
static inline void prt_progress_hot_err_log_impl(const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
#ifdef BAREMETAL
  fprintf(stderr, "[prt-progress-err] ");
  vfprintf(stderr, fmt, ap);
  fputc('\n', stderr);
  fflush(stderr);
#else
  prt_deep_prefixed_vlog_impl("[prt-progress-err] ", fmt, ap);
#endif
  va_end(ap);
}
#define PRT_PROGRESS_HOT_LOG(fmt, ...)                               \
  do {                                                               \
    if (prt_log_gate_allow_deep_logs()) {                            \
      prt_progress_hot_log_impl(fmt, ##__VA_ARGS__);                 \
    }                                                                \
  } while (0)
#define PRT_PROGRESS_HOT_ERR_LOG(fmt, ...)                           \
  do {                                                               \
    if (prt_log_gate_allow_deep_logs()) {                            \
      prt_progress_hot_err_log_impl(fmt, ##__VA_ARGS__);             \
    }                                                                \
  } while (0)
#else
#define PRT_PROGRESS_HOT_LOG(...) do { } while (0)
#define PRT_PROGRESS_HOT_ERR_LOG(...) do { } while (0)
#endif
#else
#define PRT_PROGRESS_LOG(...) do { } while (0)
#define PRT_PROGRESS_ERR_LOG(...) do { } while (0)
#define PRT_PROGRESS_HOT_LOG(...) do { } while (0)
#define PRT_PROGRESS_HOT_ERR_LOG(...) do { } while (0)
#endif

#if PRT_ENABLE_PROGRESS_RAW_LOG
#define PRT_PROGRESS_RAW_LINE(msg_literal)                           \
  do {                                                               \
    if (prt_log_gate_allow_deep_logs()) {                            \
      static const char _prt_raw_line[] = msg_literal "\n";          \
      prt_deep_write_all_impl(_prt_raw_line, sizeof(_prt_raw_line) - 1U); \
    }                                                                \
  } while (0)
#else
#define PRT_PROGRESS_RAW_LINE(...) do { } while (0)
#endif

#endif
