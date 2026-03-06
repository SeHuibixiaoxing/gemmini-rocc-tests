CC ?= gcc
CFLAGS ?= -O2 -Wall -Wextra -Werror -std=c11 -D_POSIX_C_SOURCE=200809L
CPPFLAGS ?= -Iinclude -I..
LDFLAGS ?=
LDLIBS ?= -lpthread

SRC := $(wildcard src/*.c)
OBJ := $(SRC:.c=.o)
BIN := pipeline_runtime

.PHONY: all clean

all: $(BIN)

$(BIN): $(OBJ)
	$(CC) $(CFLAGS) $(OBJ) $(LDFLAGS) $(LDLIBS) -o $@

src/%.o: src/%.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJ) $(BIN)
