#include "prt_gemmini_artifacts.h"
#include "prt_progress.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define PRT_MAPPING_CACHE_MAGIC0 0x5052544dU
#define PRT_MAPPING_CACHE_MAGIC1 0x43414348U
#define PRT_MAPPING_CACHE_VERSION 1U
#define PRT_MAPPING_CACHE_HEADER_WORDS 8U
#define PRT_MAPPING_CACHE_ENTRY_WORDS 58U
#define PRT_MAPPING_CACHE_SUFFIX ".cache.bin"
#define PRT_MAPPING_PARSE_PROGRESS_INTERVAL 2048U
#define PRT_MAPPING_SCAN_PROGRESS_INTERVAL 2048U

typedef struct {
  uint32_t layer_id;
  uint32_t target_accel;
  uint32_t split_kind;
  uint32_t dram[PRT_MAX_LAYER_TENSORS];
  uint32_t dram_n;
  uint32_t spm[PRT_MAX_LAYER_TENSORS];
  uint32_t spm_n;
  uint32_t spm_addr[PRT_MAX_LAYER_TENSORS];
  uint32_t spm_addr_n;
  uint32_t first_vpage[PRT_MAX_LAYER_TENSORS];
  uint32_t first_vpage_n;
  uint32_t page_count[PRT_MAX_LAYER_TENSORS];
  uint32_t page_count_n;
  uint32_t spm_bytes[PRT_MAX_LAYER_TENSORS];
  uint32_t spm_bytes_n;
  int active;
} mapping_entry_t;

typedef struct {
  mapping_entry_t *entries;
  uint32_t count;
  uint32_t cap;
  size_t file_size_bytes;
} mapping_db_t;

typedef struct {
  uint32_t magic0;
  uint32_t magic1;
  uint32_t version;
  uint32_t max_layer_tensors;
  uint32_t entry_count;
  uint32_t entry_words;
  uint32_t source_bytes;
  uint32_t reserved0;
} mapping_cache_header_t;

typedef struct {
  uint32_t layer_id;
  uint32_t target_accel;
  uint32_t split_kind;
  uint32_t dram_n;
  uint32_t dram[PRT_MAX_LAYER_TENSORS];
  uint32_t spm_n;
  uint32_t spm[PRT_MAX_LAYER_TENSORS];
  uint32_t spm_addr_n;
  uint32_t spm_addr[PRT_MAX_LAYER_TENSORS];
  uint32_t first_vpage_n;
  uint32_t first_vpage[PRT_MAX_LAYER_TENSORS];
  uint32_t page_count_n;
  uint32_t page_count[PRT_MAX_LAYER_TENSORS];
  uint32_t spm_bytes_n;
  uint32_t spm_bytes[PRT_MAX_LAYER_TENSORS];
  uint32_t active;
} mapping_cache_entry_t;

typedef char prt_mapping_cache_header_size_must_match[
  sizeof(mapping_cache_header_t) == PRT_MAPPING_CACHE_HEADER_WORDS * sizeof(uint32_t) ? 1 : -1];
typedef char prt_mapping_cache_entry_size_must_match[
  sizeof(mapping_cache_entry_t) == PRT_MAPPING_CACHE_ENTRY_WORDS * sizeof(uint32_t) ? 1 : -1];

static uint64_t monotonic_ms(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (uint64_t)ts.tv_sec * 1000ULL + (uint64_t)ts.tv_nsec / 1000000ULL;
}

static int load_file(const char *path, char **out_buf, size_t *out_len) {
  FILE *f;
  long sz;
  char *buf;
  if (!path || !out_buf) return PRT_ERR_INVAL;
  f = fopen(path, "rb");
  if (!f) return PRT_ERR_IO;
  if (fseek(f, 0, SEEK_END) != 0) {
    fclose(f);
    return PRT_ERR_IO;
  }
  sz = ftell(f);
  if (sz < 0) {
    fclose(f);
    return PRT_ERR_IO;
  }
  if (fseek(f, 0, SEEK_SET) != 0) {
    fclose(f);
    return PRT_ERR_IO;
  }
  buf = (char *)malloc((size_t)sz + 1U);
  if (!buf) {
    fclose(f);
    return PRT_ERR_NOMEM;
  }
  if (fread(buf, 1, (size_t)sz, f) != (size_t)sz) {
    free(buf);
    fclose(f);
    return PRT_ERR_IO;
  }
  buf[sz] = '\0';
  fclose(f);
  *out_buf = buf;
  if (out_len) *out_len = (size_t)sz;
  return PRT_OK;
}

static char *ltrim(char *s) {
  while (*s && isspace((unsigned char)*s)) s++;
  return s;
}

static void rtrim(char *s) {
  size_t n;
  if (!s) return;
  n = strlen(s);
  while (n > 0 && isspace((unsigned char)s[n - 1])) {
    s[n - 1] = '\0';
    n--;
  }
}

static int starts_key(char *line, const char *key) {
  size_t n;
  char *p = ltrim(line);
  n = strlen(key);
  return strncmp(p, key, n) == 0 && p[n] == ':';
}

static char *value_after_colon(char *line) {
  char *p = strchr(line, ':');
  if (!p) return NULL;
  p++;
  while (*p && isspace((unsigned char)*p)) p++;
  return p;
}

static int parse_u32_scalar(char *line, uint32_t *out) {
  char *v = value_after_colon(line);
  unsigned long x;
  if (!v || !out) return PRT_ERR_PARSE;
  x = strtoul(v, NULL, 10);
  *out = (uint32_t)x;
  return PRT_OK;
}

static int parse_u32_list_from_value(char *v, uint32_t *out, uint32_t *out_n, uint32_t cap) {
  uint32_t n = 0;
  char *p;
  if (!v || !out || !out_n || cap == 0) return PRT_ERR_INVAL;
  p = strchr(v, '[');
  if (!p) {
    *out_n = 0;
    return PRT_OK;
  }
  p++;
  while (*p && *p != ']') {
    while (*p && (isspace((unsigned char)*p) || *p == ',')) p++;
    if (*p == ']') break;
    if (!(isdigit((unsigned char)*p) || *p == '-')) {
      p++;
      continue;
    }
    if (n >= cap) return PRT_ERR_PARSE;
    out[n++] = (uint32_t)strtoul(p, &p, 10);
  }
  *out_n = n;
  return PRT_OK;
}

static uint32_t parse_split_kind_value(char *v) {
  size_t n;
  if (!v) return PRT_LAYER_SPLIT_UNSPEC;
  if (*v == '"' || *v == '\'') {
    char q = *v;
    char *e;
    v++;
    e = strchr(v, q);
    if (e) *e = '\0';
  }
  n = strlen(v);
  while (n > 0 && isspace((unsigned char)v[n - 1])) {
    v[n - 1] = '\0';
    n--;
  }
  if (!strcmp(v, "single")) return PRT_LAYER_SPLIT_SINGLE;
  if (!strcmp(v, "oc")) return PRT_LAYER_SPLIT_OC;
  if (!strcmp(v, "spatial")) return PRT_LAYER_SPLIT_SPATIAL;
  if (!strcmp(v, "resadd_spatial")) return PRT_LAYER_SPLIT_RESADD_SPATIAL;
  return PRT_LAYER_SPLIT_UNSPEC;
}

static int arrays_equal(const uint32_t *a, uint32_t a_n, const uint32_t *b, uint32_t b_n) {
  if (a_n != b_n) return 0;
  for (uint32_t i = 0; i < a_n; ++i) {
    if (a[i] != b[i]) return 0;
  }
  return 1;
}

static void mapping_entry_reset(mapping_entry_t *entry) {
  if (!entry) return;
  memset(entry, 0, sizeof(*entry));
}

static void mapping_entry_copy_array(uint32_t *dst, uint32_t *dst_n,
                                     const uint32_t *src, uint32_t src_n) {
  if (!dst || !dst_n) return;
  *dst_n = 0;
  if (!src || src_n > PRT_MAX_LAYER_TENSORS) return;
  if (src_n > 0) memcpy(dst, src, (size_t)src_n * sizeof(*dst));
  *dst_n = src_n;
}

static void mapping_db_reset(mapping_db_t *db) {
  if (!db) return;
  free(db->entries);
  db->entries = NULL;
  db->count = 0;
  db->cap = 0;
  db->file_size_bytes = 0;
}

static int mapping_db_reserve(mapping_db_t *db, uint32_t need_count) {
  mapping_entry_t *next_entries;
  uint32_t next_cap;
  if (!db) return PRT_ERR_INVAL;
  if (need_count <= db->cap) return PRT_OK;

  next_cap = db->cap ? db->cap : 256U;
  while (next_cap < need_count) {
    uint32_t grown = next_cap * 2U;
    if (grown < next_cap) return PRT_ERR_NOMEM;
    next_cap = grown;
  }

  next_entries = (mapping_entry_t *)realloc(db->entries, (size_t)next_cap * sizeof(*next_entries));
  if (!next_entries) return PRT_ERR_NOMEM;
  db->entries = next_entries;
  db->cap = next_cap;
  return PRT_OK;
}

static int mapping_db_append(mapping_db_t *db, const mapping_entry_t *entry) {
  int rc;
  if (!db || !entry) return PRT_ERR_INVAL;
  rc = mapping_db_reserve(db, db->count + 1U);
  if (rc != PRT_OK) return rc;
  db->entries[db->count++] = *entry;
  return PRT_OK;
}

static char *mapping_cache_path_from_yaml(const char *path) {
  const size_t path_len = path ? strlen(path) : 0U;
  const size_t suffix_len = sizeof(PRT_MAPPING_CACHE_SUFFIX) - 1U;
  char *cache_path;

  if (!path || path_len == 0U) return NULL;
  cache_path = (char *)malloc(path_len + suffix_len + 1U);
  if (!cache_path) return NULL;
  memcpy(cache_path, path, path_len);
  memcpy(cache_path + path_len, PRT_MAPPING_CACHE_SUFFIX, suffix_len + 1U);
  return cache_path;
}

static int mapping_entry_from_cache(mapping_entry_t *entry, const mapping_cache_entry_t *cached) {
  if (!entry || !cached) return PRT_ERR_INVAL;
  if (cached->dram_n > PRT_MAX_LAYER_TENSORS ||
      cached->spm_n > PRT_MAX_LAYER_TENSORS ||
      cached->spm_addr_n > PRT_MAX_LAYER_TENSORS ||
      cached->first_vpage_n > PRT_MAX_LAYER_TENSORS ||
      cached->page_count_n > PRT_MAX_LAYER_TENSORS ||
      cached->spm_bytes_n > PRT_MAX_LAYER_TENSORS) {
    return PRT_ERR_PARSE;
  }

  mapping_entry_reset(entry);
  entry->layer_id = cached->layer_id;
  entry->target_accel = cached->target_accel;
  entry->split_kind = cached->split_kind;
  mapping_entry_copy_array(entry->dram, &entry->dram_n, cached->dram, cached->dram_n);
  mapping_entry_copy_array(entry->spm, &entry->spm_n, cached->spm, cached->spm_n);
  mapping_entry_copy_array(entry->spm_addr, &entry->spm_addr_n, cached->spm_addr, cached->spm_addr_n);
  mapping_entry_copy_array(entry->first_vpage, &entry->first_vpage_n,
                           cached->first_vpage, cached->first_vpage_n);
  mapping_entry_copy_array(entry->page_count, &entry->page_count_n,
                           cached->page_count, cached->page_count_n);
  mapping_entry_copy_array(entry->spm_bytes, &entry->spm_bytes_n,
                           cached->spm_bytes, cached->spm_bytes_n);
  entry->active = cached->active ? 1 : 0;
  return PRT_OK;
}

static int load_mapping_cache_file(const char *cache_path, mapping_db_t *db) {
  FILE *f = NULL;
  mapping_cache_header_t header;
  uint64_t load_start_ms;
  uint64_t parse_start_ms;
  int rc = PRT_ERR_IO;

  if (!cache_path || !db) return PRT_ERR_INVAL;
  load_start_ms = monotonic_ms();
  (void)load_start_ms;
  PRT_PROGRESS_LOG("artifacts mapping cache load begin cache=%s", cache_path);

  f = fopen(cache_path, "rb");
  if (!f) return PRT_ERR_IO;
  if (fread(&header, sizeof(header), 1, f) != 1U) {
    rc = PRT_ERR_PARSE;
    goto out;
  }
  if (header.magic0 != PRT_MAPPING_CACHE_MAGIC0 ||
      header.magic1 != PRT_MAPPING_CACHE_MAGIC1 ||
      header.version != PRT_MAPPING_CACHE_VERSION ||
      header.max_layer_tensors != PRT_MAX_LAYER_TENSORS ||
      header.entry_words != PRT_MAPPING_CACHE_ENTRY_WORDS) {
    rc = PRT_ERR_PARSE;
    goto out;
  }

  db->file_size_bytes = header.source_bytes;
  rc = mapping_db_reserve(db, header.entry_count);
  if (rc != PRT_OK) goto out;

  parse_start_ms = monotonic_ms();
  (void)parse_start_ms;
  for (uint32_t entry_idx = 0; entry_idx < header.entry_count; ++entry_idx) {
    mapping_cache_entry_t cached;
    mapping_entry_t entry;
    if (fread(&cached, sizeof(cached), 1, f) != 1U) {
      rc = PRT_ERR_PARSE;
      goto out;
    }
    rc = mapping_entry_from_cache(&entry, &cached);
    if (rc != PRT_OK) goto out;
    rc = mapping_db_append(db, &entry);
    if (rc != PRT_OK) goto out;
    if ((db->count % PRT_MAPPING_PARSE_PROGRESS_INTERVAL) == 0U ||
        db->count == header.entry_count) {
      PRT_PROGRESS_LOG("artifacts mapping cache progress cache=%s entries=%u elapsed_ms=%llu",
                       cache_path, db->count,
                       (unsigned long long)(monotonic_ms() - parse_start_ms));
    }
  }

  PRT_PROGRESS_LOG("artifacts mapping cache load end cache=%s source_bytes=%zu entries=%u elapsed_ms=%llu",
                   cache_path, db->file_size_bytes, db->count,
                   (unsigned long long)(monotonic_ms() - load_start_ms));
  rc = PRT_OK;

out:
  if (f) fclose(f);
  if (rc != PRT_OK) {
    mapping_db_reset(db);
    fprintf(stderr, "mapping cache load failed: cache=%s rc=%d\n", cache_path, rc);
  }
  return rc;
}

static uint32_t count_mapping_entries_in_buf(const char *buf) {
  uint32_t count = 0;
  const char *p;

  if (!buf) return 0;
  p = buf;
  while (*p) {
    const char *line = p;
    const char *nl = strchr(p, '\n');
    while (*line && isspace((unsigned char)*line)) line++;
    if (strncmp(line, "- layer_id:", 11) == 0) count++;
    if (!nl) break;
    p = nl + 1;
  }
  return count;
}

static int mapping_entry_matches(const mapping_entry_t *entry, const prt_stage_map_t *stage) {
  if (!entry || !entry->active || !stage) return 0;
  if (entry->layer_id != stage->layer_id) return 0;
  if (entry->target_accel != stage->acc_util) return 0;
  if (!arrays_equal(entry->dram, entry->dram_n, stage->dram_bypass, stage->dram_bypass_count)) return 0;
  if (!arrays_equal(entry->spm, entry->spm_n, stage->spm_bypass, stage->spm_bypass_count)) return 0;
  if (stage->split_kind != PRT_LAYER_SPLIT_UNSPEC &&
      entry->split_kind != PRT_LAYER_SPLIT_UNSPEC &&
      stage->split_kind != entry->split_kind) {
    return 0;
  }
  if (stage->tensor_id_count > 0) {
    if (entry->spm_addr_n != stage->tensor_id_count ||
        entry->first_vpage_n != stage->tensor_id_count ||
        entry->page_count_n != stage->tensor_id_count ||
        entry->spm_bytes_n != stage->tensor_id_count) {
      return 0;
    }
  }
  return 1;
}

static void stage_apply_mapping(prt_stage_map_t *stage, const mapping_entry_t *entry) {
  uint32_t span = 0;
  if (!stage || !entry) return;
  stage->local_spm_tensor_count = entry->spm_addr_n;
  for (uint32_t i = 0; i < entry->spm_addr_n && i < PRT_MAX_LAYER_TENSORS; ++i) {
    uint32_t end_vpage = entry->first_vpage[i] + entry->page_count[i];
    stage->local_spm_tensor_addr[i] = entry->spm_addr[i];
    stage->local_spm_first_vpage[i] = entry->first_vpage[i];
    stage->local_spm_page_count[i] = entry->page_count[i];
    stage->local_spm_tensor_bytes[i] = entry->spm_bytes[i];
    if (end_vpage > span) span = end_vpage;
  }
  stage->local_spm_page_span = span;
}

static int parse_mapping_file(const char *path, mapping_db_t *db) {
  char *buf = NULL;
  char *cache_path = NULL;
  char *p;
  size_t buf_len = 0;
  uint32_t estimated_entries;
  mapping_entry_t cur;
  int rc;
  uint64_t load_start_ms;
  uint64_t parse_start_ms;

  if (!path || !db) return PRT_ERR_INVAL;
  (void)load_start_ms;
  (void)parse_start_ms;
  cache_path = mapping_cache_path_from_yaml(path);
  if (cache_path) {
    rc = load_mapping_cache_file(cache_path, db);
    if (rc == PRT_OK) {
      free(cache_path);
      return PRT_OK;
    }
    PRT_PROGRESS_LOG("artifacts mapping cache fallback layer_mapping=%s cache=%s rc=%d",
                     path, cache_path, rc);
    free(cache_path);
  }

  load_start_ms = monotonic_ms();
  PRT_PROGRESS_LOG("artifacts mapping load begin layer_mapping=%s", path);
  rc = load_file(path, &buf, &buf_len);
  if (rc != PRT_OK) {
    fprintf(stderr, "failed to load layer mapping file %s rc=%d\n", path, rc);
    return rc;
  }
  PRT_PROGRESS_LOG("artifacts mapping load end layer_mapping=%s bytes=%zu elapsed_ms=%llu",
                   path, buf_len,
                   (unsigned long long)(monotonic_ms() - load_start_ms));

  db->file_size_bytes = buf_len;
  estimated_entries = count_mapping_entries_in_buf(buf);
  if (estimated_entries > 0) {
    rc = mapping_db_reserve(db, estimated_entries);
    if (rc != PRT_OK) {
      free(buf);
      return rc;
    }
  }
  PRT_PROGRESS_LOG("artifacts mapping parse begin layer_mapping=%s estimated_entries=%u",
                   path, estimated_entries);
  mapping_entry_reset(&cur);
  parse_start_ms = monotonic_ms();
  p = buf;
  while (p && *p) {
    char *line = p;
    char *nl = strchr(p, '\n');
    char *t;
    if (nl) {
      *nl = '\0';
      p = nl + 1;
    } else {
      p = NULL;
    }
    rtrim(line);
    t = ltrim(line);
    if (*t == '\0' || *t == '#') continue;

    if (strncmp(t, "- layer_id:", 11) == 0) {
      if (cur.active) {
        rc = mapping_db_append(db, &cur);
        if (rc != PRT_OK) {
          free(buf);
          return rc;
        }
        if ((db->count % PRT_MAPPING_PARSE_PROGRESS_INTERVAL) == 0U) {
          PRT_PROGRESS_LOG("artifacts mapping parse progress layer_mapping=%s entries=%u elapsed_ms=%llu",
                           path, db->count,
                           (unsigned long long)(monotonic_ms() - parse_start_ms));
        }
      }
      mapping_entry_reset(&cur);
      cur.active = 1;
      if (parse_u32_scalar(t, &cur.layer_id) != PRT_OK) {
        free(buf);
        return PRT_ERR_PARSE;
      }
      continue;
    }

    if (!cur.active) continue;

    if (starts_key(t, "target_accel")) {
      if (parse_u32_scalar(t, &cur.target_accel) != PRT_OK) {
        free(buf);
        return PRT_ERR_PARSE;
      }
      continue;
    }

    if (starts_key(t, "mapping_dram_bypass")) {
      if (parse_u32_list_from_value(value_after_colon(t), cur.dram, &cur.dram_n,
                                    PRT_MAX_LAYER_TENSORS) != PRT_OK) {
        free(buf);
        return PRT_ERR_PARSE;
      }
      continue;
    }

    if (starts_key(t, "mapping_spm_bypass")) {
      if (parse_u32_list_from_value(value_after_colon(t), cur.spm, &cur.spm_n,
                                    PRT_MAX_LAYER_TENSORS) != PRT_OK) {
        free(buf);
        return PRT_ERR_PARSE;
      }
      continue;
    }

    if (starts_key(t, "split_kind")) {
      cur.split_kind = parse_split_kind_value(value_after_colon(t));
      continue;
    }

    if (starts_key(t, "others_spm_tensor_addr")) {
      if (parse_u32_list_from_value(value_after_colon(t), cur.spm_addr, &cur.spm_addr_n,
                                    PRT_MAX_LAYER_TENSORS) != PRT_OK) {
        free(buf);
        return PRT_ERR_PARSE;
      }
      continue;
    }

    if (starts_key(t, "others_first_tensor_page_num")) {
      if (parse_u32_list_from_value(value_after_colon(t), cur.first_vpage, &cur.first_vpage_n,
                                    PRT_MAX_LAYER_TENSORS) != PRT_OK) {
        free(buf);
        return PRT_ERR_PARSE;
      }
      continue;
    }

    if (starts_key(t, "others_spm_tensor_page_count")) {
      if (parse_u32_list_from_value(value_after_colon(t), cur.page_count, &cur.page_count_n,
                                    PRT_MAX_LAYER_TENSORS) != PRT_OK) {
        free(buf);
        return PRT_ERR_PARSE;
      }
      continue;
    }

    if (starts_key(t, "others_spm_tensor_util")) {
      if (parse_u32_list_from_value(value_after_colon(t), cur.spm_bytes, &cur.spm_bytes_n,
                                    PRT_MAX_LAYER_TENSORS) != PRT_OK) {
        free(buf);
        return PRT_ERR_PARSE;
      }
      continue;
    }
  }

  if (cur.active) {
    rc = mapping_db_append(db, &cur);
    if (rc != PRT_OK) {
      free(buf);
      return rc;
    }
    if ((db->count % PRT_MAPPING_PARSE_PROGRESS_INTERVAL) == 0U ||
        db->count == estimated_entries) {
      PRT_PROGRESS_LOG("artifacts mapping parse progress layer_mapping=%s entries=%u elapsed_ms=%llu",
                       path, db->count,
                       (unsigned long long)(monotonic_ms() - parse_start_ms));
    }
  }

  PRT_PROGRESS_LOG("artifacts mapping parse done layer_mapping=%s entries=%u elapsed_ms=%llu",
                   path, db->count,
                   (unsigned long long)(monotonic_ms() - parse_start_ms));
  free(buf);
  return PRT_OK;
}

static int validate_stage_against_mapping_entries(const char *path, const mapping_db_t *db,
                                                  uint32_t seg_idx, uint32_t stage_idx,
                                                  prt_stage_map_t *stage) {
  int matches = 0;
  uint32_t matched_split_kind = PRT_LAYER_SPLIT_UNSPEC;
  mapping_entry_t matched;
  uint64_t start_ms;

  if (!path || !db || !stage) return PRT_ERR_INVAL;
  (void)seg_idx;
  (void)stage_idx;
  mapping_entry_reset(&matched);
  start_ms = monotonic_ms();
  (void)start_ms;

  PRT_PROGRESS_LOG("artifacts validate stage-begin seg=%u local_stage=%u global_stage=%u layer=%u acc=%u entries=%u",
                   seg_idx, stage_idx, stage->stage_id, stage->layer_id, stage->acc_util, db->count);

  for (uint32_t entry_idx = 0; entry_idx < db->count; ++entry_idx) {
    const mapping_entry_t *entry = &db->entries[entry_idx];
    if (mapping_entry_matches(entry, stage)) {
      matches += 1;
      matched_split_kind = entry->split_kind;
      matched = *entry;
      PRT_PROGRESS_LOG("artifacts validate stage-hit seg=%u local_stage=%u global_stage=%u layer=%u entry=%u matches=%d split_kind=%u",
                       seg_idx, stage_idx, stage->stage_id, stage->layer_id,
                       entry_idx, matches, matched_split_kind);
    }
    if (((entry_idx + 1U) % PRT_MAPPING_SCAN_PROGRESS_INTERVAL) == 0U) {
      PRT_PROGRESS_LOG("artifacts validate stage-scan seg=%u local_stage=%u global_stage=%u scanned=%u/%u elapsed_ms=%llu matches=%d",
                       seg_idx, stage_idx, stage->stage_id, entry_idx + 1U, db->count,
                       (unsigned long long)(monotonic_ms() - start_ms), matches);
    }
  }
  if (matches != 1) {
    fprintf(stderr,
            "layer mapping match failure: file=%s layer=%u stage=%u accUtil=%u matches=%d dramBypassCount=%u spmBypassCount=%u\n",
            path, stage->layer_id, stage->stage_id, stage->acc_util, matches,
            stage->dram_bypass_count, stage->spm_bypass_count);
    return PRT_ERR_NOT_READY;
  }
  if (matched_split_kind == PRT_LAYER_SPLIT_UNSPEC) {
    fprintf(stderr, "layer mapping missing split_kind: file=%s layer=%u stage=%u\n",
            path, stage->layer_id, stage->stage_id);
    return PRT_ERR_PARSE;
  }
  if (stage->split_kind == PRT_LAYER_SPLIT_UNSPEC) {
    stage->split_kind = matched_split_kind;
  } else if (stage->split_kind != matched_split_kind) {
    fprintf(stderr,
            "split_kind mismatch: file=%s layer=%u stage=%u pipeline=%u mapping=%u\n",
            path, stage->layer_id, stage->stage_id, stage->split_kind, matched_split_kind);
    return PRT_ERR_PARSE;
  }
  stage_apply_mapping(stage, &matched);
  PRT_PROGRESS_LOG("artifacts validate stage-end seg=%u local_stage=%u global_stage=%u layer=%u elapsed_ms=%llu span_pages=%u tensors=%u",
                   seg_idx, stage_idx, stage->stage_id, stage->layer_id,
                   (unsigned long long)(monotonic_ms() - start_ms),
                   stage->local_spm_page_span, stage->local_spm_tensor_count);
  return PRT_OK;
}

int prt_validate_gemmini_artifacts(const char *model_yaml, const char *layer_mapping_yaml,
                                   prt_pipeline_desc_t *pipeline) {
  mapping_db_t db = {0};
  int rc;
  uint64_t parse_start_ms;
  if (!model_yaml || !layer_mapping_yaml || !pipeline) return PRT_ERR_INVAL;
  PRT_PROGRESS_LOG("artifacts validate begin model=%s layer_mapping=%s segments=%u",
                   model_yaml, layer_mapping_yaml, pipeline->num_segments);

  parse_start_ms = monotonic_ms();
  (void)parse_start_ms;
  rc = parse_mapping_file(layer_mapping_yaml, &db);
  if (rc != PRT_OK) goto out;
  PRT_PROGRESS_LOG("artifacts mapping parse end layer_mapping=%s bytes=%zu entries=%u elapsed_ms=%llu",
                   layer_mapping_yaml, db.file_size_bytes, db.count,
                   (unsigned long long)(monotonic_ms() - parse_start_ms));

  for (uint32_t seg_idx = 0; seg_idx < pipeline->num_segments; ++seg_idx) {
    prt_segment_desc_t *seg = &pipeline->segments[seg_idx];
    uint64_t seg_start_ms = monotonic_ms();
    (void)seg_start_ms;
    PRT_PROGRESS_LOG("artifacts validate segment-begin seg=%u stages=%u",
                     seg_idx, seg->num_stages);
    for (uint32_t stage_idx = 0; stage_idx < seg->num_stages; ++stage_idx) {
      prt_stage_map_t *stage = &seg->stages[stage_idx];
      if (stage->layer_count != 1U) {
        fprintf(stderr, "stage contract violation: seg=%u stage=%u layer_count=%u\n",
                seg_idx, stage_idx, stage->layer_count);
        rc = PRT_ERR_NOT_IMPL;
        goto out;
      }
      if (stage->tensor_id_count == 0U) {
        fprintf(stderr, "stage missing tensorIdList: seg=%u stage=%u layer=%u\n",
                seg_idx, stage_idx, stage->layer_id);
        rc = PRT_ERR_PARSE;
        goto out;
      }
      if (stage->dram_bypass_count == 0U || stage->spm_bypass_count == 0U) {
        fprintf(stderr,
                "stage missing bypass metadata: seg=%u stage=%u layer=%u dram=%u spm=%u\n",
                seg_idx, stage_idx, stage->layer_id,
                stage->dram_bypass_count, stage->spm_bypass_count);
        rc = PRT_ERR_PARSE;
        goto out;
      }
      if (!stage->virtual_acc_ids_present || stage->num_virtual_acc_ids != stage->acc_util) {
        fprintf(stderr,
                "stage missing vAccIdxList: seg=%u stage=%u layer=%u accUtil=%u vIds=%u\n",
                seg_idx, stage_idx, stage->layer_id, stage->acc_util, stage->num_virtual_acc_ids);
        rc = PRT_ERR_PARSE;
        goto out;
      }
      if (stage->physical_acc_ids_present && stage->num_physical_acc_ids != stage->acc_util) {
        fprintf(stderr,
                "stage invalid explicit physical binding: seg=%u stage=%u layer=%u accUtil=%u ids=%u\n",
                seg_idx, stage_idx, stage->layer_id, stage->acc_util, stage->num_physical_acc_ids);
        rc = PRT_ERR_PARSE;
        goto out;
      }
      rc = validate_stage_against_mapping_entries(layer_mapping_yaml, &db, seg_idx, stage_idx, stage);
      if (rc != PRT_OK) goto out;
    }
    PRT_PROGRESS_LOG("artifacts validate segment-end seg=%u stages=%u elapsed_ms=%llu",
                     seg_idx, seg->num_stages,
                     (unsigned long long)(monotonic_ms() - seg_start_ms));
  }
  PRT_PROGRESS_LOG("artifacts validate end layer_mapping=%s segments=%u",
                   layer_mapping_yaml, pipeline->num_segments);
  rc = PRT_OK;
out:
  mapping_db_reset(&db);
  return rc;
}
