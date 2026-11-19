/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: MIT
 */

/**
 * @file p01x_csc_common.h
 *
 * Shared code for the sample_p010-csc and sample_p012-csc tests.
 *
 * \author Tupili Krishna Gowtham Reddy <tupilikr@qti.qualcomm.com>
 */

#ifndef P01X_CSC_COMMON_H
#define P01X_CSC_COMMON_H

#include <stdint.h>

#include "piglit-util-gl.h"

/**
 * An MSB-aligned 2-plane YUV 4:2:0 format with 16-bit containers.
 *
 * The test image is defined with 10-bit codes. y_code() and c_code()
 * turn them into codes of the format's bit depth, which are then stored
 * shifted left by 16 - bits.
 */
struct p01x_csc_format {
	const char *name;	/**< e.g. "P010" */
	uint32_t fourcc;	/**< e.g. DRM_FORMAT_P010 */
	unsigned bits;		/**< significant bits per sample */

	/** Luma code at pixel (x, y) for the 10-bit test code. */
	unsigned (*y_code)(unsigned x, unsigned y, unsigned code10);

	/** Chroma code at chroma sample (x, y) for the 10-bit test code. */
	unsigned (*c_code)(unsigned x, unsigned y, unsigned code10);
};

/**
 * Check the required extensions and parse
 * [-cs=601|709|2020] [-range=narrow|full] [-tolerance=N].
 */
void
p01x_csc_init(int argc, char **argv);

/** Import, draw, read back and check the test image. */
enum piglit_result
p01x_csc_display(const struct p01x_csc_format *fmt);

#endif /* P01X_CSC_COMMON_H */
