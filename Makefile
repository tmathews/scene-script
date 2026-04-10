CC      ?= gcc
CFLAGS  ?= -Wall -Wextra -std=c11 -g
AR      ?= ar

LIB     = libnpc_script.a
OBJS    = npc_script.o
TEST    = npc_script_test
DEMO    = demo

.PHONY: all clean test

all: $(LIB)

$(LIB): $(OBJS)
	$(AR) rcs $@ $^

npc_script.o: npc_script.c npc_script.h
	$(CC) $(CFLAGS) -c -o $@ $<

$(TEST): npc_script_test.c npc_script.c npc_script.h
	$(CC) $(CFLAGS) -o $@ npc_script_test.c npc_script.c -lm

$(DEMO): demo.c npc_script.c npc_script.h
	$(CC) $(CFLAGS) -o $@ demo.c npc_script.c -lm

test: $(TEST)
	./$(TEST)

clean:
	rm -f $(OBJS) $(LIB) $(TEST) $(DEMO)
