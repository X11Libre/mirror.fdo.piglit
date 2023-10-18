/*
 * Copyright © 2013 Intel Corporation
 * Copyright © 2023 Lucas Stach (adapted to GL ES)
 *
 * SPDX-License-Identifier: MIT
 */

/**
 * \file
 * Test FenceSyncAPPLE() returns correct error messages for invalid input
 *
 * Section 5.2 (Sync Objects and Fences) of the extension spec says:
 *
 *     "An INVALID_ENUM error is generated if <condition> is not
 *      SYNC_GPU_COMMANDS_COMPLETE_APPLE. If <flags> is not zero,
 *      an INVALID_VALUE error is generated."
 *
 */

#include "piglit-util-gl.h"

PIGLIT_GL_TEST_CONFIG_BEGIN

	config.supports_gl_es_version = 20;
	config.khr_no_error_support = PIGLIT_HAS_ERRORS;

PIGLIT_GL_TEST_CONFIG_END

enum piglit_result
piglit_display(void)
{
	/* UNREACHED */
	return PIGLIT_FAIL;
}

void
piglit_init(int argc, char **argv)
{
	bool pass = true;
	GLsync a, b;

	piglit_require_extension("GL_APPLE_sync");

	/* test that an invalid condition results in INVALID_ENUM */
	a = glFenceSyncAPPLE(GL_NONE, 0);
	pass = piglit_check_gl_error(GL_INVALID_ENUM) && pass;
	glDeleteSyncAPPLE(a);

	/* test that invalid flag value results in INVALID_VALUE */
	b = glFenceSyncAPPLE(GL_SYNC_GPU_COMMANDS_COMPLETE_APPLE, 1);
	pass = piglit_check_gl_error(GL_INVALID_VALUE) && pass;
	glDeleteSyncAPPLE(b);

	piglit_report_result(pass ? PIGLIT_PASS : PIGLIT_FAIL);
}
