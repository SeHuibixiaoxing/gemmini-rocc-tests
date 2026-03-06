#include "prt_runtime.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void usage(const char *prog) {
  fprintf(stderr,
    "Usage: %s --model-yaml <path> [--model-bin <path>] [--model-offset <bytes>] --pipeline-yaml <path> [--num-cores <n>] [--num-gemmini-mgrs <n>] [--num-dma-mgrs <n>] [--gemmini-base-id <n>] [--dma-base-id <n>] [--sync-mode <async|blocking_debug>] [--pages-per-acc <n>] [--spm-page-bytes <n>] [--spm-xlate-enable <0|1>] [--spm-xlate-range-base <hex>] [--spm-xlate-range-size <bytes>] [--hw-validate-only] [--input <path>] [--golden <path>] [--batch <n>] [--trace <path>]\\n"
    "       %s --hw-validate-only [runtime knobs above]\\n",
    prog, prog);
}

static int run_hw_validate_only(const prt_runtime_cfg_t *cfg) {
  prt_runtime_t rt;
  int rc = prt_runtime_init(cfg, &rt);
  if (rc != PRT_OK) {
    fprintf(stderr, "runtime_init failed: %s (%d)\\n", prt_err_str(rc), rc);
    return 1;
  }

  printf("HW_VALIDATE_ONLY_PASS\n");
  (void)prt_runtime_destroy(&rt);
  return 0;
}

int main(int argc, char **argv) {
  prt_runtime_cfg_t cfg;
  prt_run_args_t args;
  prt_runtime_t rt;
  int rc;
  int user_set_dma_base = 0;

  memset(&cfg, 0, sizeof(cfg));
  memset(&args, 0, sizeof(args));

  cfg.num_cores = 4;
  cfg.num_gemmini_mgrs = 4;
  cfg.num_dma_mgrs = 4;
  cfg.gemmini_mgr_base_id = 0;
  cfg.dma_mgr_base_id = 0;
  cfg.page_size_bytes = PRT_PAGE_SIZE_BYTES;
  cfg.spm_xlate_enable = 1;
  cfg.spm_page_shift = 0;
  cfg.spm_xlate_range_base = 0x80000000ULL;
  cfg.spm_xlate_range_size = 0;
  cfg.pages_per_acc = 256;
  cfg.dma_backend = PRT_DMA_BACKEND_POLL_PROGRESS_THREAD;
  cfg.gemmini_mode = PRT_GEMMINI_MODE_ASYNC_EXPERIMENTAL;
  cfg.sync_mode = PRT_SYNC_MODE_ASYNC;
  cfg.watchdog_timeout_ms = 5000;

  for (int i = 1; i < argc; ++i) {
    if (!strcmp(argv[i], "--model-yaml") && i + 1 < argc) {
      args.model_yaml = argv[++i];
    } else if (!strcmp(argv[i], "--model-bin") && i + 1 < argc) {
      args.model_bin = argv[++i];
    } else if (!strcmp(argv[i], "--model-offset") && i + 1 < argc) {
      args.model_offset_bytes = (uint64_t)strtoull(argv[++i], NULL, 0);
    } else if (!strcmp(argv[i], "--pipeline-yaml") && i + 1 < argc) {
      args.pipeline_yaml = argv[++i];
    } else if (!strcmp(argv[i], "--num-cores") && i + 1 < argc) {
      cfg.num_cores = (uint32_t)strtoul(argv[++i], NULL, 10);
    } else if (!strcmp(argv[i], "--num-gemmini-mgrs") && i + 1 < argc) {
      cfg.num_gemmini_mgrs = (uint32_t)strtoul(argv[++i], NULL, 10);
    } else if (!strcmp(argv[i], "--num-dma-mgrs") && i + 1 < argc) {
      cfg.num_dma_mgrs = (uint32_t)strtoul(argv[++i], NULL, 10);
    } else if (!strcmp(argv[i], "--gemmini-base-id") && i + 1 < argc) {
      cfg.gemmini_mgr_base_id = (uint32_t)strtoul(argv[++i], NULL, 10);
    } else if (!strcmp(argv[i], "--dma-base-id") && i + 1 < argc) {
      cfg.dma_mgr_base_id = (uint32_t)strtoul(argv[++i], NULL, 10);
      user_set_dma_base = 1;
    } else if (!strcmp(argv[i], "--sync-mode") && i + 1 < argc) {
      const char *v = argv[++i];
      if (!strcmp(v, "async")) cfg.sync_mode = PRT_SYNC_MODE_ASYNC;
      else if (!strcmp(v, "blocking_debug")) cfg.sync_mode = PRT_SYNC_MODE_BLOCKING_DEBUG;
    } else if (!strcmp(argv[i], "--spm-page-bytes") && i + 1 < argc) {
      cfg.page_size_bytes = (uint32_t)strtoul(argv[++i], NULL, 10);
    } else if (!strcmp(argv[i], "--spm-xlate-enable") && i + 1 < argc) {
      cfg.spm_xlate_enable = (uint32_t)strtoul(argv[++i], NULL, 10) ? 1U : 0U;
    } else if (!strcmp(argv[i], "--spm-xlate-range-base") && i + 1 < argc) {
      cfg.spm_xlate_range_base = (uint64_t)strtoull(argv[++i], NULL, 0);
    } else if (!strcmp(argv[i], "--spm-xlate-range-size") && i + 1 < argc) {
      cfg.spm_xlate_range_size = (uint64_t)strtoull(argv[++i], NULL, 0);
    } else if (!strcmp(argv[i], "--pages-per-acc") && i + 1 < argc) {
      cfg.pages_per_acc = (uint32_t)strtoul(argv[++i], NULL, 10);
    } else if (!strcmp(argv[i], "--hw-validate-only")) {
      cfg.hw_validate_only = 1U;
    } else if (!strcmp(argv[i], "--input") && i + 1 < argc) {
      args.input_path = argv[++i];
    } else if (!strcmp(argv[i], "--golden") && i + 1 < argc) {
      args.golden_path = argv[++i];
    } else if (!strcmp(argv[i], "--batch") && i + 1 < argc) {
      args.batch = (uint32_t)strtoul(argv[++i], NULL, 10);
    } else if (!strcmp(argv[i], "--trace") && i + 1 < argc) {
      cfg.trace_path = argv[++i];
    } else if (!strcmp(argv[i], "--dma-backend") && i + 1 < argc) {
      const char *v = argv[++i];
      if (!strcmp(v, "blocking_fence")) cfg.dma_backend = PRT_DMA_BACKEND_BLOCKING_FENCE;
      else if (!strcmp(v, "poll_progress_thread")) cfg.dma_backend = PRT_DMA_BACKEND_POLL_PROGRESS_THREAD;
    } else if (!strcmp(argv[i], "--gemmini-mode") && i + 1 < argc) {
      const char *v = argv[++i];
      if (!strcmp(v, "blocking_fence")) cfg.gemmini_mode = PRT_GEMMINI_MODE_BLOCKING_FENCE;
      else if (!strcmp(v, "async_experimental")) cfg.gemmini_mode = PRT_GEMMINI_MODE_ASYNC_EXPERIMENTAL;
    } else if (!strcmp(argv[i], "--watchdog-ms") && i + 1 < argc) {
      cfg.watchdog_timeout_ms = (uint32_t)strtoul(argv[++i], NULL, 10);
    } else {
      usage(argv[0]);
      return 2;
    }
  }

  if (cfg.hw_validate_only) {
    return run_hw_validate_only(&cfg);
  }

  if (!args.model_yaml || !args.pipeline_yaml) {
    usage(argv[0]);
    return 2;
  }
  if (!user_set_dma_base) {
    cfg.dma_mgr_base_id = cfg.gemmini_mgr_base_id + cfg.num_gemmini_mgrs;
  }

  rc = prt_runtime_init(&cfg, &rt);
  if (rc != PRT_OK) {
    fprintf(stderr, "runtime_init failed: %s (%d)\\n", prt_err_str(rc), rc);
    return 1;
  }

  rc = prt_runtime_run(&rt, &args);
  if (rc != PRT_OK) {
    fprintf(stderr, "runtime_run failed: %s (%d)\\n", prt_err_str(rc), rc);
  }

  (void)prt_runtime_destroy(&rt);
  return rc == PRT_OK ? 0 : 1;
}
