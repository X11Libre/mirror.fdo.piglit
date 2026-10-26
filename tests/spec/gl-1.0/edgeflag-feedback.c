/*
 * Copyright © 2026 Valve Corporation
 *
 * SPDX-License-Identifier: MIT
 */

/**
 * @file edgeflag-feedback.c
 *
 * Tests that per-vertex edge flags are honoured in feedback mode with
 * polygon mode GL_LINE: each edge whose leading vertex has its edge flag
 * set must be returned as a line token, and every other edge must be
 * missing.
 *
 * The edge flags are given both with an edge flag array and with glEdgeFlag
 * between glBegin and glEnd.
 */

#include "piglit-util-gl.h"

PIGLIT_GL_TEST_CONFIG_BEGIN

	config.supports_gl_compat_version = 10;
	config.window_visual = PIGLIT_GL_VISUAL_DOUBLE | PIGLIT_GL_VISUAL_RGBA;
	config.khr_no_error_support = PIGLIT_NO_ERRORS;

PIGLIT_GL_TEST_CONFIG_END

static const float verts[3][2] = {
	{ 10, 10 },
	{ 50, 10 },
	{ 30, 50 },
};

static const GLboolean flag_patterns[][3] = {
	{ GL_TRUE,  GL_TRUE,  GL_TRUE  },
	{ GL_TRUE,  GL_FALSE, GL_TRUE  },
	{ GL_FALSE, GL_TRUE,  GL_FALSE },
	{ GL_FALSE, GL_FALSE, GL_FALSE },
};

static bool
same_point(const float *a, const float *b)
{
	return a[0] == b[0] && a[1] == b[1];
}

/* Return the index of the triangle edge from verts[i] to verts[(i + 1) % 3]
 * that the line from a to b is, or -1 if it isn't one.
 */
static int
find_edge(const float *a, const float *b)
{
	for (int i = 0; i < 3; i++) {
		if (same_point(a, verts[i]) && same_point(b, verts[(i + 1) % 3]))
			return i;
	}

	return -1;
}

static void
draw_array(const GLboolean *flags)
{
	glVertexPointer(2, GL_FLOAT, 0, verts);
	glEdgeFlagPointer(0, flags);
	glEnableClientState(GL_VERTEX_ARRAY);
	glEnableClientState(GL_EDGE_FLAG_ARRAY);
	glDrawArrays(GL_TRIANGLES, 0, 3);
	glDisableClientState(GL_EDGE_FLAG_ARRAY);
	glDisableClientState(GL_VERTEX_ARRAY);
}

static void
draw_immediate(const GLboolean *flags)
{
	glBegin(GL_TRIANGLES);
	for (int i = 0; i < 3; i++) {
		glEdgeFlag(flags[i]);
		glVertex2fv(verts[i]);
	}
	glEnd();
	glEdgeFlag(GL_TRUE);
}

static bool
test_pattern(void (*draw)(const GLboolean *flags), const GLboolean *flags)
{
	GLfloat buf[64];
	bool seen[3] = { false, false, false };
	bool pass = true;
	int count, i;

	glFeedbackBuffer(ARRAY_SIZE(buf), GL_2D, buf);
	glRenderMode(GL_FEEDBACK);
	draw(flags);
	count = glRenderMode(GL_RENDER);

	if (count < 0) {
		printf("feedback buffer overflowed\n");
		return false;
	}

	for (i = 0; i < count;) {
		int token = (int)buf[i];

		if ((token != GL_LINE_TOKEN && token != GL_LINE_RESET_TOKEN) ||
		    i + 5 > count) {
			printf("unexpected feedback token 0x%x at %d\n",
			       token, i);
			return false;
		}

		int edge = find_edge(&buf[i + 1], &buf[i + 3]);
		if (edge < 0) {
			printf("line (%g, %g) -> (%g, %g) is not an edge "
			       "of the triangle\n",
			       buf[i + 1], buf[i + 2], buf[i + 3], buf[i + 4]);
			pass = false;
		} else if (seen[edge]) {
			printf("edge %d returned twice\n", edge);
			pass = false;
		} else {
			seen[edge] = true;
		}

		i += 5;
	}

	for (i = 0; i < 3; i++) {
		if (seen[i] != (bool)flags[i]) {
			printf("edge flags %d %d %d: edge %d was %s\n",
			       flags[0], flags[1], flags[2], i,
			       seen[i] ? "returned but should be hidden" :
					 "missing");
			pass = false;
		}
	}

	return pass;
}

static enum piglit_result
run_subtest(const char *name, void (*draw)(const GLboolean *flags))
{
	bool pass = true;

	for (unsigned i = 0; i < ARRAY_SIZE(flag_patterns); i++)
		pass = test_pattern(draw, flag_patterns[i]) && pass;

	pass = piglit_check_gl_error(GL_NO_ERROR) && pass;

	enum piglit_result result = pass ? PIGLIT_PASS : PIGLIT_FAIL;
	piglit_report_subtest_result(result, "%s", name);
	return result;
}

enum piglit_result
piglit_display(void)
{
	enum piglit_result result = PIGLIT_PASS;

	piglit_merge_result(&result, run_subtest("array", draw_array));
	piglit_merge_result(&result, run_subtest("immediate", draw_immediate));

	return result;
}

void
piglit_init(int argc, char **argv)
{
	piglit_ortho_projection(piglit_width, piglit_height, false);
	glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
}
