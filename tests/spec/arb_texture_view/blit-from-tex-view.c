/*
 * Copyright 2025 Advanced Micro Devices, Inc.
 * SPDX-License-Identifier: MIT
 * Reproducer for https://gitlab.freedesktop.org/mesa/mesa/-/issues/13527
 */

#include "piglit-util-gl.h"
#include "common.h"

PIGLIT_GL_TEST_CONFIG_BEGIN

	config.supports_gl_compat_version = 30;
	config.supports_gl_es_version = 31;
	config.window_visual = PIGLIT_GL_VISUAL_RGBA;
	config.khr_no_error_support = PIGLIT_NO_ERRORS;

PIGLIT_GL_TEST_CONFIG_END

#define TEX_SIZE 8
#define NUM_LAYERS 4
GLuint framebuffers[NUM_LAYERS];
GLuint views[NUM_LAYERS];

enum piglit_result
piglit_display(void)
{
	GLint fbo;
	int w = piglit_width / NUM_LAYERS;
	bool pass = true;

	glClear(GL_COLOR_BUFFER_BIT);
	glEnable(GL_TEXTURE_2D);

	glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &fbo);
	for (int i = 0; i < NUM_LAYERS; ++i) {
		int dx = i * w;

		glBindFramebuffer(GL_READ_FRAMEBUFFER, framebuffers[i]);
		glBlitFramebuffer(
			0, 0, TEX_SIZE, TEX_SIZE,
			dx, 0, dx + w, piglit_height,
			GL_COLOR_BUFFER_BIT, GL_NEAREST);
	}

	glBindFramebuffer(GL_READ_FRAMEBUFFER, fbo);
	for (int i = 0; i < NUM_LAYERS && pass; ++i) {
		float expected[] = {
			Colors[i + 1][0] / 255.0, Colors[i + 1][1] / 255.0, Colors[i + 1][2] / 255.0, Colors[i + 1][3] / 255.0
		};
		int dx = i * w;

		pass = piglit_probe_rect_rgba(dx, 0, w, piglit_height, expected);
	}

	piglit_present_results();
	return pass ? PIGLIT_PASS : PIGLIT_FAIL;
}

void
piglit_init(int argc, char **argv)
{
	GLuint tex;
	int i;

#ifdef PIGLIT_USE_OPENGL
	piglit_require_extension("GL_ARB_texture_view");
#else
	piglit_require_extension("GL_OES_texture_view");
#endif

	glGenTextures(1, &tex);
	glBindTexture(GL_TEXTURE_2D_ARRAY, tex);
	glTexStorage3D(GL_TEXTURE_2D_ARRAY, 1, GL_RGBA8, TEX_SIZE, TEX_SIZE, NUM_LAYERS);

	glGenTextures(NUM_LAYERS, views);
	glCreateFramebuffers(NUM_LAYERS, framebuffers);

	/* Create a view for each layer. */
	for (i = 0; i < NUM_LAYERS; i++) {
		glTextureView(views[i], GL_TEXTURE_2D, tex, GL_RGBA8, 0, 1, i, 1);
		glNamedFramebufferTexture(framebuffers[i], GL_COLOR_ATTACHMENT0, views[i], 0);
	}

	/* Clear each layer to a different color. */
	glClearTexImage(tex, 0, GL_RGBA, GL_UNSIGNED_BYTE, Colors[0]);
	for (i = 0; i < NUM_LAYERS; i++)
		glClearTexImage(views[i], 0, GL_RGBA, GL_UNSIGNED_BYTE, Colors[i + 1]);

	glClearColor(1, 0, 0, 0);

	if (!piglit_check_gl_error(GL_NO_ERROR))
		piglit_report_result(PIGLIT_FAIL);
}
