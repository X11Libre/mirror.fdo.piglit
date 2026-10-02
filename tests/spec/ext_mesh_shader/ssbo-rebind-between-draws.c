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
static GLuint ssbo[2];

enum piglit_result
piglit_display(void)
{
	ms_begin_frame(prog);

	glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, ssbo[0]);
	glDrawMeshTasksEXT(1, 1, 1);

	glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, ssbo[1]);
	glDrawMeshTasksEXT(1, 1, 1);

	return ms_probe_both_halves();
}

void
piglit_init(int argc, char **argv)
{
	static const float offsets[2] = {MESH_X_LEFT, MESH_X_RIGHT};
	int i;

	piglit_require_extension("GL_EXT_mesh_shader");

	prog = generate_ms_prog(MESH_SSBO_BLOCK);

	glGenBuffers(2, ssbo);
	for (i = 0; i < 2; i++) {
		glBindBuffer(GL_SHADER_STORAGE_BUFFER, ssbo[i]);
		glBufferData(GL_SHADER_STORAGE_BUFFER, sizeof(float),
			     &offsets[i], GL_STATIC_DRAW);
	}

	if (!piglit_check_gl_error(GL_NO_ERROR))
		piglit_report_result(PIGLIT_FAIL);
}
