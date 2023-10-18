/*
 * Copyright © 2012 Intel Corporation
 * Copyright © 2023 Lucas Stach (adapted to GL ES)
 *
 * SPDX-License-Identifier: MIT
 */

/** @file repeat-wait.c
 *
 * From the GL_APPLE_sync spec:
 *
 *     "A return value of ALREADY_SIGNALED_APPLE indicates that <sync>
 *      was signaled at the time ClientWaitSyncAPPLE was called.
 *      ALREADY_SIGNALED_APPLE will always be returned if <sync> was
 *      signaled, even if the value of <timeout> is zero."
 *
 * There was concern that the implementation of the kernel API on i965
 * might violate this for the specific case of back-to-back
 * ClientWaitSyncs, but Mesa core doesn't end up calling into the
 * driver on a later ClientWaitSync.
 */

#include "piglit-util-gl.h"

PIGLIT_GL_TEST_CONFIG_BEGIN

	config.supports_gl_es_version = 20;

	config.window_width = 10;
	config.window_height = 10;
	config.window_visual = PIGLIT_GL_VISUAL_RGBA | PIGLIT_GL_VISUAL_DOUBLE;
	config.khr_no_error_support = PIGLIT_NO_ERRORS;

PIGLIT_GL_TEST_CONFIG_END

#define ONE_SECOND 1000000000

enum piglit_result
piglit_display(void)
{
	/* UNREACHED */
	return PIGLIT_FAIL;
}

void
piglit_init(int argc, char **argv)
{
	GLsync sync;
	GLenum ret1, ret2;

	piglit_require_extension("GL_APPLE_sync");

	glClear(GL_COLOR_BUFFER_BIT);

	sync = glFenceSyncAPPLE(GL_SYNC_GPU_COMMANDS_COMPLETE_APPLE, 0);

	ret1 = glClientWaitSyncAPPLE(sync, GL_SYNC_FLUSH_COMMANDS_BIT_APPLE, ONE_SECOND);
	ret2 = glClientWaitSyncAPPLE(sync, 0, ONE_SECOND);

	if (ret1 == GL_TIMEOUT_EXPIRED_APPLE) {
		printf("timeout expired on the first wait\n");
		piglit_report_result(PIGLIT_SKIP);
	}

	if (ret2 != GL_ALREADY_SIGNALED_APPLE) {
		fprintf(stderr,
			"Expected GL_ALREADY_SIGNALED_APPLE on second wait, got %s",
			piglit_get_gl_enum_name(ret2));
		piglit_report_result(PIGLIT_FAIL);
	}

	piglit_report_result(PIGLIT_PASS);
}
