/*
 * Copyright © 2026 Valve Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

/** @file bptc-compress-alpha.c
 *
 * Tests that the implementation's BPTC encoder preserves the alpha channel.
 *
 * bptc-modes.c covers the decoder, which the extension specifies exactly.
 * The encoder is deliberately left unspecified, so this test cannot assert a
 * particular encoding.  What it can do is feed the encoder content the format
 * is able to represent exactly no matter which mode it picks, and require the
 * round trip to survive it.
 *
 * Every 4x4 block here uses a single RGB colour and exactly two distinct
 * alpha values.  Two values are the most that can be relied on: the
 * interpolation weights are not uniformly spaced (the 3-bit table is
 * 0, 9, 18, 27, 37, 46, 55, 64 out of 64, and the 2-bit and 4-bit tables
 * subdivide differently again), so intermediate values sit on a lattice that
 * varies per mode.  The two endpoint weights, 0 and 64, are the only ones
 * every table shares.  A block with one colour and two alphas therefore lands
 * exactly in any mode that carries alpha: both colour endpoints take the
 * block's colour, the alpha endpoints take its two alpha values, and the
 * indices select between them.
 *
 * The residual error is then endpoint quantisation alone.  That is worst in
 * mode 7, whose 5-bit endpoints are spaced 255/31 apart and so cost at most
 * ~4.  TOLERANCE sits well above that while staying far below the error a
 * broken encoder produces.
 *
 * The colour is constant within each block on purpose.  An encoder that
 * derives its alpha endpoints from a colour channel rather than from alpha
 * then has no per-texel variation to work with, and collapses the block to a
 * single alpha.  Mesa's BPTC encoder did exactly that for eleven years by
 * comparing the blue channel against the average alpha, which this test
 * catches with an alpha error of ~127.  It went unnoticed because
 * compressedteximage only probes RGB.
 *
 * Colour fidelity for blocks that are not exactly representable is out of
 * scope: BPTC offers no bound there, so there is nothing portable to assert.
 */

#include "piglit-util-gl.h"

PIGLIT_GL_TEST_CONFIG_BEGIN

	config.supports_gl_compat_version = 11;

	config.window_visual = PIGLIT_GL_VISUAL_RGBA | PIGLIT_GL_VISUAL_DOUBLE;
	config.khr_no_error_support = PIGLIT_NO_ERRORS;

PIGLIT_GL_TEST_CONFIG_END

#define BLOCK_SIZE 4
#define N_BLOCKS 4
#define TEX_SIZE (BLOCK_SIZE * N_BLOCKS)

/* See the file comment. Generous enough for any mode the encoder may pick,
 * tight enough that losing the alpha variation within a block cannot pass.
 */
#define TOLERANCE 16

/* Reported when a mismatch is found, to keep a broken encoder from spamming
 * 256 texels worth of output.
 */
#define MAX_REPORTED_ERRORS 8

static const GLenum formats[] = {
	GL_COMPRESSED_RGBA_BPTC_UNORM,
	GL_COMPRESSED_SRGB_ALPHA_BPTC_UNORM,
};

/* The two alpha values a block is built from. Both extremes, pairs that sit
 * off the endpoint lattice, and pairs close together.
 */
static const GLubyte alpha_pairs[][2] = {
	{ 0, 255 },
	{ 0, 128 },
	{ 128, 255 },
	{ 64, 192 },
	{ 32, 224 },
	{ 96, 160 },
	{ 0, 64 },
	{ 192, 255 },
};

/* How those two values are laid out across the block, so that the encoder has
 * to assign indices rather than emit a constant.
 */
static bool
alpha_is_high(int pattern, int x, int y)
{
	switch (pattern) {
	case 0:
		return (x ^ y) & 1;		/* checkerboard */
	case 1:
		return x >= BLOCK_SIZE / 2;	/* left/right halves */
	case 2:
		return y >= BLOCK_SIZE / 2;	/* top/bottom halves */
	case 3:
		return x == 0 && y == 0;	/* a single texel */
	default:
		return x >= y;			/* split on the diagonal */
	}
}

/* Constant within a block, but varying between them so that the encoder is
 * still handed a range of colours to sit alongside the alpha.
 */
static void
block_colour(int bx, int by, GLubyte rgb[3])
{
	rgb[0] = bx * 85;
	rgb[1] = by * 85;
	rgb[2] = ((bx + by) & 1) ? 64 : 192;
}

static void
build_image(GLubyte image[TEX_SIZE][TEX_SIZE][4])
{
	for (int y = 0; y < TEX_SIZE; y++) {
		for (int x = 0; x < TEX_SIZE; x++) {
			int bx = x / BLOCK_SIZE;
			int by = y / BLOCK_SIZE;
			int block = by * N_BLOCKS + bx;
			const GLubyte *pair =
				alpha_pairs[block % ARRAY_SIZE(alpha_pairs)];
			GLubyte rgb[3];

			block_colour(bx, by, rgb);
			memcpy(image[y][x], rgb, 3);
			image[y][x][3] =
				pair[alpha_is_high(block % 5,
						   x % BLOCK_SIZE,
						   y % BLOCK_SIZE)];
		}
	}
}

static enum piglit_result
test_format(GLenum internal_format)
{
	GLubyte src[TEX_SIZE][TEX_SIZE][4];
	GLubyte got[TEX_SIZE][TEX_SIZE][4];
	const char *name = piglit_get_gl_enum_name(internal_format);
	/* Only RGBA stores its colour linearly, so only there can the colour
	 * be compared against the source bytes without guessing how the
	 * implementation returns sRGB values.
	 */
	bool check_rgb = internal_format == GL_COMPRESSED_RGBA_BPTC_UNORM;
	int worst_alpha = 0, worst_rgb = 0;
	int n_errors = 0;
	GLint compressed = 0;
	GLuint tex;

	build_image(src);

	glGenTextures(1, &tex);
	glBindTexture(GL_TEXTURE_2D, tex);
	glTexImage2D(GL_TEXTURE_2D, 0, internal_format,
		     TEX_SIZE, TEX_SIZE, 0,
		     GL_RGBA, GL_UNSIGNED_BYTE, src);

	if (!piglit_check_gl_error(GL_NO_ERROR)) {
		glDeleteTextures(1, &tex);
		return PIGLIT_FAIL;
	}

	glGetTexLevelParameteriv(GL_TEXTURE_2D, 0,
				 GL_TEXTURE_COMPRESSED, &compressed);
	if (!compressed) {
		printf("%s: implementation did not compress the texture\n",
		       name);
		glDeleteTextures(1, &tex);
		return PIGLIT_SKIP;
	}

	memset(got, 0, sizeof got);
	glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, got);

	if (!piglit_check_gl_error(GL_NO_ERROR)) {
		glDeleteTextures(1, &tex);
		return PIGLIT_FAIL;
	}

	glDeleteTextures(1, &tex);

	for (int y = 0; y < TEX_SIZE; y++) {
		for (int x = 0; x < TEX_SIZE; x++) {
			int da = abs((int) got[y][x][3] - (int) src[y][x][3]);
			int drgb = 0;

			for (int c = 0; c < 3 && check_rgb; c++) {
				int d = abs((int) got[y][x][c] -
					    (int) src[y][x][c]);
				if (d > drgb)
					drgb = d;
			}

			if (da > worst_alpha)
				worst_alpha = da;
			if (drgb > worst_rgb)
				worst_rgb = drgb;

			if (da <= TOLERANCE && drgb <= TOLERANCE)
				continue;

			if (n_errors++ < MAX_REPORTED_ERRORS) {
				printf("%s: texel (%d,%d) in block (%d,%d): "
				       "expected %d %d %d %d, got %d %d %d %d\n",
				       name, x, y,
				       x / BLOCK_SIZE, y / BLOCK_SIZE,
				       src[y][x][0], src[y][x][1],
				       src[y][x][2], src[y][x][3],
				       got[y][x][0], got[y][x][1],
				       got[y][x][2], got[y][x][3]);
			}
		}
	}

	if (n_errors) {
		printf("%s: %d texels outside the tolerance of %d "
		       "(worst alpha error %d", name, n_errors, TOLERANCE,
		       worst_alpha);
		if (check_rgb)
			printf(", worst colour error %d", worst_rgb);
		printf(")\n");
		return PIGLIT_FAIL;
	}

	printf("%s: pass (worst alpha error %d", name, worst_alpha);
	if (check_rgb)
		printf(", worst colour error %d", worst_rgb);
	printf(")\n");

	return PIGLIT_PASS;
}

void
piglit_init(int argc, char **argv)
{
	enum piglit_result result = PIGLIT_SKIP;

	piglit_require_extension("GL_ARB_texture_compression_bptc");

	for (unsigned i = 0; i < ARRAY_SIZE(formats); i++) {
		piglit_merge_result(&result, test_format(formats[i]));
	}

	piglit_report_result(result);
}

enum piglit_result
piglit_display(void)
{
	return PIGLIT_PASS;
}
