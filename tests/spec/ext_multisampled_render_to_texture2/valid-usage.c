/*
 * SPDX-License-Identifier: MIT
 */

/**
 * Tests valid but less common uses of EXT_multisampled_render_to_texture2:
 *
 * - renderbuffer-color: a color renderbuffer from
 *   RenderbufferStorageMultisampleEXT with a depth texture attached by
 *   FramebufferTexture2DMultisampleEXT.
 * - depth-cube-face: a face of a depth cube map texture as the multisampled
 *   depth attachment (ES 3.0).
 * - depth-level: mipmap level 1 of a depth texture as the multisampled depth
 *   attachment (ES 3.0).
 * - shared-depth-texture: one depth texture attached multisampled to two
 *   framebuffers, each rendered with depth testing in turn.
 * - color-attachment-1: a multisampled texture on COLOR_ATTACHMENT1 only,
 *   with draw buffers { NONE, COLOR_ATTACHMENT1 } (ES 3.0).
 *
 * Each draws a red rectangle over the left half that writes depth, then a
 * green one over everything that the depth test keeps out of the left half,
 * and checks the color result.
 */

#include "../ext_multisampled_render_to_texture/common.h"

#define SIZE 64

static enum piglit_result renderbuffer_color(void *data);
static enum piglit_result depth_cube_face(void *data);
static enum piglit_result depth_level(void *data);
static enum piglit_result shared_depth_texture(void *data);
static enum piglit_result color_attachment_1(void *data);

static const struct piglit_subtest subtests[] = {
	{ "color renderbuffer with a depth texture", "renderbuffer-color", renderbuffer_color },
	{ "depth cube map face", "depth-cube-face", depth_cube_face },
	{ "depth mipmap level 1", "depth-level", depth_level },
	{ "depth texture shared by two framebuffers", "shared-depth-texture", shared_depth_texture },
	{ "COLOR_ATTACHMENT1 only", "color-attachment-1", color_attachment_1 },
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

static GLuint
depth_texture(GLenum target, int size)
{
	GLuint tex;

	if (piglit_get_gl_version() < 30 && target != GL_TEXTURE_2D)
		return 0;

	glGenTextures(1, &tex);
	glBindTexture(target, tex);
	if (piglit_get_gl_version() >= 30) {
		if (target == GL_TEXTURE_CUBE_MAP) {
			for (int f = 0; f < 6; f++)
				glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + f, 0,
					     GL_DEPTH_COMPONENT24, size, size, 0,
					     GL_DEPTH_COMPONENT, GL_UNSIGNED_INT,
					     NULL);
		} else {
			glTexImage2D(target, 0, GL_DEPTH_COMPONENT24, size, size, 0,
				     GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, NULL);
		}
	} else if (piglit_is_extension_supported("GL_OES_depth_texture")) {
		glTexImage2D(target, 0, GL_DEPTH_COMPONENT, size, size, 0,
			     GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, NULL);
	} else {
		glDeleteTextures(1, &tex);
		return 0;
	}
	glTexParameteri(target, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(target, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	return tex;
}

static bool
complete(void)
{
	GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);

	if (status != GL_FRAMEBUFFER_COMPLETE)
		printf("framebuffer status is %s\n", piglit_get_gl_enum_name(status));
	return status == GL_FRAMEBUFFER_COMPLETE;
}

/* Clears, draws the two rectangles with depth testing, and leaves the
 * depth test disabled.
 */
static void
draw_rects(void)
{
	glViewport(0, 0, SIZE, SIZE);
	glClearColor(0, 0, 0, 1);
	glClearDepthf(1.0);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	glEnable(GL_DEPTH_TEST);
	glDepthFunc(GL_LESS);
	msrtt_use(&prog, red, -0.5);
	piglit_draw_rect(-1, -1, 1, 2);
	msrtt_use(&prog, green, 0.5);
	piglit_draw_rect(-1, -1, 2, 2);
	glDisable(GL_DEPTH_TEST);
}

/* Checks the left half is red and the right half green in tex. */
static bool
check_texture(GLuint tex)
{
	GLuint fb = msrtt_read_framebuffer(tex);
	bool pass = piglit_probe_rect_rgba(0, 0, SIZE / 2, SIZE, red) &&
		    piglit_probe_rect_rgba(SIZE / 2, 0, SIZE / 2, SIZE, green);

	glBindFramebuffer(GL_FRAMEBUFFER, piglit_winsys_fbo);
	glDeleteFramebuffers(1, &fb);
	return pass;
}

/* A framebuffer with a multisampled color texture, which stays bound; the
 * sample count is returned in samples.
 */
static GLuint
color_framebuffer(GLuint tex, GLint *samples)
{
	GLuint fb;

	glGenFramebuffers(1, &fb);
	glBindFramebuffer(GL_FRAMEBUFFER, fb);
	glFramebufferTexture2DMultisampleEXT(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
					     GL_TEXTURE_2D, tex, 0,
					     msrtt_samples());
	*samples = msrtt_texture_samples(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0);
	return fb;
}

static enum piglit_result
result(bool pass)
{
	glBindFramebuffer(GL_FRAMEBUFFER, piglit_winsys_fbo);
	return piglit_check_gl_error(GL_NO_ERROR) && pass ? PIGLIT_PASS : PIGLIT_FAIL;
}

static enum piglit_result
renderbuffer_color(void *data)
{
	GLuint fb, rb, depth = depth_texture(GL_TEXTURE_2D, SIZE);
	GLint samples;
	bool pass;

	if (!depth)
		return PIGLIT_SKIP;

	glGenFramebuffers(1, &fb);
	glBindFramebuffer(GL_FRAMEBUFFER, fb);
	glGenRenderbuffers(1, &rb);
	glBindRenderbuffer(GL_RENDERBUFFER, rb);
	msrtt_renderbuffer_storage(GL_RENDERBUFFER, msrtt_samples(),
					    GL_RGBA4, SIZE, SIZE);
	samples = msrtt_renderbuffer_samples();
	glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
				  GL_RENDERBUFFER, rb);
	glFramebufferTexture2DMultisampleEXT(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
					     GL_TEXTURE_2D, depth, 0, samples);
	pass = complete();
	draw_rects();

	/* read the renderbuffer directly */
	pass &= piglit_probe_rect_rgba(0, 0, SIZE / 2, SIZE, red);
	pass &= piglit_probe_rect_rgba(SIZE / 2, 0, SIZE / 2, SIZE, green);

	glDeleteFramebuffers(1, &fb);
	glDeleteRenderbuffers(1, &rb);
	glDeleteTextures(1, &depth);
	return result(pass);
}

static enum piglit_result
depth_cube_face(void *data)
{
	GLuint fb, color, depth = depth_texture(GL_TEXTURE_CUBE_MAP, SIZE);
	GLint samples;
	bool pass = true;

	if (!depth)
		return PIGLIT_SKIP;

	color = msrtt_rgba_texture(SIZE, SIZE);
	fb = color_framebuffer(color, &samples);
	for (int f = 0; f < 6; f++) {
		/* check_texture() binds its own framebuffer */
		glBindFramebuffer(GL_FRAMEBUFFER, fb);
		glFramebufferTexture2DMultisampleEXT(GL_FRAMEBUFFER,
						     GL_DEPTH_ATTACHMENT,
						     GL_TEXTURE_CUBE_MAP_POSITIVE_X + f,
						     depth, 0, samples);
		pass &= complete();
		draw_rects();
		if (!check_texture(color)) {
			printf("wrong result with depth face %d\n", f);
			pass = false;
		}
	}

	glDeleteFramebuffers(1, &fb);
	glDeleteTextures(1, &color);
	glDeleteTextures(1, &depth);
	return result(pass);
}

static enum piglit_result
depth_level(void *data)
{
	GLuint fb, color, depth;
	GLint samples;
	bool pass;

	if (piglit_get_gl_version() < 30)
		return PIGLIT_SKIP;

	glGenTextures(1, &depth);
	glBindTexture(GL_TEXTURE_2D, depth);
	glTexStorage2D(GL_TEXTURE_2D, 2, GL_DEPTH_COMPONENT24, SIZE * 2, SIZE * 2);

	color = msrtt_rgba_texture(SIZE, SIZE);
	fb = color_framebuffer(color, &samples);
	glFramebufferTexture2DMultisampleEXT(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
					     GL_TEXTURE_2D, depth, 1, samples);
	pass = complete();
	draw_rects();
	pass &= check_texture(color);

	glDeleteFramebuffers(1, &fb);
	glDeleteTextures(1, &color);
	glDeleteTextures(1, &depth);
	return result(pass);
}

static enum piglit_result
shared_depth_texture(void *data)
{
	GLuint fbs[2], colors[2], depth = depth_texture(GL_TEXTURE_2D, SIZE);
	GLint samples;
	bool pass = true;

	if (!depth)
		return PIGLIT_SKIP;

	for (int i = 0; i < 2; i++) {
		colors[i] = msrtt_rgba_texture(SIZE, SIZE);
		fbs[i] = color_framebuffer(colors[i], &samples);
		glFramebufferTexture2DMultisampleEXT(GL_FRAMEBUFFER,
						     GL_DEPTH_ATTACHMENT,
						     GL_TEXTURE_2D, depth, 0,
						     samples);
		pass &= complete();
	}
	for (int i = 0; i < 2; i++) {
		glBindFramebuffer(GL_FRAMEBUFFER, fbs[i]);
		draw_rects();
	}
	pass &= check_texture(colors[0]);
	pass &= check_texture(colors[1]);

	glDeleteFramebuffers(2, fbs);
	glDeleteTextures(2, colors);
	glDeleteTextures(1, &depth);
	return result(pass);
}

static enum piglit_result
color_attachment_1(void *data)
{
	static const GLenum bufs[] = { GL_NONE, GL_COLOR_ATTACHMENT1 };
	static const char *fs_source =
		"#version 300 es\n"
		"precision mediump float;\n"
		"uniform vec4 color;\n"
		"layout(location = 1) out vec4 out1;\n"
		"void main()\n"
		"{\n"
		"	out1 = color;\n"
		"}\n";
	static const char *vs_source =
		"#version 300 es\n"
		"in vec2 piglit_vertex;\n"
		"uniform float depth;\n"
		"void main()\n"
		"{\n"
		"	gl_Position = vec4(piglit_vertex, depth, 1.0);\n"
		"}\n";
	struct msrtt_program p;
	GLuint fb, color, depth;
	bool pass;

	if (piglit_get_gl_version() < 30)
		return PIGLIT_SKIP;

	p.prog = piglit_build_simple_program(vs_source, fs_source);
	p.color = glGetUniformLocation(p.prog, "color");
	p.depth = glGetUniformLocation(p.prog, "depth");

	color = msrtt_rgba_texture(SIZE, SIZE);
	depth = depth_texture(GL_TEXTURE_2D, SIZE);
	glGenFramebuffers(1, &fb);
	glBindFramebuffer(GL_FRAMEBUFFER, fb);
	glFramebufferTexture2DMultisampleEXT(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1,
					     GL_TEXTURE_2D, color, 0,
					     msrtt_samples());
	glFramebufferTexture2DMultisampleEXT(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
					     GL_TEXTURE_2D, depth, 0,
					     msrtt_texture_samples(GL_FRAMEBUFFER,
								   GL_COLOR_ATTACHMENT1));
	glDrawBuffers(2, bufs);
	pass = complete();

	glViewport(0, 0, SIZE, SIZE);
	glClearColor(0, 0, 0, 1);
	glClearDepthf(1.0);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	glEnable(GL_DEPTH_TEST);
	msrtt_use(&p, red, -0.5);
	piglit_draw_rect(-1, -1, 1, 2);
	msrtt_use(&p, green, 0.5);
	piglit_draw_rect(-1, -1, 2, 2);
	glDisable(GL_DEPTH_TEST);
	pass &= check_texture(color);

	glDeleteFramebuffers(1, &fb);
	glDeleteTextures(1, &color);
	glDeleteTextures(1, &depth);
	glDeleteProgram(p.prog);
	return result(pass);
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
