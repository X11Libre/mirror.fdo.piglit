/*
 * Copyright © 2024 Collabora Ltd
 * SPDX-License-Identifier: MIT
 *
 * Test for clear before render in texture preparation
 * If the texture is small and we use memcpy in
 * TexSubImage2D then that can complete before the clear,
 * which means that if the driver doesn't synchronize the
 * GPU and CPU properly the clear can overwrite the texture data.
 */

#include "piglit-util-gl.h"

PIGLIT_GL_TEST_CONFIG_BEGIN

	config.supports_gl_compat_version = 30;

	config.window_visual = PIGLIT_GL_VISUAL_DOUBLE | PIGLIT_GL_VISUAL_RGBA;
	config.khr_no_error_support = PIGLIT_NO_ERRORS;

PIGLIT_GL_TEST_CONFIG_END

static GLuint
create_texture(GLsizei w, GLsizei h)
{
	GLuint tex, fbo;
	GLint i;
	static GLfloat red[] = { 1.0, 0, 0, 1.0 };

	/* prepare a blob filled with green and max alpha */
	GLubyte *colorblob = calloc(4, w * h);
	for (i = 1; i < w*h*4; i += 2) {
		colorblob[i] = 0xff;
	}

	/* get a framebuffer for the texture */
	glGenFramebuffers(1, &fbo);
	glBindFramebuffer(GL_FRAMEBUFFER, fbo);

	/* create texture */
	glGenTextures(1, &tex);
	glBindTexture(GL_TEXTURE_2D, tex);
	glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA8, w, h);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

	/* use ClearBuffer to fill with red (likely on the GPU) */
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
	glClearBufferfv(GL_COLOR, 0, red);
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	glDeleteFramebuffers(1, &fbo);

	/* now fill with green via TexSubImage2D (possibly on the CPU) */
	glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, colorblob);
	glBindTexture(GL_TEXTURE_2D, 0);

	free(colorblob);

	return tex;
}

enum piglit_result
piglit_display(void)
{
	static const GLfloat green[3] = {0.0, 1.0, 0.0};
	GLboolean pass = GL_TRUE;
	GLuint tex;

	/* clear frame buffer */
	glClearColor(0.0, 0.0, 0.0, 0.0);
	glClear(GL_COLOR_BUFFER_BIT);

	/* create a small texture */
	tex = create_texture(4, 4);

	/* draw with it */
	glBindTexture(GL_TEXTURE_2D, tex);
	glEnable(GL_TEXTURE_2D);
	piglit_draw_rect_tex(0, 0, piglit_width, piglit_height, 0, 0, 1, 1);

	/* check for expected values */
	pass = pass && piglit_probe_rect_rgb(0, 0, piglit_width, piglit_height, green);

	/* show on screen */
	piglit_present_results();
	return pass ? PIGLIT_PASS : PIGLIT_FAIL;
}

void
piglit_init(int argc, char **argv)
{
	glDisable(GL_DITHER);

	glMatrixMode( GL_PROJECTION );
	glPushMatrix();
	glLoadIdentity();
	glOrtho( 0, piglit_width, 0, piglit_height, -1, 1 );

	glMatrixMode( GL_MODELVIEW );
	glPushMatrix();
	glLoadIdentity();
}
