/*
 * Copyright (C) 2009  VMware, Inc.  All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * VMWARE BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN
 * AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

/**
 * Measure simple vertex processing rate via:
 *  - immediate mode
 *  - vertex arrays
 *  - VBO vertex arrays
 *  - glDrawElements
 *  - VBO glDrawElements
 *  - glDrawRangeElements
 *  - VBO glDrawRangeElements
 *
 * Brian Paul
 * 16 Sep 2009
 */

#include <string.h>
#include "common.h"
#include "piglit-util-gl.h"

#define WINDOW_SIZE 100


PIGLIT_GL_TEST_CONFIG_BEGIN

	config.supports_gl_compat_version = 10;
	config.window_width = WINDOW_SIZE;
	config.window_height = WINDOW_SIZE;
	config.window_visual = PIGLIT_GL_VISUAL_RGBA | PIGLIT_GL_VISUAL_DOUBLE;

PIGLIT_GL_TEST_CONFIG_END

#define MAX_VERTS (100 * 100)

/** glVertex2/3/4 size */
#define VERT_SIZE 4

static GLuint VertexBO, ElementBO;

static unsigned NumVerts = MAX_VERTS;
static unsigned VertBytes = VERT_SIZE * sizeof(float);
static float *VertexData = NULL;

static unsigned NumElements = MAX_VERTS;
static GLuint *Elements = NULL;


/**
 * Load VertexData buffer with a 2-D grid of points in the range [-1,1]^2.
 */
static void
InitializeVertexData(void)
{
	unsigned i;
	float x = -1.0, y = -1.0;
	float dx = 2.0 / 100;
	float dy = 2.0 / 100;

	VertexData = (float *) malloc(NumVerts * VertBytes);

	for (i = 0; i < NumVerts; i++) {
		VertexData[i * VERT_SIZE + 0] = x;
		VertexData[i * VERT_SIZE + 1] = y;
		VertexData[i * VERT_SIZE + 2] = 0.0;
		VertexData[i * VERT_SIZE + 3] = 1.0;
		x += dx;
		if (x > 1.0) {
			x = -1.0;
			y += dy;
		}
	}

	Elements = (GLuint *) malloc(NumVerts * sizeof(GLuint));

	for (i = 0; i < NumVerts; i++) {
		Elements[i] = NumVerts - i - 1;
	}
}


void
piglit_init(int argc, char **argv)
{
	InitializeVertexData();

	/* setup VertexBO */
	glGenBuffersARB(1, &VertexBO);
	glBindBufferARB(GL_ARRAY_BUFFER_ARB, VertexBO);
	glBufferDataARB(GL_ARRAY_BUFFER_ARB,
	                NumVerts * VertBytes, VertexData, GL_STATIC_DRAW_ARB);
	glEnableClientState(GL_VERTEX_ARRAY);

	/* setup ElementBO */
	glGenBuffersARB(1, &ElementBO);
	glBindBufferARB(GL_ELEMENT_ARRAY_BUFFER_ARB, ElementBO);
	glBufferDataARB(GL_ELEMENT_ARRAY_BUFFER_ARB,
	                NumElements * sizeof(GLuint), Elements, GL_STATIC_DRAW_ARB);
}


static void
DrawImmediate(unsigned count)
{
	unsigned i;
	glBindBufferARB(GL_ELEMENT_ARRAY_BUFFER, 0);
	glBindBufferARB(GL_ARRAY_BUFFER, 0);
	for (i = 0; i < count; i++) {
		unsigned j;
		glBegin(GL_POINTS);
		for (j = 0; j < NumVerts; j++) {
#if VERT_SIZE == 4
			glVertex4fv(VertexData + j * 4);
#elif VERT_SIZE == 3
			glVertex3fv(VertexData + j * 3);
#elif VERT_SIZE == 2
			glVertex2fv(VertexData + j * 2);
#else
			abort();
#endif
		}
		glEnd();
	}
	glFinish();
	piglit_swap_buffers();
}


static void
DrawArraysMem(unsigned count)
{
	unsigned i;
	glBindBufferARB(GL_ELEMENT_ARRAY_BUFFER, 0);
	glBindBufferARB(GL_ARRAY_BUFFER, 0);
	glVertexPointer(VERT_SIZE, GL_FLOAT, VertBytes, VertexData);
	for (i = 0; i < count; i++) {
		glDrawArrays(GL_POINTS, 0, NumVerts);
	}
	glFinish();
	piglit_swap_buffers();
}


static void
DrawArraysVBO(unsigned count)
{
	unsigned i;
	glBindBufferARB(GL_ELEMENT_ARRAY_BUFFER, 0);
	glBindBufferARB(GL_ARRAY_BUFFER, VertexBO);
	glVertexPointer(VERT_SIZE, GL_FLOAT, VertBytes, (void *) 0);
	for (i = 0; i < count; i++) {
		glDrawArrays(GL_POINTS, 0, NumVerts);
	}
	glFinish();
	piglit_swap_buffers();
}


static void
DrawElementsMem(unsigned count)
{
	unsigned i;
	glBindBufferARB(GL_ELEMENT_ARRAY_BUFFER, 0);
	glBindBufferARB(GL_ARRAY_BUFFER, 0);
	glVertexPointer(VERT_SIZE, GL_FLOAT, VertBytes, VertexData);
	for (i = 0; i < count; i++) {
		glDrawElements(GL_POINTS, NumVerts, GL_UNSIGNED_INT, Elements);
	}
	glFinish();
	piglit_swap_buffers();
}


static void
DrawElementsBO(unsigned count)
{
	unsigned i;
	glBindBufferARB(GL_ELEMENT_ARRAY_BUFFER, ElementBO);
	glBindBufferARB(GL_ARRAY_BUFFER, VertexBO);
	glVertexPointer(VERT_SIZE, GL_FLOAT, VertBytes, (void *) 0);
	for (i = 0; i < count; i++) {
		glDrawElements(GL_POINTS, NumVerts, GL_UNSIGNED_INT, (void *) 0);
	}
	glFinish();
	piglit_swap_buffers();
}


static void
DrawRangeElementsMem(unsigned count)
{
	unsigned i;
	glBindBufferARB(GL_ELEMENT_ARRAY_BUFFER, 0);
	glBindBufferARB(GL_ARRAY_BUFFER, 0);
	glVertexPointer(VERT_SIZE, GL_FLOAT, VertBytes, VertexData);
	for (i = 0; i < count; i++) {
		glDrawRangeElements(GL_POINTS, 0, NumVerts - 1,
		                    NumVerts, GL_UNSIGNED_INT, Elements);
	}
	glFinish();
	piglit_swap_buffers();
}


static void
DrawRangeElementsBO(unsigned count)
{
	unsigned i;
	glBindBufferARB(GL_ELEMENT_ARRAY_BUFFER, ElementBO);
	glBindBufferARB(GL_ARRAY_BUFFER, VertexBO);
	glVertexPointer(VERT_SIZE, GL_FLOAT, VertBytes, (void *) 0);
	for (i = 0; i < count; i++) {
		glDrawRangeElements(GL_POINTS, 0, NumVerts - 1,
		                    NumVerts, GL_UNSIGNED_INT, (void *) 0);
	}
	glFinish();
	piglit_swap_buffers();
}

enum piglit_result
piglit_display(void)
{
	double rate;

	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	printf("Vertex rate (%d x Vertex%df)\n", NumVerts, VERT_SIZE);

	rate = perf_measure_cpu_rate(DrawImmediate, 1.0);
	rate *= NumVerts;
	printf("  Immediate mode: %g verts/sec\n", rate);

	rate = perf_measure_cpu_rate(DrawArraysMem, 1.0);
	rate *= NumVerts;
	printf("  glDrawArrays: %g verts/sec\n", rate);

	rate = perf_measure_cpu_rate(DrawArraysVBO, 1.0);
	rate *= NumVerts;
	printf("  VBO glDrawArrays: %g verts/sec\n", rate);

	rate = perf_measure_cpu_rate(DrawElementsMem, 1.0);
	rate *= NumVerts;
	printf("  glDrawElements: %g verts/sec\n", rate);

	rate = perf_measure_cpu_rate(DrawElementsBO, 1.0);
	rate *= NumVerts;
	printf("  VBO glDrawElements: %g verts/sec\n", rate);

	rate = perf_measure_cpu_rate(DrawRangeElementsMem, 1.0);
	rate *= NumVerts;
	printf("  glDrawRangeElements: %g verts/sec\n", rate);

	rate = perf_measure_cpu_rate(DrawRangeElementsBO, 1.0);
	rate *= NumVerts;
	printf("  VBO glDrawRangeElements: %g verts/sec\n", rate);

	exit(0);
	return PIGLIT_SKIP;
}
