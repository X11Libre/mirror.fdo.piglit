/*
 * SPDX-License-Identifier: MIT
 */

/**
 * Tests the API of OVR_multiview_multisampled_render_to_texture:
 *
 *    "<target>, <attachment>, <texture>, <level>, <baseViewIndex>, and
 *    <numViews> correspond to the same parameters for FramebufferTexture-
 *    MultiviewOVR and have the same restrictions and errors. <samples>
 *    corresponds to the same parameter for FramebufferTexture2DMultisampleEXT
 *    and has the same restrictions and errors."
 *
 * Subtests:
 * - errors: the INVALID_VALUE and INVALID_OPERATION errors in the Errors
 *   section, and that a failed call attaches nothing.
 * - queries: FRAMEBUFFER_ATTACHMENT_TEXTURE_SAMPLES_EXT follows the
 *   FramebufferTexture2DMultisampleEXT rules, and the view queries match
 *   the call; with 0 samples it is FramebufferTextureMultiviewOVR.
 * - completeness: "the number of views and the number of samples must both
 *   match across attachments" (issue 3).
 * - read-errors: reading from a read framebuffer with more than one view is
 *   INVALID_FRAMEBUFFER_OPERATION, and reading from one view is not.
 */

#include "../ovr_multiview/mv-common.h"
#include "../ext_multisampled_render_to_texture/common.h"

#define SIZE 16
#define LAYERS 4

static enum piglit_result errors(void *data);
static enum piglit_result queries(void *data);
static enum piglit_result completeness(void *data);
static enum piglit_result read_errors(void *data);

static const struct piglit_subtest subtests[] = {
	{ "errors", "errors", errors },
	{ "queries", "queries", queries },
	{ "completeness", "completeness", completeness },
	{ "read errors", "read-errors", read_errors },
	{ NULL }
};

static const struct piglit_gl_test_config *piglit_config;

PIGLIT_GL_TEST_CONFIG_BEGIN

	config.supports_gl_es_version = 30;
	config.window_visual = PIGLIT_GL_VISUAL_RGBA;
	config.khr_no_error_support = PIGLIT_HAS_ERRORS;
	config.subtests = subtests;
	piglit_config = &config;

PIGLIT_GL_TEST_CONFIG_END

static GLint
attachment_param(GLenum attachment, GLenum pname)
{
	GLint value = -1;

	glGetFramebufferAttachmentParameteriv(GL_FRAMEBUFFER, attachment, pname,
					      &value);
	return value;
}

static enum piglit_result
errors(void *data)
{
	GLint max_layers, samples = msrtt_samples(), views = mv_max_views();
	GLuint fb, tex, tex2d;
	bool pass = true;

	glGetIntegerv(GL_MAX_ARRAY_TEXTURE_LAYERS, &max_layers);
	glGenFramebuffers(1, &fb);
	glBindFramebuffer(GL_FRAMEBUFFER, fb);
	tex = mv_array_texture(GL_RGBA8, SIZE, SIZE, LAYERS);
	tex2d = msrtt_rgba_texture(SIZE, SIZE);

	glFramebufferTextureMultisampleMultiviewOVR(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
						    tex, 0, samples, 0, 0);
	pass = piglit_check_gl_error(GL_INVALID_VALUE) && pass;
	glFramebufferTextureMultisampleMultiviewOVR(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
						    tex, 0, samples, 0, views + 1);
	pass = piglit_check_gl_error(GL_INVALID_VALUE) && pass;
	glFramebufferTextureMultisampleMultiviewOVR(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
						    tex, 0, samples, max_layers - 1, 2);
	pass = piglit_check_gl_error(GL_INVALID_VALUE) && pass;
	glFramebufferTextureMultisampleMultiviewOVR(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
						    tex, 0, msrtt_max_samples() + 1, 0, 2);
	pass = piglit_check_gl_error(GL_INVALID_VALUE) && pass;
	glFramebufferTextureMultisampleMultiviewOVR(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
						    tex2d, 0, samples, 0, 2);
	pass = piglit_check_gl_error(GL_INVALID_OPERATION) && pass;

	if (attachment_param(GL_COLOR_ATTACHMENT0,
			     GL_FRAMEBUFFER_ATTACHMENT_OBJECT_TYPE) != GL_NONE) {
		printf("a failed call attached something\n");
		pass = false;
	}

	glFramebufferTextureMultisampleMultiviewOVR(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
						    tex, 0, -1, 0, 2);
	pass = piglit_check_gl_error(GL_INVALID_VALUE) && pass;

	glBindFramebuffer(GL_FRAMEBUFFER, piglit_winsys_fbo);
	glDeleteFramebuffers(1, &fb);
	glDeleteTextures(1, &tex);
	glDeleteTextures(1, &tex2d);
	return piglit_check_gl_error(GL_NO_ERROR) && pass ? PIGLIT_PASS : PIGLIT_FAIL;
}

static enum piglit_result
queries(void *data)
{
	GLuint fb, tex = mv_array_texture(GL_RGBA8, SIZE, SIZE, LAYERS);
	bool pass = true;

	glGenFramebuffers(1, &fb);
	glBindFramebuffer(GL_FRAMEBUFFER, fb);

	for (GLint s = 0; s <= msrtt_max_samples_for(GL_RGBA8); s++) {
		GLint got, again, base, num;

		glFramebufferTextureMultisampleMultiviewOVR(GL_FRAMEBUFFER,
							    GL_COLOR_ATTACHMENT0,
							    tex, 0, s, 1, 2);
		got = msrtt_texture_samples(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0);
		base = attachment_param(GL_COLOR_ATTACHMENT0,
					GL_FRAMEBUFFER_ATTACHMENT_TEXTURE_BASE_VIEW_INDEX_OVR);
		num = attachment_param(GL_COLOR_ATTACHMENT0,
				       GL_FRAMEBUFFER_ATTACHMENT_TEXTURE_NUM_VIEWS_OVR);
		if (s == 0 ? got != 0 : got < s) {
			printf("%d samples requested, %d reported\n", s, got);
			pass = false;
		}
		if (base != 1 || num != 2) {
			printf("base view %d and %d views reported, expected 1 and 2\n",
			       base, num);
			pass = false;
		}

		/* Asking for the count it reports must give that count. */
		glFramebufferTextureMultisampleMultiviewOVR(GL_FRAMEBUFFER,
							    GL_COLOR_ATTACHMENT0,
							    tex, 0, got, 1, 2);
		again = msrtt_texture_samples(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0);
		if (again != got) {
			printf("%d samples requested, %d reported\n", got, again);
			pass = false;
		}
	}

	/* FramebufferTextureMultiviewOVR replaces it with no samples. */
	glFramebufferTextureMultisampleMultiviewOVR(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
						    tex, 0, msrtt_samples(), 0, 2);
	glFramebufferTextureMultiviewOVR(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, tex,
					 0, 0, 2);
	if (msrtt_texture_samples(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0) != 0) {
		printf("FramebufferTextureMultiviewOVR kept the samples\n");
		pass = false;
	}

	glBindFramebuffer(GL_FRAMEBUFFER, piglit_winsys_fbo);
	glDeleteFramebuffers(1, &fb);
	glDeleteTextures(1, &tex);
	return piglit_check_gl_error(GL_NO_ERROR) && pass ? PIGLIT_PASS : PIGLIT_FAIL;
}

/* Attaches color and depth with the given samples and views, and checks
 * the status.
 */
static bool
check(GLuint color, GLuint depth, GLint color_samples, GLint depth_samples,
      int color_views, int depth_views, GLenum expected)
{
	glFramebufferTextureMultisampleMultiviewOVR(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
						    color, 0, color_samples, 0,
						    color_views);
	glFramebufferTextureMultisampleMultiviewOVR(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
						    depth, 0, depth_samples, 0,
						    depth_views);
	if (mv_check_status(expected))
		return true;
	printf("  color: %d samples, %d views; depth: %d samples, %d views\n",
	       color_samples, color_views, depth_samples, depth_views);
	return false;
}

static enum piglit_result
completeness(void *data)
{
	GLint max = msrtt_samples();
	GLuint fb, color, depth;
	bool pass = true;

	glGenFramebuffers(1, &fb);
	glBindFramebuffer(GL_FRAMEBUFFER, fb);
	color = mv_array_texture(GL_RGBA8, SIZE, SIZE, LAYERS);
	depth = mv_array_texture(GL_DEPTH_COMPONENT24, SIZE, SIZE, LAYERS);

	pass &= check(color, depth, max, max, 2, 2, GL_FRAMEBUFFER_COMPLETE);
	/* 0 samples is single-sampled multiview on both. */
	pass &= check(color, depth, 0, 0, 2, 2, GL_FRAMEBUFFER_COMPLETE);
	pass &= check(color, depth, max, 0, 2, 2,
		      GL_FRAMEBUFFER_INCOMPLETE_MULTISAMPLE);
	pass &= check(color, depth, max, max, 2, 1,
		      GL_FRAMEBUFFER_INCOMPLETE_VIEW_TARGETS_OVR);

	glBindFramebuffer(GL_FRAMEBUFFER, piglit_winsys_fbo);
	glDeleteFramebuffers(1, &fb);
	glDeleteTextures(1, &color);
	glDeleteTextures(1, &depth);
	return piglit_check_gl_error(GL_NO_ERROR) && pass ? PIGLIT_PASS : PIGLIT_FAIL;
}

static enum piglit_result
read_errors(void *data)
{
	GLuint fb, tex = mv_array_texture(GL_RGBA8, SIZE, SIZE, LAYERS);
	GLuint dst = msrtt_rgba_texture(SIZE, SIZE);
	GLubyte pixel[4];
	bool pass = true;

	glGenFramebuffers(1, &fb);
	glBindFramebuffer(GL_FRAMEBUFFER, fb);

	glFramebufferTextureMultisampleMultiviewOVR(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
						    tex, 0, msrtt_samples(), 0, 2);
	pass = mv_check_status(GL_FRAMEBUFFER_COMPLETE) && pass;
	glReadPixels(0, 0, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
	pass = piglit_check_gl_error(GL_INVALID_FRAMEBUFFER_OPERATION) && pass;
	glBindTexture(GL_TEXTURE_2D, dst);
	glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 0, 0, 1, 1);
	pass = piglit_check_gl_error(GL_INVALID_FRAMEBUFFER_OPERATION) && pass;

	/* One multisampled view resolves for reading like
	 * FramebufferTexture2DMultisampleEXT.
	 */
	glFramebufferTextureMultisampleMultiviewOVR(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
						    tex, 0, msrtt_samples(), 0, 1);
	pass = mv_check_status(GL_FRAMEBUFFER_COMPLETE) && pass;
	glReadPixels(0, 0, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
	pass = piglit_check_gl_error(GL_NO_ERROR) && pass;

	glBindFramebuffer(GL_FRAMEBUFFER, piglit_winsys_fbo);
	glDeleteFramebuffers(1, &fb);
	glDeleteTextures(1, &tex);
	glDeleteTextures(1, &dst);
	return piglit_check_gl_error(GL_NO_ERROR) && pass ? PIGLIT_PASS : PIGLIT_FAIL;
}

enum piglit_result
piglit_display(void)
{
	return mv_run_subtests(subtests);
}

void
piglit_init(int argc, char **argv)
{
	piglit_require_extension("GL_OVR_multiview_multisampled_render_to_texture");
}
