/*
 * SPDX-License-Identifier: MIT
 */

/**
 * Tests rendering with depth and stencil textures attached by
 * FramebufferTexture2DMultisampleEXT, which
 * EXT_multisampled_render_to_texture2 allows:
 *
 *    "This means values like GL_COLOR_ATTACHMENTi, GL_DEPTH_ATTACHMENT,
 *    GL_STENCIL_ATTACHMENT, or GL_DEPTH_STENCIL_ATTACHMENT may be used."
 *
 * A red rectangle over the left half writes depth or stencil, then a green
 * one over everything that the depth or stencil test keeps out of the left
 * half, all in one pass, and the color texture must have red on the left and
 * green on the right.  The depth and stencil contents are not checked, since
 *
 *    "If the multisampled buffer is a depth or stencil texture, then it is
 *    discarded - equivalent to the application calling
 *    InvalidateFramebuffer for this attachment."
 *
 * The "mismatched" subtest checks that a depth texture with a different
 * sample count than the color texture makes the framebuffer
 * FRAMEBUFFER_INCOMPLETE_MULTISAMPLE.
 */

#include "../ext_multisampled_render_to_texture/common.h"

#define SIZE 64

static enum piglit_result depth(void *data);
static enum piglit_result depth_stencil(void *data);
static enum piglit_result mismatched(void *data);

static const struct piglit_subtest subtests[] = {
	{ "depth texture", "depth", depth },
	{ "depth/stencil texture", "depth-stencil", depth_stencil },
	{ "mismatched sample counts", "mismatched", mismatched },
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
static GLuint fb, color, ds;

static GLuint
depth_texture(void)
{
	if (piglit_get_gl_version() >= 30)
		return msrtt_texture(GL_DEPTH_COMPONENT24, GL_DEPTH_COMPONENT,
				     GL_UNSIGNED_INT, SIZE, SIZE);
	if (piglit_is_extension_supported("GL_OES_depth_texture"))
		return msrtt_texture(GL_DEPTH_COMPONENT, GL_DEPTH_COMPONENT,
				     GL_UNSIGNED_INT, SIZE, SIZE);
	return 0;
}

static GLuint
depth_stencil_texture(void)
{
	if (piglit_get_gl_version() >= 30)
		return msrtt_texture(GL_DEPTH24_STENCIL8, GL_DEPTH_STENCIL,
				     GL_UNSIGNED_INT_24_8, SIZE, SIZE);
	return 0;
}

/* Attaches a multisampled color texture, and ds to the given attachments
 * with ds_samples samples, or with the color texture's count when negative.
 */
static GLint
begin(const GLenum *attachments, unsigned count, GLint ds_samples)
{
	GLint samples;

	glGenFramebuffers(1, &fb);
	glBindFramebuffer(GL_FRAMEBUFFER, fb);
	color = msrtt_rgba_texture(SIZE, SIZE);
	glFramebufferTexture2DMultisampleEXT(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
					     GL_TEXTURE_2D, color, 0,
					     msrtt_samples());
	samples = msrtt_texture_samples(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0);
	for (unsigned i = 0; i < count; i++)
		glFramebufferTexture2DMultisampleEXT(GL_FRAMEBUFFER, attachments[i],
						     GL_TEXTURE_2D, ds, 0,
						     ds_samples < 0 ? samples : ds_samples);
	return samples;
}

static enum piglit_result
end(bool pass)
{
	glDisable(GL_DEPTH_TEST);
	glDisable(GL_STENCIL_TEST);
	glBindFramebuffer(GL_FRAMEBUFFER, piglit_winsys_fbo);
	glDeleteFramebuffers(1, &fb);
	glDeleteTextures(1, &color);
	glDeleteTextures(1, &ds);
	return piglit_check_gl_error(GL_NO_ERROR) && pass ? PIGLIT_PASS : PIGLIT_FAIL;
}

static bool
check_complete(void)
{
	GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);

	if (status != GL_FRAMEBUFFER_COMPLETE) {
		printf("framebuffer status is %s\n", piglit_get_gl_enum_name(status));
		return false;
	}
	glViewport(0, 0, SIZE, SIZE);
	glClearColor(0, 0, 0, 1);
	glClearDepthf(1.0);
	glClearStencil(0);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
	return true;
}

/* left half red, right half green, read through the color texture */
static bool
check_result(void)
{
	GLuint read_fb = msrtt_read_framebuffer(color);
	bool pass = true;

	pass &= piglit_probe_rect_rgba(0, 0, SIZE / 2, SIZE, red);
	pass &= piglit_probe_rect_rgba(SIZE / 2, 0, SIZE / 2, SIZE, green);
	glDeleteFramebuffers(1, &read_fb);
	return pass;
}

static enum piglit_result
depth(void *data)
{
	static const GLenum attachments[] = { GL_DEPTH_ATTACHMENT };

	ds = depth_texture();
	if (!ds)
		return PIGLIT_SKIP;
	begin(attachments, 1, -1);
	if (!check_complete())
		return end(false);

	glEnable(GL_DEPTH_TEST);
	glDepthFunc(GL_LESS);
	msrtt_use(&prog, red, -0.5);
	piglit_draw_rect(-1, -1, 1, 2);
	msrtt_use(&prog, green, 0.5);
	piglit_draw_rect(-1, -1, 2, 2);
	return end(check_result());
}

static enum piglit_result
depth_stencil(void *data)
{
	static const GLenum attachments[] = { GL_DEPTH_STENCIL_ATTACHMENT };

	ds = depth_stencil_texture();
	if (!ds)
		return PIGLIT_SKIP;
	begin(attachments, 1, -1);
	if (!check_complete())
		return end(false);

	/* stencil keeps green out of the left half; depth has to pass it
	 * elsewhere
	 */
	glEnable(GL_DEPTH_TEST);
	glDepthFunc(GL_LESS);
	glEnable(GL_STENCIL_TEST);
	glStencilFunc(GL_ALWAYS, 1, 0xff);
	glStencilOp(GL_KEEP, GL_KEEP, GL_REPLACE);
	msrtt_use(&prog, red, -0.5);
	piglit_draw_rect(-1, -1, 1, 2);
	glStencilFunc(GL_NOTEQUAL, 1, 0xff);
	glStencilOp(GL_KEEP, GL_KEEP, GL_KEEP);
	msrtt_use(&prog, green, 0.5);
	piglit_draw_rect(-1, -1, 2, 2);
	return end(check_result());
}

static enum piglit_result
mismatched(void *data)
{
	static const GLenum attachments[] = { GL_DEPTH_ATTACHMENT };
	GLenum status;

	ds = depth_texture();
	if (!ds)
		return PIGLIT_SKIP;

	/* a single-sampled depth texture with a multisampled color one */
	begin(attachments, 1, 0);
	status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
	if (status != GL_FRAMEBUFFER_INCOMPLETE_MULTISAMPLE_EXT) {
		printf("status is %s, expected "
		       "GL_FRAMEBUFFER_INCOMPLETE_MULTISAMPLE_EXT\n",
		       piglit_get_gl_enum_name(status));
		return end(false);
	}
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
	piglit_require_extension("GL_EXT_multisampled_render_to_texture2");

	prog = msrtt_color_program();
}
