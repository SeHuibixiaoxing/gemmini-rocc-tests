#define _GNU_SOURCE

#include <asm/ptrace.h>
#include <elf.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ptrace.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <sys/wait.h>
#include <unistd.h>

static void usage(const char *argv0) {
  fprintf(stderr, "usage: %s <pid> [window-bytes]\n", argv0);
}

static int wait_stopped(pid_t pid) {
  int status = 0;
  if (waitpid(pid, &status, 0) < 0) {
    perror("waitpid");
    return 1;
  }
  if (!WIFSTOPPED(status)) {
    fprintf(stderr, "target pid=%ld did not stop cleanly status=0x%x\n",
            (long)pid, status);
    return 1;
  }
  return 0;
}

static int read_target_mem(pid_t pid, uint64_t addr, void *buf, size_t len) {
  char mem_path[64];
  int fd;
  ssize_t got;

  snprintf(mem_path, sizeof(mem_path), "/proc/%ld/mem", (long)pid);
  fd = open(mem_path, O_RDONLY | O_CLOEXEC);
  if (fd < 0) {
    perror("open /proc/pid/mem");
    return 1;
  }

  got = pread(fd, buf, len, (off_t)addr);
  close(fd);
  if (got < 0) {
    perror("pread /proc/pid/mem");
    return 1;
  }
  if ((size_t)got != len) {
    fprintf(stderr,
            "short read /proc/%ld/mem addr=0x%016" PRIx64 " want=%zu got=%zd\n",
            (long)pid, addr, len, got);
    return 1;
  }
  return 0;
}

static void dump_words(uint64_t start_addr, const uint8_t *buf, size_t len) {
  size_t off;
  for (off = 0; off + 4U <= len; off += 4U) {
    uint32_t word = 0;
    memcpy(&word, buf + off, sizeof(word));
    printf("insn addr=0x%016" PRIx64 " word=0x%08" PRIx32 "\n",
           start_addr + off, word);
  }
}

int main(int argc, char **argv) {
  char *end = NULL;
  long pid_long;
  long window_bytes = 32;
  pid_t pid;
  struct user_regs_struct regs;
  struct iovec iov;
  uint8_t *window = NULL;
  uint64_t start_addr;
  int rc = 1;

  if (argc < 2 || argc > 3) {
    usage(argv[0]);
    return 2;
  }

  errno = 0;
  pid_long = strtol(argv[1], &end, 10);
  if (errno != 0 || end == argv[1] || *end != '\0' || pid_long <= 0) {
    usage(argv[0]);
    return 2;
  }
  pid = (pid_t)pid_long;

  if (argc == 3) {
    errno = 0;
    window_bytes = strtol(argv[2], &end, 10);
    if (errno != 0 || end == argv[2] || *end != '\0' || window_bytes < 4) {
      usage(argv[0]);
      return 2;
    }
  }

  if ((window_bytes % 4L) != 0L) {
    window_bytes += 4L - (window_bytes % 4L);
  }

  if (ptrace(PTRACE_ATTACH, pid, 0, 0) < 0) {
    perror("ptrace attach");
    return 1;
  }

  if (wait_stopped(pid) != 0) {
    goto detach;
  }

  memset(&regs, 0, sizeof(regs));
  iov.iov_base = &regs;
  iov.iov_len = sizeof(regs);
  if (ptrace(PTRACE_GETREGSET, pid, (void *)NT_PRSTATUS, &iov) < 0) {
    perror("ptrace getregset");
    goto detach;
  }

  printf("ptrace pid=%ld pc=0x%016" PRIx64 " ra=0x%016" PRIx64
         " sp=0x%016" PRIx64 " s0=0x%016" PRIx64
         " a0=0x%016" PRIx64 " a1=0x%016" PRIx64 "\n",
         (long)pid,
         (uint64_t)regs.pc,
         (uint64_t)regs.ra,
         (uint64_t)regs.sp,
         (uint64_t)regs.s0,
         (uint64_t)regs.a0,
         (uint64_t)regs.a1);

  start_addr = regs.pc >= (uint64_t)(window_bytes / 2L)
                 ? regs.pc - (uint64_t)(window_bytes / 2L)
                 : 0;
  window = calloc(1U, (size_t)window_bytes);
  if (!window) {
    perror("calloc");
    goto detach;
  }

  if (read_target_mem(pid, start_addr, window, (size_t)window_bytes) == 0) {
    printf("ptrace-window start=0x%016" PRIx64 " bytes=%ld\n",
           start_addr, window_bytes);
    dump_words(start_addr, window, (size_t)window_bytes);
  }

  rc = 0;

detach:
  if (ptrace(PTRACE_DETACH, pid, 0, 0) < 0) {
    perror("ptrace detach");
    rc = 1;
  }
  free(window);
  return rc;
}
