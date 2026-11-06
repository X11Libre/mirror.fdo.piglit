/*
 * Copyright © 2026 Timothy Arceri
 *
 * SPDX-License-Identifier: MIT
 */

/**
 * \file render-to-texture-with-xfb-varyings.c
 *
 * Clear a framebuffer, render into a second framebuffer with a program that
 * declares transform feedback varyings (transform feedback itself is never
 * active), then composite the second framebuffer into the first one with the
 * same program and check the result: green rectangles over a grey
 * background.  The sequence is repeated a few times with new textures, like
 * an application drawing frames.
 *
 * This is what Adobe Substance 3D Designer does for its 3D viewport.
 * radeonsi doesn't use NGG for programs with transform feedback varyings, and
 * on Navi21 the transition from NGG to legacy shaders needs a workaround,
 * without which the composited pixels were lost:
 * https://gitlab.freedesktop.org/drm/amd/-/issues/1810
 */

#include "piglit-util-gl.h"

#define WIDTH 256
#define HEIGHT 256
#define ITERATIONS 8

PIGLIT_GL_TEST_CONFIG_BEGIN

	config.supports_gl_core_version = 32;
	config.window_width = WIDTH;
	config.window_height = HEIGHT;
	config.window_visual = PIGLIT_GL_VISUAL_RGBA | PIGLIT_GL_VISUAL_DOUBLE;
	config.khr_no_error_support = PIGLIT_NO_ERRORS;

PIGLIT_GL_TEST_CONFIG_END

/* Rectangles drawn into the second framebuffer, in pixels. */
static const int rects[][4] = {
	{ 24, 28, 96, 168 },
	{ 144, 0, 72, 256 },
};

static const float background[4] = { 0.25, 0.25, 0.25, 1.0 };
static const float rect_color[4] = { 0.0, 1.0, 0.0, 1.0 };

/* Draws the rectangles in rect_color, or with "composite" a full-screen
 * quad that copies the color of the second framebuffer where its depth
 * was written and discards everywhere else.
 */
static const char *vs_source =
	"#version 150\n"
	"uniform mat4 proj;\n"
	"in vec4 piglit_vertex;\n"
	"out vec4 xfb_out;\n"
	"void main()\n"
	"{\n"
	"	gl_Position = proj * piglit_vertex;\n"
	"	xfb_out = piglit_vertex;\n"
	"}\n";

static const char *fs_source =
	"#version 150\n"
	"uniform bool composite;\n"
	"uniform vec4 rect_color;\n"
	"uniform sampler2D color_tex;\n"
	"uniform sampler2D depth_tex;\n"
	"in vec4 xfb_out;\n"
	"out vec4 color;\n"
	"void main()\n"
	"{\n"
	"	if (!composite) {\n"
	"		color = rect_color + vec4(0.0 * xfb_out.x);\n"
	"		return;\n"
	"	}\n"
	"	ivec2 coord = ivec2(gl_FragCoord.xy);\n"
	"	if (texelFetch(depth_tex, coord, 0).r == 1.0)\n"
	"		discard;\n"
	"	color = texelFetch(color_tex, coord, 0);\n"
	"}\n";

static GLuint prog, fbo;
static GLint composite_loc;
static float *expected;

static GLuint
create_texture(GLenum internal_format)
{
	GLuint tex;

	glGenTextures(1, &tex);
	glBindTexture(GL_TEXTURE_2D, tex);
	glTexStorage2D(GL_TEXTURE_2D, 1, internal_format, WIDTH, HEIGHT);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glBindTexture(GL_TEXTURE_2D, 0);

	return tex;
}

static void
attach(GLuint color_tex, GLuint depth_tex)
{
	GLenum status;

	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
			       GL_TEXTURE_2D, color_tex, 0);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
			       GL_TEXTURE_2D, depth_tex, 0);

	status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
	if (status != GL_FRAMEBUFFER_COMPLETE) {
		printf("FBO incomplete: %s\n",
		       piglit_get_gl_enum_name(status));
		piglit_report_result(PIGLIT_FAIL);
	}
}

void
piglit_init(int argc, char **argv)
{
	static const char *varyings[] = { "xfb_out" };

	piglit_require_extension("GL_ARB_texture_storage");

	prog = piglit_build_simple_program_unlinked(vs_source, fs_source);
	glTransformFeedbackVaryings(prog, 1, varyings,
				    GL_INTERLEAVED_ATTRIBS);
	glLinkProgram(prog);
	if (!piglit_link_check_status(prog))
		piglit_report_result(PIGLIT_FAIL);

	glUseProgram(prog);
	piglit_ortho_uniform(glGetUniformLocation(prog, "proj"),
			     WIDTH, HEIGHT);
	composite_loc = glGetUniformLocation(prog, "composite");
	glUniform4fv(glGetUniformLocation(prog, "rect_color"), 1, rect_color);
	glUniform1i(glGetUniformLocation(prog, "color_tex"), 0);
	glUniform1i(glGetUniformLocation(prog, "depth_tex"), 1);

	glGenFramebuffers(1, &fbo);

	/* The expected image: the rectangles over the background. */
	expected = malloc(WIDTH * HEIGHT * 4 * sizeof(float));
	for (int i = 0; i < WIDTH * HEIGHT; i++)
		memcpy(&expected[i * 4], background, sizeof(background));
	for (unsigned i = 0; i < ARRAY_SIZE(rects); i++) {
		const int *r = rects[i];

		for (int y = r[1]; y < r[1] + r[3]; y++) {
			for (int x = r[0]; x < r[0] + r[2]; x++) {
				memcpy(&expected[(y * WIDTH + x) * 4],
				       rect_color, sizeof(rect_color));
			}
		}
	}

	if (!piglit_check_gl_error(GL_NO_ERROR))
		piglit_report_result(PIGLIT_FAIL);
}

static bool
draw_and_check(void)
{
	static const float one = 1.0;
	static const float zero[4] = { 0 };
	bool pass;

	/* New textures each time, like the application. */
	GLuint final_color = create_texture(GL_RGBA32F);
	GLuint final_depth = create_texture(GL_DEPTH_COMPONENT32F);
	GLuint scene_color = create_texture(GL_RGBA16F);
	GLuint scene_depth = create_texture(GL_DEPTH_COMPONENT32F);

	glBindFramebuffer(GL_FRAMEBUFFER, fbo);
	glViewport(0, 0, WIDTH, HEIGHT);

	/* Clear the final framebuffer to the background. */
	attach(final_color, final_depth);
	glClearDepth(1.0);
	glClearColor(0.0, 0.0, 0.0, 1.0);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	glClearColor(background[0], background[1], background[2],
		     background[3]);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	/* Render the scene into the other framebuffer. */
	attach(scene_color, scene_depth);
	glClearBufferfv(GL_COLOR, 0, zero);
	glClearBufferfv(GL_DEPTH, 0, &one);

	glEnable(GL_DEPTH_TEST);
	glDepthFunc(GL_LESS);
	glUniform1i(composite_loc, 0);
	for (unsigned i = 0; i < ARRAY_SIZE(rects); i++)
		piglit_draw_rect(rects[i][0], rects[i][1],
				 rects[i][2], rects[i][3]);
	glDisable(GL_DEPTH_TEST);

	/* Composite the scene over the background. */
	attach(final_color, final_depth);
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, scene_color);
	glActiveTexture(GL_TEXTURE1);
	glBindTexture(GL_TEXTURE_2D, scene_depth);
	glUniform1i(composite_loc, 1);
	piglit_draw_rect(0, 0, WIDTH, HEIGHT);
	glDisable(GL_BLEND);
	glBindTexture(GL_TEXTURE_2D, 0);
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, 0);

	pass = piglit_probe_image_rgba(0, 0, WIDTH, HEIGHT, expected);

	/* Show the result in the window. */
	glBindFramebuffer(GL_READ_FRAMEBUFFER, fbo);
	glBindFramebuffer(GL_DRAW_FRAMEBUFFER, piglit_winsys_fbo);
	glBlitFramebuffer(0, 0, WIDTH, HEIGHT, 0, 0, WIDTH, HEIGHT,
			  GL_COLOR_BUFFER_BIT, GL_NEAREST);

	glDeleteTextures(1, &final_color);
	glDeleteTextures(1, &final_depth);
	glDeleteTextures(1, &scene_color);
	glDeleteTextures(1, &scene_depth);

	return pass;
}

enum piglit_result
piglit_display(void)
{
	bool pass = true;

	for (int i = 0; i < ITERATIONS && pass; i++) {
		if (!draw_and_check()) {
			printf("Iteration %d failed\n", i);
			pass = false;
		}
	}

	pass = piglit_check_gl_error(GL_NO_ERROR) && pass;

	piglit_present_results();

	return pass ? PIGLIT_PASS : PIGLIT_FAIL;
}
