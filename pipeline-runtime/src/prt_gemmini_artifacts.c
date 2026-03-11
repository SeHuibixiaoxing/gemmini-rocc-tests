#include "prt_gemmini_artifacts.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static int dir_exists(const char *path) {
  struct stat st;
  if (!path) return 0;
  return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

static int load_file(const char *path, char **out_buf) {
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

static int arrays_equal(const uint32_t *a, uint32_t a_n, const uint32_t *b, uint32_t b_n) {
  if (a_n != b_n) return 0;
  for (uint32_t i = 0; i < a_n; ++i) {
    if (a[i] != b[i]) return 0;
  }
  return 1;
}

static int build_mapping_dir(const char *model_yaml, char *out_dir, size_t out_dir_size) {
  const char *slash;
  size_t dir_len;
  int n;
  if (!model_yaml || !out_dir || out_dir_size == 0) return PRT_ERR_INVAL;
  slash = strrchr(model_yaml, '/');
  if (!slash) return PRT_ERR_PARSE;
  dir_len = (size_t)(slash - model_yaml);
  if (dir_len + strlen("/mapping_gemmini") + 1U > out_dir_size) return PRT_ERR_INVAL;
  memcpy(out_dir, model_yaml, dir_len);
  out_dir[dir_len] = '\0';
  n = snprintf(out_dir + dir_len, out_dir_size - dir_len, "%s", "/mapping_gemmini");
  if (n < 0 || (size_t)n >= out_dir_size - dir_len) return PRT_ERR_INVAL;
  if (!dir_exists(out_dir)) {
    fprintf(stderr, "mapping_gemmini dir not found for model yaml %s: %s\n", model_yaml, out_dir);
    return PRT_ERR_NOT_READY;
  }
  return PRT_OK;
}

static int validate_stage_against_file(const char *path, const prt_stage_map_t *stage) {
  char *buf = NULL;
  char *p;
  uint32_t cur_acc = 0;
  uint32_t cur_dram[PRT_MAX_LAYER_TENSORS];
  uint32_t cur_spm[PRT_MAX_LAYER_TENSORS];
  uint32_t cur_dram_n = 0;
  uint32_t cur_spm_n = 0;
  int have_candidate = 0;
  int matches = 0;
  int rc = load_file(path, &buf);
  if (rc != PRT_OK) {
    fprintf(stderr, "failed to load mapping_gemmini file %s rc=%d\n", path, rc);
    return rc;
  }

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

    if (strncmp(t, "- target_accel:", 15) == 0) {
      if (have_candidate) {
        if (cur_acc == stage->acc_util &&
            arrays_equal(cur_dram, cur_dram_n, stage->dram_bypass, stage->dram_bypass_count) &&
            arrays_equal(cur_spm, cur_spm_n, stage->spm_bypass, stage->spm_bypass_count)) {
          matches += 1;
        }
      }
      have_candidate = 1;
      cur_acc = 0;
      cur_dram_n = 0;
      cur_spm_n = 0;
      if (parse_u32_scalar(t, &cur_acc) != PRT_OK) {
        free(buf);
        return PRT_ERR_PARSE;
      }
      continue;
    }

    if (!have_candidate) continue;

    if (starts_key(t, "mapping_dram_bypass")) {
      if (parse_u32_list_from_value(value_after_colon(t), cur_dram, &cur_dram_n, PRT_MAX_LAYER_TENSORS) != PRT_OK) {
        free(buf);
        return PRT_ERR_PARSE;
      }
      continue;
    }

    if (starts_key(t, "mapping_spm_bypass")) {
      if (parse_u32_list_from_value(value_after_colon(t), cur_spm, &cur_spm_n, PRT_MAX_LAYER_TENSORS) != PRT_OK) {
        free(buf);
        return PRT_ERR_PARSE;
      }
      continue;
    }
  }

  if (have_candidate) {
    if (cur_acc == stage->acc_util &&
        arrays_equal(cur_dram, cur_dram_n, stage->dram_bypass, stage->dram_bypass_count) &&
        arrays_equal(cur_spm, cur_spm_n, stage->spm_bypass, stage->spm_bypass_count)) {
      matches += 1;
    }
  }

  free(buf);
  if (matches != 1) {
    fprintf(stderr,
            "mapping_gemmini match failure: file=%s layer=%u stage=%u accUtil=%u matches=%d dramBypassCount=%u spmBypassCount=%u\n",
            path, stage->layer_id, stage->stage_id, stage->acc_util, matches,
            stage->dram_bypass_count, stage->spm_bypass_count);
    return PRT_ERR_NOT_READY;
  }
  return PRT_OK;
}

int prt_validate_gemmini_artifacts(const char *model_yaml, const prt_pipeline_desc_t *pipeline) {
  char mapping_dir[1024];
  char path[1152];
  int rc;
  if (!model_yaml || !pipeline) return PRT_ERR_INVAL;
  rc = build_mapping_dir(model_yaml, mapping_dir, sizeof(mapping_dir));
  if (rc != PRT_OK) return rc;

  for (uint32_t seg_idx = 0; seg_idx < pipeline->num_segments; ++seg_idx) {
    const prt_segment_desc_t *seg = &pipeline->segments[seg_idx];
    for (uint32_t stage_idx = 0; stage_idx < seg->num_stages; ++stage_idx) {
      const prt_stage_map_t *stage = &seg->stages[stage_idx];
      int n;
      if (stage->layer_count != 1U) {
        fprintf(stderr, "stage contract violation: seg=%u stage=%u layer_count=%u\n",
                seg_idx, stage_idx, stage->layer_count);
        return PRT_ERR_NOT_IMPL;
      }
      if (stage->dram_bypass_count == 0U || stage->spm_bypass_count == 0U) {
        fprintf(stderr,
                "stage missing bypass metadata: seg=%u stage=%u layer=%u dram=%u spm=%u\n",
                seg_idx, stage_idx, stage->layer_id,
                stage->dram_bypass_count, stage->spm_bypass_count);
        return PRT_ERR_PARSE;
      }
      n = snprintf(path, sizeof(path), "%s/%u.yaml", mapping_dir, stage->layer_id);
      if (n < 0 || (size_t)n >= sizeof(path)) return PRT_ERR_INVAL;
      rc = validate_stage_against_file(path, stage);
      if (rc != PRT_OK) return rc;
    }
  }
  return PRT_OK;
}
