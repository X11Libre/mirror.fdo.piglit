/*
 * SPDX-License-Identifier: MIT
 */

/**
 * Tests the draw errors of OVR_multiview on OpenGL and OpenGL ES:
 *
 *    "* Any attachment of the draw framebuffer is multiview (section 9.2.8)
 *      and any of the following conditions are true:
 *
 *      - There is an active program for tessellation control, tessellation
 *      evaluation, or geometry stages, or
 *
 *      - Transform feedback is active and not paused."
 *
 *    "INVALID_OPERATION is generated if a rendering command is issued and the
 *    the number of views in the current draw framebuffer is not equal to the
 *    number of views declared in the currently bound program."
 *
 * Subtests:
 * - transform-feedback: drawing with transform feedback active is an error,
 *   and drawing with it paused is not.
 * - geometry-shader and tessellation: drawing with those stages is an error.
 * - num-views: a program declaring a different number of views, or none
 *   (which "is as if it had been set to 1"), is an error, and a matching one
 *   is not.
 */

#include "mv-common.h"

#define SIZE 16

static enum piglit_result transform_feedback(void *data);
static enum piglit_result geometry_shader(void *data);
static enum piglit_result tessellation(void *data);
static enum piglit_result num_views(void *data);

static const struct piglit_subtest subtests[] = {
	{ "transform feedback", "transform-feedback", transform_feedback },
	{ "geometry shader", "geometry-shader", geometry_shader },
	{ "tessellation", "tessellation", tessellation },
	{ "num_views mismatch", "num-views", num_views },
	{ NULL }
};

static const struct piglit_gl_test_config *piglit_config;

PIGLIT_GL_TEST_CONFIG_BEGIN

	/* for geometry and tessellation shaders, since some drivers create a
	 * context of exactly the requested version
	 */
	config.supports_gl_core_version = 40;
	config.supports_gl_es_version = 30;
	config.window_visual = PIGLIT_GL_VISUAL_RGBA;
	config.khr_no_error_support = PIGLIT_HAS_ERRORS;
	config.subtests = subtests;
	piglit_config = &config;

PIGLIT_GL_TEST_CONFIG_END

static const char *fs_body =
	"out vec4 frag_color;\n"
	"void main()\n"
	"{\n"
	"	frag_color = vec4(0.0, 1.0, 0.0, 1.0);\n"
	"}\n";

static GLuint fb, tex;

/* Binds a framebuffer with `views` views of a 4-layer texture. */
static bool
begin(int views)
{
	glGenFramebuffers(1, &fb);
	glBindFramebuffer(GL_FRAMEBUFFER, fb);
	tex = mv_array_texture(GL_RGBA8, SIZE, SIZE, 4);
	glFramebufferTextureMultiviewOVR(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, tex,
					 0, 0, views);
	glViewport(0, 0, SIZE, SIZE);
	return mv_check_status(GL_FRAMEBUFFER_COMPLETE);
}

static enum piglit_result
end(bool pass, GLuint prog)
{
	glUseProgram(0);
	glDeleteProgram(prog);
	glBindFramebuffer(GL_FRAMEBUFFER, piglit_winsys_fbo);
	glDeleteFramebuffers(1, &fb);
	glDeleteTextures(1, &tex);
	return pass ? PIGLIT_PASS : PIGLIT_FAIL;
}

/* A vertex shader body declaring `views` views, or none if 0. */
static char *
vs_body(int views, const char *extra_out, const char *extra_main)
{
	char *s;

	(void)!asprintf(&s,
			"%s"
			"in vec2 piglit_vertex;\n"
			"%s"
			"void main()\n"
			"{\n"
			"	gl_Position = vec4(piglit_vertex, 0.0, 1.0);\n"
			"%s"
			"}\n",
			views ? (views == 2 ? "layout(num_views = 2) in;\n"
					    : "layout(num_views = 3) in;\n") : "",
			extra_out, extra_main);
	return s;
}

static GLuint
build(const char *header, GLenum *stages, const char **sources, int count,
      const char *xfb_varying)
{
	GLuint prog = glCreateProgram();

	for (int i = 0; i < count; i++) {
		char *src;
		GLuint sh;

		/* only the vertex shader declares the views */
		(void)!asprintf(&src, "%s%s" MV_PRECISION "%s", header,
				stages[i] == GL_VERTEX_SHADER ?
				"#extension GL_OVR_multiview : require\n" : "",
				sources[i]);
		sh = piglit_compile_shader_text(stages[i], src);
		free(src);
		glAttachShader(prog, sh);
		glDeleteShader(sh);
	}
	glBindAttribLocation(prog, PIGLIT_ATTRIB_POS, "piglit_vertex");
	if (xfb_varying)
		glTransformFeedbackVaryings(prog, 1, &xfb_varying,
					    GL_INTERLEAVED_ATTRIBS);
	glLinkProgram(prog);
	/* the spec makes the extra stages a draw-time error, so the program
	 * has to link
	 */
	if (!piglit_link_check_status(prog)) {
		glDeleteProgram(prog);
		return 0;
	}
	return prog;
}

/* Draws one triangle with GL_TRIANGLES, which matches the transform feedback
 * primitive mode as OpenGL ES requires, from its own vertex array object.
 */
static void
draw_triangle(void)
{
	static const float verts[] = { -1, -1, 1, -1, -1, 1 };
	GLuint vao, vbo;

	glGenVertexArrays(1, &vao);
	glBindVertexArray(vao);
	glGenBuffers(1, &vbo);
	glBindBuffer(GL_ARRAY_BUFFER, vbo);
	glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_STATIC_DRAW);
	glVertexAttribPointer(PIGLIT_ATTRIB_POS, 2, GL_FLOAT, GL_FALSE, 0, NULL);
	glEnableVertexAttribArray(PIGLIT_ATTRIB_POS);
	glDrawArrays(GL_TRIANGLES, 0, 3);
	glBindVertexArray(0);
	glBindBuffer(GL_ARRAY_BUFFER, 0);
	glDeleteVertexArrays(1, &vao);
	glDeleteBuffers(1, &vbo);
}

static enum piglit_result
transform_feedback(void *data)
{
	char *header;
	char *vs = vs_body(2, "out float xfb_out;\n", "	xfb_out = 1.0;\n");
	const char *sources[] = { vs, fs_body };
	GLenum stages[] = { GL_VERTEX_SHADER, GL_FRAGMENT_SHADER };
	GLuint prog, buf, xfb;
	bool pass;

	if (!piglit_is_gles() && piglit_get_gl_version() < 40 &&
	    !piglit_is_extension_supported("GL_ARB_transform_feedback2")) {
		free(vs);
		return PIGLIT_SKIP;
	}

	header = strdup(mv_glsl_version());
	prog = build(header, stages, sources, 2, "xfb_out");
	free(header);
	free(vs);
	if (!prog)
		return PIGLIT_FAIL;

	pass = begin(2);
	glUseProgram(prog);
	glGenTransformFeedbacks(1, &xfb);
	glBindTransformFeedback(GL_TRANSFORM_FEEDBACK, xfb);
	glGenBuffers(1, &buf);
	glBindBufferBase(GL_TRANSFORM_FEEDBACK_BUFFER, 0, buf);
	glBufferData(GL_TRANSFORM_FEEDBACK_BUFFER, 1024, NULL, GL_STREAM_READ);

	glBeginTransformFeedback(GL_TRIANGLES);
	draw_triangle();
	pass &= piglit_check_gl_error(GL_INVALID_OPERATION);

	glPauseTransformFeedback();
	draw_triangle();
	pass &= piglit_check_gl_error(GL_NO_ERROR);

	glResumeTransformFeedback();
	glEndTransformFeedback();
	glBindTransformFeedback(GL_TRANSFORM_FEEDBACK, 0);
	glDeleteTransformFeedbacks(1, &xfb);
	glDeleteBuffers(1, &buf);
	pass &= piglit_check_gl_error(GL_NO_ERROR);
	return end(pass, prog);
}

/* The GLSL version for geometry and tessellation shaders, or NULL if they
 * are not supported.
 */
static const char *
stage_header(int gl_version, int es_version)
{
	if (piglit_is_gles())
		return piglit_get_gl_version() >= es_version ?
		       "#version 320 es\n" : NULL;
	if (piglit_get_gl_version() >= gl_version)
		return gl_version >= 40 ? "#version 400\n" : "#version 330\n";
	return NULL;
}

static enum piglit_result
geometry_shader(void *data)
{
	static const char *gs =
		"layout(triangles) in;\n"
		"layout(triangle_strip, max_vertices = 3) out;\n"
		"void main()\n"
		"{\n"
		"	for (int i = 0; i < 3; i++) {\n"
		"		gl_Position = gl_in[i].gl_Position;\n"
		"		EmitVertex();\n"
		"	}\n"
		"}\n";
	const char *version = stage_header(33, 32);
	char *header, *vs;
	const char *sources[3];
	GLenum stages[] = { GL_VERTEX_SHADER, GL_GEOMETRY_SHADER, GL_FRAGMENT_SHADER };
	GLuint prog;
	bool pass;

	if (!version)
		return PIGLIT_SKIP;

	header = strdup(version);
	vs = vs_body(2, "", "");
	sources[0] = vs;
	sources[1] = gs;
	sources[2] = fs_body;
	prog = build(header, stages, sources, 3, NULL);
	free(header);
	free(vs);
	if (!prog)
		return PIGLIT_FAIL;

	pass = begin(2);
	glUseProgram(prog);
	piglit_draw_rect(-1, -1, 2, 2);
	pass &= piglit_check_gl_error(GL_INVALID_OPERATION);
	return end(pass, prog);
}

static enum piglit_result
tessellation(void *data)
{
	static const char *tcs =
		"layout(vertices = 4) out;\n"
		"void main()\n"
		"{\n"
		"	gl_out[gl_InvocationID].gl_Position = gl_in[gl_InvocationID].gl_Position;\n"
		"	gl_TessLevelInner[0] = 1.0;\n"
		"	gl_TessLevelInner[1] = 1.0;\n"
		"	for (int i = 0; i < 4; i++)\n"
		"		gl_TessLevelOuter[i] = 1.0;\n"
		"}\n";
	static const char *tes =
		"layout(quads) in;\n"
		"void main()\n"
		"{\n"
		"	vec4 a = mix(gl_in[0].gl_Position, gl_in[1].gl_Position, gl_TessCoord.x);\n"
		"	vec4 b = mix(gl_in[2].gl_Position, gl_in[3].gl_Position, gl_TessCoord.x);\n"
		"	gl_Position = mix(a, b, gl_TessCoord.y);\n"
		"}\n";
	const char *version = stage_header(40, 32);
	char *header, *vs;
	const char *sources[4];
	GLenum stages[] = {
		GL_VERTEX_SHADER, GL_TESS_CONTROL_SHADER, GL_TESS_EVALUATION_SHADER,
		GL_FRAGMENT_SHADER,
	};
	GLuint prog;
	bool pass;

	if (!version)
		return PIGLIT_SKIP;

	header = strdup(version);
	vs = vs_body(2, "", "");
	sources[0] = vs;
	sources[1] = tcs;
	sources[2] = tes;
	sources[3] = fs_body;
	prog = build(header, stages, sources, 4, NULL);
	free(header);
	free(vs);
	if (!prog)
		return PIGLIT_FAIL;

	pass = begin(2);
	glUseProgram(prog);
	piglit_draw_rect_custom(-1, -1, 2, 2, true, 1);
	pass &= piglit_check_gl_error(GL_INVALID_OPERATION);
	return end(pass, prog);
}

static enum piglit_result
num_views(void *data)
{
	/* program views, framebuffer views, expected error */
	static const struct { int program, framebuffer; GLenum error; } cases[] = {
		{ 2, 2, GL_NO_ERROR },
		{ 2, 3, GL_INVALID_OPERATION },
		{ 3, 2, GL_INVALID_OPERATION },
		{ 0, 2, GL_INVALID_OPERATION },
	};
	bool pass = true;

	for (unsigned i = 0; i < ARRAY_SIZE(cases); i++) {
		char *vs;
		GLuint prog;
		bool ok;

		/* A program declaring more views than MAX_VIEWS_OVR does not
		 * compile.
		 */
		if (MAX2(cases[i].program, cases[i].framebuffer) > mv_max_views())
			continue;
		vs = vs_body(cases[i].program, "", "");
		prog = mv_program("#extension GL_OVR_multiview : require\n",
				  vs, fs_body);
		free(vs);
		ok = begin(cases[i].framebuffer);
		glUseProgram(prog);
		piglit_draw_rect(-1, -1, 2, 2);
		ok &= piglit_check_gl_error(cases[i].error);
		if (!ok)
			printf("program with %d views, framebuffer with %d\n",
			       cases[i].program, cases[i].framebuffer);
		pass &= end(ok, prog) == PIGLIT_PASS;
	}
	return pass ? PIGLIT_PASS : PIGLIT_FAIL;
}

enum piglit_result
piglit_display(void)
{
	return mv_run_subtests(subtests);
}

void
piglit_init(int argc, char **argv)
{
	piglit_require_extension("GL_OVR_multiview");
}
