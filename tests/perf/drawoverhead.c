/*
 * Copyright (C) 2009 VMware, Inc.
 * Copyright (C) 2017 Advanced Micro Devices, Inc.
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

#include "common.h"
#include <stdbool.h>
#include "piglit-util-gl.h"

static bool color = true;
static bool is_compat;
static int selected_test_index = -1;
static int duration = 1;

PIGLIT_GL_TEST_CONFIG_BEGIN

	config.supports_gl_compat_version = 0;
	config.supports_gl_core_version = 32;
	for (int i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "-compat")) {
			config.supports_gl_compat_version = 10;
			config.supports_gl_core_version = 0;
			is_compat = true;
		}
		if (!strcmp(argv[i], "-nocolor")) {
			color = false;
		}
		if (!strcmp(argv[i], "-test")) {
			if (i == argc - 1) {
				fprintf(stderr, "-test requires an argument\n");
				exit(1);
			}

			const char *testnum = argv[i + 1];
			char *endptr;
			selected_test_index = strtol(testnum, &endptr, 10);

			if (endptr != argv[i + 1] + strlen(testnum)) {
				fprintf(stderr,
					"Failed to parse test number '%s'\n", testnum);
				exit(1);
			}

			printf("Running only test %d\n", selected_test_index);
			i++;
		}
		if (!strcmp(argv[i], "-duration")) {
			if (i == argc - 1) {
				fprintf(stderr, "-duration requires an argument\n");
				exit(1);
			}

			const char *testnum = argv[i + 1];
			char *endptr;
			duration = strtol(testnum, &endptr, 10);

			printf("Duration forced to %i seconds\n", duration);
			i++;
		}

		if (!strcmp(argv[i], "-help")) {
			fprintf(stderr, "drawoverhead [-compat] [-test TESTNUM] [-nocolor]\n");
			exit(1);
		}
	}
	puts(config.supports_gl_core_version ? "Using Core profile." :
					       "Using Compatibility profile.");

	config.window_visual = PIGLIT_GL_VISUAL_RGBA | PIGLIT_GL_VISUAL_DOUBLE |
                               PIGLIT_GL_VISUAL_DEPTH | PIGLIT_GL_VISUAL_STENCIL;

PIGLIT_GL_TEST_CONFIG_END

static GLuint prog[2], uniform_loc, tex[8], ubo[8], tbo[8], stream, dlist[16];
static bool indexed, is_dlist;
static GLenum enable_enum;

static unsigned num_ubos;
static unsigned num_textures;
static unsigned num_tbos;
static unsigned num_images;
static unsigned num_imgbos;
static unsigned num_vbos;
static unsigned vbo_offset;
static unsigned vbo_stride = 3 * sizeof(float);

/* Small vertex coordinate to generate as small a triangle as possible
 * for the lowest GPU overhead.
 */
#define V1 0.00001

void
piglit_init(int argc, char **argv)
{
	static const unsigned indices[4] = {0, 1, 2, 3};
	GLuint vao, ebo;

	piglit_require_gl_version(30);

	glGenVertexArrays(1, &vao);
	glBindVertexArray(vao);

	glGenBuffers(1, &ebo);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
	glBufferData(GL_ELEMENT_ARRAY_BUFFER,
		     sizeof(indices), indices, GL_STATIC_DRAW);

	if (is_compat) {
		unsigned i = 0;

		/* 0..3: {glVertex}. All display lists should use the same
		 * internal VAO.
		 */
		for (; i < 4; i++) {
			dlist[i] = glGenLists(1);
			glNewList(dlist[i], GL_COMPILE);
			glBegin(GL_TRIANGLES);
			glVertex2f(0, 0);
			glVertex2f(0, V1);
			glVertex2f(V1, 0);
			glEnd();
			glEndList();
		}

		/* 4..7: {glVertex, glColor}. All display lists should use
		 * the same internal VAO.
		 */
		for (; i < 8; i++) {
			dlist[i] = glGenLists(1);
			glNewList(dlist[i], GL_COMPILE);
			glBegin(GL_TRIANGLES);
			glColor3f(1, 1, 1);
			glVertex2f(0, 0);
			glColor3f(1, 1, 1);
			glVertex2f(0, V1);
			glColor3f(1, 1, 1);
			glVertex2f(V1, 0);
			glEnd();
			glEndList();
		}

		/* 8..15: {glVertex} and {glVertex, glColor} created interleaved,
		 * which should result in different internal VAOs in suboptimal
		 * implementations.
		 */
		for (; i < 16; i++) {
			dlist[i] = glGenLists(1);
			glNewList(dlist[i], GL_COMPILE);
			glBegin(GL_TRIANGLES);
			if (i % 2)
				glColor3f(1, 1, 1);
			glVertex2f(0, 0);
			if (i % 2)
				glColor3f(1, 1, 1);
			glVertex2f(0, V1);
			if (i % 2)
				glColor3f(1, 1, 1);
			glVertex2f(V1, 0);
			glEnd();
			glEndList();
		}
	}
}

static void
get_dlist_vs_text(char *s, unsigned prog_index)
{
	sprintf(s,
		"#version 130\n"
		"void main() {\n"
		"	gl_Position = gl_Vertex;\n"
		"	%s\n"
		"}\n",
		prog_index == 1 ? "gl_FrontColor = gl_Color;" : "");
}

static void
get_vs_text(char *s, unsigned num_vbos, unsigned num_tbos, bool is_second)
{
	unsigned i;

	sprintf(s, "#version %u\n", num_tbos ? 140 : 130);
	strcat(s, "#extension GL_ARB_explicit_attrib_location : require\n");
	for (i = 0; i < num_vbos; i++) {
		sprintf(s + strlen(s),
			"layout (location = %u) in vec4 v%u;\n", i, i);
	}
	strcat(s, "void main() {\n"
		  "	gl_Position = vec4(0.0)");
	for (i = 0; i < num_vbos; i++)
		sprintf(s + strlen(s), " + v%u", i);
	if (is_second)
		strcat(s, " + vec4(0.5)");
	strcat(s, ";\n}\n");
}

static void
get_fs_text(char *s, unsigned num_ubos, unsigned num_textures,
	    unsigned num_tbos, unsigned num_images, unsigned num_imgbos,
	    bool is_second)
{
	unsigned i;

	sprintf(s, "#version %u\n", num_tbos ? 140 : 130);
	strcat(s, "#extension GL_ARB_uniform_buffer_object : require\n");
	if (num_images || num_imgbos)
		strcat(s, "#extension GL_ARB_shader_image_load_store : require\n");
	sprintf(s + strlen(s),
		"uniform int index = 0;\n"
		"uniform vec4 u[%u];\n", is_second ? 240 : 1);

	for (i = 0; i < num_textures; i++)
		sprintf(s + strlen(s), "uniform sampler2D s%u;\n", i);
	for (i = 0; i < num_tbos; i++)
		sprintf(s + strlen(s), "uniform samplerBuffer sb%u;\n", i);
	for (i = 0; i < num_images; i++)
		sprintf(s + strlen(s), "layout(rgba8) readonly uniform image2D i%u;\n", i);
	for (i = 0; i < num_imgbos; i++)
		sprintf(s + strlen(s), "layout(rgba8) readonly uniform imageBuffer ib%u;\n", i);
	for (i = 0; i < num_ubos; i++)
		sprintf(s + strlen(s), "uniform ub%u { vec4 ubu%u[10]; };\n", i, i);
	strcat(s, "void main() {\n");
	strcat(s, "	gl_FragData[0] = u[index]");
	for (i = 0; i < num_textures; i++)
		sprintf(s + strlen(s), " + texture(s%u, u[0].xy)", i);
	for (i = 0; i < num_tbos; i++)
		sprintf(s + strlen(s), " + texelFetch(sb%u, int(u[0].x))", i);
	for (i = 0; i < num_images; i++)
		sprintf(s + strlen(s), " + imageLoad(i%u, ivec2(u[0].xy))", i);
	for (i = 0; i < num_imgbos; i++)
		sprintf(s + strlen(s), " + imageLoad(ib%u, int(u[0].x))", i);
	for (i = 0; i < num_ubos; i++)
		sprintf(s + strlen(s), " + ubu%u[index]", i);
	if (is_second)
		strcat(s, " + vec4(0.5)");
	strcat(s, ";\n}\n");
}

static void
setup_shaders_and_resources(unsigned prog_index)
{
	const unsigned max = 16;
	char vs[4096], fs[4096];
	unsigned p, i;

	assert(num_vbos <= max);
	assert(num_ubos <= max);
	assert(num_textures <= max);
	assert(num_tbos <= max);

	for (i = 0; i < max; i++)
		glDisableVertexAttribArray(i);

	/* Create two programs in case we want to test program changes. */
	for (p = 0; p < 2; p++) {
		if (is_dlist) {
			get_dlist_vs_text(vs, p);
			prog[p] = piglit_build_simple_program(vs, NULL);
		} else {
			get_vs_text(vs, num_vbos, num_tbos, p);
			get_fs_text(fs, num_ubos, num_textures, num_tbos, num_images,
				    num_imgbos, p);
			prog[p] = piglit_build_simple_program(vs, fs);
		}

		/* Assign texture units to samplers. */
		glUseProgram(prog[p]);
		for (i = 0; i < num_textures; i++) {
			char sampler[20];
			int loc;

			snprintf(sampler, sizeof(sampler), "s%u", i);
			loc = glGetUniformLocation(prog[p], sampler);
			assert(loc >= 0);
			glUniform1i(loc, i);
		}
		/* Assign texture units to samplers. */
		for (i = 0; i < num_tbos; i++) {
			char sampler[20];
			int loc;

			snprintf(sampler, sizeof(sampler), "sb%u", i);
			loc = glGetUniformLocation(prog[p], sampler);
			assert(loc >= 0);
			glUniform1i(loc, num_textures + i);
		}
		for (i = 0; i < num_images; i++) {
			char name[20];
			int loc;

			snprintf(name, sizeof(name), "i%u", i);
			loc = glGetUniformLocation(prog[p], name);
			assert(loc >= 0);
			glUniform1i(loc, i);
		}
		for (i = 0; i < num_imgbos; i++) {
			char name[20];
			int loc;

			snprintf(name, sizeof(name), "ib%u", i);
			loc = glGetUniformLocation(prog[p], name);
			assert(loc >= 0);
			glUniform1i(loc, num_images + i);
		}
		/* Assign UBO slots to uniform blocks. */
		for (i = 0; i < num_ubos; i++) {
			char block[20];
			int index;

			snprintf(block, sizeof(block), "ub%u", i);
			index = glGetUniformBlockIndex(prog[p], block);
			assert(index != GL_INVALID_INDEX);
			glUniformBlockBinding(prog[p], index, i);
		}
	}
	glUseProgram(prog[prog_index]);

        static const float stream_data[32];

        glGenBuffers(1, &stream);
        glBindBuffer(GL_UNIFORM_BUFFER, stream);
        glBufferData(GL_UNIFORM_BUFFER, sizeof(stream_data), stream_data,
                     GL_DYNAMIC_DRAW);

	for (i = 0; i < num_ubos; i++) {
		static const float data[10*4];
		GLuint ub;

		glGenBuffers(1, &ub);
		glBindBuffer(GL_UNIFORM_BUFFER, ub);
		glBufferData(GL_UNIFORM_BUFFER, sizeof(data), data,
			     GL_STATIC_DRAW);

		glBindBufferBase(GL_UNIFORM_BUFFER, i, ub);
		/* Save the last UBOs for testing UBO changes. */
		ubo[i % 8] = ub;
	}
	/* setup VBO w/ vertex data, we need a different buffer in each attrib */
	for (i = 0; i < num_vbos; i++) {
		/* Vertex positions are all zeroed - we want all primitives
		 * to be culled.
		 */
		static const float vertices[4][5];
		GLuint vbo;

		assert(vbo_offset + 4 * vbo_stride <= sizeof(vertices));

		glGenBuffers(1, &vbo);
		glBindBuffer(GL_ARRAY_BUFFER, vbo);
		glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices,
			     GL_STATIC_DRAW);

		glVertexAttribPointer(i, 2, GL_FLOAT, GL_FALSE, vbo_stride,
				      (void *)(uintptr_t)vbo_offset);
		glEnableVertexAttribArray(i);
	}
	for (i = 0; i < MAX2(num_textures, num_images); i++) {
		glActiveTexture(GL_TEXTURE0 + i);
		/* Save the last texture IDs for testing texture changes. */
		tex[i % 8] = piglit_rgbw_texture(GL_RGBA8, 4, 4, false, true,
						 GL_UNSIGNED_BYTE);
	}
	for (i = 0; i < MAX2(num_tbos, num_imgbos); i++) {
		static const float data[10*4];
		GLuint buf, tb;

		glGenBuffers(1, &buf);
		glBindBuffer(GL_TEXTURE_BUFFER, buf);
		glBufferData(GL_TEXTURE_BUFFER, sizeof(data), data,
			     GL_STATIC_DRAW);

		glActiveTexture(GL_TEXTURE0 + num_textures + i);
		glGenTextures(1, &tb);
		glBindTexture(GL_TEXTURE_BUFFER, tb);
		glTexBuffer(GL_TEXTURE_BUFFER, GL_RGBA8, buf);

		/* Save the last TBOs for testing UBO changes. */
		tbo[i % 8] = tb;
	}
	for (i = 0; i < num_images; i++)
		glBindImageTexture(i, tex[i % 8], 0, false, 0, GL_READ_ONLY, GL_RGBA8);
	for (i = 0; i < num_imgbos; i++)
		glBindImageTexture(i, tbo[i % 8], 0, false, 0, GL_READ_ONLY, GL_RGBA8);

	glActiveTexture(GL_TEXTURE0);
}

static void
calllist_1attrib(unsigned count)
{
	for (unsigned i = 0; i < count; i++)
		glCallList(dlist[i % 4]);
}

static void
calllist_2attribs(unsigned count)
{
	for (unsigned i = 0; i < count; i++)
		glCallList(dlist[4 + i % 4]);
}

static void
calllist_1attrib_mixed_alloc(unsigned count)
{
	for (unsigned i = 0; i < count; i++)
		glCallList(dlist[8 + (i % 4) * 2]);
}

static void
calllist_2attribs_mixed_alloc(unsigned count)
{
	for (unsigned i = 0; i < count; i++)
		glCallList(dlist[9 + (i % 4) * 2]);
}

static void
calllist_mixed_attribs(unsigned count)
{
	for (unsigned i = 0; i < count; i++)
		glCallList(dlist[8 + i % 8]);
}

static void
draw(unsigned count)
{
	unsigned i;
	if (indexed) {
		for (i = 0; i < count; i++)
			glDrawElements(GL_TRIANGLES, 3, GL_UNSIGNED_INT, NULL);
	} else {
		for (i = 0; i < count; i++)
			glDrawArrays(GL_TRIANGLES, 0, 3);
	}
}

static void
draw_shader_change(unsigned count)
{
	unsigned i;
	if (indexed) {
		for (i = 0; i < count; i++) {
			glUseProgram(prog[i & 1]);
			glDrawElements(GL_TRIANGLES, 3, GL_UNSIGNED_INT, NULL);
		}
	} else {
		for (i = 0; i < count; i++) {
			glUseProgram(prog[i & 1]);
			glDrawArrays(GL_TRIANGLES, 0, 3);
		}
	}
	glUseProgram(prog[0]);
}

static void
draw_uniform_change(unsigned count)
{
	unsigned i;
	if (indexed) {
		for (i = 0; i < count; i++) {
			glUniform4f(uniform_loc, i & 1, 0, 0, 0);
			glDrawElements(GL_TRIANGLES, 3, GL_UNSIGNED_INT, NULL);
		}
	} else {
		for (i = 0; i < count; i++) {
			glUniform4f(uniform_loc, i & 1, 0, 0, 0);
			glDrawArrays(GL_TRIANGLES, 0, 3);
		}
	}
}

static void
draw_one_texture_change(unsigned count)
{
	unsigned i;
	if (indexed) {
		for (i = 0; i < count; i++) {
			glBindTexture(GL_TEXTURE_2D, tex[i & 1]);
			glDrawElements(GL_TRIANGLES, 3, GL_UNSIGNED_INT, NULL);
		}
	} else {
		for (i = 0; i < count; i++) {
			glBindTexture(GL_TEXTURE_2D, tex[i & 1]);
			glDrawArrays(GL_TRIANGLES, 0, 3);
		}
	}
}

static void
draw_many_texture_change(unsigned count)
{
	unsigned i,j;
	if (indexed) {
		for (i = 0; i < count; i++) {
			for (j = 0; j < 8; j++) {
				glActiveTexture(GL_TEXTURE0 + j);
				glBindTexture(GL_TEXTURE_2D, tex[(i + j) % 8]);
			}
			glActiveTexture(GL_TEXTURE0);
			glDrawElements(GL_TRIANGLES, 3, GL_UNSIGNED_INT, NULL);
		}
	} else {
		for (i = 0; i < count; i++) {
			for (j = 0; j < 8; j++) {
				glActiveTexture(GL_TEXTURE0 + j);
				glBindTexture(GL_TEXTURE_2D, tex[(i + j) % 8]);
			}
			glActiveTexture(GL_TEXTURE0);
			glDrawArrays(GL_TRIANGLES, 0, 3);
		}
	}
}

static void
draw_one_tbo_change(unsigned count)
{
	unsigned i;
	if (indexed) {
		for (i = 0; i < count; i++) {
			glBindTexture(GL_TEXTURE_BUFFER, tbo[i & 1]);
			glDrawElements(GL_TRIANGLES, 3, GL_UNSIGNED_INT, NULL);
		}
	} else {
		for (i = 0; i < count; i++) {
			glBindTexture(GL_TEXTURE_BUFFER, tbo[i & 1]);
			glDrawArrays(GL_TRIANGLES, 0, 3);
		}
	}
}

static void
draw_many_tbo_change(unsigned count)
{
	unsigned i,j;
	if (indexed) {
		for (i = 0; i < count; i++) {
			for (j = 0; j < 8; j++) {
				glActiveTexture(GL_TEXTURE0 + j);
				glBindTexture(GL_TEXTURE_BUFFER, tbo[(i + j) % 8]);
			}
			glActiveTexture(GL_TEXTURE0);
			glDrawElements(GL_TRIANGLES, 3, GL_UNSIGNED_INT, NULL);
		}
	} else {
		for (i = 0; i < count; i++) {
			for (j = 0; j < 8; j++) {
				glActiveTexture(GL_TEXTURE0 + j);
				glBindTexture(GL_TEXTURE_BUFFER, tbo[(i + j) % 8]);
			}
			glActiveTexture(GL_TEXTURE0);
			glDrawArrays(GL_TRIANGLES, 0, 3);
		}
	}
}

static void
draw_one_img_change(unsigned count)
{
	unsigned i;
	if (indexed) {
		for (i = 0; i < count; i++) {
			glBindImageTexture(0, tex[i & 1], 0, false, 0,
					   GL_READ_ONLY, GL_RGBA8);
			glDrawElements(GL_TRIANGLES, 3, GL_UNSIGNED_INT, NULL);
		}
	} else {
		for (i = 0; i < count; i++) {
			glBindImageTexture(0, tex[i & 1], 0, false, 0,
					   GL_READ_ONLY, GL_RGBA8);
			glDrawArrays(GL_TRIANGLES, 0, 3);
		}
	}
}

static void
draw_many_img_change(unsigned count)
{
	unsigned i,j;
	if (indexed) {
		for (i = 0; i < count; i++) {
			for (j = 0; j < 8; j++)
				glBindImageTexture(j, tex[(i + j) % 8], 0, false,
						   0, GL_READ_ONLY, GL_RGBA8);
			glDrawElements(GL_TRIANGLES, 3, GL_UNSIGNED_INT, NULL);
		}
	} else {
		for (i = 0; i < count; i++) {
			for (j = 0; j < 8; j++)
				glBindImageTexture(j, tex[(i + j) % 8], 0, false,
						   0, GL_READ_ONLY, GL_RGBA8);
			glActiveTexture(GL_TEXTURE0);
			glDrawArrays(GL_TRIANGLES, 0, 3);
		}
	}
}

static void
draw_one_imgbo_change(unsigned count)
{
	unsigned i;
	if (indexed) {
		for (i = 0; i < count; i++) {
			glBindImageTexture(0, tbo[i & 1], 0, false, 0,
					   GL_READ_ONLY, GL_RGBA8);
			glDrawElements(GL_TRIANGLES, 3, GL_UNSIGNED_INT, NULL);
		}
	} else {
		for (i = 0; i < count; i++) {
			glBindImageTexture(0, tbo[i & 1], 0, false, 0,
					   GL_READ_ONLY, GL_RGBA8);
			glDrawArrays(GL_TRIANGLES, 0, 3);
		}
	}
}

static void
draw_many_imgbo_change(unsigned count)
{
	unsigned i,j;
	if (indexed) {
		for (i = 0; i < count; i++) {
			for (j = 0; j < 8; j++)
				glBindImageTexture(j, tbo[(i + j) % 8], 0, false,
						   0, GL_READ_ONLY, GL_RGBA8);
			glActiveTexture(GL_TEXTURE0);
			glDrawElements(GL_TRIANGLES, 3, GL_UNSIGNED_INT, NULL);
		}
	} else {
		for (i = 0; i < count; i++) {
			for (j = 0; j < 8; j++)
				glBindImageTexture(j, tbo[(i + j) % 8], 0, false,
						   0, GL_READ_ONLY, GL_RGBA8);
			glActiveTexture(GL_TEXTURE0);
			glDrawArrays(GL_TRIANGLES, 0, 3);
		}
	}
}

static void
draw_subdata_change(unsigned count)
{
	unsigned i;
	static const float stream_data[32];
	glBindBufferBase(GL_UNIFORM_BUFFER, 0, stream);
	if (indexed) {
		for (i = 0; i < count; i++) {
			glBufferData(GL_UNIFORM_BUFFER, sizeof(stream_data), stream_data,
				     GL_DYNAMIC_DRAW);
			glDrawElements(GL_TRIANGLES, 3, GL_UNSIGNED_INT, NULL);
		}
	} else {
		for (i = 0; i < count; i++) {
			glBufferData(GL_UNIFORM_BUFFER, sizeof(stream_data), stream_data,
				     GL_DYNAMIC_DRAW);
			glDrawArrays(GL_TRIANGLES, 0, 3);
		}
	}
}

static void
draw_one_ubo_change(unsigned count)
{
	unsigned i;
	if (indexed) {
		for (i = 0; i < count; i++) {
			glBindBufferBase(GL_UNIFORM_BUFFER, 0, ubo[i & 1]);
			glDrawElements(GL_TRIANGLES, 3, GL_UNSIGNED_INT, NULL);
		}
	} else {
		for (i = 0; i < count; i++) {
			glBindBufferBase(GL_UNIFORM_BUFFER, 0, ubo[i & 1]);
			glDrawArrays(GL_TRIANGLES, 0, 3);
		}
	}
}

static void
draw_many_ubo_change(unsigned count)
{
	unsigned i,j;
	if (indexed) {
		for (i = 0; i < count; i++) {
			for (j = 0; j < 8; j++)
				glBindBufferBase(GL_UNIFORM_BUFFER, 0, ubo[(i + j) % 8]);
			glDrawElements(GL_TRIANGLES, 3, GL_UNSIGNED_INT, NULL);
		}
	} else {
		for (i = 0; i < count; i++) {
			for (j = 0; j < 8; j++)
				glBindBufferBase(GL_UNIFORM_BUFFER, 0, ubo[(i + j) % 8]);
			glDrawArrays(GL_TRIANGLES, 0, 3);
		}
	}
}

static void
draw_state_change(unsigned count)
{
	unsigned i;
	PFNGLENABLEPROC enable = glEnable;
	PFNGLDISABLEPROC disable = glDisable;
	PFNGLPRIMITIVERESTARTINDEXPROC set_index = glPrimitiveRestartIndex;
	GLenum glenum = enable_enum;

	if (is_compat && enable_enum == GL_PRIMITIVE_RESTART) {
		enable = glEnableClientState;
		disable = glDisableClientState;
		glenum = GL_PRIMITIVE_RESTART_NV;
		set_index = glPrimitiveRestartIndexNV;
	}
	set_index(0xffffffff);

	if (indexed) {
		for (i = 0; i < count; i++) {
			if (i & 1)
				enable(glenum);
			else
				disable(glenum);
			glDrawElements(GL_TRIANGLES, 3, GL_UNSIGNED_INT, NULL);
		}
	} else {
		for (i = 0; i < count; i++) {
			if (i & 1)
				enable(glenum);
			else
				disable(glenum);
			glDrawArrays(GL_TRIANGLES, 0, 3);
		}
	}
	disable(glenum);
}

static void
draw_scissor_change(unsigned count)
{
	unsigned i;
	glEnable(GL_SCISSOR_TEST);
	if (indexed) {
		for (i = 0; i < count; i++) {
			if (i & 1)
				glScissor(0, 0, piglit_width / 2, piglit_height / 2);
			else
				glScissor(0, 0, piglit_width, piglit_height);
			glDrawElements(GL_TRIANGLES, 3, GL_UNSIGNED_INT, NULL);
		}
	} else {
		for (i = 0; i < count; i++) {
			if (i & 1)
				glScissor(0, 0, piglit_width / 2, piglit_height / 2);
			else
				glScissor(0, 0, piglit_width, piglit_height);
			glDrawArrays(GL_TRIANGLES, 0, 3);
		}
	}
	glDisable(GL_SCISSOR_TEST);
}

static void
draw_viewport_change(unsigned count)
{
	unsigned i;
	if (indexed) {
		for (i = 0; i < count; i++) {
			if (i & 1)
				glViewport(0, 0, piglit_width / 2, piglit_height / 2);
			else
				glViewport(0, 0, piglit_width, piglit_height);
			glDrawElements(GL_TRIANGLES, 3, GL_UNSIGNED_INT, NULL);
		}
	} else {
		for (i = 0; i < count; i++) {
			if (i & 1)
				glViewport(0, 0, piglit_width / 2, piglit_height / 2);
			else
				glViewport(0, 0, piglit_width, piglit_height);
			glDrawArrays(GL_TRIANGLES, 0, 3);
		}
	}
	glViewport(0, 0, piglit_width, piglit_height);
}

static void
draw_vertex_attrib_change(unsigned count)
{
	unsigned i;
	if (indexed) {
		for (i = 0; i < count; i++) {
			if (i & 1)
				glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE,
						      3 * sizeof(float), NULL);
			else
				glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE,
						      3 * sizeof(float), NULL);
			glDrawElements(GL_TRIANGLES, 3, GL_UNSIGNED_INT, NULL);
		}
	} else {
		for (i = 0; i < count; i++) {
			if (i & 1)
				glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE,
						      3 * sizeof(float), NULL);
			else
				glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE,
						      3 * sizeof(float), NULL);
			glDrawArrays(GL_TRIANGLES, 0, 3);
		}
	}
}

#define COLOR_RESET	"\033[0m"
#define COLOR_RED	"\033[31m"
#define COLOR_GREEN	"\033[1;32m"
#define COLOR_YELLOW	"\033[1;33m"
#define COLOR_CYAN	"\033[1;36m"

static double
perf_run(const char *call, unsigned prog_index, const char *change,
	 perf_rate_func f, double base_rate)
{
	static unsigned test_index;
	test_index++;

	if (selected_test_index != -1 && test_index != selected_test_index)
		return 0;

	setup_shaders_and_resources(prog_index);

	if (f == draw_uniform_change)
		uniform_loc = glGetUniformLocation(prog[prog_index], "u");

	double rate = perf_measure_cpu_rate(f, duration);
	double ratio = base_rate ? rate / base_rate : 1;

	bool no_change = is_dlist || vbo_offset ||
			 vbo_stride != 3 * sizeof(float);

	const char *ratio_color = base_rate == 0 ? COLOR_RESET :
		ratio > 0.7 ? COLOR_GREEN :
		ratio > 0.4 ? COLOR_YELLOW : COLOR_RED;

	printf(" %3u, %*s (%2u VBO| %u UBO| %2u %s) w/ %s%s%*s"
	       "%s%5u%s, %s%.1f%%%s\n",
	       test_index, -(int)strlen("DrawElements"), call, num_vbos, num_ubos,
	       num_textures ? num_textures :
	         num_tbos ? num_tbos :
	         num_images ? num_images : num_imgbos,
	       num_textures ? "Tex" :
	         num_tbos ? "TBO" :
	         num_images ? "Img" :
	         num_imgbos ? "ImB" : "   ",
	       change,
	       no_change ? "," : " change,",
	       MAX2(26 - (int)strlen(change), 0) + (no_change ? 7 : 0), "",
	       color ? COLOR_CYAN : "",
	       (unsigned)(rate / 1000),
	       color ? COLOR_RESET : "",
	       color ? ratio_color : "",
	       100 * ratio,
	       color ? COLOR_RESET : "");
	return rate;
}

struct enable_state_t {
	GLenum enable;
	const char *name;
};

static struct enable_state_t enable_states[] = {
	{GL_PRIMITIVE_RESTART, "primitive restart enable"},
	{GL_BLEND,	"blend enable"},
	{GL_DEPTH_TEST, "depth enable"},
	{GL_DEPTH_CLAMP, "depth clamp enable"},
	{GL_STENCIL_TEST, "stencil enable"},
	{GL_SCISSOR_TEST, "scissor enable"},
	{GL_MULTISAMPLE, "MSAA enable"},
	{GL_SAMPLE_MASK, "sample mask enable"},
	{GL_SAMPLE_ALPHA_TO_COVERAGE, "alpha-to-coverage enable"},
	{GL_SAMPLE_SHADING, "sample shading enable"},
	{GL_CULL_FACE,	"cull face enable"},
	{GL_CLIP_DISTANCE0, "clip distance enable"},
};

/** Called from test harness/main */
enum piglit_result
piglit_display(void)
{
	double base_rate = 0;

	puts("   #, Test name                                              ,    Thousands draws/s, Difference vs the 1st");

	const char *call = "DrawArrays";
	indexed = false;

	num_vbos = 1;
	perf_run(call, 0, "no state", draw, 0);

	if (is_compat) {
		is_dlist = true;
		call = "glCallList";
		num_vbos = 0;
		base_rate = perf_run(call, 0, "1 attrib", calllist_1attrib, 0);
		perf_run(call, 1, "2 attribs", calllist_2attribs, base_rate);
		perf_run(call, 0, "1 attrib (mixed alloc)", calllist_1attrib_mixed_alloc, base_rate);
		perf_run(call, 1, "2 attribs (mixed alloc)", calllist_2attribs_mixed_alloc, base_rate);
		perf_run(call, 1, "1 and 2 attribs intermixed", calllist_mixed_attribs, base_rate);
		perf_run(call, 1, "1 attrib + 1 zero-stride", calllist_1attrib, base_rate);
		num_vbos = 1;
		is_dlist = false;
	}

	call = "DrawElements";
	indexed = true;

	base_rate = perf_run(call, 0, "no state", draw, 0);

	num_textures = 16;
	num_ubos = 8;
	num_vbos = 16;
	perf_run(call, 0,  "no state", draw, base_rate);

	num_vbos = 1;
	num_ubos = 1;
	num_textures = 1;
	perf_run(call, 0, "buffer replacement", draw_subdata_change, base_rate);

	/* Test state changes. */
	num_ubos = 8;
	num_textures = 8;
	for (num_vbos = 1; num_vbos <= 16; num_vbos *= 16) {
		perf_run(call, 0, num_vbos == 1 ? "1 vertex attrib" : "16 vertex attribs",
			 draw_vertex_attrib_change, base_rate);
	}

	num_vbos = 1;
	num_ubos = 0;
	num_textures = 0;
	perf_run(call, 0, "shader program", draw_shader_change, base_rate);

	num_vbos = 8;
	num_ubos = 8;
	num_textures = 8;
	perf_run(call, 0, "shader program", draw_shader_change, base_rate);

	num_textures = 1;
	perf_run(call, 0, "1/1 texture", draw_one_texture_change, base_rate);
	num_textures = 8;
	perf_run(call, 0, "1/8 texture", draw_one_texture_change, base_rate);
	perf_run(call, 0, "8/8 textures", draw_many_texture_change, base_rate);

	num_textures = 0;

	if (piglit_get_gl_version() >= 31) {
		num_tbos = 1;
		perf_run(call, 0, "1/1 TBO", draw_one_tbo_change, base_rate);
		num_tbos = 8;
		perf_run(call, 0, "1/8 TBO", draw_one_tbo_change, base_rate);
		perf_run(call, 0, "8/8 TBOs", draw_many_tbo_change, base_rate);
		num_tbos = 0;
	}

	if (piglit_is_extension_supported("GL_ARB_shader_image_load_store")) {
		num_images = 1;
		perf_run(call, 0, "1/1 image", draw_one_img_change, base_rate);
		num_images = 8;
		perf_run(call, 0, "1/8 image", draw_one_img_change, base_rate);
		perf_run(call, 0, "8/8 images", draw_many_img_change, base_rate);
		num_images = 0;

		num_imgbos = 1;
		perf_run(call, 0, "1/1 image buffer", draw_one_imgbo_change, base_rate);
		num_imgbos = 8;
		perf_run(call, 0, "1/8 image buffer", draw_one_imgbo_change, base_rate);
		perf_run(call, 0, "8/8 image buffers", draw_many_imgbo_change, base_rate);
		num_imgbos = 0;
	}

	num_textures = 8;
	num_ubos = 1;
	perf_run(call, 0, "1/1 UBO", draw_one_ubo_change, base_rate);
	num_ubos = 8;
	perf_run(call, 0, "1/8 UBO", draw_one_ubo_change, base_rate);
	perf_run(call, 0, "8/8 UBOs", draw_many_ubo_change, base_rate);

	perf_run(call, 0, "few uniforms / 1", draw_uniform_change, base_rate);
	perf_run(call, 1, "many uniforms / 1", draw_uniform_change, base_rate);

	perf_run(call, 0, "scissor", draw_scissor_change, base_rate);
	perf_run(call, 0, "viewport", draw_viewport_change, base_rate);

	for (int state = 0; state < ARRAY_SIZE(enable_states); state++) {
		enable_enum = enable_states[state].enable;
		perf_run(call, 0, enable_states[state].name,
			 draw_state_change, base_rate);
	}

	num_textures = 0;
	num_ubos = 0;
	num_vbos = 1;
	vbo_offset = 1;
	perf_run(call, 0, "VBO offset 1", draw, base_rate);
	vbo_offset = 0;
	vbo_stride = 13;
	perf_run(call, 0, "VBO stride 13", draw, base_rate);
	vbo_stride = 3 * sizeof(float);

	exit(0);
	return PIGLIT_SKIP;
}
