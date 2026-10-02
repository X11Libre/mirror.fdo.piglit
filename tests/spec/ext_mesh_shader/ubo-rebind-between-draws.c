/*
 * Copyright © 2026 Valve Corporation
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
 */

#include "common.h"

PIGLIT_GL_TEST_CONFIG_BEGIN

config.supports_gl_core_version = 45;
config.window_width = 128;
config.window_height = 128;
config.window_visual = PIGLIT_GL_VISUAL_DOUBLE | PIGLIT_GL_VISUAL_RGBA;

PIGLIT_GL_TEST_CONFIG_END

static GLuint prog;
static GLuint ubo;
static GLint block_stride;

enum piglit_result
piglit_display(void)
{
	ms_begin_frame(prog);

	glBindBufferRange(GL_UNIFORM_BUFFER, 0, ubo, 0, sizeof(float) * 4);
	glDrawMeshTasksEXT(1, 1, 1);

	glBindBufferRange(GL_UNIFORM_BUFFER, 0, ubo, block_stride,
			  sizeof(float) * 4);
	glDrawMeshTasksEXT(1, 1, 1);

	return ms_probe_both_halves();
}

void
piglit_init(int argc, char **argv)
{
	GLint align;
	char *data;

	piglit_require_extension("GL_EXT_mesh_shader");

	prog = generate_ms_prog(MESH_UBO_BLOCK);

	glGetIntegerv(GL_UNIFORM_BUFFER_OFFSET_ALIGNMENT, &align);
	block_stride = MAX2(align, (GLint) (sizeof(float) * 4));

	data = calloc(2, block_stride);
	*(float *) data = MESH_X_LEFT;
	*(float *) (data + block_stride) = MESH_X_RIGHT;

	glGenBuffers(1, &ubo);
	glBindBuffer(GL_UNIFORM_BUFFER, ubo);
	glBufferData(GL_UNIFORM_BUFFER, 2 * block_stride, data, GL_STATIC_DRAW);
	free(data);

	if (!piglit_check_gl_error(GL_NO_ERROR))
		piglit_report_result(PIGLIT_FAIL);
}
