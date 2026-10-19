/* SPDX-License-Identifier: MIT */

/**
 * @file begin-end-line-loop.c
 * Draw many small line loops with glBegin/glEnd and compare the result
 * against the same loops drawn with glDrawArrays.
 *
 * The total number of vertices is large enough that immediate mode
 * implementations have to flush their vertex buffer in the middle of a
 * line loop.  Each loop lies in its own grid cell, so a wrong vertex count
 * at the end of a loop shows up as a line into a neighboring cell.
 */

#include "piglit-util-gl.h"

PIGLIT_GL_TEST_CONFIG_BEGIN

	config.supports_gl_compat_version = 10;
	config.window_width = 512;
	config.window_height = 512;
	config.window_visual = PIGLIT_GL_VISUAL_RGBA | PIGLIT_GL_VISUAL_DOUBLE;
	config.khr_no_error_support = PIGLIT_NO_ERRORS;

PIGLIT_GL_TEST_CONFIG_END

#define CELL 8
#define SIZE 3
#define VERTS_PER_SIDE 8
#define VERTS_PER_LOOP (4 * VERTS_PER_SIDE)

static float verts[VERTS_PER_LOOP][3];

static void
loop_vertices(int cell)
{
	const int cells_per_row = piglit_width / CELL;
	const float x = (cell % cells_per_row) * CELL + 2.5;
	const float y = (cell / cells_per_row) * CELL + 2.5;

	for (int i = 0; i < VERTS_PER_SIDE; i++) {
		const float t = SIZE * (float)i / VERTS_PER_SIDE;

		verts[i][0] = x + t;
		verts[i][1] = y;
		verts[VERTS_PER_SIDE + i][0] = x + SIZE;
		verts[VERTS_PER_SIDE + i][1] = y + t;
		verts[2 * VERTS_PER_SIDE + i][0] = x + SIZE - t;
		verts[2 * VERTS_PER_SIDE + i][1] = y + SIZE;
		verts[3 * VERTS_PER_SIDE + i][0] = x;
		verts[3 * VERTS_PER_SIDE + i][1] = y + SIZE - t;
	}
	for (int i = 0; i < VERTS_PER_LOOP; i++)
		verts[i][2] = 0;
}

static int
num_cells(void)
{
	return (piglit_width / CELL) * (piglit_height / CELL);
}

static void
draw_arrays(void)
{
	glVertexPointer(3, GL_FLOAT, 0, verts);
	glEnableClientState(GL_VERTEX_ARRAY);

	for (int cell = 0; cell < num_cells(); cell++) {
		loop_vertices(cell);
		glDrawArrays(GL_LINE_LOOP, 0, VERTS_PER_LOOP);
	}

	glDisableClientState(GL_VERTEX_ARRAY);
}

static void
draw_begin_end(void)
{
	for (int cell = 0; cell < num_cells(); cell++) {
		loop_vertices(cell);
		glBegin(GL_LINE_LOOP);
		for (int i = 0; i < VERTS_PER_LOOP; i++) {
			/* A per-vertex color makes the vertices larger, so the
			 * vertex buffer fills up sooner.
			 */
			glColor4f(1, 1, 1, 1);
			glVertex3fv(verts[i]);
		}
		glEnd();
	}
}

void
piglit_init(int argc, char **argv)
{
	piglit_ortho_projection(piglit_width, piglit_height, false);
}

enum piglit_result
piglit_display(void)
{
	const int n = piglit_width * piglit_height * 4;
	float *ref = malloc(n * sizeof(float));
	bool pass;

	glColor4f(1, 1, 1, 1);
	glClear(GL_COLOR_BUFFER_BIT);
	draw_arrays();
	glReadPixels(0, 0, piglit_width, piglit_height, GL_RGBA, GL_FLOAT,
		     ref);

	glClear(GL_COLOR_BUFFER_BIT);
	draw_begin_end();

	pass = piglit_probe_image_rgba(0, 0, piglit_width, piglit_height,
				       ref);

	piglit_present_results();
	free(ref);

	return pass ? PIGLIT_PASS : PIGLIT_FAIL;
}
