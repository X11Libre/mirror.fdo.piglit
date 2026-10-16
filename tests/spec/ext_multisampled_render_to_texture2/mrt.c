/*
 * SPDX-License-Identifier: MIT
 */

/**
 * Tests multisampled rendering to several color textures at once, which
 * EXT_multisampled_render_to_texture2 allows:
 *
 *    "This means values like GL_COLOR_ATTACHMENTi [...] may be used."
 *
 * Up to four color attachments get textures attached by
 * FramebufferTexture2DMultisampleEXT, a draw writes a different color to
 * each, and each texture must hold the antialiased edge in its color.
 */

#include "../ext_multisampled_render_to_texture/common.h"

#define SIZE 64
#define MAX_TARGETS 4

PIGLIT_GL_TEST_CONFIG_BEGIN

	config.supports_gl_es_version = 30;
	config.window_visual = PIGLIT_GL_VISUAL_RGBA;
	config.khr_no_error_support = PIGLIT_NO_ERRORS;

PIGLIT_GL_TEST_CONFIG_END

static const char *vs_source =
	"#version 300 es\n"
	"in vec2 piglit_vertex;\n"
	"void main()\n"
	"{\n"
	"	gl_Position = vec4(piglit_vertex, 0.0, 1.0);\n"
	"}\n";

static const char *fs_source =
	"#version 300 es\n"
	"precision mediump float;\n"
	"layout(location = 0) out vec4 out0;\n"
	"layout(location = 1) out vec4 out1;\n"
	"layout(location = 2) out vec4 out2;\n"
	"layout(location = 3) out vec4 out3;\n"
	"void main()\n"
	"{\n"
	"	out0 = vec4(1.0, 0.0, 0.0, 1.0);\n"
	"	out1 = vec4(0.0, 1.0, 0.0, 1.0);\n"
	"	out2 = vec4(0.0, 0.0, 1.0, 1.0);\n"
	"	out3 = vec4(1.0, 1.0, 0.0, 1.0);\n"
	"}\n";

static const float colors[MAX_TARGETS][4] = {
	{ 1.0, 0.0, 0.0, 1.0 },
	{ 0.0, 1.0, 0.0, 1.0 },
	{ 0.0, 0.0, 1.0, 1.0 },
	{ 1.0, 1.0, 0.0, 1.0 },
};
static const float bg[4] = { 0.0, 0.0, 0.0, 1.0 };

enum piglit_result
piglit_display(void)
{
	static const GLenum bufs[MAX_TARGETS] = {
		GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1,
		GL_COLOR_ATTACHMENT2, GL_COLOR_ATTACHMENT3,
	};
	GLint max_draw_buffers, max_attachments;
	GLuint fb, tex[MAX_TARGETS], prog;
	int count;
	bool pass = true;

	glGetIntegerv(GL_MAX_DRAW_BUFFERS, &max_draw_buffers);
	glGetIntegerv(GL_MAX_COLOR_ATTACHMENTS, &max_attachments);
	count = MIN2(MAX_TARGETS, MIN2(max_draw_buffers, max_attachments));

	glGenFramebuffers(1, &fb);
	glBindFramebuffer(GL_FRAMEBUFFER, fb);
	for (int i = 0; i < count; i++) {
		tex[i] = msrtt_rgba_texture(SIZE, SIZE);
		glFramebufferTexture2DMultisampleEXT(GL_FRAMEBUFFER, bufs[i],
						     GL_TEXTURE_2D, tex[i], 0,
						     msrtt_samples());
	}
	if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
		printf("%d multisampled color attachments are incomplete\n", count);
		return PIGLIT_FAIL;
	}
	glDrawBuffers(count, bufs);

	prog = piglit_build_simple_program(vs_source, fs_source);
	glUseProgram(prog);
	glViewport(0, 0, SIZE, SIZE);
	glClearColor(bg[0], bg[1], bg[2], bg[3]);
	glClear(GL_COLOR_BUFFER_BIT);
	msrtt_draw_edge();

	for (int i = 0; i < count; i++) {
		GLuint read_fb = msrtt_read_framebuffer(tex[i]);
		char what[32];

		snprintf(what, sizeof(what), "attachment %d", i);
		pass &= msrtt_check_edge_framebuffer(SIZE, SIZE, colors[i], bg, what);
		glDeleteFramebuffers(1, &read_fb);
	}

	glBindFramebuffer(GL_FRAMEBUFFER, piglit_winsys_fbo);
	glDeleteFramebuffers(1, &fb);
	glDeleteTextures(count, tex);
	glDeleteProgram(prog);
	return piglit_check_gl_error(GL_NO_ERROR) && pass ? PIGLIT_PASS : PIGLIT_FAIL;
}

void
piglit_init(int argc, char **argv)
{
	piglit_require_extension("GL_EXT_multisampled_render_to_texture2");
}
