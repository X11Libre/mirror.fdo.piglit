/* SPDX-License-Identifier: MIT */

/**
 * \file egl-arb-sync-shared-context.c
 *
 * Tests that waiting on a sync object from a context other than the one
 * which created it does not report completion before the GPU has finished
 * the commands of the creating context.
 *
 * The test uses surfaceless EGL with desktop GL contexts in one share
 * group, each on its own thread. As in arb_sync-wait-completes, a
 * GL_TIMESTAMP query queued right before the sync gives the time at which
 * the GPU finished, and a wait or status query which reports the sync as
 * signaled before that time is a failure.
 *
 * Modes:
 *
 * - deferred: another context polls and waits on a sync which its creator
 *   has not flushed yet, then waits again after the creator has flushed
 * - deferred-blocked: the same, but the final wait starts before the
 *   creator flushes
 * - threaded: a second context waits on a stream of syncs while the
 *   creating context keeps drawing, creating syncs, and calling glFinish
 * - egl-fence: threaded, with EGL fence syncs
 * - released: the creating context is released from its thread before the
 *   main context waits on the sync
 * - destroyed: the creating context is destroyed before the wait
 * - churn: the main context waits on its own syncs, and checks that syncs
 *   completed by glFinish stay signaled, while another context submits
 *   small batches continuously
 * - churn-many: churn with 12 submitting contexts and without the old
 *   syncs
 * - content: after the wait, the waiting context reads the texture and the
 *   pixel pack buffer that the creating context rendered and read into,
 *   and both must have the contents from the current iteration
 * - unflushed: the creating context has a release behavior of none and is
 *   released without a flush; another context waits before and across the
 *   point where a third thread flushes or destroys the creator
 * - contend: 8 contexts draw and wait with glFinish or glClientWaitSync at
 *   the same time
 * - write-unflushed: a second context maps a shared buffer for writing
 *   while the main context holds unflushed usage of it from glReadPixels;
 *   the map must return within 10 seconds, and the write must be visible
 *   afterwards
 */

#include "piglit-util-egl.h"
#include "wait-common.h"

#include <pthread.h>
#include <semaphore.h>
#include <unistd.h>

#define DEFERRED_ITERATIONS 40
#define THREADED_JOBS 240
#define MAX_OUTSTANDING 6
#define BATCHES 8

static EGLDisplay dpy;
static EGLContext main_ctx;

struct wait_result {
	bool signaled;
	GLint64 time;
};

static EGLContext
create_context(EGLContext share)
{
	EGLContext ctx = EGL_NO_CONTEXT;

	if (piglit_egl_bind_api(EGL_OPENGL_API))
		ctx = eglCreateContext(dpy, EGL_NO_CONFIG_KHR, share, NULL);
	if (ctx == EGL_NO_CONTEXT) {
		printf("eglCreateContext failed\n");
		piglit_report_result(PIGLIT_FAIL);
	}
	return ctx;
}

static void
make_current(EGLContext ctx)
{
	if (!eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, ctx)) {
		printf("eglMakeCurrent failed\n");
		piglit_report_result(PIGLIT_FAIL);
	}
}

static struct wait_result
record(bool is_signaled)
{
	struct wait_result r = { is_signaled, wait_timestamp_now() };

	return r;
}

static struct wait_result
record_status(GLsync sync)
{
	GLint status = 0;

	glGetSynciv(sync, GL_SYNC_STATUS, 1, NULL, &status);
	return record(status == GL_SIGNALED);
}

static struct wait_result
record_wait(GLsync sync, GLuint64 timeout)
{
	GLenum ret = glClientWaitSync(sync, 0, timeout);

	if (ret == GL_WAIT_FAILED) {
		printf("glClientWaitSync returned GL_WAIT_FAILED\n");
		piglit_report_result(PIGLIT_FAIL);
	}
	return record(ret == GL_ALREADY_SIGNALED || ret == GL_CONDITION_SATISFIED);
}

static bool
check(const struct wait_result *r, GLint64 completed, const char *what,
      unsigned iteration)
{
	if (!r->signaled || completed <= r->time)
		return true;
	printf("%s, iteration %u: signaled %.3f ms before the GPU completed "
	       "the preceding commands\n", what, iteration,
	       (completed - r->time) / 1e6);
	return false;
}

static bool deferred_blocked;

struct deferred_data {
	GLsync sync;
	sem_t probed;
	sem_t flushed;
	struct wait_result results[4];
	double timed_wait_ms;
};

static void *
deferred_thread(void *arg)
{
	struct deferred_data *d = arg;
	EGLContext ctx = create_context(main_ctx);

	make_current(ctx);
	d->results[0] = record_status(d->sync);
	d->results[1] = record_wait(d->sync, 0);
	d->timed_wait_ms = wait_cpu_time_ms();
	d->results[2] = record_wait(d->sync, 20 * ONE_MS);
	d->timed_wait_ms = wait_cpu_time_ms() - d->timed_wait_ms;
	sem_post(&d->probed);
	if (!deferred_blocked)
		sem_wait(&d->flushed);
	d->results[3] = record_wait(d->sync, 60 * ONE_SECOND);
	make_current(EGL_NO_CONTEXT);
	eglDestroyContext(dpy, ctx);
	return NULL;
}

static unsigned
test_deferred(void)
{
	static const char *names[] = {
		"glGetSynciv before the creating context flushed",
		"zero-timeout wait before the creating context flushed",
		"20 ms wait before the creating context flushed",
		"wait after the creating context flushed",
	};
	unsigned failures = 0;

	for (unsigned it = 0; it < DEFERRED_ITERATIONS; it++) {
		struct deferred_data d;
		pthread_t thread;
		GLuint query;
		GLint64 completed;

		sem_init(&d.probed, 0, 0);
		sem_init(&d.flushed, 0, 0);

		wait_workload_draw(it % BATCHES, true);
		wait_workload_draw(1, false);
		query = wait_timestamp_queue();
		d.sync = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);

		pthread_create(&thread, NULL, deferred_thread, &d);
		sem_wait(&d.probed);
		if (deferred_blocked)
			usleep(2000 + (it % 4) * 1000);
		glFlush();
		sem_post(&d.flushed);
		pthread_join(thread, NULL);

		completed = wait_timestamp_result(query);
		for (unsigned i = 0; i < ARRAY_SIZE(names); i++) {
			if (!check(&d.results[i], completed, names[i], it))
				failures++;
		}
		if (!d.results[3].signaled) {
			printf("%s, iteration %u: timed out\n", names[3], it);
			failures++;
		}
		if (d.timed_wait_ms > 2000.0) {
			printf("%s, iteration %u: took %.0f ms\n", names[2], it,
			       d.timed_wait_ms);
			failures++;
		}
		glDeleteSync(d.sync);
		sem_destroy(&d.probed);
		sem_destroy(&d.flushed);
	}
	return failures;
}

static bool use_egl_sync;

static struct wait_result
record_egl_status(EGLSync sync)
{
	EGLAttrib status = 0;

	eglGetSyncAttrib(dpy, sync, EGL_SYNC_STATUS, &status);
	return record(status == EGL_SIGNALED);
}

static struct wait_result
record_egl_wait(EGLSync sync, EGLTime timeout)
{
	EGLint ret = eglClientWaitSync(dpy, sync, 0, timeout);

	if (ret == EGL_FALSE) {
		printf("eglClientWaitSync failed\n");
		piglit_report_result(PIGLIT_FAIL);
	}
	return record(ret == EGL_CONDITION_SATISFIED);
}

struct threaded_data {
	EGLSync egl_syncs[THREADED_JOBS];
	GLsync syncs[THREADED_JOBS];
	struct wait_result results[THREADED_JOBS];
	sem_t queued;
	sem_t done;
};

static void *
threaded_thread(void *arg)
{
	struct threaded_data *d = arg;
	EGLContext ctx = create_context(main_ctx);

	make_current(ctx);
	for (unsigned i = 0; i < THREADED_JOBS; i++) {
		double start = wait_cpu_time_ms();

		sem_wait(&d->queued);
		do {
			GLuint64 timeout = i % 4 == 0 ? 60 * ONE_SECOND :
					   i % 4 == 3 ? ONE_MS / 4 : 0;

			if (use_egl_sync && i % 4 == 2)
				d->results[i] = record_egl_status(d->egl_syncs[i]);
			else if (use_egl_sync)
				d->results[i] = record_egl_wait(d->egl_syncs[i], timeout);
			else if (i % 4 == 2)
				d->results[i] = record_status(d->syncs[i]);
			else
				d->results[i] = record_wait(d->syncs[i], timeout);
		} while (i % 4 && !d->results[i].signaled &&
			 wait_cpu_time_ms() - start < 60000.0);
		sem_post(&d->done);
	}
	make_current(EGL_NO_CONTEXT);
	eglDestroyContext(dpy, ctx);
	return NULL;
}

static unsigned
test_threaded(void)
{
	static struct threaded_data d;
	static GLuint queries[THREADED_JOBS];
	pthread_t thread;
	unsigned failures = 0;

	sem_init(&d.queued, 0, 0);
	sem_init(&d.done, 0, MAX_OUTSTANDING);
	pthread_create(&thread, NULL, threaded_thread, &d);

	for (unsigned i = 0; i < THREADED_JOBS; i++) {
		sem_wait(&d.done);
		wait_workload_draw(i % 5, true);
		wait_workload_draw(1, false);
		queries[i] = wait_timestamp_queue();
		if (use_egl_sync)
			d.egl_syncs[i] = eglCreateSync(dpy, EGL_SYNC_FENCE, NULL);
		else
			d.syncs[i] = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
		if (i % 3)
			glFlush();
		sem_post(&d.queued);
		usleep((i % 8) * 50);
		if (i % 7 == 6) {
			GLuint query;

			wait_workload_draw(1, false);
			query = wait_timestamp_queue();
			glFinish();
			if (!wait_check_completed(query, "glFinish in the creating "
						  "context", i))
				failures++;
		}
	}
	glFlush();
	pthread_join(thread, NULL);

	for (unsigned i = 0; i < THREADED_JOBS; i++) {
		GLint64 completed = wait_timestamp_result(queries[i]);

		if (!d.results[i].signaled) {
			printf("wait from another context, job %u: timed out\n", i);
			failures++;
		} else if (!check(&d.results[i], completed,
				  "wait from another context", i)) {
			failures++;
		}
		if (use_egl_sync)
			eglDestroySync(dpy, d.egl_syncs[i]);
		else
			glDeleteSync(d.syncs[i]);
	}
	return failures;
}

struct creator_data {
	bool destroy;
	unsigned iteration;
	EGLContext ctx;
	GLsync sync;
	GLuint query;
};

static void *
creator_thread(void *arg)
{
	struct creator_data *d = arg;

	d->ctx = create_context(main_ctx);
	make_current(d->ctx);
	wait_workload_init();
	wait_workload_draw(d->iteration % BATCHES, true);
	wait_workload_draw(1, false);
	d->query = wait_timestamp_queue();
	d->sync = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
	if (d->iteration & 1)
		glFlush();
	make_current(EGL_NO_CONTEXT);
	if (d->destroy)
		eglDestroyContext(dpy, d->ctx);
	return NULL;
}

static void *
query_thread(void *arg)
{
	struct creator_data *d = arg;
	GLint64 *completed = malloc(sizeof(*completed));

	make_current(d->ctx);
	*completed = wait_timestamp_result(d->query);
	make_current(EGL_NO_CONTEXT);
	eglDestroyContext(dpy, d->ctx);
	return completed;
}

static unsigned
test_creator(bool destroy)
{
	const char *what = destroy ? "wait after the creating context was destroyed" :
				     "wait after the creating context was released";
	unsigned failures = 0;

	for (unsigned it = 0; it < DEFERRED_ITERATIONS; it++) {
		struct creator_data d = { destroy, it };
		struct wait_result r;
		pthread_t thread;
		double start;

		pthread_create(&thread, NULL, creator_thread, &d);
		pthread_join(thread, NULL);
		start = wait_cpu_time_ms();

		switch (it % 3) {
		case 0:
			r = record_wait(d.sync, 60 * ONE_SECOND);
			break;
		case 1:
			do {
				r = record_wait(d.sync, 0);
			} while (!r.signaled && wait_cpu_time_ms() - start < 60000.0);
			break;
		default:
			do {
				r = record_status(d.sync);
			} while (!r.signaled && wait_cpu_time_ms() - start < 60000.0);
			break;
		}
		if (!r.signaled) {
			printf("%s, iteration %u: timed out\n", what, it);
			failures++;
		}
		if (!destroy) {
			void *completed;

			pthread_create(&thread, NULL, query_thread, &d);
			pthread_join(thread, &completed);
			if (!check(&r, *(GLint64 *)completed, what, it))
				failures++;
			free(completed);
		}
		glDeleteSync(d.sync);
	}
	return failures;
}

static volatile bool churn_stop;

static void *
churn_thread(void *arg)
{
	EGLContext ctx = create_context(main_ctx);
	GLuint tex, fbo;

	make_current(ctx);
	glGenTextures(1, &tex);
	glBindTexture(GL_TEXTURE_2D, tex);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 16, 16, 0, GL_RGBA,
		     GL_UNSIGNED_BYTE, NULL);
	glGenFramebuffers(1, &fbo);
	glBindFramebuffer(GL_FRAMEBUFFER, fbo);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
			       GL_TEXTURE_2D, tex, 0);
	for (unsigned i = 0; !churn_stop; i++) {
		glClearColor(0, (i & 255) / 255.0, 0, 1);
		glClear(GL_COLOR_BUFFER_BIT);
		if (i % 64 == 63)
			glFinish();
		else
			glFlush();
	}
	make_current(EGL_NO_CONTEXT);
	eglDestroyContext(dpy, ctx);
	return NULL;
}

#define CHURN_ROUNDS 60
#define CHURN_SYNCS 8

#define CHURN_MAX_THREADS 12

static unsigned
test_churn(unsigned num_threads, unsigned rounds, unsigned old_syncs)
{
	pthread_t threads[CHURN_MAX_THREADS];
	unsigned failures = 0;

	for (unsigned i = 0; i < num_threads; i++)
		pthread_create(&threads[i], NULL, churn_thread, NULL);
	for (unsigned round = 0; round < rounds; round++) {
		GLsync old[CHURN_SYNCS];
		GLsync sync;
		GLuint query;
		GLenum ret;

		for (unsigned i = 0; i < old_syncs; i++) {
			wait_workload_draw(1, false);
			old[i] = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
			glFlush();
		}
		if (old_syncs) {
			glFinish();
			usleep(1000 + (round % 5) * 1000);
		}

		wait_workload_draw(round % BATCHES, true);
		wait_workload_draw(1, false);
		query = wait_timestamp_queue();
		sync = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
		glFlush();

		for (unsigned i = 0; i < old_syncs; i++) {
			ret = glClientWaitSync(old[i], 0, 0);
			if (ret != GL_ALREADY_SIGNALED) {
				printf("round %u, sync %u: glClientWaitSync returned "
				       "%s for a sync that glFinish completed\n",
				       round, i, piglit_get_gl_enum_name(ret));
				failures++;
			}
			glDeleteSync(old[i]);
		}

		if (round & 1) {
			ret = glClientWaitSync(sync, 0, 60 * ONE_SECOND);
		} else {
			do {
				ret = glClientWaitSync(sync, 0, 0);
			} while (ret == GL_TIMEOUT_EXPIRED);
		}
		if (ret != GL_ALREADY_SIGNALED && ret != GL_CONDITION_SATISFIED) {
			printf("round %u: glClientWaitSync returned %s\n", round,
			       piglit_get_gl_enum_name(ret));
			failures++;
			glDeleteQueries(1, &query);
		} else if (!wait_check_completed(query, "wait while another "
						 "context submits", round)) {
			failures++;
		}
		glDeleteSync(sync);
	}
	churn_stop = true;
	for (unsigned i = 0; i < num_threads; i++)
		pthread_join(threads[i], NULL);
	return failures;
}

#define CONTENT_ITERATIONS 120

struct content_data {
	GLuint tex;
	GLuint pbo;
	GLsync sync;
	bool stop;
	sem_t queued;
	sem_t done;
	GLubyte tex_pixel[4];
	GLubyte pbo_pixel[4];
	bool signaled;
};

static void *
content_thread(void *arg)
{
	struct content_data *d = arg;
	EGLContext ctx = create_context(main_ctx);
	GLuint fbo;

	make_current(ctx);
	glGenFramebuffers(1, &fbo);
	glBindFramebuffer(GL_FRAMEBUFFER, fbo);
	sem_post(&d->done);
	for (unsigned i = 0;; i++) {
		double start;

		sem_wait(&d->queued);
		if (d->stop)
			break;
		start = wait_cpu_time_ms();
		do {
			d->signaled = record_wait(d->sync, i & 1 ? 0 : 60 * ONE_SECOND).signaled;
		} while (!d->signaled && wait_cpu_time_ms() - start < 60000.0);

		glBindTexture(GL_TEXTURE_2D, d->tex);
		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
				       GL_TEXTURE_2D, d->tex, 0);
		glReadPixels(5, 5, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, d->tex_pixel);
		glBindBuffer(GL_ARRAY_BUFFER, d->pbo);
		glGetBufferSubData(GL_ARRAY_BUFFER, 0, 4, d->pbo_pixel);
		sem_post(&d->done);
	}
	make_current(EGL_NO_CONTEXT);
	eglDestroyContext(dpy, ctx);
	return NULL;
}

static unsigned
test_content(void)
{
	static const float green[] = {0, 1, 0, 1};
	struct content_data d = {0};
	pthread_t thread;
	unsigned failures = 0;

	d.tex = wait_workload_texture();
	glGenBuffers(1, &d.pbo);
	glBindBuffer(GL_PIXEL_PACK_BUFFER, d.pbo);
	glBufferData(GL_PIXEL_PACK_BUFFER, 16 * 16 * 4, NULL, GL_STREAM_READ);
	sem_init(&d.queued, 0, 0);
	sem_init(&d.done, 0, 0);
	pthread_create(&thread, NULL, content_thread, &d);
	sem_wait(&d.done);

	for (unsigned it = 0; it < CONTENT_ITERATIONS; it++) {
		unsigned bits = it % 7 + 1;
		const float color[] = { bits & 1, (bits >> 1) & 1, (bits >> 2) & 1, 1 };
		GLubyte expected[4];

		for (unsigned c = 0; c < 4; c++)
			expected[c] = color[c] * 255;

		wait_workload_set_color(color);
		wait_workload_draw(it % 5, true);
		wait_workload_draw(1, false);
		glReadPixels(0, 0, 16, 16, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
		d.sync = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
		if (it % 3)
			glFlush();
		sem_post(&d.queued);
		sem_wait(&d.done);

		if (!d.signaled) {
			printf("iteration %u: timed out\n", it);
			failures++;
		}
		if (memcmp(d.tex_pixel, expected, 4)) {
			printf("iteration %u: texture read from another context "
			       "after the wait is %u %u %u %u, expected %u %u %u %u\n",
			       it, d.tex_pixel[0], d.tex_pixel[1], d.tex_pixel[2],
			       d.tex_pixel[3], expected[0], expected[1], expected[2],
			       expected[3]);
			failures++;
		}
		if (memcmp(d.pbo_pixel, expected, 4)) {
			printf("iteration %u: buffer read from another context "
			       "after the wait is %u %u %u %u, expected %u %u %u %u\n",
			       it, d.pbo_pixel[0], d.pbo_pixel[1], d.pbo_pixel[2],
			       d.pbo_pixel[3], expected[0], expected[1], expected[2],
			       expected[3]);
			failures++;
		}
		glDeleteSync(d.sync);
	}
	d.stop = true;
	sem_post(&d.queued);
	pthread_join(thread, NULL);

	glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
	wait_workload_set_color(green);
	wait_workload_draw(1, false);
	return failures;
}

#define UNFLUSHED_ITERATIONS 20

struct unflushed_data {
	bool destroy;
	unsigned iteration;
	EGLContext creator;
	EGLContext waiter;
	GLsync sync;
	GLuint query;
	GLint64 completed;
	sem_t probed;
	struct wait_result results[2];
};

static void *
unflushed_creator_thread(void *arg)
{
	static const EGLint attribs[] = {
		EGL_CONTEXT_RELEASE_BEHAVIOR_KHR,
		EGL_CONTEXT_RELEASE_BEHAVIOR_NONE_KHR,
		EGL_NONE
	};
	struct unflushed_data *d = arg;

	if (piglit_egl_bind_api(EGL_OPENGL_API))
		d->creator = eglCreateContext(dpy, EGL_NO_CONFIG_KHR,
					      EGL_NO_CONTEXT, attribs);
	if (d->creator == EGL_NO_CONTEXT) {
		printf("eglCreateContext failed\n");
		piglit_report_result(PIGLIT_FAIL);
	}
	make_current(d->creator);
	wait_workload_init();
	glFinish();
	wait_workload_draw(d->iteration % 4, true);
	wait_workload_draw(1, false);
	d->query = wait_timestamp_queue();
	d->sync = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
	d->waiter = create_context(d->creator);
	make_current(EGL_NO_CONTEXT);
	return NULL;
}

static void *
unflushed_waiter_thread(void *arg)
{
	struct unflushed_data *d = arg;

	make_current(d->waiter);
	d->results[0] = record_wait(d->sync, 50 * ONE_MS);
	sem_post(&d->probed);
	d->results[1] = record_wait(d->sync, 60 * ONE_SECOND);
	make_current(EGL_NO_CONTEXT);
	return NULL;
}

static void *
unflushed_flush_thread(void *arg)
{
	struct unflushed_data *d = arg;

	if (d->destroy) {
		eglDestroyContext(dpy, d->creator);
		return NULL;
	}
	make_current(d->creator);
	glFlush();
	d->completed = wait_timestamp_result(d->query);
	make_current(EGL_NO_CONTEXT);
	eglDestroyContext(dpy, d->creator);
	return NULL;
}

static unsigned
test_unflushed(void)
{
	static const char *names[] = {
		"wait before the released creating context flushed",
		"wait across the flush of the released creating context",
	};
	unsigned failures = 0;

	piglit_require_egl_extension(dpy, "EGL_KHR_context_flush_control");

	for (unsigned it = 0; it < UNFLUSHED_ITERATIONS; it++) {
		struct unflushed_data d = { it & 1, it };
		pthread_t creator, waiter, flusher;

		sem_init(&d.probed, 0, 0);
		pthread_create(&creator, NULL, unflushed_creator_thread, &d);
		pthread_join(creator, NULL);
		pthread_create(&waiter, NULL, unflushed_waiter_thread, &d);
		sem_wait(&d.probed);
		usleep(2000);
		pthread_create(&flusher, NULL, unflushed_flush_thread, &d);
		pthread_join(flusher, NULL);
		pthread_join(waiter, NULL);

		if (!d.results[1].signaled) {
			printf("%s, iteration %u: timed out\n", names[1], it);
			failures++;
		}
		for (unsigned i = 0; !d.destroy && i < 2; i++) {
			if (!check(&d.results[i], d.completed, names[i], it))
				failures++;
		}
		eglDestroyContext(dpy, d.waiter);
		sem_destroy(&d.probed);
	}
	return failures;
}

#define CONTEND_THREADS 8
#define CONTEND_ITERATIONS 80

static void *
contend_thread(void *arg)
{
	EGLContext ctx = create_context(main_ctx);
	unsigned *failures = arg;

	make_current(ctx);
	wait_workload_init();
	glFinish();
	for (unsigned it = 0; it < CONTEND_ITERATIONS; it++) {
		GLuint query;

		wait_workload_draw(1, false);
		query = wait_timestamp_queue();
		if (it & 1) {
			glFinish();
		} else {
			GLsync sync = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);

			glFlush();
			glClientWaitSync(sync, 0, 60 * ONE_SECOND);
			glDeleteSync(sync);
		}
		if (!wait_check_completed(query, "wait while other contexts "
					  "submit", it))
			(*failures)++;
	}
	make_current(EGL_NO_CONTEXT);
	eglDestroyContext(dpy, ctx);
	return NULL;
}

static unsigned
test_contend(void)
{
	pthread_t threads[CONTEND_THREADS];
	unsigned thread_failures[CONTEND_THREADS] = {0};
	unsigned failures = 0;

	for (unsigned i = 0; i < CONTEND_THREADS; i++)
		pthread_create(&threads[i], NULL, contend_thread,
			       &thread_failures[i]);
	for (unsigned i = 0; i < CONTEND_THREADS; i++) {
		pthread_join(threads[i], NULL);
		failures += thread_failures[i];
	}
	return failures;
}

#define WRITE_ITERATIONS 200

struct write_data {
	GLuint buffer;
	sem_t start;
	sem_t done;
	bool stop;
	double map_ms;
	bool mapped;
};

static void *
write_thread(void *arg)
{
	struct write_data *d = arg;
	EGLContext ctx = create_context(main_ctx);

	make_current(ctx);
	glBindBuffer(GL_ARRAY_BUFFER, d->buffer);
	sem_post(&d->done);
	for (unsigned i = 0;; i++) {
		double start;
		GLubyte *map;

		sem_wait(&d->start);
		if (d->stop)
			break;
		start = wait_cpu_time_ms();
		map = glMapBufferRange(GL_ARRAY_BUFFER, 64, 4, GL_MAP_WRITE_BIT);
		d->mapped = map != NULL;
		if (map) {
			memset(map, i & 255, 4);
			glUnmapBuffer(GL_ARRAY_BUFFER);
		}
		glFinish();
		d->map_ms = wait_cpu_time_ms() - start;
		sem_post(&d->done);
	}
	make_current(EGL_NO_CONTEXT);
	eglDestroyContext(dpy, ctx);
	return NULL;
}

static unsigned
test_write_unflushed(void)
{
	struct write_data d = {0};
	pthread_t thread;
	unsigned failures = 0;

	glGenBuffers(1, &d.buffer);
	glBindBuffer(GL_PIXEL_PACK_BUFFER, d.buffer);
	glBufferData(GL_PIXEL_PACK_BUFFER, 16 * 16 * 4, NULL, GL_DYNAMIC_DRAW);
	sem_init(&d.start, 0, 0);
	sem_init(&d.done, 0, 0);
	pthread_create(&thread, NULL, write_thread, &d);
	sem_wait(&d.done);

	for (unsigned it = 0; it < WRITE_ITERATIONS; it++) {
		struct timespec deadline;
		GLubyte pixel[4];

		wait_workload_draw(1, false);
		glReadPixels(0, 0, 4, 4, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
		sem_post(&d.start);
		clock_gettime(CLOCK_REALTIME, &deadline);
		deadline.tv_sec += 10;
		if (sem_timedwait(&d.done, &deadline)) {
			printf("iteration %u: a write map in another context did not "
			       "return while this context held unflushed usage\n", it);
			piglit_report_result(PIGLIT_FAIL);
		}
		if (!d.mapped) {
			printf("iteration %u: glMapBufferRange failed\n", it);
			failures++;
		}
		glFinish();
		glGetBufferSubData(GL_PIXEL_PACK_BUFFER, 64, 4, pixel);
		if (pixel[0] != (it & 255) || pixel[3] != (it & 255)) {
			printf("iteration %u: the other context's write is missing "
			       "(%u %u %u %u)\n", it, pixel[0], pixel[1], pixel[2],
			       pixel[3]);
			failures++;
		}
	}
	d.stop = true;
	sem_post(&d.start);
	pthread_join(thread, NULL);
	glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
	return failures;
}

int
main(int argc, char **argv)
{
	EGLint major, minor;
	static const char *modes[] = {
		"deferred", "deferred-blocked", "threaded", "released",
		"destroyed", "churn", "egl-fence", "content", "unflushed",
		"churn-many", "contend", "write-unflushed",
	};
	int mode = -1;
	unsigned failures;

	for (int i = 1; i < argc; i++) {
		for (unsigned m = 0; m < ARRAY_SIZE(modes); m++) {
			if (!strcmp(argv[i], modes[m]))
				mode = m;
		}
	}
	if (mode < 0) {
		printf("usage: %s <mode>\n", argv[0]);
		for (unsigned m = 0; m < ARRAY_SIZE(modes); m++)
			printf("	%s\n", modes[m]);
		piglit_report_result(PIGLIT_FAIL);
	}

	dpy = piglit_egl_get_default_display(EGL_PLATFORM_SURFACELESS_MESA);
	if (!eglInitialize(dpy, &major, &minor))
		piglit_report_result(PIGLIT_SKIP);
	piglit_require_egl_extension(dpy, "EGL_KHR_no_config_context");
	piglit_require_egl_extension(dpy, "EGL_KHR_surfaceless_context");
	main_ctx = create_context(EGL_NO_CONTEXT);
	make_current(main_ctx);
	piglit_dispatch_default_init(PIGLIT_DISPATCH_GL);

	wait_workload_init();
	wait_workload_calibrate(2.0);

	{
		GLsync sync = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
		GLint status;

		glGetSynciv(sync, GL_SYNC_STATUS, 1, NULL, &status);
		glClientWaitSync(sync, GL_SYNC_FLUSH_COMMANDS_BIT, ONE_SECOND);
		glDeleteSync(sync);
		wait_timestamp_now();
	}

	switch (mode) {
	case 0:
		failures = test_deferred();
		break;
	case 1:
		deferred_blocked = true;
		failures = test_deferred();
		break;
	case 2:
		failures = test_threaded();
		break;
	case 3:
		failures = test_creator(false);
		break;
	case 4:
		failures = test_creator(true);
		break;
	case 5:
		failures = test_churn(1, CHURN_ROUNDS, CHURN_SYNCS);
		break;
	case 6:
		if (major == 1 && minor < 5)
			piglit_report_result(PIGLIT_SKIP);
		use_egl_sync = true;
		failures = test_threaded();
		break;
	case 7:
		failures = test_content();
		break;
	case 8:
		failures = test_unflushed();
		break;
	case 9:
		failures = test_churn(CHURN_MAX_THREADS, 5 * CHURN_ROUNDS, 0);
		break;
	case 10:
		failures = test_contend();
		break;
	default:
		failures = test_write_unflushed();
		break;
	}

	glFinish();
	wait_verify_workload(modes[mode]);
	if (!piglit_check_gl_error(GL_NO_ERROR))
		piglit_report_result(PIGLIT_FAIL);
	if (failures)
		printf("%u checks failed\n", failures);
	piglit_report_result(failures ? PIGLIT_FAIL : PIGLIT_PASS);
}
