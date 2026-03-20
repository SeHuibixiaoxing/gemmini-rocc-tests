#ifndef REROCC_LINUX_PAGEMAP_H
#define REROCC_LINUX_PAGEMAP_H

#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

typedef struct {
  int page_size;
  int pagemap_fd;
} rerocc_linux_pagemap_t;

static inline void rerocc_linux_pagemap_reset(rerocc_linux_pagemap_t *pm) {
  pm->page_size = 0;
  pm->pagemap_fd = -1;
}

static inline bool rerocc_linux_pagemap_init(rerocc_linux_pagemap_t *pm) {
  if (pm->pagemap_fd >= 0) {
    return true;
  }

  pm->page_size = getpagesize();
  if (pm->page_size <= 0) {
    printf("getpagesize failed\n");
    return false;
  }

  pm->pagemap_fd = open("/proc/self/pagemap", O_RDONLY);
  if (pm->pagemap_fd < 0) {
    printf("open /proc/self/pagemap failed: %s\n", strerror(errno));
    return false;
  }

  return true;
}

static inline bool rerocc_linux_virt_to_phys(
    const rerocc_linux_pagemap_t *pm, const void *vaddr, uint64_t *paddr) {
  const uint64_t va = (uint64_t)(uintptr_t)vaddr;
  const uint64_t vpn = va / (uint64_t)pm->page_size;
  const off_t offset = (off_t)(vpn * sizeof(uint64_t));
  uint64_t entry = 0;
  const uint64_t present = 1ULL << 63;
  const uint64_t pfn_mask = (1ULL << 55) - 1ULL;
  ssize_t n;

  n = pread(pm->pagemap_fd, &entry, sizeof(entry), offset);
  if (n != (ssize_t)sizeof(entry)) {
    return false;
  }
  if ((entry & present) == 0) {
    return false;
  }

  entry &= pfn_mask;
  if (entry == 0) {
    return false;
  }

  *paddr = entry * (uint64_t)pm->page_size + (va % (uint64_t)pm->page_size);
  return true;
}

#endif  // REROCC_LINUX_PAGEMAP_H
