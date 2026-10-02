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

/** @file ubo-invalidate-between-draws.c
 *
 * Tests that a mesh shader sees the new contents of a uniform block after the
 * buffer backing it has been orphaned between two draws.
 *
 * GL_MAP_INVALIDATE_BUFFER_BIT lets the driver discard the old contents and
 * hand back fresh storage, so the buffer the mesh shader reads from may move.
 * A driver that tracks which stages a buffer is bound to has to notice the
 * mesh stage among them and point it at the new allocation. Updating the
 * buffer with glBufferSubData instead would keep the same allocation and never
 * exercise that path.
 *
 * The two draws differ only in the block contents: the first places a quad in
 * the left half of the window, the second in the right half. Both halves must
 * end up green. A driver that misses the mesh stage when rebinding draws the
 * first quad twice and leaves the right half at the clear colour.
 *
 * The colour is a constant rather than a uniform on purpose. If
 * uniform, the linker could sink that load into the fragment shader, which
 * would then have a uniform block of its own; rebinding that block is enough
 * to make some drivers revalidate everything and hide the bug. Keeping the
 * colour constant leaves the mesh shader's uniform block as the only thing
 * that changes between the draws.
 *
 * The same sequence through a vertex shader passes everywhere, so a failure
 * here is specific to the mesh stage.
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

enum piglit_result
piglit_display(void)
{
	float *map;

	ms_begin_frame(prog);

	glBindBufferBase(GL_UNIFORM_BUFFER, 0, ubo);
	glDrawMeshTasksEXT(1, 1, 1);

	map = glMapBufferRange(GL_UNIFORM_BUFFER, 0, sizeof(float) * 4,
			       GL_MAP_WRITE_BIT | GL_MAP_INVALIDATE_BUFFER_BIT);
	if (map == NULL)
		return PIGLIT_FAIL;
	map[0] = MESH_X_RIGHT;
	glUnmapBuffer(GL_UNIFORM_BUFFER);

	glDrawMeshTasksEXT(1, 1, 1);

	return ms_probe_both_halves();
}

void
piglit_init(int argc, char **argv)
{
	static const float initial[4] = {MESH_X_LEFT, 0.0, 0.0, 0.0};

	piglit_require_extension("GL_EXT_mesh_shader");

	prog = generate_ms_prog(MESH_UBO_BLOCK);

	glGenBuffers(1, &ubo);
	glBindBuffer(GL_UNIFORM_BUFFER, ubo);
	glBufferData(GL_UNIFORM_BUFFER, sizeof(initial), initial,
		     GL_DYNAMIC_DRAW);

	if (!piglit_check_gl_error(GL_NO_ERROR))
		piglit_report_result(PIGLIT_FAIL);
}
