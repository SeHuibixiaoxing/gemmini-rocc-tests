#include "prt_yaml_loader.h"

#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int load_file(const char *path, char **out_buf, size_t *out_len) {
  FILE *f;
  long sz;
  char *buf;

  if (!path || !out_buf || !out_len) return PRT_ERR_INVAL;
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

  buf = (char *)malloc((size_t)sz + 1);
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
  *out_len = (size_t)sz;
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

static int parse_u32_scalar(char *line, uint32_t *out) {
  char *v = value_after_colon(line);
  unsigned long x;
  if (!v || !out) return PRT_ERR_PARSE;
  x = strtoul(v, NULL, 10);
  *out = (uint32_t)x;
  return PRT_OK;
}

static int parse_accutil_anywhere(char *line, uint32_t *out) {
  char *p;
  unsigned long x;
  if (!line || !out) return PRT_ERR_INVAL;
  p = strstr(line, "accUtil:");
  if (!p) return PRT_ERR_PARSE;
  p += strlen("accUtil:");
  p = ltrim(p);
  if (!*p) return PRT_ERR_PARSE;
  x = strtoul(p, NULL, 10);
  *out = (uint32_t)x;
  return PRT_OK;
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

    unsigned long val = strtoul(p, &p, 10);
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

    unsigned long long val = strtoull(p, &p, 10);
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

    k = strtoul(p, &p, 10);
    while (*p && *p != ':') p++;
    if (*p != ':') break;
    p++;
    while (*p && isspace((unsigned char)*p)) p++;
    if (!isdigit((unsigned char)*p)) break;
    val = strtoul(p, &p, 10);

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

static void stage_set_layer_id(prt_stage_map_t *st, const uint32_t *ids, uint32_t n) {
  if (!st) return;
  st->layer_id = (n > 0) ? ids[0] : 0;
}

int prt_load_model_yaml(const char *path, prt_model_desc_t *out) {
  char *buf = NULL;
  size_t len = 0;
  int rc;
  prt_model_layer_t *cur_layer = NULL;
  int cur_layer_has_index = 0;
  if (!out) return PRT_ERR_INVAL;
  memset(out, 0, sizeof(*out));

  rc = load_file(path, &buf, &len);
  if (rc != PRT_OK) return rc;
  (void)len;

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
    rtrim(line);
    indent = count_indent(line);
    char *t = ltrim(line);
    int is_list_item = (t[0] == '-');
    if (*t == '\0' || *t == '#') continue;

    if (starts_key(t, "address")) {
      uint64_t *vals = NULL;
      uint32_t n = 0;
      if (parse_u64_list_from_value(value_after_colon(t), &vals, &n) == PRT_OK && n > 0) {
        if (is_list_item) {
          if (!cur_layer || cur_layer_has_index) {
            if (push_model_layer(out, &cur_layer) != PRT_OK) {
              free(vals);
              free(buf);
              return PRT_ERR_NOMEM;
            }
            cur_layer_has_index = 0;
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
      if (parse_u32_scalar(t, &idx) != PRT_OK) continue;
      if (!cur_layer || cur_layer_has_index) {
        if (push_model_layer(out, &cur_layer) != PRT_OK) {
          free(buf);
          return PRT_ERR_NOMEM;
        }
      }
      if (!cur_layer) {
        free(buf);
        return PRT_ERR_NOMEM;
      }
      cur_layer->index = idx;
      cur_layer_has_index = 1;
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
      if (parse_int_list_from_value(value_after_colon(t), &vals, &n) == PRT_OK) {
        cur_layer->param_len = n > 16 ? 16 : n;
        for (uint32_t i = 0; i < cur_layer->param_len; ++i) cur_layer->param[i] = vals[i];
      }
      free(vals);
      continue;
    }

    if (starts_key(t, "tensorIds")) {
      uint32_t *vals = NULL;
      uint32_t n = 0;
      if (parse_int_list_from_value(value_after_colon(t), &vals, &n) == PRT_OK) {
        cur_layer->tensor_count = n > 8 ? 8 : n;
        for (uint32_t i = 0; i < cur_layer->tensor_count; ++i) cur_layer->tensor_ids[i] = vals[i];
      }
      free(vals);
      continue;
    }

    if (starts_key(t, "tensorSize")) {
      uint32_t *vals = NULL;
      uint32_t n = 0;
      if (parse_int_list_from_value(value_after_colon(t), &vals, &n) == PRT_OK) {
        cur_layer->tensor_size_count = n > 8 ? 8 : n;
        for (uint32_t i = 0; i < cur_layer->tensor_size_count; ++i) cur_layer->tensor_size[i] = vals[i];
      }
      free(vals);
      continue;
    }

    if (starts_key(t, "address2")) {
      uint64_t *vals = NULL;
      uint32_t n = 0;
      if (parse_u64_list_from_value(value_after_colon(t), &vals, &n) == PRT_OK) {
        cur_layer->address2_count = n > 8 ? 8 : n;
        for (uint32_t i = 0; i < cur_layer->address2_count; ++i) cur_layer->address2[i] = vals[i];
      }
      free(vals);
      continue;
    }
  }

  out->num_stages = 0;
  out->stages = NULL;
  out->num_tensors = 0;
  out->tensors = NULL;
  free(buf);
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
  int cur_stage_has_global_id = 0;
  int cur_seg_has_segment_idx = 0;

  if (!out) return PRT_ERR_INVAL;
  memset(out, 0, sizeof(*out));

  rc = load_file(path, &buf, &len);
  if (rc != PRT_OK) return rc;
  (void)len;

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

    rtrim(line);
    indent = count_indent(line);
    char *t = ltrim(line);
    if (*t == '\0' || *t == '#') continue;

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
      }
      cur_seg->segment_idx = seg_idx;
      cur_seg_has_segment_idx = 1;
      continue;
    }

    if (!cur_seg) continue;

    if (starts_key(t, "subBatchSize")) {
      uint32_t sb = 1;
      if (parse_u32_scalar(t, &sb) == PRT_OK && sb > 0) cur_seg->subbatch_size = sb;
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

    if (starts_key(t, "shared_tensor_is_read_first")) {
      map_kv_t *kv = NULL;
      uint32_t n = 0;
      if (parse_u32_map_from_value(t, &kv, &n) == PRT_OK) {
        if (u32_map_set_from_kv(&cur_seg->shared_tensor_is_read_first, kv, n) != PRT_OK) {
          free(kv);
          free(buf);
          return PRT_ERR_NOMEM;
        }
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

    if (starts_key(t, "tensor_spm_util_in_stage")) {
      in_stage_spm_util_list = 1;
      stage_spm_util_indent = indent;
      stage_spm_util_idx = 0;
      continue;
    }

    if (t[0] == '-' && strstr(t, "accUtil:")) {
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

    if (starts_key(t, "exportTensorTypeList")) {
      char **types = NULL;
      uint32_t n = 0;
      if (parse_str_list_from_value(value_after_colon(t), &types, &n) == PRT_OK) stage_set_export_types(cur_stage, types, n);
      for (uint32_t i = 0; i < n; ++i) free(types[i]);
      free(types);
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
  }

  if (out->num_segments > 0) out->subbatch_size = out->segments[0].subbatch_size;
  else out->subbatch_size = 1;

  for (uint32_t s = 0; s < out->num_segments; ++s) {
    prt_segment_desc_t *seg = &out->segments[s];
    for (uint32_t i = 0; i < seg->num_stages; ++i) {
      if (!seg->stages[i].acc_util_present) {
        free(buf);
        return PRT_ERR_PARSE;
      }
    }
  }

  free(buf);
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
      free(seg->ring_cfgs);
    }
    free(pipeline->segments);
  }
  memset(pipeline, 0, sizeof(*pipeline));
}
