#include "prt_yaml_loader.h"
#include "prt_progress.h"

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int read_full_pread(int fd, char *buf, size_t len) {
  size_t off = 0U;

  while (off < len) {
    size_t chunk = len - off;
    ssize_t n;

    if (chunk > (1U << 20)) chunk = (1U << 20);
    n = pread(fd, buf + off, chunk, (off_t)off);
    if (n > 0) {
      off += (size_t)n;
      continue;
    }
    if (n == 0) return PRT_ERR_IO;
    if (errno == EINTR) continue;
    return PRT_ERR_IO;
  }

  return PRT_OK;
}

static int load_file(const char *path, char **out_buf, size_t *out_len) {
  int fd = -1;
  off_t sz_off;
  size_t sz;
  char *buf;
  int rc;

  if (!path || !out_buf || !out_len) return PRT_ERR_INVAL;
  PRT_PROGRESS_LOG("yaml file open begin path=%s", path);
  fd = open(path, O_RDONLY | O_CLOEXEC);
  if (fd < 0) {
    PRT_PROGRESS_LOG("yaml file open fail path=%s errno=%d", path, errno);
    return PRT_ERR_IO;
  }
  PRT_PROGRESS_LOG("yaml file open end path=%s", path);

  PRT_PROGRESS_LOG("yaml file seek-end begin path=%s", path);
  sz_off = lseek(fd, 0, SEEK_END);
  if (sz_off < 0) {
    PRT_PROGRESS_LOG("yaml file seek-end fail path=%s errno=%d", path, errno);
    close(fd);
    return PRT_ERR_IO;
  }
  PRT_PROGRESS_LOG("yaml file seek-end end path=%s", path);

  PRT_PROGRESS_LOG("yaml file ftell begin path=%s", path);
  if ((uint64_t)sz_off > (uint64_t)(SIZE_MAX - 1U)) {
    PRT_PROGRESS_LOG("yaml file ftell fail path=%s bytes=%lld too_large=1",
                     path, (long long)sz_off);
    close(fd);
    return PRT_ERR_IO;
  }
  sz = (size_t)sz_off;
  PRT_PROGRESS_LOG("yaml file ftell end path=%s bytes=%lld", path, (long long)sz_off);

  PRT_PROGRESS_LOG("yaml file read-mode path=%s mode=pread", path);

  PRT_PROGRESS_LOG("yaml file alloc begin path=%s bytes=%zu", path, sz);
  buf = (char *)malloc(sz + 1U);
  if (!buf) {
    PRT_PROGRESS_LOG("yaml file alloc fail path=%s bytes=%zu", path, sz);
    close(fd);
    return PRT_ERR_NOMEM;
  }
  PRT_PROGRESS_LOG("yaml file alloc end path=%s bytes=%zu", path, sz);

  PRT_PROGRESS_LOG("yaml file read begin path=%s bytes=%zu", path, sz);
  rc = read_full_pread(fd, buf, sz);
  if (rc != PRT_OK) {
    PRT_PROGRESS_LOG("yaml file read fail path=%s bytes=%zu rc=%d errno=%d",
                     path, sz, rc, errno);
    free(buf);
    close(fd);
    return rc;
  }
  PRT_PROGRESS_LOG("yaml file read end path=%s bytes=%zu", path, sz);
  buf[sz] = '\0';
  close(fd);
  PRT_PROGRESS_LOG("yaml file close end path=%s", path);

  *out_buf = buf;
  *out_len = sz;
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

static int count_indent(const char *s) {
  int n = 0;
  while (*s == ' ') {
    n++;
    s++;
  }
  return n;
}

static char *strip_list_prefix(char *s) {
  s = ltrim(s);
  while (*s == '-') {
    s++;
    s = ltrim(s);
  }
  return s;
}

static int starts_key(char *line, const char *key) {
  char *p = strip_list_prefix(line);
  size_t n = strlen(key);
  return strncmp(p, key, n) == 0 && p[n] == ':';
}

static char *value_after_colon(char *line) {
  char *p = strchr(line, ':');
  if (!p) return NULL;
  p++;
  p = ltrim(p);
  return p;
}

static const char *key_name(char *line) {
  char *p;
  char *colon;

  if (!line) return NULL;
  p = strip_list_prefix(line);
  colon = strchr(p, ':');
  if (!colon) return p;
  *colon = '\0';
  return p;
}

static int parse_u32_scalar(char *line, uint32_t *out) {
  char *v = value_after_colon(line);
  char *end = NULL;
  unsigned long x;
  if (!v || !out) return PRT_ERR_PARSE;
  x = strtoul(v, &end, 10);
  if (end == v) return PRT_ERR_PARSE;
  *out = (uint32_t)x;
  return PRT_OK;
}

static int parse_accutil_anywhere(char *line, uint32_t *out) {
  char *p;
  char *end = NULL;
  unsigned long x;
  if (!line || !out) return PRT_ERR_INVAL;
  p = strstr(line, "accUtil:");
  if (!p) return PRT_ERR_PARSE;
  p += strlen("accUtil:");
  p = ltrim(p);
  if (!*p) return PRT_ERR_PARSE;
  x = strtoul(p, &end, 10);
  if (end == p) return PRT_ERR_PARSE;
  *out = (uint32_t)x;
  return PRT_OK;
}

static int yaml_line_log_enabled(void) {
  static int enabled = -1;
  const char *v;

  if (enabled >= 0) return enabled;
  v = getenv("PIPELINE_RUNTIME_YAML_LINE_LOG_ENABLE");
  enabled = (v && *v && strcmp(v, "0") != 0 &&
             strcmp(v, "false") != 0 && strcmp(v, "FALSE") != 0 &&
             strcmp(v, "off") != 0 && strcmp(v, "OFF") != 0) ? 1 : 0;
  return enabled;
}

static int parse_int_list_from_value(char *v, uint32_t **out, uint32_t *out_n) {
  uint32_t *arr = NULL;
  uint32_t n = 0;
  uint32_t cap = 0;
  char *p;

  if (!v || !out || !out_n) return PRT_ERR_INVAL;
  p = strchr(v, '[');
  if (!p) {
    *out = NULL;
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

    char *next = NULL;
    unsigned long val = strtoul(p, &next, 10);
    if (next == p) {
      free(arr);
      return PRT_ERR_PARSE;
    }
    p = next;
    if (n == cap) {
      uint32_t new_cap = cap ? (cap << 1) : 8;
      uint32_t *tmp = (uint32_t *)realloc(arr, sizeof(uint32_t) * new_cap);
      if (!tmp) {
        free(arr);
        return PRT_ERR_NOMEM;
      }
      arr = tmp;
      cap = new_cap;
    }
    arr[n++] = (uint32_t)val;
  }

  *out = arr;
  *out_n = n;
  return PRT_OK;
}

static int parse_u64_list_from_value(char *v, uint64_t **out, uint32_t *out_n) {
  uint64_t *arr = NULL;
  uint32_t n = 0;
  uint32_t cap = 0;
  char *p;

  if (!v || !out || !out_n) return PRT_ERR_INVAL;
  p = strchr(v, '[');
  if (!p) {
    *out = NULL;
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

    char *next = NULL;
    unsigned long long val = strtoull(p, &next, 10);
    if (next == p) {
      free(arr);
      return PRT_ERR_PARSE;
    }
    p = next;
    if (n == cap) {
      uint32_t new_cap = cap ? (cap << 1) : 8;
      uint64_t *tmp = (uint64_t *)realloc(arr, sizeof(uint64_t) * new_cap);
      if (!tmp) {
        free(arr);
        return PRT_ERR_NOMEM;
      }
      arr = tmp;
      cap = new_cap;
    }
    arr[n++] = (uint64_t)val;
  }

  *out = arr;
  *out_n = n;
  return PRT_OK;
}

static int parse_str_list_from_value(char *v, char ***out, uint32_t *out_n) {
  char **arr = NULL;
  uint32_t n = 0;
  uint32_t cap = 0;
  char *p;

  if (!v || !out || !out_n) return PRT_ERR_INVAL;
  p = strchr(v, '[');
  if (!p) {
    *out = NULL;
    *out_n = 0;
    return PRT_OK;
  }
  p++;

  while (*p && *p != ']') {
    char token[64];
    size_t t = 0;

    while (*p && (isspace((unsigned char)*p) || *p == ',')) p++;
    if (*p == ']') break;

    if (*p == '"' || *p == '\'') {
      char q = *p++;
      while (*p && *p != q && t + 1 < sizeof(token)) token[t++] = *p++;
      if (*p == q) p++;
    } else {
      while (*p && *p != ',' && *p != ']' && t + 1 < sizeof(token)) token[t++] = *p++;
      while (t > 0 && isspace((unsigned char)token[t - 1])) t--;
    }
    token[t] = '\0';

    if (t == 0) continue;

    if (n == cap) {
      uint32_t new_cap = cap ? (cap << 1) : 8;
      char **tmp = (char **)realloc(arr, sizeof(char *) * new_cap);
      if (!tmp) {
        for (uint32_t i = 0; i < n; ++i) free(arr[i]);
        free(arr);
        return PRT_ERR_NOMEM;
      }
      arr = tmp;
      cap = new_cap;
    }

    arr[n] = (char *)malloc(t + 1);
    if (!arr[n]) {
      for (uint32_t i = 0; i < n; ++i) free(arr[i]);
      free(arr);
      return PRT_ERR_NOMEM;
    }
    memcpy(arr[n], token, t + 1);
    n++;
  }

  *out = arr;
  *out_n = n;
  return PRT_OK;
}

typedef struct {
  uint32_t key;
  uint32_t value;
} map_kv_t;

static int parse_u32_map_from_value(char *v, map_kv_t **out, uint32_t *out_n) {
  map_kv_t *arr = NULL;
  uint32_t n = 0;
  uint32_t cap = 0;
  char *p;

  if (!v || !out || !out_n) return PRT_ERR_INVAL;
  p = strchr(v, '{');
  if (!p) {
    *out = NULL;
    *out_n = 0;
    return PRT_OK;
  }
  p++;

  while (*p && *p != '}') {
    unsigned long k;
    unsigned long val;

    while (*p && (isspace((unsigned char)*p) || *p == ',')) p++;
    if (*p == '}') break;
    if (!isdigit((unsigned char)*p)) {
      p++;
      continue;
    }

    char *next = NULL;
    k = strtoul(p, &next, 10);
    if (next == p) {
      free(arr);
      return PRT_ERR_PARSE;
    }
    p = next;
    while (*p && *p != ':') p++;
    if (*p != ':') break;
    p++;
    while (*p && isspace((unsigned char)*p)) p++;
    if (!isdigit((unsigned char)*p)) break;
    val = strtoul(p, &next, 10);
    if (next == p) {
      free(arr);
      return PRT_ERR_PARSE;
    }
    p = next;

    if (n == cap) {
      uint32_t new_cap = cap ? (cap << 1) : 8;
      map_kv_t *tmp = (map_kv_t *)realloc(arr, sizeof(map_kv_t) * new_cap);
      if (!tmp) {
        free(arr);
        return PRT_ERR_NOMEM;
      }
      arr = tmp;
      cap = new_cap;
    }
    arr[n].key = (uint32_t)k;
    arr[n].value = (uint32_t)val;
    n++;
  }

  *out = arr;
  *out_n = n;
  return PRT_OK;
}

static int ensure_stage_capacity(prt_segment_desc_t *seg, uint32_t need) {
  if (seg->num_stages >= need) return PRT_OK;
  prt_stage_map_t *tmp = (prt_stage_map_t *)realloc(seg->stages, sizeof(prt_stage_map_t) * need);
  if (!tmp) return PRT_ERR_NOMEM;
  for (uint32_t i = seg->num_stages; i < need; ++i) memset(&tmp[i], 0, sizeof(tmp[i]));
  seg->stages = tmp;
  return PRT_OK;
}

static int ensure_segment_capacity(prt_pipeline_desc_t *out, uint32_t need) {
  if (out->num_segments >= need) return PRT_OK;
  prt_segment_desc_t *tmp = (prt_segment_desc_t *)realloc(out->segments, sizeof(prt_segment_desc_t) * need);
  if (!tmp) return PRT_ERR_NOMEM;
  for (uint32_t i = out->num_segments; i < need; ++i) memset(&tmp[i], 0, sizeof(tmp[i]));
  out->segments = tmp;
  return PRT_OK;
}

static int ensure_model_layer_capacity(prt_model_desc_t *model, uint32_t need) {
  if (model->num_layers >= need) return PRT_OK;
  prt_model_layer_t *tmp = (prt_model_layer_t *)realloc(model->layers, sizeof(prt_model_layer_t) * need);
  if (!tmp) return PRT_ERR_NOMEM;
  for (uint32_t i = model->num_layers; i < need; ++i) memset(&tmp[i], 0, sizeof(tmp[i]));
  model->layers = tmp;
  return PRT_OK;
}

static int push_model_layer(prt_model_desc_t *model, prt_model_layer_t **out_layer) {
  if (!model || !out_layer) return PRT_ERR_INVAL;
  if (ensure_model_layer_capacity(model, model->num_layers + 1) != PRT_OK) return PRT_ERR_NOMEM;
  *out_layer = &model->layers[model->num_layers++];
  memset(*out_layer, 0, sizeof(**out_layer));
  return PRT_OK;
}

static int ensure_stage_spm_util_capacity(prt_segment_desc_t *seg, uint32_t need) {
  if (seg->num_stage_spm_util >= need) return PRT_OK;
  prt_u32_map_t *tmp = (prt_u32_map_t *)realloc(seg->tensor_spm_util_in_stage, sizeof(prt_u32_map_t) * need);
  if (!tmp) return PRT_ERR_NOMEM;
  for (uint32_t i = seg->num_stage_spm_util; i < need; ++i) memset(&tmp[i], 0, sizeof(tmp[i]));
  seg->tensor_spm_util_in_stage = tmp;
  seg->num_stage_spm_util = need;
  return PRT_OK;
}

static int ensure_buffer_binding_capacity(prt_segment_desc_t *seg, uint32_t need) {
  if (!seg) return PRT_ERR_INVAL;
  if (seg->buffer_binding_count >= need) return PRT_OK;
  prt_buffer_binding_t *tmp =
    (prt_buffer_binding_t *)realloc(seg->buffer_bindings, sizeof(prt_buffer_binding_t) * need);
  if (!tmp) return PRT_ERR_NOMEM;
  for (uint32_t i = seg->buffer_binding_count; i < need; ++i) memset(&tmp[i], 0, sizeof(tmp[i]));
  seg->buffer_bindings = tmp;
  seg->buffer_binding_count = need;
  return PRT_OK;
}

static int u32_map_set_from_kv(prt_u32_map_t *dst, const map_kv_t *kv, uint32_t n) {
  if (!dst) return PRT_ERR_INVAL;
  free(dst->data);
  dst->data = NULL;
  dst->size = 0;
  dst->cap = 0;

  if (n == 0) return PRT_OK;
  dst->data = (prt_u32_kv_t *)calloc(n, sizeof(prt_u32_kv_t));
  if (!dst->data) return PRT_ERR_NOMEM;
  dst->size = n;
  dst->cap = n;
  for (uint32_t i = 0; i < n; ++i) {
    dst->data[i].key = kv[i].key;
    dst->data[i].value = kv[i].value;
  }
  return PRT_OK;
}

static prt_ring_cfg_t *find_or_add_ring_cfg(prt_segment_desc_t *seg, uint32_t tensor_id) {
  for (uint32_t i = 0; i < seg->num_ring_cfg; ++i) {
    if (seg->ring_cfgs[i].tensor_id == tensor_id) return &seg->ring_cfgs[i];
  }

  prt_ring_cfg_t *tmp = (prt_ring_cfg_t *)realloc(seg->ring_cfgs, sizeof(prt_ring_cfg_t) * (seg->num_ring_cfg + 1));
  if (!tmp) return NULL;
  seg->ring_cfgs = tmp;
  memset(&seg->ring_cfgs[seg->num_ring_cfg], 0, sizeof(prt_ring_cfg_t));
  seg->ring_cfgs[seg->num_ring_cfg].tensor_id = tensor_id;
  seg->num_ring_cfg += 1;
  return &seg->ring_cfgs[seg->num_ring_cfg - 1];
}

static void stage_set_entry_ids(prt_stage_map_t *st, const uint32_t *ids, uint32_t n) {
  free(st->entry);
  st->entry = NULL;
  st->num_entry = n;
  if (n == 0) return;
  st->entry = (prt_tensor_binding_t *)calloc(n, sizeof(prt_tensor_binding_t));
  if (!st->entry) {
    st->num_entry = 0;
    return;
  }
  for (uint32_t i = 0; i < n; ++i) st->entry[i].tensor_id = ids[i];
}

static void stage_set_export_ids(prt_stage_map_t *st, const uint32_t *ids, uint32_t n) {
  free(st->exports);
  st->exports = NULL;
  st->num_export = n;
  if (n == 0) return;
  st->exports = (prt_tensor_binding_t *)calloc(n, sizeof(prt_tensor_binding_t));
  if (!st->exports) {
    st->num_export = 0;
    return;
  }
  for (uint32_t i = 0; i < n; ++i) st->exports[i].tensor_id = ids[i];
}

static void stage_set_entry_types(prt_stage_map_t *st, char **types, uint32_t n) {
  uint32_t m = (st->num_entry < n) ? st->num_entry : n;
  for (uint32_t i = 0; i < m; ++i) {
    strncpy(st->entry[i].tensor_type, types[i], sizeof(st->entry[i].tensor_type) - 1);
    st->entry[i].tensor_type[sizeof(st->entry[i].tensor_type) - 1] = '\0';
  }
}

static void stage_set_export_types(prt_stage_map_t *st, char **types, uint32_t n) {
  uint32_t m = (st->num_export < n) ? st->num_export : n;
  for (uint32_t i = 0; i < m; ++i) {
    strncpy(st->exports[i].tensor_type, types[i], sizeof(st->exports[i].tensor_type) - 1);
    st->exports[i].tensor_type[sizeof(st->exports[i].tensor_type) - 1] = '\0';
  }
}

static void stage_set_entry_dbuf(prt_stage_map_t *st, const uint32_t *db, uint32_t n) {
  uint32_t m = (st->num_entry < n) ? st->num_entry : n;
  for (uint32_t i = 0; i < m; ++i) st->entry[i].double_buffer = db[i];
}

static void stage_set_export_dbuf(prt_stage_map_t *st, const uint32_t *db, uint32_t n) {
  uint32_t m = (st->num_export < n) ? st->num_export : n;
  for (uint32_t i = 0; i < m; ++i) st->exports[i].double_buffer = db[i];
}

static void stage_set_entry_buffer_ids(prt_stage_map_t *st, const uint32_t *ids, uint32_t n) {
  uint32_t m = (st->num_entry < n) ? st->num_entry : n;
  for (uint32_t i = 0; i < m; ++i) st->entry[i].buffer_id = ids[i];
}

static void stage_set_export_buffer_ids(prt_stage_map_t *st, const uint32_t *ids, uint32_t n) {
  uint32_t m = (st->num_export < n) ? st->num_export : n;
  for (uint32_t i = 0; i < m; ++i) st->exports[i].buffer_id = ids[i];
}

static void stage_set_layer_id(prt_stage_map_t *st, const uint32_t *ids, uint32_t n) {
  if (!st) return;
  st->layer_count = n;
  st->layer_id = (n > 0) ? ids[0] : 0;
}

static void stage_set_tensor_ids(prt_stage_map_t *st, const uint32_t *ids, uint32_t n) {
  uint32_t m = n > PRT_MAX_LAYER_TENSORS ? PRT_MAX_LAYER_TENSORS : n;
  if (!st) return;
  st->tensor_id_count = m;
  for (uint32_t i = 0; i < m; ++i) st->tensor_ids[i] = ids[i];
}

static void stage_set_fix_tensor_ids(prt_stage_map_t *st, const uint32_t *ids, uint32_t n) {
  uint32_t m = n > PRT_MAX_LAYER_TENSORS ? PRT_MAX_LAYER_TENSORS : n;
  if (!st) return;
  st->fix_tensor_count = m;
  for (uint32_t i = 0; i < m; ++i) st->fix_tensor_ids[i] = ids[i];
}

static void stage_set_inner_isolate_ids(prt_stage_map_t *st, const uint32_t *ids, uint32_t n) {
  uint32_t m = n > PRT_MAX_LAYER_TENSORS ? PRT_MAX_LAYER_TENSORS : n;
  if (!st) return;
  st->inner_isolate_count = m;
  for (uint32_t i = 0; i < m; ++i) st->inner_isolate_ids[i] = ids[i];
}

static void stage_set_inner_shared_ids(prt_stage_map_t *st, const uint32_t *ids, uint32_t n) {
  uint32_t m = n > PRT_MAX_LAYER_TENSORS ? PRT_MAX_LAYER_TENSORS : n;
  if (!st) return;
  st->inner_shared_count = m;
  for (uint32_t i = 0; i < m; ++i) st->inner_shared_ids[i] = ids[i];
}

static void stage_set_dram_bypass(prt_stage_map_t *st, const uint32_t *vals, uint32_t n) {
  uint32_t m = n > PRT_MAX_LAYER_TENSORS ? PRT_MAX_LAYER_TENSORS : n;
  if (!st) return;
  st->dram_bypass_count = m;
  if (m > 0) memcpy(st->dram_bypass, vals, sizeof(uint32_t) * m);
}

static void stage_set_spm_bypass(prt_stage_map_t *st, const uint32_t *vals, uint32_t n) {
  uint32_t m = n > PRT_MAX_LAYER_TENSORS ? PRT_MAX_LAYER_TENSORS : n;
  if (!st) return;
  st->spm_bypass_count = m;
  if (m > 0) memcpy(st->spm_bypass, vals, sizeof(uint32_t) * m);
}

static void stage_set_physical_acc_ids(prt_stage_map_t *st, const uint32_t *vals, uint32_t n) {
  uint32_t m = n > PRT_MAX_CORES ? PRT_MAX_CORES : n;
  if (!st) return;
  st->num_physical_acc_ids = m;
  st->physical_acc_ids_present = m > 0 ? 1U : 0U;
  for (uint32_t i = 0; i < m; ++i) st->physical_acc_ids[i] = vals[i];
}

static void stage_set_virtual_acc_ids(prt_stage_map_t *st, const uint32_t *vals, uint32_t n) {
  uint32_t m = n > PRT_MAX_CORES ? PRT_MAX_CORES : n;
  if (!st) return;
  st->num_virtual_acc_ids = m;
  st->virtual_acc_ids_present = m > 0 ? 1U : 0U;
  for (uint32_t i = 0; i < m; ++i) st->virtual_acc_ids[i] = vals[i];
}

static void stage_set_tensor_usage_count(prt_stage_map_t *st, const uint32_t *vals, uint32_t n) {
  uint32_t m = n > PRT_MAX_LAYER_TENSORS ? PRT_MAX_LAYER_TENSORS : n;
  if (!st) return;
  st->tensor_usage_count_present = 1U;
  st->tensor_usage_count_count = m;
  for (uint32_t i = 0; i < m; ++i) st->tensor_usage_count[i] = vals[i];
}

static void stage_set_tensor_lazy_fetch(prt_stage_map_t *st, const uint32_t *vals, uint32_t n) {
  uint32_t m = n > PRT_MAX_LAYER_TENSORS ? PRT_MAX_LAYER_TENSORS : n;
  if (!st) return;
  st->tensor_lazy_fetch_present = 1U;
  st->tensor_lazy_fetch_count = m;
  for (uint32_t i = 0; i < m; ++i) st->tensor_lazy_fetch[i] = vals[i];
}

static void stage_set_local_spm_tensor_addr(prt_stage_map_t *st, const uint32_t *vals, uint32_t n) {
  uint32_t m = n > PRT_MAX_LAYER_TENSORS ? PRT_MAX_LAYER_TENSORS : n;
  if (!st) return;
  st->local_spm_tensor_count = m;
  for (uint32_t i = 0; i < m; ++i) st->local_spm_tensor_addr[i] = vals[i];
}

static void stage_set_local_spm_first_vpage(prt_stage_map_t *st, const uint32_t *vals, uint32_t n) {
  uint32_t m = n > PRT_MAX_LAYER_TENSORS ? PRT_MAX_LAYER_TENSORS : n;
  if (!st) return;
  if (st->local_spm_tensor_count < m) st->local_spm_tensor_count = m;
  for (uint32_t i = 0; i < m; ++i) st->local_spm_first_vpage[i] = vals[i];
}

static void stage_set_local_spm_page_count(prt_stage_map_t *st, const uint32_t *vals, uint32_t n) {
  uint32_t m = n > PRT_MAX_LAYER_TENSORS ? PRT_MAX_LAYER_TENSORS : n;
  if (!st) return;
  if (st->local_spm_tensor_count < m) st->local_spm_tensor_count = m;
  for (uint32_t i = 0; i < m; ++i) st->local_spm_page_count[i] = vals[i];
}

static void stage_set_local_spm_tensor_bytes(prt_stage_map_t *st, const uint32_t *vals, uint32_t n) {
  uint32_t m = n > PRT_MAX_LAYER_TENSORS ? PRT_MAX_LAYER_TENSORS : n;
  if (!st) return;
  if (st->local_spm_tensor_count < m) st->local_spm_tensor_count = m;
  for (uint32_t i = 0; i < m; ++i) st->local_spm_tensor_bytes[i] = vals[i];
}

static uint32_t parse_buffer_binding_kind_str(const char *s) {
  if (!s) return PRT_BUFFER_BINDING_UNKNOWN;
  if (!strcmp(s, "WEIGHT")) return PRT_BUFFER_BINDING_WEIGHT;
  if (!strcmp(s, "PIPE")) return PRT_BUFFER_BINDING_PIPE;
  if (!strcmp(s, "RING")) return PRT_BUFFER_BINDING_RING;
  return PRT_BUFFER_BINDING_UNKNOWN;
}

static void segment_set_buffer_binding_u32(prt_segment_desc_t *seg, const uint32_t *vals, uint32_t n,
                                           uint32_t field) {
  if (!seg || !vals) return;
  if (ensure_buffer_binding_capacity(seg, n) != PRT_OK) return;
  for (uint32_t i = 0; i < n; ++i) {
    prt_buffer_binding_t *b = &seg->buffer_bindings[i];
    switch (field) {
      case 0: b->buffer_id = vals[i]; break;
      case 1: b->tensor_id = vals[i]; break;
      case 2: b->stage_local_id = vals[i]; break;
      case 3: b->is_entry = vals[i]; break;
      case 4: b->slot_count = vals[i]; break;
      case 5: b->pages_per_slot = vals[i]; break;
      case 6: b->alias_group_id = vals[i]; break;
      default: break;
    }
  }
}

static void segment_set_buffer_binding_kinds(prt_segment_desc_t *seg, char **vals, uint32_t n) {
  if (!seg || !vals) return;
  if (ensure_buffer_binding_capacity(seg, n) != PRT_OK) return;
  for (uint32_t i = 0; i < n; ++i) {
    seg->buffer_bindings[i].kind = parse_buffer_binding_kind_str(vals[i]);
  }
}

static uint32_t parse_split_kind_str(const char *s) {
  if (!s) return PRT_LAYER_SPLIT_UNSPEC;
  if (!strcmp(s, "single")) return PRT_LAYER_SPLIT_SINGLE;
  if (!strcmp(s, "oc")) return PRT_LAYER_SPLIT_OC;
  if (!strcmp(s, "spatial")) return PRT_LAYER_SPLIT_SPATIAL;
  if (!strcmp(s, "resadd_spatial")) return PRT_LAYER_SPLIT_RESADD_SPATIAL;
  return PRT_LAYER_SPLIT_UNSPEC;
}

static void stage_set_split_kind_from_value(prt_stage_map_t *st, char *v) {
  size_t n;
  if (!st || !v) return;
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
  st->split_kind = parse_split_kind_str(v);
}

int prt_load_model_yaml(const char *path, prt_model_desc_t *out) {
  char *buf = NULL;
  size_t len = 0;
  int rc;
  prt_model_layer_t *cur_layer = NULL;
  int cur_layer_has_index = 0;
  uint32_t line_no = 0;
  if (!out) return PRT_ERR_INVAL;
  memset(out, 0, sizeof(*out));
  PRT_PROGRESS_LOG("yaml model load begin path=%s", path ? path : "(null)");

  rc = load_file(path, &buf, &len);
  if (rc != PRT_OK) return rc;
  PRT_PROGRESS_LOG("yaml model file read path=%s bytes=%zu", path ? path : "(null)", len);
  PRT_PROGRESS_LOG("yaml model parse begin path=%s bytes=%zu", path ? path : "(null)", len);

  char *p = buf;
  while (p && *p) {
    char *line = p;
    char *nl = strchr(p, '\n');
    int indent;
    if (nl) {
      *nl = '\0';
      p = nl + 1;
    } else {
      p = NULL;
    }
    line_no++;
    rtrim(line);
    indent = count_indent(line);
    char *t = ltrim(line);
    int is_list_item = (t[0] == '-');
    if (*t == '\0' || *t == '#') continue;
    if (line_no <= 12 || (line_no % 64U) == 0U) {
      PRT_PROGRESS_LOG("yaml model parse line=%u indent=%d list=%d text=%s", line_no, indent,
                       is_list_item, t);
    }

    if (starts_key(t, "address")) {
      uint64_t *vals = NULL;
      uint32_t n = 0;
      rc = parse_u64_list_from_value(value_after_colon(t), &vals, &n);
      if (rc != PRT_OK) {
        PRT_PROGRESS_LOG("yaml model parse error line=%u key=address rc=%d text=%s", line_no, rc,
                         t);
        free(buf);
        return rc;
      }
      if (n > 0) {
        if (is_list_item) {
          if (!cur_layer || cur_layer_has_index) {
            if (push_model_layer(out, &cur_layer) != PRT_OK) {
              free(vals);
              free(buf);
              return PRT_ERR_NOMEM;
            }
            cur_layer_has_index = 0;
            if (out->num_layers <= 8 || (out->num_layers % 8U) == 0U) {
              PRT_PROGRESS_LOG("yaml model layer-push line=%u reason=address layers=%u",
                               line_no, out->num_layers);
            }
          }
        }
        if (!is_list_item && indent == 0) {
          out->addr_base = vals[0];
          out->addr_end = (n > 1) ? vals[1] : vals[0];
        } else if (cur_layer) {
          cur_layer->address_count = n > 8 ? 8 : n;
          for (uint32_t i = 0; i < cur_layer->address_count; ++i) cur_layer->address[i] = vals[i];
        }
      }
      free(vals);
      continue;
    }

    if (starts_key(t, "index")) {
      uint32_t idx = 0;
      rc = parse_u32_scalar(t, &idx);
      if (rc != PRT_OK) {
        PRT_PROGRESS_LOG("yaml model parse error line=%u key=index rc=%d text=%s", line_no, rc,
                         t);
        free(buf);
        return rc;
      }
      if (!cur_layer || cur_layer_has_index) {
        if (push_model_layer(out, &cur_layer) != PRT_OK) {
          free(buf);
          return PRT_ERR_NOMEM;
        }
        if (out->num_layers <= 8 || (out->num_layers % 8U) == 0U) {
          PRT_PROGRESS_LOG("yaml model layer-push line=%u reason=index layers=%u",
                           line_no, out->num_layers);
        }
      }
      if (!cur_layer) {
        free(buf);
        return PRT_ERR_NOMEM;
      }
      cur_layer->index = idx;
      cur_layer_has_index = 1;
      if (out->num_layers <= 8 || (out->num_layers % 8U) == 0U) {
        PRT_PROGRESS_LOG("yaml model layer-index line=%u layers=%u index=%u", line_no,
                         out->num_layers, idx);
      }
      continue;
    }

    if (!cur_layer) continue;

    if (starts_key(t, "type")) {
      char *v = value_after_colon(t);
      if (v) {
        size_t n;
        if (*v == '"' || *v == '\'') {
          char q = *v;
          v++;
          char *e = strchr(v, q);
          if (e) *e = '\0';
        }
        n = strlen(v);
        while (n > 0 && isspace((unsigned char)v[n - 1])) {
          v[n - 1] = '\0';
          n--;
        }
        strncpy(cur_layer->type, v, sizeof(cur_layer->type) - 1);
        cur_layer->type[sizeof(cur_layer->type) - 1] = '\0';
      }
      continue;
    }

    if (starts_key(t, "param")) {
      uint32_t *vals = NULL;
      uint32_t n = 0;
      rc = parse_int_list_from_value(value_after_colon(t), &vals, &n);
      if (rc != PRT_OK) {
        PRT_PROGRESS_LOG("yaml model parse error line=%u key=param rc=%d text=%s", line_no, rc,
                         t);
        free(buf);
        return rc;
      }
      cur_layer->param_len = n > 16 ? 16 : n;
      for (uint32_t i = 0; i < cur_layer->param_len; ++i) cur_layer->param[i] = vals[i];
      free(vals);
      continue;
    }

    if (starts_key(t, "tensorIds")) {
      uint32_t *vals = NULL;
      uint32_t n = 0;
      rc = parse_int_list_from_value(value_after_colon(t), &vals, &n);
      if (rc != PRT_OK) {
        PRT_PROGRESS_LOG("yaml model parse error line=%u key=tensorIds rc=%d text=%s",
                         line_no, rc, t);
        free(buf);
        return rc;
      }
      cur_layer->tensor_count = n > 8 ? 8 : n;
      for (uint32_t i = 0; i < cur_layer->tensor_count; ++i) cur_layer->tensor_ids[i] = vals[i];
      free(vals);
      continue;
    }

    if (starts_key(t, "tensorStride")) {
      uint32_t *vals = NULL;
      uint32_t n = 0;
      rc = parse_int_list_from_value(value_after_colon(t), &vals, &n);
      if (rc != PRT_OK) {
        PRT_PROGRESS_LOG("yaml model parse error line=%u key=tensorStride rc=%d text=%s",
                         line_no, rc, t);
        free(buf);
        return rc;
      }
      cur_layer->tensor_stride_count = n > 8 ? 8 : n;
      for (uint32_t i = 0; i < cur_layer->tensor_stride_count; ++i) cur_layer->tensor_stride[i] = vals[i];
      free(vals);
      continue;
    }

    if (starts_key(t, "tensorSize")) {
      uint32_t *vals = NULL;
      uint32_t n = 0;
      rc = parse_int_list_from_value(value_after_colon(t), &vals, &n);
      if (rc != PRT_OK) {
        PRT_PROGRESS_LOG("yaml model parse error line=%u key=tensorSize rc=%d text=%s",
                         line_no, rc, t);
        free(buf);
        return rc;
      }
      cur_layer->tensor_size_count = n > 8 ? 8 : n;
      for (uint32_t i = 0; i < cur_layer->tensor_size_count; ++i) cur_layer->tensor_size[i] = vals[i];
      free(vals);
      continue;
    }

    if (starts_key(t, "address2")) {
      uint64_t *vals = NULL;
      uint32_t n = 0;
      rc = parse_u64_list_from_value(value_after_colon(t), &vals, &n);
      if (rc != PRT_OK) {
        PRT_PROGRESS_LOG("yaml model parse error line=%u key=address2 rc=%d text=%s", line_no,
                         rc, t);
        free(buf);
        return rc;
      }
      cur_layer->address2_count = n > 8 ? 8 : n;
      for (uint32_t i = 0; i < cur_layer->address2_count; ++i) cur_layer->address2[i] = vals[i];
      free(vals);
      continue;
    }
  }

  out->num_stages = 0;
  out->stages = NULL;
  out->num_tensors = 0;
  out->tensors = NULL;
  PRT_PROGRESS_LOG("yaml model parse end path=%s lines=%u layers=%u", path ? path : "(null)",
                   line_no, out->num_layers);
  free(buf);
  PRT_PROGRESS_LOG("yaml model load end path=%s layers=%u tensors=%u stages=%u",
                   path ? path : "(null)", out->num_layers, out->num_tensors, out->num_stages);
  return PRT_OK;
}

int prt_load_pipeline_yaml(const char *path, prt_pipeline_desc_t *out) {
  char *buf = NULL;
  size_t len = 0;
  int rc;
  prt_segment_desc_t *cur_seg = NULL;
  prt_stage_map_t *cur_stage = NULL;
  int in_stage_spm_util_list = 0;
  int stage_spm_util_indent = -1;
  uint32_t stage_spm_util_idx = 0;
  int in_stage_dram_bypass_list = 0;
  int stage_dram_bypass_indent = -1;
  int in_stage_spm_bypass_list = 0;
  int stage_spm_bypass_indent = -1;
  int in_stage_virtual_acc_list = 0;
  uint32_t stage_virtual_acc_rows = 0;
  int in_stage_physical_acc_list = 0;
  uint32_t stage_physical_acc_rows = 0;
  int in_stage_tensor_usage_list = 0;
  uint32_t stage_tensor_usage_rows = 0;
  int in_stage_tensor_lazy_fetch_list = 0;
  uint32_t stage_tensor_lazy_fetch_rows = 0;
  int cur_stage_has_global_id = 0;
  int cur_seg_has_segment_idx = 0;
  uint32_t total_stages = 0;
  uint32_t line_no = 0;

  if (!out) return PRT_ERR_INVAL;
  memset(out, 0, sizeof(*out));
  PRT_PROGRESS_LOG("yaml pipeline load begin path=%s", path ? path : "(null)");

  rc = load_file(path, &buf, &len);
  if (rc != PRT_OK) return rc;
  PRT_PROGRESS_LOG("yaml pipeline file read path=%s bytes=%zu", path ? path : "(null)", len);
  PRT_PROGRESS_LOG("yaml pipeline parse begin path=%s bytes=%zu", path ? path : "(null)", len);

  char *p = buf;
  while (p && *p) {
    char *line = p;
    char *nl = strchr(p, '\n');
    int indent;
    if (nl) {
      *nl = '\0';
      p = nl + 1;
    } else {
      p = NULL;
    }

    line_no++;
    rtrim(line);
    indent = count_indent(line);
    char *t = ltrim(line);
    if (*t == '\0' || *t == '#') continue;
    if (yaml_line_log_enabled()) {
      PRT_PROGRESS_LOG("yaml pipeline parse line=%u indent=%d text=%.160s", line_no, indent, t);
    }

    if (in_stage_spm_util_list && cur_seg) {
      if (indent <= stage_spm_util_indent && *t != '-') {
        in_stage_spm_util_list = 0;
      } else {
        map_kv_t *kv = NULL;
        uint32_t n = 0;
        if (*t == '-' || *t == '{') {
          if (parse_u32_map_from_value(t, &kv, &n) == PRT_OK) {
            if (ensure_stage_spm_util_capacity(cur_seg, stage_spm_util_idx + 1) != PRT_OK) {
              free(kv);
              free(buf);
              return PRT_ERR_NOMEM;
            }
            if (u32_map_set_from_kv(&cur_seg->tensor_spm_util_in_stage[stage_spm_util_idx], kv, n) != PRT_OK) {
              free(kv);
              free(buf);
              return PRT_ERR_NOMEM;
            }
            stage_spm_util_idx += 1;
          }
          free(kv);
          continue;
        }
      }
    }

    if (in_stage_dram_bypass_list && cur_stage) {
      if (indent <= stage_dram_bypass_indent && *t != '-') {
        in_stage_dram_bypass_list = 0;
      } else if (*t == '-') {
        uint32_t *vals = NULL;
        uint32_t n = 0;
        if (parse_int_list_from_value(t, &vals, &n) != PRT_OK) {
          free(buf);
          return PRT_ERR_PARSE;
        }
        stage_set_dram_bypass(cur_stage, vals, n);
        free(vals);
        in_stage_dram_bypass_list = 0;
        continue;
      }
    }

    if (in_stage_spm_bypass_list && cur_stage) {
      if (indent <= stage_spm_bypass_indent && *t != '-') {
        in_stage_spm_bypass_list = 0;
      } else if (*t == '-') {
        uint32_t *vals = NULL;
        uint32_t n = 0;
        if (parse_int_list_from_value(t, &vals, &n) != PRT_OK) {
          free(buf);
          return PRT_ERR_PARSE;
        }
        stage_set_spm_bypass(cur_stage, vals, n);
        free(vals);
        in_stage_spm_bypass_list = 0;
        continue;
      }
    }

    if (in_stage_virtual_acc_list && cur_stage) {
      if (!(*t == '-' && strchr(t, '['))) {
        in_stage_virtual_acc_list = 0;
      } else if (*t == '-') {
        uint32_t *vals = NULL;
        uint32_t n = 0;
        if (stage_virtual_acc_rows > 0) {
          free(buf);
          return PRT_ERR_NOT_IMPL;
        }
        if (parse_int_list_from_value(t, &vals, &n) != PRT_OK) {
          free(buf);
          return PRT_ERR_PARSE;
        }
        stage_set_virtual_acc_ids(cur_stage, vals, n);
        stage_virtual_acc_rows += 1U;
        free(vals);
        continue;
      }
    }

    if (in_stage_physical_acc_list && cur_stage) {
      if (!(*t == '-' && strchr(t, '['))) {
        in_stage_physical_acc_list = 0;
      } else if (*t == '-') {
        uint32_t *vals = NULL;
        uint32_t n = 0;
        if (stage_physical_acc_rows > 0) {
          free(buf);
          return PRT_ERR_NOT_IMPL;
        }
        if (parse_int_list_from_value(t, &vals, &n) != PRT_OK) {
          free(buf);
          return PRT_ERR_PARSE;
        }
        stage_set_physical_acc_ids(cur_stage, vals, n);
        stage_physical_acc_rows += 1U;
        free(vals);
        continue;
      }
    }

    if (in_stage_tensor_usage_list && cur_stage) {
      if (!(*t == '-' && strchr(t, '['))) {
        in_stage_tensor_usage_list = 0;
      } else if (*t == '-') {
        uint32_t *vals = NULL;
        uint32_t n = 0;
        if (stage_tensor_usage_rows > 0) {
          free(buf);
          return PRT_ERR_NOT_IMPL;
        }
        if (parse_int_list_from_value(t, &vals, &n) != PRT_OK) {
          free(buf);
          return PRT_ERR_PARSE;
        }
        stage_set_tensor_usage_count(cur_stage, vals, n);
        stage_tensor_usage_rows += 1U;
        free(vals);
        continue;
      }
    }

    if (in_stage_tensor_lazy_fetch_list && cur_stage) {
      if (!(*t == '-' && strchr(t, '['))) {
        in_stage_tensor_lazy_fetch_list = 0;
      } else if (*t == '-') {
        uint32_t *vals = NULL;
        uint32_t n = 0;
        if (stage_tensor_lazy_fetch_rows > 0) {
          free(buf);
          return PRT_ERR_NOT_IMPL;
        }
        if (parse_int_list_from_value(t, &vals, &n) != PRT_OK) {
          free(buf);
          return PRT_ERR_PARSE;
        }
        stage_set_tensor_lazy_fetch(cur_stage, vals, n);
        stage_tensor_lazy_fetch_rows += 1U;
        free(vals);
        continue;
      }
    }

    if (t[0] == '-' && strstr(t, "acc_util:")) {
      if (ensure_segment_capacity(out, out->num_segments + 1) != PRT_OK) {
        free(buf);
        return PRT_ERR_NOMEM;
      }
      cur_seg = &out->segments[out->num_segments++];
      memset(cur_seg, 0, sizeof(*cur_seg));
      cur_seg->segment_idx = out->num_segments - 1;
      cur_seg->subbatch_size = 1;
      cur_seg_has_segment_idx = 0;
      cur_stage = NULL;
      cur_stage_has_global_id = 0;
      in_stage_spm_util_list = 0;
      stage_spm_util_indent = -1;
      stage_spm_util_idx = 0;
      in_stage_dram_bypass_list = 0;
      stage_dram_bypass_indent = -1;
      in_stage_spm_bypass_list = 0;
      stage_spm_bypass_indent = -1;
      in_stage_virtual_acc_list = 0;
      stage_virtual_acc_rows = 0;
      in_stage_physical_acc_list = 0;
      stage_physical_acc_rows = 0;
      in_stage_tensor_usage_list = 0;
      stage_tensor_usage_rows = 0;
      in_stage_tensor_lazy_fetch_list = 0;
      stage_tensor_lazy_fetch_rows = 0;
      PRT_PROGRESS_LOG("yaml pipeline segment-push line=%u segments=%u", line_no, out->num_segments);
      continue;
    }

    if (starts_key(t, "segment_idx")) {
      uint32_t seg_idx = 0;
      if (parse_u32_scalar(t, &seg_idx) != PRT_OK) continue;
      if (!cur_seg || cur_seg_has_segment_idx) {
        if (ensure_segment_capacity(out, out->num_segments + 1) != PRT_OK) {
          free(buf);
          return PRT_ERR_NOMEM;
        }
        cur_seg = &out->segments[out->num_segments++];
        memset(cur_seg, 0, sizeof(*cur_seg));
        cur_seg->subbatch_size = 1;
        cur_stage = NULL;
        cur_stage_has_global_id = 0;
        in_stage_spm_util_list = 0;
        stage_spm_util_indent = -1;
        stage_spm_util_idx = 0;
        in_stage_dram_bypass_list = 0;
        stage_dram_bypass_indent = -1;
        in_stage_spm_bypass_list = 0;
        stage_spm_bypass_indent = -1;
        in_stage_virtual_acc_list = 0;
        stage_virtual_acc_rows = 0;
        in_stage_physical_acc_list = 0;
        stage_physical_acc_rows = 0;
        in_stage_tensor_usage_list = 0;
        stage_tensor_usage_rows = 0;
        in_stage_tensor_lazy_fetch_list = 0;
        stage_tensor_lazy_fetch_rows = 0;
        PRT_PROGRESS_LOG("yaml pipeline segment-push line=%u reason=segment_idx segments=%u",
                         line_no, out->num_segments);
      }
      cur_seg->segment_idx = seg_idx;
      cur_seg_has_segment_idx = 1;
      PRT_PROGRESS_LOG("yaml pipeline segment-idx line=%u segment=%u", line_no, seg_idx);
      continue;
    }

    if (!cur_seg) continue;

    if (starts_key(t, "subBatchSize")) {
      uint32_t sb = 1;
      if (parse_u32_scalar(t, &sb) == PRT_OK && sb > 0) cur_seg->subbatch_size = sb;
      continue;
    }

    if (starts_key(t, "start_layer_idx") ||
        starts_key(t, "end_layer_idx") ||
        starts_key(t, "cost") ||
        starts_key(t, "total_spm_util")) {
      uint32_t scalar = 0;
      const char *key;
      (void)parse_u32_scalar(t, &scalar);
      key = key_name(t);
      (void)key;
      PRT_PROGRESS_LOG("yaml pipeline ignore-scalar line=%u key=%s value=%u",
                       line_no, key ? key : "(null)", scalar);
      continue;
    }

    if (starts_key(t, "segmentSpmPageSpan")) {
      (void)parse_u32_scalar(t, &cur_seg->segment_spm_page_span);
      PRT_PROGRESS_LOG("yaml pipeline segment-span line=%u segment=%u pages=%u",
                       line_no, cur_seg->segment_idx, cur_seg->segment_spm_page_span);
      continue;
    }

    if (starts_key(t, "ring_buffer_count") || starts_key(t, "ring_buffer_size_per") || starts_key(t, "ring_buffer_use_count")) {
      map_kv_t *kv = NULL;
      uint32_t n = 0;
      if (parse_u32_map_from_value(t, &kv, &n) == PRT_OK) {
        for (uint32_t i = 0; i < n; ++i) {
          prt_ring_cfg_t *cfg = find_or_add_ring_cfg(cur_seg, kv[i].key);
          if (!cfg) {
            free(kv);
            free(buf);
            return PRT_ERR_NOMEM;
          }
          if (starts_key(t, "ring_buffer_count")) cfg->count = kv[i].value;
          if (starts_key(t, "ring_buffer_size_per")) cfg->size_per = kv[i].value;
          if (starts_key(t, "ring_buffer_use_count")) cfg->use_count = kv[i].value;
        }
      }
      free(kv);
      continue;
    }

    if (starts_key(t, "tensor_spm_util_shared")) {
      map_kv_t *kv = NULL;
      uint32_t n = 0;
      if (parse_u32_map_from_value(t, &kv, &n) == PRT_OK) {
        if (u32_map_set_from_kv(&cur_seg->tensor_spm_util_shared, kv, n) != PRT_OK) {
          free(kv);
          free(buf);
          return PRT_ERR_NOMEM;
        }
      }
      free(kv);
      continue;
    }

    if (starts_key(t, "transport_effective_bytes")) {
      map_kv_t *kv = NULL;
      uint32_t n = 0;
      if (parse_u32_map_from_value(t, &kv, &n) == PRT_OK) {
        if (u32_map_set_from_kv(&cur_seg->transport_effective_bytes, kv, n) != PRT_OK) {
          free(kv);
          free(buf);
          return PRT_ERR_NOMEM;
        }
      }
      free(kv);
      continue;
    }

    if (starts_key(t, "ring_slot_effective_bytes")) {
      map_kv_t *kv = NULL;
      uint32_t n = 0;
      if (parse_u32_map_from_value(t, &kv, &n) == PRT_OK) {
        if (u32_map_set_from_kv(&cur_seg->ring_slot_effective_bytes, kv, n) != PRT_OK) {
          free(kv);
          free(buf);
          return PRT_ERR_NOMEM;
        }
      }
      free(kv);
      continue;
    }

    if (starts_key(t, "shared_tensor_is_read_first")) {
      map_kv_t *kv = NULL;
      uint32_t n = 0;
      PRT_PROGRESS_LOG("yaml pipeline shared-read-first begin line=%u segment=%u",
                       line_no, cur_seg->segment_idx);
      PRT_CRIT_LOG("yaml-srf-begin line=%u segment=%u", line_no, cur_seg->segment_idx);
      if (parse_u32_map_from_value(t, &kv, &n) == PRT_OK) {
        PRT_PROGRESS_LOG("yaml pipeline shared-read-first parsed line=%u segment=%u count=%u",
                         line_no, cur_seg->segment_idx, n);
        PRT_CRIT_LOG("yaml-srf-parsed line=%u segment=%u count=%u",
                     line_no, cur_seg->segment_idx, n);
        if (u32_map_set_from_kv(&cur_seg->shared_tensor_is_read_first, kv, n) != PRT_OK) {
          free(kv);
          free(buf);
          return PRT_ERR_NOMEM;
        }
        PRT_PROGRESS_LOG("yaml pipeline shared-read-first stored line=%u segment=%u count=%u",
                         line_no, cur_seg->segment_idx, n);
        PRT_CRIT_LOG("yaml-srf-stored line=%u segment=%u count=%u",
                     line_no, cur_seg->segment_idx, n);
      } else {
        PRT_PROGRESS_LOG("yaml pipeline shared-read-first parse-fail line=%u segment=%u",
                         line_no, cur_seg->segment_idx);
        PRT_CRIT_LOG("yaml-srf-parse-fail line=%u segment=%u",
                     line_no, cur_seg->segment_idx);
      }
      free(kv);
      continue;
    }

    if (starts_key(t, "tensor_spm_util_in_ringbuffer")) {
      map_kv_t *kv = NULL;
      uint32_t n = 0;
      if (parse_u32_map_from_value(t, &kv, &n) == PRT_OK) {
        if (u32_map_set_from_kv(&cur_seg->tensor_spm_util_in_ringbuffer, kv, n) != PRT_OK) {
          free(kv);
          free(buf);
          return PRT_ERR_NOMEM;
        }
      }
      free(kv);
      continue;
    }

    if (starts_key(t, "tensor_spm_util_weight")) {
      map_kv_t *kv = NULL;
      uint32_t n = 0;
      if (parse_u32_map_from_value(t, &kv, &n) == PRT_OK) {
        if (u32_map_set_from_kv(&cur_seg->tensor_spm_util_weight, kv, n) != PRT_OK) {
          free(kv);
          free(buf);
          return PRT_ERR_NOMEM;
        }
      }
      free(kv);
      continue;
    }

    if (starts_key(t, "tensor_spm_util_in_stage")) {
      in_stage_spm_util_list = 1;
      stage_spm_util_indent = indent;
      stage_spm_util_idx = 0;
      continue;
    }

    if (starts_key(t, "bufferBindingIdList")) {
      uint32_t *vals = NULL;
      uint32_t n = 0;
      if (parse_int_list_from_value(value_after_colon(t), &vals, &n) == PRT_OK) {
        segment_set_buffer_binding_u32(cur_seg, vals, n, 0);
        PRT_PROGRESS_LOG("yaml pipeline binding-ids line=%u segment=%u count=%u",
                         line_no, cur_seg->segment_idx, n);
      }
      free(vals);
      continue;
    }

    if (starts_key(t, "bufferBindingTensorIdList")) {
      uint32_t *vals = NULL;
      uint32_t n = 0;
      if (parse_int_list_from_value(value_after_colon(t), &vals, &n) == PRT_OK) {
        segment_set_buffer_binding_u32(cur_seg, vals, n, 1);
        PRT_PROGRESS_LOG("yaml pipeline binding-tensor-ids line=%u segment=%u count=%u",
                         line_no, cur_seg->segment_idx, n);
      }
      free(vals);
      continue;
    }

    if (starts_key(t, "bufferBindingStageLocalIdList")) {
      uint32_t *vals = NULL;
      uint32_t n = 0;
      if (parse_int_list_from_value(value_after_colon(t), &vals, &n) == PRT_OK) {
        segment_set_buffer_binding_u32(cur_seg, vals, n, 2);
        PRT_PROGRESS_LOG("yaml pipeline binding-stage-local-ids line=%u segment=%u count=%u",
                         line_no, cur_seg->segment_idx, n);
      }
      free(vals);
      continue;
    }

    if (starts_key(t, "bufferBindingIsEntryList")) {
      uint32_t *vals = NULL;
      uint32_t n = 0;
      if (parse_int_list_from_value(value_after_colon(t), &vals, &n) == PRT_OK) {
        segment_set_buffer_binding_u32(cur_seg, vals, n, 3);
        PRT_PROGRESS_LOG("yaml pipeline binding-is-entry line=%u segment=%u count=%u",
                         line_no, cur_seg->segment_idx, n);
      }
      free(vals);
      continue;
    }

    if (starts_key(t, "bufferBindingKindList")) {
      char **vals = NULL;
      uint32_t n = 0;
      if (parse_str_list_from_value(value_after_colon(t), &vals, &n) == PRT_OK) {
        segment_set_buffer_binding_kinds(cur_seg, vals, n);
        PRT_PROGRESS_LOG("yaml pipeline binding-kinds line=%u segment=%u count=%u",
                         line_no, cur_seg->segment_idx, n);
      }
      for (uint32_t i = 0; i < n; ++i) free(vals[i]);
      free(vals);
      continue;
    }

    if (starts_key(t, "bufferBindingSlotCountList")) {
      uint32_t *vals = NULL;
      uint32_t n = 0;
      if (parse_int_list_from_value(value_after_colon(t), &vals, &n) == PRT_OK) {
        segment_set_buffer_binding_u32(cur_seg, vals, n, 4);
        PRT_PROGRESS_LOG("yaml pipeline binding-slot-count line=%u segment=%u count=%u",
                         line_no, cur_seg->segment_idx, n);
      }
      free(vals);
      continue;
    }

    if (starts_key(t, "bufferBindingPagesPerSlotList")) {
      uint32_t *vals = NULL;
      uint32_t n = 0;
      if (parse_int_list_from_value(value_after_colon(t), &vals, &n) == PRT_OK) {
        segment_set_buffer_binding_u32(cur_seg, vals, n, 5);
        PRT_PROGRESS_LOG("yaml pipeline binding-pages-per-slot line=%u segment=%u count=%u",
                         line_no, cur_seg->segment_idx, n);
      }
      free(vals);
      continue;
    }

    if (starts_key(t, "bufferBindingAliasGroupIdList")) {
      uint32_t *vals = NULL;
      uint32_t n = 0;
      if (parse_int_list_from_value(value_after_colon(t), &vals, &n) == PRT_OK) {
        segment_set_buffer_binding_u32(cur_seg, vals, n, 6);
        PRT_PROGRESS_LOG("yaml pipeline binding-alias-group line=%u segment=%u count=%u",
                         line_no, cur_seg->segment_idx, n);
      }
      free(vals);
      continue;
    }

    if (t[0] == '-' && strstr(t, "accUtil:") && strncmp(t, "- -", 3) != 0) {
      free(buf);
      return PRT_ERR_NOT_IMPL;
    }

    if (strncmp(t, "- -", 3) == 0 && strstr(t, "accUtil:")) {
      uint32_t acc = 1;
      if (ensure_stage_capacity(cur_seg, cur_seg->num_stages + 1) != PRT_OK) {
        free(buf);
        return PRT_ERR_NOMEM;
      }
      cur_stage = &cur_seg->stages[cur_seg->num_stages++];
      memset(cur_stage, 0, sizeof(*cur_stage));
      cur_stage->stage_id = cur_seg->num_stages - 1;
      cur_stage->layer_id = 0;
      if (parse_accutil_anywhere(t, &acc) == PRT_OK) {
        cur_stage->acc_util = acc;
        cur_stage->acc_util_present = 1;
      }
      cur_stage_has_global_id = 0;
      in_stage_virtual_acc_list = 0;
      stage_virtual_acc_rows = 0;
      in_stage_physical_acc_list = 0;
      stage_physical_acc_rows = 0;
      PRT_PROGRESS_LOG("yaml pipeline stage-push line=%u segment=%u local_stage=%u acc=%u",
                       line_no, cur_seg->segment_idx, cur_seg->num_stages - 1, cur_stage->acc_util);
      continue;
    }

    if (strstr(t, "globalStageId:")) {
      uint32_t sid = 0;
      if (parse_u32_scalar(t, &sid) != PRT_OK) sid = cur_seg->num_stages;
      if (!cur_stage || cur_stage_has_global_id) {
        if (ensure_stage_capacity(cur_seg, cur_seg->num_stages + 1) != PRT_OK) {
          free(buf);
          return PRT_ERR_NOMEM;
        }
        cur_stage = &cur_seg->stages[cur_seg->num_stages++];
        memset(cur_stage, 0, sizeof(*cur_stage));
        cur_stage->layer_id = 0;
      }
      cur_stage->stage_id = sid;
      cur_stage_has_global_id = 1;
      in_stage_virtual_acc_list = 0;
      stage_virtual_acc_rows = 0;
      in_stage_physical_acc_list = 0;
      stage_physical_acc_rows = 0;
      PRT_PROGRESS_LOG("yaml pipeline global-stage line=%u segment=%u local_stage=%u global_stage=%u",
                       line_no, cur_seg->segment_idx, cur_seg->num_stages - 1, sid);
      continue;
    }

    if (!cur_stage) continue;

    if (starts_key(t, "accUtil")) {
      uint32_t acc = 1;
      if (parse_u32_scalar(t, &acc) == PRT_OK) {
        cur_stage->acc_util = acc;
        cur_stage->acc_util_present = 1;
      }
      continue;
    }

    if (starts_key(t, "layerIdList")) {
      uint32_t *ids = NULL;
      uint32_t n = 0;
      if (parse_int_list_from_value(value_after_colon(t), &ids, &n) == PRT_OK) stage_set_layer_id(cur_stage, ids, n);
      free(ids);
      continue;
    }

    if (starts_key(t, "tensorIdList")) {
      uint32_t *ids = NULL;
      uint32_t n = 0;
      if (parse_int_list_from_value(value_after_colon(t), &ids, &n) == PRT_OK) stage_set_tensor_ids(cur_stage, ids, n);
      free(ids);
      continue;
    }

    if (starts_key(t, "dramBypassList")) {
      in_stage_dram_bypass_list = 1;
      stage_dram_bypass_indent = indent;
      continue;
    }

    if (starts_key(t, "spmBypassList")) {
      in_stage_spm_bypass_list = 1;
      stage_spm_bypass_indent = indent;
      continue;
    }

    if (starts_key(t, "physicalAccIds")) {
      uint32_t *ids = NULL;
      uint32_t n = 0;
      if (parse_int_list_from_value(value_after_colon(t), &ids, &n) == PRT_OK) {
        stage_set_physical_acc_ids(cur_stage, ids, n);
      }
      free(ids);
      continue;
    }

    if (starts_key(t, "vAccIdxList")) {
      uint32_t *ids = NULL;
      uint32_t n = 0;
      char *v = value_after_colon(t);
      stage_virtual_acc_rows = 0;
      if (v && strchr(v, '[')) {
        if (parse_int_list_from_value(v, &ids, &n) == PRT_OK) {
          stage_set_virtual_acc_ids(cur_stage, ids, n);
          stage_virtual_acc_rows = 1U;
        }
        free(ids);
      }
      in_stage_virtual_acc_list = 1;
      continue;
    }

    if (starts_key(t, "pAccIdxList")) {
      uint32_t *ids = NULL;
      uint32_t n = 0;
      char *v = value_after_colon(t);
      stage_physical_acc_rows = 0;
      if (v && strchr(v, '[')) {
        if (parse_int_list_from_value(v, &ids, &n) == PRT_OK) {
          stage_set_physical_acc_ids(cur_stage, ids, n);
          stage_physical_acc_rows = 1U;
        }
        free(ids);
      }
      in_stage_physical_acc_list = 1;
      continue;
    }

    if (starts_key(t, "splitKind")) {
      stage_set_split_kind_from_value(cur_stage, value_after_colon(t));
      continue;
    }

    if (starts_key(t, "execBaseVPage")) {
      (void)parse_u32_scalar(t, &cur_stage->exec_base_vpage);
      continue;
    }

    if (starts_key(t, "entryTensorIdList")) {
      uint32_t *ids = NULL;
      uint32_t n = 0;
      if (parse_int_list_from_value(value_after_colon(t), &ids, &n) == PRT_OK) stage_set_entry_ids(cur_stage, ids, n);
      free(ids);
      continue;
    }

    if (starts_key(t, "exportTensorIdList")) {
      uint32_t *ids = NULL;
      uint32_t n = 0;
      if (parse_int_list_from_value(value_after_colon(t), &ids, &n) == PRT_OK) stage_set_export_ids(cur_stage, ids, n);
      free(ids);
      continue;
    }

    if (starts_key(t, "entryTensorTypeList")) {
      char **types = NULL;
      uint32_t n = 0;
      if (parse_str_list_from_value(value_after_colon(t), &types, &n) == PRT_OK) stage_set_entry_types(cur_stage, types, n);
      for (uint32_t i = 0; i < n; ++i) free(types[i]);
      free(types);
      continue;
    }

    if (starts_key(t, "entryBufferIdList")) {
      uint32_t *ids = NULL;
      uint32_t n = 0;
      if (parse_int_list_from_value(value_after_colon(t), &ids, &n) == PRT_OK) {
        stage_set_entry_buffer_ids(cur_stage, ids, n);
      }
      free(ids);
      continue;
    }

    if (starts_key(t, "exportTensorTypeList")) {
      char **types = NULL;
      uint32_t n = 0;
      if (parse_str_list_from_value(value_after_colon(t), &types, &n) == PRT_OK) stage_set_export_types(cur_stage, types, n);
      for (uint32_t i = 0; i < n; ++i) free(types[i]);
      free(types);
      continue;
    }

    if (starts_key(t, "exportBufferIdList")) {
      uint32_t *ids = NULL;
      uint32_t n = 0;
      if (parse_int_list_from_value(value_after_colon(t), &ids, &n) == PRT_OK) {
        stage_set_export_buffer_ids(cur_stage, ids, n);
      }
      free(ids);
      continue;
    }

    if (starts_key(t, "fixTensorDramBypassIdList")) {
      uint32_t *ids = NULL;
      uint32_t n = 0;
      if (parse_int_list_from_value(value_after_colon(t), &ids, &n) == PRT_OK) {
        stage_set_fix_tensor_ids(cur_stage, ids, n);
      }
      free(ids);
      continue;
    }

    if (starts_key(t, "innerIsolateTensorId")) {
      uint32_t *ids = NULL;
      uint32_t n = 0;
      if (parse_int_list_from_value(value_after_colon(t), &ids, &n) == PRT_OK) {
        stage_set_inner_isolate_ids(cur_stage, ids, n);
      }
      free(ids);
      continue;
    }

    if (starts_key(t, "innerSharedTensorId")) {
      uint32_t *ids = NULL;
      uint32_t n = 0;
      if (parse_int_list_from_value(value_after_colon(t), &ids, &n) == PRT_OK) {
        stage_set_inner_shared_ids(cur_stage, ids, n);
      }
      free(ids);
      continue;
    }

    if (starts_key(t, "entryTensorDoubleBufferList")) {
      uint32_t *db = NULL;
      uint32_t n = 0;
      if (parse_int_list_from_value(value_after_colon(t), &db, &n) == PRT_OK) stage_set_entry_dbuf(cur_stage, db, n);
      free(db);
      continue;
    }

    if (starts_key(t, "exportTensorDoubleBufferList")) {
      uint32_t *db = NULL;
      uint32_t n = 0;
      if (parse_int_list_from_value(value_after_colon(t), &db, &n) == PRT_OK) stage_set_export_dbuf(cur_stage, db, n);
      free(db);
      continue;
    }

    if (starts_key(t, "tensorUsageCountList")) {
      uint32_t *vals = NULL;
      uint32_t n = 0;
      char *v = value_after_colon(t);
      stage_tensor_usage_rows = 0;
      if (v && strchr(v, '[')) {
        if (parse_int_list_from_value(v, &vals, &n) == PRT_OK) {
          stage_set_tensor_usage_count(cur_stage, vals, n);
          stage_tensor_usage_rows = 1U;
        }
        free(vals);
      }
      in_stage_tensor_usage_list = 1;
      continue;
    }

    if (starts_key(t, "tensorUseLazyFetch")) {
      uint32_t *vals = NULL;
      uint32_t n = 0;
      char *v = value_after_colon(t);
      stage_tensor_lazy_fetch_rows = 0;
      if (v && strchr(v, '[')) {
        if (parse_int_list_from_value(v, &vals, &n) == PRT_OK) {
          stage_set_tensor_lazy_fetch(cur_stage, vals, n);
          stage_tensor_lazy_fetch_rows = 1U;
        }
        free(vals);
      }
      in_stage_tensor_lazy_fetch_list = 1;
      continue;
    }

    if (starts_key(t, "localSpmTensorAddrList")) {
      uint32_t *vals = NULL;
      uint32_t n = 0;
      if (parse_int_list_from_value(value_after_colon(t), &vals, &n) == PRT_OK) {
        stage_set_local_spm_tensor_addr(cur_stage, vals, n);
      }
      free(vals);
      continue;
    }

    if (starts_key(t, "localSpmFirstVPageList")) {
      uint32_t *vals = NULL;
      uint32_t n = 0;
      if (parse_int_list_from_value(value_after_colon(t), &vals, &n) == PRT_OK) {
        stage_set_local_spm_first_vpage(cur_stage, vals, n);
      }
      free(vals);
      continue;
    }

    if (starts_key(t, "localSpmPageCountList")) {
      uint32_t *vals = NULL;
      uint32_t n = 0;
      if (parse_int_list_from_value(value_after_colon(t), &vals, &n) == PRT_OK) {
        stage_set_local_spm_page_count(cur_stage, vals, n);
      }
      free(vals);
      continue;
    }

    if (starts_key(t, "localSpmTensorBytesList")) {
      uint32_t *vals = NULL;
      uint32_t n = 0;
      if (parse_int_list_from_value(value_after_colon(t), &vals, &n) == PRT_OK) {
        stage_set_local_spm_tensor_bytes(cur_stage, vals, n);
      }
      free(vals);
      continue;
    }

    if (starts_key(t, "localSpmPageSpan")) {
      (void)parse_u32_scalar(t, &cur_stage->local_spm_page_span);
      continue;
    }
  }

  if (out->num_segments > 0) out->subbatch_size = out->segments[0].subbatch_size;
  else out->subbatch_size = 1;
  PRT_PROGRESS_LOG("yaml pipeline parse end path=%s lines=%u segments=%u",
                   path ? path : "(null)", line_no, out->num_segments);

  PRT_PROGRESS_LOG("yaml pipeline validate begin path=%s segments=%u",
                   path ? path : "(null)", out->num_segments);
  for (uint32_t s = 0; s < out->num_segments; ++s) {
    prt_segment_desc_t *seg = &out->segments[s];
    uint32_t seg_span_fallback = 0;
    PRT_PROGRESS_LOG("yaml pipeline validate segment-begin seg=%u stages=%u bindings=%u stage_spm_util=%u",
                     s, seg->num_stages, seg->buffer_binding_count, seg->num_stage_spm_util);
    total_stages += seg->num_stages;
    for (uint32_t i = 0; i < seg->num_stages; ++i) {
      uint32_t local_span = 0;
      if (!seg->stages[i].acc_util_present || seg->stages[i].layer_count != 1 ||
          seg->stages[i].dram_bypass_count == 0 || seg->stages[i].spm_bypass_count == 0 ||
          !seg->stages[i].virtual_acc_ids_present ||
          seg->stages[i].num_virtual_acc_ids == 0 ||
          seg->stages[i].num_virtual_acc_ids != seg->stages[i].acc_util ||
          (seg->stages[i].physical_acc_ids_present &&
           seg->stages[i].num_physical_acc_ids != seg->stages[i].acc_util)) {
        PRT_PROGRESS_LOG(
          "yaml pipeline validate stage-shape-error seg=%u local_stage=%u global_stage=%u "
          "acc_present=%u acc=%u layers=%u dram=%u spm=%u virt_present=%u virt=%u phys_present=%u phys=%u",
          s, i, seg->stages[i].stage_id,
          seg->stages[i].acc_util_present, seg->stages[i].acc_util,
          seg->stages[i].layer_count, seg->stages[i].dram_bypass_count, seg->stages[i].spm_bypass_count,
          seg->stages[i].virtual_acc_ids_present, seg->stages[i].num_virtual_acc_ids,
          seg->stages[i].physical_acc_ids_present, seg->stages[i].num_physical_acc_ids);
        free(buf);
        return PRT_ERR_PARSE;
      }
      {
        uint8_t seen_virtual[PRT_MAX_CORES] = {0};
        for (uint32_t k = 0; k < seg->stages[i].num_virtual_acc_ids; ++k) {
          const uint32_t v_acc = seg->stages[i].virtual_acc_ids[k];
          if (v_acc >= seg->stages[i].acc_util) {
            PRT_PROGRESS_LOG(
              "yaml pipeline validate virtual-acc-error seg=%u local_stage=%u global_stage=%u idx=%u v_acc=%u acc=%u",
              s, i, seg->stages[i].stage_id, k, v_acc, seg->stages[i].acc_util);
            free(buf);
            return PRT_ERR_PARSE;
          }
          if (seen_virtual[v_acc]) {
            PRT_PROGRESS_LOG(
              "yaml pipeline validate virtual-acc-duplicate seg=%u local_stage=%u global_stage=%u idx=%u v_acc=%u",
              s, i, seg->stages[i].stage_id, k, v_acc);
            free(buf);
            return PRT_ERR_PARSE;
          }
          seen_virtual[v_acc] = 1U;
        }
        for (uint32_t k = 0; k < seg->stages[i].acc_util; ++k) {
          if (seen_virtual[k]) continue;
          PRT_PROGRESS_LOG(
            "yaml pipeline validate virtual-acc-missing seg=%u local_stage=%u global_stage=%u v_acc=%u acc=%u",
            s, i, seg->stages[i].stage_id, k, seg->stages[i].acc_util);
          free(buf);
          return PRT_ERR_PARSE;
        }
      }
      if (!seg->stages[i].tensor_usage_count_present ||
          seg->stages[i].tensor_usage_count_count != seg->stages[i].tensor_id_count ||
          !seg->stages[i].tensor_lazy_fetch_present ||
          seg->stages[i].tensor_lazy_fetch_count != seg->stages[i].tensor_id_count) {
        PRT_PROGRESS_LOG(
          "yaml pipeline validate tensor-meta-error seg=%u local_stage=%u global_stage=%u "
          "tensor_ids=%u usage_present=%u usage=%u lazy_present=%u lazy=%u",
          s, i, seg->stages[i].stage_id, seg->stages[i].tensor_id_count,
          seg->stages[i].tensor_usage_count_present, seg->stages[i].tensor_usage_count_count,
          seg->stages[i].tensor_lazy_fetch_present, seg->stages[i].tensor_lazy_fetch_count);
        free(buf);
        return PRT_ERR_PARSE;
      }
      if (seg->stages[i].local_spm_tensor_count > 0) {
        if (seg->stages[i].local_spm_tensor_count != seg->stages[i].tensor_id_count) {
          PRT_PROGRESS_LOG(
            "yaml pipeline validate local-spm-count-error seg=%u local_stage=%u global_stage=%u "
            "local_spm_tensors=%u tensor_ids=%u",
            s, i, seg->stages[i].stage_id,
            seg->stages[i].local_spm_tensor_count, seg->stages[i].tensor_id_count);
          free(buf);
          return PRT_ERR_PARSE;
        }
        if (seg->stages[i].local_spm_page_span == 0U) {
          for (uint32_t t_idx = 0; t_idx < seg->stages[i].local_spm_tensor_count; ++t_idx) {
            uint32_t end_vpage =
              seg->stages[i].local_spm_first_vpage[t_idx] + seg->stages[i].local_spm_page_count[t_idx];
            if (end_vpage > local_span) local_span = end_vpage;
          }
          seg->stages[i].local_spm_page_span = local_span;
        } else {
          local_span = seg->stages[i].local_spm_page_span;
        }
        if (seg->stages[i].exec_base_vpage + local_span > seg_span_fallback) {
          seg_span_fallback = seg->stages[i].exec_base_vpage + local_span;
        }
      }
    }
    if (seg->segment_spm_page_span == 0U) seg->segment_spm_page_span = seg_span_fallback;
    for (uint32_t i = 0; i < seg->buffer_binding_count; ++i) {
      if (seg->buffer_bindings[i].kind == PRT_BUFFER_BINDING_UNKNOWN) {
        PRT_PROGRESS_LOG(
          "yaml pipeline validate buffer-binding-kind-error seg=%u binding=%u buffer_id=%u tensor_id=%u",
          s, i, seg->buffer_bindings[i].buffer_id, seg->buffer_bindings[i].tensor_id);
        free(buf);
        return PRT_ERR_PARSE;
      }
    }
    PRT_PROGRESS_LOG("yaml pipeline validate segment-end seg=%u stages=%u span=%u bindings=%u",
                     s, seg->num_stages, seg->segment_spm_page_span, seg->buffer_binding_count);
  }

  free(buf);
  PRT_PROGRESS_LOG("yaml pipeline validate end path=%s segments=%u total_stages=%u",
                   path ? path : "(null)", out->num_segments, total_stages);
  PRT_PROGRESS_LOG("yaml pipeline load end path=%s segments=%u total_stages=%u subbatch_size=%u",
                   path ? path : "(null)", out->num_segments, total_stages, out->subbatch_size);
  return PRT_OK;
}

void prt_free_model_desc(prt_model_desc_t *model) {
  if (!model) return;
  free(model->layers);
  if (model->stages) {
    for (uint32_t i = 0; i < model->num_stages; ++i) {
      free(model->stages[i].entry_tensor_ids);
      free(model->stages[i].export_tensor_ids);
    }
    free(model->stages);
  }
  free(model->tensors);
  memset(model, 0, sizeof(*model));
}

void prt_free_pipeline_desc(prt_pipeline_desc_t *pipeline) {
  if (!pipeline) return;
  if (pipeline->segments) {
    for (uint32_t s = 0; s < pipeline->num_segments; ++s) {
      prt_segment_desc_t *seg = &pipeline->segments[s];
      if (seg->stages) {
        for (uint32_t i = 0; i < seg->num_stages; ++i) {
          free(seg->stages[i].entry);
          free(seg->stages[i].exports);
        }
        free(seg->stages);
      }
      if (seg->tensor_spm_util_in_stage) {
        for (uint32_t i = 0; i < seg->num_stage_spm_util; ++i) {
          free(seg->tensor_spm_util_in_stage[i].data);
        }
        free(seg->tensor_spm_util_in_stage);
      }
      free(seg->shared_tensor_is_read_first.data);
      free(seg->tensor_spm_util_shared.data);
      free(seg->tensor_spm_util_in_ringbuffer.data);
      free(seg->tensor_spm_util_weight.data);
      free(seg->transport_effective_bytes.data);
      free(seg->ring_slot_effective_bytes.data);
      free(seg->ring_cfgs);
      free(seg->buffer_bindings);
    }
    free(pipeline->segments);
  }
  memset(pipeline, 0, sizeof(*pipeline));
}
