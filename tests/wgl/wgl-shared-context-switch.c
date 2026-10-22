/* SPDX-License-Identifier: MIT */

/**
 * Work done in one context must be visible to a second context sharing its
 * objects once wglMakeCurrent has switched to it, with no glFinish or sync
 * object in between. Context A does a pile of slow overdraw and delivers the
 * result into a shared object, wglMakeCurrent switches to context B in the
 * same thread, and B reads the object back. A wglMakeCurrent that doesn't
 * order A's work before B's, because it skips the flush of A or leaves A's
 * work queued behind B's, shows up as a stale value.
 *
 * GL itself only promises this after a glFinish or a sync object. The
 * documented wglMakeCurrent flush and the way applications switch between
 * shared contexts are what this relies on, so a failure means such
 * applications see stale data on the implementation.
 *
 * The producer, A, delivers its result three ways:
 *  - render: renders straight into the shared texture
 *  - copy: renders into a private texture and glCopyTexSubImage2D's it into
 *    the shared one
 *  - pbo: renders into a private texture and glReadPixels's it into a shared
 *    pixel buffer
 * The consumer, B, samples the shared texture into a private target and reads
 * a pixel, or reads the shared buffer with glGetBufferSubData.
 *
 * Every iteration uses a value far from the previous one, so a read of the
 * previous iteration's result is a mismatch too.
 */

#include <stdio.h>
#include "piglit-util-gl.h"
#include "piglit-wgl-util.h"

#define SIZE 1024
#define ITERATIONS 300

enum mode {
	MODE_RENDER,
	MODE_COPY,
	MODE_PBO,
};

static HDC dc;
static HGLRC ctx_a, ctx_b;
static GLuint shared_tex, shared_buf;
static GLuint prog_a, fbo_a, tex_a;
static GLuint fbo_b, tex_b;
static GLint want_loc;

static const char *vs_text =
	"void main()\n"
	"{\n"
	"	gl_Position = ftransform();\n"
	"}\n";

/* the uniform zero keeps the compiler from folding the loop away */
static const char *fs_text =
	"uniform float want;\n"
	"uniform float zero;\n"
	"void main()\n"
	"{\n"
	"	float c = want;\n"
	"	for (int i = 0; i < 64; i++)\n"
	"		c += zero * float(i);\n"
	"	gl_FragColor = vec4(c, c, c, 1.0);\n"
	"}\n";

static GLenum
create_target(GLuint *fbo, GLuint tex)
{
	glGenFramebuffers(1, fbo);
	glBindFramebuffer(GL_FRAMEBUFFER, *fbo);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
			       GL_TEXTURE_2D, tex, 0);
	return glCheckFramebufferStatus(GL_FRAMEBUFFER);
}

static GLuint
create_texture(int size)
{
	GLuint tex;

	glGenTextures(1, &tex);
	glBindTexture(GL_TEXTURE_2D, tex);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, size, size, 0, GL_RGBA,
		     GL_UNSIGNED_BYTE, NULL);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	return tex;
}

static void
clear_shared(void)
{
	static unsigned char zero[SIZE * SIZE * 4];

	glBindTexture(GL_TEXTURE_2D, shared_tex);
	glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, SIZE, SIZE, GL_RGBA,
			GL_UNSIGNED_BYTE, zero);
	glBindBuffer(GL_PIXEL_PACK_BUFFER, shared_buf);
	glBufferSubData(GL_PIXEL_PACK_BUFFER, 0, 4, zero);
	glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
	glFinish();
}

static bool
setup(void)
{
	bool ok;

	if (!wglMakeCurrent(dc, ctx_a)) {
		printf("wglMakeCurrent(A) failed\n");
		return false;
	}
	piglit_dispatch_default_init(PIGLIT_DISPATCH_GL);
	if (piglit_get_gl_version() < 30)
		piglit_require_extension("GL_ARB_framebuffer_object");

	shared_tex = create_texture(SIZE);
	glGenBuffers(1, &shared_buf);
	glBindBuffer(GL_PIXEL_PACK_BUFFER, shared_buf);
	glBufferData(GL_PIXEL_PACK_BUFFER, 4, NULL, GL_DYNAMIC_READ);
	glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);

	tex_a = create_texture(SIZE);
	ok = create_target(&fbo_a, tex_a) == GL_FRAMEBUFFER_COMPLETE;
	prog_a = piglit_build_simple_program(vs_text, fs_text);
	want_loc = glGetUniformLocation(prog_a, "want");
	glUseProgram(prog_a);
	glUniform1f(glGetUniformLocation(prog_a, "zero"), 0.0f);
	glUseProgram(0);
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	glFinish();

	/* the shared objects exist, so B can see them now */
	if (!wglMakeCurrent(dc, ctx_b)) {
		printf("wglMakeCurrent(B) failed\n");
		return false;
	}
	tex_b = create_texture(16);
	ok &= create_target(&fbo_b, tex_b) == GL_FRAMEBUFFER_COMPLETE;
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	if (!ok)
		printf("framebuffer incomplete\n");
	return ok;
}

/* values at least 40 apart from the previous one and never 0 */
static unsigned char
value_for(int i)
{
	return 16 + (i * 73) % 200;
}

static void
reset_transforms(void)
{
	glMatrixMode(GL_PROJECTION);
	glLoadIdentity();
	glMatrixMode(GL_MODELVIEW);
	glLoadIdentity();
}

static void
produce(enum mode mode, unsigned char want)
{
	glBindFramebuffer(GL_FRAMEBUFFER, fbo_a);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
			       GL_TEXTURE_2D,
			       mode == MODE_RENDER ? shared_tex : tex_a, 0);

	glViewport(0, 0, SIZE, SIZE);
	reset_transforms();
	glDisable(GL_BLEND);
	glClearColor(0, 0, 0, 0);
	glClear(GL_COLOR_BUFFER_BIT);

	glUseProgram(prog_a);
	glUniform1f(want_loc, want / 255.0f);
	for (int j = 0; j < 20; j++)
		piglit_draw_rect(-1, -1, 2, 2);
	glUseProgram(0);

	if (mode == MODE_COPY) {
		glBindTexture(GL_TEXTURE_2D, shared_tex);
		glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 0, 0, SIZE, SIZE);
	} else if (mode == MODE_PBO) {
		glBindBuffer(GL_PIXEL_PACK_BUFFER, shared_buf);
		glReadPixels(SIZE / 2, SIZE / 2, 1, 1, GL_RGBA,
			     GL_UNSIGNED_BYTE, NULL);
		glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
	}
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

static int
consume(enum mode mode)
{
	unsigned char px[4] = {0};

	if (mode == MODE_PBO) {
		glBindBuffer(GL_PIXEL_PACK_BUFFER, shared_buf);
		glGetBufferSubData(GL_PIXEL_PACK_BUFFER, 0, 4, px);
		glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
		return px[0];
	}

	glBindFramebuffer(GL_FRAMEBUFFER, fbo_b);
	glViewport(0, 0, 16, 16);
	reset_transforms();
	glClearColor(0, 0, 0, 1);
	glClear(GL_COLOR_BUFFER_BIT);
	glEnable(GL_TEXTURE_2D);
	glBindTexture(GL_TEXTURE_2D, shared_tex);
	glColor4f(1, 1, 1, 1);
	piglit_draw_rect_tex(-1, -1, 2, 2, 0, 0, 1, 1);
	glDisable(GL_TEXTURE_2D);
	glReadPixels(8, 8, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	return px[0];
}

static enum piglit_result
run_mode(enum mode mode, const char *name)
{
	int stale = 0, previous = 0;

	wglMakeCurrent(dc, ctx_a);
	clear_shared();

	for (int i = 0; i < ITERATIONS; i++) {
		unsigned char want = value_for(i);
		int got;

		if (!wglMakeCurrent(dc, ctx_a)) {
			printf("wglMakeCurrent(A) failed\n");
			return PIGLIT_FAIL;
		}
		produce(mode, want);

		if (!wglMakeCurrent(dc, ctx_b)) {
			printf("wglMakeCurrent(B) failed\n");
			return PIGLIT_FAIL;
		}
		got = consume(mode);

		if (abs(got - want) > 2) {
			if (!stale)
				printf("%s: iteration %d read %d, expected %d "
				       "(previous result %d)\n", name, i, got,
				       want, previous);
			stale++;
		}
		previous = want;
	}

	if (stale)
		printf("%s: %d of %d iterations were stale\n", name, stale,
		       ITERATIONS);
	return stale ? PIGLIT_FAIL : PIGLIT_PASS;
}

int
main(int argc, char **argv)
{
	static const struct {
		enum mode mode;
		const char *name;
	} modes[] = {
		{ MODE_RENDER, "render" },
		{ MODE_COPY, "copy" },
		{ MODE_PBO, "pbo" },
	};
	enum piglit_result result = PIGLIT_PASS;
	HWND win;

	win = piglit_get_wgl_window();
	if (!win) {
		printf("failed to create a window\n");
		piglit_report_result(PIGLIT_SKIP);
	}
	dc = GetDC(win);
	ctx_a = piglit_get_wgl_context(win);
	ctx_b = piglit_get_wgl_context(win);
	if (!ctx_a || !ctx_b || !wglShareLists(ctx_a, ctx_b)) {
		printf("couldn't create two shared contexts\n");
		piglit_report_result(PIGLIT_SKIP);
	}

	if (!setup())
		piglit_report_result(PIGLIT_FAIL);

	for (unsigned i = 0; i < ARRAY_SIZE(modes); i++) {
		enum piglit_result r = run_mode(modes[i].mode, modes[i].name);

		piglit_report_subtest_result(r, "%s", modes[i].name);
		piglit_merge_result(&result, r);
	}

	wglMakeCurrent(NULL, NULL);
	piglit_report_result(result);
	return 0;
}
