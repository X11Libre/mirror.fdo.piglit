/*
 * Copyright 2026 Valve Corporation
 * SPDX-License-Identifier: MIT
 */

#include "piglit-util-gl.h"

PIGLIT_GL_TEST_CONFIG_BEGIN

	config.supports_gl_compat_version = 10;
	config.window_visual = PIGLIT_GL_VISUAL_RGBA | PIGLIT_GL_VISUAL_DOUBLE;

PIGLIT_GL_TEST_CONFIG_END

static const GLfloat green[4] = {0.0, 1.0, 0.0, 1.0};
static const GLfloat black[4] = {0.0, 0.0, 0.0, 1.0};

/*
 * Tiny 8x8 bitmap.
 * Small bitmaps are important because Mesa routes them through the bitmap
 * cache batching path.
 */
static const GLubyte bitmap8x8[8] = {
	0xff,
	0xff,
	0xff,
	0xff,
	0xff,
	0xff,
	0xff,
	0xff,
};

static void
setup_ortho(void)
{
	glMatrixMode(GL_PROJECTION);
	glLoadIdentity();
	glOrtho(0, piglit_width, 0, piglit_height, -1, 1);

	glMatrixMode(GL_MODELVIEW);
	glLoadIdentity();
}

void
piglit_init(int argc, char **argv)
{
	glDisable(GL_DEPTH_TEST);
	glDisable(GL_CULL_FACE);
	glDisable(GL_BLEND);

	glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

	setup_ortho();
}

enum piglit_result
piglit_display(void)
{
	glViewport(0, 0, piglit_width, piglit_height);

	glClearColor(0.0, 0.0, 0.0, 1.0);
	glClear(GL_COLOR_BUFFER_BIT);

	glColor4fv(green);

	/*
	 * ORIGINAL scissor box.
	 * Only bitmap draws within x=[0,32) should survive.
	 */
	glEnable(GL_SCISSOR_TEST);
	glScissor(0, 0, 32, piglit_height);

	/*
	 * Queue several tiny bitmap draws.
	 * Multiple small glBitmap calls are used intentionally to trigger
	 * Mesa's bitmap batching/cache path.
	 */
	for (int i = 0; i < 16; i++) {
		glRasterPos2i(i * 8, 32);
		glBitmap(8, 8, 0.0f, 0.0f, 0.0f, 0.0f, bitmap8x8);
	}

	/*
	 * Change scissor BEFORE flushing.
	 * The bug is that queued bitmap draws inherit THIS scissor instead
	 * of the one active when queued.
	 */
	glScissor(64, 0, 32, piglit_height);

	/* trigger bitmap cache flush */
	glRasterPos2i(0, 0);
	glBitmap(8, 8, 0.0f, 0.0f, 0.0f, 0.0f, bitmap8x8);

	/*
	 * EXPECTED CORRECT RESULT:
	 *
	 * Pixels in the original scissor region should be green.
	 * Pixels in the new scissor region should remain black because the
	 * bitmap draws were submitted before the scissor change.
	 */

	/* Left/original scissor region: should contain rendered bitmap data. */
	piglit_probe_rect_rgba(0, 32, 32, 8, green);

	/* Right/new scissor region: must remain untouched. */
	piglit_probe_rect_rgba(64, 32, 32, 8, black);

	piglit_present_results();

	return piglit_check_gl_error(GL_NO_ERROR)
		? PIGLIT_PASS
		: PIGLIT_FAIL;
}
