#ifndef __MAIN_H_
#define __MAIN_H_

typedef struct threadargs_t {
	int tid;
	int socket;
	unsigned char** lines;
	long* linelengths;
	long numlines;
} threadargs_t;
#endif
