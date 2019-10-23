#ifndef EGBE_COMMON_H
#define EGBE_COMMON_H

#include "gameboy.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

#define GBLOG(msg, ...) \
	fprintf(stderr, "%s (%s +%d): " msg "\n", \
	        __func__, __FILE__, __LINE__, ##__VA_ARGS__)

#endif
