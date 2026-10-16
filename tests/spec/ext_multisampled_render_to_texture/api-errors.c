/*
 * SPDX-License-Identifier: MIT
 */

/**
 * Tests the errors of RenderbufferStorageMultisampleEXT and
 * FramebufferTexture2DMultisampleEXT from the
 * EXT_multisampled_render_to_texture spec:
 *
 *    "If RenderbufferStorageMultisampleEXT is called with a value of
 *    <samples> that is greater than MAX_SAMPLES_EXT, then the error
 *    INVALID_VALUE is generated.
 *
 *    The error INVALID_ENUM is generated if FramebufferTexture2DMultisampleEXT
 *    is called with a <target> that is not FRAMEBUFFER, DRAW_FRAMEBUFFER, or
 *    READ_FRAMEBUFFER.
 *
 *    The error INVALID_ENUM is generated if FramebufferTexture2DMultisampleEXT
 *    is called with an <attachment> that is not COLOR_ATTACHMENT0.
 *
 *    The error INVALID_ENUM is generated if FramebufferTexture2DMultisampleEXT
 *    is called with a <textarget> that is not TEXTURE_2D,
 *    TEXTURE_CUBE_MAP_POSITIVE_X, [...]
 *
 *    The error INVALID_OPERATION is generated if
 *    FramebufferTexture2DMultisampleEXT is called with <samples> greater than
 *    the maximum number of samples supported for <target> and its
 *    internalformat."
 *
 * and from its description of the commands, which also follow the usual
 * ES 2.0 rule that a negative sizei is INVALID_VALUE.
 *
 * EXT_multisampled_render_to_texture2 removes the COLOR_ATTACHMENT0
 * restriction, so that subtest only checks other attachments without it.
 */

#include "common.h"

static enum piglit_result renderbuffer_samples(void *data);
static enum piglit_result renderbuffer_size(void *data);
static enum piglit_result renderbuffer_negative(void *data);
static enum piglit_result renderbuffer_target(void *data);
static enum piglit_result texture_target(void *data);
static enum piglit_result texture_attachment(void *data);
static enum piglit_result texture_textarget(void *data);
static enum piglit_result texture_samples(void *data);
static enum piglit_result texture_negative(void *data);
static enum piglit_result texture_format_samples(void *data);

static const struct piglit_subtest subtests[] = {
	{ "renderbuffer samples above MAX_SAMPLES_EXT", "renderbuffer-samples", renderbuffer_samples },
	{ "renderbuffer size above MAX_RENDERBUFFER_SIZE", "renderbuffer-size", renderbuffer_size },
	{ "renderbuffer negative samples or size", "renderbuffer-negative", renderbuffer_negative },
	{ "renderbuffer bad target", "renderbuffer-target", renderbuffer_target },
	{ "texture bad target", "texture-target", texture_target },
	{ "texture bad attachment", "texture-attachment", texture_attachment },
	{ "texture bad textarget", "texture-textarget", texture_textarget },
	{ "texture samples above MAX_SAMPLES_EXT", "texture-samples", texture_samples },
	{ "texture negative samples", "texture-negative", texture_negative },
	{ "texture samples above the format maximum", "texture-format-samples", texture_format_samples },
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

static GLuint tex;

static void
setup_renderbuffer(void)
{
	GLuint rb;

	glGenRenderbuffers(1, &rb);
	glBindRenderbuffer(GL_RENDERBUFFER, rb);
}

static void
setup_framebuffer(void)
{
	GLuint fb;

	glGenFramebuffers(1, &fb);
	glBindFramebuffer(GL_FRAMEBUFFER, fb);
}

static enum piglit_result
renderbuffer_samples(void *data)
{
	setup_renderbuffer();
	msrtt_renderbuffer_storage(GL_RENDERBUFFER, msrtt_max_samples() + 1,
					    GL_RGBA4, 16, 16);
	return piglit_check_gl_error(GL_INVALID_VALUE) ? PIGLIT_PASS : PIGLIT_FAIL;
}

static enum piglit_result
renderbuffer_size(void *data)
{
	GLint max_size;
	bool pass = true;

	glGetIntegerv(GL_MAX_RENDERBUFFER_SIZE, &max_size);
	setup_renderbuffer();
	msrtt_renderbuffer_storage(GL_RENDERBUFFER, 1, GL_RGBA4,
					    max_size + 1, 16);
	pass &= piglit_check_gl_error(GL_INVALID_VALUE);
	msrtt_renderbuffer_storage(GL_RENDERBUFFER, 1, GL_RGBA4,
					    16, max_size + 1);
	pass &= piglit_check_gl_error(GL_INVALID_VALUE);
	return pass ? PIGLIT_PASS : PIGLIT_FAIL;
}

static enum piglit_result
renderbuffer_negative(void *data)
{
	bool pass = true;

	setup_renderbuffer();
	msrtt_renderbuffer_storage(GL_RENDERBUFFER, -1, GL_RGBA4, 16, 16);
	pass &= piglit_check_gl_error(GL_INVALID_VALUE);
	msrtt_renderbuffer_storage(GL_RENDERBUFFER, 1, GL_RGBA4, -1, 16);
	pass &= piglit_check_gl_error(GL_INVALID_VALUE);
	msrtt_renderbuffer_storage(GL_RENDERBUFFER, 1, GL_RGBA4, 16, -1);
	pass &= piglit_check_gl_error(GL_INVALID_VALUE);
	return pass ? PIGLIT_PASS : PIGLIT_FAIL;
}

static enum piglit_result
renderbuffer_target(void *data)
{
	setup_renderbuffer();
	msrtt_renderbuffer_storage(GL_TEXTURE_2D, 1, GL_RGBA4, 16, 16);
	return piglit_check_gl_error(GL_INVALID_ENUM) ? PIGLIT_PASS : PIGLIT_FAIL;
}

static enum piglit_result
texture_target(void *data)
{
	setup_framebuffer();
	glFramebufferTexture2DMultisampleEXT(GL_TEXTURE_2D, GL_COLOR_ATTACHMENT0,
					     GL_TEXTURE_2D, tex, 0, 1);
	return piglit_check_gl_error(GL_INVALID_ENUM) ? PIGLIT_PASS : PIGLIT_FAIL;
}

static enum piglit_result
texture_attachment(void *data)
{
	bool pass = true;

	setup_framebuffer();

	/* never an attachment */
	glFramebufferTexture2DMultisampleEXT(GL_FRAMEBUFFER, GL_TEXTURE_2D,
					     GL_TEXTURE_2D, tex, 0, 1);
	pass &= piglit_check_gl_error(GL_INVALID_ENUM);

	/* allowed by EXT_multisampled_render_to_texture2 */
	if (!piglit_is_extension_supported("GL_EXT_multisampled_render_to_texture2")) {
		static const GLenum attachments[] = {
			GL_DEPTH_ATTACHMENT, GL_STENCIL_ATTACHMENT,
		};

		for (unsigned i = 0; i < ARRAY_SIZE(attachments); i++) {
			glFramebufferTexture2DMultisampleEXT(GL_FRAMEBUFFER,
							     attachments[i],
							     GL_TEXTURE_2D, tex,
							     0, 1);
			pass &= piglit_check_gl_error(GL_INVALID_ENUM);
		}
	}
	return pass ? PIGLIT_PASS : PIGLIT_FAIL;
}

static enum piglit_result
texture_textarget(void *data)
{
	static const GLenum textargets[] = {
		GL_TEXTURE_CUBE_MAP, GL_RENDERBUFFER, GL_FRAMEBUFFER,
	};
	bool pass = true;

	setup_framebuffer();
	for (unsigned i = 0; i < ARRAY_SIZE(textargets); i++) {
		glFramebufferTexture2DMultisampleEXT(GL_FRAMEBUFFER,
						     GL_COLOR_ATTACHMENT0,
						     textargets[i], tex, 0, 1);
		pass &= piglit_check_gl_error(GL_INVALID_ENUM);
	}
	return pass ? PIGLIT_PASS : PIGLIT_FAIL;
}

static enum piglit_result
texture_samples(void *data)
{
	setup_framebuffer();
	glFramebufferTexture2DMultisampleEXT(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
					     GL_TEXTURE_2D, tex, 0,
					     msrtt_max_samples() + 1);
	return piglit_check_gl_error(GL_INVALID_VALUE) ? PIGLIT_PASS : PIGLIT_FAIL;
}

static enum piglit_result
texture_negative(void *data)
{
	setup_framebuffer();
	glFramebufferTexture2DMultisampleEXT(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
					     GL_TEXTURE_2D, tex, 0, -1);
	return piglit_check_gl_error(GL_INVALID_VALUE) ? PIGLIT_PASS : PIGLIT_FAIL;
}

/* Finds a color-renderable format that supports fewer samples than
 * MAX_SAMPLES_EXT, which needs GetInternalformativ from ES 3.0, and checks
 * that asking for one more than its maximum is INVALID_OPERATION.
 */
static enum piglit_result
texture_format_samples(void *data)
{
	static const struct {
		GLenum internalformat, format, type;
		const char *extension;
	} formats[] = {
		{ GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE, NULL },
		{ GL_RGB10_A2, GL_RGBA, GL_UNSIGNED_INT_2_10_10_10_REV, NULL },
		{ GL_RGB565, GL_RGB, GL_UNSIGNED_BYTE, NULL },
		{ GL_RGBA16F, GL_RGBA, GL_HALF_FLOAT, "GL_EXT_color_buffer_float" },
		{ GL_RGBA32F, GL_RGBA, GL_FLOAT, "GL_EXT_color_buffer_float" },
		{ GL_R11F_G11F_B10F, GL_RGB, GL_FLOAT, "GL_EXT_color_buffer_float" },
	};
	GLint max = msrtt_max_samples();

	if (piglit_get_gl_version() < 30) {
		printf("needs GetInternalformativ from OpenGL ES 3.0\n");
		return PIGLIT_SKIP;
	}

	for (unsigned i = 0; i < ARRAY_SIZE(formats); i++) {
		GLint format_max = 0;

		if (formats[i].extension &&
		    !piglit_is_extension_supported(formats[i].extension))
			continue;

		glGetInternalformativ(GL_RENDERBUFFER, formats[i].internalformat,
				      GL_SAMPLES, 1, &format_max);
		if (format_max <= 0 || format_max >= max)
			continue;

		GLuint t = msrtt_texture(formats[i].internalformat,
					 formats[i].format, formats[i].type,
					 16, 16);

		setup_framebuffer();
		glFramebufferTexture2DMultisampleEXT(GL_FRAMEBUFFER,
						     GL_COLOR_ATTACHMENT0,
						     GL_TEXTURE_2D, t, 0,
						     format_max + 1);
		glDeleteTextures(1, &t);
		printf("%s supports up to %d samples\n",
		       piglit_get_gl_enum_name(formats[i].internalformat),
		       format_max);
		return piglit_check_gl_error(GL_INVALID_OPERATION) ?
		       PIGLIT_PASS : PIGLIT_FAIL;
	}

	printf("every tested format supports MAX_SAMPLES_EXT samples\n");
	return PIGLIT_SKIP;
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

	tex = msrtt_rgba_texture(16, 16);
}
