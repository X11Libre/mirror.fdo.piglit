/*
 * Copyright (C) 2025  Advanced Micro Devices, Inc.
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
#undef NDEBUG
#include <assert.h>
#include "piglit-util-gl.h"

#define FBO_SIZE 3
#define WINDOW_SIZE 512

PIGLIT_GL_TEST_CONFIG_BEGIN

	config.supports_gl_compat_version = 10;
	config.window_width = WINDOW_SIZE;
	config.window_height = WINDOW_SIZE;
	config.window_visual = PIGLIT_GL_VISUAL_RGBA | PIGLIT_GL_VISUAL_DOUBLE;

PIGLIT_GL_TEST_CONFIG_END

#define VS \
	"#version 130\n" \
	"void main() {\n" \
	"   gl_Position = gl_Vertex;\n" \
	"}\n"

#define FS_COORD \
	"#version 130\n" \
	"void main() {\n" \
	"	gl_FragColor = vec4(gl_FragCoord.xy / 3, 0, 1);\n" \
	"}"

#define FS_WHITE \
	"#version 130\n" \
	"void main() {\n" \
	"	gl_FragColor = vec4(1.0);\n" \
	"}"

#define FS_GRAY \
	"#version 130\n" \
	"void main() {\n" \
	"	gl_FragColor = vec4(0.4);\n" \
	"}"

static GLuint fbo, prog_fragcoord, prog_white, prog_gray;

static float verts[6] = {
	-0.75, -0.75,
	 0.5, -0.75,
	-0.75,  0.5,
};

static void
key_press(unsigned char key, int x, int y)
{
	unsigned i;

	switch (key) {
	case 's':
	case 'd':
	case 'f':
		i = key == 's' ? 0 :
		    key == 'd' ? 1 : 2;
		verts[i*2+0] = (float)x / (WINDOW_SIZE / 2) - 1;
		verts[i*2+1] = (float)(WINDOW_SIZE - y) / (WINDOW_SIZE / 2) - 1;
		break;
	case 27:
		exit(0);
		break;
	}
	piglit_post_redisplay();
}

void
piglit_init(int argc, char **argv)
{
	piglit_set_keyboard_func(key_press);
	piglit_require_gl_version(30);

	prog_fragcoord = piglit_build_simple_program(VS, FS_COORD);
	prog_white = piglit_build_simple_program(VS, FS_WHITE);
	prog_gray = piglit_build_simple_program(VS, FS_GRAY);

	GLuint rb;
	glGenRenderbuffers(1, &rb);
	glBindRenderbuffer(GL_RENDERBUFFER, rb);
	glRenderbufferStorage(GL_RENDERBUFFER, GL_RGBA8, FBO_SIZE, FBO_SIZE);

	glGenFramebuffers(1, &fbo);
	glBindFramebuffer(GL_FRAMEBUFFER, fbo);
	glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, rb);
	bool status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
	assert(status);

	glClearColor(0.1, 0.1, 0.1, 0.1);
}

enum piglit_result
piglit_display(void)
{
	glBindFramebuffer(GL_FRAMEBUFFER, fbo);
	glViewport(0, 0, FBO_SIZE, FBO_SIZE);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	glUseProgram(prog_fragcoord);

	glBegin(GL_TRIANGLES);
	for (unsigned i = 0; i < 3; i++)
		glVertex2fv(verts + i*2);
	glEnd();

	glBindFramebuffer(GL_READ_FRAMEBUFFER, fbo);
	glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
	glBlitFramebuffer(0, 0, FBO_SIZE, FBO_SIZE, 0, 0, WINDOW_SIZE, WINDOW_SIZE, GL_COLOR_BUFFER_BIT, GL_NEAREST);

	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	glViewport(0, 0, piglit_width, piglit_height);

	glUseProgram(prog_gray);
	glBegin(GL_LINES);
	for (unsigned i = 0; i < 3; i++) {
		unsigned j = (i + 1) % 3;
		float x = verts[i*2];
		float y = verts[i*2+1];
		float dx = verts[j*2] - x;
		float dy = verts[j*2+1] - y;

		glVertex2f(x - dx*1000, y - dy*1000);
		glVertex2f(x + dx*1000, y + dy*1000);
	}
	glEnd();

	glUseProgram(prog_white);
	glBegin(GL_LINE_LOOP);
	for (unsigned i = 0; i < 3; i++)
		glVertex2fv(verts + i*2);
	glEnd();

	glBegin(GL_POINTS);
	for (unsigned i = 0; i < 9; i++)
		glVertex2f(-1 + 1/3.0 + (i % FBO_SIZE) / 1.5,
			   -1 + 1/3.0 + (i / FBO_SIZE) / 1.5);
	glEnd();

	piglit_swap_buffers();
	piglit_post_redisplay();
	return PIGLIT_PASS;
}
