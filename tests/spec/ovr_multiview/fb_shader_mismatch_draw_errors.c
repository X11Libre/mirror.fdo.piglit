/*
 * Copyright (C) 2025 James Hogan <james@albanarts.com>
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
 *
 */

/**
 * Test that draws produce errors when the number of views declared in the
 * shader program doesn't match the number of views in the current draw
 * framebuffer.
 */

#include "piglit-util-gl.h"

/*
 * nvidia has GL_MAX_VIEWS_OVR=32, so lets have enough width in texture to
 * offset the triangle up to 31 pixels.
 */
#define TEX_WIDTH 32
#define TEX_HEIGHT 4

#define _STRINGIFY(X) #X
#define STRINGIFY(X) _STRINGIFY(X)

PIGLIT_GL_TEST_CONFIG_BEGIN

	config.supports_gl_compat_version = 30;
	config.khr_no_error_support = PIGLIT_HAS_ERRORS;

PIGLIT_GL_TEST_CONFIG_END

#define SHADER_VERSION_STR "#version 130\n"

static const char *vertex_shader_multiview =
	SHADER_VERSION_STR
	"#extension GL_OVR_multiview: enable\n"
	"layout (num_views = %u) in;\n" /* num_views */
	"in vec3 inPos;\n"
	"void main()\n"
	"{\n"
	"  gl_Position = vec4(inPos, 1.0);\n"
	"}\n";

static const char *vertex_shader_normal =
	SHADER_VERSION_STR
	"in vec3 inPos;\n"
	"void main()\n"
	"{\n"
	"  gl_Position = vec4(inPos, 1.0);\n"
	"}\n";

static const char *fragment_shader =
	SHADER_VERSION_STR
	"void main()\n"
	"{\n"
	"  gl_FragColor = vec4(1.0, 1.0, 1.0, 1.0);\n"
	"}\n";

/* Set up the scene rendering shader */
static GLuint
build_scene_program(unsigned int num_views)
{
	const GLchar *vert_src[1], *frag_src[1];
	GLint success;
	GLuint shader_vert, shader_frag, prog;
	GLchar info_log[512];

	GLchar vert_src_buf[1024];
	if (num_views > 0) {
		/* Inject num_views into multiview shader */
		snprintf(vert_src_buf, sizeof(vert_src_buf),
			 vertex_shader_multiview, num_views);
		vert_src[0] = vert_src_buf;
	} else {
		vert_src[0] = vertex_shader_normal;
	}
	frag_src[0] = fragment_shader;

	/* Vertex shader */
	shader_vert = glCreateShader(GL_VERTEX_SHADER);
	glShaderSource(shader_vert, 1, vert_src, NULL);
	glCompileShader(shader_vert);
	glGetShaderiv(shader_vert, GL_COMPILE_STATUS, &success);
	if (!success) {
		glGetShaderInfoLog(shader_vert, sizeof(info_log), NULL,
				   info_log);
		printf("multiview vertex shader compilation (%u views) failed: %s\n",
		       num_views, info_log);
		piglit_report_result(PIGLIT_FAIL);
	}

	/* Fragment shader */
	shader_frag = glCreateShader(GL_FRAGMENT_SHADER);
	glShaderSource(shader_frag, 1, frag_src, NULL);
	glCompileShader(shader_frag);
	glGetShaderiv(shader_frag, GL_COMPILE_STATUS, &success);
	if (!success) {
		glGetShaderInfoLog(shader_frag, sizeof(info_log), NULL,
				   info_log);
		printf("multiview fragment shader compilation (%u views) failed: %s\n",
		       num_views, info_log);
		piglit_report_result(PIGLIT_FAIL);
	}

	/* Shader program */
	prog = glCreateProgram();
	glBindAttribLocation(prog, 0, "inPos");
	glAttachShader(prog, shader_vert);
	glAttachShader(prog, shader_frag);
	glLinkProgram(prog);
	glGetProgramiv(prog, GL_LINK_STATUS, &success);
	if (!success) {
		glGetProgramInfoLog(prog, sizeof(info_log), NULL, info_log);
		printf("multiview shader link (%u views) failed: %s\n",
		       num_views, info_log);
		piglit_report_result(PIGLIT_FAIL);
	}

	glDeleteShader(shader_vert);
	glDeleteShader(shader_frag);

	return prog;
}

/* Render a triangle */
static void
render_triangle()
{
	float verts[6][3] = {
		/* x      y     z */
		{-1.0f, -1.0f, 1.0f},
		{ 0.5f, -1.0f, 0.0f},
		{-1.0f,  1.0f, 1.0f},
	};
	glEnableVertexAttribArray(0);
	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, &verts[0][0]);
	glDrawArrays(GL_TRIANGLES, 0, 3);
	glDisableVertexAttribArray(0);
}

static void
usage(const char *arg0, enum piglit_result result)
{
	printf("usage: %s <fb-views> <shader-views>\n",
	       arg0);
	piglit_report_result(result);
}

void
piglit_init(int argc, char **argv)
{
	GLint max_views = 2;
	GLuint tex, fbo;
	GLuint prog;
	GLenum fbstatus, expect;
	bool ok = true;
	int fb_views, shader_views;

	piglit_require_extension("GL_OVR_multiview");

	/* get limits */
	glGetIntegerv(GL_MAX_VIEWS_OVR, &max_views);
	printf("GL_MAX_VIEWS_OVR = %d\n", max_views);
	if (!piglit_check_gl_error(GL_NO_ERROR) || max_views < 2)
		max_views = 2;

	if (argc != 3)
		usage(argv[0], PIGLIT_FAIL);

	fb_views = atoi(argv[1]);
	if (fb_views < 0) {
		printf("fb_views (%u) must be >= 0\n", fb_views);
		usage(argv[0], PIGLIT_FAIL);
	}
	if (fb_views > max_views) {
		printf("fb_views (%u) must be <= GL_MAX_VIEWS_OVR (%u)\n",
		       fb_views, max_views);
		usage(argv[0], PIGLIT_SKIP);
	}

	shader_views = atoi(argv[2]);
	if (shader_views < 0) {
		printf("shader_views (%u) must be >= 0\n", shader_views);
		usage(argv[0], PIGLIT_FAIL);
	}
	if (shader_views > max_views) {
		printf("shader_views (%u) must be <= GL_MAX_VIEWS_OVR (%u)\n",
		       shader_views, max_views);
		usage(argv[0], PIGLIT_SKIP);
	}

	/* does a non multiview FB/shader count as having 1 view or none? */
	if (fb_views != shader_views && fb_views <= 1 && shader_views <= 1) {
		printf("fb_views (%u) <= 1 and shader_views (%u) <= 1, OVR_multiview spec is unclear\n",
		       fb_views, shader_views);
		usage(argv[0], PIGLIT_SKIP);
	}

	printf("fb_views = %u (%s)\n", fb_views,
	       fb_views > 0 ? "multiview" : "normal");
	printf("shader_views = %u (%s)\n", shader_views,
	       shader_views > 0 ? "multiview" : "normal");

	/* build shaders */
	prog = build_scene_program(shader_views);
	if (!piglit_check_gl_error(GL_NO_ERROR))
		piglit_report_result(PIGLIT_FAIL);

	/* generate 2d array texture */
	glGenTextures(1, &tex);
	if (fb_views > 0) {
		glBindTexture(GL_TEXTURE_2D_ARRAY, tex);
		glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, GL_RGB8, TEX_WIDTH,
			     TEX_HEIGHT, fb_views, 0, GL_RGB, GL_UNSIGNED_BYTE,
			     NULL);
		glBindTexture(GL_TEXTURE_2D_ARRAY, 0);
	} else {
		glBindTexture(GL_TEXTURE_2D, tex);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB8, TEX_WIDTH, TEX_HEIGHT,
			     0, GL_RGB, GL_UNSIGNED_BYTE, NULL);
		glBindTexture(GL_TEXTURE_2D, 0);
	}

	/* generate FBO */
	glGenFramebuffers(1, &fbo);
	glBindFramebuffer(GL_FRAMEBUFFER, fbo);
	if (fb_views > 0) {
		glFramebufferTextureMultiviewOVR(GL_FRAMEBUFFER,
						 GL_COLOR_ATTACHMENT0, tex, 0,
						 0, fb_views);
	} else {
		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
				       GL_TEXTURE_2D, tex, 0);
	}
	fbstatus = glCheckFramebufferStatus(GL_FRAMEBUFFER);
	if (fbstatus != GL_FRAMEBUFFER_COMPLETE) {
		printf("Framebuffer not complete: %s\n",
		       piglit_get_gl_enum_name(fbstatus));
		piglit_report_result(PIGLIT_FAIL);
	}

	glViewport(0, 0, TEX_WIDTH, TEX_HEIGHT);
	glUseProgram(prog);

	/*
	 * OVR_multiview:
	 * "INVALID_OPERATION is generated if a rendering command is issued and
	 * the the number of views in the current draw framebuffer is not equal
	 * to the number of views declared in the currently bound program."
	 */

	/* try clear & draw normal triangle */
	glClear(GL_COLOR_BUFFER_BIT);
	render_triangle();

	/* ensure we get any errors */
	glFinish();

	/* check for error on fb/shader view mismatch */
	expect = GL_INVALID_OPERATION;
	if (fb_views == shader_views)
		expect = GL_NO_ERROR;
	/*
	 * Note, ambiguity around whether a non multiview FB/shader counts as
	 * having 1 view or none is avoided by skipping the whole test above
	 */
	ok &= piglit_check_gl_error(expect);

	piglit_report_result(ok ? PIGLIT_PASS : PIGLIT_FAIL);
}

enum piglit_result
piglit_display(void)
{
	/* Should never be reached */
	return PIGLIT_FAIL;
}
