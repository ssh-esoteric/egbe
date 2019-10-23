CC = gcc
CFLAGS = \
	-Wall -Wextra -Werror -Wstrict-prototypes \
	-Wno-unused-function \
	-Wno-unused-parameter \
	-Wno-unused-variable \
	-Og -g \
	-std=c11

SRCS = \
	gameboy.c
OBJS = $(SRCS:.c=.o)
EGBE_SRCS = $(SRCS) egbe.c
EGBE_OBJS = $(OBJS) egbe.o

.PHONY: all clean

all: egbe

clean:
	rm -f egbe *.o **/*.o

egbe: $(EGBE_OBJS)
	$(CC) $(CFLAGS) -o $@ $^

%.o: %.c
	$(CC) $(CFLAGS) -o $@ -c $<
