/*
 * Copyright © 2013 Intel Corporation
 * Copyright © 2023 Lucas Stach (adapted to GL ES)
 *
 * SPDX-License-Identifier: MIT
 */

/**
 * \file
 * Test DeleteSyncAPPLE() returns correct error messages
 *
 * Section 5.2 (Sync Objects and Fences) of the extension spec says:
 *
 *     "DeleteSyncAPPLE will silently ignore a <sync> value of zero. An
 *      INVALID_VALUE error is generated if <sync> is neither zero nor the
 *      name of a sync object."
 *
 */

#include "piglit-util-gl.h"

PIGLIT_GL_TEST_CONFIG_BEGIN

        config.supports_gl_es_version = 20;
	config.khr_no_error_support = PIGLIT_NO_ERRORS;

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
	GLsync sync;
	GLsync invalid = (GLsync) GL_FRONT;

	piglit_require_extension("GL_APPLE_sync");

	/* Test for successful function calls
	 * DeleteSync will silently ignore a sync value of zero
	 */
	glDeleteSync(0);
	pass = piglit_check_gl_error(GL_NO_ERROR) && pass;

	sync = glFenceSyncAPPLE(GL_SYNC_GPU_COMMANDS_COMPLETE_APPLE, 0);
	glDeleteSyncAPPLE(sync);
	pass = piglit_check_gl_error(GL_NO_ERROR) && pass;
	/* Check if sync was deleted */
	pass = !glIsSyncAPPLE(sync) && pass;

	if (!piglit_khr_no_error) {
		/* Test for unsuccessful function calls */
		glDeleteSyncAPPLE(invalid);
		pass = piglit_check_gl_error(GL_INVALID_VALUE) && pass;
	}

	piglit_report_result(pass ? PIGLIT_PASS : PIGLIT_FAIL);
}
