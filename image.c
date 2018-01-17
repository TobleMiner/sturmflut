#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <errno.h>

#include <MagickWand/MagickWand.h>

#include "image.h"

int image_alloc(struct img_ctx** ret) {
	int err = 0;
	struct img_ctx* ctx = malloc(sizeof(struct img_ctx));
	if(!ctx) {
		err = -ENOMEM;
		goto fail;
	}

	MagickWandGenesis();

	*ret = ctx;

fail:
	return err;
}

void image_free(struct img_ctx* ctx) {
	MagickWandTerminus();
	free(ctx);
}



int image_load_animation(struct img_animation** ret, char* fname) {
	int err = 0;
	MagickWand* wand_base, *wand_coalesce;
	PixelWand* pixel;
	struct img_animation* anim;
	struct img_frame* frames, *img_frame;
	struct img_pixel* img_pixel;
	size_t num_images, x, y;

	wand_base = NewMagickWand();
	if(!wand_base) {
		err = -ENOMEM;
		goto fail;
	}

	if(!MagickReadImage(wand_base, fname)) {
		err = -ENOENT;
		goto fail_base_alloc;
	}

	anim = malloc(sizeof(struct img_animation));
	if(!anim) {
		err = -ENOMEM;
		goto fail_base_alloc;
	}
	anim->num_frames = 0;

	wand_coalesce = MagickCoalesceImages(wand_base);

	if(!wand_coalesce) {
		err = -ENOMEM;
		goto fail_anim_alloc;
	}
/**
	if(!MagickTransformImageColorspace(wand_coalesce, RGBColorspace)) {
		err = -EINVAL;
		goto fail_coalesce_alloc;
	}

	if(!MagickSetImageDepth(wand_coalesce, 32)) {
		err = -EINVAL;
		goto fail_coalesce_alloc;
	}
*/
	anim->width = MagickGetImageWidth(wand_coalesce);
	anim->height = MagickGetImageHeight(wand_coalesce);

	num_images = MagickGetNumberImages(wand_coalesce);
	frames = malloc(num_images * sizeof(struct img_frame));
	if(!frames) {
		err = -ENOMEM;
		goto fail_coalesce_alloc;
	}
	anim->frames = frames;

	pixel = NewPixelWand();
	if(!pixel) {
		err = -ENOMEM;
		goto fail_frames_alloc;
	}

	do {
		assert(anim->width == MagickGetImageWidth(wand_coalesce));
		assert(anim->height == MagickGetImageHeight(wand_coalesce));
		assert(anim->num_frames < num_images);

		img_frame = &frames[anim->num_frames];
		img_frame->duration_ms = MagickGetImageDelay(wand_coalesce);
		img_frame->num_pixels = anim->width * anim->height;

		img_frame->pixels = malloc(img_frame->num_pixels * sizeof(struct img_pixel));
		if(!img_frame->pixels) {
			err = -ENOMEM;
			goto fail_alloc_pixels;
		}
		anim->num_frames++;

		for(y = 0; y < anim->height; y++) {
			for(x = 0; x < anim->width; x++) {
				assert(y * anim->width + x < img_frame->num_pixels);

				if(!MagickGetImagePixelColor(wand_coalesce, x, y, pixel)) {
					err = -EINVAL;
					goto fail_alloc_pixels;
				}
				img_pixel = &img_frame->pixels[y * anim->width + x];
				img_pixel->color.red = PixelGetRedQuantum(pixel);
				img_pixel->color.green = PixelGetGreenQuantum(pixel);
				img_pixel->color.blue = PixelGetBlueQuantum(pixel);
				img_pixel->color.alpha = PixelGetAlphaQuantum(pixel);

				img_pixel->x = x;
				img_pixel->y = y;
			}
		}

	} while(MagickNextImage(wand_coalesce));

	*ret = anim;

	DestroyPixelWand(pixel);
	DestroyMagickWand(wand_coalesce);
	DestroyMagickWand(wand_base);
	return 0;

fail_alloc_pixels:
	while(anim->num_frames-- > 0) {
		img_frame = &frames[anim->num_frames];
		free(img_frame->pixels);
	}
//fail_pixel_alloc:
	DestroyPixelWand(pixel);
fail_frames_alloc:
	free(frames);
fail_coalesce_alloc:
	DestroyMagickWand(wand_coalesce);
fail_anim_alloc:
	free(anim);
fail_base_alloc:
	DestroyMagickWand(wand_base);
fail:
	return err;
}

void image_free_animation(struct img_animation* anim) {
	while(anim->num_frames-- > 0) {
		free(anim->frames[anim->num_frames].pixels);
	}

	free(anim->frames);
	free(anim);
}


void image_shuffle_frame(struct img_frame* frame) {
	size_t num_pixels = frame->num_pixels, i, pos1, pos2;
	struct img_pixel pixel;
	for(i = 0; i < num_pixels; i++) {
		pos1 = rand() % num_pixels;
		pos2 = rand() % num_pixels;
		pixel = frame->pixels[pos2];
		frame->pixels[pos2] = frame->pixels[pos1];
		frame->pixels[pos1] = pixel;
	}
}

void image_shuffle_animation(struct img_animation* anim) {
	size_t num_frames = anim->num_frames, i;
	for(i = 0; i < num_frames; i++) {
		image_shuffle_frame(&anim->frames[i]);
	}
}
