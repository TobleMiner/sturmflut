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
#include "image.h"
#include "network.h"

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

int ignore_broken_pipe = IGNORE_BROKEN_PIPE_DEFAULT;

char* filename = FILENAME_DEFAULT;

bool do_exit = false;

void doshutdown(int signal) {
	do_exit = true;
}

static void print_usage(char* binary) {
	fprintf(stderr, "USAGE: %s <host> [file to send] [-p <port>] [-a <source ip address>] "
			"[-i <0|1>] [-t <number of threads>] [-m <%d|%d>] [-h]\n", binary, TX_METHOD_SENDFILE, TX_METHOD_SEND);
}

int main(int argc, char** argv)
{
	int num_threads = NUM_THREADS_DEFAULT, err = 0, method = METHOD_DEFAULT;
	struct sockaddr_in inaddr;
	char opt, *host;
	unsigned short port = PORT_DEFAULT;

	struct img_ctx* img_ctx;
	struct img_animation* anim;
	struct net_animation* net_anim;
	struct net* net;

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

	inet_pton(AF_INET, host, &(inaddr.sin_addr.s_addr));
	inaddr.sin_port = htons(port);
	inaddr.sin_family = AF_INET;
/*	if(NUM_MY_ADDRS)
	{
		for(i = 0; i < NUM_MY_ADDRS; i++)
		{
			inmyaddrs[i].sin_family = AF_INET;
			inmyaddrs[i].sin_port = 0;
			inet_pton(AF_INET, myaddrs[i], &(inmyaddrs[i].sin_addr.s_addr));
		}
	}
*/
	if((err = image_alloc(&img_ctx))) {
		fprintf(stderr, "Failed to allocate image context: %s\n", strerror(-err));
		goto fail;
	}

	if((err = image_load_animation(&anim, filename))) {
		fprintf(stderr, "Failed load animation: %s\n", strerror(-err));
		goto fail_image_alloc;
	}

	if((err = net_animation_to_net_animation(&net_anim, anim))) {
		fprintf(stderr, "Failed to convert animation to pixelflut commands: %s\n", strerror(-err));
		goto fail_anim_load;
	}

	if((err = net_alloc(&net))) {
		fprintf(stderr, "Failed to allocate network context: %s\n", strerror(-err));
		goto fail_anim_convert;
	}

	if((err = net_send_animation(net, &inaddr, num_threads, net_anim))) {
		fprintf(stderr, "Failed to send animation: %s\n", strerror(-err));
		goto fail_net_alloc;
	}

	while(!do_exit) {
		usleep(500000);
	}

	net_shutdown(net);

fail_net_alloc:
	net_free(net);
fail_anim_convert:
	net_free_animation(net_anim);
fail_anim_load:
	image_free_animation(anim);
fail_image_alloc:
	image_free(img_ctx);
fail:
	return err;
}
