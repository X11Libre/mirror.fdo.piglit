/*
 * SPDX-License-Identifier: MIT
 */

/**
 * Tests drawing after a scissored depth clear to a depth texture attached by
 * FramebufferTexture2DMultisampleEXT, when the framebuffer's color texture
 * has been drawn to through another framebuffer since the first one last
 * drew to it.
 *
 * Blue is drawn at depth 0.5, red is drawn into the color texture through a
 * framebuffer that attaches it single-sampled, and back on the first
 * framebuffer the depth of the lower left quadrant is cleared to 1.0 with
 * the scissor test enabled before green is drawn at depth 0.75 over that
 * quadrant.  The clear to 1.0 has to let the green draw through, and the
 * rest of the color texture has to stay red, since (issue 7)
 *
 *    "an attachment will always see the most recent change made to the
 *    attached texture, whether that was done by rendering into an
 *    attachment or by a texture update operation."
 */

#include "../ext_multisampled_render_to_texture/common.h"

#define SIZE 64

PIGLIT_GL_TEST_CONFIG_BEGIN

	config.supports_gl_es_version = 20;
	config.window_visual = PIGLIT_GL_VISUAL_RGBA;
	config.khr_no_error_support = PIGLIT_NO_ERRORS;

PIGLIT_GL_TEST_CONFIG_END

static const float blue[4] = { 0.0, 0.0, 1.0, 1.0 };
static const float green[4] = { 0.0, 1.0, 0.0, 1.0 };
static const float red[4] = { 1.0, 0.0, 0.0, 1.0 };

static struct msrtt_program prog;

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

enum piglit_result
piglit_display(void)
{
	GLuint fb, other_fb, color, depth;
	GLint samples;
	bool pass = true;

	depth = depth_texture();
	if (!depth)
		return PIGLIT_SKIP;

	glGenFramebuffers(1, &fb);
	glBindFramebuffer(GL_FRAMEBUFFER, fb);
	color = msrtt_rgba_texture(SIZE, SIZE);
	glFramebufferTexture2DMultisampleEXT(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
					     GL_TEXTURE_2D, color, 0,
					     msrtt_samples());
	samples = msrtt_texture_samples(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0);
	glFramebufferTexture2DMultisampleEXT(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
					     GL_TEXTURE_2D, depth, 0, samples);
	if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
		printf("framebuffer with %d samples is incomplete\n", samples);
		pass = false;
		goto done;
	}

	other_fb = msrtt_read_framebuffer(color);
	glViewport(0, 0, SIZE, SIZE);

	/* blue at depth 0.5 everywhere */
	glBindFramebuffer(GL_FRAMEBUFFER, fb);
	glClearColor(0, 0, 0, 1);
	glClearDepthf(1.0);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	glEnable(GL_DEPTH_TEST);
	glDepthFunc(GL_LESS);
	msrtt_use(&prog, blue, 0.0);
	piglit_draw_rect(-1, -1, 2, 2);

	/* red into the color texture itself, through another framebuffer */
	glBindFramebuffer(GL_FRAMEBUFFER, other_fb);
	glDisable(GL_DEPTH_TEST);
	msrtt_use(&prog, red, 0.0);
	piglit_draw_rect(-1, -1, 2, 2);

	/* back, with a depth clear on the lower left quadrant only, and green
	 * at depth 0.75 over that quadrant
	 */
	glBindFramebuffer(GL_FRAMEBUFFER, fb);
	glEnable(GL_SCISSOR_TEST);
	glScissor(0, 0, SIZE / 2, SIZE / 2);
	glClear(GL_DEPTH_BUFFER_BIT);
	glDisable(GL_SCISSOR_TEST);
	glEnable(GL_DEPTH_TEST);
	msrtt_use(&prog, green, 0.5);
	piglit_draw_rect(-1, -1, 1, 1);
	glDisable(GL_DEPTH_TEST);

	glBindFramebuffer(GL_FRAMEBUFFER, other_fb);
	pass &= piglit_probe_rect_rgba(0, 0, SIZE / 2, SIZE / 2, green);
	pass &= piglit_probe_rect_rgba(SIZE / 2, 0, SIZE / 2, SIZE, red);
	pass &= piglit_probe_rect_rgba(0, SIZE / 2, SIZE / 2, SIZE / 2, red);
	glDeleteFramebuffers(1, &other_fb);

done:
	glBindFramebuffer(GL_FRAMEBUFFER, piglit_winsys_fbo);
	glDeleteFramebuffers(1, &fb);
	glDeleteTextures(1, &color);
	glDeleteTextures(1, &depth);
	return piglit_check_gl_error(GL_NO_ERROR) && pass ? PIGLIT_PASS : PIGLIT_FAIL;
}

void
piglit_init(int argc, char **argv)
{
	piglit_require_extension("GL_EXT_multisampled_render_to_texture2");

	prog = msrtt_color_program();
}
