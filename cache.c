#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <errno.h>

#include "network.h"

#define PATH_SIZE_DEFAULT 32
#define PATH_SIZE_BLOCK 32

int cache_alloc(struct cache** ret, char* prefix) {
	int err = 0;
	struct cache* cache = malloc(sizeof(struct cache));
	if(!cache) {
		err = -ENOMEM;
		goto fail;
	}
	cache->default_path_len = PATH_SIZE_DEFAULT;
	cache->prefix = NULL;

	if(prefix) {
		cache->prefix = strdup(prefix);
		if(!cache->prefix) {
			err = -ENOMEM;
			goto fail_cache_alloc;
		}
		cache->len_prefix = strlen(prefix);
	}

	return 0;

fail_cache_alloc:
	free(cache);
fail:
	return err;
}

void cache_free(struct cache* cache) {
	if(cache->prefix) {
		free(cache->prefix);
	}
	free(cache);
}

static char* cache_get_fname_prefix(struct cache* cache, char* name) {
	size_t len = cache->len_prefix + strlen(name) + 1;
	char* fname = malloc(len);
	if(!fname) {
		return NULL;
	}
	fname[len - 1] = 0;
	strncpy(fname, cache->prefix, cache->len_prefix);
	strcpy(fname + cache->len_prefix, name);
	return fname;
}

static char* cache_get_fname_frame(struct cache* cache, char* name, unsigned int frame_id) {
	ssize_t str_len;
	char* fname;
	do {
		fname = malloc(cache->default_path_len);
		if(!fname) {
			goto fail;
		}
		str_len = snprintf(fname, cache->default_path_len, "%s_%u", name, frame_id);
		if(str_len < 0) {
			goto fail_fname;
		}
		if(str_len >= cache->default_path_len) {
			cache->default_path_len += PATH_SIZE_BLOCK;
			free(fname);
		}
	} while(str_len >= cache->default_path_len);
	return fname;

fail_fname:
	free(fname);
fail:
	return NULL;
}

static int cache_save_file(struct cache* cache, char* name, char* data, size_t data_len) {
	int err = 0;
	off_t offset = 0;
	ssize_t write_size;
	char* fname = cache_get_fname_prefix(cache, name);
	if(!fname) {
		err = -ENOMEM;
		goto fail;
	}

	FILE* file = fopen(fname, "w");
	if(!file) {
		err = -errno;
		goto fail_fname;
	}

	while(offset < data_len) {
		write_size = write(fileno(file), data + offset, data_len - offset);
		if(write_size < 0) {
			err = -errno;
			goto fail_file;
		}
		offset += write_size;
	}

fail_file:
	fclose(file);
fail_fname:
	free(fname);
fail:
	return err;
}

static int cache_save_frame(struct cache* cache, char* name, struct net_frame* frame, unsigned int frame_id) {
	int err;
	char* fname;
	struct pf_cmd* cmd_start, *cmd_end;

	cmd_start = &frame->cmds[0];
	cmd_end = &frame->cmds[frame->num_cmds - 1];

	assert(cmd_start->data == cmd_end->data);

	if(frame->num_cmds < 1) {
		return 0;
	}

	fname = cache_get_fname_frame(cache, name, frame_id);
	if(!fname) {
		err = -ENOMEM;
		goto fail;
	}

	err = cache_save_file(fname, cmd_start->data + cmd_start->offset, cmd_end->offset + cmd_end->length - cmd_start->offset);

	free(fname);

fail:
	return err;
}

int cache_save_animation(struct cache* cache, char* name, struct net_animation* anim) {
	int err;
	size_t i;
	for(i = 0; i < anim->num_frames; i++) {
		if((err = cache_save_frame(cache, name, &anim->frames[i]))) {
			return err;
		}
	}
	return 0;
}

bool cache_has_animation(struct cache* cache, char* name) {

}

bool cache_has_frame(struct cache* cache, char* name) {

}

int cache_load_animation(struct cache* cache, char* name) {

}

int cache_load_frame(struct cache* cache, char* name, unsigned int frame_id) {

}

int cache_load_file(char* name, char** ret, size_t* file_size) {
	int err = 0;
	size_t len;
	ssize_t read_len;
	char* data;
	off_t offset = 0;
	char* fname = cache_get_fname_prefix(cache, name);
	if(!fname) {
		err = -ENOMEM;
		goto fail;
	}

	FILE* file = fopen(fname, "r");
	if(!file) {
		err = -errno;
		goto fail_fname;
	}
	fseek(file, 0, SEEK_END);
	len = ftell(file);
	fseek(file, 0, SEEK_SET);

	data = malloc(len);
	if(!data) {
		err = -ENOMEM;
		goto fail_file;
	}

	while(offset < len) {
		read_len = read(fileno(file), data + offset, len - offset);
		if(read_len < 0) {
			err = -errno;
			goto fail_data_alloc;
		}
		offset += read_len;
	}

	*ret = data;
	*file_size = len;

	fclose(file);
	free(fname);
	return 0;

fail_data_alloc:
	free(data);
fail_file:
	fclose(file);
fail_fname:
	free(fname);
fail:
	return err;
}
