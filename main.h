#ifndef __MAIN_H_
#define __MAIN_H_

typedef struct threadargs_t {
	int tid;
	int socket;
	int* sockets;
	FILE* file;
	off_t offset;
	size_t length;
	struct sockaddr* remoteaddr;
} threadargs_t;
#endif
