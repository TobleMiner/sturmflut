#ifndef __MAIN_H_
#define __MAIN_H_

enum {
	TX_METHOD_SENDFILE,
	TX_METHOD_SEND
};

struct threadargs_t {
	int tid;
	int socket;
	int* sockets;
	struct sockaddr* remoteaddr;
	int method;

	FILE* file;
	char* buffer;

	off_t offset;
	size_t length;
};

struct pf_cmd {
	char* cmd;
	size_t length;

	char* data;
	off_t offset;
};

#endif
