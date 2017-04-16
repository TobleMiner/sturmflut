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

#define NUM_THREADS 16
#define NUM_CONNECTIONS 16
#define SHARED_CONNECTIONS 0

const char* filename = "data.txt";


#define LINE_CNT_DEFAULT 1024
#define LINE_CNT_BLOCK 256
#define LINE_LENGTH_DEFAULT 16
#define LINE_LENGTH_BLOCK 8

const char* host = "94.45.231.43";
unsigned short port = 1234;

int sockets[NUM_CONNECTIONS];

pthread_t threads[NUM_THREADS];

struct threadargs_t threadargs[NUM_THREADS];

bool doexit = false;

void* send_thread(void* data)
{
	long i = 0;
	int err;
	struct threadargs_t* args = data;
	printf("Starting thread %d, lines @%p, lengths @%p\n", args->tid, args->lines, args->linelengths);
	while(!doexit)
	{
		for(i = 0; i < args->numlines; i++)
		{
			if((err = write(args->socket, args->lines[i], args->linelengths[i])) < 0)
			{
				fprintf(stderr, "Write failed: %d => %s\n", err, strerror(errno));
				doexit = true;
				break;
			}
		}
	}

	return NULL;
}

void doshutdown(int signal)
{
	doexit = true;
}

int main()
{
	unsigned char* buffer, *line, *linetmp;
	unsigned char** lines, **linestmp;
	int socket_cnt, thread_cnt, i, err = 0;
	long* linelengths, *linelengthstmp;
	struct sockaddr_in inaddr;
	struct sockaddr addr;
	FILE* file;
	long fsize, linenum = 0, linenum_alloc, linepos = 0, linepos_alloc, fpos = 0, lines_per_thread;
	if(SHARED_CONNECTIONS || NUM_THREADS != NUM_CONNECTIONS)
		return -EINVAL;
	if(signal(SIGINT, doshutdown))
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
					fprintf(stderr, "Allocation of %d lines failed, offset %d\n", LINE_CNT_BLOCK, linenum_alloc - LINE_CNT_BLOCK);
					err = -ENOMEM;
					goto lines_cleanup;
				}
				lines = linestmp;
				printf("Linelengths are @%p\n", linelengths);
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
			linepos_alloc += LINE_LENGTH_DEFAULT;
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
	inaddr.sin_port = (port & 0xFF) << 8 | ((port >> 8) & 0xFF);
	inaddr.sin_family = AF_INET;
	for(socket_cnt = 0; socket_cnt < NUM_CONNECTIONS; socket_cnt++)
	{
		sockets[socket_cnt] = socket(AF_INET, SOCK_STREAM, 0);
		if((err = sockets[socket_cnt]) < 0)
		{
			err = -errno;
			fprintf(stderr, "Failed to create socket: %s\n", strerror(errno));
			goto socket_cleanup;
		}
		printf("Connecting socket %d\n", socket_cnt);
		if((err = connect(sockets[socket_cnt], (struct sockaddr *)&inaddr, sizeof(inaddr))))
		{
			err = -errno;
			fprintf(stderr, "Failed to connect socket: %s\n", strerror(errno));
			goto socket_cleanup;
		}
		printf("Connected socket %d\n", socket_cnt);
	}
	for(thread_cnt = 0; thread_cnt < NUM_THREADS; thread_cnt++)
	{
		threadargs[thread_cnt].socket = sockets[thread_cnt % socket_cnt];
		threadargs[thread_cnt].tid = thread_cnt;
		threadargs[thread_cnt].numlines = lines_per_thread;
		threadargs[thread_cnt].lines = lines + lines_per_thread * thread_cnt;
		threadargs[thread_cnt].linelengths = linelengths + lines_per_thread * thread_cnt;
		pthread_create(&threads[thread_cnt], NULL, send_thread, &threadargs[thread_cnt]);
	}

	for(i = 0; i < NUM_THREADS; i++)
	{
		pthread_join(threads[i], NULL);
		printf("Thread %d finished\n", i);
	}
	err = 0;

socket_cleanup:
	while(socket_cnt-- >= 0)
	{
		close(sockets[socket_cnt]);
		shutdown(sockets[socket_cnt], SHUT_RDWR);
		int one = 1;
		setsockopt(sockets[socket_cnt], SOL_SOCKET, SO_REUSEADDR, &one, sizeof(int));
	}
lines_cleanup:
	while(linenum >= 0)
	{
		free(lines[linenum]);
		linenum--;
	}
	free(lines);
	free(linelengths);
memory_cleanup:
	free(buffer);
file_cleanup:
	fclose(file);
	return err;
}
