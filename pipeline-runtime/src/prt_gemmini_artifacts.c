#include "prt_gemmini_artifacts.h"
#include "prt_debug_state.h"
#include "prt_progress.h"

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

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

static int read_full_pread_at(int fd, char *buf, size_t len, off_t base_off) {
  size_t off = 0U;

  while (off < len) {
    size_t chunk = len - off;
    ssize_t n;

    if (chunk > (1U << 20)) chunk = (1U << 20);
    n = pread(fd, buf + off, chunk, base_off + (off_t)off);
    if (n > 0) {
      off += (size_t)n;
      continue;
    }
    if (n == 0) return PRT_ERR_IO;
    if (n < 0 && errno == EINTR) continue;
    return PRT_ERR_IO;
  }

  return PRT_OK;
}

static int __attribute__((unused)) read_full_pread(int fd, char *buf, size_t len) {
  return read_full_pread_at(fd, buf, len, 0);
}

static uint32_t prt_env_u32_default(const char *name, uint32_t default_value) {
  const char *value = getenv(name);
  char *end = NULL;
  unsigned long parsed;

  if (!value || !*value) return default_value;
  errno = 0;
  parsed = strtoul(value, &end, 10);
  if (errno != 0 || end == value) return default_value;
  if (parsed > 0xffffffffUL) return default_value;
  return (uint32_t)parsed;
}

static int parse_u32_token_advance(char *p, char **out_next, uint32_t *out) {
  char *end = NULL;
  unsigned long x;

  if (!p || !out_next || !out) return PRT_ERR_INVAL;
  errno = 0;
  x = strtoul(p, &end, 10);
  if (end == p) return PRT_ERR_PARSE;
  if (errno != 0 || x > 0xffffffffUL) return PRT_ERR_PARSE;
  *out = (uint32_t)x;
  *out_next = end;
  return PRT_OK;
}

static int load_file(const char *path, char **out_buf, size_t *out_len) {
  int fd = -1;
  off_t sz_off;
  size_t sz;
  char *buf;
  size_t off = 0U;
  if (!path || !out_buf) return PRT_ERR_INVAL;

  PRT_PROGRESS_LOG("artifacts file open begin path=%s", path);
  fd = open(path, O_RDONLY | O_CLOEXEC);
  if (fd < 0) {
    PRT_PROGRESS_LOG("artifacts file open fail path=%s errno=%d", path, errno);
    return PRT_ERR_IO;
  }
  PRT_PROGRESS_LOG("artifacts file open end path=%s", path);

  PRT_PROGRESS_LOG("artifacts file seek-end begin path=%s", path);
  sz_off = lseek(fd, 0, SEEK_END);
  if (sz_off < 0) {
    PRT_PROGRESS_LOG("artifacts file seek-end fail path=%s errno=%d", path, errno);
    close(fd);
    return PRT_ERR_IO;
  }
  PRT_PROGRESS_LOG("artifacts file seek-end end path=%s", path);
  if ((uint64_t)sz_off > (uint64_t)(SIZE_MAX - 1U)) {
    PRT_PROGRESS_LOG("artifacts file size overflow path=%s bytes=%lld", path, (long long)sz_off);
    close(fd);
    return PRT_ERR_IO;
  }
  sz = (size_t)sz_off;
  PRT_PROGRESS_LOG("artifacts file size path=%s bytes=%zu", path, sz);

  PRT_PROGRESS_LOG("artifacts file alloc begin path=%s bytes=%zu", path, sz + 1U);
  buf = (char *)malloc(sz + 1U);
  if (!buf) {
    PRT_PROGRESS_LOG("artifacts file alloc fail path=%s bytes=%zu", path, sz + 1U);
    close(fd);
    return PRT_ERR_NOMEM;
  }
  PRT_PROGRESS_LOG("artifacts file alloc end path=%s bytes=%zu", path, sz + 1U);

  PRT_PROGRESS_LOG("artifacts file read begin path=%s bytes=%zu", path, sz);
  if (lseek(fd, 0, SEEK_SET) < 0) {
    PRT_PROGRESS_LOG("artifacts file seek-begin fail path=%s errno=%d", path, errno);
    free(buf);
    close(fd);
    return PRT_ERR_IO;
  }
  while (off < sz) {
    size_t chunk = sz - off;
    ssize_t n;

    if (chunk > (1U << 20)) chunk = (1U << 20);
    PRT_PROGRESS_LOG("artifacts file read chunk-begin path=%s off=%zu chunk=%zu", path, off, chunk);
    n = read(fd, buf + off, chunk);
    if (n > 0) {
      off += (size_t)n;
      PRT_PROGRESS_LOG("artifacts file read chunk-end path=%s off=%zu read=%lld",
                       path, off, (long long)n);
      continue;
    }
    if (n < 0 && errno == EINTR) {
      PRT_PROGRESS_LOG("artifacts file read chunk-retry path=%s off=%zu errno=%d",
                       path, off, errno);
      continue;
    }
    PRT_PROGRESS_LOG("artifacts file read fail path=%s off=%zu rc=%lld errno=%d",
                     path, off, (long long)n, errno);
    free(buf);
    close(fd);
    return PRT_ERR_IO;
  }
  PRT_PROGRESS_LOG("artifacts file read end path=%s bytes=%zu", path, sz);
  buf[sz] = '\0';
  PRT_PROGRESS_LOG("artifacts file close begin path=%s", path);
  close(fd);
  PRT_PROGRESS_LOG("artifacts file close end path=%s", path);
  *out_buf = buf;
  if (out_len) *out_len = sz;
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
  char *end = NULL;
  if (!v || !out) return PRT_ERR_PARSE;
  return parse_u32_token_advance(v, &end, out);
}

static int parse_u32_list_from_value(char *v, uint32_t *out, uint32_t *out_n, uint32_t cap) {
  uint32_t n = 0;
  char *p;
  int rc;
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
    rc = parse_u32_token_advance(p, &p, &out[n]);
    if (rc != PRT_OK) return rc;
    n++;
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

static uint32_t u32_map_lookup_value(const prt_u32_map_t *map, uint32_t key, int *out_found) {
  if (out_found) *out_found = 0;
  if (!map || !map->data) return 0;
  for (uint32_t i = 0; i < map->size; ++i) {
    if (map->data[i].key == key) {
      if (out_found) *out_found = 1;
      return map->data[i].value;
    }
  }
  return 0;
}

static int stage_find_tensor_slot_local(const prt_stage_map_t *stage, uint32_t tensor_id, uint32_t *out_slot) {
  if (!stage || !out_slot) return PRT_ERR_INVAL;
  for (uint32_t i = 0; i < stage->tensor_id_count; ++i) {
    if (stage->tensor_ids[i] == tensor_id) {
      *out_slot = i;
      return PRT_OK;
    }
  }
  return PRT_ERR_NOT_READY;
}

static uint32_t stage_local_tensor_bytes_with_fallback(const prt_stage_map_t *stage, uint32_t slot) {
  uint32_t bytes;
  if (!stage || slot >= PRT_MAX_LAYER_TENSORS) return 0U;
  bytes = stage->local_spm_tensor_bytes[slot];
  if (bytes == 0U) bytes = stage->local_spm_page_count[slot] * PRT_PAGE_SIZE_BYTES;
  return bytes;
}

typedef struct {
  uint32_t tensor_id;
  uint32_t min_bytes;
  uint32_t max_bytes;
  uint32_t type_mask;
} segment_tensor_contract_t;

#define SEGMENT_TENSOR_MASK_TRANSPORT 0x1U
#define SEGMENT_TENSOR_MASK_ALL_RING 0x2U

static segment_tensor_contract_t *find_or_add_segment_tensor_contract(segment_tensor_contract_t *contracts,
                                                                     uint32_t *count,
                                                                     uint32_t tensor_id) {
  if (!contracts || !count) return NULL;
  for (uint32_t i = 0; i < *count; ++i) {
    if (contracts[i].tensor_id == tensor_id) return &contracts[i];
  }
  if (*count >= PRT_MAX_TENSORS) return NULL;
  contracts[*count].tensor_id = tensor_id;
  contracts[*count].min_bytes = UINT32_MAX;
  contracts[*count].max_bytes = 0U;
  contracts[*count].type_mask = 0U;
  *count += 1U;
  return &contracts[*count - 1U];
}

static uint32_t segment_tensor_contract_expected_ring_pages(const segment_tensor_contract_t *contract) {
  uint32_t bytes;
  if (!contract) return 0U;
  if (contract->type_mask & SEGMENT_TENSOR_MASK_ALL_RING) bytes = contract->max_bytes;
  else if (contract->type_mask & SEGMENT_TENSOR_MASK_TRANSPORT) bytes = contract->min_bytes;
  else return 0U;
  if (bytes == 0U) return 0U;
  return (bytes + PRT_PAGE_SIZE_BYTES - 1U) / PRT_PAGE_SIZE_BYTES;
}

static const prt_buffer_binding_t *find_segment_ring_binding_by_tensor(const prt_segment_desc_t *seg, uint32_t tensor_id) {
  if (!seg || !seg->buffer_bindings) return NULL;
  for (uint32_t i = 0; i < seg->buffer_binding_count; ++i) {
    const prt_buffer_binding_t *binding = &seg->buffer_bindings[i];
    if (binding->kind == PRT_BUFFER_BINDING_RING && binding->tensor_id == tensor_id) return binding;
  }
  return NULL;
}

static int validate_segment_transport_contract(uint32_t seg_idx, const prt_segment_desc_t *seg) {
  segment_tensor_contract_t contracts[PRT_MAX_TENSORS];
  uint32_t contract_count = 0U;
  memset(contracts, 0, sizeof(contracts));

  if (!seg) return PRT_ERR_INVAL;

  for (uint32_t stage_idx = 0; stage_idx < seg->num_stages; ++stage_idx) {
    const prt_stage_map_t *stage = &seg->stages[stage_idx];
    for (uint32_t i = 0; i < stage->num_entry; ++i) {
      const prt_tensor_binding_t *tb = &stage->entry[i];
      segment_tensor_contract_t *contract;
      uint32_t slot = 0U;
      uint32_t local_bytes = 0U;
      if (stage_find_tensor_slot_local(stage, tb->tensor_id, &slot) != PRT_OK) {
        fprintf(stderr, "segment transport validate: seg=%u stage=%u missing entry tensor slot tensor=%u\n",
                seg_idx, stage_idx, tb->tensor_id);
        return PRT_ERR_PARSE;
      }
      local_bytes = stage_local_tensor_bytes_with_fallback(stage, slot);
      contract = find_or_add_segment_tensor_contract(contracts, &contract_count, tb->tensor_id);
      if (!contract) return PRT_ERR_NOMEM;
      if (!strcmp(tb->tensor_type, "ALL_RINGBUFFER")) {
        contract->type_mask |= SEGMENT_TENSOR_MASK_ALL_RING;
        if (stage_idx == 0U) {
          fprintf(stderr, "segment transport validate: seg=%u tensor=%u boundary entry cannot be ALL_RINGBUFFER\n",
                  seg_idx, tb->tensor_id);
          return PRT_ERR_PARSE;
        }
      } else if (!strcmp(tb->tensor_type, "ISOLATE_SPM") || !strcmp(tb->tensor_type, "DRAM_DEPEN")) {
        contract->type_mask |= SEGMENT_TENSOR_MASK_TRANSPORT;
      }
      if (local_bytes > 0U) {
        if (contract->min_bytes == UINT32_MAX || local_bytes < contract->min_bytes) contract->min_bytes = local_bytes;
        if (local_bytes > contract->max_bytes) contract->max_bytes = local_bytes;
      }
    }

    for (uint32_t i = 0; i < stage->num_export; ++i) {
      const prt_tensor_binding_t *tb = &stage->exports[i];
      segment_tensor_contract_t *contract;
      uint32_t slot = 0U;
      uint32_t local_bytes = 0U;
      if (stage_find_tensor_slot_local(stage, tb->tensor_id, &slot) != PRT_OK) {
        fprintf(stderr, "segment transport validate: seg=%u stage=%u missing export tensor slot tensor=%u\n",
                seg_idx, stage_idx, tb->tensor_id);
        return PRT_ERR_PARSE;
      }
      local_bytes = stage_local_tensor_bytes_with_fallback(stage, slot);
      contract = find_or_add_segment_tensor_contract(contracts, &contract_count, tb->tensor_id);
      if (!contract) return PRT_ERR_NOMEM;
      if (!strcmp(tb->tensor_type, "ALL_RINGBUFFER")) {
        contract->type_mask |= SEGMENT_TENSOR_MASK_ALL_RING;
        if (stage_idx + 1U == seg->num_stages) {
          fprintf(stderr, "segment transport validate: seg=%u tensor=%u boundary export cannot be ALL_RINGBUFFER\n",
                  seg_idx, tb->tensor_id);
          return PRT_ERR_PARSE;
        }
      } else if (!strcmp(tb->tensor_type, "ISOLATE_SPM") || !strcmp(tb->tensor_type, "DRAM_DEPEN")) {
        contract->type_mask |= SEGMENT_TENSOR_MASK_TRANSPORT;
      }
      if (local_bytes > 0U) {
        if (contract->min_bytes == UINT32_MAX || local_bytes < contract->min_bytes) contract->min_bytes = local_bytes;
        if (local_bytes > contract->max_bytes) contract->max_bytes = local_bytes;
      }
    }
  }

  for (uint32_t i = 0; i < contract_count; ++i) {
    const segment_tensor_contract_t *contract = &contracts[i];
    int has_transport = 0;
    int has_ring_slot = 0;
    uint32_t transport_bytes =
      u32_map_lookup_value(&seg->transport_effective_bytes, contract->tensor_id, &has_transport);
    uint32_t ring_slot_bytes =
      u32_map_lookup_value(&seg->ring_slot_effective_bytes, contract->tensor_id, &has_ring_slot);
    if ((contract->type_mask & SEGMENT_TENSOR_MASK_ALL_RING) &&
        (contract->type_mask & SEGMENT_TENSOR_MASK_TRANSPORT)) {
      fprintf(stderr, "segment transport validate: seg=%u tensor=%u mixes ALL_RINGBUFFER with transport types\n",
              seg_idx, contract->tensor_id);
      return PRT_ERR_PARSE;
    }
    if ((contract->type_mask & SEGMENT_TENSOR_MASK_TRANSPORT) != 0U) {
      if (contract->min_bytes == UINT32_MAX || contract->min_bytes == 0U) {
        fprintf(stderr, "segment transport validate: seg=%u tensor=%u missing min bytes for transport contract\n",
                seg_idx, contract->tensor_id);
        return PRT_ERR_PARSE;
      }
      if (!has_transport || transport_bytes != contract->min_bytes) {
        fprintf(stderr,
                "segment transport validate: seg=%u tensor=%u transport_effective_bytes=%u expected=%u present=%u\n",
                seg_idx, contract->tensor_id, transport_bytes, contract->min_bytes, has_transport);
        return PRT_ERR_PARSE;
      }
      if (has_ring_slot) {
        fprintf(stderr, "segment transport validate: seg=%u tensor=%u unexpected ring_slot_effective_bytes=%u\n",
                seg_idx, contract->tensor_id, ring_slot_bytes);
        return PRT_ERR_PARSE;
      }
    } else if (has_transport) {
      fprintf(stderr, "segment transport validate: seg=%u tensor=%u unexpected transport_effective_bytes=%u\n",
              seg_idx, contract->tensor_id, transport_bytes);
      return PRT_ERR_PARSE;
    }

    if ((contract->type_mask & SEGMENT_TENSOR_MASK_ALL_RING) != 0U) {
      if (contract->max_bytes == 0U) {
        fprintf(stderr, "segment transport validate: seg=%u tensor=%u missing max bytes for ALL_RINGBUFFER\n",
                seg_idx, contract->tensor_id);
        return PRT_ERR_PARSE;
      }
      if (!has_ring_slot || ring_slot_bytes != contract->max_bytes) {
        fprintf(stderr,
                "segment transport validate: seg=%u tensor=%u ring_slot_effective_bytes=%u expected=%u present=%u\n",
                seg_idx, contract->tensor_id, ring_slot_bytes, contract->max_bytes, has_ring_slot);
        return PRT_ERR_PARSE;
      }
    } else if (has_ring_slot) {
      fprintf(stderr, "segment transport validate: seg=%u tensor=%u unexpected ring_slot_effective_bytes=%u\n",
              seg_idx, contract->tensor_id, ring_slot_bytes);
      return PRT_ERR_PARSE;
    }
  }

  for (uint32_t i = 0; i < seg->num_ring_cfg; ++i) {
    const prt_ring_cfg_t *cfg = &seg->ring_cfgs[i];
    const prt_buffer_binding_t *binding;
    const segment_tensor_contract_t *contract = NULL;
    uint32_t expected_pages = 0U;
    if (cfg->count == 0U) continue;
    for (uint32_t c = 0; c < contract_count; ++c) {
      if (contracts[c].tensor_id == cfg->tensor_id) {
        contract = &contracts[c];
        break;
      }
    }
    if (!contract) {
      fprintf(stderr, "segment transport validate: seg=%u tensor=%u ring cfg missing tensor contract\n",
              seg_idx, cfg->tensor_id);
      return PRT_ERR_PARSE;
    }
    expected_pages = segment_tensor_contract_expected_ring_pages(contract);
    if (expected_pages == 0U) {
      fprintf(stderr, "segment transport validate: seg=%u tensor=%u ring cfg has unsupported tensor class\n",
              seg_idx, cfg->tensor_id);
      return PRT_ERR_PARSE;
    }
    if (cfg->size_per != expected_pages) {
      fprintf(stderr, "segment transport validate: seg=%u tensor=%u ring size_per=%u expected=%u\n",
              seg_idx, cfg->tensor_id, cfg->size_per, expected_pages);
      return PRT_ERR_PARSE;
    }
    binding = find_segment_ring_binding_by_tensor(seg, cfg->tensor_id);
    if (!binding) {
      fprintf(stderr, "segment transport validate: seg=%u tensor=%u missing ring buffer binding\n",
              seg_idx, cfg->tensor_id);
      return PRT_ERR_PARSE;
    }
    if (binding->pages_per_slot != expected_pages || binding->slot_count != cfg->count) {
      fprintf(stderr,
              "segment transport validate: seg=%u tensor=%u ring binding slot_count/pages=%u/%u expected=%u/%u\n",
              seg_idx, cfg->tensor_id, binding->slot_count, binding->pages_per_slot,
              cfg->count, expected_pages);
      return PRT_ERR_PARSE;
    }
  }

  return PRT_OK;
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
  char *cache_buf = NULL;
  size_t cache_len = 0U;
  mapping_cache_header_t header;
  size_t expected_len;
  uint64_t load_start_ms;
  uint64_t parse_start_ms;
  const uint32_t probe_start = prt_env_u32_default("PIPELINE_RUNTIME_MAPPING_CACHE_PROBE_START",
                                                   UINT32_MAX);
  const uint32_t probe_end = prt_env_u32_default("PIPELINE_RUNTIME_MAPPING_CACHE_PROBE_END",
                                                 probe_start);
  const int probe_enabled = probe_start != UINT32_MAX && probe_end >= probe_start;
  int rc = PRT_ERR_IO;

  if (!cache_path || !db) return PRT_ERR_INVAL;
  load_start_ms = monotonic_ms();
  (void)load_start_ms;
  PRT_PROGRESS_LOG("artifacts mapping cache load begin cache=%s", cache_path);
  PRT_PROGRESS_LOG("artifacts mapping cache read-mode cache=%s mode=bulk-read", cache_path);
  if (probe_enabled) {
    PRT_PROGRESS_LOG("artifacts mapping cache probe-range cache=%s start=%u end=%u",
                     cache_path, probe_start, probe_end);
  }

  rc = load_file(cache_path, &cache_buf, &cache_len);
  if (rc != PRT_OK) goto out;
  if (cache_len < sizeof(header)) {
    rc = PRT_ERR_PARSE;
    goto out;
  }
  memcpy(&header, cache_buf, sizeof(header));
  if (header.magic0 != PRT_MAPPING_CACHE_MAGIC0 ||
      header.magic1 != PRT_MAPPING_CACHE_MAGIC1 ||
      header.version != PRT_MAPPING_CACHE_VERSION ||
      header.max_layer_tensors != PRT_MAX_LAYER_TENSORS ||
      header.entry_words != PRT_MAPPING_CACHE_ENTRY_WORDS) {
    rc = PRT_ERR_PARSE;
    goto out;
  }
#if SIZE_MAX < UINT64_MAX
  if ((size_t)header.entry_count > (SIZE_MAX - sizeof(header)) / sizeof(mapping_cache_entry_t)) {
    rc = PRT_ERR_PARSE;
    goto out;
  }
#endif
  expected_len = sizeof(header) + (size_t)header.entry_count * sizeof(mapping_cache_entry_t);
  if (cache_len != expected_len) {
    PRT_PROGRESS_LOG("artifacts mapping cache size mismatch cache=%s bytes=%zu expected=%zu entries=%u",
                     cache_path, cache_len, expected_len, header.entry_count);
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
    const size_t entry_off = sizeof(header) + (size_t)entry_idx * sizeof(cached);
    const int trace_entry = probe_enabled && entry_idx >= probe_start && entry_idx <= probe_end;
    if (trace_entry) {
      PRT_PROGRESS_LOG("artifacts mapping cache probe idx=%u step=before-decode off=%zu",
                       entry_idx, entry_off);
    }
    memcpy(&cached, cache_buf + entry_off, sizeof(cached));
    if (trace_entry) {
      PRT_PROGRESS_LOG(
        "artifacts mapping cache probe idx=%u step=after-copy layer=%u acc=%u split=%u active=%u counts=%u/%u/%u/%u/%u/%u",
        entry_idx, cached.layer_id, cached.target_accel, cached.split_kind, cached.active,
        cached.dram_n, cached.spm_n, cached.spm_addr_n,
        cached.first_vpage_n, cached.page_count_n, cached.spm_bytes_n);
    }
    rc = mapping_entry_from_cache(&entry, &cached);
    if (rc != PRT_OK) goto out;
    if (trace_entry) {
      PRT_PROGRESS_LOG(
        "artifacts mapping cache probe idx=%u step=after-decode layer=%u acc=%u split=%u active=%d counts=%u/%u/%u/%u/%u/%u",
        entry_idx, entry.layer_id, entry.target_accel, entry.split_kind, entry.active,
        entry.dram_n, entry.spm_n, entry.spm_addr_n,
        entry.first_vpage_n, entry.page_count_n, entry.spm_bytes_n);
    }
    rc = mapping_db_append(db, &entry);
    if (rc != PRT_OK) goto out;
    if (trace_entry) {
      PRT_PROGRESS_LOG("artifacts mapping cache probe idx=%u step=after-append db_count=%u",
                       entry_idx, db->count);
    }
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
  free(cache_buf);
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

static int stage_local_mapping_matches(const prt_stage_map_t *stage, const mapping_entry_t *entry) {
  uint32_t span = 0;
  if (!stage || !entry) return 0;
  if (stage->local_spm_tensor_count != entry->spm_addr_n) return 0;
  for (uint32_t i = 0; i < entry->spm_addr_n && i < PRT_MAX_LAYER_TENSORS; ++i) {
    uint32_t end_vpage = entry->first_vpage[i] + entry->page_count[i];
    if (stage->local_spm_tensor_addr[i] != entry->spm_addr[i]) return 0;
    if (stage->local_spm_first_vpage[i] != entry->first_vpage[i]) return 0;
    if (stage->local_spm_page_count[i] != entry->page_count[i]) return 0;
    if (stage->local_spm_tensor_bytes[i] != entry->spm_bytes[i]) return 0;
    if (end_vpage > span) span = end_vpage;
  }
  return stage->local_spm_page_span == span;
}

static int parse_mapping_file(const char *path, mapping_db_t *db) {
  char *buf = NULL;
  char *cache_path = NULL;
  char *p;
  size_t buf_len = 0;
  uint32_t line_no = 0;
  uint32_t estimated_entries;
  uint32_t cur_entry_ord = 0;
  uint32_t parse_progress_interval =
    prt_env_u32_default("PIPELINE_RUNTIME_MAPPING_PARSE_PROGRESS_INTERVAL",
                        PRT_MAPPING_PARSE_PROGRESS_INTERVAL);
  const uint32_t probe_start = prt_env_u32_default("PIPELINE_RUNTIME_MAPPING_PARSE_PROBE_START",
                                                   UINT32_MAX);
  const uint32_t probe_end = prt_env_u32_default("PIPELINE_RUNTIME_MAPPING_PARSE_PROBE_END",
                                                 probe_start);
  const int probe_enabled = probe_start != UINT32_MAX && probe_end >= probe_start;
  mapping_entry_t cur;
  int rc;
  int trace_entry = 0;
  uint64_t load_start_ms;
  uint64_t parse_start_ms;

  if (!path || !db) return PRT_ERR_INVAL;
  (void)load_start_ms;
  (void)parse_start_ms;
  if (parse_progress_interval == 0U) {
    parse_progress_interval = PRT_MAPPING_PARSE_PROGRESS_INTERVAL;
  }
  cache_path = mapping_cache_path_from_yaml(path);
  if (cache_path) {
    if (prt_env_flag_enabled_impl("PIPELINE_RUNTIME_DISABLE_MAPPING_CACHE", 0)) {
      PRT_PROGRESS_LOG("artifacts mapping cache disabled layer_mapping=%s cache=%s reason=env",
                       path, cache_path);
    } else {
      rc = load_mapping_cache_file(cache_path, db);
      if (rc == PRT_OK) {
        free(cache_path);
        return PRT_OK;
      }
      PRT_PROGRESS_LOG("artifacts mapping cache fallback layer_mapping=%s cache=%s rc=%d",
                       path, cache_path, rc);
    }
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
  PRT_PROGRESS_LOG("artifacts mapping parse begin layer_mapping=%s estimated_entries=%u progress_interval=%u",
                   path, estimated_entries, parse_progress_interval);
  if (probe_enabled) {
    PRT_PROGRESS_LOG("artifacts mapping parse probe-range layer_mapping=%s start=%u end=%u",
                     path, probe_start, probe_end);
  }
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
    line_no++;
    rtrim(line);
    t = ltrim(line);
    if (*t == '\0' || *t == '#') continue;

    if (strncmp(t, "- layer_id:", 11) == 0) {
      if (cur.active) {
        if (trace_entry) {
          PRT_PROGRESS_LOG("artifacts mapping parse probe entry=%u step=before-append line=%u layer=%u acc=%u split=%u counts=%u/%u/%u/%u/%u/%u",
                           cur_entry_ord, line_no, cur.layer_id, cur.target_accel, cur.split_kind,
                           cur.dram_n, cur.spm_n, cur.spm_addr_n,
                           cur.first_vpage_n, cur.page_count_n, cur.spm_bytes_n);
        }
        rc = mapping_db_append(db, &cur);
        if (rc != PRT_OK) {
          free(buf);
          return rc;
        }
        if (trace_entry) {
          PRT_PROGRESS_LOG("artifacts mapping parse probe entry=%u step=after-append db_count=%u",
                           cur_entry_ord, db->count);
        }
        if ((db->count % parse_progress_interval) == 0U) {
          PRT_PROGRESS_LOG("artifacts mapping parse progress layer_mapping=%s entries=%u elapsed_ms=%llu",
                           path, db->count,
                           (unsigned long long)(monotonic_ms() - parse_start_ms));
        }
      }
      mapping_entry_reset(&cur);
      cur.active = 1;
      cur_entry_ord = db->count + 1U;
      trace_entry = probe_enabled && cur_entry_ord >= probe_start && cur_entry_ord <= probe_end;
      if (trace_entry) {
        PRT_PROGRESS_LOG("artifacts mapping parse probe entry=%u step=begin line=%u text=%s",
                         cur_entry_ord, line_no, t);
      }
      if (parse_u32_scalar(t, &cur.layer_id) != PRT_OK) {
        PRT_PROGRESS_LOG("artifacts mapping parse error layer_mapping=%s entry=%u line=%u field=layer_id text=%s",
                         path, cur_entry_ord, line_no, t);
        free(buf);
        return PRT_ERR_PARSE;
      }
      continue;
    }

    if (!cur.active) continue;
    if (trace_entry) {
      PRT_PROGRESS_LOG("artifacts mapping parse probe entry=%u step=line line=%u text=%s",
                       cur_entry_ord, line_no, t);
    }

    if (starts_key(t, "target_accel")) {
      if (parse_u32_scalar(t, &cur.target_accel) != PRT_OK) {
        PRT_PROGRESS_LOG("artifacts mapping parse error layer_mapping=%s entry=%u line=%u field=target_accel text=%s",
                         path, cur_entry_ord, line_no, t);
        free(buf);
        return PRT_ERR_PARSE;
      }
      continue;
    }

    if (starts_key(t, "mapping_dram_bypass")) {
      if (parse_u32_list_from_value(value_after_colon(t), cur.dram, &cur.dram_n,
                                    PRT_MAX_LAYER_TENSORS) != PRT_OK) {
        PRT_PROGRESS_LOG("artifacts mapping parse error layer_mapping=%s entry=%u line=%u field=mapping_dram_bypass text=%s",
                         path, cur_entry_ord, line_no, t);
        free(buf);
        return PRT_ERR_PARSE;
      }
      continue;
    }

    if (starts_key(t, "mapping_spm_bypass")) {
      if (parse_u32_list_from_value(value_after_colon(t), cur.spm, &cur.spm_n,
                                    PRT_MAX_LAYER_TENSORS) != PRT_OK) {
        PRT_PROGRESS_LOG("artifacts mapping parse error layer_mapping=%s entry=%u line=%u field=mapping_spm_bypass text=%s",
                         path, cur_entry_ord, line_no, t);
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
        PRT_PROGRESS_LOG("artifacts mapping parse error layer_mapping=%s entry=%u line=%u field=others_spm_tensor_addr text=%s",
                         path, cur_entry_ord, line_no, t);
        free(buf);
        return PRT_ERR_PARSE;
      }
      continue;
    }

    if (starts_key(t, "others_first_tensor_page_num")) {
      if (parse_u32_list_from_value(value_after_colon(t), cur.first_vpage, &cur.first_vpage_n,
                                    PRT_MAX_LAYER_TENSORS) != PRT_OK) {
        PRT_PROGRESS_LOG("artifacts mapping parse error layer_mapping=%s entry=%u line=%u field=others_first_tensor_page_num text=%s",
                         path, cur_entry_ord, line_no, t);
        free(buf);
        return PRT_ERR_PARSE;
      }
      continue;
    }

    if (starts_key(t, "others_spm_tensor_page_count")) {
      if (parse_u32_list_from_value(value_after_colon(t), cur.page_count, &cur.page_count_n,
                                    PRT_MAX_LAYER_TENSORS) != PRT_OK) {
        PRT_PROGRESS_LOG("artifacts mapping parse error layer_mapping=%s entry=%u line=%u field=others_spm_tensor_page_count text=%s",
                         path, cur_entry_ord, line_no, t);
        free(buf);
        return PRT_ERR_PARSE;
      }
      continue;
    }

    if (starts_key(t, "others_spm_tensor_util")) {
      if (parse_u32_list_from_value(value_after_colon(t), cur.spm_bytes, &cur.spm_bytes_n,
                                    PRT_MAX_LAYER_TENSORS) != PRT_OK) {
        PRT_PROGRESS_LOG("artifacts mapping parse error layer_mapping=%s entry=%u line=%u field=others_spm_tensor_util text=%s",
                         path, cur_entry_ord, line_no, t);
        free(buf);
        return PRT_ERR_PARSE;
      }
      continue;
    }
  }

  if (cur.active) {
    if (trace_entry) {
      PRT_PROGRESS_LOG("artifacts mapping parse probe entry=%u step=before-final-append line=%u layer=%u acc=%u split=%u counts=%u/%u/%u/%u/%u/%u",
                       cur_entry_ord, line_no, cur.layer_id, cur.target_accel, cur.split_kind,
                       cur.dram_n, cur.spm_n, cur.spm_addr_n,
                       cur.first_vpage_n, cur.page_count_n, cur.spm_bytes_n);
    }
    rc = mapping_db_append(db, &cur);
    if (rc != PRT_OK) {
      free(buf);
      return rc;
    }
    if (trace_entry) {
      PRT_PROGRESS_LOG("artifacts mapping parse probe entry=%u step=after-final-append db_count=%u",
                       cur_entry_ord, db->count);
    }
    if ((db->count % parse_progress_interval) == 0U ||
        db->count == estimated_entries) {
      PRT_PROGRESS_LOG("artifacts mapping parse progress layer_mapping=%s entries=%u elapsed_ms=%llu",
                       path, db->count,
                       (unsigned long long)(monotonic_ms() - parse_start_ms));
    }
  }

  {
    uint64_t parse_elapsed_ms = monotonic_ms() - parse_start_ms;
    PRT_PROGRESS_LOG("artifacts mapping parse done layer_mapping=%s entries=%u elapsed_ms=%llu",
                     path, db->count, (unsigned long long)parse_elapsed_ms);
    prt_gdb_marker_note(PRT_GDB_MARKER_SITE_ARTIFACT_MAPPING_PARSE_DONE,
                        PRT_DEBUG_U32_NONE, PRT_DEBUG_U32_NONE,
                        PRT_DEBUG_U32_NONE, PRT_DEBUG_U32_NONE,
                        PRT_DEBUG_U32_NONE, PRT_DEBUG_U32_NONE,
                        PRT_DEBUG_U32_NONE, PRT_DEBUG_U32_NONE, PRT_OK,
                        (uint64_t)db->count, parse_elapsed_ms, __LINE__);
  }
  free(buf);
  return PRT_OK;
}

static int validate_stage_against_mapping_entries(const char *path, const mapping_db_t *db,
                                                  uint32_t seg_idx, uint32_t stage_idx,
                                                  prt_stage_map_t *stage) {
  int matches = 0;
  int layout_matches = 0;
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
    } else if (stage->local_spm_tensor_count > 0U &&
               entry->active &&
               entry->layer_id == stage->layer_id &&
               stage_local_mapping_matches(stage, entry)) {
      layout_matches += 1;
      if (layout_matches == 1) {
        matched_split_kind = entry->split_kind;
        matched = *entry;
      }
      PRT_PROGRESS_HOT_LOG("artifacts validate stage-layout-hit seg=%u local_stage=%u global_stage=%u layer=%u entry=%u layout_matches=%d split_kind=%u",
                           seg_idx, stage_idx, stage->stage_id, stage->layer_id,
                           entry_idx, layout_matches, entry->split_kind);
    }
    if (((entry_idx + 1U) % PRT_MAPPING_SCAN_PROGRESS_INTERVAL) == 0U) {
      PRT_PROGRESS_HOT_LOG("artifacts validate stage-scan seg=%u local_stage=%u global_stage=%u scanned=%u/%u elapsed_ms=%llu matches=%d layout_matches=%d",
                           seg_idx, stage_idx, stage->stage_id, entry_idx + 1U, db->count,
                           (unsigned long long)(monotonic_ms() - start_ms), matches, layout_matches);
    }
  }
  if (matches == 0 && layout_matches > 0) {
    PRT_PROGRESS_LOG("artifacts validate stage-layout-fallback seg=%u local_stage=%u global_stage=%u layer=%u layout_matches=%d",
                     seg_idx, stage_idx, stage->stage_id, stage->layer_id, layout_matches);
  } else if (matches != 1) {
    fprintf(stderr,
            "layer mapping match failure: file=%s layer=%u stage=%u accUtil=%u matches=%d layout_matches=%d dramBypassCount=%u spmBypassCount=%u\n",
            path, stage->layer_id, stage->stage_id, stage->acc_util, matches, layout_matches,
            stage->dram_bypass_count, stage->spm_bypass_count);
    return PRT_ERR_NOT_READY;
  }
  if (matches == 1 && matched_split_kind == PRT_LAYER_SPLIT_UNSPEC) {
    fprintf(stderr, "layer mapping missing split_kind: file=%s layer=%u stage=%u\n",
            path, stage->layer_id, stage->stage_id);
    return PRT_ERR_PARSE;
  }
  if (matches == 1 && stage->split_kind == PRT_LAYER_SPLIT_UNSPEC) {
    stage->split_kind = matched_split_kind;
  } else if (matches == 1 && stage->split_kind != matched_split_kind) {
    if (stage->local_spm_tensor_count > 0U && stage_local_mapping_matches(stage, &matched)) {
      PRT_PROGRESS_LOG(
        "artifacts validate split-kind-fallback seg=%u local_stage=%u global_stage=%u layer=%u pipeline=%u mapping=%u",
        seg_idx, stage_idx, stage->stage_id, stage->layer_id, stage->split_kind, matched_split_kind);
    } else {
      fprintf(stderr,
              "split_kind mismatch: file=%s layer=%u stage=%u pipeline=%u mapping=%u\n",
              path, stage->layer_id, stage->stage_id, stage->split_kind, matched_split_kind);
      return PRT_ERR_PARSE;
    }
  }
  if (stage->local_spm_tensor_count == 0U) stage_apply_mapping(stage, &matched);
  else if (!stage_local_mapping_matches(stage, &matched)) {
    fprintf(stderr,
            "local_spm layout mismatch: file=%s layer=%u stage=%u\n",
            path, stage->layer_id, stage->stage_id);
    return PRT_ERR_PARSE;
  }
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
    rc = validate_segment_transport_contract(seg_idx, seg);
    if (rc != PRT_OK) goto out;
    PRT_PROGRESS_LOG("artifacts validate segment-end seg=%u stages=%u elapsed_ms=%llu",
                     seg_idx, seg->num_stages,
                     (unsigned long long)(monotonic_ms() - seg_start_ms));
  }
  PRT_PROGRESS_LOG("artifacts validate end layer_mapping=%s segments=%u",
                   layer_mapping_yaml, pipeline->num_segments);
  prt_gdb_marker_note(PRT_GDB_MARKER_SITE_ARTIFACT_VALIDATE_DONE,
                      PRT_DEBUG_U32_NONE, PRT_DEBUG_U32_NONE,
                      PRT_DEBUG_U32_NONE, PRT_DEBUG_U32_NONE,
                      PRT_DEBUG_U32_NONE, PRT_DEBUG_U32_NONE,
                      PRT_DEBUG_U32_NONE, PRT_DEBUG_U32_NONE, PRT_OK,
                      (uint64_t)pipeline->num_segments, (uint64_t)db.count,
                      __LINE__);
  rc = PRT_OK;
out:
  mapping_db_reset(&db);
  return rc;
}
