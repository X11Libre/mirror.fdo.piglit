/*
 * Copyright © 2013 Intel Corporation
 * Copyright © 2023 Lucas Stach (adapted to GL ES)
 *
 * SPDX-License-Identifier: MIT
 */

/**
 * \file
 * Test ClientWaitSyncAPPLE() returns correct error messages for invalid input
 *
 *
 * Section 5.2.1 (Waiting for Sync Objects) of the extension spec says:
 *
 *     "If <sync> is not the name of a sync object, an INVALID_VALUE error
 *      is generated. If <flags> contains any bits other than
 *      SYNC_FLUSH_COMMANDS_BIT_APPLE, an INVALID_VALUE error is generated."
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
	GLsync a = (GLsync)0xDEADBEEF;
	GLenum status;
	int i;

	piglit_require_extension("GL_APPLE_sync");

	/* sync not set up yet so this should fail with both GL error and
	 * respond GL_WAIT_FAILED
	 */
	status = glClientWaitSyncAPPLE(a, GL_SYNC_FLUSH_COMMANDS_BIT_APPLE, 0);
	pass = piglit_check_gl_error(GL_INVALID_VALUE) && pass;
	if (status != GL_WAIT_FAILED_APPLE) {
		printf("Expected GL_WAIT_FAILED but returned: %s\n",
			piglit_get_gl_enum_name(status));
		pass = false;
	}

	a = glFenceSyncAPPLE(GL_SYNC_GPU_COMMANDS_COMPLETE_APPLE, 0);

	/* test that valid sync results in NO_ERROR */
	status = glClientWaitSyncAPPLE(a, GL_SYNC_FLUSH_COMMANDS_BIT_APPLE, 0);
	pass = piglit_check_gl_error(GL_NO_ERROR) && pass;

	/* test that invalid flag value results in INVALID_VALUE */
	for (i = 0; i < sizeof(GLbitfield) * 8; i++) {
		GLbitfield mask = 1 << i;
		/* Skip over the valid bit */
		if (mask == GL_SYNC_FLUSH_COMMANDS_BIT_APPLE) {
			continue;
		}
		status = glClientWaitSyncAPPLE(a, mask, 0);
		pass = (status == GL_WAIT_FAILED_APPLE) && pass;
		pass = piglit_check_gl_error(GL_INVALID_VALUE) && pass;
	}

	glDeleteSyncAPPLE(a);

	piglit_report_result(pass ? PIGLIT_PASS : PIGLIT_FAIL);
}
