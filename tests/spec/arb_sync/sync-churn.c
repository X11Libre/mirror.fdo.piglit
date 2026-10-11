/* SPDX-License-Identifier: MIT */

/**
 * \file sync-churn.c
 *
 * Stress test for sync object creation and deletion.
 *
 * Creates and deletes 400,000 syncs, keeping a ring of 257 alive, with
 * occasional flushes and finishes and without waiting on any of them. At
 * the end, every sync that is still alive must become signaled.
 *
 * This is meant to catch crashes and corruption in the fence bookkeeping
 * of drivers which create and destroy fences on different threads.
 */

#include "wait-common.h"

PIGLIT_GL_TEST_CONFIG_BEGIN

	config.supports_gl_compat_version = 30;
	config.window_width = 64;
	config.window_height = 64;
	config.window_visual = PIGLIT_GL_VISUAL_RGBA | PIGLIT_GL_VISUAL_DOUBLE;
	config.khr_no_error_support = PIGLIT_NO_ERRORS;

PIGLIT_GL_TEST_CONFIG_END

#define ITERATIONS 400000
#define LIVE 257

enum piglit_result
piglit_display(void)
{
	return PIGLIT_FAIL;
}

void
piglit_init(int argc, char **argv)
{
	static GLsync syncs[LIVE];
	GLenum ret;

	piglit_require_extension("GL_ARB_sync");

	for (unsigned i = 0; i < ITERATIONS; i++) {
		unsigned slot = (i * 7) % LIVE;

		if (syncs[slot])
			glDeleteSync(syncs[slot]);
		glClear(GL_COLOR_BUFFER_BIT);
		syncs[slot] = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
		if (i % 5 == 0)
			glFlush();
		if (i % 20000 == 19999)
			glFinish();
	}

	for (unsigned i = 0; i < LIVE; i++) {
		ret = glClientWaitSync(syncs[i], GL_SYNC_FLUSH_COMMANDS_BIT,
				       60 * ONE_SECOND);
		if (ret != GL_ALREADY_SIGNALED && ret != GL_CONDITION_SATISFIED) {
			printf("sync %u: glClientWaitSync returned %s\n", i,
			       piglit_get_gl_enum_name(ret));
			piglit_report_result(PIGLIT_FAIL);
		}
		glDeleteSync(syncs[i]);
	}

	if (!piglit_check_gl_error(GL_NO_ERROR))
		piglit_report_result(PIGLIT_FAIL);
	piglit_report_result(PIGLIT_PASS);
}
