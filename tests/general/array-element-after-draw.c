/*
 * Copyright © 2026 Timothy Arceri
 * SPDX-License-Identifier: MIT
 */

/**
 * @file array-element-after-draw.c
 *
 * Test that reading vertices from client arrays one element at a time
 * (glArrayElement, or glDrawArrays and glDrawElements while compiling a
 * display list) works after drawing from the same arrays.
 *
 * This tests a Mesa bug where glthread uploaded the vertices that
 * glDrawElements used to a buffer, left that buffer bound in place of the
 * client arrays, and glArrayElement then read the wrong vertices from it.
 */

#include "piglit-util-gl.h"

PIGLIT_GL_TEST_CONFIG_BEGIN

	config.supports_gl_compat_version = 11;
	config.window_visual = PIGLIT_GL_VISUAL_RGB | PIGLIT_GL_VISUAL_DOUBLE;
	config.khr_no_error_support = PIGLIT_NO_ERRORS;

PIGLIT_GL_TEST_CONFIG_END

/* Vertices 0-3 are the left half of the window in green and vertices 4-7
 * are the right half in blue.
 */
static const float pos[8][2] = {
	{-1, -1}, {0, -1}, {0, 1}, {-1, 1},
	{ 0, -1}, {1, -1}, {1, 1}, { 0, 1},
};

static const float color[8][3] = {
	{0, 1, 0}, {0, 1, 0}, {0, 1, 0}, {0, 1, 0},
	{0, 0, 1}, {0, 0, 1}, {0, 0, 1}, {0, 0, 1},
};

static const GLuint left_indices[] = {0, 1, 2, 3};
static const GLuint right_indices[] = {4, 5, 6, 7};

enum left_mode {
	ARRAY_ELEMENT,
	DLIST_DRAW_ARRAYS,
	DLIST_DRAW_ELEMENTS,
};

static const struct {
	const char *name;
	enum left_mode mode;
} subtests[] = {
	{"glArrayElement", ARRAY_ELEMENT},
	{"display list glDrawArrays", DLIST_DRAW_ARRAYS},
	{"display list glDrawElements", DLIST_DRAW_ELEMENTS},
};

static bool
run_subtest(enum left_mode mode)
{
	static const float green[] = {0, 1, 0};
	static const float blue[] = {0, 0, 1};
	GLuint list;
	bool pass;
	int i;

	glClearColor(1, 0, 0, 1);
	glClear(GL_COLOR_BUFFER_BIT);

	/* Draw the right half from the arrays. It doesn't use the vertices
	 * of the left half.
	 */
	glDrawElements(GL_QUADS, 4, GL_UNSIGNED_INT, right_indices);

	/* Draw the left half one element at a time. */
	switch (mode) {
	case ARRAY_ELEMENT:
		glBegin(GL_QUADS);
		for (i = 0; i < 4; i++)
			glArrayElement(i);
		glEnd();
		break;
	case DLIST_DRAW_ARRAYS:
	case DLIST_DRAW_ELEMENTS:
		list = glGenLists(1);
		glNewList(list, GL_COMPILE);
		if (mode == DLIST_DRAW_ARRAYS) {
			glDrawArrays(GL_QUADS, 0, 4);
		} else {
			glDrawElements(GL_QUADS, 4, GL_UNSIGNED_INT,
				       left_indices);
		}
		glEndList();
		glCallList(list);
		glDeleteLists(list, 1);
		break;
	}

	pass = piglit_probe_rect_rgb(0, 0, piglit_width / 2, piglit_height,
				     green);
	pass = piglit_probe_rect_rgb(piglit_width / 2, 0, piglit_width / 2,
				     piglit_height, blue) && pass;

	piglit_present_results();
	return pass;
}

enum piglit_result
piglit_display(void)
{
	enum piglit_result result = PIGLIT_PASS;
	int i;

	for (i = 0; i < ARRAY_SIZE(subtests); i++) {
		bool pass = run_subtest(subtests[i].mode);

		piglit_report_subtest_result(pass ? PIGLIT_PASS : PIGLIT_FAIL,
					     "%s", subtests[i].name);
		if (!pass)
			result = PIGLIT_FAIL;
	}

	return result;
}

void
piglit_init(int argc, char **argv)
{
	glEnableClientState(GL_VERTEX_ARRAY);
	glEnableClientState(GL_COLOR_ARRAY);
	glVertexPointer(2, GL_FLOAT, 0, pos);
	glColorPointer(3, GL_FLOAT, 0, color);
}
