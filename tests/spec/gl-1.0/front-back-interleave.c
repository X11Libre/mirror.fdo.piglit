/* SPDX-License-Identifier: MIT */

/**
 * Interleave drawing to the front, the back, and both buffers of the window
 * without swapping, and check both buffers after every step and after swaps.
 *
 * An implementation that presents through a swapchain may need an image for
 * the front and another for the back at the same time.
 */

#include "piglit-util-gl.h"

PIGLIT_GL_TEST_CONFIG_BEGIN

	config.supports_gl_compat_version = 10;

	config.window_visual = PIGLIT_GL_VISUAL_RGB | PIGLIT_GL_VISUAL_DOUBLE;
	config.requires_displayed_window = true;
	config.khr_no_error_support = PIGLIT_NO_ERRORS;

PIGLIT_GL_TEST_CONFIG_END

static const float colors[][4] = {
	{ 1.0f, 0.0f, 0.0f, 1.0f },
	{ 0.0f, 1.0f, 0.0f, 1.0f },
	{ 0.0f, 0.0f, 1.0f, 1.0f },
	{ 1.0f, 1.0f, 0.0f, 1.0f },
	{ 0.0f, 1.0f, 1.0f, 1.0f },
};

static void
clear_buffer(GLenum buffer, const float *color)
{
	glDrawBuffer(buffer);
	glClearColor(color[0], color[1], color[2], color[3]);
	glClear(GL_COLOR_BUFFER_BIT);
}

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
	const unsigned n = ARRAY_SIZE(colors);
	bool pass = true;

	for (unsigned i = 0; i < 2 * n; i++) {
		const float *a = colors[i % n];
		const float *b = colors[(i + 1) % n];
		const float *c = colors[(i + 2) % n];

		clear_buffer(GL_FRONT, a);
		clear_buffer(GL_BACK, b);
		pass &= probe(GL_FRONT, a, "front then back", i);
		pass &= probe(GL_BACK, b, "front then back", i);

		clear_buffer(GL_FRONT_AND_BACK, c);
		pass &= probe(GL_FRONT, c, "both", i);
		pass &= probe(GL_BACK, c, "both", i);

		clear_buffer(GL_FRONT, a);
		pass &= probe(GL_FRONT, a, "front after both", i);
		pass &= probe(GL_BACK, c, "front after both", i);

		glDrawBuffer(GL_BACK);
		piglit_swap_buffers();
		pass &= probe(GL_FRONT, c, "after swap", i);
	}

	pass &= piglit_check_gl_error(GL_NO_ERROR);
	return pass ? PIGLIT_PASS : PIGLIT_FAIL;
}

void
piglit_init(int argc, char **argv)
{
}
