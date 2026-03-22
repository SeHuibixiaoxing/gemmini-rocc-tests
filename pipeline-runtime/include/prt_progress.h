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
    (void)write(STDERR_FILENO, _prt_raw_line, sizeof(_prt_raw_line) - 1U); \
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
