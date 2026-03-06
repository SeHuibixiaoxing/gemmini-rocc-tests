#ifndef PRT_YAML_LOADER_H
#define PRT_YAML_LOADER_H

#include "prt_types.h"

#ifdef __cplusplus
extern "C" {
#endif

int prt_load_model_yaml(const char *path, prt_model_desc_t *out);
int prt_load_pipeline_yaml(const char *path, prt_pipeline_desc_t *out);
void prt_free_model_desc(prt_model_desc_t *model);
void prt_free_pipeline_desc(prt_pipeline_desc_t *pipeline);

#ifdef __cplusplus
}
#endif

#endif
