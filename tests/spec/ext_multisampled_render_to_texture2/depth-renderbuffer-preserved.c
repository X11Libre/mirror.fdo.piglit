/*
 * SPDX-License-Identifier: MIT
 */

/**
 * Tests that a multisampled depth/stencil renderbuffer keeps its contents
 * when the color attachment is resolved, from revision 4 of
 * EXT_multisampled_render_to_texture2:
 *
 *    "If the multisample buffer is a depth/stencil renderbuffer then it is
 *    not discarded."
 *
 * Each subtest draws a red rectangle over the left half that writes depth or
 * stencil, does an operation that may resolve the color texture, and then
 * draws a green rectangle over everything that the depth or stencil test
 * must keep out of the left half.
 */

#include "../ext_multisampled_render_to_texture/common.h"

#define SIZE 64

enum op {
	OP_READPIXELS,
	OP_BIND_FRAMEBUFFER,
	OP_FLUSH,
	OP_SAMPLE,
};

struct params {
	GLenum internalformat;
	enum op op;
};

static enum piglit_result run(void *data);

static const struct params depth_readpixels = { GL_DEPTH_COMPONENT16, OP_READPIXELS };
static const struct params depth_bind = { GL_DEPTH_COMPONENT16, OP_BIND_FRAMEBUFFER };
static const struct params depth_flush = { GL_DEPTH_COMPONENT16, OP_FLUSH };
static const struct params depth_sample = { GL_DEPTH_COMPONENT16, OP_SAMPLE };
static const struct params stencil_readpixels = { GL_STENCIL_INDEX8, OP_READPIXELS };
static const struct params stencil_bind = { GL_STENCIL_INDEX8, OP_BIND_FRAMEBUFFER };

static const struct piglit_subtest subtests[] = {
	{ "depth after ReadPixels", "depth-readpixels", run, (void *)&depth_readpixels },
	{ "depth after BindFramebuffer", "depth-bindframebuffer", run, (void *)&depth_bind },
	{ "depth after Flush", "depth-flush", run, (void *)&depth_flush },
	{ "depth after sampling the color texture", "depth-sample", run, (void *)&depth_sample },
	{ "stencil after ReadPixels", "stencil-readpixels", run, (void *)&stencil_readpixels },
	{ "stencil after BindFramebuffer", "stencil-bindframebuffer", run, (void *)&stencil_bind },
	{ NULL }
};

static const struct piglit_gl_test_config *piglit_config;

PIGLIT_GL_TEST_CONFIG_BEGIN

	config.supports_gl_es_version = 20;
	config.window_visual = PIGLIT_GL_VISUAL_RGBA;
	config.khr_no_error_support = PIGLIT_NO_ERRORS;
	config.subtests = subtests;
	piglit_config = &config;

PIGLIT_GL_TEST_CONFIG_END

static const float red[4] = { 1.0, 0.0, 0.0, 1.0 };
static const float green[4] = { 0.0, 1.0, 0.0, 1.0 };

static struct msrtt_program prog;
static GLuint tex_prog;

static const char *vs_texture_source =
	"attribute vec2 piglit_vertex;\n"
	"void main()\n"
	"{\n"
	"	gl_Position = vec4(piglit_vertex, 0.0, 1.0);\n"
	"}\n";

static const char *fs_texture_source =
	"precision mediump float;\n"
	"uniform sampler2D tex;\n"
	"void main()\n"
	"{\n"
	"	gl_FragColor = texture2D(tex, vec2(0.5));\n"
	"}\n";

static void
do_op(enum op op, GLuint fb, GLuint tex)
{
	GLubyte pixel[4];
	GLuint other, other_tex;

	switch (op) {
	case OP_READPIXELS:
		glReadPixels(0, 0, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
		break;
	case OP_BIND_FRAMEBUFFER:
		glBindFramebuffer(GL_FRAMEBUFFER, piglit_winsys_fbo);
		glBindFramebuffer(GL_FRAMEBUFFER, fb);
		break;
	case OP_FLUSH:
		glFlush();
		break;
	case OP_SAMPLE:
		other_tex = msrtt_rgba_texture(4, 4);
		other = msrtt_read_framebuffer(other_tex);
		glViewport(0, 0, 4, 4);
		glDisable(GL_DEPTH_TEST);
		glDisable(GL_STENCIL_TEST);
		glUseProgram(tex_prog);
		glBindTexture(GL_TEXTURE_2D, tex);
		piglit_draw_rect(-1, -1, 2, 2);
		glDeleteFramebuffers(1, &other);
		glDeleteTextures(1, &other_tex);
		glBindFramebuffer(GL_FRAMEBUFFER, fb);
		glViewport(0, 0, SIZE, SIZE);
		break;
	}
}

static void
set_tests(GLenum internalformat, bool second)
{
	if (internalformat == GL_STENCIL_INDEX8) {
		glEnable(GL_STENCIL_TEST);
		glStencilFunc(second ? GL_NOTEQUAL : GL_ALWAYS, 1, 0xff);
		glStencilOp(GL_KEEP, GL_KEEP, second ? GL_KEEP : GL_REPLACE);
	} else {
		glEnable(GL_DEPTH_TEST);
		glDepthFunc(GL_LESS);
	}
}

static enum piglit_result
run(void *data)
{
	const struct params *p = data;
	GLenum attachment = p->internalformat == GL_STENCIL_INDEX8 ?
			    GL_STENCIL_ATTACHMENT : GL_DEPTH_ATTACHMENT;
	GLuint fb, tex, rb, read_fb;
	bool pass = true;

	glGenFramebuffers(1, &fb);
	glBindFramebuffer(GL_FRAMEBUFFER, fb);
	tex = msrtt_rgba_texture(SIZE, SIZE);
	glFramebufferTexture2DMultisampleEXT(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
					     GL_TEXTURE_2D, tex, 0,
					     msrtt_samples());
	glGenRenderbuffers(1, &rb);
	glBindRenderbuffer(GL_RENDERBUFFER, rb);
	msrtt_renderbuffer_storage(GL_RENDERBUFFER,
					    msrtt_texture_samples(GL_FRAMEBUFFER,
								  GL_COLOR_ATTACHMENT0),
					    p->internalformat, SIZE, SIZE);
	glFramebufferRenderbuffer(GL_FRAMEBUFFER, attachment, GL_RENDERBUFFER, rb);
	if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
		printf("framebuffer is incomplete\n");
		pass = false;
		goto done;
	}

	glViewport(0, 0, SIZE, SIZE);
	glClearColor(0, 0, 0, 1);
	glClearDepthf(1.0);
	glClearStencil(0);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);

	set_tests(p->internalformat, false);
	msrtt_use(&prog, red, -0.5);
	piglit_draw_rect(-1, -1, 1, 2);

	do_op(p->op, fb, tex);

	set_tests(p->internalformat, true);
	msrtt_use(&prog, green, 0.5);
	piglit_draw_rect(-1, -1, 2, 2);
	glDisable(GL_DEPTH_TEST);
	glDisable(GL_STENCIL_TEST);

	read_fb = msrtt_read_framebuffer(tex);
	pass &= piglit_probe_rect_rgba(0, 0, SIZE / 2, SIZE, red);
	pass &= piglit_probe_rect_rgba(SIZE / 2, 0, SIZE / 2, SIZE, green);
	glDeleteFramebuffers(1, &read_fb);

done:
	glDisable(GL_DEPTH_TEST);
	glDisable(GL_STENCIL_TEST);
	glBindFramebuffer(GL_FRAMEBUFFER, piglit_winsys_fbo);
	glDeleteFramebuffers(1, &fb);
	glDeleteRenderbuffers(1, &rb);
	glDeleteTextures(1, &tex);
	return piglit_check_gl_error(GL_NO_ERROR) && pass ? PIGLIT_PASS : PIGLIT_FAIL;
}

enum piglit_result
piglit_display(void)
{
	return msrtt_run_subtests(subtests);
}

void
piglit_init(int argc, char **argv)
{
	piglit_require_extension("GL_EXT_multisampled_render_to_texture2");

	prog = msrtt_color_program();
	tex_prog = piglit_build_simple_program(vs_texture_source, fs_texture_source);
	glUseProgram(tex_prog);
	glUniform1i(glGetUniformLocation(tex_prog, "tex"), 0);
}
