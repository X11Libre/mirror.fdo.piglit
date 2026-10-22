/* SPDX-License-Identifier: MIT */

/**
 * \file egl-shared-genbuffers-churn.c
 *
 * Tests that creating and destroying contexts which share buffer objects
 * with a context that is generating a lot of buffer names does not corrupt
 * the shared object table.
 *
 * One thread keeps calling glGenBuffers in small batches on a context with
 * glthread enabled, so the object name allocator grows its bitmap, and
 * deletes all names once it has generated a million. Another thread
 * creates a context in the same share group, generates names, and destroys
 * it again. Creating and destroying a context walks the shared table while
 * the first thread allocates names without waiting for its GL thread.
 */

#include "piglit-util-gl.h"
#include "piglit-util-egl.h"

#include <pthread.h>
#include <stdlib.h>
#include <unistd.h>

#define RUN_SECONDS 10
#define NAMES_PER_CALL 15
#define MAX_NAMES 1000000

static EGLDisplay dpy;
static EGLContext main_ctx;
static volatile bool stop;

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

static void *
generate_thread(void *arg)
{
	GLuint *names = malloc((MAX_NAMES + NAMES_PER_CALL) * sizeof(*names));
	unsigned count = 0;

	piglit_egl_bind_api(EGL_OPENGL_API);
	make_current(main_ctx);

	while (!stop) {
		glGenBuffers(NAMES_PER_CALL, names + count);
		count += NAMES_PER_CALL;
		if (count >= MAX_NAMES) {
			glDeleteBuffers(count, names);
			count = 0;
		}
	}

	glDeleteBuffers(count, names);
	glFinish();
	make_current(EGL_NO_CONTEXT);
	free(names);
	return NULL;
}

static void *
context_thread(void *arg)
{
	while (!stop) {
		GLuint names[16];
		EGLContext ctx = create_context(main_ctx);

		make_current(ctx);
		glGenBuffers(16, names);
		glFlush();
		make_current(EGL_NO_CONTEXT);
		eglDestroyContext(dpy, ctx);
	}
	return NULL;
}

int
main(int argc, char **argv)
{
	pthread_t gen, ctx;
	EGLint major, minor;

	/* The names are only reserved without waiting for the GL thread when
	 * glthread is enabled.
	 */
	setenv("mesa_glthread", "true", 0);

	dpy = piglit_egl_get_default_display(EGL_PLATFORM_SURFACELESS_MESA);
	if (!eglInitialize(dpy, &major, &minor))
		piglit_report_result(PIGLIT_SKIP);
	piglit_require_egl_extension(dpy, "EGL_KHR_no_config_context");
	piglit_require_egl_extension(dpy, "EGL_KHR_surfaceless_context");

	main_ctx = create_context(EGL_NO_CONTEXT);
	make_current(main_ctx);
	piglit_dispatch_default_init(PIGLIT_DISPATCH_GL);
	make_current(EGL_NO_CONTEXT);

	pthread_create(&gen, NULL, generate_thread, NULL);
	pthread_create(&ctx, NULL, context_thread, NULL);
	sleep(RUN_SECONDS);
	stop = true;
	pthread_join(gen, NULL);
	pthread_join(ctx, NULL);

	eglDestroyContext(dpy, main_ctx);

	/* Corruption shows up as a crash. */
	piglit_report_result(PIGLIT_PASS);
	return 0;
}
