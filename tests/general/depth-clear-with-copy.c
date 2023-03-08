/*
 * Copyright © 2023 Zeb Figura
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
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

/** @file depth-copy.c
 *
 * A regression test for a broken code path in radeonsi related to
 * fast depth clears.
 */

#include "piglit-util-gl.h"
#include <stdlib.h>

PIGLIT_GL_TEST_CONFIG_BEGIN

	config.supports_gl_compat_version = 11;
	config.window_visual = PIGLIT_GL_VISUAL_RGB | PIGLIT_GL_VISUAL_DOUBLE |
			       PIGLIT_GL_VISUAL_DEPTH;

PIGLIT_GL_TEST_CONFIG_END

void piglit_init(int argc, char **argv)
{
	piglit_require_extension("GL_ARB_copy_image");
	piglit_require_extension("GL_ARB_framebuffer_object");
	glDepthFunc(GL_ALWAYS);
	glEnable(GL_DEPTH_TEST);
	glDepthMask(GL_TRUE);
}

enum piglit_result piglit_display(void)
{
	GLuint fb, depth[2];
	bool pass = true;
	GLfloat *pixels;
	unsigned int i;

	glGenTextures(2, depth);
	glBindTexture(GL_TEXTURE_2D, depth[0]);
	glTexStorage2D(GL_TEXTURE_2D, 1, GL_DEPTH_COMPONENT24, piglit_width, piglit_height);
	glBindTexture(GL_TEXTURE_2D, depth[1]);
	glTexStorage2D(GL_TEXTURE_2D, 1, GL_DEPTH_COMPONENT24, piglit_width, piglit_height);

	/* Clear the first texture to 0. */
	glGenFramebuffers(1, &fb);
	glBindFramebuffer(GL_FRAMEBUFFER, fb);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, depth[0], 0);
	assert(glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE);
	glClearDepth(0.0);
	glClear(GL_DEPTH_BUFFER_BIT);

	/* Copy to the second texture. */
	glCopyImageSubData(depth[0], GL_TEXTURE_2D, 0, 0, 0, 0, depth[1], GL_TEXTURE_2D, 0, 0, 0, 0, piglit_width, piglit_height, 1);

	/* Clear the first texture to 1. */
	glClearDepth(1.0);
	glClear(GL_DEPTH_BUFFER_BIT);

	/* Draw to the first texture. */

	glBegin(GL_TRIANGLE_STRIP);
	glVertex3f(-0.6f, -0.7f, 0.5f);
	glVertex3f( 0.8f,  0.5f, 0.5f);
	glVertex3f( 0.7f, -0.7f, 0.5f);
	glEnd();

	pixels = malloc(piglit_width * piglit_height * sizeof(*pixels));
	glReadPixels(0, 0, piglit_width, piglit_height, GL_DEPTH_COMPONENT, GL_FLOAT, pixels);
	for (i = 0; i < piglit_width * piglit_height; ++i) {
		GLfloat probe = pixels[i];
		/* Every pixel should either be 0.75f (tri depth) or 1.0f (clear depth). */
		if (fabsf(probe - 0.75f) >= 0.01 && fabsf(probe - 1.0f) >= 0.01) {
			printf("Got depth %.8e at (%u, %u).\n", probe, i % piglit_width, i / piglit_width);
			pass = false;
			break;
		}
	}
	free(pixels);

	piglit_present_results();

	glDeleteFramebuffers(1, &fb);
	glDeleteTextures(2, depth);

	return pass ? PIGLIT_PASS : PIGLIT_FAIL;
}
