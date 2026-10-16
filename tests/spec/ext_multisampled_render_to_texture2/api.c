/*
 * SPDX-License-Identifier: MIT
 */

/**
 * Tests the attachments EXT_multisampled_render_to_texture2 allows:
 *
 *    "The <attachment> parameters for FramebufferTexture2DMultisampleEXT is
 *    no longer required to be COLOR_ATTACHMENT0.  The attachment parameter
 *    now matches what is allowed in FramebufferTexture2D."
 *
 * The "errors" subtest checks that FramebufferTexture2DMultisampleEXT gives
 * the same error as FramebufferTexture2D for each attachment, valid or not.
 * The "samples" subtest checks FRAMEBUFFER_ATTACHMENT_TEXTURE_SAMPLES_EXT on
 * depth, stencil, and other color attachments.
 */

#include "../ext_multisampled_render_to_texture/common.h"

static enum piglit_result errors(void *data);
static enum piglit_result samples(void *data);

static const struct piglit_subtest subtests[] = {
	{ "same errors as FramebufferTexture2D", "errors", errors },
	{ "attachment sample counts", "samples", samples },
	{ NULL }
};

static const struct piglit_gl_test_config *piglit_config;

PIGLIT_GL_TEST_CONFIG_BEGIN

	config.supports_gl_es_version = 20;
	config.window_visual = PIGLIT_GL_VISUAL_RGBA;
	config.khr_no_error_support = PIGLIT_HAS_ERRORS;
	config.subtests = subtests;
	piglit_config = &config;

PIGLIT_GL_TEST_CONFIG_END

static GLint max_color_attachments = 1;
static bool has_depth_stencil_attachment;

static enum piglit_result
errors(void *data)
{
	GLenum attachments[8];
	unsigned count = 0;
	GLuint fb, tex = msrtt_rgba_texture(16, 16);
	bool pass = true;

	attachments[count++] = GL_COLOR_ATTACHMENT0;
	attachments[count++] = GL_COLOR_ATTACHMENT0 + max_color_attachments - 1;
	attachments[count++] = GL_COLOR_ATTACHMENT0 + max_color_attachments;
	attachments[count++] = GL_DEPTH_ATTACHMENT;
	attachments[count++] = GL_STENCIL_ATTACHMENT;
	if (has_depth_stencil_attachment)
		attachments[count++] = GL_DEPTH_STENCIL_ATTACHMENT;
	attachments[count++] = GL_TEXTURE_2D;

	glGenFramebuffers(1, &fb);
	glBindFramebuffer(GL_FRAMEBUFFER, fb);
	for (unsigned i = 0; i < count; i++) {
		GLenum plain, ms;

		glFramebufferTexture2D(GL_FRAMEBUFFER, attachments[i],
				       GL_TEXTURE_2D, tex, 0);
		plain = glGetError();
		glFramebufferTexture2D(GL_FRAMEBUFFER, attachments[i],
				       GL_TEXTURE_2D, 0, 0);
		glGetError();

		glFramebufferTexture2DMultisampleEXT(GL_FRAMEBUFFER, attachments[i],
						     GL_TEXTURE_2D, tex, 0,
						     msrtt_samples());
		ms = glGetError();
		glFramebufferTexture2D(GL_FRAMEBUFFER, attachments[i],
				       GL_TEXTURE_2D, 0, 0);
		glGetError();

		if (plain != ms) {
			printf("%s: FramebufferTexture2D gives %s, "
			       "FramebufferTexture2DMultisampleEXT gives %s\n",
			       piglit_get_gl_enum_name(attachments[i]),
			       piglit_get_gl_enum_name(plain),
			       piglit_get_gl_enum_name(ms));
			pass = false;
		}
	}

	glBindFramebuffer(GL_FRAMEBUFFER, piglit_winsys_fbo);
	glDeleteFramebuffers(1, &fb);
	glDeleteTextures(1, &tex);
	return pass ? PIGLIT_PASS : PIGLIT_FAIL;
}

/* Attaches tex to attachment with one sample, which every format supports
 * when it supports multisampling at all, and checks the attachment's sample
 * count.
 */
static bool
check_samples(GLenum attachment, GLuint tex)
{
	GLint got;

	glFramebufferTexture2DMultisampleEXT(GL_FRAMEBUFFER, attachment,
					     GL_TEXTURE_2D, tex, 0, 1);
	got = msrtt_texture_samples(GL_FRAMEBUFFER, attachment);
	if (got < 1 || got > msrtt_max_samples()) {
		printf("%s: requested 1 sample, got %d\n",
		       piglit_get_gl_enum_name(attachment), got);
		return false;
	}
	return piglit_check_gl_error(GL_NO_ERROR);
}

static enum piglit_result
samples(void *data)
{
	GLuint fb, color, depth = 0, depth_stencil = 0;
	bool pass = true;

	glGenFramebuffers(1, &fb);
	glBindFramebuffer(GL_FRAMEBUFFER, fb);

	color = msrtt_rgba_texture(16, 16);
	if (max_color_attachments > 1)
		pass &= check_samples(GL_COLOR_ATTACHMENT0 + max_color_attachments - 1,
				      color);

	if (piglit_get_gl_version() >= 30) {
		depth = msrtt_texture(GL_DEPTH_COMPONENT24, GL_DEPTH_COMPONENT,
				      GL_UNSIGNED_INT, 16, 16);
		depth_stencil = msrtt_texture(GL_DEPTH24_STENCIL8, GL_DEPTH_STENCIL,
					      GL_UNSIGNED_INT_24_8, 16, 16);
	} else if (piglit_is_extension_supported("GL_OES_depth_texture")) {
		depth = msrtt_texture(GL_DEPTH_COMPONENT, GL_DEPTH_COMPONENT,
				      GL_UNSIGNED_INT, 16, 16);
	}

	if (depth)
		pass &= check_samples(GL_DEPTH_ATTACHMENT, depth);
	if (depth_stencil) {
		pass &= check_samples(GL_STENCIL_ATTACHMENT, depth_stencil);
		pass &= check_samples(GL_DEPTH_STENCIL_ATTACHMENT, depth_stencil);
	}

	glBindFramebuffer(GL_FRAMEBUFFER, piglit_winsys_fbo);
	glDeleteFramebuffers(1, &fb);
	glDeleteTextures(1, &color);
	glDeleteTextures(1, &depth);
	glDeleteTextures(1, &depth_stencil);

	if (max_color_attachments == 1 && !depth)
		return PIGLIT_SKIP;
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

	if (piglit_get_gl_version() >= 30 ||
	    piglit_is_extension_supported("GL_EXT_draw_buffers") ||
	    piglit_is_extension_supported("GL_NV_fbo_color_attachments"))
		glGetIntegerv(GL_MAX_COLOR_ATTACHMENTS, &max_color_attachments);
	has_depth_stencil_attachment = piglit_get_gl_version() >= 30;
}
