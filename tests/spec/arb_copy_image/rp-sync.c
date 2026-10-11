/* SPDX-License-Identifier: MIT */

#include "piglit-util-gl.h"

PIGLIT_GL_TEST_CONFIG_BEGIN

	config.supports_gl_compat_version = 30;

	config.window_visual = PIGLIT_GL_VISUAL_RGBA | PIGLIT_GL_VISUAL_DOUBLE;
	config.khr_no_error_support = PIGLIT_NO_ERRORS;

PIGLIT_GL_TEST_CONFIG_END

#define SIZE 32

static const float green[4] = { 0.0, 1.0, 0.0, 1.0 };
static const float red[4] = { 1.0, 0.0, 0.0, 1.0 };
static const float blue[4] = { 0.0, 0.0, 1.0, 1.0 };

static GLuint fbo_a, fbo_b, fbo_c, fbo_d, fbo_read;
static GLuint tex_a, tex_src;

static GLuint
make_tex(void)
{
	GLuint tex;

	glGenTextures(1, &tex);
	glBindTexture(GL_TEXTURE_2D, tex);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, SIZE, SIZE, 0,
		     GL_RGBA, GL_UNSIGNED_BYTE, NULL);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	return tex;
}

static GLuint
make_fbo(GLuint color, bool depth)
{
	GLuint fbo;

	glGenFramebuffers(1, &fbo);
	glBindFramebuffer(GL_FRAMEBUFFER, fbo);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
			       GL_TEXTURE_2D, color, 0);
	if (depth) {
		GLuint rb;

		glGenRenderbuffers(1, &rb);
		glBindRenderbuffer(GL_RENDERBUFFER, rb);
		glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24,
				      SIZE, SIZE);
		glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
					  GL_RENDERBUFFER, rb);
	}
	if (glCheckFramebufferStatus(GL_FRAMEBUFFER) !=
	    GL_FRAMEBUFFER_COMPLETE)
		piglit_report_result(PIGLIT_SKIP);
	return fbo;
}

static void
sync_read(void)
{
	GLubyte pix[4];

	glBindFramebuffer(GL_READ_FRAMEBUFFER, fbo_read);
	glReadPixels(0, 0, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pix);
}

static void
quad(const float *color, float z)
{
	glColor4fv(color);
	piglit_draw_rect_z(z, -1, -1, 2, 2);
}

void
piglit_init(int argc, char **argv)
{
	piglit_require_extension("GL_ARB_copy_image");
	piglit_require_extension("GL_ARB_invalidate_subdata");

	tex_a = make_tex();
	tex_src = make_tex();
	fbo_a = make_fbo(tex_a, false);
	fbo_b = make_fbo(make_tex(), true);
	fbo_c = make_fbo(make_tex(), true);
	fbo_d = make_fbo(make_tex(), false);
	fbo_read = make_fbo(make_tex(), false);

	glViewport(0, 0, SIZE, SIZE);
	glDepthFunc(GL_LESS);
}

enum piglit_result
piglit_display(void)
{
	static const GLenum depth_att = GL_DEPTH_ATTACHMENT;
	bool pass = true;
	int i;

	for (i = 0; i < 4; i++) {
		glBindFramebuffer(GL_DRAW_FRAMEBUFFER, fbo_a);
		glDisable(GL_DEPTH_TEST);
		quad(blue, 0.0);
		sync_read();

		glCopyImageSubData(tex_src, GL_TEXTURE_2D, 0, 0, 0, 0,
				   tex_a, GL_TEXTURE_2D, 0, 0, 0, 0,
				   SIZE, SIZE, 1);
		sync_read();

		quad(blue, 0.0);
		quad(blue, 0.0);

		glBindFramebuffer(GL_DRAW_FRAMEBUFFER, fbo_b);
		glEnable(GL_DEPTH_TEST);
		glClearColor(0.0, 0.0, 0.0, 1.0);
		glClearDepth(1.0);
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
		quad(red, 0.8);

		glBindFramebuffer(GL_DRAW_FRAMEBUFFER, fbo_c);
		quad(blue, 0.0);
		glInvalidateFramebuffer(GL_DRAW_FRAMEBUFFER, 1, &depth_att);

		glBindFramebuffer(GL_DRAW_FRAMEBUFFER, fbo_b);
		quad(green, 0.0);

		glBindFramebuffer(GL_DRAW_FRAMEBUFFER, fbo_d);
		glDisable(GL_DEPTH_TEST);
		quad(blue, 0.0);

		glBindFramebuffer(GL_READ_FRAMEBUFFER, fbo_b);
		pass = piglit_probe_rect_rgba(0, 0, SIZE, SIZE, green) && pass;
	}

	glBindFramebuffer(GL_FRAMEBUFFER, piglit_winsys_fbo);

	return pass ? PIGLIT_PASS : PIGLIT_FAIL;
}
