#include <stdint.h>
#include <time.h>

#include "progress.h"

struct timespec progress_limit_rate(progress_cb cb, size_t current, size_t total, unsigned interval_ms, struct timespec* last) {
	struct timespec now;
	clock_gettime(CLOCK_MONOTONIC, &now);
	if(last && current != total) {
		unsigned long delta;
		delta = (now.tv_sec - last->tv_sec) * 1000UL + (now.tv_nsec - last->tv_nsec) / 1000000UL;
		if(delta < interval_ms) {
			return *last;
		}
	}
	cb(current, total);
	return now;
}
