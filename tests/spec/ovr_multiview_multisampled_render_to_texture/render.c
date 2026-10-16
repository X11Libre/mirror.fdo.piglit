/*
 * SPDX-License-Identifier: MIT
 */

/**
 * Tests multisampled multiview rendering with
 * OVR_multiview_multisampled_render_to_texture:
 *
 *    "operates similarly to FramebufferTextureMultiviewOVR, except that it
 *    also enables multisampled rendering into the images of a non-multisampled
 *    texture object similarly to FramebufferTexture2DMultisampleEXT."
 *
 * Each draw is an antialiased edge, which each selected layer must hold
 * after the implicit resolve, while the other layers keep their contents.
 *
 * Subtests:
 * - edge: every view gets the antialiased edge.
 * - base-view: views 1 and 2 of 4 layers get it, and layers 0 and 3 do not.
 * - single-view: one view at base 2, with num_views = 1.
 * - depth: a multisampled multiview depth attachment, as in the spec's
 *   sample code, tests each view.
 */

#include "../ovr_multiview/mv-common.h"
#include "../ext_multisampled_render_to_texture/common.h"

#define SIZE 64
#define MAX_VIEWS 4

static enum piglit_result edge(void *data);
static enum piglit_result base_view(void *data);
static enum piglit_result single_view(void *data);
static enum piglit_result depth(void *data);

static const struct piglit_subtest subtests[] = {
	{ "edge", "edge", edge },
	{ "base view index", "base-view", base_view },
	{ "single view", "single-view", single_view },
	{ "depth attachment", "depth", depth },
	{ NULL }
};

static const struct piglit_gl_test_config *piglit_config;

PIGLIT_GL_TEST_CONFIG_BEGIN

	config.supports_gl_es_version = 30;
	config.window_visual = PIGLIT_GL_VISUAL_RGBA;
	config.khr_no_error_support = PIGLIT_NO_ERRORS;
	config.subtests = subtests;
	piglit_config = &config;

PIGLIT_GL_TEST_CONFIG_END

static const float black[4] = { 0.0, 0.0, 0.0, 1.0 };
static const float red[4] = { 1.0, 0.0, 0.0, 1.0 };
static const float green[4] = { 0.0, 1.0, 0.0, 1.0 };
static const float blue[4] = { 0.0, 0.0, 1.0, 1.0 };

static int views;
static GLuint fb, tex, prog;

/* A solid color program for `count` views. */
static void
use_program(int count)
{
	char vs[512];

	snprintf(vs, sizeof(vs),
		 "layout(num_views = %d) in;\n"
		 "in vec2 piglit_vertex;\n"
		 "uniform float depth;\n"
		 "void main()\n"
		 "{\n"
		 "	gl_Position = vec4(piglit_vertex, depth, 1.0);\n"
		 "}\n", count);
	prog = mv_program("#extension GL_OVR_multiview : require\n", vs,
			  "uniform vec4 color;\n"
			  "out vec4 frag_color;\n"
			  "void main()\n"
			  "{\n"
			  "	frag_color = color;\n"
			  "}\n");
	glUseProgram(prog);
}

static void
set_color(const float *color, float depth)
{
	glUniform4fv(glGetUniformLocation(prog, "color"), 1, color);
	glUniform1f(glGetUniformLocation(prog, "depth"), depth);
}

/* A framebuffer with multisampled views [base, base + count) of a
 * `layers`-layer texture, every layer of which starts out blue, cleared to
 * black, and a program for those views.
 */
static bool
begin(int layers, int base, int count)
{
	bool pass;

	glGenFramebuffers(1, &fb);
	glBindFramebuffer(GL_FRAMEBUFFER, fb);
	tex = mv_array_texture(GL_RGBA8, SIZE, SIZE, layers);
	for (int l = 0; l < layers; l++) {
		glFramebufferTextureLayer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, tex, 0, l);
		glClearColor(blue[0], blue[1], blue[2], blue[3]);
		glClear(GL_COLOR_BUFFER_BIT);
	}
	glFramebufferTextureMultisampleMultiviewOVR(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
						    tex, 0, msrtt_samples(),
						    base, count);
	pass = mv_check_status(GL_FRAMEBUFFER_COMPLETE);
	glViewport(0, 0, SIZE, SIZE);
	glClearColor(black[0], black[1], black[2], black[3]);
	glClear(GL_COLOR_BUFFER_BIT);
	use_program(count);
	return pass;
}

/* Checks that layer has the edge in fg on bg. */
static bool
check_edge(int layer, const float *fg, const float *bg)
{
	GLuint read_fb = mv_layer_framebuffer(tex, layer);
	char what[32];
	bool pass;

	snprintf(what, sizeof(what), "layer %d", layer);
	pass = msrtt_check_edge_framebuffer(SIZE, SIZE, fg, bg, what);
	glDeleteFramebuffers(1, &read_fb);
	return pass;
}

static enum piglit_result
end(bool pass)
{
	glDisable(GL_DEPTH_TEST);
	glBindFramebuffer(GL_FRAMEBUFFER, piglit_winsys_fbo);
	glDeleteFramebuffers(1, &fb);
	glDeleteTextures(1, &tex);
	glDeleteProgram(prog);
	return piglit_check_gl_error(GL_NO_ERROR) && pass ? PIGLIT_PASS : PIGLIT_FAIL;
}

static enum piglit_result
edge(void *data)
{
	bool pass = begin(views, 0, views);

	set_color(red, 0);
	msrtt_draw_edge();
	for (int v = 0; v < views; v++)
		pass &= check_edge(v, red, black);
	return end(pass);
}

static enum piglit_result
base_view(void *data)
{
	bool pass = begin(4, 1, 2);

	set_color(red, 0);
	msrtt_draw_edge();
	pass &= mv_probe_layer(tex, 0, 0, 0, SIZE, SIZE, blue);
	pass &= check_edge(1, red, black);
	pass &= check_edge(2, red, black);
	pass &= mv_probe_layer(tex, 3, 0, 0, SIZE, SIZE, blue);
	return end(pass);
}

static enum piglit_result
single_view(void *data)
{
	bool pass = begin(3, 2, 1);

	set_color(red, 0);
	msrtt_draw_edge();
	pass &= mv_probe_layer(tex, 0, 0, 0, SIZE, SIZE, blue);
	pass &= mv_probe_layer(tex, 1, 0, 0, SIZE, SIZE, blue);
	pass &= check_edge(2, red, black);
	return end(pass);
}

/* A green quad at depth 0.5 hides a red edge at depth 0.75 and not one at
 * depth -0.5, in every view.
 */
static enum piglit_result
depth(void *data)
{
	GLuint depth_tex;
	bool pass = begin(views, 0, views);

	depth_tex = mv_array_texture(GL_DEPTH_COMPONENT24, SIZE, SIZE, views);
	glFramebufferTextureMultisampleMultiviewOVR(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
						    depth_tex, 0, msrtt_samples(),
						    0, views);
	pass &= mv_check_status(GL_FRAMEBUFFER_COMPLETE);
	glEnable(GL_DEPTH_TEST);
	glDepthFunc(GL_LESS);
	glClearDepthf(1.0);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	set_color(green, 0.5);
	piglit_draw_rect(-1, -1, 2, 2);
	set_color(red, -0.5);
	msrtt_draw_edge();
	/* farther than both, so it must not show anywhere */
	set_color(blue, 0.75);
	msrtt_draw_edge();
	for (int v = 0; v < views; v++)
		pass &= check_edge(v, red, green);

	glDeleteTextures(1, &depth_tex);
	return end(pass);
}

enum piglit_result
piglit_display(void)
{
	return mv_run_subtests(subtests);
}

void
piglit_init(int argc, char **argv)
{
	piglit_require_extension("GL_OVR_multiview_multisampled_render_to_texture");

	views = MIN2(mv_max_views(), MAX_VIEWS);
}
