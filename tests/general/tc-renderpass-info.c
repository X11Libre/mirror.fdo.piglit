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
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

/** @file tc-renderpass-info.c
 *
 * Tests that the renderpass tracking of gallium's threaded context stays in
 * sync with the driver around a renderpass that ends with a resolve blit and
 * is followed by more rendering into the same framebuffer.
 *
 * The threaded context records one renderpass info per renderpass on the
 * application thread, and the driver steps through them in order on the
 * queue thread.  A resolve blit of the bound multisampled framebuffer is
 * merged into the renderpass it ends, so a driver that is handed the wrong
 * info for the next pass resolves that pass into the destination of the
 * previous one.
 *
 * Each iteration runs
 *
 *   pass A: clear, draw, and N tiny draws of padding, on a multisampled FBO
 *   resolve of the FBO into texture T, which ends pass A
 *   pass A2: a draw or a clear into the same FBO, with no framebuffer change
 *            (resolve-twice has no work here and resolves again, and
 *            continue-depth-resolve also resolves the depth of pass A)
 *   resolve of the FBO into texture X, which ends pass A2
 *   pass B: samples T into the right half of OUT
 *
 * and verifies T, X, and OUT.  N is swept over more than a whole batch of
 * calls (TC_SLOTS_PER_BATCH is 1536), so the batch flush lands before the
 * first resolve, on it, and after it with every alignment.
 *
 * Only drivers that parse renderpass infos in the threaded context can
 * fail; zink does so for drivers that track renderpasses (set ZINK_DEBUG=rp
 * to force it everywhere).  Everywhere else this is a plain rendering test.
 */

#include "piglit-util-gl.h"

PIGLIT_GL_TEST_CONFIG_BEGIN

	config.supports_gl_compat_version = 30;
	config.window_visual = PIGLIT_GL_VISUAL_RGBA | PIGLIT_GL_VISUAL_DOUBLE;

PIGLIT_GL_TEST_CONFIG_END

#define W 16
/* more than TC_SLOTS_PER_BATCH / the smallest call used for padding: a
 * unit is at least a slot, and the draws are smaller than the resolve, so
 * the end of the batch passes every alignment of the resolve
 */
#define MAX_UNITS 1500

enum next_pass {
	NEXT_DRAW,
	NEXT_DRAW_INDIRECT,
	NEXT_DRAW_LIST,
	NEXT_DRAW_QUERY,
	NEXT_DRAW_MULTI,
	NEXT_RESOLVE_TWICE,
	NEXT_DEPTH_RESOLVE,
	NEXT_CLEAR,
};

static const struct {
	const char *name;
	enum next_pass next;
} scenarios[] = {
	{ "continue-draw", NEXT_DRAW },
	{ "continue-draw-indirect", NEXT_DRAW_INDIRECT },
	{ "continue-draw-list", NEXT_DRAW_LIST },
	{ "continue-draw-query", NEXT_DRAW_QUERY },
	{ "continue-draw-multi", NEXT_DRAW_MULTI },
	{ "resolve-twice", NEXT_RESOLVE_TWICE },
	{ "continue-depth-resolve", NEXT_DEPTH_RESOLVE },
	{ "continue-clear", NEXT_CLEAR },
};

static enum next_pass next_pass;

static GLuint fbm, fbt, fbx, fbo, prog, pad_vbo, rect_vbo, cmd_buf;
static GLuint tex_t, tex_x, tex_out, tex_d, fbd;
static GLuint rb_color, rb_depth;

static const float red[4] = {1, 0, 0, 1};
static const float green[4] = {0, 1, 0, 1};
static const float blue[4] = {0, 0, 1, 1};
static const float cyan[4] = {0, 1, 1, 1};

static GLuint
make_tex(void)
{
	GLuint tex;

	glGenTextures(1, &tex);
	glBindTexture(GL_TEXTURE_2D, tex);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, W, W, 0, GL_RGBA,
		     GL_UNSIGNED_BYTE, NULL);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	return tex;
}

static GLuint
make_fbo(GLuint color_tex)
{
	GLuint fb;

	glGenFramebuffers(1, &fb);
	glBindFramebuffer(GL_FRAMEBUFFER, fb);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
			       GL_TEXTURE_2D, color_tex, 0);
	return fb;
}

static void
set_target(GLuint fb)
{
	glBindFramebuffer(GL_FRAMEBUFFER, fb);
	glViewport(0, 0, W, W);
	piglit_ortho_projection(W, W, GL_FALSE);
}

static void
clear_color(GLuint fb, const float *c)
{
	set_target(fb);
	glClearColor(c[0], c[1], c[2], c[3]);
	glClear(GL_COLOR_BUFFER_BIT);
}

static void
draw_flat(const float *c, float x, float y, float w, float h)
{
	glUseProgram(0);
	glColor4fv(c);
	piglit_draw_rect(x, y, w, h);
}

/* A unit is a bit more than one slot of the batch, and the draws need to
 * be different from each other so that the threaded context cannot merge
 * them into a single call.
 */
static void
pad(unsigned units)
{
	static const GLint first[4] = {0, 0, 0, 0};
	static const GLsizei count[4] = {1, 1, 1, 1};
	unsigned draws = units / 3;
	unsigned extra = (units % 3) * 2;

	glUseProgram(0);
	glBindBuffer(GL_ARRAY_BUFFER, pad_vbo);
	glVertexPointer(2, GL_FLOAT, 0, NULL);
	glEnableClientState(GL_VERTEX_ARRAY);
	for (unsigned i = 0; i < draws; i++)
		glDrawArrays(i & 1 ? GL_POINTS : GL_TRIANGLES, 0, i & 1 ? 1 : 3);
	if (extra)
		glMultiDrawArrays(GL_POINTS, first, count, extra);
	glDisableClientState(GL_VERTEX_ARRAY);
	glBindBuffer(GL_ARRAY_BUFFER, 0);
}

static void
draw_indirect_blue(void)
{
	glUseProgram(0);
	glColor4fv(blue);
	glBindBuffer(GL_ARRAY_BUFFER, rect_vbo);
	glVertexPointer(2, GL_FLOAT, 0, NULL);
	glEnableClientState(GL_VERTEX_ARRAY);
	glBindBuffer(GL_DRAW_INDIRECT_BUFFER, cmd_buf);
	glDrawArraysIndirect(GL_TRIANGLE_STRIP, NULL);
	glBindBuffer(GL_DRAW_INDIRECT_BUFFER, 0);
	glDisableClientState(GL_VERTEX_ARRAY);
	glBindBuffer(GL_ARRAY_BUFFER, 0);
}

static GLuint list;
static GLuint query;

static void
setup(void)
{
	/* the pad draws are far outside of the viewport */
	static const float verts[6] = {-100, -100, -100, -90, -90, -100};
	/* the right half of the framebuffer for the indirect draw, then its
	 * two quarters for the sub-draws of the multi draw
	 */
	static const float rect[24] = {
		W / 2, 0, W, 0, W / 2, W, W, W,
		W / 2, 0, W * 3 / 4, 0, W / 2, W, W * 3 / 4, W,
		W * 3 / 4, 0, W, 0, W * 3 / 4, W, W, W,
	};
	/* count, instance count, first, base instance */
	static const GLuint cmd[4] = {4, 1, 0, 0};
	GLenum status;

	/* a display list draw goes through the draw_vertex_state hook */
	glGenQueries(1, &query);
	list = glGenLists(1);
	glNewList(list, GL_COMPILE);
	glColor4fv(blue);
	glBegin(GL_TRIANGLE_STRIP);
	glVertex2f(W / 2, 0);
	glVertex2f(W, 0);
	glVertex2f(W / 2, W);
	glVertex2f(W, W);
	glEnd();
	glEndList();

	prog = piglit_build_simple_program(
		"#version 120\n"
		"void main() {\n"
		"	gl_Position = gl_ModelViewProjectionMatrix * gl_Vertex;\n"
		"	gl_TexCoord[0] = gl_MultiTexCoord0;\n"
		"}\n",
		"#version 120\n"
		"uniform sampler2D tex;\n"
		"void main() {\n"
		"	gl_FragColor = texture2D(tex, gl_TexCoord[0].xy);\n"
		"}\n");
	glUseProgram(prog);
	glUniform1i(glGetUniformLocation(prog, "tex"), 0);
	glUseProgram(0);

	glGenBuffers(1, &pad_vbo);
	glBindBuffer(GL_ARRAY_BUFFER, pad_vbo);
	glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_STATIC_DRAW);
	glBindBuffer(GL_ARRAY_BUFFER, 0);

	if (next_pass == NEXT_DRAW_INDIRECT || next_pass == NEXT_DRAW_MULTI) {
		glGenBuffers(1, &rect_vbo);
		glBindBuffer(GL_ARRAY_BUFFER, rect_vbo);
		glBufferData(GL_ARRAY_BUFFER, sizeof(rect), rect, GL_STATIC_DRAW);
		glBindBuffer(GL_ARRAY_BUFFER, 0);
		glGenBuffers(1, &cmd_buf);
		glBindBuffer(GL_DRAW_INDIRECT_BUFFER, cmd_buf);
		glBufferData(GL_DRAW_INDIRECT_BUFFER, sizeof(cmd), cmd, GL_STATIC_DRAW);
		glBindBuffer(GL_DRAW_INDIRECT_BUFFER, 0);
	}

	tex_t = make_tex();
	tex_x = make_tex();
	tex_out = make_tex();
	fbt = make_fbo(tex_t);
	fbx = make_fbo(tex_x);
	fbo = make_fbo(tex_out);

	if (next_pass == NEXT_DEPTH_RESOLVE) {
		glGenTextures(1, &tex_d);
		glBindTexture(GL_TEXTURE_2D, tex_d);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, W, W, 0,
			     GL_DEPTH_COMPONENT, GL_FLOAT, NULL);
		glGenFramebuffers(1, &fbd);
		glBindFramebuffer(GL_FRAMEBUFFER, fbd);
		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
				       GL_TEXTURE_2D, tex_d, 0);
		glDrawBuffer(GL_NONE);
		glReadBuffer(GL_NONE);
	}

	/* multisampled source: color and depth */
	glGenFramebuffers(1, &fbm);
	glBindFramebuffer(GL_FRAMEBUFFER, fbm);
	glGenRenderbuffers(1, &rb_color);
	glBindRenderbuffer(GL_RENDERBUFFER, rb_color);
	glRenderbufferStorageMultisample(GL_RENDERBUFFER, 4, GL_RGBA8, W, W);
	glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
				  GL_RENDERBUFFER, rb_color);
	glGenRenderbuffers(1, &rb_depth);
	glBindRenderbuffer(GL_RENDERBUFFER, rb_depth);
	glRenderbufferStorageMultisample(GL_RENDERBUFFER, 4,
					 GL_DEPTH_COMPONENT24, W, W);
	glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
				  GL_RENDERBUFFER, rb_depth);
	status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
	if (status != GL_FRAMEBUFFER_COMPLETE) {
		printf("multisampled framebuffer incomplete\n");
		piglit_report_result(PIGLIT_SKIP);
	}
}

/* Put every target into a known state and make sure the next batch starts
 * empty.
 */
static void
reset(void)
{
	clear_color(fbo, blue);
	clear_color(fbt, cyan);
	clear_color(fbx, cyan);
	if (next_pass == NEXT_DEPTH_RESOLVE) {
		/* a depth resolve that gets lost has to show up in every
		 * iteration, so the expected values must not be left over
		 */
		glBindFramebuffer(GL_FRAMEBUFFER, fbd);
		glClearDepth(0.25);
		glClear(GL_DEPTH_BUFFER_BIT);
	}
	glFinish();
}

static void
resolve_to(GLuint dst)
{
	glBindFramebuffer(GL_READ_FRAMEBUFFER, fbm);
	glBindFramebuffer(GL_DRAW_FRAMEBUFFER, dst);
	glBlitFramebuffer(0, 0, W, W, 0, 0, W, W, GL_COLOR_BUFFER_BIT,
			  GL_NEAREST);
}

static void
run_passes(unsigned units)
{
	/* pass A */
	set_target(fbm);
	glClearColor(red[0], red[1], red[2], red[3]);
	glClearDepth(1.0);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	glEnable(GL_DEPTH_TEST);
	glDepthFunc(GL_LESS);
	glUseProgram(0);
	glColor4fv(green);
	piglit_draw_rect_z(-0.5, 0, 0, W / 2, W);
	pad(units);
	glDisable(GL_DEPTH_TEST);

	/* the end of pass A */
	resolve_to(fbt);
	if (next_pass == NEXT_DEPTH_RESOLVE) {
		glBindFramebuffer(GL_READ_FRAMEBUFFER, fbm);
		glBindFramebuffer(GL_DRAW_FRAMEBUFFER, fbd);
		glBlitFramebuffer(0, 0, W, W, 0, 0, W, W,
				  GL_DEPTH_BUFFER_BIT, GL_NEAREST);
	}

	/* pass A2: the framebuffer state does not change, so this starts
	 * with a draw or a clear and no framebuffer call
	 */
	glBindFramebuffer(GL_FRAMEBUFFER, fbm);
	if (next_pass == NEXT_DRAW_QUERY) {
		/* the occlusion query ends in the pass that continues */
		glBeginQuery(GL_SAMPLES_PASSED, query);
		draw_flat(blue, W / 2, 0, W / 2, W);
		glEndQuery(GL_SAMPLES_PASSED);
	} else if (next_pass == NEXT_DRAW_MULTI) {
		static const GLint first[2] = {4, 8};
		static const GLsizei count[2] = {4, 4};

		glUseProgram(0);
		glColor4fv(blue);
		glBindBuffer(GL_ARRAY_BUFFER, rect_vbo);
		glVertexPointer(2, GL_FLOAT, 0, NULL);
		glEnableClientState(GL_VERTEX_ARRAY);
		glMultiDrawArrays(GL_TRIANGLE_STRIP, first, count, 2);
		glDisableClientState(GL_VERTEX_ARRAY);
		glBindBuffer(GL_ARRAY_BUFFER, 0);
	} else if (next_pass == NEXT_RESOLVE_TWICE) {
		/* a second resolve of the same pass, nothing is drawn */
	} else if (next_pass == NEXT_DRAW || next_pass == NEXT_DEPTH_RESOLVE) {
		draw_flat(blue, W / 2, 0, W / 2, W);
	} else if (next_pass == NEXT_DRAW_INDIRECT) {
		draw_indirect_blue();
	} else if (next_pass == NEXT_DRAW_LIST) {
		glUseProgram(0);
		glCallList(list);
	} else {
		glClearColor(blue[0], blue[1], blue[2], blue[3]);
		glClear(GL_COLOR_BUFFER_BIT);
	}
	resolve_to(fbx);

	/* pass B */
	set_target(fbo);
	glUseProgram(prog);
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, tex_t);
	piglit_draw_rect_tex(W / 2, 0, W / 2, W, 0, 0, 1, 1);
	glUseProgram(0);
}

static bool
probe_color(GLuint fb, int x, const float *expect, const char *what,
	    unsigned units, bool print)
{
	uint8_t buf[W * W * 4];
	const uint8_t *p = buf + (W / 2 * W + x) * 4;

	glBindFramebuffer(GL_READ_FRAMEBUFFER, fb);
	glReadPixels(0, 0, W, W, GL_RGBA, GL_UNSIGNED_BYTE, buf);
	for (unsigned i = 0; i < 4; i++) {
		if (fabsf(p[i] / 255.0f - expect[i]) > 0.02f) {
			if (print)
				printf("units %u: %s x=%d: got %u,%u,%u,%u expected %.0f,%.0f,%.0f,%.0f\n",
				       units, what, x, p[0], p[1], p[2], p[3],
				       expect[0] * 255, expect[1] * 255,
				       expect[2] * 255, expect[3] * 255);
			return false;
		}
	}
	return true;
}

static bool
verify(unsigned units, bool print)
{
	bool pass = true;

	/* T is the result of pass A only */
	pass &= probe_color(fbt, 4, green, "T left", units, print);
	pass &= probe_color(fbt, 12, red, "T right", units, print);

	/* X is the result of pass A2: a draw leaves the left half of pass
	 * A, a clear replaces everything
	 */
	pass &= probe_color(fbx, 4, next_pass == NEXT_CLEAR ? blue : green,
			    "X left", units, print);
	pass &= probe_color(fbx, 12, next_pass == NEXT_RESOLVE_TWICE ? red : blue,
			    "X right", units, print);
	if (next_pass == NEXT_DRAW_MULTI) {
		/* each sub-draw covers one quarter */
		pass &= probe_color(fbx, 9, blue, "X first sub-draw", units, print);
		pass &= probe_color(fbx, 14, blue, "X second sub-draw", units, print);
	}

	if (next_pass == NEXT_DEPTH_RESOLVE) {
		float d[W * W];

		/* the depth of pass A: nearer where it drew */
		glBindFramebuffer(GL_READ_FRAMEBUFFER, fbd);
		glReadPixels(0, 0, W, W, GL_DEPTH_COMPONENT, GL_FLOAT, d);
		if (fabsf(d[W / 2 * W + 4] - 0.75f) > 0.01f ||
		    fabsf(d[W / 2 * W + 12] - 1.0f) > 0.01f) {
			if (print)
				printf("units %u: depth left %f right %f expected 0.75 and 1\n",
				       units, d[W / 2 * W + 4], d[W / 2 * W + 12]);
			pass = false;
		}
	}

	if (next_pass == NEXT_DRAW_QUERY) {
		GLuint samples = 0;

		glGetQueryObjectuiv(query, GL_QUERY_RESULT, &samples);
		/* a half of the framebuffer, at least one sample per pixel */
		if (samples < W * W / 2) {
			if (print)
				printf("units %u: %u samples counted for the draw, expected at least %u\n",
				       units, samples, W * W / 2);
			pass = false;
		}
	}

	/* pass B samples T and has to keep what it did not draw over */
	pass &= probe_color(fbo, 2, blue, "OUT left", units, print);
	pass &= probe_color(fbo, 9, green, "OUT sampled", units, print);
	pass &= probe_color(fbo, 14, red, "OUT sampled", units, print);
	return pass;
}

void
piglit_init(int argc, char **argv)
{
	piglit_require_extension("GL_ARB_framebuffer_object");

	if (argc != 2) {
		printf("usage: tc-renderpass-info <scenario>\n");
		piglit_report_result(PIGLIT_FAIL);
	}
	for (unsigned i = 0; i < ARRAY_SIZE(scenarios); i++) {
		if (!strcmp(scenarios[i].name, argv[1])) {
			next_pass = scenarios[i].next;
			if (next_pass == NEXT_DRAW_QUERY)
				piglit_require_extension("GL_ARB_occlusion_query");
			if (next_pass == NEXT_DRAW_INDIRECT)
				piglit_require_extension("GL_ARB_draw_indirect");
			setup();
			return;
		}
	}
	printf("unknown scenario %s\n", argv[1]);
	piglit_report_result(PIGLIT_FAIL);
}

enum piglit_result
piglit_display(void)
{
	unsigned failures = 0;
	unsigned first_failure = 0;

	for (unsigned units = 0; units < MAX_UNITS; units++) {
		reset();
		run_passes(units);

		if (!verify(units, failures < 4) && !failures++)
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
