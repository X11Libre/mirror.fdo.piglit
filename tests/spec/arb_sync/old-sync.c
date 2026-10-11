/* SPDX-License-Identifier: MIT */

/**
 * \file old-sync.c
 *
 * Tests that sync objects which completed long ago keep reporting as
 * signaled.
 *
 * Every round creates a set of syncs (some flushed, some not), completes
 * them with glFinish, and submits more work so that the driver recycles
 * the resources that the syncs were tied to. It then checks every old sync
 * with glGetSynciv, a zero-timeout glClientWaitSync, or a 1 ms
 * glClientWaitSync with the flush bit, all of which must report the sync
 * as signaled.
 *
 * With the pending-sync argument, a new sync is created and left unflushed
 * while the old ones are checked.
 */

#include "wait-common.h"

PIGLIT_GL_TEST_CONFIG_BEGIN

	config.supports_gl_compat_version = 30;
	config.window_width = 64;
	config.window_height = 64;
	config.window_visual = PIGLIT_GL_VISUAL_RGBA | PIGLIT_GL_VISUAL_DOUBLE;
	config.khr_no_error_support = PIGLIT_NO_ERRORS;

PIGLIT_GL_TEST_CONFIG_END

#define ROUNDS 40
#define SYNCS 24
#define BUSY_BATCHES 24

enum piglit_result
piglit_display(void)
{
	return PIGLIT_FAIL;
}

static bool
check_old_sync(GLsync sync, unsigned round, unsigned idx)
{
	GLint status = 0;
	GLenum ret;

	switch ((round + idx) % 3) {
	case 0:
		glGetSynciv(sync, GL_SYNC_STATUS, 1, NULL, &status);
		if (status == GL_SIGNALED)
			return true;
		printf("round %u, sync %u: GL_SYNC_STATUS is not GL_SIGNALED "
		       "after glFinish\n", round, idx);
		return false;
	case 1:
		ret = glClientWaitSync(sync, 0, 0);
		break;
	default:
		ret = glClientWaitSync(sync, GL_SYNC_FLUSH_COMMANDS_BIT, ONE_MS);
		break;
	}
	if (ret == GL_ALREADY_SIGNALED)
		return true;
	printf("round %u, sync %u: glClientWaitSync returned %s for a sync "
	       "that glFinish completed\n", round, idx,
	       piglit_get_gl_enum_name(ret));
	return false;
}

void
piglit_init(int argc, char **argv)
{
	GLsync syncs[SYNCS];
	GLsync pending = NULL;
	bool pending_sync = false;
	unsigned failures = 0;

	for (int i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "pending-sync"))
			pending_sync = true;
	}

	wait_workload_init();
	wait_workload_calibrate(4.0);

	for (unsigned round = 0; round < ROUNDS; round++) {
		for (unsigned i = 0; i < SYNCS; i++) {
			wait_workload_draw(1, false);
			syncs[i] = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
			if ((round + i) & 1)
				glFlush();
		}
		glFinish();

		wait_workload_draw(BUSY_BATCHES + round % 5, true);
		if (round & 2)
			wait_workload_draw(1, false);
		if (pending_sync) {
			wait_workload_draw(1, false);
			pending = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
		}

		for (unsigned i = 0; i < SYNCS; i++) {
			if (!check_old_sync(syncs[i], round, i))
				failures++;
			glDeleteSync(syncs[i]);
		}
		if (pending_sync)
			glDeleteSync(pending);
		if (round % 4 == 3)
			glFinish();
	}

	glFinish();
	wait_verify_workload("old-sync");
	if (!piglit_check_gl_error(GL_NO_ERROR))
		piglit_report_result(PIGLIT_FAIL);
	if (failures)
		printf("%u checks failed\n", failures);
	piglit_report_result(failures ? PIGLIT_FAIL : PIGLIT_PASS);
}
