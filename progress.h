#ifndef _PROGRESS_H_
#define _PROGRESS_H_

#define PROGESS_INTERVAL_DEFAULT 100

typedef void (*progress_cb)(size_t current, size_t total);

struct timespec progress_limit_rate(progress_cb cb, size_t current, size_t total, unsigned interval_ms, struct timespec* last);

#endif
