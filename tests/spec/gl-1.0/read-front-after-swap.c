/* SPDX-License-Identifier: MIT */

/**
 * Swap, start drawing the next frame, and read the front buffer while the
 * back buffer is in use. The front buffer must hold the frame that was
 * swapped last, for several frames in a row and for several reads per frame.
 *
 * An implementation that presents through a swapchain has to get the
 * presented image back for this, not any image that is available.
 */

#include "piglit-util-gl.h"

PIGLIT_GL_TEST_CONFIG_BEGIN

	config.supports_gl_compat_version = 10;

	config.window_visual = PIGLIT_GL_VISUAL_RGB | PIGLIT_GL_VISUAL_DOUBLE;
	config.requires_displayed_window = true;
	config.khr_no_error_support = PIGLIT_NO_ERRORS;

PIGLIT_GL_TEST_CONFIG_END

static const float marker[4] = { 0.5f, 0.5f, 0.5f, 1.0f };
static const float colors[][4] = {
	{ 1.0f, 0.0f, 0.0f, 1.0f },
	{ 0.0f, 1.0f, 0.0f, 1.0f },
	{ 0.0f, 0.0f, 1.0f, 1.0f },
	{ 1.0f, 1.0f, 0.0f, 1.0f },
	{ 0.0f, 1.0f, 1.0f, 1.0f },
	{ 1.0f, 0.0f, 1.0f, 1.0f },
};

static bool
probe(GLenum buffer, const float *color, const char *step, unsigned frame)
{
	glReadBuffer(buffer);
	if (piglit_probe_rect_rgb(0, 0, piglit_width, piglit_height, color))
		return true;
	printf("  %s wrong in frame %u, %s\n",
	       piglit_get_gl_enum_name(buffer), frame, step);
	return false;
}

enum piglit_result
piglit_display(void)
{
	bool pass = true;

	piglit_ortho_projection(piglit_width, piglit_height, GL_FALSE);
	glDrawBuffer(GL_BACK);
	for (unsigned i = 0; i < ARRAY_SIZE(colors); i++) {
		glClearColor(colors[i][0], colors[i][1], colors[i][2], colors[i][3]);
		glClear(GL_COLOR_BUFFER_BIT);
		piglit_swap_buffers();

		/* the next frame is under way when the front gets read */
		glColor4fv(marker);
		piglit_draw_rect(0, 0, piglit_width, piglit_height);

		for (unsigned r = 0; r < 4; r++)
			pass &= probe(GL_FRONT, colors[i], "while drawing the back", i);
		pass &= probe(GL_BACK, marker, "after reading the front", i);
	}

	pass &= piglit_check_gl_error(GL_NO_ERROR);
	return pass ? PIGLIT_PASS : PIGLIT_FAIL;
}

void
piglit_init(int argc, char **argv)
{
}
