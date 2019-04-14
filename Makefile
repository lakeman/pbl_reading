CC=clang
LDFLAGS=`pkg-config --libs --cflags icu-uc icu-io`
CFLAGS=-g -O3 -flto -Werror -Wall -Wextra -Werror=format-security

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
