/*
 * SPDX-License-Identifier: MIT
 */

/**
 * Tests multisampled depth and stencil renderbuffers from
 * RenderbufferStorageMultisampleEXT with a texture attached by
 * FramebufferTexture2DMultisampleEXT, as in the spec's sample code:
 *
 *    "In order to allow the use of multisampled depth and stencil buffers
 *    when performing multisampled rendering to a texture, the extension
 *    also adds the command RenderbufferStorageMultisampleEXT."
 *
 * Each subtest draws a red rectangle over the left half that writes depth or
 * stencil, then a green one over everything that the depth or stencil test
 * keeps out of the left half, all in one pass.
 */

#include "common.h"

#define SIZE 64

static enum piglit_result depth(void *data);
static enum piglit_result stencil(void *data);
static enum piglit_result depth_stencil(void *data);

static const struct piglit_subtest subtests[] = {
	{ "depth", "depth", depth },
	{ "stencil", "stencil", stencil },
	{ "depth-stencil", "depth-stencil", depth_stencil },
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
static GLuint fb, tex, rb;

/* Attaches a multisampled color texture and a renderbuffer with the same
 * sample count to each of the given attachments.
 */
static bool
begin(GLenum internalformat, const GLenum *attachments, unsigned count)
{
	GLint samples;

	glGenFramebuffers(1, &fb);
	glBindFramebuffer(GL_FRAMEBUFFER, fb);
	tex = msrtt_rgba_texture(SIZE, SIZE);
	glFramebufferTexture2DMultisampleEXT(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
					     GL_TEXTURE_2D, tex, 0,
					     msrtt_samples());
	samples = msrtt_texture_samples(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0);

	glGenRenderbuffers(1, &rb);
	glBindRenderbuffer(GL_RENDERBUFFER, rb);
	msrtt_renderbuffer_storage(GL_RENDERBUFFER, samples,
					    internalformat, SIZE, SIZE);
	for (unsigned i = 0; i < count; i++)
		glFramebufferRenderbuffer(GL_FRAMEBUFFER, attachments[i],
					  GL_RENDERBUFFER, rb);

	if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
		printf("%s with %d samples is incomplete\n",
		       piglit_get_gl_enum_name(internalformat), samples);
		return false;
	}

	glViewport(0, 0, SIZE, SIZE);
	glClearColor(0, 0, 0, 1);
	glClearDepthf(1.0);
	glClearStencil(0);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
	return true;
}

static enum piglit_result
end(bool pass)
{
	/* left half red, right half green, read through the texture */
	glBindFramebuffer(GL_FRAMEBUFFER, piglit_winsys_fbo);
	glDeleteFramebuffers(1, &fb);
	fb = msrtt_read_framebuffer(tex);
	pass &= piglit_probe_rect_rgba(0, 0, SIZE / 2, SIZE, red);
	pass &= piglit_probe_rect_rgba(SIZE / 2, 0, SIZE / 2, SIZE, green);

	glDisable(GL_DEPTH_TEST);
	glDisable(GL_STENCIL_TEST);
	glBindFramebuffer(GL_FRAMEBUFFER, piglit_winsys_fbo);
	glDeleteFramebuffers(1, &fb);
	glDeleteRenderbuffers(1, &rb);
	glDeleteTextures(1, &tex);
	return piglit_check_gl_error(GL_NO_ERROR) && pass ? PIGLIT_PASS : PIGLIT_FAIL;
}

static void
draw_rects(void)
{
	msrtt_use(&prog, red, -0.5);
	piglit_draw_rect(-1, -1, 1, 2);
	msrtt_use(&prog, green, 0.5);
	piglit_draw_rect(-1, -1, 2, 2);
}

static enum piglit_result
depth(void *data)
{
	static const GLenum attachments[] = { GL_DEPTH_ATTACHMENT };

	if (!begin(GL_DEPTH_COMPONENT16, attachments, 1))
		return end(false);

	glEnable(GL_DEPTH_TEST);
	glDepthFunc(GL_LESS);
	draw_rects();
	return end(true);
}

static void
draw_stencil_rects(void)
{
	glEnable(GL_STENCIL_TEST);
	glStencilFunc(GL_ALWAYS, 1, 0xff);
	glStencilOp(GL_KEEP, GL_KEEP, GL_REPLACE);
	msrtt_use(&prog, red, 0);
	piglit_draw_rect(-1, -1, 1, 2);
	glStencilFunc(GL_NOTEQUAL, 1, 0xff);
	glStencilOp(GL_KEEP, GL_KEEP, GL_KEEP);
	msrtt_use(&prog, green, 0);
	piglit_draw_rect(-1, -1, 2, 2);
}

static enum piglit_result
stencil(void *data)
{
	static const GLenum attachments[] = { GL_STENCIL_ATTACHMENT };

	if (!begin(GL_STENCIL_INDEX8, attachments, 1))
		return end(false);

	draw_stencil_rects();
	return end(true);
}

/* Packed depth/stencil, from OES_packed_depth_stencil or ES 3.0. */
static enum piglit_result
depth_stencil(void *data)
{
	static const GLenum attachments[] = {
		GL_DEPTH_ATTACHMENT, GL_STENCIL_ATTACHMENT,
	};

	if (piglit_get_gl_version() < 30 &&
	    !piglit_is_extension_supported("GL_OES_packed_depth_stencil"))
		return PIGLIT_SKIP;

	if (!begin(GL_DEPTH24_STENCIL8, attachments, 2))
		return end(false);

	/* the stencil test keeps green out of the left half, and the depth
	 * test, which red passed, has to pass green elsewhere
	 */
	glEnable(GL_DEPTH_TEST);
	glDepthFunc(GL_LESS);
	draw_stencil_rects();
	return end(true);
}

enum piglit_result
piglit_display(void)
{
	return msrtt_run_subtests(subtests);
}

void
piglit_init(int argc, char **argv)
{
	piglit_require_extension("GL_EXT_multisampled_render_to_texture");

	prog = msrtt_color_program();
}
