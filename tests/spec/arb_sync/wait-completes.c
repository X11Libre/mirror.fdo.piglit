/* SPDX-License-Identifier: MIT */

/**
 * \file wait-completes.c
 *
 * Tests that glFinish, glClientWaitSync, and GL_SYNC_STATUS do not report
 * completion before the GPU has finished the preceding draws, using a
 * single context.
 *
 * Every iteration queues several flushed draws and one unflushed draw,
 * queues a GL_TIMESTAMP query behind them, and then waits in the way that
 * the mode selects:
 *
 * - finish: glFinish
 * - finish-flushed: glFlush, then glFinish
 * - client-wait: glClientWaitSync with GL_SYNC_FLUSH_COMMANDS_BIT and a
 *   long timeout
 * - client-wait-flushed: glFlush, then glClientWaitSync without the flush
 *   bit
 * - client-wait-short: repeated 0.5 ms waits with the flush bit
 * - client-wait-zero: glFlush, then repeated zero-timeout waits
 * - client-wait-zero-flush-bit: repeated zero-timeout waits with the flush
 *   bit
 * - get-status: glFlush, then polling GL_SYNC_STATUS
 * - no-work: glFlush before the sync is created, so that no commands are
 *   pending when it is created, then a wait without the flush bit
 * - mixed: cycles through all of the above
 *
 * An iteration fails if the result of the timestamp query is later than
 * GL_TIMESTAMP as read right after the wait.
 */

#include "wait-common.h"

PIGLIT_GL_TEST_CONFIG_BEGIN

	config.supports_gl_compat_version = 30;
	config.window_width = 64;
	config.window_height = 64;
	config.window_visual = PIGLIT_GL_VISUAL_RGBA | PIGLIT_GL_VISUAL_DOUBLE;
	config.khr_no_error_support = PIGLIT_NO_ERRORS;

PIGLIT_GL_TEST_CONFIG_END

#define ITERATIONS 150
#define BATCHES 10
#define WAIT_LIMIT_MS 60000.0

enum wait_mode {
	MODE_FINISH,
	MODE_CLIENT_WAIT,
	MODE_CLIENT_WAIT_FLUSHED,
	MODE_CLIENT_WAIT_SHORT,
	MODE_CLIENT_WAIT_ZERO,
	MODE_GET_STATUS,
	MODE_FINISH_FLUSHED,
	MODE_CLIENT_WAIT_ZERO_FLUSH_BIT,
	MODE_NO_WORK,
	MODE_MIXED,
	MODE_COUNT = MODE_MIXED,
};

static const char *mode_names[] = {
	"finish",
	"client-wait",
	"client-wait-flushed",
	"client-wait-short",
	"client-wait-zero",
	"get-status",
	"finish-flushed",
	"client-wait-zero-flush-bit",
	"no-work",
	"mixed",
};

static bool
signaled(GLenum ret, const char *what, unsigned iteration)
{
	if (ret == GL_ALREADY_SIGNALED || ret == GL_CONDITION_SATISFIED)
		return true;
	if (ret != GL_TIMEOUT_EXPIRED) {
		printf("%s, iteration %u: glClientWaitSync returned %s\n", what,
		       iteration, piglit_get_gl_enum_name(ret));
		piglit_report_result(PIGLIT_FAIL);
	}
	return false;
}

static bool
do_wait(enum wait_mode mode, unsigned iteration)
{
	const char *what = mode_names[mode];
	double start = wait_cpu_time_ms();
	GLsync sync;
	GLint status;
	bool done = false;

	if (mode == MODE_FINISH || mode == MODE_FINISH_FLUSHED) {
		if (mode == MODE_FINISH_FLUSHED)
			glFlush();
		glFinish();
		return true;
	}
	if (mode == MODE_NO_WORK)
		glFlush();

	sync = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
	switch (mode) {
	case MODE_CLIENT_WAIT:
		done = signaled(glClientWaitSync(sync, GL_SYNC_FLUSH_COMMANDS_BIT,
						 60 * ONE_SECOND), what, iteration);
		break;
	case MODE_CLIENT_WAIT_FLUSHED:
		glFlush();
		done = signaled(glClientWaitSync(sync, 0, 60 * ONE_SECOND), what,
				iteration);
		break;
	case MODE_CLIENT_WAIT_SHORT:
		while (!done && wait_cpu_time_ms() - start < WAIT_LIMIT_MS)
			done = signaled(glClientWaitSync(sync,
							 GL_SYNC_FLUSH_COMMANDS_BIT,
							 ONE_MS / 2), what, iteration);
		break;
	case MODE_CLIENT_WAIT_ZERO:
		glFlush();
		while (!done && wait_cpu_time_ms() - start < WAIT_LIMIT_MS)
			done = signaled(glClientWaitSync(sync, 0, 0), what,
					iteration);
		break;
	case MODE_NO_WORK:
		done = signaled(glClientWaitSync(sync, 0, 60 * ONE_SECOND), what,
				iteration);
		break;
	case MODE_CLIENT_WAIT_ZERO_FLUSH_BIT:
		while (!done && wait_cpu_time_ms() - start < WAIT_LIMIT_MS)
			done = signaled(glClientWaitSync(sync,
							 GL_SYNC_FLUSH_COMMANDS_BIT,
							 0), what, iteration);
		break;
	case MODE_GET_STATUS:
		glFlush();
		while (!done && wait_cpu_time_ms() - start < WAIT_LIMIT_MS) {
			glGetSynciv(sync, GL_SYNC_STATUS, 1, NULL, &status);
			done = status == GL_SIGNALED;
		}
		break;
	default:
		break;
	}
	glDeleteSync(sync);

	if (!done)
		printf("%s, iteration %u: sync never signaled\n", what, iteration);
	return done;
}

enum piglit_result
piglit_display(void)
{
	return PIGLIT_FAIL;
}

void
piglit_init(int argc, char **argv)
{
	enum wait_mode mode = MODE_COUNT + 1;
	unsigned failures = 0;

	for (int i = 1; i < argc; i++) {
		for (unsigned m = 0; m < ARRAY_SIZE(mode_names); m++) {
			if (!strcmp(argv[i], mode_names[m]))
				mode = m;
		}
	}
	if (mode > MODE_MIXED) {
		printf("usage: %s <mode>\n", argv[0]);
		for (unsigned m = 0; m < ARRAY_SIZE(mode_names); m++)
			printf("	%s\n", mode_names[m]);
		piglit_report_result(PIGLIT_FAIL);
	}

	wait_workload_init();
	wait_workload_calibrate(2.0);

	for (unsigned it = 0; it < ITERATIONS; it++) {
		enum wait_mode m = mode == MODE_MIXED ? it % MODE_COUNT : mode;
		GLuint query;

		wait_workload_draw(BATCHES, true);
		piglit_delay_ns((it % 16) * 40000);
		wait_workload_draw(1, false);
		query = wait_timestamp_queue();
		if (!do_wait(m, it)) {
			failures++;
			glDeleteQueries(1, &query);
			continue;
		}
		if (!wait_check_completed(query, mode_names[m], it))
			failures++;
	}

	wait_verify_workload(mode_names[mode]);
	if (!piglit_check_gl_error(GL_NO_ERROR))
		piglit_report_result(PIGLIT_FAIL);
	if (failures)
		printf("%u of %u waits failed\n", failures, ITERATIONS);
	piglit_report_result(failures ? PIGLIT_FAIL : PIGLIT_PASS);
}
