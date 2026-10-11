/*
 * SPDX-License-Identifier: MIT
 */

/** \file read-after-sampled-redraw.c
 * Reading a texture back must see rendering that is still queued for it,
 * also when an earlier queued draw sampled that texture.
 *
 * Per iteration, with framebuffers A, P, Q and B backed by RGBA8 textures:
 *   1. clear A to c0
 *   2. draw into P, sampling A
 *   3. clear A to c1 and draw c1 over all of A
 *   4. bind Q as the draw framebuffer and clear it
 *   5. read A: "blit" blits A to B and probes B; "readpixels" probes A
 *      directly.
 * Every probed pixel must be c1.  No feedback loop exists at any point and
 * GL commands take effect in the order they are issued.
 *
 * Mesa 25.2's llvmpipe returns A's contents from before step 3 (c0) unless
 * glFinish is called before step 5 (glFlush is not enough); softpipe, zink,
 * d3d12 and llvmpipe with LP_NUM_THREADS=0 return c1.  See
 * https://gitlab.freedesktop.org/mesa/mesa/-/work_items/16539
 */

#include "piglit-util-gl.h"

PIGLIT_GL_TEST_CONFIG_BEGIN

	config.supports_gl_core_version = 33;
	config.window_visual = PIGLIT_GL_VISUAL_RGBA;
	config.khr_no_error_support = PIGLIT_NO_ERRORS;

PIGLIT_GL_TEST_CONFIG_END

#define SIZE 64
#define ITERATIONS 4

static const char *vs_text =
	"#version 330 core\n"
	"void main() {\n"
	"	vec2 p = vec2(float((gl_VertexID << 1) & 2),\n"
	"		      float(gl_VertexID & 2));\n"
	"	gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);\n"
	"}\n";

static const char *solid_fs_text =
	"#version 330 core\n"
	"uniform vec4 color;\n"
	"out vec4 o;\n"
	"void main() { o = color; }\n";

static const char *sample_fs_text =
	"#version 330 core\n"
	"uniform sampler2D src;\n"
	"out vec4 o;\n"
	"void main() { o = texelFetch(src, ivec2(gl_FragCoord.xy), 0); }\n";

static GLuint
make_target(GLuint *tex)
{
	GLuint fbo;

	glGenTextures(1, tex);
	glBindTexture(GL_TEXTURE_2D, *tex);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, SIZE, SIZE, 0, GL_RGBA,
		     GL_UNSIGNED_BYTE, NULL);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glBindTexture(GL_TEXTURE_2D, 0);

	glGenFramebuffers(1, &fbo);
	glBindFramebuffer(GL_FRAMEBUFFER, fbo);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
			       GL_TEXTURE_2D, *tex, 0);
	if (glCheckFramebufferStatus(GL_FRAMEBUFFER) !=
	    GL_FRAMEBUFFER_COMPLETE) {
		printf("incomplete framebuffer\n");
		piglit_report_result(PIGLIT_FAIL);
	}
	return fbo;
}

static void
usage(const char *name)
{
	printf("usage: %s blit|readpixels\n", name);
	piglit_report_result(PIGLIT_FAIL);
}

void
piglit_init(int argc, char **argv)
{
	GLuint solid, sample, vao;
	GLuint a, p, q, b, a_tex, p_tex, q_tex, b_tex;
	GLint color;
	bool use_blit;
	bool pass = true;
	int i;

	if (argc != 2)
		usage(argv[0]);
	if (strcmp(argv[1], "blit") == 0)
		use_blit = true;
	else if (strcmp(argv[1], "readpixels") == 0)
		use_blit = false;
	else
		usage(argv[0]);

	solid = piglit_build_simple_program(vs_text, solid_fs_text);
	color = glGetUniformLocation(solid, "color");
	sample = piglit_build_simple_program(vs_text, sample_fs_text);
	glUseProgram(sample);
	glUniform1i(glGetUniformLocation(sample, "src"), 0);

	glGenVertexArrays(1, &vao);
	glBindVertexArray(vao);
	glViewport(0, 0, SIZE, SIZE);

	a = make_target(&a_tex);
	p = make_target(&p_tex);
	q = make_target(&q_tex);
	b = make_target(&b_tex);

	for (i = 0; i < ITERATIONS && pass; i++) {
		const float c0[4] = { 1.0, 0.0, 0.0, 1.0 };
		const float c1[4] = {
			0.0, (float) (i + 1) / ITERATIONS, 1.0, 1.0
		};

		/* 1. A = c0 */
		glBindFramebuffer(GL_FRAMEBUFFER, a);
		glClearColor(c0[0], c0[1], c0[2], c0[3]);
		glClear(GL_COLOR_BUFFER_BIT);

		/* 2. P = texelFetch(A) */
		glBindFramebuffer(GL_FRAMEBUFFER, p);
		glUseProgram(sample);
		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_2D, a_tex);
		glDrawArrays(GL_TRIANGLES, 0, 3);
		glBindTexture(GL_TEXTURE_2D, 0);

		/* 3. A = c1: a clear, then a draw */
		glBindFramebuffer(GL_FRAMEBUFFER, a);
		glClearColor(c1[0], c1[1], c1[2], c1[3]);
		glClear(GL_COLOR_BUFFER_BIT);
		glUseProgram(solid);
		glUniform4fv(color, 1, c1);
		glDrawArrays(GL_TRIANGLES, 0, 3);

		/* 4. A is no longer the draw framebuffer */
		glBindFramebuffer(GL_FRAMEBUFFER, q);
		glClearColor(0.0, 0.0, 0.0, 0.0);
		glClear(GL_COLOR_BUFFER_BIT);

		/* 5. read A */
		glBindFramebuffer(GL_READ_FRAMEBUFFER, a);
		if (use_blit) {
			glBindFramebuffer(GL_DRAW_FRAMEBUFFER, b);
			glBlitFramebuffer(0, 0, SIZE, SIZE, 0, 0, SIZE, SIZE,
					  GL_COLOR_BUFFER_BIT, GL_NEAREST);
			glBindFramebuffer(GL_READ_FRAMEBUFFER, b);
		}
		if (!piglit_probe_rect_rgba(0, 0, SIZE, SIZE, c1)) {
			printf("iteration %d: %s\n", i,
			       use_blit ? "blit of A" : "A");
			pass = false;
		}
	}

	pass = piglit_check_gl_error(GL_NO_ERROR) && pass;
	piglit_report_result(pass ? PIGLIT_PASS : PIGLIT_FAIL);
}

enum piglit_result
piglit_display(void)
{
	/* UNREACHED */
	return PIGLIT_FAIL;
}
