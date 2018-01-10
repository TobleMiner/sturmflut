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
#define METHOD_DEFAULT TX_METHOD_SENDFILE
#define FILENAME_DEFAULT "data.txt"

#define CMDS_DEFAULT 1024
#define CMDS_BLOCK 1024

#define NUM_MY_ADDRS 0

/*#if NUM_MY_ADDRS > 0
	#if NUM_THREADS > MAX_CONNECTIONS_PER_ADDRESS * NUM_MY_ADDRS
		#error Too many connections for the given number of source addresses
	#endif
#endif
*/

const char* myaddrs[NUM_MY_ADDRS] = {};

int ignore_broken_pipe = IGNORE_BROKEN_PIPE_DEFAULT;

char* filename = FILENAME_DEFAULT;

struct sockaddr_in inmyaddrs[NUM_MY_ADDRS];

pthread_t* threads;
int thread_cnt = 0;

void doshutdown(int signal) {
	int i = thread_cnt;
	while(i-- > 0) {
		pthread_cancel(threads[i]);
	}
}



void* send_thread(void* data) {
	long i = 0;
	int err;
	off_t offset, send_offset;
	struct threadargs_t* args = data;
	ssize_t send_size;
	printf("Starting thread %d, length %zd, offset @%zd\n", args->tid, args->length, args->offset);
reconnect:
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
	if(args->method == TX_METHOD_SENDFILE) {
		while(true) {
			offset = args->offset;
			while((offset - args->offset) < args->length) {
				printf("Sendfileing %zd bytes, offset %zd\n", args->length - (offset - args->offset), offset);
				i++;
				if((err = sendfile(args->sockets[args->socket], fileno(args->file), &offset, args->length - (offset - args->offset))) < 0) {
					if(errno == EPIPE && ignore_broken_pipe) {
						continue;
					}

					fprintf(stderr, "Write failed after %ld sendfiles: %d => %s\n", i, errno, strerror(errno));
					if(errno == ECONNRESET)
						goto newsocket;

					goto fail;
				}
			}
		}
	} else {
		while(true) {
			send_offset = 0;
			while(send_offset < args->length) {
				if((send_size = write(args->sockets[args->socket], args->buffer + args->offset + send_offset, args->length - send_offset)) < 0) {
					if(errno == EPIPE && ignore_broken_pipe) {
						continue;
					}

					fprintf(stderr, "Write failed after %ld lines: %d => %s\n", i, errno, strerror(errno));
					if(errno == ECONNRESET) {
						goto newsocket;
					}
					goto fail;
				}
				send_offset += send_size;
			}
		}
	}


	return NULL;
fail:
	doshutdown(SIGKILL);
	return NULL;
newsocket:
	close(args->sockets[args->socket]);
	shutdown(args->sockets[args->socket], SHUT_RDWR);
	int one = 1;
	setsockopt(args->sockets[args->socket], SOL_SOCKET, SO_REUSEADDR, &one, sizeof(int));
	goto reconnect;
}

void print_usage(char* binary) {
	fprintf(stderr, "USAGE: %s <host> [file to send] [-p <port>] [-a <source ip address>] "
			"[-i <0|1>] [-t <number of threads>] [-m <%d|%d>] [-h]\n", binary, TX_METHOD_SENDFILE, TX_METHOD_SEND);
}

int main(int argc, char** argv)
{
	int num_threads = NUM_THREADS_DEFAULT, i, err = 0, method = METHOD_DEFAULT;
	struct sockaddr_in inaddr;
	FILE* file;
	long fsize, linepos = 0, fpos = 0, cmds_per_thread, commands_alloc, cmd_num = 0;
	char opt, *host, *buffer;
	unsigned short port = PORT_DEFAULT;
	int* sockets;
	struct threadargs_t* threadargs;
	struct pf_cmd* commands, *commandstmp, *cmd_current;


	while((opt = getopt(argc, argv, "p:it:hm:")) != -1) {
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
			case('m'):
				method = atoi(optarg);
				if(method != TX_METHOD_SENDFILE && method != TX_METHOD_SEND) {
					fprintf(stderr, "TX method must be either %d (sendfile) or %d (send)\n", TX_METHOD_SENDFILE, TX_METHOD_SEND);
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

	commands = malloc(CMDS_DEFAULT * sizeof(struct pf_cmd));
	commands_alloc = CMDS_DEFAULT;
	if(!commands) {
		err = -ENOMEM;
		fprintf(stderr, "Failed to allocate initial %d commands\n", CMDS_DEFAULT);
		goto file_cleanup;
	}

	while(fpos < fsize)
	{
		if(linepos == 0) {
			cmd_current = &commands[cmd_num];
			cmd_current->data = buffer;
			cmd_current->cmd = &buffer[fpos];
			cmd_current->offset = fpos;
		}
		linepos++;
		if(buffer[fpos] == '\n') {
			cmd_current->length = linepos;
			cmd_num++;
			if(cmd_num >= commands_alloc) {
				commands_alloc += CMDS_BLOCK;
				commandstmp = realloc(commands, commands_alloc * sizeof(struct pf_cmd));
				if(!commandstmp) {
					fprintf(stderr, "Allocation of %d commands failed\n", CMDS_BLOCK);
					err = -ENOMEM;
					goto commands_cleanup;
				}
				commands = commandstmp;
			}
			linepos = 0;
		}
		fpos++;
	}
	printf("Got %ld commands\n", cmd_num);

	cmds_per_thread = cmd_num / num_threads;
	printf("Using %ld commands per thread\n", cmds_per_thread);

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
		goto commands_cleanup;
	}

	threadargs = malloc(num_threads * sizeof(struct threadargs_t));
	if(!threadargs) {
		fprintf(stderr, "Failed to allocate memory for threadargs\n");
		goto thread_cleanup;
	}

	#define min(a, b) ((a) > (b) ? (b) : (a))

	for(thread_cnt = 0; thread_cnt < num_threads; thread_cnt++)
	{
		threadargs[thread_cnt].sockets = sockets;
		threadargs[thread_cnt].socket = thread_cnt;
		threadargs[thread_cnt].tid = thread_cnt;
		threadargs[thread_cnt].remoteaddr = (struct sockaddr*)&inaddr;
		threadargs[thread_cnt].method = method;

		threadargs[thread_cnt].offset = commands[cmds_per_thread * thread_cnt].offset;
		threadargs[thread_cnt].length = commands[min(cmds_per_thread * (thread_cnt + 1) - 1, cmd_num)].offset -
						commands[cmds_per_thread * thread_cnt].offset;
		threadargs[thread_cnt].buffer = buffer;

		threadargs[thread_cnt].file = NULL;
		if(method == TX_METHOD_SENDFILE) {
			threadargs[thread_cnt].file = fopen(filename, "r");
			if(threadargs[thread_cnt].file < 0) {
				err = -errno;
				fprintf(stderr, "Failed to open file for thread %d: %s\n", thread_cnt, strerror(errno));
				goto thread_file_cleanup;
			}
		}

		err = -pthread_create(&threads[thread_cnt], NULL, send_thread, &threadargs[thread_cnt]);
		if(err) {
			fprintf(stderr, "Failed to create thread %d: %s\n", thread_cnt, strerror(-err));
			goto thread_file_cleanup;
		}
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
	i = thread_cnt;
	while(i-- > 0) {
		if(threadargs[i].file == NULL) {
			continue;
		}
		fclose(threadargs[i].file);
	}
//threadargs_cleanup:
	free(threadargs);
thread_cleanup:
	free(threads);
commands_cleanup:
	free(commands);
//memory_cleanup:
	free(buffer);
file_cleanup:
	fclose(file);
socket_cleanup:
	for(i = 0; i < num_threads; i++) {
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
