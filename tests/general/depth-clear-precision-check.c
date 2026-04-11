/*
 * Copyright (C) 2019, 2020, 2021, 2022 Intel Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 *
 */

/** @file depth-clear-precision-check.c
 *
 * Tests that glClearDepth store on gpu exactly the transferred values
 * with different depth's values and with different depth formats.
 */

#include "piglit-util-gl.h"

const struct piglit_subtest subtests[];
enum piglit_result test(void *data);
static struct piglit_gl_test_config *piglit_config;

#define ftoh(x) (piglit_float_from_half(piglit_half_from_float(x)))

PIGLIT_GL_TEST_CONFIG_BEGIN

	piglit_config = &config;
	config.subtests = subtests;
	config.supports_gl_compat_version = 10;
	config.window_width = 20;
	config.window_height = 20;
	config.window_visual = PIGLIT_GL_VISUAL_DOUBLE;
	config.khr_no_error_support = PIGLIT_NO_ERRORS;

PIGLIT_GL_TEST_CONFIG_END

const struct piglit_subtest subtests[] = {
	{
		"depth16",
		"depth16",
		test,
		(void *)GL_DEPTH_COMPONENT16
	},
	{
		"depth24",
		"depth24",
		test,
		(void *)GL_DEPTH_COMPONENT24
	},
	{
		"depth32",
		"depth32",
		test,
		(void *)GL_DEPTH_COMPONENT32
	},
	{
		"depth24_stencil8",
		"depth24_stencil8",
		test,
		(void *)GL_DEPTH24_STENCIL8
	},
	{
		"depth32f",
		"depth32f",
		test,
		(void *)GL_DEPTH_COMPONENT32F
	},
	{
		"depth32f_stencil8",
		"depth32f_stencil8",
		test,
		(void *)GL_DEPTH32F_STENCIL8
	},
	{0}};

static GLuint prog;

static const char *vs_source =
	"#version 110 \n"
	"void main() \n"
	"{ \n"
	"   gl_Position = gl_Vertex;\n"
	"} \n";

static const char *fs_source =
	"#version 110 \n"
	"uniform float depth;\n"
	"void main() \n"
	"{ \n"
	"    gl_FragColor = vec4(0.0, 1.0, 0.0, 0.0);\n"
	"    gl_FragDepth = depth;\n"
	"} \n";

/**
 * number of iteration must be below than all possible,
 * because for big format (like 32bit) if testing every value,
 * time of test approaches infinity. iteration_pow controls
 * sampling rate, we test 2^iteration_pow number of values,
 * independent of format and evenly chosen in all possible values
 * for this format
 */
static const uint32_t iteration_pow = 8;

uint32_t
depth_num_of_bits(uint32_t depth)
{
	switch (depth) {
		case GL_DEPTH_COMPONENT16:	return 16;
		case GL_DEPTH_COMPONENT24:	return 24;
		case GL_DEPTH_COMPONENT32:	return 32;
		case GL_DEPTH24_STENCIL8:	return 24;
		case GL_DEPTH_COMPONENT32F:	return 32;
		case GL_DEPTH32F_STENCIL8:	return 32;
		default:			return 8;
	};
}

bool
check_color(float depth)
{
	int depth_location;
	static const GLfloat green[] = {0.0, 1.0, 0.0, 1.0};

	glClearDepth(depth);
	glClearColor(1.0, 0.0, 0.0, 1.0);
	glClear(GL_DEPTH_BUFFER_BIT | GL_COLOR_BUFFER_BIT);

	depth_location = glGetUniformLocation(prog, "depth");
	glUniform1f(depth_location, depth);

	piglit_draw_rect(-1, -1, 2, 2);

	return piglit_probe_pixel_rgb(0, 0, green);
}

enum piglit_result
test(void *data)
{
	GLuint fb;
	GLuint db, cb;
	GLenum depth_format = (uintptr_t)data;

	GLenum status;

	if (depth_format == GL_DEPTH24_STENCIL8)
		if (!piglit_is_extension_supported(
			"GL_EXT_packed_depth_stencil"))
			return PIGLIT_SKIP;

	if (depth_format == GL_DEPTH_COMPONENT32F ||
	    depth_format == GL_DEPTH32F_STENCIL8)
		if (!piglit_is_extension_supported(
			"GL_ARB_depth_buffer_float"))
			return PIGLIT_SKIP;

	glUseProgram(prog);

	/* Create the FBO. */
	glGenRenderbuffers(1, &db);
	glBindRenderbuffer(GL_RENDERBUFFER, db);

	glRenderbufferStorage(GL_RENDERBUFFER, depth_format,
				piglit_width, piglit_height);

	glGenRenderbuffers(1, &cb);
	glBindRenderbuffer(GL_RENDERBUFFER, cb);

	glRenderbufferStorage(GL_RENDERBUFFER, GL_RGBA8,
				piglit_width, piglit_height);

	glBindRenderbuffer(GL_RENDERBUFFER, 0);

	glGenFramebuffers(1, &fb);
	glBindFramebuffer(GL_FRAMEBUFFER, fb);

	glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
				GL_RENDERBUFFER, db);

	glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
				GL_RENDERBUFFER, cb);

	status = glCheckFramebufferStatus(GL_FRAMEBUFFER);

	if (status == GL_FRAMEBUFFER_UNSUPPORTED)
		return PIGLIT_SKIP;

	if (status != GL_FRAMEBUFFER_COMPLETE) {
		printf("FBO incomplete status %s\n",
		piglit_get_gl_enum_name(status));
		return PIGLIT_FAIL;
	}

	bool pass = true;

	glBindFramebuffer(GL_DRAW_FRAMEBUFFER, fb);
	glEnable(GL_DEPTH_TEST);
	glDepthFunc(GL_EQUAL);

	uint32_t numOfBits = depth_num_of_bits(depth_format);
	uint32_t maxStoreableValue = (1l << numOfBits) - 1;
	uint32_t step = 1 << (numOfBits - iteration_pow);

	for (uint64_t depth = 0; depth < maxStoreableValue; depth += step) {
		float fdepth = depth / (float)maxStoreableValue;

		assert(maxStoreableValue * fdepth == depth);

		if (numOfBits < 32)
			fdepth = ftoh(depth / (float)maxStoreableValue);

		pass = check_color(fdepth);

		if (!pass) {
			printf("Testing format: %s.\n"
				"Mismatch between value set with "
				"glClearDepth and set in shader. "
				"Depth: %f\n",
				piglit_get_gl_enum_name(depth_format),
				fdepth);
			break;
		}
	}

	/* Cleanup. */
	glDeleteRenderbuffers(1, &cb);
	glDeleteRenderbuffers(1, &db);
	glDeleteFramebuffers(1, &fb);

	if (!piglit_check_gl_error(GL_NO_ERROR))
		pass = false;

	return pass ? PIGLIT_PASS : PIGLIT_FAIL;
}

enum piglit_result
piglit_display(void)
{
	enum piglit_result pass = PIGLIT_SKIP;
	GLuint fs, vs;

	vs = piglit_compile_shader_text(GL_VERTEX_SHADER, vs_source);
	fs = piglit_compile_shader_text(GL_FRAGMENT_SHADER, fs_source);
	prog = piglit_link_simple_program(vs, fs);

	pass = piglit_run_selected_subtests(piglit_config->subtests,
					piglit_config->selected_subtests,
					piglit_config->num_selected_subtests,
					pass);

	glDeleteProgram(prog);
	glDeleteShader(fs);
	glDeleteShader(vs);

	return pass;
}

void piglit_init(int argc, char **argv)
{
	piglit_require_extension("GL_ARB_framebuffer_object");
}
