/*
 * SPDX-License-Identifier: MIT
 *
 * glTexSubImage2D with GL_UNPACK_ROW_LENGTH > width must not read past
 * the last texel of the source.
 *
 * The source ends right before a PROT_NONE page, so an over-read crashes.
 * Before each upload a draw samples the texture, so it is busy and a render
 * pass is open. Mesa's threaded context (zink on tilers) then uploads through
 * a staging buffer, and that path used to read the padding after the last
 * row. The result is checked with glGetTexImage.
 */

#include "piglit-util-gl.h"

#if defined(__unix__) || defined(__APPLE__)
#include <sys/mman.h>
#include <unistd.h>
#define HAVE_MPROTECT 1
#endif

PIGLIT_GL_TEST_CONFIG_BEGIN

	config.supports_gl_compat_version = 30;

	config.window_visual = PIGLIT_GL_VISUAL_RGBA | PIGLIT_GL_VISUAL_DOUBLE;
	config.khr_no_error_support = PIGLIT_NO_ERRORS;

PIGLIT_GL_TEST_CONFIG_END

#define SIZE 32
#define ROW_LENGTH 1024

static const char *vs_source =
	"#version 130\n"
	"in vec4 piglit_vertex;\n"
	"void main()\n"
	"{\n"
	"	gl_Position = piglit_vertex;\n"
	"}\n";

static const char *fs_source =
	"#version 130\n"
	"uniform sampler2D tex;\n"
	"void main()\n"
	"{\n"
	"	gl_FragColor = texture(tex, gl_FragCoord.xy / 32.0);\n"
	"}\n";

static GLuint fbo, tex, prog;
static GLubyte *data;

static GLubyte
pattern(int x, int y)
{
	return (GLubyte)(x * 7 + y * 13);
}

void
piglit_init(int argc, char **argv)
{
#ifdef HAVE_MPROTECT
	size_t stride = ROW_LENGTH * 4;
	size_t size = (SIZE - 1) * stride + SIZE * 4;
	size_t page = sysconf(_SC_PAGESIZE);
	size_t span = (size + page - 1) / page * page;
	GLubyte *map;
	GLuint rt;
	int x, y;

	map = mmap(NULL, span + page, PROT_READ | PROT_WRITE,
		   MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (map == MAP_FAILED ||
	    mprotect(map + span, page, PROT_NONE) != 0)
		piglit_report_result(PIGLIT_SKIP);

	data = map + span - size;
	for (y = 0; y < SIZE; y++)
		for (x = 0; x < SIZE * 4; x++)
			data[y * stride + x] = pattern(x, y);

	glGenTextures(1, &rt);
	glBindTexture(GL_TEXTURE_2D, rt);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, SIZE, SIZE, 0,
		     GL_RGBA, GL_UNSIGNED_BYTE, NULL);
	glGenFramebuffers(1, &fbo);
	glBindFramebuffer(GL_FRAMEBUFFER, fbo);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
			       GL_TEXTURE_2D, rt, 0);
	if (glCheckFramebufferStatus(GL_FRAMEBUFFER) !=
	    GL_FRAMEBUFFER_COMPLETE)
		piglit_report_result(PIGLIT_SKIP);

	glGenTextures(1, &tex);
	glBindTexture(GL_TEXTURE_2D, tex);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, SIZE, SIZE, 0,
		     GL_RGBA, GL_UNSIGNED_BYTE, NULL);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

	prog = piglit_build_simple_program(vs_source, fs_source);
#else
	piglit_report_result(PIGLIT_SKIP);
#endif
}

enum piglit_result
piglit_display(void)
{
	GLubyte out[SIZE * SIZE * 4];
	bool pass = true;
	int i, x, y;

	glBindFramebuffer(GL_FRAMEBUFFER, fbo);
	glViewport(0, 0, SIZE, SIZE);
	glUseProgram(prog);
	glBindTexture(GL_TEXTURE_2D, tex);
	glPixelStorei(GL_UNPACK_ROW_LENGTH, ROW_LENGTH);

	for (i = 0; i < 4; i++) {
		piglit_draw_rect(-1, -1, 2, 2);
		glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, SIZE, SIZE,
				GL_RGBA, GL_UNSIGNED_BYTE, data);
	}

	glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
	glPixelStorei(GL_PACK_ALIGNMENT, 1);
	glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, out);

	for (y = 0; y < SIZE && pass; y++) {
		for (x = 0; x < SIZE * 4; x++) {
			if (out[y * SIZE * 4 + x] != pattern(x, y)) {
				printf("Mismatch at byte %d of row %d: "
				       "expected %u, got %u\n", x, y,
				       pattern(x, y), out[y * SIZE * 4 + x]);
				pass = false;
				break;
			}
		}
	}

	pass = piglit_check_gl_error(GL_NO_ERROR) && pass;

	glBindFramebuffer(GL_FRAMEBUFFER, piglit_winsys_fbo);

	return pass ? PIGLIT_PASS : PIGLIT_FAIL;
}
