/*
 * Copyright © 2013 Intel Corporation
 * Copyright © 2023 Lucas Stach (adapted to GL ES)
 *
 * SPDX-License-Identifier: MIT
 */

/**
 * \file
 * Test IsSyncAPPLE()
 *
 * Section 6.1.6 (Sync Object Queries) of the extension spec says:
 *
 *      "The command
 *           boolean IsSyncAPPLE(sync sync);
 *       returns TRUE if <sync> is the name of a sync object. If <sync> is
 *       not the name of a sync object, or if an error condition occurs,
 *       IsSyncAPPLE returns FALSE (note that zero is not the name of a
 *       sync object).
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
	GLsync valid_sync;
	GLsync invalid_sync = (GLsync)GL_BACK;

	piglit_require_extension("GL_APPLE_sync");

	/* Create valid sync object */
	valid_sync = glFenceSyncAPPLE(GL_SYNC_GPU_COMMANDS_COMPLETE_APPLE, 0);

	/* Check if a valid name returns true */
	pass = glIsSyncAPPLE(valid_sync) && pass;

	/* Check if invalid names return false.
	 * From the extension specification:
	 *	"If <sync> is not the name of a sync object, or if an error
	 *	 condition occurs, IsSyncAPPLE returns FALSE (note that zero
	 *	 is not the name of a sync object)."
	 */
	pass = !glIsSyncAPPLE(invalid_sync) && pass;

	pass = !glIsSyncAPPLE(0) && pass;

	glDeleteSyncAPPLE(valid_sync);

	piglit_report_result(pass ? PIGLIT_PASS : PIGLIT_FAIL);
}
