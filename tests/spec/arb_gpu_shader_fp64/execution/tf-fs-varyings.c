/*
 * Copyright © 2026 Matt Turner
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

/**
 * \file tf-fs-varyings.c
 *
 * Capture 64-bit varyings with transform feedback and read them in the
 * fragment shader in the same draw.
 *
 * The other fp64 transform feedback tests discard rasterization, so they
 * pass even if a driver that splits 64-bit varyings into 32-bit halves
 * orders the halves differently for the two consumers.
 */

#include <inttypes.h>

#include "piglit-util-gl.h"

PIGLIT_GL_TEST_CONFIG_BEGIN

	config.supports_gl_core_version = 32;
	config.window_visual = PIGLIT_GL_VISUAL_DOUBLE | PIGLIT_GL_VISUAL_RGBA;
	config.khr_no_error_support = PIGLIT_NO_ERRORS;

PIGLIT_GL_TEST_CONFIG_END

/* The 32-bit halves of each value are nonzero and differ. */
static const struct values {
	double d1, d3[3], da[2][3], xfb_only[2], fs_only[4];
} values = {
	1.1,
	{ 2.1, 2.2, 2.3 },
	{ { 3.1, 3.2, 3.3 }, { 3.4, 3.6, 3.7 } },
	{ 4.1, 4.2 },
	{ 5.1, 5.2, 5.3, 5.4 },
};

/* The captured varyings are the leading members of struct values. */
static const char *const xfb_varyings[] = { "d1", "d3", "da", "xfb_only" };

#define XFB_DOUBLES (offsetof(struct values, fs_only) / sizeof(double))
#define XFB_VERTICES 6
#define ATTR_LOCATION 1

static const char *const header =
	"#version 150\n"
	"#extension GL_ARB_gpu_shader_fp64 : require\n"
	"#extension GL_ARB_vertex_attrib_64bit : require\n"
	"#extension GL_ARB_separate_shader_objects : require\n"
	"uniform double u_d1;\n"
	"uniform dvec3 u_d3;\n"
	"uniform dvec3 u_da[2];\n"
	"uniform dvec2 u_xfb_only;\n"
	"uniform dvec4 u_fs_only;\n";

static const char *const vs_text =
	"in vec4 piglit_vertex;\n"
	"in dvec3 attr;\n"
	"out gl_PerVertex { vec4 gl_Position; };\n"
	"LOC(0) flat out double d1;\n"
	"LOC(1) flat out float f1;\n"
	"LOC(2) flat out dvec3 d3;\n"
	"LOC(4) flat out dvec3 da[2];\n"
	"LOC(8) flat out dvec2 xfb_only;\n"
	"LOC(9) flat out dvec4 fs_only;\n"
	"void main()\n"
	"{\n"
	"	gl_Position = piglit_vertex;\n"
	"	d1 = u_d1;\n"
	"	f1 = 0.25;\n"
	"	d3 = attr;\n"
	"	da = u_da;\n"
	"	xfb_only = u_xfb_only;\n"
	"	fs_only = u_fs_only;\n"
	"}\n";

static const char *const fs_text =
	"LOC(0) flat in double d1;\n"
	"LOC(1) flat in float f1;\n"
	"LOC(2) flat in dvec3 d3;\n"
	"LOC(4) flat in dvec3 da[2];\n"
	"LOC(9) flat in dvec4 fs_only;\n"
	"out vec4 color;\n"
	"void main()\n"
	"{\n"
	"	bool pass = d1 == u_d1 && f1 == 0.25 && d3 == u_d3 &&\n"
	"		    da == u_da && fs_only == u_fs_only;\n"
	"	color = pass ? vec4(0.0, 1.0, 0.0, 1.0) : vec4(1.0, 0.0, 0.0, 1.0);\n"
	"}\n";

static GLuint
compile(GLenum target, bool location, const char *body)
{
	GLuint shader;
	char *text;

	(void)!asprintf(&text, "%s#define LOC(n) %s\n%s", header,
			location ? "layout(location = n)" : "", body);
	shader = piglit_compile_shader_text(target, text);
	free(text);

	return shader;
}

/** Link a program, which is separable if it lacks one of the stages. */
static GLuint
link_program(GLuint vs, GLuint fs)
{
	GLuint prog = glCreateProgram();

	if (vs) {
		glAttachShader(prog, vs);
		glBindAttribLocation(prog, PIGLIT_ATTRIB_POS, "piglit_vertex");
		glBindAttribLocation(prog, ATTR_LOCATION, "attr");
		glTransformFeedbackVaryings(prog, ARRAY_SIZE(xfb_varyings),
					    xfb_varyings,
					    GL_INTERLEAVED_ATTRIBS);
	}
	if (fs)
		glAttachShader(prog, fs);

	glProgramParameteri(prog, GL_PROGRAM_SEPARABLE, !vs || !fs);
	glLinkProgram(prog);
	if (!piglit_link_check_status(prog))
		piglit_report_result(PIGLIT_FAIL);

	glProgramUniform1dv(prog, glGetUniformLocation(prog, "u_d1"), 1,
			    &values.d1);
	glProgramUniform3dv(prog, glGetUniformLocation(prog, "u_d3"), 1,
			    values.d3);
	glProgramUniform3dv(prog, glGetUniformLocation(prog, "u_da"), 2,
			    values.da[0]);
	glProgramUniform2dv(prog, glGetUniformLocation(prog, "u_xfb_only"), 1,
			    values.xfb_only);
	glProgramUniform4dv(prog, glGetUniformLocation(prog, "u_fs_only"), 1,
			    values.fs_only);

	return prog;
}

static bool
probe_xfb(void)
{
	uint64_t expected[XFB_DOUBLES];
	const uint64_t *ptr;
	bool pass = true;

	memcpy(expected, &values, sizeof(expected));

	ptr = glMapBuffer(GL_TRANSFORM_FEEDBACK_BUFFER, GL_READ_ONLY);
	for (unsigned v = 0; v < XFB_VERTICES && pass; v++) {
		for (unsigned i = 0; i < XFB_DOUBLES; i++, ptr++) {
			if (*ptr == expected[i])
				continue;

			printf("Transform feedback vertex %u, double %u: "
			       "expected 0x%016" PRIx64 ", got 0x%016" PRIx64
			       "\n", v, i, expected[i], *ptr);
			pass = false;
		}
	}
	glUnmapBuffer(GL_TRANSFORM_FEEDBACK_BUFFER);

	return pass;
}

static bool
run(bool separable, bool location)
{
	static const float green[] = { 0.0, 1.0, 0.0, 1.0 };
	GLuint vs = compile(GL_VERTEX_SHADER, location, vs_text);
	GLuint fs = compile(GL_FRAGMENT_SHADER, location, fs_text);
	GLuint vs_prog, fs_prog = 0, pipeline = 0, buf;
	bool pass;

	if (separable) {
		vs_prog = link_program(vs, 0);
		fs_prog = link_program(0, fs);

		glGenProgramPipelines(1, &pipeline);
		glUseProgramStages(pipeline, GL_VERTEX_SHADER_BIT, vs_prog);
		glUseProgramStages(pipeline, GL_FRAGMENT_SHADER_BIT, fs_prog);
		glBindProgramPipeline(pipeline);
	} else {
		vs_prog = link_program(vs, fs);
		glUseProgram(vs_prog);
	}

	glGenBuffers(1, &buf);
	glBindBufferBase(GL_TRANSFORM_FEEDBACK_BUFFER, 0, buf);
	glBufferData(GL_TRANSFORM_FEEDBACK_BUFFER,
		     XFB_VERTICES * XFB_DOUBLES * sizeof(double), NULL,
		     GL_STREAM_READ);

	glClear(GL_COLOR_BUFFER_BIT);
	glVertexAttribL3dv(ATTR_LOCATION, values.d3);
	glBeginTransformFeedback(GL_TRIANGLES);
	piglit_draw_rect(-1, -1, 2, 2);
	glEndTransformFeedback();

	pass = piglit_check_gl_error(GL_NO_ERROR);
	pass = probe_xfb() && pass;
	pass = piglit_probe_rect_rgba(0, 0, piglit_width, piglit_height,
				      green) && pass;

	glUseProgram(0);
	glDeleteProgramPipelines(1, &pipeline);
	glDeleteProgram(vs_prog);
	glDeleteProgram(fs_prog);
	glDeleteShader(vs);
	glDeleteShader(fs);
	glDeleteBuffers(1, &buf);

	return pass;
}

void
piglit_init(int argc, char **argv)
{
	piglit_require_transform_feedback();
	piglit_require_extension("GL_ARB_gpu_shader_fp64");
	piglit_require_extension("GL_ARB_vertex_attrib_64bit");
	piglit_require_extension("GL_ARB_separate_shader_objects");
}

enum piglit_result
piglit_display(void)
{
	enum piglit_result result = PIGLIT_PASS;

	for (unsigned i = 0; i < 4; i++) {
		const bool separable = i & 2, location = i & 1;
		const bool pass = run(separable, location);

		piglit_report_subtest_result(pass ? PIGLIT_PASS : PIGLIT_FAIL,
					     "%s%s",
					     separable ? "sso" : "linked",
					     location ? "-location" : "");
		if (!pass)
			result = PIGLIT_FAIL;
	}

	piglit_present_results();

	return result;
}
