/*
 * SPDX-License-Identifier: MIT
 */

/**
 * Tests view-dependent outputs, which OVR_multiview2 allows:
 *
 *    "Delete the paragraph which states:
 *
 *    It is a compile- or link-time error if any output variable other
 *    than gl_Position is statically dependent on gl_ViewID_OVR."
 *
 * Each subtest colors view v with colors[v], so each layer must have its
 * own color.
 *
 * Subtests:
 * - vs-varying: the vertex shader picks the color from a uniform array
 *   indexed by gl_ViewID_OVR and passes it as a smooth varying.
 * - flat-varying: the vertex shader passes gl_ViewID_OVR as a flat uint
 *   varying, and the fragment shader indexes the array with it.
 * - fs-view-id: the fragment shader indexes the array with gl_ViewID_OVR.
 */

#include "../ovr_multiview/mv-common.h"

#define SIZE 16
#define MAX_VIEWS 4

static enum piglit_result vs_varying(void *data);
static enum piglit_result flat_varying(void *data);
static enum piglit_result fs_view_id(void *data);

static const struct piglit_subtest subtests[] = {
	{ "view-dependent varying", "vs-varying", vs_varying },
	{ "flat view id varying", "flat-varying", flat_varying },
	{ "gl_ViewID_OVR in the fragment shader", "fs-view-id", fs_view_id },
	{ NULL }
};

static const struct piglit_gl_test_config *piglit_config;

PIGLIT_GL_TEST_CONFIG_BEGIN

	config.supports_gl_core_version = 33;
	config.supports_gl_es_version = 30;
	config.window_visual = PIGLIT_GL_VISUAL_RGBA;
	config.khr_no_error_support = PIGLIT_NO_ERRORS;
	config.subtests = subtests;
	piglit_config = &config;

PIGLIT_GL_TEST_CONFIG_END

static const float colors[MAX_VIEWS][4] = {
	{ 1.0, 0.0, 0.0, 1.0 },
	{ 0.0, 1.0, 0.0, 1.0 },
	{ 0.0, 0.0, 1.0, 1.0 },
	{ 1.0, 1.0, 0.0, 1.0 },
};

static int views;

/* Draws a full quad with a program built from vs_main and fs_main for
 * `views` views, and checks that each layer has its view's color.
 */
static enum piglit_result
run(const char *vs_decls, const char *vs_main, const char *fs_decls,
    const char *fs_main)
{
	char *vs, *fs;
	GLuint prog, fb, tex;
	bool pass = true;

	(void)!asprintf(&vs,
			"layout(num_views = %d) in;\n"
			"in vec2 piglit_vertex;\n"
			"uniform vec4 colors[%d];\n"
			"%s"
			"void main()\n"
			"{\n"
			"	gl_Position = vec4(piglit_vertex, 0.0, 1.0);\n"
			"%s"
			"}\n", views, MAX_VIEWS, vs_decls, vs_main);
	(void)!asprintf(&fs,
			"uniform vec4 colors[%d];\n"
			"out vec4 frag_color;\n"
			"%s"
			"void main()\n"
			"{\n"
			"%s"
			"}\n", MAX_VIEWS, fs_decls, fs_main);
	prog = mv_program("#extension GL_OVR_multiview2 : require\n", vs, fs);
	free(vs);
	free(fs);
	glUseProgram(prog);
	glUniform4fv(glGetUniformLocation(prog, "colors"), MAX_VIEWS, colors[0]);

	glGenFramebuffers(1, &fb);
	glBindFramebuffer(GL_FRAMEBUFFER, fb);
	tex = mv_array_texture(GL_RGBA8, SIZE, SIZE, views);
	glFramebufferTextureMultiviewOVR(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, tex,
					 0, 0, views);
	pass &= mv_check_status(GL_FRAMEBUFFER_COMPLETE);
	glViewport(0, 0, SIZE, SIZE);
	glClearColor(0.0, 0.0, 0.0, 1.0);
	glClear(GL_COLOR_BUFFER_BIT);
	piglit_draw_rect(-1, -1, 2, 2);

	for (int v = 0; v < views; v++)
		pass &= mv_probe_layer(tex, v, 0, 0, SIZE, SIZE, colors[v]);

	glBindFramebuffer(GL_FRAMEBUFFER, piglit_winsys_fbo);
	glDeleteFramebuffers(1, &fb);
	glDeleteTextures(1, &tex);
	glDeleteProgram(prog);
	return piglit_check_gl_error(GL_NO_ERROR) && pass ? PIGLIT_PASS : PIGLIT_FAIL;
}

static enum piglit_result
vs_varying(void *data)
{
	return run("out vec4 color;\n",
		   "	color = colors[gl_ViewID_OVR];\n",
		   "in vec4 color;\n",
		   "	frag_color = color;\n");
}

static enum piglit_result
flat_varying(void *data)
{
	return run("flat out uint view;\n",
		   "	view = gl_ViewID_OVR;\n",
		   "flat in uint view;\n",
		   "	frag_color = colors[view];\n");
}

static enum piglit_result
fs_view_id(void *data)
{
	return run("", "", "", "	frag_color = colors[gl_ViewID_OVR];\n");
}

enum piglit_result
piglit_display(void)
{
	return mv_run_subtests(subtests);
}

void
piglit_init(int argc, char **argv)
{
	piglit_require_extension("GL_OVR_multiview2");

	views = MIN2(mv_max_views(), MAX_VIEWS);
}
