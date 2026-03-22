include $(abs_top_srcdir)/Makefrag

tests = 	rerocc_gemmini_conv_matrix 	rerocc_dma_matrix 
LINUX_EXTRA_BINS = rerocc_pipeline_runtime-linux

ifdef BAREMETAL_ONLY
tests_linux =
else
tests_linux = $(tests:=-linux) $(LINUX_EXTRA_BINS)
endif

BENCH_COMMON = $(abs_top_srcdir)/riscv-tests-benchmarks-common
GEMMINI_HEADERS = $(abs_top_srcdir)/include/gemmini.h $(abs_top_srcdir)/include/gemmini_params.h $(abs_top_srcdir)/include/gemmini_testutils.h
REROCC_HEADERS = $(abs_top_srcdir)/rerocc-linux-tests/rerocc_control.h
PIPELINE_RUNTIME_DIR = $(abs_top_srcdir)/pipeline-runtime
PIPELINE_RUNTIME_INC = -I$(PIPELINE_RUNTIME_DIR)/include
PIPELINE_RUNTIME_PROGRESS ?= 0
PIPELINE_RUNTIME_PROGRESS_RAW ?= 0
PIPELINE_RUNTIME_GEMMINI_PHASE ?= 0
PIPELINE_RUNTIME_CPPFLAGS = $(PIPELINE_RUNTIME_INC) -DPRT_ENABLE_PROGRESS_LOG=$(PIPELINE_RUNTIME_PROGRESS) -DPRT_ENABLE_PROGRESS_RAW_LOG=$(PIPELINE_RUNTIME_PROGRESS_RAW) -DPIPELINE_RUNTIME_GEMMINI_PHASE_LOG=$(PIPELINE_RUNTIME_GEMMINI_PHASE)
PIPELINE_RUNTIME_SRC := $(wildcard $(PIPELINE_RUNTIME_DIR)/src/*.c)
PIPELINE_RUNTIME_OBJDIR := .pipeline_runtime_objs
PIPELINE_RUNTIME_OBJS := $(patsubst $(PIPELINE_RUNTIME_DIR)/src/%.c,$(PIPELINE_RUNTIME_OBJDIR)/%.o,$(PIPELINE_RUNTIME_SRC))

CFLAGS := $(CFLAGS) 	-mcmodel=medany 	-std=gnu99 	-O2 	-pthread 	-ffast-math 	-fno-common 	-fno-builtin-printf 	-fno-tree-loop-distribute-patterns 	-march=rv64gc -Wa,-march=rv64gc 	-lm 	-lgcc 	-I$(abs_top_srcdir)/riscv-tests 	-I$(abs_top_srcdir)/riscv-tests/env 	-I$(abs_top_srcdir) 	-I$(BENCH_COMMON) 	-DID_STRING=$(ID_STRING) 

ifeq ($(strip $(CC_LINUX)),)
$(error Missing RISC-V Linux compiler. Install riscv64-unknown-linux-gnu-gcc or riscv64-linux-gnu-gcc, or set CC_LINUX=/path/to/compiler)
endif

all: $(tests_linux)

vpath %.c $(src_dir)

%-linux: %.c $(GEMMINI_HEADERS) $(REROCC_HEADERS)
	$(CC_LINUX) $(CFLAGS) $< $(LFLAGS) -o $@ $(LIBS)

$(PIPELINE_RUNTIME_OBJDIR):
	mkdir -p $@

$(PIPELINE_RUNTIME_OBJDIR)/%.o: $(PIPELINE_RUNTIME_DIR)/src/%.c | $(PIPELINE_RUNTIME_OBJDIR)
	$(CC_LINUX) $(CFLAGS) $(PIPELINE_RUNTIME_CPPFLAGS) -DPRT_NO_MAIN -c $< -o $@

rerocc_pipeline_runtime-linux: rerocc_pipeline_runtime_linux.c $(PIPELINE_RUNTIME_OBJS)
	$(CC_LINUX) $(CFLAGS) $(PIPELINE_RUNTIME_CPPFLAGS) $< $(PIPELINE_RUNTIME_OBJS) $(LFLAGS) -o $@ $(LIBS)

junk += $(tests_linux) $(PIPELINE_RUNTIME_OBJS)
