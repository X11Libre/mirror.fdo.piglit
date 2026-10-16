/*
 * SPDX-License-Identifier: MIT
 */

/**
 * Tests framebuffer completeness with EXT_multisampled_render_to_texture:
 *
 *    "The value of RENDERBUFFER_SAMPLES_EXT is the same for all attached
 *    renderbuffers; the value of TEXTURE_SAMPLES_EXT is the same for all
 *    texture attachments; and, if the attached images are a mix of
 *    renderbuffers and textures, the value of RENDERBUFFER_SAMPLES_EXT
 *    matches the value of TEXTURE_SAMPLES_EXT.
 *    FRAMEBUFFER_INCOMPLETE_MULTISAMPLE"
 *
 * and, with OpenGL ES 3.x:
 *
 *    "FBOs cannot combine attachments that have associated multisample data
 *    specified by the mechanisms described in this extension with
 *    attachments allocated using the core OpenGL ES 3.1 mechanisms, such as
 *    TexStorage2DMultisample. [...]
 *    { GL_FRAMEBUFFER_INCOMPLETE_MULTISAMPLE_EXT }"
 */

#include "common.h"

#define SIZE 16

static enum piglit_result texture_only(void *data);
static enum piglit_result matching_renderbuffer(void *data);
static enum piglit_result single_sampled_renderbuffer(void *data);
static enum piglit_result mismatched_renderbuffer(void *data);
static enum piglit_result single_sampled_texture(void *data);
static enum piglit_result core_renderbuffer(void *data);
static enum piglit_result core_renderbuffers(void *data);
static enum piglit_result core_texture(void *data);

static const struct piglit_subtest subtests[] = {
	{ "texture only", "texture-only", texture_only },
	{ "matching renderbuffer", "matching-renderbuffer", matching_renderbuffer },
	{ "single-sampled renderbuffer", "single-sampled-renderbuffer", single_sampled_renderbuffer },
	{ "mismatched renderbuffer", "mismatched-renderbuffer", mismatched_renderbuffer },
	{ "single-sampled texture", "single-sampled-texture", single_sampled_texture },
	{ "core multisample renderbuffer", "core-renderbuffer", core_renderbuffer },
	{ "core and EXT multisample renderbuffers", "core-renderbuffers", core_renderbuffers },
	{ "core multisample texture", "core-texture", core_texture },
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

static GLuint fb, tex;

static void
begin(GLint samples)
{
	glGenFramebuffers(1, &fb);
	glBindFramebuffer(GL_FRAMEBUFFER, fb);
	tex = msrtt_rgba_texture(SIZE, SIZE);
	glFramebufferTexture2DMultisampleEXT(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
					     GL_TEXTURE_2D, tex, 0, samples);
}

static GLuint
depth_renderbuffer(GLint samples)
{
	GLuint rb;

	glGenRenderbuffers(1, &rb);
	glBindRenderbuffer(GL_RENDERBUFFER, rb);
	msrtt_renderbuffer_storage(GL_RENDERBUFFER, samples,
					    GL_DEPTH_COMPONENT16, SIZE, SIZE);
	glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
				  GL_RENDERBUFFER, rb);
	return rb;
}

static enum piglit_result
end(GLenum expected)
{
	GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
	bool pass = status == expected;

	if (!pass)
		printf("status is %s, expected %s\n",
		       piglit_get_gl_enum_name(status),
		       piglit_get_gl_enum_name(expected));

	glBindFramebuffer(GL_FRAMEBUFFER, piglit_winsys_fbo);
	glDeleteFramebuffers(1, &fb);
	glDeleteTextures(1, &tex);
	return piglit_check_gl_error(GL_NO_ERROR) && pass ? PIGLIT_PASS : PIGLIT_FAIL;
}

static enum piglit_result
texture_only(void *data)
{
	begin(msrtt_samples());
	return end(GL_FRAMEBUFFER_COMPLETE);
}

static enum piglit_result
matching_renderbuffer(void *data)
{
	begin(msrtt_samples());
	depth_renderbuffer(msrtt_texture_samples(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0));
	if (msrtt_renderbuffer_samples() !=
	    msrtt_texture_samples(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0)) {
		printf("renderbuffer got %d samples for a request of %d\n",
		       msrtt_renderbuffer_samples(),
		       msrtt_texture_samples(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0));
		end(GL_FRAMEBUFFER_COMPLETE);
		return PIGLIT_FAIL;
	}
	return end(GL_FRAMEBUFFER_COMPLETE);
}

static enum piglit_result
single_sampled_renderbuffer(void *data)
{
	begin(msrtt_samples());
	depth_renderbuffer(0);
	return end(GL_FRAMEBUFFER_INCOMPLETE_MULTISAMPLE_EXT);
}

/* Needs a depth sample count other than the color texture's. */
static enum piglit_result
mismatched_renderbuffer(void *data)
{
	GLint color, depth, max = msrtt_max_samples_for(GL_DEPTH_COMPONENT16);

	begin(msrtt_samples());
	color = msrtt_texture_samples(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0);
	depth_renderbuffer(1);
	depth = msrtt_renderbuffer_samples();
	/* requests round up to a supported count, maybe the color's */
	for (GLint s = 2; depth == color && s <= max; s++) {
		msrtt_renderbuffer_storage(GL_RENDERBUFFER, s,
					   GL_DEPTH_COMPONENT16, SIZE, SIZE);
		depth = msrtt_renderbuffer_samples();
	}
	if (depth == color) {
		printf("depth supports no sample count but the color's %d\n",
		       color);
		end(GL_FRAMEBUFFER_COMPLETE);
		return PIGLIT_SKIP;
	}

	return end(GL_FRAMEBUFFER_INCOMPLETE_MULTISAMPLE_EXT);
}

static enum piglit_result
single_sampled_texture(void *data)
{
	begin(0);
	depth_renderbuffer(msrtt_samples());
	return end(GL_FRAMEBUFFER_INCOMPLETE_MULTISAMPLE_EXT);
}

static enum piglit_result
core_renderbuffer(void *data)
{
	GLuint rb;

	if (piglit_get_gl_version() < 30)
		return PIGLIT_SKIP;

	begin(msrtt_samples());
	glGenRenderbuffers(1, &rb);
	glBindRenderbuffer(GL_RENDERBUFFER, rb);
	glRenderbufferStorageMultisample(GL_RENDERBUFFER,
					 msrtt_texture_samples(GL_FRAMEBUFFER,
							       GL_COLOR_ATTACHMENT0),
					 GL_DEPTH_COMPONENT16, SIZE, SIZE);
	glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
				  GL_RENDERBUFFER, rb);
	return end(GL_FRAMEBUFFER_INCOMPLETE_MULTISAMPLE_EXT);
}

static enum piglit_result
core_renderbuffers(void *data)
{
	GLuint color, depth;
	GLint samples;

	if (piglit_get_gl_version() < 30)
		return PIGLIT_SKIP;

	glGenFramebuffers(1, &fb);
	glBindFramebuffer(GL_FRAMEBUFFER, fb);
	tex = 0;

	glGenRenderbuffers(1, &color);
	glBindRenderbuffer(GL_RENDERBUFFER, color);
	msrtt_renderbuffer_storage(GL_RENDERBUFFER, msrtt_samples(),
					    GL_RGBA8, SIZE, SIZE);
	samples = msrtt_renderbuffer_samples();
	glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
				  GL_RENDERBUFFER, color);

	glGenRenderbuffers(1, &depth);
	glBindRenderbuffer(GL_RENDERBUFFER, depth);
	glRenderbufferStorageMultisample(GL_RENDERBUFFER, samples,
					 GL_DEPTH_COMPONENT16, SIZE, SIZE);
	glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
				  GL_RENDERBUFFER, depth);
	return end(GL_FRAMEBUFFER_INCOMPLETE_MULTISAMPLE_EXT);
}

static enum piglit_result
core_texture(void *data)
{
	GLuint depth;

	if (piglit_get_gl_version() < 31)
		return PIGLIT_SKIP;

	begin(msrtt_samples());
	glGenTextures(1, &depth);
	glBindTexture(GL_TEXTURE_2D_MULTISAMPLE, depth);
	glTexStorage2DMultisample(GL_TEXTURE_2D_MULTISAMPLE,
				  msrtt_texture_samples(GL_FRAMEBUFFER,
							GL_COLOR_ATTACHMENT0),
				  GL_DEPTH_COMPONENT16, SIZE, SIZE, GL_TRUE);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
			       GL_TEXTURE_2D_MULTISAMPLE, depth, 0);
	return end(GL_FRAMEBUFFER_INCOMPLETE_MULTISAMPLE_EXT);
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
