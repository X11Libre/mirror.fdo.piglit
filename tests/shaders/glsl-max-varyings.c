/*
 * Copyright © 2010 Intel Corporation
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
 * Authors:
 *    Eric Anholt <eric@anholt.net>
 *
 */

/** @file glsl-max-varyings.c
 *
 * Tests whether each varying can be used at all numbers of varyings up to
 * GL_MAX_VARYING_FLOATS / 4.
 */

#include "piglit-util-gl.h"

#define MAX_VARYING 256

/* 1x1 rectangles with 1 pixels of pad.  Deal with up to 256 varyings. */

PIGLIT_GL_TEST_CONFIG_BEGIN

	config.supports_gl_compat_version = 10;

	config.window_width = MAX_VARYING;
	config.window_height = MAX_VARYING;
	config.window_visual = PIGLIT_GL_VISUAL_RGB | PIGLIT_GL_VISUAL_DOUBLE;

PIGLIT_GL_TEST_CONFIG_END

static bool exceed_limits = false;
static int max_varyings;

/* Generate a VS that writes to num_varyings vec4s, and put
 * interesting data in data_varying with 0.0 everywhere else.
 */
static GLint get_vs(int num_varyings)
{
	GLuint shader;
	unsigned i;
	char *code = malloc(4096);
	char temp[2048];

	code[0] = 0;
	for (i = 0; i < num_varyings; i++) {
		sprintf(temp, "varying vec4 v%d;\n", i);
		strcat(code, temp);
	}

	sprintf(temp,
		"attribute vec4 vertex;\n"
		"attribute vec4 green;\n"
		"attribute vec4 red;\n"
		"uniform int data_varying;\n"
		"void main()\n"
		"{\n"
		"	gl_Position = (gl_ModelViewProjectionMatrix * \n"
		"			vertex);\n");
	strcat(code, temp);

	for (i = 0; i < num_varyings; i++) {
		sprintf(temp, "	v%d = (data_varying == %d) ? green : red;\n", i, i);
		strcat(code, temp);
	}

	sprintf(temp,
		"}\n"
		);
	strcat(code, temp);

	shader = piglit_compile_shader_text(GL_VERTEX_SHADER, code);
	/* printf("%s\n", code); */

	free(code);
	return shader;
}

/* Generate a FS that does operations on num_varyings, yet makes only
 * data_varying contribute to the output.
 *
 * We could make a single FS per num_varyings that did this by using a
 * uniform for data_varying and some multiplication by comparisons
 * (see glsl-routing for an example), but since we're linking a new
 * shader each time anyway, this produces a simpler FS to read and
 * verify.
 */
static GLint get_fs(int num_varyings)
{
	GLuint shader;
	unsigned i;
	char *code = malloc(8192);
	char temp[2048];

	code[0] = 0;
	for (i = 0; i < num_varyings; i++) {
		sprintf(temp, "varying vec4 v%d;\n", i);
		strcat(code, temp);
	}

	sprintf(temp,
		"uniform float contribution[%d];\n"
		"void main()\n"
		"{\n"
		"	vec4 val = vec4(0.0);\n",
		num_varyings);
	strcat(code, temp);

	for (i = 0; i < num_varyings; i++) {
		sprintf(temp, "	val += contribution[%d] * v%d;\n", i, i);
		strcat(code, temp);
	}

	sprintf(temp,
		"	gl_FragColor = val;\n"
		"}\n"
		);
	strcat(code, temp);

	/* printf("%s\n", code); */
	shader = piglit_compile_shader_text(GL_FRAGMENT_SHADER, code);

	free(code);
	return shader;
}

static bool
draw(int num_varyings)
{
	int data_varying;
	float vertex[4][4] = { {0.0, 0.0, 0.0, 1.0},
			       {0.0, 0.0, 0.0, 1.0},
			       {0.0, 0.0, 0.0, 1.0},
			       {0.0, 0.0, 0.0, 1.0} };
	float green[4][4] = { {0.0, 1.0, 0.0, 0.0},
			      {0.0, 1.0, 0.0, 0.0},
			      {0.0, 1.0, 0.0, 0.0},
			      {0.0, 1.0, 0.0, 0.0} };
	float red[4][4] = { {1.0, 0.0, 0.0, 0.0},
			    {1.0, 0.0, 0.0, 0.0},
			    {1.0, 0.0, 0.0, 0.0},
			    {1.0, 0.0, 0.0, 0.0} };

	glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, 4 * sizeof(float),
			      vertex);
	glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, 4 * sizeof(float),
			      green);
	glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, 4 * sizeof(float),
			      red);
	glEnableVertexAttribArray(0);
	glEnableVertexAttribArray(1);
	glEnableVertexAttribArray(2);

	GLuint vs = get_vs(num_varyings);
	GLuint fs = get_fs(num_varyings);
	GLuint prog = glCreateProgram();
	glAttachShader(prog, vs);
	glAttachShader(prog, fs);

	glBindAttribLocation(prog, 0, "vertex");
	glBindAttribLocation(prog, 1, "green");
	glBindAttribLocation(prog, 2, "red");

	glLinkProgram(prog);
	if (!piglit_link_check_status_quiet(prog)) {
		if (num_varyings > max_varyings) {
			printf("Failed to link with %d out of %d "
				"varyings used\n",
				num_varyings, max_varyings);
			return false;
		} else {
			piglit_report_result(PIGLIT_FAIL);
		}
	}

	int data_varying_loc = glGetUniformLocation(prog, "data_varying");
	int contribution_loc = glGetUniformLocation(prog, "contribution");

	glUseProgram(prog);

	for (data_varying = 0; data_varying < num_varyings; data_varying++) {
		float x, y;

		glUniform1i(data_varying_loc, data_varying);
		glUniform1f(contribution_loc + data_varying, 1.0);
		if (data_varying > 0)
			glUniform1f(contribution_loc + data_varying - 1, 0.0);

		x = data_varying;
		y = num_varyings - 1;
		vertex[0][0] = x;
		vertex[0][1] = y;
		vertex[1][0] = x + 1;
		vertex[1][1] = y;
		vertex[2][0] = x;
		vertex[2][1] = y + 1;
		vertex[3][0] = x + 1;
		vertex[3][1] = y + 1;
		glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
	}

	glDeleteShader(vs);
	glDeleteShader(fs);
	glDeleteProgram(prog);

	return true;
}

enum piglit_result
piglit_display(void)
{
	GLint max_components;
	int test_varyings, row, col;
	GLboolean pass = GL_TRUE, warned = GL_FALSE;
	bool drew[MAX_VARYING];
	float readback_buffer[MAX_VARYING * MAX_VARYING * 3] = {0};

	piglit_ortho_projection(piglit_width, piglit_height, GL_FALSE);

	glGetIntegerv(GL_MAX_VARYING_FLOATS, &max_components);
	max_varyings = max_components / 4;

	printf("GL_MAX_VARYING_FLOATS = %i\n", max_components);

	test_varyings = max_varyings;
	if (exceed_limits)
		test_varyings++;
	if (test_varyings > MAX_VARYING) {
		printf("test not designed to handle >%d varying vec4s.\n"
		       "(implementation reports %d components)\n",
		       MAX_VARYING, max_varyings);
		test_varyings = MAX_VARYING;
		warned = GL_TRUE;
	}

	glClearColor(0.5, 0.5, 0.5, 0.5);
	glClear(GL_COLOR_BUFFER_BIT);

	for (row = 0; row < test_varyings; row++) {
		drew[row] = draw(row + 1);
	}

	glReadPixels(0, 0, test_varyings, test_varyings, GL_RGB, GL_FLOAT, readback_buffer);

	for (row = 0; row < test_varyings; row++) {
		if (!drew[row])
			continue;

		for (col = 0; col <= row; col++) {
			GLboolean ok;
			float green[3] = {0.0, 1.0, 0.0};
			int pixel_id = 3 * (row * test_varyings + col);

			ok = readback_buffer[pixel_id + 0] == green[0] &&
			     readback_buffer[pixel_id + 1] == green[1] &&
			     readback_buffer[pixel_id + 2] == green[2];

			if (!ok) {
				printf("  Failure with %d vec4 varyings used "
				       "in varying index %d\n",
				       row + 1, col);
				printf(" Expected %f %f %f\n Got %f %f %f\n\n",
				       green[0], green[1], green[2],
				       readback_buffer[pixel_id + 0],
				       readback_buffer[pixel_id + 1],
				       readback_buffer[pixel_id + 2]);
				pass = GL_FALSE;
				break;
			}
		}
	}

	piglit_present_results();

	if (!pass)
		return PIGLIT_FAIL;
	if (warned)
		return PIGLIT_WARN;
	else
		return PIGLIT_PASS;
}

void piglit_init(int argc, char **argv)
{
	int i;

	piglit_require_gl_version(20);

	for (i = 0; i < argc; i++) {
		if (strcmp(argv[i], "--exceed-limits") == 0)
			exceed_limits = true;
	}

	printf("Vertical axis: Increasing numbers of varyings.\n");
	printf("Horizontal axis: Which of the varyings contains the color.\n");
}

