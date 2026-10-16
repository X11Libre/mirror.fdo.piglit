/*
 * SPDX-License-Identifier: MIT
 */

/**
 * Tests multiview rendering with OVR_multiview on OpenGL and OpenGL ES.
 *
 * Each draw places a quad in a column of the target picked by gl_ViewID_OVR,
 * so each layer must have the quad only in its own column:
 *
 *    "When multiview rendering is enabled, the Clear (section 15.2.3),
 *    ClearBuffer* (section 15.2.3.1), and Draw* (section 10.5) commands have
 *    the same effect as:
 *
 *        for( int i = 0; i < numViews; i++ ) {
 *            for ( enum attachment : all attachment values where multiple
 *                  texture array elements have been targeted for rendering ) {
 *                FramebufferTextureLayer( target, attachment, texture,
 *                                         level, baseViewIndex + i );
 *            }
 *            View( i );
 *            <command>
 *        }"
 *
 * Subtests:
 * - clear and clearbuffer: clears reach every view (issue 15), and no
 *   other layer.
 * - draw: a draw reaches every view with its own gl_ViewID_OVR.
 * - base-view: with a base view index, only the selected layers change.
 * - instanced and indirect: issues 24 and 25 say these "just work".
 * - depth: a multiview depth attachment tests each view separately.
 * - scissor: the one scissor applies to every view (issue 2).
 * - mip-level: the views of level 1, since "these commands operate
 *   similarly to the FramebufferTextureLayer" command, which takes any level.
 * - single-view: one view, declared with num_views = 1, at a base other
 *   than 0.
 * - occlusion-query: "the samples counted must be between the maximum
 *   number of samples counted from any view, and the sum of samples counted
 *   for all views" (SAMPLES_PASSED on OpenGL, ANY_SAMPLES_PASSED on ES).
 */

#include "mv-common.h"

/* divisible by every column count, 1 through 5 */
#define W 60
#define H 16

static enum piglit_result clear(void *data);
static enum piglit_result clearbuffer(void *data);
static enum piglit_result draw(void *data);
static enum piglit_result base_view(void *data);
static enum piglit_result instanced(void *data);
static enum piglit_result indirect(void *data);
static enum piglit_result depth(void *data);
static enum piglit_result scissor(void *data);
static enum piglit_result mip_level(void *data);
static enum piglit_result single_view(void *data);
static enum piglit_result occlusion_query(void *data);

static const struct piglit_subtest subtests[] = {
	{ "Clear", "clear", clear },
	{ "ClearBuffer", "clearbuffer", clearbuffer },
	{ "draw", "draw", draw },
	{ "base view index", "base-view", base_view },
	{ "instanced draw", "instanced", instanced },
	{ "indirect draw", "indirect", indirect },
	{ "depth attachment", "depth", depth },
	{ "scissor", "scissor", scissor },
	{ "mip level", "mip-level", mip_level },
	{ "single view", "single-view", single_view },
	{ "occlusion query", "occlusion-query", occlusion_query },
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

static const float black[4] = { 0.0, 0.0, 0.0, 1.0 };
static const float red[4] = { 1.0, 0.0, 0.0, 1.0 };
static const float green[4] = { 0.0, 1.0, 0.0, 1.0 };
static const float blue[4] = { 0.0, 0.0, 1.0, 1.0 };

/* the number of views most subtests use, at most 4 */
static int views;

/* A program for `count` views that draws piglit_vertex scaled into column
 * gl_InstanceID of `columns` columns, plus gl_ViewID_OVR if per_view, with
 * the depth and color uniforms set by set_color().
 */
static GLuint
column_program(int count, int columns, bool per_view)
{
	char vs[1024];
	GLuint prog;

	snprintf(vs, sizeof(vs),
		 "layout(num_views = %d) in;\n"
		 "in vec2 piglit_vertex;\n"
		 "uniform float depth;\n"
		 "void main()\n"
		 "{\n"
		 "	float column = %s + float(gl_InstanceID);\n"
		 "	float x = (piglit_vertex.x * 0.5 + 0.5 + column) / %d.0;\n"
		 "	gl_Position = vec4(x * 2.0 - 1.0, piglit_vertex.y, depth, 1.0);\n"
		 "}\n", count, per_view ? "float(gl_ViewID_OVR)" : "0.0", columns);

	prog = mv_program("#extension GL_OVR_multiview : require\n", vs,
			  "uniform vec4 color;\n"
			  "out vec4 frag_color;\n"
			  "void main()\n"
			  "{\n"
			  "	frag_color = color;\n"
			  "}\n");
	glUseProgram(prog);
	glUniform1f(glGetUniformLocation(prog, "depth"), 0.0);
	return prog;
}

static void
set_color(GLuint prog, const float *color, float depth)
{
	glUniform4fv(glGetUniformLocation(prog, "color"), 1, color);
	glUniform1f(glGetUniformLocation(prog, "depth"), depth);
}

static GLuint fb, tex;

/* A framebuffer with views [base, base + count) of a `layers`-layer texture,
 * every layer of which starts out blue.
 */
static bool
begin(int layers, int base, int count)
{
	glGenFramebuffers(1, &fb);
	glBindFramebuffer(GL_FRAMEBUFFER, fb);
	tex = mv_array_texture(GL_RGBA8, W, H, layers);
	for (int l = 0; l < layers; l++) {
		glFramebufferTextureLayer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, tex, 0, l);
		glClearColor(blue[0], blue[1], blue[2], blue[3]);
		glClear(GL_COLOR_BUFFER_BIT);
	}
	glFramebufferTextureMultiviewOVR(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, tex,
					 0, base, count);
	glViewport(0, 0, W, H);
	return mv_check_status(GL_FRAMEBUFFER_COMPLETE);
}

static enum piglit_result
end(bool pass, GLuint prog)
{
	glDisable(GL_SCISSOR_TEST);
	glDisable(GL_DEPTH_TEST);
	glBindFramebuffer(GL_FRAMEBUFFER, piglit_winsys_fbo);
	glDeleteFramebuffers(1, &fb);
	glDeleteTextures(1, &tex);
	glDeleteProgram(prog);
	return piglit_check_gl_error(GL_NO_ERROR) && pass ? PIGLIT_PASS : PIGLIT_FAIL;
}

/* Checks that view v of `count` (layer base + v) has fg in its column of
 * `columns` and bg elsewhere.
 */
static bool
check_columns(int base, int count, int columns, const float *fg, const float *bg)
{
	int cw = W / columns;
	bool pass = true;

	for (int v = 0; v < count; v++) {
		for (int c = 0; c < columns; c++)
			pass &= mv_probe_layer(tex, base + v, c * cw, 0, cw, H,
					       c == v ? fg : bg);
	}
	return pass;
}

static void
draw_quad(void)
{
	piglit_draw_rect(-1, -1, 2, 2);
}

static enum piglit_result
clear(void *data)
{
	bool pass = begin(views + 2, 1, views);

	glClearColor(green[0], green[1], green[2], green[3]);
	glClear(GL_COLOR_BUFFER_BIT);
	pass &= mv_probe_layer(tex, 0, 0, 0, W, H, blue);
	for (int v = 0; v < views; v++)
		pass &= mv_probe_layer(tex, 1 + v, 0, 0, W, H, green);
	pass &= mv_probe_layer(tex, views + 1, 0, 0, W, H, blue);
	return end(pass, 0);
}

static enum piglit_result
clearbuffer(void *data)
{
	bool pass = begin(views, 0, views);

	glClearBufferfv(GL_COLOR, 0, green);
	for (int v = 0; v < views; v++)
		pass &= mv_probe_layer(tex, v, 0, 0, W, H, green);
	return end(pass, 0);
}

static enum piglit_result
draw(void *data)
{
	GLuint prog;
	bool pass = begin(views, 0, views);

	glClearColor(black[0], black[1], black[2], black[3]);
	glClear(GL_COLOR_BUFFER_BIT);
	prog = column_program(views, views, true);
	set_color(prog, green, 0);
	draw_quad();
	pass &= check_columns(0, views, views, green, black);
	return end(pass, prog);
}

static enum piglit_result
base_view(void *data)
{
	GLuint prog;
	bool pass = begin(4, 1, 2);

	glClearColor(black[0], black[1], black[2], black[3]);
	glClear(GL_COLOR_BUFFER_BIT);
	prog = column_program(2, 2, true);
	set_color(prog, green, 0);
	draw_quad();
	pass &= mv_probe_layer(tex, 0, 0, 0, W, H, blue);
	pass &= check_columns(1, 2, 2, green, black);
	pass &= mv_probe_layer(tex, 3, 0, 0, W, H, blue);
	return end(pass, prog);
}

/* Each view draws instances 0 and 1, which land in columns view and
 * view + 1 of views + 1 columns.
 */
static bool
check_instanced(void)
{
	int columns = views + 1, cw = W / columns;
	bool pass = true;

	for (int v = 0; v < views; v++) {
		for (int c = 0; c < columns; c++)
			pass &= mv_probe_layer(tex, v, c * cw, 0, cw, H,
					       c == v || c == v + 1 ? green : black);
	}
	return pass;
}

static enum piglit_result
instanced(void *data)
{
	GLuint prog;
	bool pass = begin(views, 0, views);

	glClearColor(black[0], black[1], black[2], black[3]);
	glClear(GL_COLOR_BUFFER_BIT);
	prog = column_program(views, views + 1, true);
	set_color(prog, green, 0);
	piglit_draw_rect_custom(-1, -1, 2, 2, false, 2);
	pass &= check_instanced();
	return end(pass, prog);
}

static enum piglit_result
indirect(void *data)
{
	static const GLuint cmd[4] = { 4, 2, 0, 0 };
	static const float quad[] = { -1, -1,  1, -1,  -1, 1,  1, 1 };
	GLuint prog, buf, vbo, vao;
	bool pass;

	if (piglit_is_gles() ? piglit_get_gl_version() < 31
			     : !piglit_is_extension_supported("GL_ARB_draw_indirect"))
		return PIGLIT_SKIP;

	pass = begin(views, 0, views);
	glClearColor(black[0], black[1], black[2], black[3]);
	glClear(GL_COLOR_BUFFER_BIT);
	prog = column_program(views, views + 1, true);
	set_color(prog, green, 0);
	glGenVertexArrays(1, &vao);
	glBindVertexArray(vao);
	glGenBuffers(1, &vbo);
	glBindBuffer(GL_ARRAY_BUFFER, vbo);
	glBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STATIC_DRAW);
	glVertexAttribPointer(PIGLIT_ATTRIB_POS, 2, GL_FLOAT, GL_FALSE, 0, NULL);
	glEnableVertexAttribArray(PIGLIT_ATTRIB_POS);
	glGenBuffers(1, &buf);
	glBindBuffer(GL_DRAW_INDIRECT_BUFFER, buf);
	glBufferData(GL_DRAW_INDIRECT_BUFFER, sizeof(cmd), cmd, GL_STATIC_DRAW);
	glDrawArraysIndirect(GL_TRIANGLE_STRIP, NULL);
	glBindBuffer(GL_DRAW_INDIRECT_BUFFER, 0);
	glDeleteBuffers(1, &buf);
	glBindVertexArray(0);
	glDeleteVertexArrays(1, &vao);
	glBindBuffer(GL_ARRAY_BUFFER, 0);
	glDeleteBuffers(1, &vbo);
	pass &= check_instanced();
	return end(pass, prog);
}

/* Each view draws a near red quad in its own column and then a far green
 * quad everywhere; the depth test must keep green out of that column in
 * that view only.
 */
static enum piglit_result
depth(void *data)
{
	GLuint prog, depth_tex;
	bool pass = begin(views, 0, views);

	depth_tex = mv_array_texture(GL_DEPTH_COMPONENT24, W, H, views);
	glFramebufferTextureMultiviewOVR(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
					 depth_tex, 0, 0, views);
	pass &= mv_check_status(GL_FRAMEBUFFER_COMPLETE);
	glClearColor(black[0], black[1], black[2], black[3]);
#ifdef PIGLIT_USE_OPENGL
	glClearDepth(1.0);
#else
	glClearDepthf(1.0);
#endif
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	glEnable(GL_DEPTH_TEST);
	glDepthFunc(GL_LESS);

	prog = column_program(views, views, true);
	set_color(prog, red, -0.5);
	draw_quad();

	/* a full-target quad: one column of one column */
	glDeleteProgram(prog);
	prog = column_program(views, 1, false);
	set_color(prog, green, 0.5);
	draw_quad();

	pass &= check_columns(0, views, views, red, green);
	glDeleteTextures(1, &depth_tex);
	return end(pass, prog);
}

static enum piglit_result
scissor(void *data)
{
	GLuint prog;
	bool pass = begin(views, 0, views);

	glClearColor(black[0], black[1], black[2], black[3]);
	glClear(GL_COLOR_BUFFER_BIT);
	glEnable(GL_SCISSOR_TEST);
	glScissor(0, 0, W, H / 2);
	prog = column_program(views, 1, false);
	set_color(prog, green, 0);
	draw_quad();
	glDisable(GL_SCISSOR_TEST);

	for (int v = 0; v < views; v++) {
		pass &= mv_probe_layer(tex, v, 0, 0, W, H / 2, green);
		pass &= mv_probe_layer(tex, v, 0, H / 2, W, H / 2, black);
	}
	return end(pass, prog);
}

static enum piglit_result
mip_level(void *data)
{
	int cw = W / views;
	GLuint prog;
	bool pass;

	/* level 0 is twice the size, and level 1 is the render target; levels
	 * other than the base one can only be attached to mipmap complete
	 * textures
	 */
	glGenFramebuffers(1, &fb);
	glBindFramebuffer(GL_FRAMEBUFFER, fb);
	tex = mv_array_texture(GL_RGBA8, W * 2, H * 2, views);
	glTexImage3D(GL_TEXTURE_2D_ARRAY, 1, GL_RGBA8, W, H, views, 0, GL_RGBA,
		     GL_UNSIGNED_BYTE, NULL);
	glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAX_LEVEL, 1);
	glFramebufferTextureMultiviewOVR(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, tex,
					 1, 0, views);
	pass = mv_check_status(GL_FRAMEBUFFER_COMPLETE);
	glViewport(0, 0, W, H);

	glClearColor(black[0], black[1], black[2], black[3]);
	glClear(GL_COLOR_BUFFER_BIT);
	prog = column_program(views, views, true);
	set_color(prog, green, 0);
	draw_quad();
	for (int v = 0; v < views; v++) {
		for (int c = 0; c < views; c++)
			pass &= mv_probe_level_layer(tex, 1, v, c * cw, 0, cw, H,
						     c == v ? green : black);
	}
	return end(pass, prog);
}

static enum piglit_result
single_view(void *data)
{
	GLuint prog;
	bool pass = begin(3, 2, 1);

	glClearColor(black[0], black[1], black[2], black[3]);
	glClear(GL_COLOR_BUFFER_BIT);
	prog = column_program(1, 1, false);
	set_color(prog, green, 0);
	draw_quad();
	pass &= mv_probe_layer(tex, 0, 0, 0, W, H, blue);
	pass &= mv_probe_layer(tex, 1, 0, 0, W, H, blue);
	pass &= mv_probe_layer(tex, 2, 0, 0, W, H, green);
	return end(pass, prog);
}

/* View v draws a quad over v + 1 columns of `views` columns, so view v
 * covers (v + 1) * W / views * H samples.
 */
static enum piglit_result
occlusion_query(void *data)
{
	char vs[1024];
	GLuint prog, query, result = 0;
	GLenum target = piglit_is_gles() ? GL_ANY_SAMPLES_PASSED : GL_SAMPLES_PASSED;
	bool pass = begin(views, 0, views);
	GLuint max = 0, sum = 0;

	snprintf(vs, sizeof(vs),
		 "layout(num_views = %d) in;\n"
		 "in vec2 piglit_vertex;\n"
		 "void main()\n"
		 "{\n"
		 "	float x = (piglit_vertex.x * 0.5 + 0.5) *\n"
		 "		  float(gl_ViewID_OVR + 1u) / %d.0;\n"
		 "	gl_Position = vec4(x * 2.0 - 1.0, piglit_vertex.y, 0.0, 1.0);\n"
		 "}\n", views, views);
	prog = mv_program("#extension GL_OVR_multiview : require\n", vs,
			  "out vec4 frag_color;\n"
			  "void main()\n"
			  "{\n"
			  "	frag_color = vec4(0.0, 1.0, 0.0, 1.0);\n"
			  "}\n");
	glUseProgram(prog);

	glGenQueries(1, &query);
	glBeginQuery(target, query);
	draw_quad();
	glEndQuery(target);
	glGetQueryObjectuiv(query, GL_QUERY_RESULT, &result);
	glDeleteQueries(1, &query);

	for (int v = 0; v < views; v++) {
		GLuint samples = (v + 1) * (W / views) * H;

		max = MAX2(max, samples);
		sum += samples;
	}
	if (piglit_is_gles()) {
		if (result != GL_TRUE) {
			printf("ANY_SAMPLES_PASSED is %u\n", result);
			pass = false;
		}
	} else if (result < max || result > sum) {
		printf("SAMPLES_PASSED is %u, expected between %u and %u\n",
		       result, max, sum);
		pass = false;
	}
	return end(pass, prog);
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

	views = MIN2(mv_max_views(), 4);
}
