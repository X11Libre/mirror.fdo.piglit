/*
 * SPDX-License-Identifier: MIT
 */

/**
 * Tests the queries of EXT_multisampled_render_to_texture:
 *
 * - MAX_SAMPLES_EXT is at least 2, through each GetInteger/Boolean/Float.
 *
 * - RENDERBUFFER_SAMPLES_EXT starts at 0, is 0 after RenderbufferStorage and
 *   after RenderbufferStorageMultisampleEXT with 0 samples, and otherwise
 *   "is guaranteed to be greater than or equal to samples and no more than
 *   the next larger sample count supported by the implementation", so asking
 *   for the count it returns gives that count again.
 *
 * - FRAMEBUFFER_ATTACHMENT_TEXTURE_SAMPLES_EXT follows the same rules for
 *   FramebufferTexture2DMultisampleEXT, is 0 for FramebufferTexture2D, and
 *   reattaching replaces it.
 *
 * - A complete framebuffer with such an attachment has SAMPLE_BUFFERS 1 and
 *   SAMPLES equal to the attachment's sample count.
 */

#include "common.h"

static enum piglit_result max_samples(void *data);
static enum piglit_result renderbuffer_samples(void *data);
static enum piglit_result texture_samples(void *data);
static enum piglit_result framebuffer_samples(void *data);

static const struct piglit_subtest subtests[] = {
	{ "MAX_SAMPLES_EXT", "max-samples", max_samples },
	{ "RENDERBUFFER_SAMPLES_EXT", "renderbuffer-samples", renderbuffer_samples },
	{ "FRAMEBUFFER_ATTACHMENT_TEXTURE_SAMPLES_EXT", "texture-samples", texture_samples },
	{ "framebuffer SAMPLES and SAMPLE_BUFFERS", "framebuffer-samples", framebuffer_samples },
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

static enum piglit_result
max_samples(void *data)
{
	GLint i = 0;
	GLboolean b = GL_FALSE;
	GLfloat f = 0;
	bool pass = true;

	glGetIntegerv(GL_MAX_SAMPLES_EXT, &i);
	glGetBooleanv(GL_MAX_SAMPLES_EXT, &b);
	glGetFloatv(GL_MAX_SAMPLES_EXT, &f);

	if (i < 2) {
		printf("MAX_SAMPLES_EXT is %d, the minimum is 2\n", i);
		pass = false;
	}
	if (b != GL_TRUE || f != (float)i) {
		printf("MAX_SAMPLES_EXT is %d as an integer but %d as a boolean "
		       "and %f as a float\n", i, b, f);
		pass = false;
	}
	return piglit_check_gl_error(GL_NO_ERROR) && pass ? PIGLIT_PASS : PIGLIT_FAIL;
}

/* Checks a sample count got for a request against the spec's rules; the
 * count got for a request of got itself is in again.
 */
static bool
check_count(const char *what, GLint requested, GLint got, GLint again,
	    GLint max)
{
	if (requested == 0 && got != 0) {
		printf("%s: requested 0 samples, got %d\n", what, got);
		return false;
	}
	if (requested > 0 && (got < requested || got > max)) {
		printf("%s: requested %d samples, got %d (maximum %d)\n",
		       what, requested, got, max);
		return false;
	}
	if (again != got) {
		printf("%s: requested %d samples and got %d, but requesting %d "
		       "gives %d, so %d is not the next supported count\n",
		       what, requested, got, got, again, got);
		return false;
	}
	return true;
}

static enum piglit_result
renderbuffer_samples(void *data)
{
	static const GLenum formats[] = {
		GL_RGBA4, GL_RGB565, GL_DEPTH_COMPONENT16, GL_STENCIL_INDEX8,
	};
	GLuint rb;
	bool pass = true;

	glGenRenderbuffers(1, &rb);
	glBindRenderbuffer(GL_RENDERBUFFER, rb);
	if (msrtt_renderbuffer_samples() != 0) {
		printf("initial RENDERBUFFER_SAMPLES_EXT is %d\n",
		       msrtt_renderbuffer_samples());
		pass = false;
	}

	for (unsigned f = 0; f < ARRAY_SIZE(formats); f++) {
		const char *name = piglit_get_gl_enum_name(formats[f]);

		GLint max = msrtt_max_samples_for(formats[f]);

		for (GLint s = 0; s <= max; s++) {
			GLint got, again;

			msrtt_renderbuffer_storage(GL_RENDERBUFFER, s,
							    formats[f], 16, 16);
			got = msrtt_renderbuffer_samples();
			msrtt_renderbuffer_storage(GL_RENDERBUFFER, got,
							    formats[f], 16, 16);
			again = msrtt_renderbuffer_samples();
			pass &= check_count(name, s, got, again, max);
		}

		glRenderbufferStorage(GL_RENDERBUFFER, formats[f], 16, 16);
		if (msrtt_renderbuffer_samples() != 0) {
			printf("%s: RenderbufferStorage leaves "
			       "RENDERBUFFER_SAMPLES_EXT %d\n", name,
			       msrtt_renderbuffer_samples());
			pass = false;
		}
	}

	glDeleteRenderbuffers(1, &rb);
	return piglit_check_gl_error(GL_NO_ERROR) && pass ? PIGLIT_PASS : PIGLIT_FAIL;
}

static enum piglit_result
texture_samples(void *data)
{
	GLuint fb, tex = msrtt_rgba_texture(16, 16);
	bool pass = true;

	glGenFramebuffers(1, &fb);
	glBindFramebuffer(GL_FRAMEBUFFER, fb);

	GLint max = msrtt_max_samples_for(GL_RGBA8);

	for (GLint s = 0; s <= max; s++) {
		GLint got, again;

		glFramebufferTexture2DMultisampleEXT(GL_FRAMEBUFFER,
						     GL_COLOR_ATTACHMENT0,
						     GL_TEXTURE_2D, tex, 0, s);
		got = msrtt_texture_samples(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0);
		glFramebufferTexture2DMultisampleEXT(GL_FRAMEBUFFER,
						     GL_COLOR_ATTACHMENT0,
						     GL_TEXTURE_2D, tex, 0, got);
		again = msrtt_texture_samples(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0);
		pass &= check_count("texture", s, got, again, max);
	}

	/* a plain attachment replaces the multisampled one */
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
			       GL_TEXTURE_2D, tex, 0);
	if (msrtt_texture_samples(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0) != 0) {
		printf("FramebufferTexture2D leaves TEXTURE_SAMPLES_EXT %d\n",
		       msrtt_texture_samples(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0));
		pass = false;
	}

	glBindFramebuffer(GL_FRAMEBUFFER, piglit_winsys_fbo);
	glDeleteFramebuffers(1, &fb);
	glDeleteTextures(1, &tex);
	return piglit_check_gl_error(GL_NO_ERROR) && pass ? PIGLIT_PASS : PIGLIT_FAIL;
}

static enum piglit_result
framebuffer_samples(void *data)
{
	GLuint fb, tex = msrtt_rgba_texture(16, 16);
	GLint samples, buffers, attachment;
	bool pass = true;

	glGenFramebuffers(1, &fb);
	glBindFramebuffer(GL_FRAMEBUFFER, fb);
	glFramebufferTexture2DMultisampleEXT(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
					     GL_TEXTURE_2D, tex, 0,
					     msrtt_samples());
	if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
		printf("RGBA8 with MAX_SAMPLES_EXT samples is incomplete\n");
		return PIGLIT_FAIL;
	}

	attachment = msrtt_texture_samples(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0);
	glGetIntegerv(GL_SAMPLES, &samples);
	glGetIntegerv(GL_SAMPLE_BUFFERS, &buffers);
	if (buffers != 1 || samples != attachment) {
		printf("multisampled attachment with %d samples: SAMPLES %d, "
		       "SAMPLE_BUFFERS %d\n", attachment, samples, buffers);
		pass = false;
	}

	glFramebufferTexture2DMultisampleEXT(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
					     GL_TEXTURE_2D, tex, 0, 0);
	glGetIntegerv(GL_SAMPLES, &samples);
	glGetIntegerv(GL_SAMPLE_BUFFERS, &buffers);
	if (buffers != 0 || samples != 0) {
		printf("attachment with 0 samples: SAMPLES %d, SAMPLE_BUFFERS %d\n",
		       samples, buffers);
		pass = false;
	}

	glBindFramebuffer(GL_FRAMEBUFFER, piglit_winsys_fbo);
	glDeleteFramebuffers(1, &fb);
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
	piglit_require_extension("GL_EXT_multisampled_render_to_texture");
}
