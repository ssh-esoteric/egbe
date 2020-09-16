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

PLUGIN_CFLAGS = \
	$(CFLAGS) \
	-I$(CURDIR) \
	-fPIC \
	-shared

SRCS = \
	apu.c \
	cpu.c \
	file.c \
	lcd.c \
	mmu.c \
	serial.c \
	timer.c \
	gameboy.c
OBJS = $(SRCS:.c=.o)
EGBE_SRCS = $(SRCS) egbe.c
EGBE_OBJS = $(EGBE_SRCS:.c=.o)

LIBS = -ldl -lSDL2
LINK = $(LIBS) -rdynamic

export CC CFLAGS PLUGIN_CFLAGS

.PHONY: all clean curl lws plugins ruby

all: egbe
plugins: curl lws ruby

clean:
	rm -f egbe *.o **/*.o

egbe: $(EGBE_OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LINK)

curl lws ruby:
	$(MAKE) -C plugins/$@/

%.o: %.c
	$(CC) $(CFLAGS) -o $@ -c $<
