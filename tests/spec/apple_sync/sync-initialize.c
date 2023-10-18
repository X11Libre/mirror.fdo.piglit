/*
 * Copyright © 2013 Intel Corporation
 * Copyright © 2023 Lucas Stach (adapted to GL ES)
 *
 * SPDX-License-Identifier: MIT
 */

/**
 * \file
 * Test that a sync is initialized correctly with FenceSyncAPPLE
 *
 * Section 5.2 (Sync Objects and Fences) of the extension spec says:
 * 	"Table 5.props: Initial properties of a sync object
 *       created with FenceSyncAPPLE."
 *
 * 	 Property Name	        Property Value
 * 	--------------------------------------
 * 	 OBJECT_TYPE_APPLE	SYNC_FENCE_APPLE
 * 	 SYNC_CONDITION_APPLE	<condition>
 * 	 SYNC_STATUS_APPLE	UNSIGNALED_APPLE
 * 	 SYNC_FLAGS_APPLE	<flags>
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
	GLsizei length = -5;
	GLint value;
	GLsync sync;

	piglit_require_extension("GL_APPLE_sync");

	/* Create a new fence sync */
	sync = glFenceSyncAPPLE(GL_SYNC_GPU_COMMANDS_COMPLETE_APPLE, 0);

	/* Test initialized as fence type */
	glGetSyncivAPPLE(sync, GL_OBJECT_TYPE_APPLE, 1, &length, &value);
	if (length != 1) {
		printf("length should be 1 but incorrectly returned: %d\n",
			length);
		pass = false;
	}
	if (value != GL_SYNC_FENCE_APPLE) {
		printf("Expected GL_SYNC_FENCE_APPLE but returned: %s\n",
			piglit_get_gl_enum_name(value));
		pass = false;
	}

	/* Test initialized to given condition */
	length = -5;
	glGetSyncivAPPLE(sync, GL_SYNC_CONDITION_APPLE, 1, &length, &value);
	if (length != 1) {
		printf("length should be 1 but incorrectly returned: %d\n",
			length);
		pass = false;
	}
	if (value != GL_SYNC_GPU_COMMANDS_COMPLETE_APPLE) {
		printf("Expected GL_SYNC_GPU_COMMANDS_COMPLETE_APPLE but returned: %s\n",
			piglit_get_gl_enum_name(value));
		pass = false;
	}

	/* Test initialized to unsignaled */
	length = -5;
	glGetSyncivAPPLE(sync, GL_SYNC_STATUS_APPLE, 1, &length, &value);
	if (length != 1) {
		printf("length should be 1 but incorrectly returned: %d\n",
			length);
		pass = false;
	}
	/* We can't test for just GL_UNSIGNALED_APPLE here, since the
	 * driver may have actually completed any previous rendering
	 * (or, in our case, no rendering at all) already.
	 */
	if (value != GL_UNSIGNALED_APPLE && value != GL_SIGNALED_APPLE) {
		printf("Expected GL_UNSIGNALED_APPLE or GL_SIGNALED_APPLE but returned: %s\n",
			piglit_get_gl_enum_name(value));
		pass = false;
	}

	/* Test initialized with given flag */
	length = -5;
	glGetSyncivAPPLE(sync, GL_SYNC_FLAGS_APPLE, 1, &length, &value);
	if (length != 1) {
		printf("length should be 1 but incorrectly returned: %d\n",
			length);
		pass = false;
	}
	if (value != 0) {
		printf("Expected GL_SYNC_FLAGS_APPLE == 0 but returned: %d\n",
			value);
		pass = false;
	}

	glDeleteSyncAPPLE(sync);

	piglit_report_result(pass ? PIGLIT_PASS : PIGLIT_FAIL);
}
