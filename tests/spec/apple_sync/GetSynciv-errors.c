/*
 * Copyright © 2013 Intel Corporation
 * Copyright © 2023 Lucas Stach (adapted to GL ES)
 *
 * SPDX-License-Identifier: MIT
 */

/**
 * \file
 * Test GetSyncivAPPLE() sets correct error codes
 *
 * Section 6.1.6 (Sync Object Queries) of the extension spec says:
 *
 *    "If <sync> is not the name of a sync object, an INVALID_VALUE error
 *     is generated. If <pname> is not one of the values described above,
 *     an INVALID_ENUM error is generated."
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
	GLsync valid_fence;
	GLsync invalid_fence = (GLsync) 0x1373;

	GLsizei len;
	GLint val;

	piglit_require_extension("GL_APPLE_sync");

	valid_fence = glFenceSyncAPPLE(GL_SYNC_GPU_COMMANDS_COMPLETE_APPLE, 0);

	/* test that invalid sync results in INVALID_VALUE */
	glGetSyncivAPPLE(invalid_fence, GL_SYNC_STATUS_APPLE, 1, &len, &val);
	pass = piglit_check_gl_error(GL_INVALID_VALUE) && pass;

	/* test valid pname values result in NO_ERROR */
	glGetSyncivAPPLE(valid_fence, GL_OBJECT_TYPE_APPLE, 1, &len, &val);
	pass = piglit_check_gl_error(GL_NO_ERROR) && pass;

	glGetSyncivAPPLE(valid_fence, GL_SYNC_STATUS_APPLE, 1, &len, &val);
	pass = piglit_check_gl_error(GL_NO_ERROR) && pass;

	glGetSyncivAPPLE(valid_fence, GL_SYNC_CONDITION_APPLE, 1, &len, &val);
	pass = piglit_check_gl_error(GL_NO_ERROR) && pass;

	glGetSyncivAPPLE(valid_fence, GL_SYNC_FLAGS_APPLE, 1, &len, &val);
	pass = piglit_check_gl_error(GL_NO_ERROR) && pass;

	/* test that invalid pname results in INVALID_ENUM */
	glGetSyncivAPPLE(valid_fence, GL_INVALID_VALUE, 1, &len, &val);
	pass = piglit_check_gl_error(GL_INVALID_ENUM) && pass;

	glDeleteSyncAPPLE(valid_fence);

	piglit_report_result(pass ? PIGLIT_PASS : PIGLIT_FAIL);
}
