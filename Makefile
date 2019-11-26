CC = gcc
CFLAGS = \
	-Wall -Wextra -Werror \
	-Wshadow \
	-Wstrict-prototypes \
	-Wno-unused-function \
	-Wno-unused-parameter \
	-Wno-unused-variable \
	-Og -g \
	-std=c11

SRCS = \
	cpu.c \
	file.c \
	lcd.c \
	mmu.c \
	timer.c \
	gameboy.c
OBJS = $(SRCS:.c=.o)
EGBE_SRCS = $(SRCS) egbe.c
EGBE_OBJS = $(OBJS) egbe.o

LIBS = -lSDL2

.PHONY: all clean

all: egbe

clean:
	rm -f egbe *.o **/*.o

egbe: $(EGBE_OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LIBS)

%.o: %.c
	$(CC) $(CFLAGS) -o $@ -c $<
