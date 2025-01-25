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
 * Test the additional conditions required for multiview framebuffer
 * completeness in the OVR_multiview spec, specifically combinations of
 * multiview attachments.
 */

#include "piglit-util-gl.h"

PIGLIT_GL_TEST_CONFIG_BEGIN

	config.supports_gl_compat_version = 30;
	config.supports_gl_core_version = 31;
	config.khr_no_error_support = PIGLIT_NO_ERRORS;

PIGLIT_GL_TEST_CONFIG_END

static void
check_attachment_param(GLenum target, GLenum attachment, GLuint texture,
		       GLint base_view_index, GLsizei num_views,
		       const char *label, GLint expected_status)
{
	GLenum fbstatus;

	glFramebufferTextureMultiviewOVR(target, attachment, texture, 0,
					 base_view_index, num_views);
	if (!piglit_check_gl_error(GL_NO_ERROR))
		piglit_report_result(PIGLIT_FAIL);
	fbstatus = glCheckFramebufferStatus(target);
	if (fbstatus != expected_status) {
		printf("%s: Expected %s, got %s\n",
		       label, piglit_get_gl_enum_name(expected_status),
		       piglit_get_gl_enum_name(fbstatus));
		piglit_report_result(PIGLIT_FAIL);
	}
}

void
piglit_init(int argc, char **argv)
{
	GLuint tex, depth, fbo;

	piglit_require_extension("GL_OVR_multiview");

	/* generate 2d array texture */
	glGenTextures(1, &tex);
	glBindTexture(GL_TEXTURE_2D_ARRAY, tex);
	glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, GL_RGB8, piglit_width,
		     piglit_height, 4, 0, GL_RGB, GL_UNSIGNED_BYTE, NULL);
	glBindTexture(GL_TEXTURE_2D_ARRAY, 0);

	/* generate 2d array texture for depth/stencil */
	glGenTextures(1, &depth);
	glBindTexture(GL_TEXTURE_2D_ARRAY, depth);
	glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, GL_DEPTH24_STENCIL8, piglit_width,
		     piglit_height, 2, 0, GL_DEPTH_STENCIL,
		     GL_UNSIGNED_INT_24_8, NULL);
	glBindTexture(GL_TEXTURE_2D_ARRAY, 0);

	/* generate FBO */
	glGenFramebuffers(1, &fbo);
	glBindFramebuffer(GL_FRAMEBUFFER, fbo);
	if (!piglit_check_gl_error(GL_NO_ERROR))
		piglit_report_result(PIGLIT_FAIL);

	/*
	 * OVR_multiview:
	 * "Add the following to the list of conditions required for framebuffer
	 * completeness in section 9.4.2 (Whole Framebuffer Completeness):
	 *
	 * "The number of views is the same for all populated attachments.
	 *
	 * { FRAMEBUFFER_INCOMPLETE_VIEW_TARGETS_OVR }"
	 */

	/*
	 * COLOR_ATTACHMENT0: 2 layers (2-3) *
	 * COLOR_ATTACHMENT1: 1 layer (0) *
	 * incomplete
	 */
	check_attachment_param(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, tex, 2, 2,
			       "initial color0", GL_FRAMEBUFFER_COMPLETE);
	check_attachment_param(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, tex, 0, 1,
			       "mismatch color0/color1", 
			       GL_FRAMEBUFFER_INCOMPLETE_VIEW_TARGETS_OVR);
	/*
	 * COLOR_ATTACHMENT0: 2 layers (2-3)
	 * COLOR_ATTACHMENT1: 2 layers (0-1) *
	 * complete
	 */
	check_attachment_param(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, tex, 0, 2,
			       "match color1", GL_FRAMEBUFFER_COMPLETE);
	/*
	 * COLOR_ATTACHMENT0: 2 layers (2-3)
	 * COLOR_ATTACHMENT1: 2 layers (0-1)
	 * DEPTH_ATTACHMENT: 1 layer (0) *
	 * incomplete
	 */
	check_attachment_param(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, depth, 0, 1,
			       "mismatch depth", 
			       GL_FRAMEBUFFER_INCOMPLETE_VIEW_TARGETS_OVR);
	/*
	 * COLOR_ATTACHMENT0: 2 layers (2-3)
	 * COLOR_ATTACHMENT1: 2 layers (0-1)
	 * DEPTH_ATTACHMENT: 2 layers (0-1) *
	 * complete
	 */
	check_attachment_param(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, depth, 0, 2,
			       "match depth", GL_FRAMEBUFFER_COMPLETE);
	/*
	 * COLOR_ATTACHMENT0: 2 layers (2-3)
	 * COLOR_ATTACHMENT1: 2 layers (0-1)
	 * DEPTH_ATTACHMENT: 2 layers (0-1)
	 * STENCIL_ATTACHMENT: 1 layer (0) *
	 * incomplete
	 */
	check_attachment_param(GL_FRAMEBUFFER, GL_STENCIL_ATTACHMENT, depth, 0,
			       1, "mismatch stencil", 
			       GL_FRAMEBUFFER_INCOMPLETE_VIEW_TARGETS_OVR);
	/*
	 * COLOR_ATTACHMENT0: 2 layers (2-3)
	 * COLOR_ATTACHMENT1: 2 layers (0-1)
	 * DEPTH_ATTACHMENT: 2 layers (0-1)
	 * STENCIL_ATTACHMENT: 2 layers (0-1) *
	 * complete
	 */
	check_attachment_param(GL_FRAMEBUFFER, GL_STENCIL_ATTACHMENT, depth, 0,
			       2, "match stencil", GL_FRAMEBUFFER_COMPLETE);
	/*
	 * COLOR_ATTACHMENT0: 2 layers (2-3)
	 * COLOR_ATTACHMENT1: 2 layers (0-1)
	 * DEPTH_ATTACHMENT: none *
	 * STENCIL_ATTACHMENT: 2 layers (0-1)
	 * complete
	 */
	check_attachment_param(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, 0, 0, 0,
			       "clear depth", GL_FRAMEBUFFER_COMPLETE);
	/*
	 * COLOR_ATTACHMENT0: 2 layers (2-3)
	 * COLOR_ATTACHMENT1: 2 layers (0-1)
	 * DEPTH_ATTACHMENT: 1 layer (0) *
	 * STENCIL_ATTACHMENT: 2 layers (0-1)
	 * incomplete
	 */
	check_attachment_param(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, depth, 0, 1,
			       "mismatch depth 2",
			       GL_FRAMEBUFFER_INCOMPLETE_VIEW_TARGETS_OVR);
	/*
	 * COLOR_ATTACHMENT0: 2 layers (2-3)
	 * COLOR_ATTACHMENT1: 2 layers (0-1)
	 * DEPTH_ATTACHMENT: 2 layers (0-1) *
	 * STENCIL_ATTACHMENT: 2 layers (0-1)
	 * complete
	 */
	check_attachment_param(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, depth, 0, 2,
			       "match depth 2", GL_FRAMEBUFFER_COMPLETE);
	/*
	 * COLOR_ATTACHMENT0: 1 layer (2) *
	 * COLOR_ATTACHMENT1: 2 layers (0-1)
	 * DEPTH_ATTACHMENT: 2 layers (0-1)
	 * STENCIL_ATTACHMENT: 2 layers (0-1)
	 * incomplete
	 */
	check_attachment_param(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, tex, 2, 1,
			       "mismatch color0",
			       GL_FRAMEBUFFER_INCOMPLETE_VIEW_TARGETS_OVR);

	piglit_report_result(PIGLIT_PASS);
}

enum piglit_result
piglit_display(void)
{
	/* Should never be reached */
	return PIGLIT_FAIL;
}
