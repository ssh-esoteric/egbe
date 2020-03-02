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

LIBS = -lSDL2

ifdef CURL
	EGBE_SRCS += egbe_curl.c
	CFLAGS += $(shell pkg-config --cflags libcurl)
	LIBS += $(shell pkg-config --libs libcurl)
else
	EGBE_SRCS += egbe_curl_stub.c
endif

ifdef DEBUG
	EGBE_SRCS += debugger.c
	CFLAGS += $(shell pkg-config --cflags ruby)
	LIBS += $(shell pkg-config --libs ruby)
else
	EGBE_SRCS += debugger_stub.c
endif

.PHONY: all clean

all: egbe

clean:
	rm -f egbe *.o **/*.o

egbe: $(EGBE_OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LIBS)

# Ruby has non-strict prototypes in its headers
debugger.o: debugger.c
	$(CC) $(CFLAGS) -Wno-strict-prototypes -o $@ -c $<

%.o: %.c
	$(CC) $(CFLAGS) -o $@ -c $<
