/*
 * SPDX-License-Identifier: MIT
 */

/**
 * Tests valid but less common uses of EXT_multisampled_render_to_texture:
 *
 * - existing-contents: drawing over a texture's contents without clearing
 *   blends with them, since issue 7 says "an attachment will always see the
 *   most recent change made to the attached texture".
 * - scissored-clear and color-mask: a scissored clear and a masked draw only
 *   change the scissored region and the unmasked channels.
 * - zero-samples: with 0 samples, FramebufferTexture2DMultisampleEXT
 *   "behaves like FramebufferTexture2D", and renders single-sampled.
 * - shared-texture: the same texture is attached multisampled to one
 *   framebuffer and single-sampled to another, and drawing alternates
 *   between them; each sees the other's rendering.
 * - reattach: the texture is reattached with another sample count between
 *   draws, and keeps the first draw's result.
 * - npot: a non-power-of-two texture.
 * - packed-formats: RGBA4 and RGB565 textures, as in the spec's sample code.
 * - discard-depth: discarding the multisampled depth renderbuffer mid-frame,
 *   as the sample code does, and rendering more color afterwards.
 * - immutable-texture: a texture from TexStorage2D (ES 3.0).
 * - blit: BlitFramebuffer from the multisampled framebuffer to a
 *   single-sampled one (ES 3.0).
 * - respecified-texture: TexImage2D with a new size while the texture is
 *   attached; rendering uses the new image.
 */

#include "common.h"

#define SIZE 64

static enum piglit_result existing_contents(void *data);
static enum piglit_result scissored_clear(void *data);
static enum piglit_result color_mask(void *data);
static enum piglit_result zero_samples(void *data);
static enum piglit_result shared_texture(void *data);
static enum piglit_result reattach(void *data);
static enum piglit_result npot(void *data);
static enum piglit_result packed_formats(void *data);
static enum piglit_result discard_depth(void *data);
static enum piglit_result immutable_texture(void *data);
static enum piglit_result blit(void *data);
static enum piglit_result respecified_texture(void *data);

static const struct piglit_subtest subtests[] = {
	{ "existing texture contents", "existing-contents", existing_contents },
	{ "scissored clear", "scissored-clear", scissored_clear },
	{ "color mask", "color-mask", color_mask },
	{ "zero samples", "zero-samples", zero_samples },
	{ "texture shared with a single-sampled framebuffer", "shared-texture", shared_texture },
	{ "reattach with another sample count", "reattach", reattach },
	{ "non-power-of-two texture", "npot", npot },
	{ "packed formats", "packed-formats", packed_formats },
	{ "discard depth mid-frame", "discard-depth", discard_depth },
	{ "immutable texture", "immutable-texture", immutable_texture },
	{ "BlitFramebuffer from the multisampled framebuffer", "blit", blit },
	{ "texture respecified while attached", "respecified-texture", respecified_texture },
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

static const float black[4] = { 0.0, 0.0, 0.0, 1.0 };
static const float white[4] = { 1.0, 1.0, 1.0, 1.0 };
static const float red[4] = { 1.0, 0.0, 0.0, 1.0 };
static const float green[4] = { 0.0, 1.0, 0.0, 1.0 };
static const float blue[4] = { 0.0, 0.0, 1.0, 1.0 };

static struct msrtt_program prog;

/* Attaches tex multisampled to a new framebuffer, which stays bound. */
static GLuint
attach(GLuint tex, GLint samples)
{
	GLuint fb;

	glGenFramebuffers(1, &fb);
	glBindFramebuffer(GL_FRAMEBUFFER, fb);
	glFramebufferTexture2DMultisampleEXT(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
					     GL_TEXTURE_2D, tex, 0, samples);
	return fb;
}

static bool
complete(void)
{
	GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);

	if (status != GL_FRAMEBUFFER_COMPLETE)
		printf("framebuffer status is %s\n", piglit_get_gl_enum_name(status));
	return status == GL_FRAMEBUFFER_COMPLETE;
}

static void
clear(const float *color, int w, int h)
{
	glViewport(0, 0, w, h);
	glClearColor(color[0], color[1], color[2], color[3]);
	glClear(GL_COLOR_BUFFER_BIT);
}

static enum piglit_result
done(bool pass, GLuint *fbs, int nfb, GLuint *texs, int ntex)
{
	glBindFramebuffer(GL_FRAMEBUFFER, piglit_winsys_fbo);
	glDeleteFramebuffers(nfb, fbs);
	glDeleteTextures(ntex, texs);
	return piglit_check_gl_error(GL_NO_ERROR) && pass ? PIGLIT_PASS : PIGLIT_FAIL;
}

static enum piglit_result
existing_contents(void *data)
{
	static const float expected[4] = { 1.0, 0.0, 1.0, 1.0 };
	GLubyte *pixels = malloc(SIZE * SIZE * 4);
	GLuint fbs[2], tex;
	bool pass;

	for (int i = 0; i < SIZE * SIZE; i++) {
		pixels[i * 4 + 0] = 255;
		pixels[i * 4 + 1] = pixels[i * 4 + 2] = 0;
		pixels[i * 4 + 3] = 255;
	}
	tex = msrtt_rgba_texture(SIZE, SIZE);
	glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, SIZE, SIZE, GL_RGBA,
			GL_UNSIGNED_BYTE, pixels);
	free(pixels);

	fbs[0] = attach(tex, msrtt_samples());
	pass = complete();
	glViewport(0, 0, SIZE, SIZE);
	glEnable(GL_BLEND);
	glBlendFunc(GL_ONE, GL_ONE);
	msrtt_use(&prog, blue, 0);
	piglit_draw_rect(-1, -1, 2, 2);
	glDisable(GL_BLEND);

	fbs[1] = msrtt_read_framebuffer(tex);
	pass &= piglit_probe_rect_rgba(0, 0, SIZE, SIZE, expected);
	return done(pass, fbs, 2, &tex, 1);
}

static enum piglit_result
scissored_clear(void *data)
{
	GLuint fbs[2], tex = msrtt_rgba_texture(SIZE, SIZE);
	bool pass;

	fbs[0] = attach(tex, msrtt_samples());
	pass = complete();
	clear(red, SIZE, SIZE);
	glEnable(GL_SCISSOR_TEST);
	glScissor(16, 16, 16, 16);
	clear(green, SIZE, SIZE);
	glDisable(GL_SCISSOR_TEST);

	fbs[1] = msrtt_read_framebuffer(tex);
	pass &= piglit_probe_rect_rgba(16, 16, 16, 16, green);
	pass &= piglit_probe_rect_rgba(0, 0, SIZE, 16, red);
	pass &= piglit_probe_rect_rgba(0, 32, SIZE, SIZE - 32, red);
	pass &= piglit_probe_rect_rgba(0, 16, 16, 16, red);
	pass &= piglit_probe_rect_rgba(32, 16, SIZE - 32, 16, red);
	return done(pass, fbs, 2, &tex, 1);
}

static enum piglit_result
color_mask(void *data)
{
	static const float yellow[4] = { 1.0, 1.0, 0.0, 1.0 };
	GLuint fbs[2], tex = msrtt_rgba_texture(SIZE, SIZE);
	bool pass;

	fbs[0] = attach(tex, msrtt_samples());
	pass = complete();
	clear(red, SIZE, SIZE);
	glColorMask(GL_FALSE, GL_TRUE, GL_FALSE, GL_FALSE);
	msrtt_use(&prog, white, 0);
	piglit_draw_rect(-1, -1, 2, 2);
	glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);

	fbs[1] = msrtt_read_framebuffer(tex);
	pass &= piglit_probe_rect_rgba(0, 0, SIZE, SIZE, yellow);
	return done(pass, fbs, 2, &tex, 1);
}

static enum piglit_result
zero_samples(void *data)
{
	GLuint fbs[2], tex = msrtt_rgba_texture(SIZE, SIZE);
	GLubyte *pixels = malloc(SIZE * SIZE * 4);
	int blended = 0;
	bool pass;

	fbs[0] = attach(tex, 0);
	pass = complete();
	clear(black, SIZE, SIZE);
	msrtt_use(&prog, white, 0);
	msrtt_draw_edge();

	fbs[1] = msrtt_read_framebuffer(tex);
	glReadPixels(0, 0, SIZE, SIZE, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
	for (int i = 0; i < SIZE * SIZE; i++) {
		if (pixels[i * 4] != 0 && pixels[i * 4] != 255)
			blended++;
	}
	free(pixels);

	/* single-sampled: every pixel is one color or the other, and the
	 * edge is in the right place
	 */
	if (blended) {
		printf("%d pixels are blended with 0 samples\n", blended);
		pass = false;
	}
	pass &= piglit_probe_pixel_rgba(0, 0, white);
	pass &= piglit_probe_pixel_rgba(0, SIZE - 1, black);
	return done(pass, fbs, 2, &tex, 1);
}

static enum piglit_result
shared_texture(void *data)
{
	GLuint fbs[2], tex = msrtt_rgba_texture(SIZE, SIZE);
	bool pass;

	fbs[0] = attach(tex, msrtt_samples());
	pass = complete();
	fbs[1] = msrtt_read_framebuffer(tex);

	/* red in the left half through the multisampled framebuffer, green
	 * in the right half through the plain one, then blue in the top
	 * quarter through the multisampled one again
	 */
	glBindFramebuffer(GL_FRAMEBUFFER, fbs[0]);
	clear(black, SIZE, SIZE);
	msrtt_use(&prog, red, 0);
	piglit_draw_rect(-1, -1, 1, 2);
	glBindFramebuffer(GL_FRAMEBUFFER, fbs[1]);
	msrtt_use(&prog, green, 0);
	piglit_draw_rect(0, -1, 1, 2);
	glBindFramebuffer(GL_FRAMEBUFFER, fbs[0]);
	msrtt_use(&prog, blue, 0);
	piglit_draw_rect(-1, 0.5, 2, 0.5);

	glBindFramebuffer(GL_FRAMEBUFFER, fbs[1]);
	pass &= piglit_probe_rect_rgba(0, 0, SIZE / 2, SIZE * 3 / 4, red);
	pass &= piglit_probe_rect_rgba(SIZE / 2, 0, SIZE / 2, SIZE * 3 / 4, green);
	pass &= piglit_probe_rect_rgba(0, SIZE * 3 / 4, SIZE, SIZE / 4, blue);
	return done(pass, fbs, 2, &tex, 1);
}

static enum piglit_result
reattach(void *data)
{
	GLuint fbs[2], tex = msrtt_rgba_texture(SIZE, SIZE);
	bool pass;

	fbs[0] = attach(tex, 1);
	pass = complete();
	clear(black, SIZE, SIZE);
	msrtt_use(&prog, red, 0);
	piglit_draw_rect(-1, -1, 1, 2);

	glFramebufferTexture2DMultisampleEXT(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
					     GL_TEXTURE_2D, tex, 0,
					     msrtt_samples());
	pass &= complete();
	msrtt_use(&prog, green, 0);
	piglit_draw_rect(0, -1, 1, 2);

	fbs[1] = msrtt_read_framebuffer(tex);
	pass &= piglit_probe_rect_rgba(0, 0, SIZE / 2, SIZE, red);
	pass &= piglit_probe_rect_rgba(SIZE / 2, 0, SIZE / 2, SIZE, green);
	return done(pass, fbs, 2, &tex, 1);
}

static bool
edge_in(GLuint tex, int w, int h, const char *what)
{
	GLuint fb = attach(tex, msrtt_samples());
	bool pass = complete();

	clear(black, w, h);
	msrtt_use(&prog, white, 0);
	msrtt_draw_edge();
	glDeleteFramebuffers(1, &fb);

	fb = msrtt_read_framebuffer(tex);
	pass &= msrtt_check_edge_framebuffer(w, h, white, black, what);
	glBindFramebuffer(GL_FRAMEBUFFER, piglit_winsys_fbo);
	glDeleteFramebuffers(1, &fb);
	return pass;
}

static enum piglit_result
npot(void *data)
{
	GLuint tex = msrtt_rgba_texture(61, 37);
	bool pass = edge_in(tex, 61, 37, "61x37");

	glDeleteTextures(1, &tex);
	return piglit_check_gl_error(GL_NO_ERROR) && pass ? PIGLIT_PASS : PIGLIT_FAIL;
}

static enum piglit_result
packed_formats(void *data)
{
	GLuint rgba4 = msrtt_texture(GL_RGBA, GL_RGBA, GL_UNSIGNED_SHORT_4_4_4_4,
				     SIZE, SIZE);
	GLuint rgb565 = msrtt_texture(GL_RGB, GL_RGB, GL_UNSIGNED_SHORT_5_6_5,
				      SIZE, SIZE);
	bool pass = true;

	pass &= edge_in(rgba4, SIZE, SIZE, "RGBA4");
	pass &= edge_in(rgb565, SIZE, SIZE, "RGB565");
	glDeleteTextures(1, &rgba4);
	glDeleteTextures(1, &rgb565);
	return piglit_check_gl_error(GL_NO_ERROR) && pass ? PIGLIT_PASS : PIGLIT_FAIL;
}

static enum piglit_result
discard_depth(void *data)
{
	static const GLenum depth_attachment[] = { GL_DEPTH_ATTACHMENT };
	GLuint fbs[2], rb, tex = msrtt_rgba_texture(SIZE, SIZE);
	bool pass;

	if (piglit_get_gl_version() < 30 &&
	    !piglit_is_extension_supported("GL_EXT_discard_framebuffer"))
		return PIGLIT_SKIP;

	fbs[0] = attach(tex, msrtt_samples());
	glGenRenderbuffers(1, &rb);
	glBindRenderbuffer(GL_RENDERBUFFER, rb);
	msrtt_renderbuffer_storage(GL_RENDERBUFFER,
					    msrtt_texture_samples(GL_FRAMEBUFFER,
								  GL_COLOR_ATTACHMENT0),
					    GL_DEPTH_COMPONENT16, SIZE, SIZE);
	glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
				  GL_RENDERBUFFER, rb);
	pass = complete();
	glClearDepthf(1.0);
	glClear(GL_DEPTH_BUFFER_BIT);
	clear(black, SIZE, SIZE);

	glEnable(GL_DEPTH_TEST);
	msrtt_use(&prog, red, 0);
	piglit_draw_rect(-1, -1, 1, 2);
	if (piglit_get_gl_version() >= 30)
		glInvalidateFramebuffer(GL_FRAMEBUFFER, 1, depth_attachment);
	else
		glDiscardFramebufferEXT(GL_FRAMEBUFFER, 1, depth_attachment);

	/* depth is undefined now, so draw without the depth test */
	glDisable(GL_DEPTH_TEST);
	msrtt_use(&prog, green, 0);
	piglit_draw_rect(0, -1, 1, 2);

	fbs[1] = msrtt_read_framebuffer(tex);
	pass &= piglit_probe_rect_rgba(0, 0, SIZE / 2, SIZE, red);
	pass &= piglit_probe_rect_rgba(SIZE / 2, 0, SIZE / 2, SIZE, green);
	glDeleteRenderbuffers(1, &rb);
	return done(pass, fbs, 2, &tex, 1);
}

static enum piglit_result
immutable_texture(void *data)
{
	GLuint tex;
	bool pass;

	if (piglit_get_gl_version() < 30)
		return PIGLIT_SKIP;

	glGenTextures(1, &tex);
	glBindTexture(GL_TEXTURE_2D, tex);
	glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA8, SIZE, SIZE);
	pass = edge_in(tex, SIZE, SIZE, "TexStorage2D");
	glDeleteTextures(1, &tex);
	return piglit_check_gl_error(GL_NO_ERROR) && pass ? PIGLIT_PASS : PIGLIT_FAIL;
}

static enum piglit_result
blit(void *data)
{
	GLuint fbs[2], texs[2];
	bool pass;

	if (piglit_get_gl_version() < 30)
		return PIGLIT_SKIP;

	texs[0] = msrtt_rgba_texture(SIZE, SIZE);
	texs[1] = msrtt_rgba_texture(SIZE, SIZE);
	fbs[0] = attach(texs[0], msrtt_samples());
	pass = complete();
	clear(black, SIZE, SIZE);
	msrtt_use(&prog, white, 0);
	msrtt_draw_edge();

	fbs[1] = msrtt_read_framebuffer(texs[1]);
	glBindFramebuffer(GL_READ_FRAMEBUFFER, fbs[0]);
	glBindFramebuffer(GL_DRAW_FRAMEBUFFER, fbs[1]);
	glBlitFramebuffer(0, 0, SIZE, SIZE, 0, 0, SIZE, SIZE,
			  GL_COLOR_BUFFER_BIT, GL_NEAREST);
	pass &= piglit_check_gl_error(GL_NO_ERROR);

	glBindFramebuffer(GL_FRAMEBUFFER, fbs[1]);
	pass &= msrtt_check_edge_framebuffer(SIZE, SIZE, white, black, "blit");
	return done(pass, fbs, 2, texs, 2);
}

static enum piglit_result
respecified_texture(void *data)
{
	GLuint fbs[2], tex = msrtt_rgba_texture(SIZE, SIZE);
	bool pass;

	fbs[0] = attach(tex, msrtt_samples());
	pass = complete();
	clear(red, SIZE, SIZE);

	/* a new, smaller image for the attached level */
	glBindTexture(GL_TEXTURE_2D, tex);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, SIZE / 2, SIZE / 2, 0, GL_RGBA,
		     GL_UNSIGNED_BYTE, NULL);
	pass &= complete();
	clear(black, SIZE / 2, SIZE / 2);
	msrtt_use(&prog, white, 0);
	msrtt_draw_edge();

	fbs[1] = msrtt_read_framebuffer(tex);
	pass &= msrtt_check_edge_framebuffer(SIZE / 2, SIZE / 2, white, black,
					     "respecified");
	return done(pass, fbs, 2, &tex, 1);
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
