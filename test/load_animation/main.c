#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "../../image.h"

int main(int argc, char** argv) {
	struct img_ctx* ctx;
	struct img_animation* anim;
	image_alloc(&ctx);
	image_load_animation(&anim, "test.gif");
	printf("Got %zu frames\n", anim->num_frames);
	image_free_animation(anim);
	image_free(ctx);
}
