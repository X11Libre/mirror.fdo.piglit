/*
 * SPDX-License-Identifier: MIT
 */

/**
 * Tests the OVR_multiview API on OpenGL and OpenGL ES:
 *
 * - max-views: MAX_VIEWS_OVR is at least 2.
 * - errors: the FramebufferTextureMultiviewOVR errors,
 *
 *      "An INVALID_VALUE error is generated if:
 *      - <numViews> is less than 1 or if <numViews> is greater than
 *        MAX_VIEWS_OVR.
 *      - <texture> is a two-dimensional array texture and <baseViewIndex> +
 *        <numViews> is larger than the value of MAX_ARRAY_TEXTURE_LAYERS.
 *      - texture is non-zero and <baseViewIndex> is negative."
 *
 *      An INVALID_OPERATION error is generated if texture is non-zero and is
 *      not the name of a two-dimensional array texture."
 *
 *   plus those of FramebufferTextureLayer, and that detaching with texture 0
 *   is not an error even with a negative base view index.
 * - queries: FRAMEBUFFER_ATTACHMENT_TEXTURE_NUM_VIEWS_OVR and
 *   FRAMEBUFFER_ATTACHMENT_TEXTURE_BASE_VIEW_INDEX_OVR, which are zero when
 *   the attachment is not a two-dimensional array texture (issue 29 leaves
 *   their values for a single layer of one up to the implementation), and
 *   FRAMEBUFFER_ATTACHMENT_TEXTURE_LAYER,
 *   which issue 28 says "is set it to the value of <baseViewIndex>".
 * - completeness: views past the last layer make the attachment incomplete,
 *   and different view counts make the framebuffer
 *   FRAMEBUFFER_INCOMPLETE_VIEW_TARGETS_OVR.
 * - redefined-texture: per issue 27, referencing more layers than the
 *   texture has is a completeness condition, so redefining the texture with
 *   fewer layers makes the framebuffer incomplete, and back complete.
 * - read-errors: reading from a read framebuffer with more than one view is
 *   INVALID_FRAMEBUFFER_OPERATION, and reading from one with a single view
 *   is allowed.
 */

#include "mv-common.h"

#define SIZE 16

static enum piglit_result max_views(void *data);
static enum piglit_result errors(void *data);
static enum piglit_result queries(void *data);
static enum piglit_result completeness(void *data);
static enum piglit_result redefined_texture(void *data);
static enum piglit_result read_errors(void *data);

static const struct piglit_subtest subtests[] = {
	{ "MAX_VIEWS_OVR", "max-views", max_views },
	{ "FramebufferTextureMultiviewOVR errors", "errors", errors },
	{ "attachment queries", "queries", queries },
	{ "completeness", "completeness", completeness },
	{ "redefined texture", "redefined-texture", redefined_texture },
	{ "read errors", "read-errors", read_errors },
	{ NULL }
};

static const struct piglit_gl_test_config *piglit_config;

PIGLIT_GL_TEST_CONFIG_BEGIN

	config.supports_gl_core_version = 33;
	config.supports_gl_es_version = 30;
	config.window_visual = PIGLIT_GL_VISUAL_RGBA;
	config.khr_no_error_support = PIGLIT_HAS_ERRORS;
	config.subtests = subtests;
	piglit_config = &config;

PIGLIT_GL_TEST_CONFIG_END

static GLuint fb;

static void
begin(void)
{
	glGenFramebuffers(1, &fb);
	glBindFramebuffer(GL_FRAMEBUFFER, fb);
}

static enum piglit_result
end(bool pass)
{
	glBindFramebuffer(GL_FRAMEBUFFER, piglit_winsys_fbo);
	glDeleteFramebuffers(1, &fb);
	return pass ? PIGLIT_PASS : PIGLIT_FAIL;
}

static enum piglit_result
max_views(void *data)
{
	if (mv_max_views() < 2) {
		printf("MAX_VIEWS_OVR is %d, the minimum is 2\n", mv_max_views());
		return PIGLIT_FAIL;
	}
	return piglit_check_gl_error(GL_NO_ERROR) ? PIGLIT_PASS : PIGLIT_FAIL;
}

static enum piglit_result
errors(void *data)
{
	GLint max_layers;
	GLuint array, tex2d, unused = 0;
	bool pass = true;

	glGetIntegerv(GL_MAX_ARRAY_TEXTURE_LAYERS, &max_layers);
	array = mv_array_texture(GL_RGBA8, SIZE, SIZE, 4);
	glGenTextures(1, &tex2d);
	glBindTexture(GL_TEXTURE_2D, tex2d);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, SIZE, SIZE, 0, GL_RGBA,
		     GL_UNSIGNED_BYTE, NULL);
	glGenTextures(1, &unused);
	begin();

	glFramebufferTextureMultiviewOVR(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
					 array, 0, 0, 0);
	pass &= piglit_check_gl_error(GL_INVALID_VALUE);
	glFramebufferTextureMultiviewOVR(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
					 array, 0, 0, mv_max_views() + 1);
	pass &= piglit_check_gl_error(GL_INVALID_VALUE);
	glFramebufferTextureMultiviewOVR(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
					 array, 0, max_layers - 1, 2);
	pass &= piglit_check_gl_error(GL_INVALID_VALUE);
	glFramebufferTextureMultiviewOVR(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
					 array, 0, -1, 2);
	pass &= piglit_check_gl_error(GL_INVALID_VALUE);
	glFramebufferTextureMultiviewOVR(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
					 tex2d, 0, 0, 2);
	pass &= piglit_check_gl_error(GL_INVALID_OPERATION);

	/* from FramebufferTextureLayer */
	glFramebufferTextureMultiviewOVR(GL_TEXTURE_2D_ARRAY, GL_COLOR_ATTACHMENT0,
					 array, 0, 0, 2);
	pass &= piglit_check_gl_error(GL_INVALID_ENUM);
	glFramebufferTextureMultiviewOVR(GL_FRAMEBUFFER, GL_TEXTURE_2D_ARRAY,
					 array, 0, 0, 2);
	pass &= piglit_check_gl_error(GL_INVALID_ENUM);
	glFramebufferTextureMultiviewOVR(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
					 array, -1, 0, 2);
	pass &= piglit_check_gl_error(GL_INVALID_VALUE);
	/* OpenGL ES 3.0 makes a name that is not a texture INVALID_OPERATION,
	 * and OpenGL ES 3.2 and OpenGL 4.6 make it INVALID_VALUE
	 */
	glDeleteTextures(1, &unused);
	glFramebufferTextureMultiviewOVR(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
					 unused, 0, 0, 2);
	{
		GLenum error = glGetError();

		if (error != GL_INVALID_OPERATION && error != GL_INVALID_VALUE) {
			printf("a deleted texture name gave %s\n",
			       piglit_get_gl_enum_name(error));
			pass = false;
		}
	}

	/* the largest valid range */
	glFramebufferTextureMultiviewOVR(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
					 array, 0, max_layers - 2, 2);
	pass &= piglit_check_gl_error(GL_NO_ERROR);

	/* only a non-zero texture needs a valid base view index */
	glFramebufferTextureMultiviewOVR(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
					 0, 0, -1, 2);
	pass &= piglit_check_gl_error(GL_NO_ERROR);

	glDeleteTextures(1, &array);
	glDeleteTextures(1, &tex2d);
	return end(pass);
}

static bool
check_query(GLenum attachment, GLenum pname, GLint expected)
{
	GLint value = -1;

	glGetFramebufferAttachmentParameteriv(GL_FRAMEBUFFER, attachment, pname,
					      &value);
	if (value != expected) {
		printf("%s is %d, expected %d\n", piglit_get_gl_enum_name(pname),
		       value, expected);
		return false;
	}
	return true;
}

static enum piglit_result
queries(void *data)
{
	GLuint array = mv_array_texture(GL_RGBA8, SIZE, SIZE, 4), tex2d;
	bool pass = true;

	begin();
	glFramebufferTextureMultiviewOVR(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
					 array, 0, 1, 2);
	pass &= check_query(GL_COLOR_ATTACHMENT0,
			    GL_FRAMEBUFFER_ATTACHMENT_TEXTURE_NUM_VIEWS_OVR, 2);
	pass &= check_query(GL_COLOR_ATTACHMENT0,
			    GL_FRAMEBUFFER_ATTACHMENT_TEXTURE_BASE_VIEW_INDEX_OVR, 1);
	pass &= check_query(GL_COLOR_ATTACHMENT0,
			    GL_FRAMEBUFFER_ATTACHMENT_TEXTURE_LAYER, 1);

	/* a 2D texture is not multiview */
	glGenTextures(1, &tex2d);
	glBindTexture(GL_TEXTURE_2D, tex2d);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, SIZE, SIZE, 0, GL_RGBA,
		     GL_UNSIGNED_BYTE, NULL);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
			       tex2d, 0);
	pass &= check_query(GL_COLOR_ATTACHMENT0,
			    GL_FRAMEBUFFER_ATTACHMENT_TEXTURE_NUM_VIEWS_OVR, 0);
	pass &= check_query(GL_COLOR_ATTACHMENT0,
			    GL_FRAMEBUFFER_ATTACHMENT_TEXTURE_BASE_VIEW_INDEX_OVR, 0);

	/* reattaching replaces the view range */
	glFramebufferTextureMultiviewOVR(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
					 array, 0, 2, 2);
	pass &= check_query(GL_COLOR_ATTACHMENT0,
			    GL_FRAMEBUFFER_ATTACHMENT_TEXTURE_NUM_VIEWS_OVR, 2);
	pass &= check_query(GL_COLOR_ATTACHMENT0,
			    GL_FRAMEBUFFER_ATTACHMENT_TEXTURE_BASE_VIEW_INDEX_OVR, 2);

	pass &= piglit_check_gl_error(GL_NO_ERROR);
	glDeleteTextures(1, &array);
	glDeleteTextures(1, &tex2d);
	return end(pass);
}

static enum piglit_result
completeness(void *data)
{
	GLuint color = mv_array_texture(GL_RGBA8, SIZE, SIZE, 4);
	GLuint depth = mv_array_texture(GL_DEPTH_COMPONENT24, SIZE, SIZE, 4);
	bool pass = true;

	begin();

	/* layers 3 and 4 of a 4-layer texture */
	glFramebufferTextureMultiviewOVR(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
					 color, 0, 3, 2);
	pass &= mv_check_status(GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT);

	glFramebufferTextureMultiviewOVR(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
					 color, 0, 2, 2);
	pass &= mv_check_status(GL_FRAMEBUFFER_COMPLETE);

	/* matching depth views, at another base */
	glFramebufferTextureMultiviewOVR(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
					 depth, 0, 0, 2);
	pass &= mv_check_status(GL_FRAMEBUFFER_COMPLETE);

	glFramebufferTextureMultiviewOVR(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
					 depth, 0, 0, 1);
	pass &= mv_check_status(GL_FRAMEBUFFER_INCOMPLETE_VIEW_TARGETS_OVR);

	pass &= piglit_check_gl_error(GL_NO_ERROR);
	glDeleteTextures(1, &color);
	glDeleteTextures(1, &depth);
	return end(pass);
}

static enum piglit_result
redefined_texture(void *data)
{
	GLuint tex;
	bool pass = true;

	/* a mutable texture, so it can be redefined */
	glGenTextures(1, &tex);
	glBindTexture(GL_TEXTURE_2D_ARRAY, tex);
	glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, GL_RGBA8, SIZE, SIZE, 4, 0, GL_RGBA,
		     GL_UNSIGNED_BYTE, NULL);
	glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_NEAREST);

	begin();
	glFramebufferTextureMultiviewOVR(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
					 tex, 0, 1, 2);
	pass &= mv_check_status(GL_FRAMEBUFFER_COMPLETE);

	/* layer 2 is gone */
	glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, GL_RGBA8, SIZE, SIZE, 2, 0, GL_RGBA,
		     GL_UNSIGNED_BYTE, NULL);
	pass &= mv_check_status(GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT);

	glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, GL_RGBA8, SIZE, SIZE, 4, 0, GL_RGBA,
		     GL_UNSIGNED_BYTE, NULL);
	pass &= mv_check_status(GL_FRAMEBUFFER_COMPLETE);

	pass &= piglit_check_gl_error(GL_NO_ERROR);
	glDeleteTextures(1, &tex);
	return end(pass);
}

static enum piglit_result
read_errors(void *data)
{
	GLuint tex = mv_array_texture(GL_RGBA8, SIZE, SIZE, 4), dst, dst_fb;
	GLubyte pixel[4];
	bool pass = true;

	glGenTextures(1, &dst);
	glBindTexture(GL_TEXTURE_2D, dst);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, SIZE, SIZE, 0, GL_RGBA,
		     GL_UNSIGNED_BYTE, NULL);
	glGenFramebuffers(1, &dst_fb);
	glBindFramebuffer(GL_DRAW_FRAMEBUFFER, dst_fb);
	glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
			       GL_TEXTURE_2D, dst, 0);

	glGenFramebuffers(1, &fb);
	glBindFramebuffer(GL_READ_FRAMEBUFFER, fb);
	for (int views = 1; views <= 2; views++) {
		GLenum expected = views > 1 ? GL_INVALID_FRAMEBUFFER_OPERATION
					    : GL_NO_ERROR;

		glFramebufferTextureMultiviewOVR(GL_READ_FRAMEBUFFER,
						 GL_COLOR_ATTACHMENT0, tex, 0, 1,
						 views);
		pass &= piglit_check_gl_error(GL_NO_ERROR);

		glReadPixels(0, 0, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
		pass &= piglit_check_gl_error(expected);
		glBindTexture(GL_TEXTURE_2D, dst);
		glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 0, 0, 1, 1);
		pass &= piglit_check_gl_error(expected);
		glBlitFramebuffer(0, 0, SIZE, SIZE, 0, 0, SIZE, SIZE,
				  GL_COLOR_BUFFER_BIT, GL_NEAREST);
		pass &= piglit_check_gl_error(expected);
	}

	glBindFramebuffer(GL_FRAMEBUFFER, piglit_winsys_fbo);
	glDeleteFramebuffers(1, &dst_fb);
	glDeleteTextures(1, &tex);
	glDeleteTextures(1, &dst);
	return end(pass);
}

enum piglit_result
piglit_display(void)
{
	return mv_run_subtests(subtests);
}

void
piglit_init(int argc, char **argv)
{
	piglit_require_extension("GL_OVR_multiview");
}
