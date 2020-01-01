#ifndef EGBE_COMMON_H
#define EGBE_COMMON_H

#include "gameboy.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

#define BIT(n) (1UL << (n))
#define BITS(from, thru) ((~0UL - BIT(from)) - (((~0UL - BIT(thru)) << 1) | 1))

#define GBLOG(msg, ...) \
	fprintf(stderr, "%s (%s +%d): " msg "\n", \
	        __func__, __FILE__, __LINE__, ##__VA_ARGS__)

void gb_callback(struct gameboy *gb, struct gameboy_callback *cb);

#endif
