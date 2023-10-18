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
 *     "ALREADY_SIGNALED_APPLE will always be returned if <sync> was
 *      signaled, even if the value of <timeout> is zero.
 *
 *      ...
 *
 *      If the value of <timeout> is zero, then ClientWaitSyncAPPLE
 *      does not block, but simply tests the current state of <sync>.
 *      TIMEOUT_EXPIRED_APPLE will be returned in this case if <sync>
 *      is not signaled, even though no actual wait was performed."
 *
 */

#include "piglit-util-gl.h"

PIGLIT_GL_TEST_CONFIG_BEGIN

	config.supports_gl_es_version = 20;

	config.window_width = 10;
	config.window_height = 10;
	config.window_visual = PIGLIT_GL_VISUAL_RGBA | PIGLIT_GL_VISUAL_DOUBLE;
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
	GLsync sync;
	GLenum ret1, ret2;
	bool pass = true;


	piglit_require_extension("GL_APPLE_sync");

	glClear(GL_COLOR_BUFFER_BIT);
	sync = glFenceSyncAPPLE(GL_SYNC_GPU_COMMANDS_COMPLETE_APPLE, 0);
	ret1 = glClientWaitSyncAPPLE(sync, GL_SYNC_FLUSH_COMMANDS_BIT_APPLE, 0);
	glFinish();
	ret2 = glClientWaitSyncAPPLE(sync, 0, 0);

	glDeleteSyncAPPLE(sync);

	if (ret1 != GL_TIMEOUT_EXPIRED_APPLE &&
	    ret1 != GL_ALREADY_SIGNALED_APPLE) {
		fprintf(stderr,
			"On first wait:\n"
			"  Expected GL_ALREADY_SIGNALED_APPLE or GL_TIMEOUT_EXPIRED_APPLE\n"
			"  Got %s\n",
			piglit_get_gl_enum_name(ret1));
		pass = false;
	}

	if (ret2 != GL_ALREADY_SIGNALED_APPLE) {
		fprintf(stderr,
			"On repeated wait:\n"
			"  Expected GL_ALREADY_SIGNALED_APPLE\n"
			"  Got %s\n",
			piglit_get_gl_enum_name(ret2));
		pass = false;
	}

	glClear(GL_COLOR_BUFFER_BIT);
	sync = glFenceSyncAPPLE(GL_SYNC_GPU_COMMANDS_COMPLETE_APPLE, 0);
	glFinish();
	ret1 = glClientWaitSyncAPPLE(sync, GL_SYNC_FLUSH_COMMANDS_BIT_APPLE, 0);

	if (ret1 != GL_ALREADY_SIGNALED_APPLE) {
		fprintf(stderr,
			"On wait after a finish:\n"
			"  Expected GL_ALREADY_SIGNALED_APPLE\n"
			"  Got %s\n",
			piglit_get_gl_enum_name(ret1));
		pass = false;
	}

	piglit_report_result(pass ? PIGLIT_PASS : PIGLIT_FAIL);
}
