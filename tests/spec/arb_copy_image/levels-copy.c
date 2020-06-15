/* Copyright © 2020 Intel Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

/** @file layers-copy.c
 *
 * Tests that glCopyImageSubData correctly copies all levels of a texture.
 * Also tests that the order in which levels are initialized doesn't affect
 * the copying (some drivers, e.g. gallium drivers, may be sensitive to the
 * initialization order).
 */

#include "piglit-util-gl.h"
#include <math.h>

PIGLIT_GL_TEST_CONFIG_BEGIN

	config.supports_gl_compat_version = 13;

	config.window_visual = PIGLIT_GL_VISUAL_RGB | PIGLIT_GL_VISUAL_DOUBLE;
	config.khr_no_error_support = PIGLIT_NO_ERRORS;

PIGLIT_GL_TEST_CONFIG_END

enum piglit_result
piglit_display(void)
{
	return PIGLIT_FAIL;
}

static const int tex_default_width = 32;
static const int tex_default_height = 32;
static const int tex_default_depth = 8;
static const int tex_default_levels = 6;

struct image {
	GLuint texture;
	GLenum target;
	int levels;
	int width, height, depth;
};

static void
get_img_dims(struct image *img, int level,
			int *width, int *height, int *depth)
{
	*width = MAX2(img->width >> level, 1);
	*height = MAX2(img->height >> level, 1);
	*depth = MAX2(img->depth >> level, 1);

	switch (img->target) {
	case GL_TEXTURE_1D:
	case GL_TEXTURE_1D_ARRAY:
		*height = 1;
		*depth = 1;
		break;

	case GL_TEXTURE_CUBE_MAP:
		*depth = 6;
		break;

	case GL_TEXTURE_2D:
	case GL_TEXTURE_2D_ARRAY:
		*depth = 1;
		break;
	}
}

static void
init_image(struct image *img, GLenum texture_type,
		int width, int height, int depth, int levels)
{
	img->target = texture_type;

	img->width = width;
	img->height = height;
	img->depth = depth;
	img->levels = levels;

	get_img_dims(img, 0, &img->width, &img->height, &img->depth);

	glGenTextures(1, &img->texture);
	glBindTexture(img->target, img->texture);
	glTexParameteri(img->target, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(img->target, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
}

static void
tex_image(struct image *img, int level, bool upload_data)
{
	glBindTexture(img->target, img->texture);

	int width, height, depth;
	get_img_dims(img, level, &width, &height, &depth);

	GLuint *data = NULL;
	if (upload_data) {
		data = malloc(width * height * depth * sizeof(GLuint));
		for (int i = 0; i < width * height * depth; i++) {
			data[i] = 0xFF / (level + 1);
		}
	}

	switch (img->target) {
	case GL_TEXTURE_1D:
		glTexImage1D(img->target, level,
				 GL_RGBA8, width, 0,
				 GL_RGBA, GL_UNSIGNED_INT_8_8_8_8, data);
		break;

	case GL_TEXTURE_CUBE_MAP:
		for (int k = 0; k < 6; ++k) {
			glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + k, level,
					 GL_RGBA8, width, height, 0,
					 GL_RGBA, GL_UNSIGNED_INT_8_8_8_8,
					 data);
		}
		break;

	case GL_TEXTURE_2D:
	case GL_TEXTURE_1D_ARRAY:
		glTexImage2D(img->target, level,
				 GL_RGBA8, width, height, 0,
				 GL_RGBA, GL_UNSIGNED_INT_8_8_8_8, data);
		break;

	case GL_TEXTURE_3D:
	case GL_TEXTURE_2D_ARRAY:
		glTexImage3D(img->target, level,
				 GL_RGBA8, width, height, depth, 0,
				 GL_RGBA, GL_UNSIGNED_INT_8_8_8_8, data);
		break;
	default:
		assert(!"Invalid target");
	}

	free(data);
}

static bool
check_image(GLenum target_type, int level, int data_size)
{
	bool pass = true;
	GLuint expected = 0xFF / (level + 1);

	GLuint *data = malloc(data_size * sizeof(GLuint));
	glGetTexImage(target_type, level, GL_RGBA, GL_UNSIGNED_INT_8_8_8_8, data);

	for (int i = 0; i < data_size; i++) {
		if (data[i] != expected) {
			pass = false;
			fprintf(stderr, "%s: level %d, texel idx %d (%d total) "
					"comparison failed (%d != %d)\n",
					piglit_get_gl_enum_name(target_type),
					level, i, data_size, data[i], expected);
			break;
		}
	}

	free(data);

	return pass;
}

enum tex_init_order {
	TEX_ORDER_FORWARD,
	TEX_ORDER_BACKWARD,

	TEX_ORDER_END,
};

static bool
run_test(GLenum target_type, enum tex_init_order init_order)
{
	struct image srcImg, dstImg;
	bool pass = true;

	init_image(&srcImg, target_type, tex_default_width, tex_default_height,
			tex_default_depth, tex_default_levels);
	init_image(&dstImg, target_type, tex_default_width, tex_default_height,
			tex_default_depth, tex_default_levels);

	if (init_order == TEX_ORDER_FORWARD) {
		for(int level = 0; level < srcImg.levels; level++) {
			tex_image(&srcImg, level, true);
			tex_image(&dstImg, level, false);
		}
	} else {
		for(int level = srcImg.levels - 1; level >= 0; level--) {
			tex_image(&srcImg, level, true);
			tex_image(&dstImg, level, false);
		}
	}

	for(int level = 0; level < srcImg.levels; level++) {
		int width, height, depth;
		get_img_dims(&srcImg, level, &width, &height, &depth);

		glCopyImageSubData(srcImg.texture, target_type, level, 0, 0, 0, dstImg.texture,
						target_type, level, 0, 0, 0, width, height, depth);
	}

	for(int level = 0; level < srcImg.levels; level++) {
		int width, height, depth;
		get_img_dims(&srcImg, level, &width, &height, &depth);

		glBindTexture(target_type, dstImg.texture);

		if (target_type == GL_TEXTURE_CUBE_MAP) {
			for (int k = 0; k < 6; ++k) {
				pass = check_image(GL_TEXTURE_CUBE_MAP_POSITIVE_X + k,
								level, width * height) && pass;
			}
		} else {
			pass = check_image(target_type, level, width * height * depth) && pass;
		}
	}

	glDeleteTextures(1, &srcImg.texture);
	glDeleteTextures(1, &dstImg.texture);

	piglit_report_subtest_result(pass ? PIGLIT_PASS : PIGLIT_FAIL,
				"Target type: %s, width: %d, height: %d, depth: %d, levels: %d, init order: %s",
				piglit_get_gl_enum_name(target_type), srcImg.width, srcImg.height,
				srcImg.depth, srcImg.levels,
				init_order == TEX_ORDER_FORWARD ? "'forward'" : "'backward'");

	return pass;
}

void
piglit_init(int argc, char **argv)
{
	piglit_require_extension("GL_ARB_copy_image");

	bool pass = true;

	for (enum tex_init_order order = TEX_ORDER_FORWARD; order < TEX_ORDER_END; order++) {
		pass = run_test(GL_TEXTURE_1D, order) && pass;
		pass = run_test(GL_TEXTURE_2D, order) && pass;
		pass = run_test(GL_TEXTURE_3D, order) && pass;

		if (piglit_is_extension_supported("GL_EXT_texture_array")) {
			pass = run_test(GL_TEXTURE_1D_ARRAY, order) && pass;
			pass = run_test(GL_TEXTURE_2D_ARRAY, order) && pass;
		}

		if (piglit_is_extension_supported("GL_ARB_texture_cube_map")) {
			pass = run_test(GL_TEXTURE_CUBE_MAP, order) && pass;
		}
	}

	piglit_report_result(pass ? PIGLIT_PASS : PIGLIT_FAIL);
}
