/* SPDX-License-Identifier: MIT */

/** @file gen-buffers-unique.c
 *
 * Generate more than 2^22 buffer names and check that no name is returned
 * twice.  Mesa hands out object names from segments of 2^22 names, and a full
 * segment must not hand out the first name of the next one.
 */

#include "piglit-util-gl.h"

PIGLIT_GL_TEST_CONFIG_BEGIN
	config.supports_gl_compat_version = 15;
	config.supports_gl_core_version = 31;
	config.window_visual = PIGLIT_GL_VISUAL_RGBA;
PIGLIT_GL_TEST_CONFIG_END

#define NUM_NAMES ((1u << 22) + 4096)

static int
compare_names(const void *a, const void *b)
{
	const GLuint x = *(const GLuint *)a;
	const GLuint y = *(const GLuint *)b;

	return (x > y) - (x < y);
}

enum piglit_result
piglit_display(void)
{
	return PIGLIT_FAIL;
}

void
piglit_init(int argc, char **argv)
{
	GLuint *names = malloc(NUM_NAMES * sizeof(*names));
	bool pass;
	GLuint i;

	if (!names) {
		printf("out of memory\n");
		piglit_report_result(PIGLIT_SKIP);
	}

	for (i = 0; i < NUM_NAMES; i += 4096)
		glGenBuffers(4096, &names[i]);

	pass = piglit_check_gl_error(GL_NO_ERROR);

	qsort(names, NUM_NAMES, sizeof(*names), compare_names);

	for (i = 0; i < NUM_NAMES; i++) {
		if (names[i] == 0 || (i && names[i] == names[i - 1])) {
			printf("name %u was returned %s\n", names[i],
			       names[i] ? "twice" : "as zero");
			pass = false;
			break;
		}
	}

	glDeleteBuffers(NUM_NAMES, names);
	free(names);

	piglit_report_result(pass ? PIGLIT_PASS : PIGLIT_FAIL);
}
