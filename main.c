#include <stdio.h>
#include <pthread.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <stdbool.h>
#include <unistd.h>
#include <errno.h>
#include <arpa/inet.h>
#include <string.h>
#include <signal.h>
#include <stdint.h>
#include <stdlib.h>

#include "main.h"

#define NUM_THREADS 10
#define MAX_CONNECTIONS_PER_ADDRESS 100
#define SHARED_CONNECTIONS 0

#define IGNORE_BROKEN_PIPE 1

#define LINE_CNT_DEFAULT 1024
#define LINE_CNT_BLOCK 256
#define LINE_LENGTH_DEFAULT 16
#define LINE_LENGTH_BLOCK 8

#define NUM_MY_ADDRS 0

#if NUM_MY_ADDRS > 0
	#if NUM_THREADS > MAX_CONNECTIONS_PER_ADDRESS * NUM_MY_ADDRS
		#error Too many connections for the given number of source addresses
	#endif
#endif

const char* host = "94.45.231.39";
const char* myaddrs[NUM_MY_ADDRS] = {};
const unsigned short port = 1234;


char* filename = "data.txt";

int sockets[NUM_THREADS];
pthread_t threads[NUM_THREADS];
struct sockaddr_in inmyaddrs[NUM_MY_ADDRS];

struct threadargs_t threadargs[NUM_THREADS];

bool doexit = false;

void* send_thread(void* data)
{
	long i = 0;
	int err;
	struct threadargs_t* args = data;
	printf("Starting thread %d, lines @%p, lengths @%p\n", args->tid, args->lines, args->linelengths);
reconnect:
	if(doexit)
		return NULL;
	sockets[args->socket] = socket(AF_INET, SOCK_STREAM, 0);
	if((err = sockets[args->socket]) < 0)
	{
		printf("Failed to create socket %d: %d\n", args->socket, err);
		goto fail;
	}
	if(NUM_MY_ADDRS)
	{
		int addrindex = args->tid / MAX_CONNECTIONS_PER_ADDRESS;
		if(bind(sockets[args->socket], (struct sockaddr *)&inmyaddrs[addrindex], sizeof(inmyaddrs[addrindex])))
		{
			err = -errno;
			fprintf(stderr, "Failed to bind socket %d: %s\n", args->socket, strerror(errno));
			goto fail;
		}
	}
	printf("Connecting socket %d\n", args->socket);
	if((err = connect(sockets[args->socket], args->remoteaddr, sizeof(struct sockaddr_in))))
	{
		err = -errno;
		fprintf(stderr, "Failed to connect socket: %s\n", strerror(errno));
		goto newsocket;
	}
	printf("Connected socket %d\n", args->socket);
	while(!doexit)
	{
		for(i = 0; i < args->numlines; i++)
		{
			if((err = write(sockets[args->socket], args->lines[i], args->linelengths[i])) < 0)
			{
				if(errno == EPIPE && IGNORE_BROKEN_PIPE)
					continue;
				fprintf(stderr, "Write failed after %ld lines: %d => %s\n", i, errno, strerror(errno));
				if(errno == ECONNRESET)
				{
					goto newsocket;
				}
				doexit = true;
				break;
			}
		}
	}

	return NULL;
fail:
	doexit = true;
	return NULL;
newsocket:
	close(sockets[args->socket]);
	shutdown(sockets[args->socket], SHUT_RDWR);
	int one = 1;
	setsockopt(sockets[args->socket], SOL_SOCKET, SO_REUSEADDR, &one, sizeof(int));
	goto reconnect;
}

void doshutdown(int signal)
{
	doexit = true;
}

int main(int argc, char** argv)
{
	unsigned char* buffer, *line, *linetmp;
	unsigned char** lines, **linestmp;
	int thread_cnt, i, err = 0;
	long* linelengths, *linelengthstmp;
	struct sockaddr_in inaddr;
	FILE* file;
	long fsize, linenum = 0, linenum_alloc, linepos = 0, linepos_alloc, fpos = 0, lines_per_thread;
	if(argc < 2)
	{
		fprintf(stderr, "Please specify a file to send\n");
		return -EINVAL;
	}
	filename = argv[1];
	if(SHARED_CONNECTIONS || NUM_THREADS != NUM_THREADS)
		return -EINVAL;
	if(signal(SIGINT, doshutdown))
	{
		fprintf(stderr, "Failed to bind signal\n");
		return -EINVAL;
	}
	if(signal(SIGPIPE, SIG_IGN))
	{
		fprintf(stderr, "Failed to bind signal\n");
		return -EINVAL;
	}
	if(!(file = fopen(filename, "r")))
	{
		fprintf(stderr, "Failed to open file: %d\n", errno);
		return -errno;
	}
	fseek(file, 0, SEEK_END);
	fsize = ftell(file);
	fseek(file, 0, SEEK_SET);
	buffer = malloc(fsize + 1);
	if(!buffer)
	{
		goto file_cleanup;
	}
	fread(buffer, fsize, 1, file);
	printf("Read %ld bytes of data to memory, counting instructions\n", fsize);
	lines = malloc(LINE_CNT_DEFAULT * sizeof(unsigned char*));
	linenum_alloc = LINE_CNT_DEFAULT;
	line = malloc(LINE_LENGTH_DEFAULT);
	linepos_alloc = LINE_LENGTH_DEFAULT;
	linelengths = malloc(LINE_CNT_DEFAULT * sizeof(long));
	while(fpos < fsize)
	{
		line[linepos] = buffer[fpos];
		if(buffer[fpos] == '\n')
		{
			linelengths[linenum] = linepos + 1;
			lines[linenum] = line;
			linenum++;
			if(linenum == linenum_alloc)
			{
				linenum_alloc += LINE_CNT_BLOCK;
				linestmp = realloc(lines, linenum_alloc * sizeof(unsigned char*));
				if(!linestmp)
				{
					fprintf(stderr, "Allocation of %d lines failed, offset %ld\n", LINE_CNT_BLOCK, linenum_alloc - LINE_CNT_BLOCK);
					err = -ENOMEM;
					goto lines_cleanup;
				}
				lines = linestmp;
				linelengthstmp = realloc(linelengths, linenum_alloc * sizeof(long));
				if(!linelengthstmp)
				{
					fprintf(stderr, "Allocation of %d line lengths failed\n", LINE_CNT_BLOCK);
					err = -ENOMEM;
					goto lines_cleanup;
				}
				linelengths = linelengthstmp;
			}
			line = malloc(LINE_LENGTH_DEFAULT);
			linepos_alloc = LINE_LENGTH_DEFAULT;
			if(!line)
			{
				fprintf(stderr, "Failed to allocate line\n");
				err = -ENOMEM;
				goto lines_cleanup;
			}
			lines[linenum] = line;
			linepos = 0;
		}
		else
			linepos++;
		if(linepos == linepos_alloc)
		{
			linepos_alloc += LINE_LENGTH_BLOCK;
			linetmp = realloc(line, linepos_alloc);
			if(!linetmp)
			{
				fprintf(stderr, "Failed to allocate line with length %ld\n", linepos_alloc);
				err = -ENOMEM;
				goto lines_cleanup;
			}
			line = linetmp;
			lines[linenum] = line;
		}
		fpos++;
	}
	line[linepos] = buffer[fpos];
	linelengths[linenum] = linepos + 1;
	printf("Got %ld instructions\n", linenum);

	lines_per_thread = linenum / NUM_THREADS;
	printf("Using %ld lines per thread\n", lines_per_thread);

	inet_pton(AF_INET, host, &(inaddr.sin_addr.s_addr));
	inaddr.sin_port = htons(port);
	inaddr.sin_family = AF_INET;
	if(NUM_MY_ADDRS)
	{
		for(i = 0; i < NUM_MY_ADDRS; i++)
		{
			inmyaddrs[i].sin_family = AF_INET;
			inmyaddrs[i].sin_port = 0;
			inet_pton(AF_INET, myaddrs[i], &(inmyaddrs[i].sin_addr.s_addr));
		}
	}
	memset(sockets, 0, NUM_THREADS * sizeof(int));
	for(thread_cnt = 0; thread_cnt < NUM_THREADS; thread_cnt++)
	{
		threadargs[thread_cnt].socket = thread_cnt;
		threadargs[thread_cnt].tid = thread_cnt;
		threadargs[thread_cnt].numlines = lines_per_thread;
		threadargs[thread_cnt].lines = lines + lines_per_thread * thread_cnt;
		threadargs[thread_cnt].linelengths = linelengths + lines_per_thread * thread_cnt;
		threadargs[thread_cnt].remoteaddr = (struct sockaddr*)&inaddr;
		pthread_create(&threads[thread_cnt], NULL, send_thread, &threadargs[thread_cnt]);
	}

	printf("Waiting for threads to finish\n");
	for(i = 0; i < NUM_THREADS; i++)
	{
		printf("Joining thread %d\n", i);
		pthread_join(threads[i], NULL);
		printf("Thread %d finished\n", i);
	}
	err = 0;

//socket_cleanup:
	for(i = 0; i < NUM_THREADS; i++)
	{
		if(!sockets[i])
			continue;
		close(sockets[i]);
		shutdown(sockets[i], SHUT_RDWR);
		int one = 1;
		setsockopt(sockets[i], SOL_SOCKET, SO_REUSEADDR, &one, sizeof(int));
	}
lines_cleanup:
	while(linenum >= 0)
	{
		free(lines[linenum]);
		linenum--;
	}
	free(lines);
	free(linelengths);
//memory_cleanup:
	free(buffer);
file_cleanup:
	fclose(file);
	return err;
}
