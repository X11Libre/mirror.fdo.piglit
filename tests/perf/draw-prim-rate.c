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

static bool has_mesh_shader;
static unsigned gpu_freq_mhz;
static GLint progs[9];
static GLint mesh_progs[9];

#define MAX_MESH_GROUPS 100000
#define MAX_MESH_GROUPS_STR "100000"

void
piglit_init(int argc, char **argv)
{
	for (unsigned i = 1; i < argc; i++) {
		if (strncmp(argv[i], "-freq=", 6) == 0)
			sscanf(argv[i] + 6, "%u", &gpu_freq_mhz);
	}

	piglit_require_gl_version(32);
	has_mesh_shader = piglit_is_extension_supported("GL_EXT_mesh_shader");

	for (unsigned i = 0; i <= 8; i++) {
		if (i == 5 || i == 7)
			continue;

		char vs[1024], fs[1024];

		snprintf(vs, sizeof(vs),
			 "#version 150 compatibility \n"
			 "#define N %u \n"
			 " \n"
			 "#if N > 0 \n"
			 "out vec4 v[N]; \n"
			 "in vec4 a[N]; \n"
			 "#endif \n"
			 " \n"
			 "void main() { \n"
			 "#if N > 0 \n"
			 "  for (int i = 0; i < N; i++) v[i] = a[i]; \n"
			 "#endif \n"
			 "  gl_Position = gl_Vertex; \n"
			 "}", i);

		snprintf(fs, sizeof(fs),
			 "#version 150 compatibility \n"
			 "#define N %u \n"
			 " \n"
			 "#if N > 0 \n"
			 "in vec4 v[N]; \n"
			 "#endif \n"
			 " \n"
			 "void main() { \n"
			 "#if N > 0 \n"
			 "  vec4 mul = vec4(1.0); \n"
			 "  for (int i = 0; i < N; i++) mul *= v[i]; \n"
			 "  gl_FragColor = vec4(dot(mul, vec4(1.0)) == 1.0 ? 1.0 : 0.0); \n"
			 "#else \n"
			 "  gl_FragColor = vec4(1.0); \n"
			 "#endif \n"
			 " \n"
			 "}", i);

		progs[i] = piglit_build_simple_program(vs, fs);

		if (has_mesh_shader) {
			char ms[4096];

			snprintf(ms, sizeof(ms),
				 "#version 450 compatibility \n"
				 "#extension GL_EXT_mesh_shader : enable \n"
				 "#define N %u \n"
				 " \n"
				 "layout(local_size_x = 128, local_size_y = 1, local_size_z = 1) in; \n"
				 "layout(max_vertices = 128, max_primitives = 126, triangles) out; \n"
				 " \n"
				 "layout(binding = 0) uniform usamplerBuffer indices; \n"
				 "layout(binding = 1) uniform samplerBuffer pos; \n"
				 " \n"
				 "layout(binding = 0) uniform GroupInfo { \n"
				 "  uvec4 group_info["MAX_MESH_GROUPS_STR"];\n"
				 "}; \n"
				 " \n"
				 "uniform int zero; \n" /* always zero to load index=0 for varyings */
				 " \n"
				 " \n"
				 "#if N > 0 \n"
				 "layout(binding = 2) uniform samplerBuffer a[N]; \n"
				 "out vec4 v[][N]; \n"
				 "#endif \n"
				 " \n"
				 "void main() { \n"
				 "  uvec4 info = group_info[gl_WorkGroupID.x]; \n"
				 "  uint base_vertex = info.x; \n"
				 "  uint base_prim = info.y; \n"
				 "  uint num_vertices = info.z; \n"
				 "  uint num_primitives = info.w; \n"
				 " \n"
				 "  if (gl_LocalInvocationID.x == 0) \n"
				 "    SetMeshOutputsEXT(num_vertices, num_primitives); \n"
				 " \n"
				 "  uint vertex_id = base_vertex + gl_LocalInvocationID.x; \n"
				 "  uint prim_id = base_prim + gl_LocalInvocationID.x; \n"
				 " \n"
				 "  if (gl_LocalInvocationID.x < num_vertices) { \n"
				 "    gl_MeshVerticesEXT[gl_LocalInvocationID.x].gl_Position = texelFetch(pos, int(vertex_id)); \n"
				 "#if N > 0 \n"
				 "    for (int i = 0; i < N; i++) v[gl_LocalInvocationID.x][i] = texelFetch(a[i], int(vertex_id) * zero); \n"
				 "#endif \n"
				 "  } \n"
				 " \n"
				 "  if (gl_LocalInvocationID.x < num_primitives) \n"
				 "    gl_PrimitiveTriangleIndicesEXT[gl_LocalInvocationID.x] = texelFetch(indices, int(prim_id)).xyz;\n"
				 " \n"
				 "}", i);

			mesh_progs[i] = piglit_build_simple_program_multiple_shaders(
					    GL_MESH_SHADER_EXT, ms,
					    GL_FRAGMENT_SHADER, fs,
					    0);
		}
	}

	glEnableClientState(GL_VERTEX_ARRAY);
	glEnable(GL_CULL_FACE);
	glPrimitiveRestartIndex(UINT32_MAX);
}

static void
gen_triangle_tile(unsigned num_quads_per_dim, double prim_size_in_pixels,
		  unsigned cull_percentage, unsigned vertices_per_prim,
		  bool back_face_culling, bool view_culling, bool degenerate_prims,
		  bool mesh_shader,
		  unsigned max_vertices, unsigned *num_vertices, float *vertices,
		  unsigned max_indices, unsigned *num_indices, unsigned *indices,
		  unsigned max_groups, unsigned *num_groups, unsigned *groups)
{
	/* clip space coordinates in both X and Y directions: */
	const double first = -1;
	const double max_length = 2;
	const double d = prim_size_in_pixels * 2.0 / WINDOW_SIZE;

	unsigned num_vertices_per_group = 0;
	unsigned num_prims_per_group = 0;

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
			cull = ty % 2 == 1;
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
					num_vertices_per_group += 2;
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

				if (mesh_shader) {
					num_vertices_per_group += 2;
					num_prims_per_group += 2;

					unsigned base_vertex = *num_vertices - num_vertices_per_group;

					for (unsigned i = idx - 6; i < idx; i++)
						indices[i] -= base_vertex;

					/* Break the group if the group is full or we are at the end of the strip. */
					if (num_prims_per_group == 126 || tx == num_quads_per_dim - 1) {
						unsigned group_id = (*num_groups);
						assert(group_id < max_groups);

						unsigned base_prim = *num_indices / 3 - num_prims_per_group;

						groups[group_id * 4 + 0] = base_vertex;
						groups[group_id * 4 + 1] = base_prim;
						groups[group_id * 4 + 2] = num_vertices_per_group;
						groups[group_id * 4 + 3] = num_prims_per_group;

						(*num_groups)++;
						num_vertices_per_group = 0;
						num_prims_per_group = 0;

						/* If we are not at the end of the strip, reuse the last 2 vertices
						 * from the previous group as the start of the new group to continue
						 * the strip.
						 */
						if (tx < num_quads_per_dim - 1)
							num_vertices_per_group += 2;
					}
				}
			} else {
				assert(!mesh_shader);
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
	MESH_TRIANGLES,
	NUM_DRAW_METHODS,
};

static enum draw_method global_draw_method;
static unsigned count, num_mesh_groups;

static void
run_draw(unsigned iterations)
{
	if (global_draw_method == INDEXED_TRIANGLES ||
	    global_draw_method == INDEXED_TRIANGLES_2VTX) {
		for (unsigned i = 0; i < iterations; i++)
			glDrawElements(GL_TRIANGLES, count, GL_UNSIGNED_INT, NULL);
	} else if (global_draw_method == TRIANGLES) {
		for (unsigned i = 0; i < iterations; i++)
			glDrawArrays(GL_TRIANGLES, 0, count);
	} else if (global_draw_method == TRIANGLE_STRIP) {
		for (unsigned i = 0; i < iterations; i++)
			glDrawArrays(GL_TRIANGLE_STRIP, 0, count);
	} else if (global_draw_method == INDEXED_TRIANGLE_STRIP ||
		   global_draw_method == INDEXED_TRIANGLE_STRIP_PRIM_RESTART) {
		for (unsigned i = 0; i < iterations; i++)
			glDrawElements(GL_TRIANGLE_STRIP, count, GL_UNSIGNED_INT, NULL);
	} else if (global_draw_method == MESH_TRIANGLES) {
		for (unsigned i = 0; i < iterations; i++)
			glDrawMeshTasksEXT(num_mesh_groups, 1, 1);
	} else {
		assert(!"unhandled draw_method");
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
	REPORT,
};

struct buffer_data {
	GLuint vb, ib, group_buf, single_attrib_buf;
	GLuint vb_tbo, ib_tbo, single_attrib_tbo;
	unsigned num_vertices, num_indices, num_groups;
};

struct test_data {
	struct buffer_data *buffers;
	GLuint query;
};

struct test_data tests[1200 * 2];
uint64_t mem_usage;

static const unsigned prim_counts[] = {
	2000,
	8000,
	32000,
	128000,
	512000,
};

static unsigned
get_num_quads_per_dim(unsigned index)
{
	assert(index < ARRAY_SIZE(prim_counts));

	return sqrt(0.5 * prim_counts[index]);
}

static double quad_sizes_in_pixels[] = {2, 1.0 / 7, 0.25, 0.5};

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
		uint16_t prim_count_index:3;
		uint16_t quad_size_in_pixels_index:2;
		uint16_t pad:3;
	};
	uint16_t index;
};

static struct buffer_data buffers[8 * 1024];

static union buffer_set_index
get_buffer_set_index(enum draw_method draw_method, enum cull_method cull_method,
		     unsigned prim_count_index, unsigned quad_size_in_pixels_index,
		     unsigned cull_percentage)
{
	assert(cull_percentage == 0 || cull_percentage == 25 || cull_percentage == 50 ||
	       cull_percentage == 75 || cull_percentage == 100);
	assert(prim_count_index < ARRAY_SIZE(prim_counts));
	assert(quad_size_in_pixels_index < ARRAY_SIZE(quad_sizes_in_pixels));

	union buffer_set_index set;

	if (draw_method == INDEXED_TRIANGLE_STRIP_PRIM_RESTART)
		set.draw_method_reduced = INDEXED_TRIANGLE_STRIP;
	else
		set.draw_method_reduced = draw_method;

	set.cull_type = cull_method == BACK_FACE_CULLING ? CULL_TYPE_BACK_FACE :
			cull_method == VIEW_CULLING ? CULL_TYPE_VIEW :
			cull_method == DEGENERATE_PRIMS ? CULL_TYPE_DEGENERATE : CULL_TYPE_NONE;
	set.cull_percentage_div25 = cull_percentage / 25;
	set.prim_count_index = prim_count_index;
	set.quad_size_in_pixels_index = quad_size_in_pixels_index;
	set.pad = 0;
	return set;
}

static void
init_buffers(enum draw_method draw_method, enum cull_method cull_method,
	     unsigned prim_count_index, unsigned quad_size_in_pixels_index,
	     unsigned cull_percentage, struct test_data *test)
{
	const unsigned max_indices = 8100000 * 3;
	const unsigned max_vertices = max_indices;
	const unsigned max_groups = MAX_MESH_GROUPS;

	union buffer_set_index set =
		get_buffer_set_index(draw_method, cull_method, prim_count_index,
				     quad_size_in_pixels_index, cull_percentage);

	assert(set.index < ARRAY_SIZE(buffers));
	test->buffers = &buffers[set.index];

	/* If we have already initialized this buffer set, nothing to do. */
	if (test->buffers->vb)
		return;

	unsigned num_quads_per_dim = get_num_quads_per_dim(set.prim_count_index);

	assert(set.quad_size_in_pixels_index < ARRAY_SIZE(quad_sizes_in_pixels));
	double quad_size_in_pixels = quad_sizes_in_pixels[set.quad_size_in_pixels_index];

	while (num_quads_per_dim * quad_size_in_pixels >= WINDOW_SIZE)
		quad_size_in_pixels *= 0.5;

	/* Generate vertices. */
	float *vertices = (float*)malloc(max_vertices * 12);
	unsigned *indices = NULL;
	unsigned *groups = NULL;

	if (set.draw_method_reduced == INDEXED_TRIANGLES ||
	    set.draw_method_reduced == INDEXED_TRIANGLES_2VTX ||
	    set.draw_method_reduced == INDEXED_TRIANGLE_STRIP ||
	    set.draw_method_reduced == MESH_TRIANGLES)
		indices = (unsigned*)malloc(max_indices * 4);

	if (set.draw_method_reduced == MESH_TRIANGLES)
		groups = (unsigned*)malloc(max_groups * 16);

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
				  set.draw_method_reduced == MESH_TRIANGLES,
				  max_vertices, &test->buffers->num_vertices, vertices,
				  max_indices, &test->buffers->num_indices, indices,
				  max_groups, &test->buffers->num_groups, groups);
	}

#if 0 /* Print vertices, indices, and groups for mesh shaders for debugging */
	for (unsigned g = 0; g < test->buffers->num_groups; g++) {
		unsigned base_vertex = groups[g * 4 + 0];
		unsigned base_prim = groups[g * 4 + 1];
		unsigned num_vertices = groups[g * 4 + 2];
		unsigned num_prims = groups[g * 4 + 3];

		printf("group[%u] = %u, %u, %u, %u\n", g, base_vertex, base_prim, num_vertices, num_prims);

		for (unsigned p = 0; p < num_prims; p++) {
			printf("prim[%u] = ", base_prim + p);

			for (unsigned i = 0; i < 3; i++) {
				printf("%u, ", indices[(base_prim + p) * 3 + i]);
			}
			puts("");
		}

		for (unsigned v = 0; v < num_vertices; v++) {
			printf("vertex[%u] = ", base_vertex + v);

			for (unsigned i = 0; i < 3; i++) {
				printf("%f, ", vertices[(base_vertex + v) * 3 + i]);
			}
			puts("");
		}
	}
#endif

	unsigned vb_size = test->buffers->num_vertices * 12;
	unsigned ib_size = test->buffers->num_indices * 4;
	unsigned group_data_size = test->buffers->num_groups * 16;

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

	if (groups) {
		glGenBuffers(1, &test->buffers->group_buf);
		glBindBuffer(GL_ARRAY_BUFFER, test->buffers->group_buf);
		glBufferData(GL_ARRAY_BUFFER, group_data_size, groups, GL_STATIC_DRAW);
		mem_usage += group_data_size;
		free(groups);

		static const float attrib[] = {0, 0, 0, 1};
		glGenBuffers(1, &test->buffers->single_attrib_buf);
		glBindBuffer(GL_ARRAY_BUFFER, test->buffers->single_attrib_buf);
		glBufferData(GL_ARRAY_BUFFER, 16, attrib, GL_STATIC_DRAW);
		mem_usage += 16;
	}

	if (set.draw_method_reduced == MESH_TRIANGLES) {
		assert(indices);

		glGenTextures(1, &test->buffers->vb_tbo);
		glBindTexture(GL_TEXTURE_BUFFER, test->buffers->vb_tbo);
		glTexBuffer(GL_TEXTURE_BUFFER, GL_RGB32F, test->buffers->vb);

		glGenTextures(1, &test->buffers->ib_tbo);
		glBindTexture(GL_TEXTURE_BUFFER, test->buffers->ib_tbo);
		glTexBuffer(GL_TEXTURE_BUFFER, GL_RGB32UI, test->buffers->ib);

		glGenTextures(1, &test->buffers->single_attrib_tbo);
		glBindTexture(GL_TEXTURE_BUFFER, test->buffers->single_attrib_tbo);
		glTexBuffer(GL_TEXTURE_BUFFER, GL_RGBA32F, test->buffers->single_attrib_buf);

		glBindTexture(GL_TEXTURE_BUFFER, 0);
	}
}

#define NUM_ITER 500

static void
run_test(unsigned debug_num_iterations, enum draw_method draw_method,
	 enum cull_method cull_method, struct test_data *test)
{
	/* Test */
	if (cull_method == RASTERIZER_DISCARD)
		glEnable(GL_RASTERIZER_DISCARD);

	global_draw_method = draw_method;

	if (draw_method == MESH_TRIANGLES) {
		glBindBufferBase(GL_UNIFORM_BUFFER, 0, test->buffers->group_buf);

		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_BUFFER, test->buffers->ib_tbo);
		glActiveTexture(GL_TEXTURE1);
		glBindTexture(GL_TEXTURE_BUFFER, test->buffers->vb_tbo);
		for (unsigned i = 0; i < 8; i++) {
			glActiveTexture(GL_TEXTURE2 + i);
			glBindTexture(GL_TEXTURE_BUFFER, test->buffers->single_attrib_tbo);
		}

		glActiveTexture(GL_TEXTURE0);

		num_mesh_groups = test->buffers->num_groups;
	} else {
		if (draw_method == INDEXED_TRIANGLE_STRIP_PRIM_RESTART)
			glEnable(GL_PRIMITIVE_RESTART);

		glBindBuffer(GL_ARRAY_BUFFER, test->buffers->vb);
		glVertexPointer(3, GL_FLOAT, 0, NULL);

		if (test->buffers->ib)
			glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, test->buffers->ib);

		count = test->buffers->ib ? test->buffers->num_indices : test->buffers->num_vertices;
	}

	if (debug_num_iterations)
		run_draw(debug_num_iterations);
	else
		test->query = perf_measure_gpu_time_get_query(run_draw, NUM_ITER);

	if (draw_method != MESH_TRIANGLES) {
		if (draw_method == INDEXED_TRIANGLE_STRIP_PRIM_RESTART)
			glDisable(GL_PRIMITIVE_RESTART);
	}

	if (cull_method == RASTERIZER_DISCARD)
		glDisable(GL_RASTERIZER_DISCARD);
}

static void
run(enum draw_method draw_method, enum cull_method cull_method, enum test_stage test_stage,
    unsigned *test_index)
{
	static unsigned cull_percentages[] = {100, 75, 50};
	unsigned num_subtests;
	GLint *shader_progs = draw_method == MESH_TRIANGLES ? mesh_progs : progs;

	if (draw_method == MESH_TRIANGLES && !has_mesh_shader)
		return;

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

		switch (test_stage) {
		case INIT:
		    for (unsigned prog = 0; prog < ARRAY_SIZE(progs); prog++) {
			if (shader_progs[prog]) {
			    for (int i = 0; i < ARRAY_SIZE(prim_counts); i++) {
				assert(*test_index < ARRAY_SIZE(tests));
				struct test_data *test = &tests[(*test_index)++];

				init_buffers(draw_method, cull_method, i,
					     quad_size_in_pixels_index, cull_percentage, test);
			    }
			}
		    }
		    break;

		case RUN:
		    for (unsigned prog = 0; prog < ARRAY_SIZE(progs); prog++) {
			    if (shader_progs[prog]) {
				glUseProgram(shader_progs[prog]);

				for (int i = 0; i < ARRAY_SIZE(prim_counts); i++) {
				    assert(*test_index < ARRAY_SIZE(tests));
				    struct test_data *test = &tests[(*test_index)++];

				    run_test(0, draw_method, cull_method, test);
				}
			    }
		    }
		    break;

		case REPORT:
		    printf("  %-14s, ",
			   draw_method == INDEXED_TRIANGLES ? "DrawElems1Vtx" :
			   draw_method == INDEXED_TRIANGLES_2VTX ? "DrawElems2Vtx" :
			   draw_method == TRIANGLES ? "DrawArraysT" :
			   draw_method == TRIANGLE_STRIP ? "DrawArraysTS" :
			   draw_method == INDEXED_TRIANGLE_STRIP ? "DrawElemsTS" :
			   draw_method == INDEXED_TRIANGLE_STRIP_PRIM_RESTART ? "DrawTS_PrimR" :
			   draw_method == MESH_TRIANGLES ? "DrawMesh" : "invalid");

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

		    for (unsigned prog = 0; prog < ARRAY_SIZE(progs); prog++) {
			    if (shader_progs[prog]) {
				if (prog > 0)
				    printf("   ");

				for (int i = 0; i < ARRAY_SIZE(prim_counts); i++) {
				    assert(*test_index < ARRAY_SIZE(tests));
				    struct test_data *test = &tests[(*test_index)++];

				    if (test->buffers->vb) {
					glDeleteBuffers(1, &test->buffers->vb);
					if (test->buffers->ib)
					    glDeleteBuffers(1, &test->buffers->ib);
					test->buffers->vb = 0;
				    }

				    double rate = perf_get_throughput_from_query(test->query, NUM_ITER);
				    rate *= prim_counts[i];

				    if (gpu_freq_mhz) {
					rate /= gpu_freq_mhz * 1000000.0;
					printf(",%6.3f", rate);
				    } else {
					printf(",%6.3f", rate / 1000000000);
				    }
				}
			    }
		    }

		    printf("\n");
		    break;
		}
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

		glUseProgram(progs[1]);
		init_buffers(INDEXED_TRIANGLES_2VTX, BACK_FACE_CULLING, 4, 0, 50, &test);
		run_test(1, INDEXED_TRIANGLES_2VTX, BACK_FACE_CULLING, &test);
		piglit_swap_buffers();
		return PIGLIT_PASS;
	}

	if (getenv("MONE")) {
		struct test_data test = {0};

		glUseProgram(mesh_progs[1]);
		init_buffers(MESH_TRIANGLES, BACK_FACE_CULLING, 4, 0, 50, &test);
		run_test(1, MESH_TRIANGLES, BACK_FACE_CULLING, &test);
		piglit_swap_buffers();
		return PIGLIT_PASS;
	}

	puts("(AMD: use 'umr --gui' and set Power to profile_peak)\n");
	iterate_tests(INIT);

	printf("GPU memory allocated: %u MB\n\n", (unsigned)(mem_usage >> 20));
	puts("Measuring performance. This may take a while...\n");

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
		for (int i = 0; i < ARRAY_SIZE(prim_counts); i++)
			printf(",  %3uK", prim_counts[i] / 1000);
	}
	printf("\n");

	iterate_tests(RUN);
	iterate_tests(REPORT);

	exit(0);
	return PIGLIT_SKIP;
}
