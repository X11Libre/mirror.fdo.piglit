/*
 * SPDX-License-Identifier: MIT
 */

/**
 * reproducer for mesa#16547: when the buffer gets new memory zink is
 * supposed to mark the vertex buffers as changed so the next draw binds the
 * new memory.
 */

#include "piglit-util-gl.h"

PIGLIT_GL_TEST_CONFIG_BEGIN

	config.supports_gl_compat_version = 20;
	config.window_width = 256;
	config.window_height = 32;
	config.window_visual = PIGLIT_GL_VISUAL_DOUBLE | PIGLIT_GL_VISUAL_RGBA;
	config.khr_no_error_support = PIGLIT_NO_ERRORS;

PIGLIT_GL_TEST_CONFIG_END

static const char *vs_source =
	"#version 110\n"
	"attribute vec2 piglit_vertex;\n"
	"void main()\n"
	"{\n"
	"	gl_Position = vec4(piglit_vertex, 0.0, 1.0);\n"
	"}\n";

static const char *fs_source =
	"#version 110\n"
	"uniform vec4 color;\n"
	"void main()\n"
	"{\n"
	"	gl_FragColor = color;\n"
	"}\n";

static GLuint prog;
static GLint color_loc;

static void
quad(float *v, int x0, int x1)
{
	float l = 2.0f * x0 / piglit_width - 1;
	float r = 2.0f * x1 / piglit_width - 1;
	const float q[9] = { l, -1, r, -1, l, 1, r, 1, 0 };

	memcpy(v, q, sizeof(q));
}

enum piglit_result
piglit_display(void)
{
	static const float white[4] = { 1, 1, 1, 1 };
	static const float red[4] = { 1, 0, 0, 1 };
	static const float blue[4] = { 0, 0, 1, 1 };
	static const int columns[2][2] = { { 224, 256 }, { 0, 32 } };
	GLuint vbo[2];
	float v[9];
	bool pass = true;

	glClearColor(0, 0, 0, 1);
	glClear(GL_COLOR_BUFFER_BIT);
	glUseProgram(prog);

	glGenBuffers(2, vbo);
	for (int i = 0; i < 2; i++) {
		quad(v, columns[i][0], columns[i][1]);
		glBindBuffer(GL_ARRAY_BUFFER, vbo[i]);
		glBufferData(GL_ARRAY_BUFFER, sizeof(v), v, GL_STREAM_DRAW);
	}
	glEnableVertexAttribArray(PIGLIT_ATTRIB_POS);

	glBindBuffer(GL_ARRAY_BUFFER, vbo[0]);
	glVertexAttribPointer(PIGLIT_ATTRIB_POS, 2, GL_FLOAT, GL_FALSE, 0, 0);
	glUniform4fv(color_loc, 1, white);
	glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

	glBindBuffer(GL_ARRAY_BUFFER, vbo[1]);
	glVertexAttribPointer(PIGLIT_ATTRIB_POS, 2, GL_FLOAT, GL_FALSE, 0, 0);
	glUniform4fv(color_loc, 1, red);
	glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

	glVertexAttribPointer(PIGLIT_ATTRIB_POS, 3, GL_FLOAT, GL_FALSE,
			      2 * sizeof(float), 0);
	glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

	quad(v, 160, 192);
	glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(v), v);
	glUniform4fv(color_loc, 1, blue);
	glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

	pass &= piglit_probe_rect_rgba(224, 0, 32, piglit_height, white);
	pass &= piglit_probe_rect_rgba(0, 0, 32, piglit_height, red);
	pass &= piglit_probe_rect_rgba(160, 0, 32, piglit_height, blue);
	pass &= piglit_check_gl_error(GL_NO_ERROR);

	glDeleteBuffers(2, vbo);
	piglit_present_results();

	return pass ? PIGLIT_PASS : PIGLIT_FAIL;
}

void
piglit_init(int argc, char **argv)
{
	prog = piglit_build_simple_program(vs_source, fs_source);
	color_loc = glGetUniformLocation(prog, "color");
}
