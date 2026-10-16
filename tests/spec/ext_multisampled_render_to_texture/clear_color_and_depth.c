/*
 * Copyright 2026 Valve Corporation
 * SPDX-License-Identifier: MIT
 */

/**
 * Test that we can clear multisampled depth+color
 * Reproducer for https://gitlab.freedesktop.org/mesa/mesa/-/issues/14647
 *
 * On buggy versions of zink, this triggers a vulkan validation error for
 * VUID-VkRenderingInfo-multisampledRenderToSingleSampled-06857
 *
 * The cleared depth is checked by depth testing against it, since reading
 * depth back needs more than OpenGL ES offers.
 */

#include "common.h"

PIGLIT_GL_TEST_CONFIG_BEGIN

	config.supports_gl_es_version = 20;
	config.window_visual = PIGLIT_GL_VISUAL_DOUBLE | PIGLIT_GL_VISUAL_RGBA;
	config.khr_no_error_support = PIGLIT_NO_ERRORS;

PIGLIT_GL_TEST_CONFIG_END

#define WIDTH 100
#define HEIGHT 100
#define SAMPLES 4

static struct msrtt_program prog;

enum piglit_result
piglit_display(void)
{
	const float color[] = {0.0, 1.0, 0.0, 0.5};
	const float blue[] = {0.0, 0.0, 1.0, 1.0};
	const float depth = 0.42;
	glClearColor(0.0, 1.0, 0.0, 0.5);
	glClearDepthf(depth);

	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	/* behind the cleared depth on the left, in front of it on the right */
	glViewport(0, 0, WIDTH, HEIGHT);
	glEnable(GL_DEPTH_TEST);
	glDepthFunc(GL_LESS);
	msrtt_use(&prog, blue, (depth + 0.02) * 2 - 1);
	piglit_draw_rect(-1, -1, 1, 2);
	msrtt_use(&prog, blue, (depth - 0.02) * 2 - 1);
	piglit_draw_rect(0, -1, 1, 2);
	glDisable(GL_DEPTH_TEST);
	glFlush();

	bool pass = true;
	pass &= piglit_probe_rect_rgba(0, 0, WIDTH / 2, HEIGHT, color);
	pass &= piglit_probe_rect_rgba(WIDTH / 2, 0, WIDTH / 2, HEIGHT, blue);

	piglit_present_results();

	return pass ? PIGLIT_PASS : PIGLIT_FAIL;
}

void
piglit_init(int argc, char **argv)
{
	piglit_require_extension("GL_EXT_multisampled_render_to_texture");
	prog = msrtt_color_program();

	GLint samples = MIN2(SAMPLES, msrtt_samples());

	GLuint colorTexture;
	glGenTextures(1, &colorTexture);
	glBindTexture(GL_TEXTURE_2D, colorTexture);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, WIDTH, HEIGHT,
		     0, GL_RGBA, GL_UNSIGNED_BYTE, 0);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);

	GLuint depthRenderbuffer;
	glGenRenderbuffers(1, &depthRenderbuffer);
	glBindRenderbuffer(GL_RENDERBUFFER, depthRenderbuffer);
	/* DEPTH_COMPONENT32F needs OpenGL ES 3.0 */
	msrtt_renderbuffer_storage(GL_RENDERBUFFER, samples,
				   piglit_get_gl_version() >= 30 ?
				   GL_DEPTH_COMPONENT32F : GL_DEPTH_COMPONENT16,
				   WIDTH, HEIGHT);

	GLuint framebuffer;
	glGenFramebuffers(1, &framebuffer);
	glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);

	glFramebufferTexture2DMultisampleEXT(GL_FRAMEBUFFER,
					     GL_COLOR_ATTACHMENT0,
		                             GL_TEXTURE_2D, colorTexture,
					     0, samples);
	glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
				  GL_RENDERBUFFER, depthRenderbuffer);
}
