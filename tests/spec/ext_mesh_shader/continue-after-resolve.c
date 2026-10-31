/*
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

/*
 * Tests that a mesh draw continues a renderpass the same way a regular draw
 * does after the threaded context merged a resolve into the pass:
 *
 *   pass A: clear of a multisampled FBO
 *   resolve of the FBO into texture T, which ends pass A
 *   pass A2: a mesh draw into the same FBO, with no framebuffer change
 *   resolve of the FBO into texture X, which ends pass A2
 *
 * T has to hold the result of pass A only, and X the result of both passes.
 * Drivers that parse renderpass infos in the threaded context can pick up
 * the resolve of pass A for pass A2 and resolve into T again, if the batch
 * ends between the resolve and the mesh draw. Padding draws are added to
 * pass A, and their count is swept so that the end of the batch lands on
 * every alignment of the calls.
 */

#include "common.h"

PIGLIT_GL_TEST_CONFIG_BEGIN

config.supports_gl_core_version = 45;
config.window_width = 64;
config.window_height = 64;
config.window_visual = PIGLIT_GL_VISUAL_DOUBLE | PIGLIT_GL_VISUAL_RGBA;

PIGLIT_GL_TEST_CONFIG_END

#define W 64

/* more than TC_SLOTS_PER_BATCH / the smallest call used for padding: a
 * unit is at least a slot, and the draws are smaller than the resolve, so
 * the end of the batch passes every alignment of the resolve
 */
#define MAX_UNITS 1500

static GLuint prog, pad_prog, pad_vao, ubo, fbm, fbt, fbx;

/* A unit is a bit more than one slot of the batch, and the draws need to
 * be different from each other so that the threaded context cannot merge
 * them into a single call. The draws are far outside of the viewport.
 */
static void
pad(unsigned units)
{
	static const GLint first[4] = {0, 0, 0, 0};
	static const GLsizei count[4] = {1, 1, 1, 1};
	unsigned draws = units / 3;
	unsigned extra = (units % 3) * 2;

	glUseProgram(pad_prog);
	glBindVertexArray(pad_vao);
	for (unsigned i = 0; i < draws; i++)
		glDrawArrays(i & 1 ? GL_POINTS : GL_TRIANGLES, 0, i & 1 ? 1 : 3);
	if (extra)
		glMultiDrawArrays(GL_POINTS, first, count, extra);
}

static GLuint
make_fbo(void)
{
	GLuint tex, fb;

	glGenTextures(1, &tex);
	glBindTexture(GL_TEXTURE_2D, tex);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, W, W, 0, GL_RGBA,
		     GL_UNSIGNED_BYTE, NULL);
	glGenFramebuffers(1, &fb);
	glBindFramebuffer(GL_FRAMEBUFFER, fb);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
			       GL_TEXTURE_2D, tex, 0);
	return fb;
}

static void
resolve_to(GLuint dst)
{
	glBindFramebuffer(GL_READ_FRAMEBUFFER, fbm);
	glBindFramebuffer(GL_DRAW_FRAMEBUFFER, dst);
	glBlitFramebuffer(0, 0, W, W, 0, 0, W, W, GL_COLOR_BUFFER_BIT,
			  GL_NEAREST);
}

static bool
probe(GLuint fb, int x, int y, const float *expect)
{
	glBindFramebuffer(GL_READ_FRAMEBUFFER, fb);
	return piglit_probe_pixel_rgba_silent(x, y, expect, NULL);
}

static bool
run(unsigned units, bool print)
{
	static const float red[4] = {1, 0, 0, 1};
	static const float green[4] = {0, 1, 0, 1};
	static const float cyan[4] = {0, 1, 1, 1};
	bool pass = true;

	/* known contents, and an empty batch */
	glBindFramebuffer(GL_FRAMEBUFFER, fbt);
	glClearColor(cyan[0], cyan[1], cyan[2], cyan[3]);
	glClear(GL_COLOR_BUFFER_BIT);
	glBindFramebuffer(GL_FRAMEBUFFER, fbx);
	glClear(GL_COLOR_BUFFER_BIT);
	glFinish();

	/* pass A */
	glBindFramebuffer(GL_FRAMEBUFFER, fbm);
	glViewport(0, 0, W, W);
	glClearColor(red[0], red[1], red[2], red[3]);
	glClear(GL_COLOR_BUFFER_BIT);
	pad(units);
	resolve_to(fbt);

	/* pass A2 */
	glBindFramebuffer(GL_FRAMEBUFFER, fbm);
	glUseProgram(prog);
	glDrawMeshTasksEXT(1, 1, 1);
	resolve_to(fbx);

	pass = probe(fbt, W / 2, W / 2, red) && pass;
	pass = probe(fbt, 2, 2, red) && pass;
	pass = probe(fbx, W / 2, W / 2, green) && pass;
	pass = probe(fbx, 2, 2, red) && pass;

	if (!pass && print)
		printf("units %u: T center/corner or X center/corner has the wrong color\n",
		       units);
	return pass;
}

enum piglit_result
piglit_display(void)
{
	unsigned failures = 0;
	unsigned first_failure = 0;

	for (unsigned units = 0; units < MAX_UNITS; units++) {
		if (!run(units, failures < 4) && !failures++)
			first_failure = units;
	}

	if (!piglit_check_gl_error(GL_NO_ERROR))
		return PIGLIT_FAIL;
	if (failures) {
		printf("%u of %u iterations failed (first: %u)\n", failures,
		       MAX_UNITS, first_failure);
		return PIGLIT_FAIL;
	}
	return PIGLIT_PASS;
}

void
piglit_init(int argc, char **argv)
{
	static const float x_offset[4] = {0, 0, 0, 0};
	static const float verts[6] = {-100, -100, -100, -90, -90, -100};
	GLuint rb, vbo;

	piglit_require_extension("GL_EXT_mesh_shader");

	pad_prog = piglit_build_simple_program(
		"#version 450\n"
		"layout(location = 0) in vec2 pos;\n"
		"void main() { gl_Position = vec4(pos, 0, 1); gl_PointSize = 1; }\n",
		"#version 450\n"
		"out vec4 color;\n"
		"void main() { color = vec4(1); }\n");
	glGenVertexArrays(1, &pad_vao);
	glBindVertexArray(pad_vao);
	glGenBuffers(1, &vbo);
	glBindBuffer(GL_ARRAY_BUFFER, vbo);
	glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_STATIC_DRAW);
	glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, NULL);
	glEnableVertexAttribArray(0);
	glBindVertexArray(0);

	prog = generate_ms_prog(MESH_UBO_BLOCK);

	glGenBuffers(1, &ubo);
	glBindBuffer(GL_UNIFORM_BUFFER, ubo);
	glBufferData(GL_UNIFORM_BUFFER, sizeof(x_offset), x_offset,
		     GL_STATIC_DRAW);
	glBindBufferBase(GL_UNIFORM_BUFFER, 0, ubo);

	fbt = make_fbo();
	fbx = make_fbo();

	glGenFramebuffers(1, &fbm);
	glBindFramebuffer(GL_FRAMEBUFFER, fbm);
	glGenRenderbuffers(1, &rb);
	glBindRenderbuffer(GL_RENDERBUFFER, rb);
	glRenderbufferStorageMultisample(GL_RENDERBUFFER, 4, GL_RGBA8, W, W);
	glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
				  GL_RENDERBUFFER, rb);
	if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
		printf("multisampled framebuffer incomplete\n");
		piglit_report_result(PIGLIT_SKIP);
	}

	if (!piglit_check_gl_error(GL_NO_ERROR))
		piglit_report_result(PIGLIT_FAIL);
}
