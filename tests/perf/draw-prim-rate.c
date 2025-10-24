/*
 * Copyright (C) 2018  Advanced Micro Devices, Inc.
 * All Rights Reserved.
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
 * Measure primitive rate under various circumstances.
 *
 * Culling methods:
 * - none
 * - rasterizer discard
 * - face culling
 * - view culling
 * - degenerate primitives
 * - subpixel primitives
 */

#include "common.h"
#include <stdbool.h>
#undef NDEBUG
#include <assert.h>
#include "piglit-util-gl.h"

/* this must be a power of two to prevent precision issues */
#define WINDOW_SIZE 1024

PIGLIT_GL_TEST_CONFIG_BEGIN

	config.supports_gl_compat_version = 10;
	config.window_width = WINDOW_SIZE;
	config.window_height = WINDOW_SIZE;
	config.window_visual = PIGLIT_GL_VISUAL_RGBA | PIGLIT_GL_VISUAL_DOUBLE;

PIGLIT_GL_TEST_CONFIG_END

static unsigned gpu_freq_mhz;
static GLint progs[9];

void
piglit_init(int argc, char **argv)
{
	for (unsigned i = 1; i < argc; i++) {
		if (strncmp(argv[i], "-freq=", 6) == 0)
			sscanf(argv[i] + 6, "%u", &gpu_freq_mhz);
	}

	piglit_require_gl_version(32);

	progs[0] = piglit_build_simple_program(
			  "#version 150 compatibility \n"
			  "void main() { \n"
			  "  gl_Position = gl_Vertex; \n"
			  "}",

			  "#version 150 compatibility \n"
			  "void main() { \n"
			  "  gl_FragColor = vec4(1.0); \n"
			  "}");

	progs[1] = piglit_build_simple_program(
			  "#version 150 compatibility \n"
			  "varying vec4 v[1]; \n"
			  "attribute vec4 a[1]; \n"
			  "void main() { \n"
			  "  for (int i = 0; i < 1; i++) v[i] = a[i]; \n"
			  "  gl_Position = gl_Vertex; \n"
			  "}",

			  "#version 150 compatibility \n"
			  "varying vec4 v[1]; \n"
			  "void main() { \n"
			  "  gl_FragColor = v[0]; \n"
			  "}");

	progs[2] = piglit_build_simple_program(
			  "#version 150 compatibility \n"
			  "varying vec4 v[2]; \n"
			  "attribute vec4 a[2]; \n"
			  "void main() { \n"
			  "  for (int i = 0; i < 2; i++) v[i] = a[i]; \n"
			  "  gl_Position = gl_Vertex; \n"
			  "}",

			  "#version 150 compatibility \n"
			  "varying vec4 v[2]; \n"
			  "void main() { \n"
			  "  gl_FragColor = vec4(dot(v[0] * v[1], vec4(1.0)) == 1.0 ? 1.0 : 0.0); \n"
			  "}");

	progs[3] = piglit_build_simple_program(
			  "#version 150 compatibility \n"
			  "varying vec4 v[3]; \n"
			  "attribute vec4 a[3]; \n"
			  "void main() { \n"
			  "  for (int i = 0; i < 3; i++) v[i] = a[i]; \n"
			  "  gl_Position = gl_Vertex; \n"
			  "}",

			  "#version 150 compatibility \n"
			  "varying vec4 v[3]; \n"
			  "void main() { \n"
			  "  gl_FragColor = vec4(dot(v[0] * v[1] * v[2], vec4(1.0)) == 1.0 ? 1.0 : 0.0); \n"
			  "}");

	progs[4] = piglit_build_simple_program(
			  "#version 150 compatibility \n"
			  "varying vec4 v[4]; \n"
			  "attribute vec4 a[4]; \n"
			  "void main() { \n"
			  "  for (int i = 0; i < 4; i++) v[i] = a[i]; \n"
			  "  gl_Position = gl_Vertex; \n"
			  "}",

			  "#version 150 compatibility \n"
			  "varying vec4 v[4]; \n"
			  "void main() { \n"
			  "  gl_FragColor = vec4(dot(v[0] * v[1] * v[2] * v[3], vec4(1.0)) == 1.0 ? 1.0 : 0.0); \n"
			  "}");

	progs[6] = piglit_build_simple_program(
			  "#version 150 compatibility \n"
			  "varying vec4 v[6]; \n"
			  "attribute vec4 a[6]; \n"
			  "void main() { \n"
			  "  for (int i = 0; i < 6; i++) v[i] = a[i]; \n"
			  "  gl_Position = gl_Vertex; \n"
			  "}",

			  "#version 150 compatibility \n"
			  "varying vec4 v[6]; \n"
			  "void main() { \n"
			  "  gl_FragColor = vec4(dot(v[0] * v[1] * v[2] * v[3] * v[4] * v[5], vec4(1.0)) == 1.0 ? 1.0 : 0.0); \n"
			  "}");

	progs[8] = piglit_build_simple_program(
			  "#version 150 compatibility \n"
			  "varying vec4 v[8]; \n"
			  "attribute vec4 a[8]; \n"
			  "void main() { \n"
			  "  for (int i = 0; i < 8; i++) v[i] = a[i]; \n"
			  "  gl_Position = gl_Vertex; \n"
			  "}",

			  "#version 150 compatibility \n"
			  "varying vec4 v[8]; \n"
			  "void main() { \n"
			  "  gl_FragColor = vec4(dot(v[0] * v[1] * v[2] * v[3] * v[4] * v[5] * v[6] * v[7], vec4(1.0)) == 1.0 ? 1.0 : 0.0); \n"
			  "}");

	glEnableClientState(GL_VERTEX_ARRAY);
	glEnable(GL_CULL_FACE);
	glPrimitiveRestartIndex(UINT32_MAX);
}

static void
gen_triangle_tile(unsigned num_quads_per_dim, double prim_size_in_pixels,
		  unsigned cull_percentage, unsigned vertices_per_prim,
		  bool back_face_culling, bool view_culling, bool degenerate_prims,
		  unsigned max_vertices, unsigned *num_vertices, float *vertices,
		  unsigned max_indices, unsigned *num_indices, unsigned *indices)
{
	/* clip space coordinates in both X and Y directions: */
	const double first = -1;
	const double max_length = 2;
	const double d = prim_size_in_pixels * 2.0 / WINDOW_SIZE;

	assert(d * num_quads_per_dim <= max_length);
	assert(*num_vertices == 0);

	/* the vertex ordering is counter-clockwise */
	for (unsigned ty = 0; ty < num_quads_per_dim; ty++) {
		bool cull;

		if (cull_percentage == 0)
			cull = false;
		else if (cull_percentage == 25)
			cull = ty % 4 == 0;
		else if (cull_percentage == 50)
			cull = ty % 2 == 0;
		else if (cull_percentage == 75)
			cull = ty % 4 != 0;
		else if (cull_percentage == 100)
			cull = true;
		else
			assert(!"wrong cull_percentage");

		for (unsigned tx = 0; tx < num_quads_per_dim; tx++) {
			unsigned x = tx;
			unsigned y = ty;

			/* view culling in different directions */
			double xoffset = 0, yoffset = 0, zoffset = 0;

			if (cull && view_culling) {
				unsigned side = (ty / 2) % 4;

				if (side == 0)		xoffset = -2;
				else if (side == 1)	xoffset =  2;
				else if (side == 2)	yoffset = -2;
				else if (side == 3)	yoffset =  2;
			}

			if (indices) {
				unsigned elem = *num_vertices * 3;

				/* generate horizontal stripes with maximum reuse */
				if (x == 0) {
					*num_vertices += 2;
					assert(*num_vertices <= max_vertices);

					vertices[elem++] = xoffset + first + d * x;
					vertices[elem++] = yoffset + first + d * y;
					vertices[elem++] = zoffset;

					vertices[elem++] = xoffset + first + d * x;
					vertices[elem++] = yoffset + first + d * (y + 1);
					vertices[elem++] = zoffset;
				}

				int base_index = *num_vertices;

				*num_vertices += vertices_per_prim == 2 ? 4 : 2;
				assert(*num_vertices <= max_vertices);

				if (vertices_per_prim == 2) {
					vertices[elem++] = xoffset + first + d * x;
					vertices[elem++] = yoffset + first + d * (y + 1);
					vertices[elem++] = zoffset;
				}

				vertices[elem++] = xoffset + first + d * (x + 1);
				vertices[elem++] = yoffset + first + d * y;
				vertices[elem++] = zoffset;

				if (vertices_per_prim == 2) {
					vertices[elem++] = xoffset + first + d * (x + 1);
					vertices[elem++] = yoffset + first + d * y;
					vertices[elem++] = zoffset;
				}

				vertices[elem++] = xoffset + first + d * (x + 1);
				vertices[elem++] = yoffset + first + d * (y + 1);
				vertices[elem++] = zoffset;

				/* generate indices */
				unsigned idx = *num_indices;
				*num_indices += 6;
				assert(*num_indices <= max_indices);

				if (vertices_per_prim == 2) {
					indices[idx++] = base_index - 2;
					indices[idx++] = base_index + 1;
					indices[idx++] = base_index;

					indices[idx++] = base_index - 1;
					indices[idx++] = base_index + 2;
					indices[idx++] = base_index + 3;
				} else {
					indices[idx++] = base_index - 2;
					indices[idx++] = base_index;
					indices[idx++] = base_index - 1;

					indices[idx++] = base_index - 1;
					indices[idx++] = base_index;
					indices[idx++] = base_index + 1;
				}

				if (cull && back_face_culling) {
					/* switch the winding order */
					unsigned tmp = indices[idx - 6];
					indices[idx - 6] = indices[idx - 5];
					indices[idx - 5] = tmp;

					tmp = indices[idx - 3];
					indices[idx - 3] = indices[idx - 2];
					indices[idx - 2] = tmp;
				}

				if (cull && degenerate_prims) {
					indices[idx - 5] = indices[idx - 4];
					indices[idx - 2] = indices[idx - 1];
				}
			} else {
				unsigned elem = *num_vertices * 3;
				*num_vertices += 6;
				assert(*num_vertices <= max_vertices);

				vertices[elem++] = xoffset + first + d * x;
				vertices[elem++] = yoffset + first + d * y;
				vertices[elem++] = zoffset;

				vertices[elem++] = xoffset + first + d * (x + 1);
				vertices[elem++] = yoffset + first + d * y;
				vertices[elem++] = zoffset;

				vertices[elem++] = xoffset + first + d * x;
				vertices[elem++] = yoffset + first + d * (y + 1);
				vertices[elem++] = zoffset;

				vertices[elem++] = xoffset + first + d * x;
				vertices[elem++] = yoffset + first + d * (y + 1);
				vertices[elem++] = zoffset;

				vertices[elem++] = xoffset + first + d * (x + 1);
				vertices[elem++] = yoffset + first + d * y;
				vertices[elem++] = zoffset;

				vertices[elem++] = xoffset + first + d * (x + 1);
				vertices[elem++] = yoffset + first + d * (y + 1);
				vertices[elem++] = zoffset;

				if (cull && back_face_culling) {
					/* switch the winding order */
					float old[6*3];
					memcpy(old, vertices + elem - 6*3, 6*3*4);

					for (unsigned i = 0; i < 6; i++) {
						vertices[elem - 6*3 + i*3 + 0] = old[(5 - i)*3 + 0];
						vertices[elem - 6*3 + i*3 + 1] = old[(5 - i)*3 + 1];
						vertices[elem - 6*3 + i*3 + 2] = old[(5 - i)*3 + 2];
					}
				}

				if (cull && degenerate_prims) {
					/* use any previously generated vertices */
					unsigned v0 = rand() % *num_vertices;
					unsigned v1 = rand() % *num_vertices;

					memcpy(&vertices[elem - 5*3], &vertices[v0*3], 12);
					memcpy(&vertices[elem - 4*3], &vertices[v0*3], 12);

					memcpy(&vertices[elem - 2*3], &vertices[v1*3], 12);
					memcpy(&vertices[elem - 1*3], &vertices[v1*3], 12);
				}
			}
		}
	}
}

static void
gen_triangle_strip_tile(unsigned num_quads_per_dim, double prim_size_in_pixels,
			unsigned cull_percentage,
			bool back_face_culling, bool view_culling, bool degenerate_prims,
			unsigned max_vertices, unsigned *num_vertices, float *vertices,
			unsigned max_indices, unsigned *num_indices, unsigned *indices)
{
	/* clip space coordinates in both X and Y directions: */
	const double first = -1;
	const double max_length = 2;
	const double d = prim_size_in_pixels * 2.0 / WINDOW_SIZE;

	assert(d * num_quads_per_dim <= max_length);
	assert(*num_vertices == 0);

	/* the vertex ordering is counter-clockwise */
	for (unsigned y = 0; y < num_quads_per_dim; y++) {
		bool cull;

		if (cull_percentage == 0)
			cull = false;
		else if (cull_percentage == 25)
			cull = y % 4 == 0;
		else if (cull_percentage == 50)
			cull = y % 2 == 0;
		else if (cull_percentage == 75)
			cull = y % 4 != 0;
		else if (cull_percentage == 100)
			cull = true;
		else
			assert(!"wrong cull_percentage");

		/* view culling in different directions */
		double xoffset = 0, yoffset = 0, zoffset = 0;

		if (cull && view_culling) {
			unsigned side = (y / 2) % 4;

			if (side == 0)		xoffset = -2;
			else if (side == 1)	xoffset =  2;
			else if (side == 2)	yoffset = -2;
			else if (side == 3)	yoffset =  2;
		}

		if (cull && degenerate_prims) {
			unsigned elem = *num_vertices * 3;
			*num_vertices += 2 + num_quads_per_dim * 2;
			assert(*num_vertices <= max_vertices);

			for (unsigned x = 0; x < 2 + num_quads_per_dim * 2; x++) {
				vertices[elem++] = 0;
				vertices[elem++] = 0;
				vertices[elem++] = 0;
			}
			continue;
		}

		unsigned elem = *num_vertices * 3;
		bool add_degenerates = y > 0;
		*num_vertices += (add_degenerates ? 4 : 0) + 2 + num_quads_per_dim * 2;
		assert(*num_vertices <= max_vertices);

		unsigned x = 0;
		unsigned y0 = y;
		unsigned y1 = y + 1;

		if (cull && back_face_culling) {
			y0 = y + 1;
			y1 = y;
		}

		/* Add degenerated triangles to connect with the previous triangle strip. */
		if (add_degenerates) {
			unsigned base = elem;

			vertices[elem++] = vertices[base - 3];
			vertices[elem++] = vertices[base - 2];
			vertices[elem++] = vertices[base - 1];
		}

		for (unsigned i = 0; i < (add_degenerates ? 4 : 1); i++) {
			vertices[elem++] = xoffset + first + d * x;
			vertices[elem++] = yoffset + first + d * y1;
			vertices[elem++] = zoffset;
		}

		vertices[elem++] = xoffset + first + d * x;
		vertices[elem++] = yoffset + first + d * y0;
		vertices[elem++] = zoffset;

		for (; x < num_quads_per_dim; x++) {
			vertices[elem++] = xoffset + first + d * (x + 1);
			vertices[elem++] = yoffset + first + d * y1;
			vertices[elem++] = zoffset;

			vertices[elem++] = xoffset + first + d * (x + 1);
			vertices[elem++] = yoffset + first + d * y0;
			vertices[elem++] = zoffset;
		}
	}

	if (indices) {
		for (unsigned i = 0; i < *num_vertices; i++)
			indices[i] = i;

		*num_indices = *num_vertices;
	}
}

enum draw_method {
	INDEXED_TRIANGLES,
	INDEXED_TRIANGLES_2VTX, /* every triangle adds 2 new vertices and reuses 1 vertex */
	TRIANGLES,
	TRIANGLE_STRIP,
	INDEXED_TRIANGLE_STRIP,
	INDEXED_TRIANGLE_STRIP_PRIM_RESTART,
	NUM_DRAW_METHODS,
};

static enum draw_method global_draw_method;
static unsigned count;
static unsigned vb_size, ib_size;

static void
run_draw(unsigned iterations)
{
	for (unsigned i = 0; i < iterations; i++) {
		if (global_draw_method == INDEXED_TRIANGLES ||
		    global_draw_method == INDEXED_TRIANGLES_2VTX) {
			glDrawElements(GL_TRIANGLES, count, GL_UNSIGNED_INT, NULL);
		} else if (global_draw_method == TRIANGLES) {
			glDrawArrays(GL_TRIANGLES, 0, count);
		} else if (global_draw_method == TRIANGLE_STRIP) {
			glDrawArrays(GL_TRIANGLE_STRIP, 0, count);
		} else if (global_draw_method == INDEXED_TRIANGLE_STRIP ||
			   global_draw_method == INDEXED_TRIANGLE_STRIP_PRIM_RESTART) {
			glDrawElements(GL_TRIANGLE_STRIP, count, GL_UNSIGNED_INT, NULL);
		}
	}
}

enum cull_method {
	NONE,
	BACK_FACE_CULLING,
	RASTERIZER_DISCARD,
	VIEW_CULLING,
	SUBPIXEL_PRIMS,
	DEGENERATE_PRIMS,
	NUM_CULL_METHODS,
};

enum test_stage {
	INIT,
	RUN,
	RELEASE,
};

struct buffer_data {
	GLuint vb, ib;
	unsigned num_vertices, num_indices;
};

struct test_data {
	struct buffer_data *buffers;
};

struct test_data tests[1200];
uint64_t mem_usage;

static const unsigned num_quads_per_dim_array[] = {
	/* The second number is the approx. number of primitives. */
	ceil(sqrt(0.5 * 2000)),
	ceil(sqrt(0.5 * 8000)),
	ceil(sqrt(0.5 * 32000)),
	ceil(sqrt(0.5 * 128000)),
	ceil(sqrt(0.5 * 512000)),
};

static double quad_sizes_in_pixels[] = {2, 1.0 / 7, 0.25, 0.5};

static unsigned
get_num_prims(unsigned num_quads_index)
{
	return num_quads_per_dim_array[num_quads_index] *
	       num_quads_per_dim_array[num_quads_index] * 2;
}

enum cull_type {
	CULL_TYPE_NONE,
	CULL_TYPE_BACK_FACE,
	CULL_TYPE_VIEW,
	CULL_TYPE_DEGENERATE,
};

union buffer_set_index {
	struct {
		uint16_t draw_method_reduced:3;
		uint16_t cull_type:2;
		uint16_t cull_percentage_div25:3;
		uint16_t num_quads_per_dim_index:3;
		uint16_t quad_size_in_pixels_index:2;
		uint16_t pad:3;
	};
	uint16_t index;
};

static struct buffer_data buffers[8 * 1024];

static union buffer_set_index
get_buffer_set_index(enum draw_method draw_method, enum cull_method cull_method,
		     unsigned num_quads_per_dim_index, unsigned quad_size_in_pixels_index,
		     unsigned cull_percentage)
{
	assert(cull_percentage == 0 || cull_percentage == 25 || cull_percentage == 50 ||
	       cull_percentage == 75 || cull_percentage == 100);
	assert(num_quads_per_dim_index < 5);
	assert(quad_size_in_pixels_index < 4);

	union buffer_set_index set;

	if (draw_method == INDEXED_TRIANGLE_STRIP_PRIM_RESTART)
		set.draw_method_reduced = INDEXED_TRIANGLE_STRIP;
	else
		set.draw_method_reduced = draw_method;

	set.cull_type = cull_method == BACK_FACE_CULLING ? CULL_TYPE_BACK_FACE :
			cull_method == VIEW_CULLING ? CULL_TYPE_VIEW :
			cull_method == DEGENERATE_PRIMS ? CULL_TYPE_DEGENERATE : CULL_TYPE_NONE;
	set.cull_percentage_div25 = cull_percentage / 25;
	set.num_quads_per_dim_index = num_quads_per_dim_index;
	set.quad_size_in_pixels_index = quad_size_in_pixels_index;
	set.pad = 0;
	return set;
}

static void
init_buffers(enum draw_method draw_method, enum cull_method cull_method,
	     unsigned num_quads_per_dim_index, unsigned quad_size_in_pixels_index,
	     unsigned cull_percentage, struct test_data *test)
{
	const unsigned max_indices = 8100000 * 3;
	const unsigned max_vertices = max_indices;
	union buffer_set_index set =
		get_buffer_set_index(draw_method, cull_method, num_quads_per_dim_index,
				     quad_size_in_pixels_index, cull_percentage);

	assert(set.index < ARRAY_SIZE(buffers));
	test->buffers = &buffers[set.index];

	/* If we have already initialized this buffer set, nothing to do. */
	if (test->buffers->vb)
		return;

	assert(set.num_quads_per_dim_index < ARRAY_SIZE(num_quads_per_dim_array));
	unsigned num_quads_per_dim = num_quads_per_dim_array[set.num_quads_per_dim_index];

	assert(set.quad_size_in_pixels_index < ARRAY_SIZE(quad_sizes_in_pixels));
	double quad_size_in_pixels = quad_sizes_in_pixels[set.quad_size_in_pixels_index];

	while (num_quads_per_dim * quad_size_in_pixels >= WINDOW_SIZE)
		quad_size_in_pixels *= 0.5;

	/* Generate vertices. */
	float *vertices = (float*)malloc(max_vertices * 12);
	unsigned *indices = NULL;

	if (set.draw_method_reduced == INDEXED_TRIANGLES ||
	    set.draw_method_reduced == INDEXED_TRIANGLES_2VTX ||
	    set.draw_method_reduced == INDEXED_TRIANGLE_STRIP)
		indices = (unsigned*)malloc(max_indices * 4);

	if (set.draw_method_reduced == TRIANGLE_STRIP ||
	    set.draw_method_reduced == INDEXED_TRIANGLE_STRIP) {
		gen_triangle_strip_tile(num_quads_per_dim, quad_size_in_pixels,
					set.cull_percentage_div25 * 25,
					set.cull_type == CULL_TYPE_BACK_FACE,
					set.cull_type == CULL_TYPE_VIEW,
					set.cull_type == CULL_TYPE_DEGENERATE,
					max_vertices, &test->buffers->num_vertices, vertices,
					max_indices, &test->buffers->num_indices, indices);
	} else {
		gen_triangle_tile(num_quads_per_dim, quad_size_in_pixels,
				  set.cull_percentage_div25 * 25,
				  set.draw_method_reduced == INDEXED_TRIANGLES_2VTX ? 2 : 1,
				  set.cull_type == CULL_TYPE_BACK_FACE,
				  set.cull_type == CULL_TYPE_VIEW,
				  set.cull_type == CULL_TYPE_DEGENERATE,
				  max_vertices, &test->buffers->num_vertices, vertices,
				  max_indices, &test->buffers->num_indices, indices);
	}

	vb_size = test->buffers->num_vertices * 12;
	ib_size = test->buffers->num_indices * 4;

	/* Create buffers. */
	glGenBuffers(1, &test->buffers->vb);
	glBindBuffer(GL_ARRAY_BUFFER, test->buffers->vb);
	glBufferData(GL_ARRAY_BUFFER, vb_size, vertices, GL_STATIC_DRAW);
	mem_usage += vb_size;
	free(vertices);

	if (indices) {
		glGenBuffers(1, &test->buffers->ib);
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, test->buffers->ib);
		glBufferData(GL_ELEMENT_ARRAY_BUFFER, ib_size, indices, GL_STATIC_DRAW);
		mem_usage += ib_size;
		free(indices);
	}
}

static double
run_test(unsigned debug_num_iterations, enum draw_method draw_method,
	 enum cull_method cull_method, struct test_data *test)
{
	/* Test */
	if (cull_method == RASTERIZER_DISCARD)
		glEnable(GL_RASTERIZER_DISCARD);
	if (draw_method == INDEXED_TRIANGLE_STRIP_PRIM_RESTART)
		glEnable(GL_PRIMITIVE_RESTART);

	glBindBuffer(GL_ARRAY_BUFFER, test->buffers->vb);
	glVertexPointer(3, GL_FLOAT, 0, NULL);

	if (test->buffers->ib)
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, test->buffers->ib);

	global_draw_method = draw_method;
	count = test->buffers->ib ? test->buffers->num_indices : test->buffers->num_vertices;

	double rate = 0;

	if (debug_num_iterations)
		run_draw(debug_num_iterations);
	else
		rate = perf_measure_gpu_rate(run_draw, 0.01);

	if (cull_method == RASTERIZER_DISCARD)
		glDisable(GL_RASTERIZER_DISCARD);
	if (draw_method == INDEXED_TRIANGLE_STRIP_PRIM_RESTART)
		glDisable(GL_PRIMITIVE_RESTART);

	return rate;
}

static double
execute_test(unsigned debug_num_iterations, enum draw_method draw_method,
	     enum cull_method cull_method, unsigned num_quads_per_dim_index,
	     unsigned quad_size_in_pixels_index, unsigned cull_percentage,
	     enum test_stage test_stage, struct test_data *test)
{
	if (test_stage == INIT) {
		init_buffers(draw_method, cull_method, num_quads_per_dim_index,
			     quad_size_in_pixels_index, cull_percentage, test);
	}

	if (test_stage == RUN)
		return run_test(debug_num_iterations, draw_method, cull_method, test);

	if (test_stage == RELEASE && test->buffers->vb) {
		glDeleteBuffers(1, &test->buffers->vb);
		if (test->buffers->ib)
			glDeleteBuffers(1, &test->buffers->ib);
		test->buffers->vb = 0;
	}

	return 0;
}

static void
run(enum draw_method draw_method, enum cull_method cull_method, enum test_stage test_stage,
    unsigned *test_index)
{
	static unsigned cull_percentages[] = {100, 75, 50};
	unsigned num_subtests;

	if (cull_method == BACK_FACE_CULLING ||
	    cull_method == VIEW_CULLING) {
		num_subtests = ARRAY_SIZE(cull_percentages);
	} else if (cull_method == SUBPIXEL_PRIMS) {
		num_subtests = 3;
	} else {
		num_subtests = 1;
	}

	for (unsigned subtest = 0; subtest < num_subtests; subtest++) {
		/* 2 is the maximum prim size when everything fits into the window */
		unsigned quad_size_in_pixels_index;
		unsigned cull_percentage;

		if (cull_method == SUBPIXEL_PRIMS) {
			quad_size_in_pixels_index = 1 + subtest;
			cull_percentage = 0;
		} else {
			quad_size_in_pixels_index = 0;
			cull_percentage = cull_percentages[subtest];
		}

		if (test_stage == RUN) {
			printf("  %-14s, ",
			       draw_method == INDEXED_TRIANGLES ? "DrawElems1Vtx" :
			       draw_method == INDEXED_TRIANGLES_2VTX ? "DrawElems2Vtx" :
			       draw_method == TRIANGLES ? "DrawArraysT" :
			       draw_method == TRIANGLE_STRIP ? "DrawArraysTS" :
			       draw_method == INDEXED_TRIANGLE_STRIP ? "DrawElemsTS" :
			       "DrawTS_PrimR");

			if (cull_method == NONE ||
			    cull_method == RASTERIZER_DISCARD) {
				printf("%-21s",
				       cull_method == NONE ? "none" : "rasterizer discard");
			} else if (cull_method == SUBPIXEL_PRIMS) {
				double quad_size_in_pixels = quad_sizes_in_pixels[quad_size_in_pixels_index];
				printf("%2u small prims/pixel ",
				       (unsigned)((1.0 / quad_size_in_pixels) *
						  (1.0 / quad_size_in_pixels) * 2));
			} else {
				printf("%3u%% %-16s", cull_percentage,
				       cull_method == BACK_FACE_CULLING ? "back faces" :
					cull_method == VIEW_CULLING ?	  "culled by view" :
					cull_method == DEGENERATE_PRIMS ? "degenerate prims" :
									  "(error)");
			}
			fflush(stdout);
		}

		for (unsigned prog = 0; prog < ARRAY_SIZE(progs); prog++) {
			if (!progs[prog])
				continue;

			glUseProgram(progs[prog]);

			if (test_stage == RUN && prog)
				printf("   ");

			for (int i = 0; i < ARRAY_SIZE(num_quads_per_dim_array); i++) {
				assert(*test_index < ARRAY_SIZE(tests));
				struct test_data *test = &tests[*test_index];
				(*test_index)++;

				double rate = execute_test(0, draw_method, cull_method, i,
							   quad_size_in_pixels_index, cull_percentage,
							   test_stage, test);

				if (test_stage == RUN) {
					rate *= get_num_prims(i);

					if (gpu_freq_mhz) {
						rate /= gpu_freq_mhz * 1000000.0;
						printf(",%6.3f", rate);
					} else {
						printf(",%6.3f", rate / 1000000000);
					}
					fflush(stdout);
				}
			}
		}
		if (test_stage == RUN)
			printf("\n");
	}
}

static void
iterate_tests(enum test_stage test_stage)
{
	unsigned test_index = 0;

	for (int cull_method = 0; cull_method < RASTERIZER_DISCARD; cull_method++)
		run(INDEXED_TRIANGLES, cull_method, test_stage, &test_index);
	for (int cull_method = 0; cull_method < RASTERIZER_DISCARD; cull_method++)
		run(INDEXED_TRIANGLES_2VTX, cull_method, test_stage, &test_index);

	for (int cull_method = RASTERIZER_DISCARD; cull_method < NUM_CULL_METHODS; cull_method++)
		run(INDEXED_TRIANGLES, cull_method, test_stage, &test_index);

	/* glDrawArrays: Only test NONE and BACK_FACE_CULLING. */
	for (int draw_method = TRIANGLES; draw_method < NUM_DRAW_METHODS; draw_method++) {
		for (int cull_method = 0; cull_method <= BACK_FACE_CULLING; cull_method++)
			run(draw_method, cull_method, test_stage, &test_index);
	}
}

enum piglit_result
piglit_display(void)
{
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	/* for debugging */
	if (getenv("ONE")) {
		struct test_data test = {0};

		glUseProgram(progs[2]);
		init_buffers(INDEXED_TRIANGLES_2VTX, BACK_FACE_CULLING, 4, 0, 50, &test);
		run_test(1, INDEXED_TRIANGLES_2VTX, BACK_FACE_CULLING, &test);
		piglit_swap_buffers();
		return PIGLIT_PASS;
	}

	iterate_tests(INIT);
	printf("GPU memory allocated: %lu MB\n\n", mem_usage >> 20);

	printf("  Measuring %-27s,    ", gpu_freq_mhz ? "Prims/clock," : "GPrims/second,");

	for (unsigned prog = 0; prog < ARRAY_SIZE(progs); prog++) {
		if (progs[prog])
			printf("%u Varyings %27s", prog, " ");
	}
	printf("\n");

	printf("  Draw Call     ,  Cull Method         ");
	for (unsigned prog = 0; prog < ARRAY_SIZE(progs); prog++) {
		if (!progs[prog])
			continue;
		if (prog)
			printf("   ");
		for (int i = 0; i < ARRAY_SIZE(num_quads_per_dim_array); i++)
			printf(",  %3uK", get_num_prims(i) / 1000);
	}
	printf("\n");

	iterate_tests(RUN);
	iterate_tests(RELEASE);

	exit(0);
	return PIGLIT_SKIP;
}
