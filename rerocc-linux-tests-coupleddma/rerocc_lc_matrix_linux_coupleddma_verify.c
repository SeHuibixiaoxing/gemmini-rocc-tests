#define _GNU_SOURCE

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

typedef enum {
  MATRIX_FULL = 0,
  MATRIX_DIAGONAL = 1,
  MATRIX_SINGLE = 2,
} matrix_mode_t;

static void print_usage(const char *prog) {
  printf("Usage: %s [--target 2c2g2d|4c4g4d|custom] [--matrix full|diagonal|single]\\n", prog);
  printf("          [--num-cores N] [--num-gemmini G] [--num-dma D] [--bytes B]\\n");
  printf("          [--gemmini-base-id B] [--dma-base-id B]\\n");
}

static bool parse_int_arg(const char *name, const char *arg, int *dst) {
  char *end = NULL;
  long v = strtol(arg, &end, 10);
  if (end == NULL || *end != '\0') {
    printf("invalid integer for %s: %s\\n", name, arg);
    return false;
  }
  if (v < 0 || v > 100000000) {
    printf("out of range integer for %s: %s\\n", name, arg);
    return false;
  }
  *dst = (int)v;
  return true;
}

static bool parse_matrix_mode(const char *arg, matrix_mode_t *mode) {
  if (strcmp(arg, "full") == 0) {
    *mode = MATRIX_FULL;
    return true;
  }
  if (strcmp(arg, "diagonal") == 0) {
    *mode = MATRIX_DIAGONAL;
    return true;
  }
  if (strcmp(arg, "single") == 0) {
    *mode = MATRIX_SINGLE;
    return true;
  }
  return false;
}

static const char *matrix_mode_name(matrix_mode_t mode) {
  switch (mode) {
    case MATRIX_FULL:
      return "full";
    case MATRIX_DIAGONAL:
      return "diagonal";
    case MATRIX_SINGLE:
      return "single";
    default:
      return "unknown";
  }
}

static int run_cmd(const char *name, const char *cmd) {
  printf("[verify] running %s: %s\\n", name, cmd);
  fflush(stdout);
  int rc = system(cmd);
  if (rc == -1) {
    printf("[verify] %s failed to launch\\n", name);
    return 127;
  }
  if (WIFEXITED(rc)) {
    int code = WEXITSTATUS(rc);
    if (code == 0) {
      printf("[verify] %s PASS\\n", name);
    } else {
      printf("[verify] %s FAIL exit=%d\\n", name, code);
    }
    return code;
  }
  printf("[verify] %s terminated abnormally\\n", name);
  return 128;
}

int main(int argc, char **argv) {
  const char *target = "custom";
  matrix_mode_t matrix_mode = MATRIX_DIAGONAL;

  int num_cores = -1;
  int num_gemmini = -1;
  int num_dma = -1;
  int bytes = 65536;
  int gemmini_base_id = 0;
  int dma_base_id = -1;

  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "--target") == 0 && i + 1 < argc) {
      target = argv[++i];
    } else if (strcmp(argv[i], "--matrix") == 0 && i + 1 < argc) {
      if (!parse_matrix_mode(argv[++i], &matrix_mode)) {
        print_usage(argv[0]);
        return 1;
      }
    } else if (strcmp(argv[i], "--num-cores") == 0 && i + 1 < argc) {
      if (!parse_int_arg("--num-cores", argv[++i], &num_cores)) return 1;
    } else if (strcmp(argv[i], "--num-gemmini") == 0 && i + 1 < argc) {
      if (!parse_int_arg("--num-gemmini", argv[++i], &num_gemmini)) return 1;
    } else if (strcmp(argv[i], "--num-dma") == 0 && i + 1 < argc) {
      if (!parse_int_arg("--num-dma", argv[++i], &num_dma)) return 1;
    } else if (strcmp(argv[i], "--bytes") == 0 && i + 1 < argc) {
      if (!parse_int_arg("--bytes", argv[++i], &bytes)) return 1;
    } else if (strcmp(argv[i], "--gemmini-base-id") == 0 && i + 1 < argc) {
      if (!parse_int_arg("--gemmini-base-id", argv[++i], &gemmini_base_id)) return 1;
    } else if (strcmp(argv[i], "--dma-base-id") == 0 && i + 1 < argc) {
      if (!parse_int_arg("--dma-base-id", argv[++i], &dma_base_id)) return 1;
    } else if (strcmp(argv[i], "--help") == 0) {
      print_usage(argv[0]);
      return 0;
    } else {
      print_usage(argv[0]);
      return 1;
    }
  }

  if (strcmp(target, "2c2g2d") == 0 || strcmp(target, "small") == 0) {
    if (num_cores < 0) num_cores = 2;
    if (num_gemmini < 0) num_gemmini = 2;
    if (num_dma < 0) num_dma = 2;
  } else if (strcmp(target, "4c4g4d") == 0 || strcmp(target, "default") == 0) {
    if (num_cores < 0) num_cores = 4;
    if (num_gemmini < 0) num_gemmini = 4;
    if (num_dma < 0) num_dma = 4;
  } else if (strcmp(target, "custom") == 0) {
    if (num_cores < 0) num_cores = 2;
    if (num_gemmini < 0) num_gemmini = 2;
    if (num_dma < 0) num_dma = 2;
  } else {
    printf("unknown target profile: %s\\n", target);
    print_usage(argv[0]);
    return 1;
  }

  if (dma_base_id < 0) dma_base_id = num_gemmini;

  if (num_cores <= 0 || num_gemmini <= 0 || num_dma <= 0 || bytes <= 0) {
    printf("num-cores/num-gemmini/num-dma/bytes must be > 0\\n");
    return 1;
  }

  printf("[rerocc-lc-coupleddma-linux] target=%s matrix=%s num_cores=%d num_gemmini=%d num_dma=%d bytes=%d gemmini_base_id=%d dma_base_id=%d\\n",
         target,
         matrix_mode_name(matrix_mode),
         num_cores,
         num_gemmini,
         num_dma,
         bytes,
         gemmini_base_id,
         dma_base_id);

  char gemmini_cmd[1024];
  char dma_cmd[1024];

  snprintf(gemmini_cmd,
           sizeof(gemmini_cmd),
           "/root/rerocc-linux-tests-coupleddma/rerocc_gemmini_conv_matrix-linux --num-cores %d --num-gemmini %d --gemmini-base-id %d --matrix %s",
           num_cores,
           num_gemmini,
           gemmini_base_id,
           matrix_mode_name(matrix_mode));

  snprintf(dma_cmd,
           sizeof(dma_cmd),
           "/root/rerocc-linux-tests-coupleddma/rerocc_dma_matrix_coupleddma-linux --num-cores %d --num-gemmini %d --num-dma %d --dma-base-id %d --bytes %d --matrix %s",
           num_cores,
           num_gemmini,
           num_dma,
           dma_base_id,
           bytes,
           matrix_mode_name(matrix_mode));

  int gemmini_ret = run_cmd("gemmini", gemmini_cmd);
  int dma_ret = run_cmd("dma", dma_cmd);

  if (gemmini_ret == 0 && dma_ret == 0) {
    printf("ALL_TESTS_PASS\\n");
    fflush(stdout);
    sync();
    system("poweroff -f");
    return 0;
  }

  printf("ALL_TESTS_FAIL gemmini_ret=%d dma_ret=%d\\n", gemmini_ret, dma_ret);
  fflush(stdout);
  sync();
  system("poweroff -f");
  return 1;
}
