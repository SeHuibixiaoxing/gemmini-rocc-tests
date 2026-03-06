#ifndef PRT_ERROR_H
#define PRT_ERROR_H

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
  PRT_OK = 0,
  PRT_ERR_UNKNOWN = -1,
  PRT_ERR_INVAL = -2,
  PRT_ERR_NOMEM = -3,
  PRT_ERR_TIMEOUT = -4,
  PRT_ERR_BUSY = -5,
  PRT_ERR_EMPTY = -6,
  PRT_ERR_NOT_READY = -7,
  PRT_ERR_NOT_IMPL = -8,
  PRT_ERR_IO = -9,
  PRT_ERR_PARSE = -10,
  PRT_ERR_STATE = -11,
  PRT_ERR_DEADLOCK = -12,
  PRT_ERR_MISMATCH = -13
} prt_err_t;

const char *prt_err_str(int code);

#ifdef __cplusplus
}
#endif

#endif
