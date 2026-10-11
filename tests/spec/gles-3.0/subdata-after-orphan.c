/*
 * SPDX-License-Identifier: MIT
 */

/**
 * reproducer for mesa!43541: zink copied the lock in
 * zink_context_replace_buffer_storage, and if another thread was holding it
 * at that moment, it stayed locked forever.
 *
 * it makes a second context so zink uses the lock. the copy makes the buffer
 * busy so glBufferData gives it new memory, and glFlush makes the driver
 * thread run the replace right away. the small writes take the lock on the
 * main thread, and if the replace copies it while one of them holds it, the
 * next write waits forever and the test fails.
 */

#include "piglit-util-gl.h"
#include "piglit-util-egl.h"

PIGLIT_GL_TEST_CONFIG_BEGIN

	config.supports_gl_es_version = 30;
	config.khr_no_error_support = PIGLIT_NO_ERRORS;

PIGLIT_GL_TEST_CONFIG_END

#define BUFFER_SIZE 65536
#define WRITE_SIZE 16
#define WRITES 64
#define SECONDS 3

static EGLContext
create_other_context(void)
{
	EGLDisplay dpy = eglGetCurrentDisplay();
	EGLConfig config = EGL_NO_CONFIG_KHR;
	EGLint config_id = 0, count;
	static const EGLint attribs[] = {
		EGL_CONTEXT_CLIENT_VERSION, 2,
		EGL_NONE,
	};

	eglQueryContext(dpy, eglGetCurrentContext(), EGL_CONFIG_ID, &config_id);
	if (config_id) {
		const EGLint config_attribs[] = {
			EGL_CONFIG_ID, config_id,
			EGL_NONE,
		};

		if (!eglChooseConfig(dpy, config_attribs, &config, 1, &count) ||
		    !count)
			return EGL_NO_CONTEXT;
	} else {
		piglit_require_egl_extension(dpy, "EGL_KHR_no_config_context");
	}

	return eglCreateContext(dpy, config, EGL_NO_CONTEXT, attribs);
}

enum piglit_result
piglit_display(void)
{
	return PIGLIT_FAIL;
}

void
piglit_init(int argc, char **argv)
{
	static const uint8_t data[WRITE_SIZE];
	EGLContext other = create_other_context();
	int64_t end;
	GLuint buffers[2];

	if (other == EGL_NO_CONTEXT) {
		printf("failed to create a second context\n");
		piglit_report_result(PIGLIT_FAIL);
	}

	piglit_set_timeout(SECONDS + 10, PIGLIT_FAIL);

	glGenBuffers(2, buffers);
	glBindBuffer(GL_COPY_WRITE_BUFFER, buffers[1]);
	glBufferData(GL_COPY_WRITE_BUFFER, BUFFER_SIZE, NULL, GL_STREAM_COPY);
	glBindBuffer(GL_COPY_READ_BUFFER, buffers[0]);
	glBufferData(GL_COPY_READ_BUFFER, BUFFER_SIZE, NULL, GL_STREAM_DRAW);

	end = piglit_time_get_nano() + SECONDS * INT64_C(1000000000);
	while (piglit_time_get_nano() < end) {
		glCopyBufferSubData(GL_COPY_READ_BUFFER, GL_COPY_WRITE_BUFFER,
				    0, 0, BUFFER_SIZE);
		glBufferData(GL_COPY_READ_BUFFER, BUFFER_SIZE, NULL,
			     GL_STREAM_DRAW);
		glFlush();
		for (int i = 0; i < WRITES; i++) {
			glBufferSubData(GL_COPY_READ_BUFFER, i * WRITE_SIZE,
					WRITE_SIZE, data);
		}
	}
	glFinish();

	glDeleteBuffers(2, buffers);
	eglDestroyContext(eglGetCurrentDisplay(), other);

	piglit_report_result(piglit_check_gl_error(GL_NO_ERROR) ?
			     PIGLIT_PASS : PIGLIT_FAIL);
}
