/* SPDX-License-Identifier: MIT */

/**
 * \file wait-common.c
 *
 * Helpers shared by the tests which check that a wait does not return
 * before the GPU has finished the commands queued ahead of it.
 *
 * The workload is a draw covering a 512x512 framebuffer with a loop in the
 * fragment shader, calibrated so that one draw takes a chosen number of
 * milliseconds. A GL_TIMESTAMP query queued behind the draws gives the
 * time at which the GPU finished them. wait_check_completed() fails if
 * that time is later than GL_TIMESTAMP as read right after the wait
 * returned.
 */

#include "wait-common.h"

#ifdef _MSC_VER
#define THREAD_LOCAL __declspec(thread)
#else
#define THREAD_LOCAL __thread
#endif

#define WORKLOAD_SIZE 512

static THREAD_LOCAL GLuint workload_prog;
static THREAD_LOCAL GLuint workload_tex;
static THREAD_LOCAL GLint workload_iters_loc;
static THREAD_LOCAL GLint workload_seed_loc;
static THREAD_LOCAL GLint workload_color_loc;
static int workload_iters = 64;
static THREAD_LOCAL unsigned workload_seed;

static const char *workload_vs =
	"#version 130\n"
	"void main()\n"
	"{\n"
	"	gl_Position = gl_Vertex;\n"
	"}\n";

static const char *workload_fs =
	"#version 130\n"
	"uniform int iters;\n"
	"uniform float seed;\n"
	"uniform vec4 color;\n"
	"void main()\n"
	"{\n"
	"	float a = seed + gl_FragCoord.x * 0.001;\n"
	"	for (int i = 0; i < iters; i++)\n"
	"		a = fract(sin(a * 12.9898 + float(i)) * 43758.5453);\n"
	"	gl_FragColor = color + vec4(a * 0.0);\n"
	"}\n";

double
wait_cpu_time_ms(void)
{
	return piglit_time_get_nano() / 1e6;
}

void
wait_workload_init(void)
{
	GLuint fbo;

	piglit_require_gl_version(30);
	piglit_require_extension("GL_ARB_sync");
	piglit_require_extension("GL_ARB_timer_query");

	workload_prog = piglit_build_simple_program(workload_vs, workload_fs);
	glUseProgram(workload_prog);
	workload_iters_loc = glGetUniformLocation(workload_prog, "iters");
	workload_seed_loc = glGetUniformLocation(workload_prog, "seed");
	workload_color_loc = glGetUniformLocation(workload_prog, "color");
	glUniform4f(workload_color_loc, 0, 1, 0, 1);

	glGenTextures(1, &workload_tex);
	glBindTexture(GL_TEXTURE_2D, workload_tex);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, WORKLOAD_SIZE, WORKLOAD_SIZE, 0,
		     GL_RGBA, GL_UNSIGNED_BYTE, NULL);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glGenFramebuffers(1, &fbo);
	glBindFramebuffer(GL_FRAMEBUFFER, fbo);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
			       GL_TEXTURE_2D, workload_tex, 0);
	if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
		piglit_report_result(PIGLIT_SKIP);
	glViewport(0, 0, WORKLOAD_SIZE, WORKLOAD_SIZE);
	glUniform1i(workload_iters_loc, workload_iters);
}

static double
measure_draw_ms(void)
{
	GLubyte px[4];
	double start = wait_cpu_time_ms();

	wait_workload_draw(1, false);
	glReadPixels(0, 0, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
	return wait_cpu_time_ms() - start;
}

double
wait_workload_calibrate(double target_ms)
{
	double ms = 0;

	for (unsigned i = 0; i < 4; i++)
		measure_draw_ms();

	for (unsigned attempt = 0; attempt < 12; attempt++) {
		double best = measure_draw_ms();
		for (unsigned i = 0; i < 2; i++) {
			double again = measure_draw_ms();
			if (again < best)
				best = again;
		}
		ms = best;
		if (ms > target_ms * 0.7 && ms < target_ms * 1.5)
			break;
		double scale = target_ms / (ms > 0.05 ? ms : 0.05);
		if (scale > 8.0)
			scale = 8.0;
		int next = workload_iters * scale;
		if (next < 1)
			next = 1;
		if (next > 4000000)
			next = 4000000;
		if (next == workload_iters)
			break;
		workload_iters = next;
		glUniform1i(workload_iters_loc, workload_iters);
	}
	return ms;
}

void
wait_workload_draw(unsigned draws, bool flush_each)
{
	for (unsigned i = 0; i < draws; i++) {
		glUniform1f(workload_seed_loc, (workload_seed++ % 1024) * 0.37f);
		piglit_draw_rect(-1, -1, 2, 2);
		if (flush_each)
			glFlush();
	}
}

void
wait_workload_set_color(const float *color)
{
	glUniform4fv(workload_color_loc, 1, color);
}

GLuint
wait_workload_texture(void)
{
	return workload_tex;
}

GLint64
wait_timestamp_now(void)
{
	GLint64 t = 0;

	glGetInteger64v(GL_TIMESTAMP, &t);
	return t;
}

GLuint
wait_timestamp_queue(void)
{
	GLuint q;

	glGenQueries(1, &q);
	glQueryCounter(q, GL_TIMESTAMP);
	return q;
}

GLint64
wait_timestamp_result(GLuint query)
{
	GLint64 t = 0;

	glGetQueryObjecti64v(query, GL_QUERY_RESULT, &t);
	glDeleteQueries(1, &query);
	return t;
}

bool
wait_check_completed(GLuint query, const char *what, unsigned iteration)
{
	GLint64 after_wait = wait_timestamp_now();
	GLint64 completed = wait_timestamp_result(query);

	if (completed > after_wait) {
		printf("%s, iteration %u: returned %.3f ms before the GPU "
		       "completed the preceding commands\n", what, iteration,
		       (completed - after_wait) / 1e6);
		return false;
	}
	return true;
}

void
wait_verify_workload(const char *what)
{
	static const float green[] = {0, 1, 0, 1};

	if (!piglit_probe_rect_rgba(0, 0, WORKLOAD_SIZE, WORKLOAD_SIZE, green)) {
		printf("%s: workload framebuffer has wrong contents\n", what);
		piglit_report_result(PIGLIT_FAIL);
	}
}
