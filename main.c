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
#include <unistd.h>
#include <getopt.h>
#include <sys/sendfile.h>

#include "main.h"

#define NUM_THREADS_DEFAULT 10
#define MAX_CONNECTIONS_PER_ADDRESS 100
#define SHARED_CONNECTIONS 0

#define IGNORE_BROKEN_PIPE_DEFAULT 1
#define PORT_DEFAULT 1234

#define LINE_CNT_DEFAULT 1024
#define LINE_CNT_BLOCK 256
#define LINE_LENGTH_DEFAULT 16
#define LINE_LENGTH_BLOCK 8

#define NUM_MY_ADDRS 0

/*#if NUM_MY_ADDRS > 0
	#if NUM_THREADS > MAX_CONNECTIONS_PER_ADDRESS * NUM_MY_ADDRS
		#error Too many connections for the given number of source addresses
	#endif
#endif
*/

const char* myaddrs[NUM_MY_ADDRS] = {};

int ignore_broken_pipe = IGNORE_BROKEN_PIPE_DEFAULT;

char* filename = "data.txt";

struct sockaddr_in inmyaddrs[NUM_MY_ADDRS];

bool doexit = false;

void* send_thread(void* data)
{
	long i = 0;
	int err;
	off_t offset;
	struct threadargs_t* args = data;
	printf("Starting thread %d, length %zd, offset @%zd\n", args->tid, args->length, args->offset);
reconnect:
	if(doexit)
		return NULL;
	args->sockets[args->socket] = socket(AF_INET, SOCK_STREAM, 0);
	if((err = args->sockets[args->socket]) < 0)
	{
		printf("Failed to create socket %d: %d\n", args->socket, err);
		goto fail;
	}
	if(NUM_MY_ADDRS)
	{
		int addrindex = args->tid / MAX_CONNECTIONS_PER_ADDRESS;
		if(bind(args->sockets[args->socket], (struct sockaddr *)&inmyaddrs[addrindex], sizeof(inmyaddrs[addrindex])))
		{
			err = -errno;
			fprintf(stderr, "Failed to bind socket %d: %s\n", args->socket, strerror(errno));
			goto fail;
		}
	}
	printf("Connecting socket %d\n", args->socket);
	if((err = connect(args->sockets[args->socket], args->remoteaddr, sizeof(struct sockaddr_in))))
	{
		err = -errno;
		fprintf(stderr, "Failed to connect socket: %s\n", strerror(errno));
		goto newsocket;
	}
	printf("Connected socket %d\n", args->socket);
	while(!doexit)
	{
		offset = args->offset;
		while((offset - args->offset) < args->length) {
			printf("Sendfileing %zd bytes, offset %zd\n", args->length - (offset - args->offset), offset);
			i++;
			if((err = sendfile(args->sockets[args->socket], fileno(args->file), &offset, args->length - (offset - args->offset))) < 0)
			{
				if(errno == EPIPE && ignore_broken_pipe)
					continue;

				fprintf(stderr, "Write failed after %ld sendfiles: %d => %s\n", i, errno, strerror(errno));
				if(errno == ECONNRESET)
					goto newsocket;

				goto fail;
			}
		}
	}


	return NULL;
fail:
	doexit = true;
	return NULL;
newsocket:
	close(args->sockets[args->socket]);
	shutdown(args->sockets[args->socket], SHUT_RDWR);
	int one = 1;
	setsockopt(args->sockets[args->socket], SOL_SOCKET, SO_REUSEADDR, &one, sizeof(int));
	goto reconnect;
}

void doshutdown(int signal)
{
	doexit = true;
}

void print_usage(char* binary) {
	fprintf(stderr, "USAGE: %s <host> [file to send] [-p <port>] [-a <source ip address>] [-i <0|1>] [-t <number of threads>] [-h]\n", binary);
}

int main(int argc, char** argv)
{
	unsigned char* buffer, *line, *linetmp;
	unsigned char** lines, **linestmp;
	int num_threads = NUM_THREADS_DEFAULT, thread_cnt = 0, i, err = 0;
	off_t* lineofftmp, *line_offsets;
	struct sockaddr_in inaddr;
	FILE* file;
	long fsize, linenum = 0, lineoff_alloc, linepos = 0, linepos_alloc, fpos = 0, lines_per_thread;
	char opt, *host;
	unsigned short port = PORT_DEFAULT;
	int* sockets;
	pthread_t* threads;
	struct threadargs_t* threadargs;
	off_t current_offset = 0;


	while((opt = getopt(argc, argv, "p:it:h")) != -1) {
		switch(opt) {
			case('p'):
				port = (unsigned short)strtoul(optarg, NULL, 10);
				break;
			case('i'):
				ignore_broken_pipe = atoi(optarg);
				break;
			case('t'):
				num_threads = atoi(optarg);
				if(num_threads < 1) {
					fprintf(stderr, "Number of threads must be > 0\n");
					print_usage(argv[0]);
					exit(1);
				}
				break;
			default:
				print_usage(argv[0]);
				exit(1);
		}
	}

	if(optind >= argc) {
		fprintf(stderr, "Please specify a host to send to\n");
		print_usage(argv[0]);
		return -EINVAL;
	}

	host = argv[optind++];

	if(optind < argc) {
		filename = argv[optind];
	}

	printf("Sending '%s' to %s:%u\n", filename, host, port);

	if(SHARED_CONNECTIONS)
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

	sockets = malloc(num_threads * sizeof(int));
	if(!sockets) {
		fprintf(stderr, "Failed to allocate memory for socket handles\n");
		return -ENOMEM;
	}

	if(!(file = fopen(filename, "r")))
	{
		fprintf(stderr, "Failed to open file: %d\n", errno);
		err = -errno;
		goto socket_cleanup;
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

	line_offsets = malloc(LINE_LENGTH_DEFAULT * sizeof(off_t));
	lineoff_alloc = LINE_LENGTH_DEFAULT;
	while(fpos < fsize)
	{
		if(linepos == 0)
			line_offsets[linenum] = fpos;
			
		if(buffer[fpos] == '\n')
		{
			linenum++;
			if(linenum == lineoff_alloc)
			{
				lineoff_alloc += LINE_CNT_BLOCK;
				lineofftmp = realloc(line_offsets, lineoff_alloc * sizeof(off_t));
				if(!lineofftmp)
				{
					fprintf(stderr, "Allocation of %d line offsets failed\n", LINE_CNT_BLOCK);
					err = -ENOMEM;
					goto lines_cleanup;
				}
				line_offsets = lineofftmp;
			}
			linepos = 0;
		}
		else
			linepos++;
		fpos++;
	}
	line_offsets[linenum] = fpos;
	printf("Got %ld instructions\n", linenum);

	lines_per_thread = linenum / num_threads;
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
	memset(sockets, 0, num_threads * sizeof(int));

	threads = malloc(num_threads * sizeof(pthread_t));
	printf("Thread handles: %p\n", threads);
	if(!threads) {
		fprintf(stderr, "Failed to allocate memory for thread handles\n");
		goto lines_cleanup;
	}

	threadargs = malloc(num_threads * sizeof(struct threadargs_t));
	if(!threadargs) {
		fprintf(stderr, "Failed to allocate memory for threadargs\n");
		goto thread_cleanup;
	}

	for(thread_cnt = 0; thread_cnt < num_threads; thread_cnt++)
	{
		threadargs[thread_cnt].sockets = sockets;
		threadargs[thread_cnt].socket = thread_cnt;
		threadargs[thread_cnt].tid = thread_cnt;
		threadargs[thread_cnt].offset = line_offsets[lines_per_thread * thread_cnt];
		threadargs[thread_cnt].length = line_offsets[lines_per_thread * (thread_cnt + 1)] - line_offsets[lines_per_thread * thread_cnt];
		threadargs[thread_cnt].remoteaddr = (struct sockaddr*)&inaddr;
		threadargs[thread_cnt].file = fopen(filename, "r");
		if(!threadargs[thread_cnt].file) {
			printf("Failed to open file for thread %d\n", thread_cnt);
			goto thread_file_cleanup;
		}
		pthread_create(&threads[thread_cnt], NULL, send_thread, &threadargs[thread_cnt]);
	}

	printf("Waiting for threads to finish\n");
	for(i = 0; i < num_threads; i++)
	{
		printf("Joining thread %d\n", i);
		pthread_join(threads[i], NULL);
		printf("Thread %d finished\n", i);
	}
	err = 0;

thread_file_cleanup:
	while(thread_cnt-- > 0) {
		fclose(threadargs[thread_cnt].file);
	}
threadargs_cleanup:
	free(threadargs);
thread_cleanup:
	free(threads);
lines_cleanup:
	free(line_offsets);
//memory_cleanup:
	free(buffer);
file_cleanup:
	fclose(file);
socket_cleanup:
	for(i = 0; i < num_threads; i++)
	{
		if(!sockets[i])
			continue;
		close(sockets[i]);
		shutdown(sockets[i], SHUT_RDWR);
		int one = 1;
		setsockopt(sockets[i], SOL_SOCKET, SO_REUSEADDR, &one, sizeof(int));
	}
	free(sockets);
	return err;
}
