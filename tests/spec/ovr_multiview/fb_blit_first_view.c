/*
 * Copyright (C) 2025 James Hogan <james@albanarts.com>
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
 *
 */

/**
 * Test that blits to multiview framebuffers only write to the first view.
 *
 * A normal 2D read framebuffer containing green is blitted to a multiview draw
 * framebuffer containing red, either as a single full blit or two half blits,
 * and the multiview framebuffer is read to confirm that only the first view is
 * changed to green and the others remain red.
 */

#include "piglit-util-gl.h"

PIGLIT_GL_TEST_CONFIG_BEGIN

	config.supports_gl_compat_version = 30;
	config.supports_gl_core_version = 31;
	config.khr_no_error_support = PIGLIT_NO_ERRORS;

PIGLIT_GL_TEST_CONFIG_END

#define TEX_WIDTH 4
#define TEX_HEIGHT 4

static void
usage(const char *arg0, enum piglit_result result)
{
	printf("usage: %s [<blit> [<base>]]\n"
	       "  <blit>:   'full' or 'partial'\n"
	       "  <base>:   Multiview base layer\n",
	       arg0);
	piglit_report_result(result);
}

void
piglit_init(int argc, char **argv)
{
	GLint max_views = 2;
	GLuint tex_2d, tex_2da, fbo[2];
	GLenum fbstatus;
	bool pass = true;
	GLuint *pixels, *ptr;
	unsigned int layer, x, y;
	unsigned int base_layer = 0;
	unsigned int num_layers;
	bool partial_blit = false;

	piglit_require_extension("GL_OVR_multiview");

	glGetIntegerv(GL_MAX_VIEWS_OVR, &max_views);
	printf("GL_MAX_VIEWS_OVR = %d\n", max_views);

	if (!piglit_check_gl_error(GL_NO_ERROR) || max_views < 2)
		max_views = 2;

	if (argc > 1) {
		if (!strcmp(argv[1], "full")) {
			partial_blit = false;
		} else if (!strcmp(argv[1], "partial")) {
			partial_blit = true;
		} else {
			printf("unknown blit mode %s\n",
			       argv[1]);
			usage(argv[0], PIGLIT_FAIL);
		}
	}

	if (argc > 2) {
		base_layer = atoi(argv[2]);
		if ((int)base_layer < 0) {
			printf("base (%d) must be >= 0\n",
			       (int)base_layer);
			usage(argv[0], PIGLIT_FAIL);
		}
		if (base_layer > max_views - 2) {
			printf("base (%u) must be <= GL_MAX_VIEWS_OVR-2 (%u)\n",
			       base_layer, max_views - 2);
			usage(argv[0], PIGLIT_SKIP);
		}
	}
	num_layers = base_layer + 2;

	if (argc > 3)
		usage(argv[0], PIGLIT_FAIL);

	/* generate 2d texture */
	glGenTextures(1, &tex_2d);
	glBindTexture(GL_TEXTURE_2D, tex_2d);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB8, TEX_WIDTH, TEX_HEIGHT, 0,
		     GL_RGB, GL_UNSIGNED_BYTE, NULL);
	glBindTexture(GL_TEXTURE_2D, 0);

	/* generate 2d array texture */
	glGenTextures(1, &tex_2da);
	glBindTexture(GL_TEXTURE_2D_ARRAY, tex_2da);
	glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, GL_RGB8, TEX_WIDTH, TEX_HEIGHT,
		     num_layers, 0, GL_RGB, GL_UNSIGNED_BYTE, NULL);
	glBindTexture(GL_TEXTURE_2D_ARRAY, 0);

	/* Generate non-multiview read framebuffer and clear it green */
	glGenFramebuffers(2, fbo);
	glBindFramebuffer(GL_FRAMEBUFFER, fbo[0]);
	if (!piglit_check_gl_error(GL_NO_ERROR))
		piglit_report_result(PIGLIT_FAIL);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
			       GL_TEXTURE_2D, tex_2d, 0);
	if (!piglit_check_gl_error(GL_NO_ERROR))
		piglit_report_result(PIGLIT_FAIL);
	fbstatus = glCheckFramebufferStatus(GL_FRAMEBUFFER);
	if (fbstatus != GL_FRAMEBUFFER_COMPLETE) {
		printf("Line %u: Expected GL_FRAMEBUFFER_COMPLETE, got %s\n",
		       __LINE__, piglit_get_gl_enum_name(fbstatus));
		piglit_report_result(PIGLIT_FAIL);
	}
	glClearColor(0.0f, 1.0f, 0.0f, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT);
	if (!piglit_check_gl_error(GL_NO_ERROR))
		piglit_report_result(PIGLIT_FAIL);

	/* Generate multiview draw framebuffer and clear it red */
	glBindFramebuffer(GL_FRAMEBUFFER, fbo[1]);
	if (!piglit_check_gl_error(GL_NO_ERROR))
		piglit_report_result(PIGLIT_FAIL);
	glFramebufferTextureMultiviewOVR(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
					 tex_2da, 0, 0, num_layers);
	if (!piglit_check_gl_error(GL_NO_ERROR))
		piglit_report_result(PIGLIT_FAIL);
	fbstatus = glCheckFramebufferStatus(GL_FRAMEBUFFER);
	if (fbstatus != GL_FRAMEBUFFER_COMPLETE) {
		printf("Line %u: Expected GL_FRAMEBUFFER_COMPLETE, got %s\n",
		       __LINE__, piglit_get_gl_enum_name(fbstatus));
		piglit_report_result(PIGLIT_FAIL);
	}
	glClearColor(1.0f, 0.0f, 0.0f, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT);
	if (!piglit_check_gl_error(GL_NO_ERROR))
		piglit_report_result(PIGLIT_FAIL);

	/* Reattach only 2 layers to the multiview framebuffer */
	if (base_layer != 0) {
		glFramebufferTextureMultiviewOVR(GL_FRAMEBUFFER,
						 GL_COLOR_ATTACHMENT0, tex_2da,
						 0, base_layer, 2);
		if (!piglit_check_gl_error(GL_NO_ERROR))
			piglit_report_result(PIGLIT_FAIL);
	}

	/* Rebind the read framebuffer */
	glBindFramebuffer(GL_READ_FRAMEBUFFER, fbo[0]);
	if (!piglit_check_gl_error(GL_NO_ERROR))
		piglit_report_result(PIGLIT_FAIL);

	/*
	 * OVR_multiview:
	 * "Add the following paragraph to the end of the description of
	 * BlitFramebuffer in section 16.2.1 (Blitting Pixel Rectangles):
	 * 
	 * "If the draw framebuffer has multiple views (see section 9.2.8,
	 * FramebufferTextureMultiviewOVR), values taken from the read buffer
	 * are only written to draw buffers in the first view of the draw
	 * framebuffer."
	 */

	if (partial_blit) {
		/* do two partial blits */
		glBlitFramebuffer(0, 0, TEX_WIDTH/2, TEX_HEIGHT,
				  0, 0, TEX_WIDTH/2, TEX_HEIGHT,
				  GL_COLOR_BUFFER_BIT, GL_NEAREST);
		glBlitFramebuffer(TEX_WIDTH/2, 0, TEX_WIDTH, TEX_HEIGHT,
				  TEX_WIDTH/2, 0, TEX_WIDTH, TEX_HEIGHT,
				  GL_COLOR_BUFFER_BIT, GL_NEAREST);
	} else {
		/* do a single full blit */
		glBlitFramebuffer(0, 0, TEX_WIDTH, TEX_HEIGHT,
				  0, 0, TEX_WIDTH, TEX_HEIGHT,
				  GL_COLOR_BUFFER_BIT, GL_NEAREST);
	}
	if (!piglit_check_gl_error(GL_NO_ERROR))
		piglit_report_result(PIGLIT_FAIL);

	/* read back the multiview draw framebuffer */
	pixels = malloc(num_layers*TEX_WIDTH*TEX_HEIGHT*sizeof(*pixels));
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	glBindTexture(GL_TEXTURE_2D_ARRAY, tex_2da);
	glGetTexImage(GL_TEXTURE_2D_ARRAY, 0, GL_RGBA, GL_UNSIGNED_INT_8_8_8_8,
		      pixels);
	if (!piglit_check_gl_error(GL_NO_ERROR))
		piglit_report_result(PIGLIT_FAIL);

	/* validate that only the first layer has been made red */
	ptr = pixels;
	for (layer = 0; layer < num_layers; ++layer) {
		unsigned int layer_diff = 0;
		GLuint expect = (layer == base_layer) ? 0x00ff00ff : 0xff0000ff;
		for (y = 0; y < TEX_HEIGHT; ++y) {
			for (x = 0; x < TEX_WIDTH; ++x) {
				if (*ptr != expect && !layer_diff) {
					++layer_diff;
					pass = false;
					printf("layer %u at (%u, %u): unexpected #%08x, expected #%08x\n",
					       layer, x, y,
					       *ptr, expect);
				}
				++ptr;
			}
		}
	}

	piglit_report_result(pass ? PIGLIT_PASS : PIGLIT_FAIL);
}

enum piglit_result
piglit_display(void)
{
	/* Should never be reached */
	return PIGLIT_FAIL;
}
