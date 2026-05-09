# 20260509T104719Z mapping-cache bulk-read loader

## Problem

The no-DMA F2 bisection with `PAIRDUMMY_SBUS64_DISABLE_MAPPING_CACHE=0` avoided
the large YAML layer-mapping read, but then stalled in the mapping cache loader
after:

```text
artifacts mapping cache progress ... entries=2048 elapsed_ms=57
```

The old loader read the cache header once, then issued one `pread` per
`mapping_cache_entry_t`. On F2 this produced thousands of small guest block
image reads and stalled before the runtime reached Gemmini/no-DMA compute.

## Change

Updated `src/prt_gemmini_artifacts.c`:

- changed the generic artifact file loader from offset `pread` chunks to
  sequential `read` chunks after `lseek(fd, 0, SEEK_SET)`
- changed `load_mapping_cache_file()` to bulk-read the `.cache.bin` into memory
  and decode entries from the buffer
- added cache file size validation against
  `sizeof(header) + entry_count * sizeof(mapping_cache_entry_t)`
- changed the cache read-mode progress label to `mode=bulk-read`

## Validation

Host build passed:

```sh
make -C generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime all \
  PIPELINE_RUNTIME_PROGRESS=1 PIPELINE_RUNTIME_MLOCKALL_MODE=0
```

Host no-DMA CPU dry-run for dummy8x8/sbus64/ours2 passed with
`PIPELINE_RUNTIME_DISABLE_MAPPING_CACHE=0` and `PIPELINE_RUNTIME_YAML_LINE_LOG_ENABLE=0`.
The log reached:

```text
artifacts mapping cache read-mode ... mode=bulk-read
artifacts mapping cache progress ... entries=16848
artifacts mapping cache load end ... entries=16848
runtime end run_rc=0 rc=0
```

F2 validation is still pending. The next run must rebuild/reinstall the
FireMarshal image and rerun `infrasetup` before `runworkload`.
