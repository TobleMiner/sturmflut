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
#include <netdb.h>

#include "main.h"
#include "image.h"
#include "network.h"

#define NUM_THREADS_DEFAULT 10
#define MAX_CONNECTIONS_PER_ADDRESS 100
#define SHARED_CONNECTIONS 0

#define IGNORE_BROKEN_PIPE_DEFAULT 1
#define PORT_DEFAULT "1234"
#define FILENAME_DEFAULT "image.png"

#define NUM_MY_ADDRS 0

int ignore_broken_pipe = IGNORE_BROKEN_PIPE_DEFAULT;

char* filename = FILENAME_DEFAULT;

bool do_exit = false;

void doshutdown(int signal) {
	do_exit = true;
}

static void print_usage(char* binary) {
	fprintf(stderr, "USAGE: %s <host> [file to send] [-p <port>] [-a <source ip address>] "
			"[-i <0|1>] [-t <number of threads>] [-m] [-h]\n", binary);
}

int main(int argc, char** argv)
{
	int opt, num_threads = NUM_THREADS_DEFAULT, err = 0;
	struct sockaddr_storage* inaddr;
	size_t inaddr_len;
	bool monochrome = false;
	char *host, *port = PORT_DEFAULT;

	struct img_ctx* img_ctx;
	struct img_animation* anim;
	struct net_animation* net_anim;
	struct net* net;
	struct addrinfo* host_addr;

	while((opt = getopt(argc, argv, "p:it:hm")) != -1) {
		switch(opt) {
			case('p'):
				port = optarg;
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
				monochrome = true;
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

	printf("Will send '%s' to %s:%s\n", filename, host, port);

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

	if((err = -getaddrinfo(host, port, NULL, &host_addr))) {
		goto fail;
	}

	inaddr = (struct sockaddr_storage*)host_addr->ai_addr;
	inaddr_len = host_addr->ai_addrlen;

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
		goto fail_addrinfo_alloc;
	}

	printf("Loading animation...\n");

	if((err = image_load_animation(&anim, filename))) {
		fprintf(stderr, "Failed load animation: %s\n", strerror(-err));
		goto fail_image_alloc;
	}

	printf("Animation loaded\n");
	printf("Shuffling animation...\n");

	image_shuffle_animation(anim);

	printf("Shuffling complete\n");
	printf("Converting animation to pixelflut commands...\n");

	if((err = net_animation_to_net_animation(&net_anim, anim, monochrome))) {
		fprintf(stderr, "Failed to convert animation to pixelflut commands: %s\n", strerror(-err));
		goto fail_anim_load;
	}

	printf("Conversion finished\n");
	image_free_animation(anim);

	if((err = net_alloc(&net))) {
		fprintf(stderr, "Failed to allocate network context: %s\n", strerror(-err));
		goto fail_anim_convert;
	}
	net->ignore_broken_pipe = ignore_broken_pipe;

	printf("Starting to flut\n");

	if((err = net_send_animation(net, inaddr, inaddr_len, num_threads, net_anim))) {
		fprintf(stderr, "Failed to send animation: %s\n", strerror(-err));
		goto fail_net_alloc;
	}

	while(!do_exit) {
		usleep(500000);
	}

	printf("Exiting...\n");

	net_shutdown(net);

	net_free(net);
	net_free_animation(net_anim);
	image_free(img_ctx);
	return 0;

fail_net_alloc:
	net_free(net);
fail_anim_convert:
	net_free_animation(net_anim);
fail_anim_load:
	image_free_animation(anim);
fail_image_alloc:
	image_free(img_ctx);
fail_addrinfo_alloc:
	freeaddrinfo(host_addr);
fail:
	return err;
}
