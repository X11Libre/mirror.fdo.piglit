/*
 * Copyright © 2013 Intel Corporation
 * Copyright © 2023 Lucas Stach (adapted to GL ES)
 *
 * SPDX-License-Identifier: MIT
 */

/**
 * \file
 * Test WaitSyncAPPLE() returns correct error codes
 *
 * Section 5.2.1 (Waiting for Sync Objects) of the extension spec says:
 *
 *      "If <sync> is not the name of a sync object, an INVALID_VALUE error
 *       is generated. If <flags> contains any bits other than
 *       SYNC_FLUSH_COMMANDS_BIT_APPLE, an INVALID_VALUE error is generated."
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
	GLsync valid_sync;
	GLsync invalid_sync = (GLsync)20;

	piglit_require_extension("GL_APPLE_sync");

	valid_sync = glFenceSyncAPPLE(GL_SYNC_GPU_COMMANDS_COMPLETE_APPLE, 0);

	/* test that valid parameters passed results in NO_ERROR */
	glWaitSyncAPPLE(valid_sync, 0, GL_TIMEOUT_IGNORED_APPLE);
	pass = piglit_check_gl_error(GL_NO_ERROR) && pass;

	/* test that invalid sync results in INVALID_VALUE */
	glWaitSyncAPPLE(invalid_sync, 0, GL_TIMEOUT_IGNORED_APPLE);
	pass = piglit_check_gl_error(GL_INVALID_VALUE) && pass;

	/* test that invalid flag value results in INVALID_VALUE */
	glWaitSyncAPPLE(valid_sync, 3, GL_TIMEOUT_IGNORED_APPLE);
	pass = piglit_check_gl_error(GL_INVALID_VALUE) && pass;

	glDeleteSyncAPPLE(valid_sync);

	piglit_report_result(pass ? PIGLIT_PASS : PIGLIT_FAIL);
}
