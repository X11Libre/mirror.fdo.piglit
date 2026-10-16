/*
 * SPDX-License-Identifier: MIT
 */

/**
 * Tests the OpenGL ES 3.0 interactions of EXT_multisampled_render_to_texture.
 *
 * Issue 6:
 *
 *    "If this extension is supported in OpenGL ES 3.0 or later then
 *    DRAW_FRAMEBUFFER and READ_FRAMEBUFFER are valid values for the <target>
 *    parameter to FramebufferTexture2DMultisampleEXT. [...] this also
 *    implies that FRAMEBUFFER is equivalent to DRAW_FRAMEBUFFER for this
 *    command."
 *
 * and the exception from the ES 3.1 errors for ReadPixels and
 * CopyTex[Sub]Image2D with SAMPLE_BUFFERS of one:
 *
 *    "These errors do not apply to textures and renderbuffers that have
 *    associated multisample data specified by the mechanisms described in
 *    this extension"
 */

#include "common.h"

#define SIZE 64

static enum piglit_result draw_target(void *data);
static enum piglit_result read_target(void *data);
static enum piglit_result framebuffer_target(void *data);
static enum piglit_result readpixels_texture(void *data);
static enum piglit_result readpixels_renderbuffer(void *data);
static enum piglit_result copytexsubimage(void *data);

static const struct piglit_subtest subtests[] = {
	{ "DRAW_FRAMEBUFFER target", "draw-framebuffer", draw_target },
	{ "READ_FRAMEBUFFER target", "read-framebuffer", read_target },
	{ "FRAMEBUFFER means DRAW_FRAMEBUFFER", "framebuffer", framebuffer_target },
	{ "ReadPixels from a multisampled texture", "readpixels-texture", readpixels_texture },
	{ "ReadPixels from a multisampled renderbuffer", "readpixels-renderbuffer", readpixels_renderbuffer },
	{ "CopyTexSubImage2D from a multisampled texture", "copytexsubimage", copytexsubimage },
	{ NULL }
};

static const struct piglit_gl_test_config *piglit_config;

PIGLIT_GL_TEST_CONFIG_BEGIN

	config.supports_gl_es_version = 30;
	config.window_visual = PIGLIT_GL_VISUAL_RGBA;
	config.khr_no_error_support = PIGLIT_NO_ERRORS;
	config.subtests = subtests;
	piglit_config = &config;

PIGLIT_GL_TEST_CONFIG_END

static const float fg[4] = { 1.0, 1.0, 1.0, 1.0 };
static const float bg[4] = { 0.0, 0.0, 0.0, 1.0 };

static struct msrtt_program prog;
static GLuint draw_fb, read_fb, tex;

static void
begin(void)
{
	glGenFramebuffers(1, &draw_fb);
	glGenFramebuffers(1, &read_fb);
	glBindFramebuffer(GL_DRAW_FRAMEBUFFER, draw_fb);
	glBindFramebuffer(GL_READ_FRAMEBUFFER, read_fb);
	tex = msrtt_rgba_texture(SIZE, SIZE);
}

static enum piglit_result
end(bool pass)
{
	glBindFramebuffer(GL_FRAMEBUFFER, piglit_winsys_fbo);
	glDeleteFramebuffers(1, &draw_fb);
	glDeleteFramebuffers(1, &read_fb);
	glDeleteTextures(1, &tex);
	return piglit_check_gl_error(GL_NO_ERROR) && pass ? PIGLIT_PASS : PIGLIT_FAIL;
}

static void
draw_edge(void)
{
	glViewport(0, 0, SIZE, SIZE);
	glClearColor(bg[0], bg[1], bg[2], bg[3]);
	glClear(GL_COLOR_BUFFER_BIT);
	msrtt_use(&prog, fg, 0);
	msrtt_draw_edge();
}

/* Checks which of the two framebuffers got the multisampled attachment. */
static bool
check_attached(GLenum attached, GLenum other)
{
	GLint type = -1;
	bool pass = true;

	if (msrtt_texture_samples(attached, GL_COLOR_ATTACHMENT0) <= 0) {
		printf("%s has no multisampled attachment\n",
		       piglit_get_gl_enum_name(attached));
		pass = false;
	}
	glGetFramebufferAttachmentParameteriv(other, GL_COLOR_ATTACHMENT0,
					      GL_FRAMEBUFFER_ATTACHMENT_OBJECT_TYPE,
					      &type);
	if (type != GL_NONE) {
		printf("%s got an attachment too\n", piglit_get_gl_enum_name(other));
		pass = false;
	}
	return pass;
}

static enum piglit_result
draw_target(void *data)
{
	bool pass;

	begin();
	glFramebufferTexture2DMultisampleEXT(GL_DRAW_FRAMEBUFFER,
					     GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
					     tex, 0, msrtt_samples());
	pass = piglit_check_gl_error(GL_NO_ERROR);
	pass &= check_attached(GL_DRAW_FRAMEBUFFER, GL_READ_FRAMEBUFFER);

	/* and it renders multisampled */
	draw_edge();
	glBindFramebuffer(GL_READ_FRAMEBUFFER, draw_fb);
	pass &= msrtt_check_edge_framebuffer(SIZE, SIZE, fg, bg, "draw framebuffer");
	return end(pass);
}

static enum piglit_result
read_target(void *data)
{
	bool pass;

	begin();
	glFramebufferTexture2DMultisampleEXT(GL_READ_FRAMEBUFFER,
					     GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
					     tex, 0, msrtt_samples());
	pass = piglit_check_gl_error(GL_NO_ERROR);
	pass &= check_attached(GL_READ_FRAMEBUFFER, GL_DRAW_FRAMEBUFFER);
	return end(pass);
}

static enum piglit_result
framebuffer_target(void *data)
{
	bool pass;

	begin();
	glFramebufferTexture2DMultisampleEXT(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
					     GL_TEXTURE_2D, tex, 0,
					     msrtt_samples());
	pass = piglit_check_gl_error(GL_NO_ERROR);
	pass &= check_attached(GL_DRAW_FRAMEBUFFER, GL_READ_FRAMEBUFFER);
	return end(pass);
}

/* SAMPLE_BUFFERS is one for the read framebuffer, but reading is allowed. */
static bool
read_back(const char *what)
{
	GLint buffers = 0;

	glGetIntegerv(GL_SAMPLE_BUFFERS, &buffers);
	if (buffers != 1)
		printf("%s: SAMPLE_BUFFERS is %d\n", what, buffers);

	bool pass = buffers == 1;

	pass &= msrtt_check_edge_framebuffer(SIZE, SIZE, fg, bg, what);
	return piglit_check_gl_error(GL_NO_ERROR) && pass;
}

static enum piglit_result
readpixels_texture(void *data)
{
	begin();
	glBindFramebuffer(GL_FRAMEBUFFER, draw_fb);
	glFramebufferTexture2DMultisampleEXT(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
					     GL_TEXTURE_2D, tex, 0,
					     msrtt_samples());
	draw_edge();
	return end(read_back("texture"));
}

static enum piglit_result
readpixels_renderbuffer(void *data)
{
	GLuint rb;
	bool pass;

	begin();
	glBindFramebuffer(GL_FRAMEBUFFER, draw_fb);
	glGenRenderbuffers(1, &rb);
	glBindRenderbuffer(GL_RENDERBUFFER, rb);
	msrtt_renderbuffer_storage(GL_RENDERBUFFER, msrtt_samples(),
					    GL_RGBA8, SIZE, SIZE);
	glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
				  GL_RENDERBUFFER, rb);
	draw_edge();
	pass = read_back("renderbuffer");
	glDeleteRenderbuffers(1, &rb);
	return end(pass);
}

static enum piglit_result
copytexsubimage(void *data)
{
	GLuint dst, dst_fb;
	bool pass;

	begin();
	glBindFramebuffer(GL_FRAMEBUFFER, draw_fb);
	glFramebufferTexture2DMultisampleEXT(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
					     GL_TEXTURE_2D, tex, 0,
					     msrtt_samples());
	draw_edge();

	dst = msrtt_rgba_texture(SIZE, SIZE);
	glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 0, 0, SIZE, SIZE);
	pass = piglit_check_gl_error(GL_NO_ERROR);

	dst_fb = msrtt_read_framebuffer(dst);
	pass &= msrtt_check_edge_framebuffer(SIZE, SIZE, fg, bg, "CopyTexSubImage2D");
	glDeleteFramebuffers(1, &dst_fb);
	glDeleteTextures(1, &dst);
	return end(pass);
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
