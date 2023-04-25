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
			"[-i <0|1>] [-t <number of threads>] [-m] [-o <offset-spec>] [-O] [-s <percentage>] [-S] [-h]\n", binary);
}

static void generic_progress_cb(size_t current, size_t total, const char* fmt) {
	printf("\r                                                      "); // Don't ask
	printf(fmt, current, total);
	if(current == total) {
		printf("\n");
	}
	fflush(stdout);
}

static void load_progress_cb(size_t current, size_t total) {
	generic_progress_cb(current, total, "\r%zu/%zu frames loaded");
}

static void optimize_progress_cb(size_t current, size_t total) {
	generic_progress_cb(current, total, "\r%zu/%zu frames optimized");
}

static void shuffle_progress_cb(size_t current, size_t total) {
	generic_progress_cb(current, total, "\r%zu/%zu frames shuffled");
}

static void conversion_progress_cb(size_t current, size_t total) {
	generic_progress_cb(current, total, "\r%zu/%zu frames converted");
}

int main(int argc, char** argv)
{
	int opt, num_threads = NUM_THREADS_DEFAULT, err = 0;
	struct sockaddr_storage* inaddr;
	size_t inaddr_len;
	bool monochrome = false;
	bool optimize = false;
	bool saver = false;
	char *host, *port = PORT_DEFAULT;
	unsigned int offset_x = 0, offset_y = 0, sparse_perc = 100;

	struct img_ctx* img_ctx;
	struct img_animation* anim;
	struct net_animation* net_anim;
	struct net* net;
	struct addrinfo* host_addr;

	while((opt = getopt(argc, argv, "p:i:t:hmo:Os:S")) != -1) {
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
			case('o'):
				if(sscanf(optarg, "%u:%u", &offset_x, &offset_y) != 2) {
					fprintf(stderr, "Invalid offset spec, must be <horizontal>:<vertical>\n");
					print_usage(argv[0]);
					exit(1);
				}
				break;
			case('O'):
				optimize = true;
				break;
			case('s'):
				sparse_perc = atoi(optarg);
				if(sparse_perc > 100 || sparse_perc < 0) {
					fprintf(stderr, "Invalid sparse spec, must be in range 0-100\n");
					print_usage(argv[0]);
					exit(1);
				}
				break;
			case('S'):
				saver = true;
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

	if((err = image_load_animation(&anim, filename, load_progress_cb))) {
		fprintf(stderr, "Failed load animation: %s\n", strerror(-err));
		goto fail_image_alloc;
	}

	printf("Animation loaded\n");
	if (optimize) {
		printf("Optimizing animation...\n");

		if((err = image_optimize_animation(anim, optimize_progress_cb))) {
			fprintf(stderr, "Failed to optimize animation: %s\n", strerror(-err));
			goto fail_image_alloc;
		}

		printf("Animation optimized\n");
	}
	printf("Shuffling animation...\n");

	image_shuffle_animation(anim, shuffle_progress_cb);

	printf("Shuffling complete\n");
	printf("Converting animation to pixelflut commands...\n");

	if((err = net_animation_to_net_animation(&net_anim, anim, monochrome, offset_x, offset_y, sparse_perc, conversion_progress_cb))) {
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
	net->data_saving = saver;

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
