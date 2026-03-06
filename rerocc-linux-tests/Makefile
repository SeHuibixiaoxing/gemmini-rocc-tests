include $(abs_top_srcdir)/Makefrag

tests = \
	rerocc_gemmini_conv_matrix \
	rerocc_dma_matrix \

ifdef BAREMETAL_ONLY
tests_linux =
else
tests_linux = $(tests:=-linux)
endif

BENCH_COMMON = $(abs_top_srcdir)/riscv-tests-benchmarks-common
GEMMINI_HEADERS = $(abs_top_srcdir)/include/gemmini.h $(abs_top_srcdir)/include/gemmini_params.h $(abs_top_srcdir)/include/gemmini_testutils.h
REROCC_HEADERS = $(abs_top_srcdir)/rerocc-linux-tests/rerocc_control.h

CFLAGS := $(CFLAGS) \
	-mcmodel=medany \
	-std=gnu99 \
	-O2 \
	-pthread \
	-ffast-math \
	-fno-common \
	-fno-builtin-printf \
	-fno-tree-loop-distribute-patterns \
	-march=rv64gc -Wa,-march=rv64gc \
	-lm \
	-lgcc \
	-I$(abs_top_srcdir)/riscv-tests \
	-I$(abs_top_srcdir)/riscv-tests/env \
	-I$(abs_top_srcdir) \
	-I$(BENCH_COMMON) \
	-DID_STRING=$(ID_STRING) \

all: $(tests_linux)

vpath %.c $(src_dir)

%-linux: %.c $(GEMMINI_HEADERS) $(REROCC_HEADERS)
	$(CC_LINUX) $(CFLAGS) $< $(LFLAGS) -o $@ $(LIBS)

junk += $(tests_linux)
