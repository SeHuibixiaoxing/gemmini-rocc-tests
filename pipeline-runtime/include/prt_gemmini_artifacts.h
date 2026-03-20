#ifndef PRT_GEMMINI_ARTIFACTS_H
#define PRT_GEMMINI_ARTIFACTS_H

#include "prt_types.h"

#ifdef __cplusplus
extern "C" {
#endif

int prt_validate_gemmini_artifacts(const char *model_yaml, const char *layer_mapping_yaml,
                                   prt_pipeline_desc_t *pipeline);

#ifdef __cplusplus
}
#endif

#endif
