#include "prt_cli.h"
#include "prt_progress.h"
#include "prt_runtime.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#if defined(__linux__) && defined(__riscv)
#include <errno.h>
#include <sys/mman.h>
#endif

#ifndef PRT_MLOCKALL_MODE
#define PRT_MLOCKALL_MODE 0
#endif

#if (PRT_MLOCKALL_MODE < 0) || (PRT_MLOCKALL_MODE > 3)
#error "PRT_MLOCKALL_MODE must be one of: 0=current|future, 1=current-only, 2=disabled, 3=future-only"
#endif

#if PRT_ENABLE_PROGRESS_LOG
static void prt_early_progress(const char *msg) {
  size_t remaining;
  ssize_t written;
  if (!msg) return;
  remaining = strlen(msg);
  while (remaining > 0) {
    written = write(STDERR_FILENO, msg, remaining);
    if (written <= 0) break;
    msg += (size_t)written;
    remaining -= (size_t)written;
  }
  written = write(STDERR_FILENO, "\n", 1);
  (void)written;
}
#else
static void prt_early_progress(const char *msg) {
  (void)msg;
}
#endif

static void prt_enable_live_stdio(void) {
  /* Keep guest logs visible immediately in FireSim/UART captures. */
  (void)setvbuf(stdout, NULL, _IONBF, 0);
  (void)setvbuf(stderr, NULL, _IONBF, 0);
}

#if defined(__linux__) && defined(__riscv)
static int prt_mlockall_flags(void) {
  switch (PRT_MLOCKALL_MODE) {
    case 0: return MCL_CURRENT | MCL_FUTURE;
    case 1: return MCL_CURRENT;
    case 2: return 0;
    case 3: return MCL_FUTURE;
    default: return MCL_CURRENT | MCL_FUTURE;
  }
}

static const char *prt_mlockall_mode_name(void) {
  switch (PRT_MLOCKALL_MODE) {
    case 0: return "current+future";
    case 1: return "current-only";
    case 2: return "disabled";
    case 3: return "future-only";
    default: return "unknown";
  }
}
#endif

static void prt_try_lock_process_memory(void) {
#if defined(__linux__) && defined(__riscv)
  static int attempted = 0;
  const int flags = prt_mlockall_flags();
  const char *mode_name = prt_mlockall_mode_name();
  int rc;
  int saved_errno = 0;
  char msg[128];
  if (attempted) return;
  attempted = 1;
  snprintf(msg, sizeof(msg),
           "[prt-early] process memory lock mode=%s flags=0x%x",
           mode_name, flags);
  prt_early_progress(msg);
  if (flags == 0) {
    prt_early_progress("[prt-early] skip mlockall by build config");
    return;
  }
  prt_early_progress("[prt-early] before mlockall");
  rc = mlockall(flags);
  if (rc != 0) {
    saved_errno = errno;
    snprintf(msg, sizeof(msg),
             "[prt-early] after mlockall rc=%d errno=%d flags=0x%x",
             rc, saved_errno, flags);
    prt_early_progress(msg);
    fprintf(stderr, "warning: mlockall failed: %s\n", strerror(saved_errno));
    return;
  }
  prt_early_progress("[prt-early] after mlockall rc=0");
  snprintf(msg, sizeof(msg),
           "[prt-early] after mlockall mode=%s flags=0x%x",
           mode_name, flags);
  prt_early_progress(msg);
#endif
}

static void usage(const char *prog) {
  fprintf(stderr,
    "Usage: %s --backend <cpu|fpga> --model-yaml <path> --layer-mapping-yaml <path> [--model-bin <path>] [--model-offset <bytes>] [--skip-model-bin-load] --pipeline-yaml <path> [--num-cores <n>] [--num-gemmini-mgrs <n>] [--num-dma-mgrs <n>] [--gemmini-base-id <n>] [--dma-base-id <n>] [--pair-manager-mode <0|1>] [--sync-mode <async|blocking_debug>] [--pages-per-acc <n>] [--spm-page-bytes <n>] [--spm-xlate-enable <0|1>] [--spm-xlate-range-base <hex>] [--spm-xlate-range-size <bytes>] [--spm-pt-pool-prealloc-hugepages <n>] [--spm-pt-pool-max-hugepages <n>] [--spm-pt-require-hugetlb <0|1>] [--watchdog-ms <n>] [--export-dma-timeout-ms <n>] [--hw-validate-only] [--input <path>] [--skip-input-load] [--golden <path>] [--skip-golden-check] [--golden-out <path>] [--batch <n>] [--trace <path>]\\n"
    "       [--deep-log-enable <0|1>] [--deep-log-segment <n>] [--deep-log-global-stage <n>] [--deep-log-local-stage <n>] [--deep-log-subbatch <n>] [--deep-log-stage-radius <n>] [--deep-log-subbatch-radius <n>]\\n"
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

int prt_main_entry(int argc, char **argv) {
  prt_runtime_cfg_t cfg;
  prt_run_args_t args;
  prt_runtime_t rt;
  int rc;
  int user_set_dma_base = 0;
  int backend_set = 0;
  int watchdog_set = 0;

  prt_early_progress("[prt-early] enter main");
  prt_enable_live_stdio();
  prt_early_progress("[prt-early] live stdio ready");
  PRT_CRIT_LOG("main build-config crit_probe=%u marker=%u progress=%u raw=%u hot=%u phase=%u",
               (unsigned)PRT_ENABLE_CRITICAL_UART_PROBE,
               (unsigned)PRT_ENABLE_ONLY_MARKER,
               (unsigned)PRT_ENABLE_PROGRESS_LOG,
               (unsigned)PRT_ENABLE_PROGRESS_RAW_LOG,
               (unsigned)PRT_ENABLE_PROGRESS_HOT_LOG,
               (unsigned)PIPELINE_RUNTIME_GEMMINI_PHASE_LOG);
  PRT_CRIT_LOG("main build-config deep_gate_cap=%u",
               (unsigned)PIPELINE_RUNTIME_DEEP_LOG_GATE);

  memset(&cfg, 0, sizeof(cfg));
  memset(&args, 0, sizeof(args));

  cfg.num_cores = 4;
  cfg.num_gemmini_mgrs = 4;
  cfg.num_dma_mgrs = 4;
  cfg.gemmini_mgr_base_id = 0;
  cfg.dma_mgr_base_id = 0;
  cfg.pair_manager_mode = 0;
  cfg.backend = PRT_BACKEND_FPGA;
  cfg.page_size_bytes = PRT_PAGE_SIZE_BYTES;
  cfg.spm_xlate_enable = 1;
  cfg.spm_page_shift = 0;
  cfg.spm_xlate_range_base = 0;
  cfg.spm_xlate_range_size = 0;
  cfg.pages_per_acc = 256;
  cfg.spm_pt_pool_prealloc_hugepages = 0;
  cfg.spm_pt_pool_max_hugepages = 0;
  cfg.spm_pt_require_hugetlb = 0;
  cfg.dma_backend = PRT_DMA_BACKEND_POLL_PROGRESS_THREAD;
  cfg.gemmini_mode = PRT_GEMMINI_MODE_ASYNC_EXPERIMENTAL;
  cfg.sync_mode = PRT_SYNC_MODE_ASYNC;
  cfg.watchdog_timeout_ms = 5000;
  cfg.export_dma_timeout_ms = 5000;
  cfg.deep_log_gate_enable = 0;
  cfg.deep_log_segment = PRT_LOG_GATE_ANY_U32;
  cfg.deep_log_global_stage = PRT_LOG_GATE_ANY_U32;
  cfg.deep_log_local_stage = PRT_LOG_GATE_ANY_U32;
  cfg.deep_log_subbatch = PRT_LOG_GATE_ANY_U32;
  cfg.deep_log_stage_radius = 0;
  cfg.deep_log_subbatch_radius = 0;
  prt_early_progress("[prt-early] defaults ready");

  for (int i = 1; i < argc; ++i) {
    if (!strcmp(argv[i], "--backend") && i + 1 < argc) {
      const char *v = argv[++i];
      if (!strcmp(v, "fpga")) cfg.backend = PRT_BACKEND_FPGA;
      else if (!strcmp(v, "cpu")) cfg.backend = PRT_BACKEND_CPU;
      else {
        usage(argv[0]);
        return 2;
      }
      backend_set = 1;
    } else if (!strcmp(argv[i], "--model-yaml") && i + 1 < argc) {
      args.model_yaml = argv[++i];
    } else if (!strcmp(argv[i], "--layer-mapping-yaml") && i + 1 < argc) {
      args.layer_mapping_yaml = argv[++i];
    } else if (!strcmp(argv[i], "--model-bin") && i + 1 < argc) {
      args.model_bin = argv[++i];
    } else if (!strcmp(argv[i], "--model-offset") && i + 1 < argc) {
      args.model_offset_bytes = (uint64_t)strtoull(argv[++i], NULL, 0);
    } else if (!strcmp(argv[i], "--skip-model-bin-load")) {
      args.skip_model_bin_load = 1U;
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
    } else if (!strcmp(argv[i], "--pair-manager-mode") && i + 1 < argc) {
      cfg.pair_manager_mode = (uint32_t)strtoul(argv[++i], NULL, 10) ? 1U : 0U;
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
    } else if (!strcmp(argv[i], "--spm-pt-pool-prealloc-hugepages") && i + 1 < argc) {
      cfg.spm_pt_pool_prealloc_hugepages = (uint32_t)strtoul(argv[++i], NULL, 10);
    } else if (!strcmp(argv[i], "--spm-pt-pool-max-hugepages") && i + 1 < argc) {
      cfg.spm_pt_pool_max_hugepages = (uint32_t)strtoul(argv[++i], NULL, 10);
    } else if (!strcmp(argv[i], "--spm-pt-require-hugetlb") && i + 1 < argc) {
      cfg.spm_pt_require_hugetlb = (uint32_t)strtoul(argv[++i], NULL, 10) ? 1U : 0U;
    } else if (!strcmp(argv[i], "--hw-validate-only")) {
      cfg.hw_validate_only = 1U;
    } else if (!strcmp(argv[i], "--input") && i + 1 < argc) {
      args.input_path = argv[++i];
    } else if (!strcmp(argv[i], "--skip-input-load")) {
      args.skip_input_load = 1U;
    } else if (!strcmp(argv[i], "--golden") && i + 1 < argc) {
      args.golden_path = argv[++i];
    } else if (!strcmp(argv[i], "--skip-golden-check")) {
      args.skip_golden_check = 1U;
    } else if (!strcmp(argv[i], "--golden-out") && i + 1 < argc) {
      args.golden_out_path = argv[++i];
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
      watchdog_set = 1;
    } else if (!strcmp(argv[i], "--export-dma-timeout-ms") && i + 1 < argc) {
      cfg.export_dma_timeout_ms = (uint32_t)strtoul(argv[++i], NULL, 10);
    } else if (!strcmp(argv[i], "--deep-log-enable") && i + 1 < argc) {
      cfg.deep_log_gate_enable = (uint32_t)strtoul(argv[++i], NULL, 10) ? 1U : 0U;
    } else if (!strcmp(argv[i], "--deep-log-segment") && i + 1 < argc) {
      cfg.deep_log_segment = (uint32_t)strtoul(argv[++i], NULL, 0);
      cfg.deep_log_gate_enable = 1U;
    } else if (!strcmp(argv[i], "--deep-log-global-stage") && i + 1 < argc) {
      cfg.deep_log_global_stage = (uint32_t)strtoul(argv[++i], NULL, 0);
      cfg.deep_log_gate_enable = 1U;
    } else if (!strcmp(argv[i], "--deep-log-local-stage") && i + 1 < argc) {
      cfg.deep_log_local_stage = (uint32_t)strtoul(argv[++i], NULL, 0);
      cfg.deep_log_gate_enable = 1U;
    } else if (!strcmp(argv[i], "--deep-log-subbatch") && i + 1 < argc) {
      cfg.deep_log_subbatch = (uint32_t)strtoul(argv[++i], NULL, 0);
      cfg.deep_log_gate_enable = 1U;
    } else if (!strcmp(argv[i], "--deep-log-stage-radius") && i + 1 < argc) {
      cfg.deep_log_stage_radius = (uint32_t)strtoul(argv[++i], NULL, 0);
      cfg.deep_log_gate_enable = 1U;
    } else if (!strcmp(argv[i], "--deep-log-subbatch-radius") && i + 1 < argc) {
      cfg.deep_log_subbatch_radius = (uint32_t)strtoul(argv[++i], NULL, 0);
      cfg.deep_log_gate_enable = 1U;
    } else {
      usage(argv[0]);
      return 2;
    }
  }
  prt_early_progress("[prt-early] arg parse done");

  if (cfg.hw_validate_only) {
    prt_early_progress("[prt-early] hw-validate-only");
    return run_hw_validate_only(&cfg);
  }

  if (!backend_set || !args.model_yaml || !args.layer_mapping_yaml || !args.pipeline_yaml) {
    usage(argv[0]);
    return 2;
  }
  prt_early_progress("[prt-early] required args ready");
  if (!user_set_dma_base) {
    if (cfg.pair_manager_mode) {
      cfg.dma_mgr_base_id = cfg.gemmini_mgr_base_id;
    } else {
      cfg.dma_mgr_base_id = cfg.gemmini_mgr_base_id + cfg.num_gemmini_mgrs;
    }
  }
  prt_early_progress("[prt-early] dma base ready");
  if (!watchdog_set && cfg.backend == PRT_BACKEND_CPU) {
    cfg.watchdog_timeout_ms = 300000;
  }
  prt_early_progress("[prt-early] watchdog ready");

  if (cfg.backend == PRT_BACKEND_FPGA) {
    prt_early_progress("[prt-early] before process memory lock");
    prt_try_lock_process_memory();
    prt_early_progress("[prt-early] after process memory lock");
  }

  PRT_MARKER_LOG("main runtime-init-begin backend=%u cores=%u gemmini=%u dma=%u pair=%u gemmini_base=%u dma_base=%u spm_xlate=%u pages_per_acc=%u export_dma_timeout_ms=%u",
                 (uint32_t)cfg.backend, cfg.num_cores, cfg.num_gemmini_mgrs, cfg.num_dma_mgrs,
                 cfg.pair_manager_mode, cfg.gemmini_mgr_base_id, cfg.dma_mgr_base_id,
                 cfg.spm_xlate_enable, cfg.pages_per_acc, cfg.export_dma_timeout_ms);
  prt_early_progress("[prt-early] calling runtime_init");
  rc = prt_runtime_init(&cfg, &rt);
  if (rc != PRT_OK) {
    fprintf(stderr, "runtime_init failed: %s (%d)\\n", prt_err_str(rc), rc);
    return 1;
  }
  PRT_MARKER_LOG("main runtime-init-end backend=%u gemm_mode=%u dma_backend=%u",
                 (uint32_t)rt.cfg.backend, (uint32_t)rt.cfg.gemmini_mode,
                 (uint32_t)rt.cfg.dma_backend);
  prt_early_progress("[prt-early] runtime_init done");

  PRT_MARKER_LOG("main runtime-run-begin");
  prt_early_progress("[prt-early] calling runtime_run");
  rc = prt_runtime_run(&rt, &args);
  PRT_MARKER_LOG("main runtime-run-end rc=%d", rc);
  if (rc != PRT_OK) {
    fprintf(stderr, "runtime_run failed: %s (%d)\\n", prt_err_str(rc), rc);
  }

  (void)prt_runtime_destroy(&rt);
  return rc == PRT_OK ? 0 : 1;
}

#ifndef PRT_NO_MAIN
int main(int argc, char **argv) {
  return prt_main_entry(argc, argv);
}
#endif
