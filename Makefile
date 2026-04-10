CC      ?= gcc
CFLAGS  ?= -Wall -Wextra -std=c11 -g
AR      ?= ar

HDRS    = scene_script.h internal.h
SRCS    = lexer.c parser.c eval.c
LIB     = libscene_script.a
OBJS    = lexer.o parser.o eval.o
TEST    = tests
DEMO    = demo

.PHONY: all clean test

all: $(LIB)

$(LIB): $(OBJS)
	$(AR) rcs $@ $^

%.o: %.c $(HDRS)
	$(CC) $(CFLAGS) -c -o $@ $<

$(TEST): tests.c $(SRCS) $(HDRS)
	$(CC) $(CFLAGS) -o $@ tests.c $(SRCS) -lm

$(DEMO): demo.c $(SRCS) $(HDRS)
	$(CC) $(CFLAGS) -o $@ demo.c $(SRCS) -lm

test: $(TEST)
	./$(TEST)

clean:
	rm -f $(OBJS) $(LIB) $(TEST) $(DEMO)
