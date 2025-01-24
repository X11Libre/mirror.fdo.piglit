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
 * Test that FramebufferTextureMultiviewOVR produces the necessary errors under
 * the conditions specified in the OVR_multiview spec.
 */

#include "piglit-util-gl.h"

PIGLIT_GL_TEST_CONFIG_BEGIN

	config.supports_gl_compat_version = 30;
	config.supports_gl_core_version = 31;
	config.khr_no_error_support = PIGLIT_HAS_ERRORS;

PIGLIT_GL_TEST_CONFIG_END

#define TEX_WIDTH 4
#define TEX_HEIGHT 4

void
piglit_init(int argc, char **argv)
{
	GLint max_layers, max_views = -1;
	GLuint tex_array, tex_3d, fbo;
	bool pass = true;

	piglit_require_extension("GL_OVR_multiview");

	/* get limits */
	glGetIntegerv(GL_MAX_ARRAY_TEXTURE_LAYERS, &max_layers);
	glGetIntegerv(GL_MAX_VIEWS_OVR, &max_views);
	printf("GL_MAX_ARRAY_TEXTURE_LAYERS = %d\n", max_layers);
	printf("GL_MAX_VIEWS_OVR = %d\n", max_views);

	if (!piglit_check_gl_error(GL_NO_ERROR))
		piglit_report_result(PIGLIT_FAIL);

	/*
	 * OVR_multiview specifies a minimum value of 2
	 */
	assert(max_views >= 2);

	/* generate 2d array texture */
	glGenTextures(1, &tex_array);
	glBindTexture(GL_TEXTURE_2D_ARRAY, tex_array);
	glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, GL_RGB8, TEX_WIDTH, TEX_HEIGHT, 2,
		     0, GL_RGB, GL_UNSIGNED_BYTE, NULL);
	glBindTexture(GL_TEXTURE_2D_ARRAY, 0);

	/* generate 3d texture */
	glGenTextures(1, &tex_3d);
	glBindTexture(GL_TEXTURE_3D, tex_3d);
	glTexImage3D(GL_TEXTURE_3D, 0, GL_RGB8, TEX_WIDTH, TEX_HEIGHT, 2, 0,
		     GL_RGB, GL_UNSIGNED_BYTE, NULL);
	glBindTexture(GL_TEXTURE_3D, 0);

	/* generate FBO */
	glGenFramebuffers(1, &fbo);
	glBindFramebuffer(GL_FRAMEBUFFER, fbo);
	if (!piglit_check_gl_error(GL_NO_ERROR))
		piglit_report_result(PIGLIT_FAIL);

	/*
	 * OVR_multiview:
	 * "An INVALID_VALUE error is generated if:
	 * - <numViews> is less than 1 or if <numViews> is greater than
	 *   MAX_VIEWS_OVR."
	 */
	glFramebufferTextureMultiviewOVR(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
					 tex_array, 0, 0, 0);
	pass &= piglit_check_gl_error(GL_INVALID_VALUE);

	glFramebufferTextureMultiviewOVR(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
					 tex_array, 0, 0, max_views + 1);
	pass &= piglit_check_gl_error(GL_INVALID_VALUE);

	/* (so <numViews> of 1 or MAX_VIEWS_OVR are presumably valid) */
	glFramebufferTextureMultiviewOVR(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
					 tex_array, 0, 0, 1);
	pass &= piglit_check_gl_error(GL_NO_ERROR);

	glFramebufferTextureMultiviewOVR(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
					 tex_array, 0, 0, max_views);
	pass &= piglit_check_gl_error(GL_NO_ERROR);

	/*
	 * OVR_multiview:
	 * "An INVALID_VALUE error is generated if:
	 * [...]
	 * - <texture> is a two-dimensional array texture and <baseViewIndex> +
	 *   <numViews> is larger than the value of MAX_ARRAY_TEXTURE_LAYERS."
	 */
	glFramebufferTextureMultiviewOVR(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
					 tex_array, 0, max_layers-1, 2);
	pass &= piglit_check_gl_error(GL_INVALID_VALUE);

	/*
	 * (so <baseViewIndex> + <numViews> less than MAX_ARRAY_TEXTURE_LAYERS
	 * is presumably valid)
	 */
	glFramebufferTextureMultiviewOVR(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
					 tex_array, 0, max_layers-2, 2);
	pass &= piglit_check_gl_error(GL_NO_ERROR);

	/*
	 * OVR_multiview:
	 * "An INVALID_VALUE error is generated if:
	 * [...]
	 * - texture is non-zero and <baseViewIndex> is negative."
	 */
	glFramebufferTextureMultiviewOVR(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
					 tex_array, 0, -1, 2);
	pass &= piglit_check_gl_error(GL_INVALID_VALUE);

	/*
	 * "An INVALID_OPERATION error is generated if texture is non-zero and
	 * is not the name of a two-dimensional array texture."
	 */
	glFramebufferTextureMultiviewOVR(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
					 tex_3d, 0, 0, 2);
	pass &= piglit_check_gl_error(GL_INVALID_OPERATION);

	piglit_report_result(pass ? PIGLIT_PASS : PIGLIT_FAIL);
}

enum piglit_result
piglit_display(void)
{
	/* Should never be reached */
	return PIGLIT_FAIL;
}
