#include "prt_error.h"

const char *prt_err_str(int code) {
  switch (code) {
    case PRT_OK: return "ok";
    case PRT_ERR_UNKNOWN: return "unknown";
    case PRT_ERR_INVAL: return "invalid";
    case PRT_ERR_NOMEM: return "no_memory";
    case PRT_ERR_TIMEOUT: return "timeout";
    case PRT_ERR_BUSY: return "busy";
    case PRT_ERR_EMPTY: return "empty";
    case PRT_ERR_NOT_READY: return "not_ready";
    case PRT_ERR_NOT_IMPL: return "not_implemented";
    case PRT_ERR_IO: return "io_error";
    case PRT_ERR_PARSE: return "parse_error";
    case PRT_ERR_STATE: return "bad_state";
    case PRT_ERR_DEADLOCK: return "deadlock";
    case PRT_ERR_MISMATCH: return "mismatch";
    default: return "invalid_error_code";
  }
}
