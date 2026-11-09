/*
 * Copyright © 2026 Timothy Arceri
 *
 * SPDX-License-Identifier: MIT
 */

/**
 * \file copy-then-blend.c
 *
 * Render to two textures, copy part of one into the other with
 * glCopyImageSubData, then blend on top of the whole destination and check
 * the result.
 *
 * Drivers may copy with a compute shader that writes to the destination's
 * compressed storage directly.  The blend that follows reads the
 * destination back through the render backend, which must see everything
 * the compute shader wrote.
 */

#include "piglit-util-gl.h"

PIGLIT_GL_TEST_CONFIG_BEGIN

	config.supports_gl_compat_version = 30;
	config.window_visual = PIGLIT_GL_VISUAL_RGBA |
			       PIGLIT_GL_VISUAL_DOUBLE;
	config.khr_no_error_support = PIGLIT_NO_ERRORS;
	/* The textures are the size of the window.  At the default
	 * 160x160, radeonsi on Navi31 gives them 4KB tiles and no DCC, so
	 * the copy doesn't store to compressed memory at all.  256x256
	 * gets 64KB tiles with DCC.
	 */
	config.window_width = 256;
	config.window_height = 256;

PIGLIT_GL_TEST_CONFIG_END

/* The rectangles to copy, with edges that don't line up with any tile or
 * compression block size, and the colour drawn to the source texture
 * before each copy.  Each copy uses a different colour so that data left
 * over from an earlier copy can't pass for the result of a later one.
 *
 * Colours are in the red and green channels of the GL_RG8 textures, and
 * are multiples of 1/8 so that they and their sums with "base" and "add"
 * below are exact.
 */
static const struct {
	int x, y, w, h;
	float color[2];
} copies[] = {
	{ 3, 7, 241, 239, { 0.0, 0.5 } },
	{ 1, 1, 254, 254, { 0.125, 0.625 } },
	{ 13, 29, 37, 101, { 0.25, 0.5 } },
	{ 77, 5, 178, 250, { 0.375, 0.625 } },
};

/* The colour drawn to the destination texture before each copy, and the
 * colour added to it by the blend after the copy.
 */
static const float base[2] = { 0.25, 0.125 };
static const float add[2] = { 0.125, 0.25 };

static GLuint src_tex, dst_tex, src_fbo, dst_fbo;
static float *expected;

static GLuint
make_tex_fbo(GLuint *fbo)
{
	GLuint tex;

	glGenTextures(1, &tex);
	glBindTexture(GL_TEXTURE_2D, tex);
	/* 16 bits per texel, so that drivers that only use a compute
	 * shader for small texels still do.
	 */
	glTexStorage2D(GL_TEXTURE_2D, 1, GL_RG8, piglit_width,
		       piglit_height);

	glGenFramebuffers(1, fbo);
	glBindFramebuffer(GL_FRAMEBUFFER, *fbo);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
			       GL_TEXTURE_2D, tex, 0);
	if (glCheckFramebufferStatus(GL_FRAMEBUFFER) !=
	    GL_FRAMEBUFFER_COMPLETE)
		piglit_report_result(PIGLIT_SKIP);

	return tex;
}

void
piglit_init(int argc, char **argv)
{
	piglit_require_extension("GL_ARB_copy_image");
	piglit_require_extension("GL_ARB_texture_storage");

	src_tex = make_tex_fbo(&src_fbo);
	dst_tex = make_tex_fbo(&dst_fbo);

	expected = malloc(piglit_width * piglit_height * 2 * sizeof(float));
}

/* Draw over all of a texture, so that the render backend writes it
 * compressed.
 */
static void
fill(GLuint fbo, const float *color)
{
	glBindFramebuffer(GL_FRAMEBUFFER, fbo);
	glColor4f(color[0], color[1], 0.0, 1.0);
	piglit_draw_rect(-1, -1, 2, 2);
}

static void
set_expected(int x, int y, int w, int h, const float *color)
{
	for (int j = y; j < y + h; j++) {
		for (int i = x; i < x + w; i++) {
			float *texel = &expected[(j * piglit_width + i) * 2];

			texel[0] = color[0] + add[0];
			texel[1] = color[1] + add[1];
		}
	}
}

enum piglit_result
piglit_display(void)
{
	bool pass = true;

	for (unsigned i = 0; i < ARRAY_SIZE(copies) && pass; i++) {
		const int x = copies[i].x, y = copies[i].y;
		const int w = copies[i].w, h = copies[i].h;

		glDisable(GL_BLEND);
		fill(src_fbo, copies[i].color);
		fill(dst_fbo, base);

		glCopyImageSubData(src_tex, GL_TEXTURE_2D, 0, x, y, 0,
				   dst_tex, GL_TEXTURE_2D, 0, x, y, 0,
				   w, h, 1);

		/* The blend reads what the copy wrote. */
		glEnable(GL_BLEND);
		glBlendFunc(GL_ONE, GL_ONE);
		fill(dst_fbo, add);

		set_expected(0, 0, piglit_width, piglit_height, base);
		set_expected(x, y, w, h, copies[i].color);
		if (!piglit_probe_image_color(0, 0, piglit_width,
					      piglit_height, GL_RG,
					      expected)) {
			printf("Failed copying %dx%d at (%d, %d)\n",
			       w, h, x, y);
			pass = false;
		}
	}

	/* Show the last result. */
	glBindFramebuffer(GL_READ_FRAMEBUFFER, dst_fbo);
	glBindFramebuffer(GL_DRAW_FRAMEBUFFER, piglit_winsys_fbo);
	glBlitFramebuffer(0, 0, piglit_width, piglit_height,
			  0, 0, piglit_width, piglit_height,
			  GL_COLOR_BUFFER_BIT, GL_NEAREST);

	pass = piglit_check_gl_error(GL_NO_ERROR) && pass;

	piglit_present_results();

	return pass ? PIGLIT_PASS : PIGLIT_FAIL;
}
