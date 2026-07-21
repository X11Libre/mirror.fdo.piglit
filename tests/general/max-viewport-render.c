/*
 * Copyright 2026 Valve Corporation
 *
 * SPDX-License-Identifier: MIT
 */

/**
 * @file max-viewport-render.c
 *
 * Tests that rendering still works when the viewport is set to the maximum
 * size reported by GL_MAX_VIEWPORT_DIMS, with a projection that maps
 * window-pixel coordinates across that full viewport range.
 *
 * This is the exact pattern used by the picom compositor: it sets the viewport
 * to the advertised maximum once (and never resizes it on screen changes),
 * relying on a projection matrix scaled by the viewport size so that vertices
 * given in window-pixel coordinates map to the correct place, with fragments
 * outside the render target simply skipped.
 *
 * radeonsi on gfx12 regressed this (mesa issue #15010): enabling 64K textures
 * raised GL_MAX_VIEWPORT_DIMS to 65536, but a maximum-sized viewport is no
 * longer representable in the rasterizer's 16.8 fixed-point guardband, so the
 * whole scene rendered black.
 */

#include "piglit-util-gl.h"

PIGLIT_GL_TEST_CONFIG_BEGIN

	config.supports_gl_compat_version = 12;
	config.window_width = 256;
	config.window_height = 256;
	config.window_visual = PIGLIT_GL_VISUAL_RGBA | PIGLIT_GL_VISUAL_DOUBLE;
	config.khr_no_error_support = PIGLIT_NO_ERRORS;

PIGLIT_GL_TEST_CONFIG_END

enum piglit_result
piglit_display(void)
{
	static const float green[3] = { 0.0f, 1.0f, 0.0f };
	const int w = piglit_width, h = piglit_height;
	GLint maxdims[2];
	bool pass;

	glGetIntegerv(GL_MAX_VIEWPORT_DIMS, maxdims);
	if (maxdims[0] < w || maxdims[1] < h) {
		printf("GL_MAX_VIEWPORT_DIMS (%d, %d) is smaller than the "
		       "window\n", maxdims[0], maxdims[1]);
		return PIGLIT_SKIP;
	}

	glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT);

	/* Mimic picom: viewport = advertised maximum, and a projection scaled to
	 * that maximum so that vertices given in window-pixel coordinates still
	 * land at the right place. This exercises the driver's viewport/guardband
	 * programming at the largest value it claims to support.
	 */
	glViewport(0, 0, maxdims[0], maxdims[1]);
	glMatrixMode(GL_PROJECTION);
	glLoadIdentity();
	glOrtho(0.0, maxdims[0], 0.0, maxdims[1], -1.0, 1.0);
	glMatrixMode(GL_MODELVIEW);
	glLoadIdentity();

	/* Cover the whole window, addressed in pixel coordinates. */
	glColor3fv(green);
	glBegin(GL_TRIANGLE_FAN);
	glVertex2f(0, 0);
	glVertex2f(w, 0);
	glVertex2f(w, h);
	glVertex2f(0, h);
	glEnd();

	/* On a correct driver the green quad fills the window. When the maximum
	 * viewport can't be rasterized the window stays at the clear color.
	 */
	pass = piglit_probe_rect_rgb(0, 0, w, h, green);

	piglit_present_results();

	return pass ? PIGLIT_PASS : PIGLIT_FAIL;
}

void
piglit_init(int argc, char **argv)
{
	(void)argc;
	(void)argv;
}
