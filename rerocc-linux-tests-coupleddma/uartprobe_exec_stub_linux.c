#define _GNU_SOURCE

#include <fcntl.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

static void append_line(const char *path, const char *line) {
  int fd;
  size_t len;

  if (!path || !*path || !line) {
    return;
  }

  fd = open(path, O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0644);
  if (fd < 0) {
    return;
  }

  len = strlen(line);
  while (len > 0U) {
    ssize_t written = write(fd, line, len);
    if (written > 0) {
      line += (size_t)written;
      len -= (size_t)written;
      continue;
    }
    if (written < 0) {
      continue;
    }
    break;
  }

  (void)fdatasync(fd);
  (void)close(fd);
}

static unsigned parse_sleep_seconds(void) {
  const char *value;
  char *endptr = NULL;
  unsigned long parsed;

  value = getenv("UARTPROBE_EXECSTUB_SLEEP_SECONDS");
  if (!value || !*value) {
    return 5U;
  }

  parsed = strtoul(value, &endptr, 10);
  if (endptr == value || (endptr && *endptr != '\0')) {
    return 5U;
  }
  if (parsed > 3600UL) {
    return 3600U;
  }
  return (unsigned)parsed;
}

int main(void) {
  const char *stage_path = getenv("UARTPROBE_BINARY_STAGE_PATH");
  unsigned sleep_seconds = parse_sleep_seconds();

  append_line(stage_path, "execstub-enter-main\n");
  sync();
  if (sleep_seconds > 0U) {
    sleep(sleep_seconds);
  }
  append_line(stage_path, "execstub-exit\n");
  sync();
  return 0;
}
