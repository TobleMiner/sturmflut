#ifndef _NETWORK_H_
#define _NETWORK_H_

#include <stdint.h>
#include <pthread.h>

#include <sys/socket.h>
#include <sys/types.h>

#include "image.h"
#include "progress.h"
#include "main.h"

enum {
	NET_STATE_IDLE,
	NET_STATE_SENDING,
	NET_STATE_SHUTDOWN
};

struct net_threadargs_send {
	struct net* net;
	struct sockaddr_storage* remoteaddr;
	size_t remoteaddr_len;

	unsigned int thread_id;
};

struct pf_cmd {
	char* cmd;
	size_t length;

	char* data;
	off_t offset;
};

struct net_frame {
	unsigned long duration_ms;
	struct pf_cmd* cmds;
	size_t num_cmds;

	char* data;
};

struct net_animation {
	struct net_frame* frames;
	size_t num_frames;
};

struct net_threadargs_animate {
	struct net* net;
	struct net_animation* anim;
};

struct net {
	int state;

	bool ignore_broken_pipe;

	struct sockaddr_in* src_addresses;
	unsigned int num_src_addresses;

	struct net_frame* current_frame;

	pthread_t* threads_send;
	struct net_threadargs_send* targs_send;
	unsigned int num_send_threads;

	pthread_t thread_animate;
	struct net_threadargs_animate targs_animate;
};

void net_frame_free(struct net_frame* frame);
int net_frame_to_net_frame(struct net_frame* ret, struct img_frame* src, bool monochrome, unsigned int offset_x, unsigned int offset_y, unsigned int sparse_perc);
void net_free_animation(struct net_animation* anim);
int net_animation_to_net_animation(struct net_animation** ret, struct img_animation* src, bool monochrome, unsigned int offset_x, unsigned int offset_y, unsigned int sparse_perc, progress_cb progress_cb);

int net_alloc(struct net** ret);
void net_free(struct net* net);
int net_send_animation(struct net* net, struct sockaddr_storage* dst_address, size_t dstaddr_len, unsigned int num_threads, struct net_animation* anim);
void net_shutdown(struct net* net);

#endif
