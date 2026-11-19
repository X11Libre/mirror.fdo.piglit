/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: MIT
 */

/**
 * @file sample_p010_csc.c
 *
 * Test EGL_EXT_image_dma_buf_import + GL_OES_EGL_image_external: sample a
 * P010 dma-buf through samplerExternalOES and check the YUV->RGB conversion
 * honours the 10-bit sample precision and the
 * EGL_YUV_COLOR_SPACE_HINT_EXT / EGL_SAMPLE_RANGE_HINT_EXT import hints.
 *
 * P010 stores each 10-bit sample MSB-aligned in 16 bits (shifted left by
 * 6). Converting it with 8-bit coefficients was measured to be off by up
 * to 9 10-bit steps in narrow range (a visible tint on greys), so the
 * result is read back from a 10-bit render target.
 *
 * Flow:
 *   1. Fill an 8x4 P010 image of 2x2 blocks: neutral greys one code
 *      apart, narrow-range black/white (64/940) and saturated colours.
 *   2. Import it with the requested colour space (-cs=601|709|2020) and
 *      range (-range=narrow|full) hints.
 *   3. Draw it into an RGB10_A2 FBO and read it back.
 *   4. Compare every pixel against the ITU-T H.273 / BT.2100 10-bit
 *      equations rounded to 10 bits, within -tolerance (default 2).
 *
 * SKIP if the driver cannot import P010. The shared code is in
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

/* The 10-bit test codes are used as they are. */
static unsigned
p010_code(unsigned x, unsigned y, unsigned code10)
{
	(void)x;
	(void)y;
	return code10;
}

static const struct p01x_csc_format p010 = {
	.name = "P010",
	.fourcc = DRM_FORMAT_P010,
	.bits = 10,
	.y_code = p010_code,
	.c_code = p010_code,
};

enum piglit_result
piglit_display(void)
{
	return p01x_csc_display(&p010);
}

void
piglit_init(int argc, char **argv)
{
	p01x_csc_init(argc, argv);
}
