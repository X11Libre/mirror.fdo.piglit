/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: MIT
 */

/**
 * @file sample_p012_csc.c
 *
 * Test EGL_EXT_image_dma_buf_import + GL_OES_EGL_image_external: sample a
 * P012 dma-buf through samplerExternalOES and check the YUV->RGB conversion
 * uses 12-bit maths and honours the
 * EGL_YUV_COLOR_SPACE_HINT_EXT / EGL_SAMPLE_RANGE_HINT_EXT import hints.
 *
 * P012 stores each 12-bit sample MSB-aligned in 16 bits (shifted left by
 * 4). Converting it with 8-bit coefficients gives visibly wrong colours
 * (a tint on greys), so the result is read back from a 10-bit render
 * target.
 *
 * Flow:
 *   1. Fill an 8x4 P012 image of 2x2 blocks: neutral greys one code
 *      apart, narrow-range black/white (64/940 in 10-bit terms) and
 *      saturated colours. The 10-bit codes are shifted up to 12 bits and
 *      their two LSBs are dithered, for luma and chroma, so the input
 *      uses all 12 bits.
 *   2. Import it with the requested colour space (-cs=601|709|2020) and
 *      range (-range=narrow|full) hints.
 *   3. Draw it into an RGB10_A2 FBO and read it back.
 *   4. Compare every pixel against the ITU-T H.273 / BT.2100 12-bit
 *      equations rounded to 10 bits, within -tolerance (default 2).
 *
 * The comparison is at 10-bit output precision, so this checks the 12-bit
 * scaling and offsets rather than that every low input bit survives.
 *
 * SKIP if the driver cannot import P012. The shared code is in
 * p01x_csc_common.c.
 *
 * \author Tupili Krishna Gowtham Reddy <tupilikr@qti.qualcomm.com>
 */

#include "piglit-util-gl.h"
#include "image_common.h"
#include "p01x_csc_common.h"

PIGLIT_GL_TEST_CONFIG_BEGIN

	config.supports_gl_es_version = 30;
	config.window_visual = PIGLIT_GL_VISUAL_RGBA;

PIGLIT_GL_TEST_CONFIG_END

/* Shift the 10-bit test code up to 12 bits and dither the two LSBs. Luma
 * passes pixel coordinates and chroma passes chroma sample coordinates, so
 * all four dither values are used by both.
 */
static unsigned
p012_code(unsigned x, unsigned y, unsigned code10)
{
	return (code10 << 2) | ((x + y) & 3);
}

static const struct p01x_csc_format p012 = {
	.name = "P012",
	.fourcc = DRM_FORMAT_P012,
	.bits = 12,
	.y_code = p012_code,
	.c_code = p012_code,
};

enum piglit_result
piglit_display(void)
{
	return p01x_csc_display(&p012);
}

void
piglit_init(int argc, char **argv)
{
	p01x_csc_init(argc, argv);
}
