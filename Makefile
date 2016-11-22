CC=clang-3.8
LDFLAGS=`icu-config --ldflags`
CFLAGS=-g -O3 -Werror -Wall -Wextra -Werror=format-security `icu-config --cflags`

all:	pb_thingy

OBJ := $(patsubst %.c,%.o,$(wildcard *.c))
HDR := $(wildcard *.h)

%.o:	%.c $(HDR) Makefile
	@echo CC $<
	$(CC) $(CFLAGS) -c $< -o $@

pb_thingy:	$(OBJ)
	@echo LINK $@
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^
