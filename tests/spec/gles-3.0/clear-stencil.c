/*
 * Copyright © 2024 Collabora Ltd
 *
 * Based on read-depth, which has
 * Copyright © 2015 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 */

/** @file clear-stencil.c
 *
 * Tests clearing stencil data. Some drivers, particularly for tile
 * based renderers, may try to keep track of stencil data to optimize
 * clears. This test will fail if they do it wrong (as the panfrost
 * driver did at one point).
 */

#include "piglit-util-gl.h"

#define TEX_WIDTH 4
#define TEX_HEIGHT 4
#define TEX_LAYERS 2

PIGLIT_GL_TEST_CONFIG_BEGIN

	config.supports_gl_es_version = 31;
	config.window_visual = PIGLIT_GL_VISUAL_DEPTH;

PIGLIT_GL_TEST_CONFIG_END

static GLint prog;

const char *vs_source =
	"#version 310 es\n"
	"vec4 vertcoord(uint i) {\n"
	"   vec2 base[3] = vec2[3](vec2(-1.0f, -3.0f), vec2(3.0f, 1.0f), vec2(-1.0f, 1.0f));\n"
	"   return vec4(base[i], 0.0f, 1.0f);\n"
	"}\n"

	"void main()\n"
	"{\n"
	"	gl_Position = vertcoord(uint(gl_VertexID));\n"
	"       gl_Position.y = -(gl_Position.y);\n"
	"       gl_Position.z = ((2.0f * gl_Position.z) - gl_Position.w);\n"
	"}\n";

const char *fs_source =
	"#version 310 es\n"
	"precision highp float;\n"
	"layout(location = 0) out float value;\n"

	"void main()\n"
	"{\n"
	"   value = 1.0f;\n"
	"}\n";

/* fill the depth and stencil buffers with some arbitrary (non-zero) data */
static void
fill_layer(GLuint tex, int layer)
{
	GLuint fb;
	GLenum zero = GL_ZERO;

	glGenFramebuffers(1, &fb);
	glBindFramebuffer(GL_FRAMEBUFFER, fb);
	glFramebufferTextureLayer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, tex, 0, layer);
	glDrawBuffers(1, &zero);

	glDisable(GL_SCISSOR_TEST);
	glDepthMask(GL_TRUE);
	glStencilMask(255);
	glClearBufferfi(GL_DEPTH_STENCIL, 0, 0.8, 42);
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	glDeleteFramebuffers(1, &fb);
}

/* create a multiple layer depth/stencil texture that is cleared to 0 initially */
static GLuint
create_depth_stencil_tex(GLenum depth_type)
{
	GLuint fbo, tex;
	int i;

	glGenFramebuffers(1, &fbo);
	glBindFramebuffer(GL_FRAMEBUFFER, fbo);

	glDepthMask(GL_TRUE);
	glDisable(GL_STENCIL_TEST);
	glGenTextures(1, &tex);
	glBindTexture(GL_TEXTURE_2D_ARRAY, tex);
	/* create a 2x2x7 texture */
	glTexStorage3D(GL_TEXTURE_2D_ARRAY, 1, depth_type, TEX_WIDTH, TEX_HEIGHT, TEX_LAYERS);
	glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

	/* clear all the layers */
	for (i = 0; i < TEX_LAYERS; i++) {
		glFramebufferTextureLayer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, tex, 0, i);
		glDepthMask(GL_TRUE);
		glStencilMask(255);
		glClearBufferfi(GL_DEPTH_STENCIL, 0, 0, 0);
	}
	glBindFramebuffer(GL_FRAMEBUFFER, 0);

	fill_layer(tex, 0);
	return tex;
}

/* create the target texture for rendering, and fill it with 0's */
static GLuint
create_target_tex()
{
	static GLubyte byte_zeros[TEX_WIDTH*TEX_HEIGHT] = { 0 };
	static GLfloat float_zeros[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
	GLuint tex, fbo;

	glGenFramebuffers(1, &fbo);
	glBindFramebuffer(GL_FRAMEBUFFER, fbo);

	glGenTextures(1, &tex);
	glBindTexture(GL_TEXTURE_2D, tex);
	glTexStorage2D(GL_TEXTURE_2D, 1, GL_R8, TEX_WIDTH, TEX_HEIGHT);
	glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, TEX_WIDTH, TEX_HEIGHT, GL_RED, GL_UNSIGNED_BYTE, byte_zeros);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

	glColorMaskiEXT(0, GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
	glClearBufferfv(GL_COLOR, 0, float_zeros);
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	glDeleteFramebuffers(1, &fbo);
	return tex;
}

static bool
check_pixels(GLuint tex)
{
	GLuint fbo;
	GLubyte pixels[TEX_WIDTH*TEX_HEIGHT];
	int i;

	memset(pixels, 0xcc, sizeof(pixels));
	glGenFramebuffers(1, &fbo);
	glBindFramebuffer(GL_FRAMEBUFFER, fbo);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
	glReadBuffer(GL_COLOR_ATTACHMENT0);
	glReadPixels(0, 0, TEX_WIDTH, TEX_HEIGHT, GL_RED, GL_UNSIGNED_BYTE, pixels);
	for (i = 0; i < TEX_WIDTH; i++) {
		if (pixels[i] != 0xff)
			return false;
	}
	return true;
}

static bool
test_format(GLenum depth_format)
{
	GLuint fbo;
	GLuint depth_tex;
	GLuint out_tex;
	static GLenum drawto[] = { GL_COLOR_ATTACHMENT0 };
	bool result;

	glViewport(0, 0, TEX_WIDTH, TEX_HEIGHT);
	glScissor(0, 0, TEX_WIDTH, TEX_HEIGHT);

	depth_tex = create_depth_stencil_tex(depth_format);

	out_tex = create_target_tex();
	glGenFramebuffers(1, &fbo);
	glBindFramebuffer(GL_FRAMEBUFFER, fbo);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, out_tex, 0);
	glFramebufferTextureLayer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, depth_tex, 0, TEX_LAYERS-1);
	glDrawBuffers(1, drawto);
	glUseProgram(prog);

	glStencilFuncSeparate(GL_BACK, GL_EQUAL, 0, 255);
	glStencilFuncSeparate(GL_FRONT, GL_EQUAL, 0, 255);
	glStencilOpSeparate(GL_BACK, GL_KEEP, GL_KEEP, GL_KEEP);
	glStencilOpSeparate(GL_FRONT, GL_KEEP, GL_KEEP, GL_KEEP);

	GLuint vert_array;
	glGenVertexArrays(1, &vert_array);
	glBindVertexArray(vert_array);
	glDisable(GL_SCISSOR_TEST);
	glDepthMask(GL_FALSE);
	glEnable(GL_STENCIL_TEST);
	glEnable(GL_SCISSOR_TEST);
	glDrawArraysInstanced(GL_TRIANGLES, 0, 3, 1);
	glDisable(GL_SCISSOR_TEST);
	glBindVertexArray(0);
	glDeleteVertexArrays(1, &vert_array);

	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	glDeleteFramebuffers(1, &fbo);
	result = check_pixels(out_tex);

	glDeleteTextures(1, &out_tex);
	glDeleteTextures(1, &depth_tex);
	return result;
}

const GLenum tests[] = {
	GL_DEPTH24_STENCIL8,
	GL_DEPTH32F_STENCIL8,
};

enum piglit_result
piglit_display(void)
{
	unsigned j;

	/* Loop through formats listed in 'tests'. */
	for (j = 0; j < ARRAY_SIZE(tests); j++) {
		if (!test_format(tests[j]))
			return PIGLIT_FAIL;
	}
	return PIGLIT_PASS;
}

void
piglit_init(int argc, char **argv)
{
	piglit_require_extension("GL_NV_read_depth");
	prog = piglit_build_simple_program(vs_source, fs_source);
	glUseProgram(prog);
}
