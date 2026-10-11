/* SPDX-License-Identifier: MIT */

/**
 * \file flush-readback.c
 *
 * Tests that reading back a pixel pack buffer right after glFlush returns
 * the data of the preceding glReadPixels.
 *
 * Every iteration clears a 4x4 framebuffer to a color derived from the
 * iteration number, reads it into a pixel pack buffer, calls glFlush, and
 * reads the buffer back, alternating between glGetBufferSubData and
 * glMapBufferRange. The data must be the color of the current iteration
 * and not that of an earlier one.
 *
 * The number of iterations defaults to 300,000 and can be passed as an
 * argument.
 */

#include "piglit-util-gl.h"

PIGLIT_GL_TEST_CONFIG_BEGIN

	config.supports_gl_compat_version = 30;
	config.window_width = 64;
	config.window_height = 64;
	config.window_visual = PIGLIT_GL_VISUAL_RGBA | PIGLIT_GL_VISUAL_DOUBLE;
	config.khr_no_error_support = PIGLIT_NO_ERRORS;

PIGLIT_GL_TEST_CONFIG_END

#define ITERATIONS 300000
#define SIZE 4

enum piglit_result
piglit_display(void)
{
	return PIGLIT_FAIL;
}

void
piglit_init(int argc, char **argv)
{
	GLuint tex, fbo, pbo;
	unsigned failures = 0;
	unsigned iterations = ITERATIONS;

	for (int i = 1; i < argc; i++) {
		if (atoi(argv[i]) > 0)
			iterations = atoi(argv[i]);
	}

	glGenTextures(1, &tex);
	glBindTexture(GL_TEXTURE_2D, tex);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, SIZE, SIZE, 0, GL_RGBA,
		     GL_UNSIGNED_BYTE, NULL);
	glGenFramebuffers(1, &fbo);
	glBindFramebuffer(GL_FRAMEBUFFER, fbo);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
			       GL_TEXTURE_2D, tex, 0);
	if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
		piglit_report_result(PIGLIT_SKIP);

	glGenBuffers(1, &pbo);
	glBindBuffer(GL_PIXEL_PACK_BUFFER, pbo);
	glBufferData(GL_PIXEL_PACK_BUFFER, SIZE * SIZE * 4, NULL, GL_STREAM_READ);

	for (unsigned i = 0; i < iterations; i++) {
		GLubyte expected[4] = { i & 255, (i >> 8) & 255, (i * 7) & 255, 255 };
		GLubyte pixel[4];

		glClearColor(expected[0] / 255.0, expected[1] / 255.0,
			     expected[2] / 255.0, 1.0);
		glClear(GL_COLOR_BUFFER_BIT);
		glReadPixels(0, 0, SIZE, SIZE, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
		glFlush();
		if (i & 1) {
			glGetBufferSubData(GL_PIXEL_PACK_BUFFER, 0, 4, pixel);
		} else {
			const GLubyte *map = glMapBufferRange(GL_PIXEL_PACK_BUFFER,
							      0, 4, GL_MAP_READ_BIT);
			memcpy(pixel, map, 4);
			glUnmapBuffer(GL_PIXEL_PACK_BUFFER);
		}
		if (memcmp(pixel, expected, 4)) {
			if (failures++ < 5)
				printf("iteration %u: read %u %u %u %u, expected "
				       "%u %u %u %u\n", i, pixel[0], pixel[1],
				       pixel[2], pixel[3], expected[0], expected[1],
				       expected[2], expected[3]);
		}
	}

	if (!piglit_check_gl_error(GL_NO_ERROR))
		piglit_report_result(PIGLIT_FAIL);
	if (failures)
		printf("%u of %u readbacks returned stale data\n", failures,
		       iterations);
	piglit_report_result(failures ? PIGLIT_FAIL : PIGLIT_PASS);
}
