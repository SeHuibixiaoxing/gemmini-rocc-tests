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

// Keep marker logs deterministic by default; pad bursts are optional because
// they can dominate Linux UART bandwidth during long FireSim runs.
#ifndef PRT_ENABLE_PROGRESS_PAD_BURST
#define PRT_ENABLE_PROGRESS_PAD_BURST 0
#endif

#if PRT_ENABLE_ONLY_MARKER
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#ifndef BAREMETAL
#include <unistd.h>
#endif
static inline void prt_progress_write_all_impl(const char *buf, size_t len) {
  if (!buf || len == 0U) return;
#ifdef BAREMETAL
  fwrite(buf, 1U, len, stderr);
  fflush(stderr);
#else
  const char *ptr = buf;
  size_t remaining = len;
  while (remaining > 0U) {
    ssize_t written = write(STDERR_FILENO, ptr, remaining);
    if (written <= 0) break;
    ptr += (size_t)written;
    remaining -= (size_t)written;
  }
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
static inline void prt_marker_log_impl(const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  fprintf(stderr, "[prt-marker] ");
  vfprintf(stderr, fmt, ap);
  fputc('\n', stderr);
  fflush(stderr);
  va_end(ap);
}
static inline void prt_marker_crit_log_impl(const char *fmt, ...) {
  char line[2048];
  int prefix_rc;
  int body_rc;
  size_t used = 0U;
  va_list ap;

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

  prt_progress_write_all_impl(line, used);
  prt_progress_emit_pad_burst_impl(NULL);
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
static inline void prt_progress_err_log_impl(const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  fprintf(stderr, "[prt-progress-err] ");
  vfprintf(stderr, fmt, ap);
  fputc('\n', stderr);
  fflush(stderr);
  va_end(ap);
}
#define PRT_PROGRESS_LOG(fmt, ...)                                   \
  do {                                                               \
    fprintf(stdout, "[prt-progress] " fmt "\n", ##__VA_ARGS__);      \
    fflush(stdout);                                                  \
  } while (0)
#define PRT_PROGRESS_ERR_LOG(fmt, ...)                               \
  do {                                                               \
    prt_progress_err_log_impl(fmt, ##__VA_ARGS__);                   \
  } while (0)
#if PRT_ENABLE_PROGRESS_HOT_LOG
#define PRT_PROGRESS_HOT_LOG(fmt, ...)                               \
  do {                                                               \
    fprintf(stdout, "[prt-progress] " fmt "\n", ##__VA_ARGS__);      \
    fflush(stdout);                                                  \
  } while (0)
#define PRT_PROGRESS_HOT_ERR_LOG(fmt, ...)                           \
  do {                                                               \
    prt_progress_err_log_impl(fmt, ##__VA_ARGS__);                   \
  } while (0)
#else
#define PRT_PROGRESS_HOT_LOG(...) do { } while (0)
#define PRT_PROGRESS_HOT_ERR_LOG(...) do { } while (0)
#endif
#if PRT_ENABLE_PROGRESS_RAW_LOG
#define PRT_PROGRESS_RAW_LINE(msg_literal)                           \
  do {                                                               \
    static const char _prt_raw_line[] = msg_literal "\n";            \
    const char *_prt_raw_ptr = _prt_raw_line;                        \
    size_t _prt_raw_remaining = sizeof(_prt_raw_line) - 1U;          \
    while (_prt_raw_remaining > 0U) {                                \
      ssize_t _prt_raw_written = write(STDERR_FILENO, _prt_raw_ptr, _prt_raw_remaining); \
      if (_prt_raw_written <= 0) break;                              \
      _prt_raw_ptr += (size_t)_prt_raw_written;                      \
      _prt_raw_remaining -= (size_t)_prt_raw_written;                \
    }                                                                \
  } while (0)
#else
#define PRT_PROGRESS_RAW_LINE(...) do { } while (0)
#endif
#else
#define PRT_PROGRESS_LOG(...) do { } while (0)
#define PRT_PROGRESS_ERR_LOG(...) do { } while (0)
#define PRT_PROGRESS_HOT_LOG(...) do { } while (0)
#define PRT_PROGRESS_HOT_ERR_LOG(...) do { } while (0)
#define PRT_PROGRESS_RAW_LINE(...) do { } while (0)
#endif

#endif
