CC=clang-3.8
LDFLAGS=`icu-config --ldflags`
CFLAGS=-g -O3 -flto -Werror -Wall -Wextra -Werror=format-security `icu-config --cflags`

all:	pb_thingy

OBJ := $(patsubst %.c,%.o,$(wildcard *.c))
HDR := $(wildcard *.h)
INC := $(wildcard *.inc)

%.o:	%.c $(HDR) $(INC) Makefile
	@echo CC $<
	$(CC) $(CFLAGS) -c $< -o $@

pb_thingy:	$(OBJ)
	@echo LINK $@
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^
