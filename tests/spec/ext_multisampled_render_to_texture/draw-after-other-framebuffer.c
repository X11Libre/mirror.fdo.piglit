/*
 * SPDX-License-Identifier: MIT
 */

/**
 * reproducer for mesa#16550: when zink emulates msrtss, the blit runs in the
 * middle of the draw (in zink_batch_rp) and leaves its primitive type and,
 * with threading, its vertex buffers behind.
 */

#include "piglit-util-gl.h"

PIGLIT_GL_TEST_CONFIG_BEGIN

	config.supports_gl_es_version = 20;
	config.window_visual = PIGLIT_GL_VISUAL_DOUBLE | PIGLIT_GL_VISUAL_RGBA;
	config.khr_no_error_support = PIGLIT_NO_ERRORS;

PIGLIT_GL_TEST_CONFIG_END

#define WIDTH 512
#define HEIGHT 128
#define LAYER 32
#define CELLS 8
#define SAMPLES 4

static const char *vs_source =
	"attribute vec2 piglit_vertex;\n"
	"void main()\n"
	"{\n"
	"	gl_Position = vec4(piglit_vertex, 0.0, 1.0);\n"
	"}\n";

static const char *fs_color_source =
	"precision mediump float;\n"
	"uniform vec4 color;\n"
	"void main()\n"
	"{\n"
	"	gl_FragColor = color;\n"
	"}\n";

static const char *fs_texture_source =
	"precision mediump float;\n"
	"uniform sampler2D tex;\n"
	"void main()\n"
	"{\n"
	"	gl_FragColor = texture2D(tex, vec2(0.5));\n"
	"}\n";

static GLuint canvas_fb, layer_fb, read_fb, layer_tex;
static GLuint prog_color, prog_texture;
static GLint color_loc;

#define QUAD_LAYER 0
#define QUAD_CELL_LAYER(i) (1 + 2 * (i))
#define QUAD_CELL_SOLID(i) (2 + 2 * (i))
#define QUADS (1 + 2 * CELLS)

static void
cell_layer_rect(int i, int *x, int *y)
{
	*x = i * 64 + 16;
	*y = 64;
}

static void
cell_solid_rect(int i, int *x, int *y)
{
	*x = i * 64 + 8;
	*y = 16;
}

static void
strip(float *v, int x, int y, int w, int h, int fb_w, int fb_h)
{
	float l = 2.0f * x / fb_w - 1, r = 2.0f * (x + w) / fb_w - 1;
	float b = 2.0f * y / fb_h - 1, t = 2.0f * (y + h) / fb_h - 1;
	const float s[8] = { l, b, r, b, l, t, r, t };

	memcpy(v, s, sizeof(s));
}

static void
draw_quad(int quad)
{
	glVertexAttribPointer(PIGLIT_ATTRIB_POS, 2, GL_FLOAT, GL_FALSE, 0,
			      (void *)(intptr_t)(quad * 8 * sizeof(float)));
	glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
}

static void
cell_color(int i, float *color)
{
	color[0] = (i & 1) ? 1.0 : 0.0;
	color[1] = (i & 2) ? 1.0 : 0.5;
	color[2] = (i & 4) ? 1.0 : 0.0;
	color[3] = 1.0;
}

enum piglit_result
piglit_display(void)
{
	static const float white[4] = { 1.0, 1.0, 1.0, 1.0 };
	bool pass = true;

	glBindFramebuffer(GL_FRAMEBUFFER, canvas_fb);
	glViewport(0, 0, WIDTH, HEIGHT);
	glClearColor(0.1, 0.1, 0.1, 1.0);
	glClear(GL_COLOR_BUFFER_BIT);

	for (int i = 0; i < CELLS; i++) {
		float color[4];

		cell_color(i, color);

		glBindFramebuffer(GL_FRAMEBUFFER, layer_fb);
		glViewport(0, 0, LAYER, LAYER);
		glUseProgram(prog_color);
		glUniform4fv(color_loc, 1, color);
		draw_quad(QUAD_LAYER);

		glBindFramebuffer(GL_FRAMEBUFFER, canvas_fb);
		glViewport(0, 0, WIDTH, HEIGHT);
		glUseProgram(prog_texture);
		glBindTexture(GL_TEXTURE_2D, layer_tex);
		draw_quad(QUAD_CELL_LAYER(i));
		glUseProgram(prog_color);
		glUniform4fv(color_loc, 1, white);
		draw_quad(QUAD_CELL_SOLID(i));
	}

	glBindFramebuffer(GL_FRAMEBUFFER, read_fb);
	for (int i = 0; i < CELLS; i++) {
		float color[4];
		int x, y;

		cell_color(i, color);
		cell_layer_rect(i, &x, &y);
		pass &= piglit_probe_rect_rgba(x + 1, y + 1, LAYER - 2, LAYER - 2,
					       color);
		cell_solid_rect(i, &x, &y);
		pass &= piglit_probe_rect_rgba(x + 1, y + 1, 46, 14, white);
	}

	pass &= piglit_check_gl_error(GL_NO_ERROR);

	return pass ? PIGLIT_PASS : PIGLIT_FAIL;
}

void
piglit_init(int argc, char **argv)
{
	float vertices[QUADS][8];
	GLuint canvas_tex, buffer;
	int x, y;

	piglit_require_extension("GL_EXT_multisampled_render_to_texture");

	glGenTextures(1, &canvas_tex);
	glBindTexture(GL_TEXTURE_2D, canvas_tex);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, WIDTH, HEIGHT, 0, GL_RGBA,
		     GL_UNSIGNED_BYTE, NULL);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

	glGenFramebuffers(1, &canvas_fb);
	glBindFramebuffer(GL_FRAMEBUFFER, canvas_fb);
	glFramebufferTexture2DMultisampleEXT(GL_FRAMEBUFFER,
					     GL_COLOR_ATTACHMENT0,
					     GL_TEXTURE_2D, canvas_tex, 0,
					     SAMPLES);
	if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
		piglit_report_result(PIGLIT_SKIP);

	glGenFramebuffers(1, &read_fb);
	glBindFramebuffer(GL_FRAMEBUFFER, read_fb);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
			       GL_TEXTURE_2D, canvas_tex, 0);

	glGenTextures(1, &layer_tex);
	glBindTexture(GL_TEXTURE_2D, layer_tex);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, LAYER, LAYER, 0, GL_RGBA,
		     GL_UNSIGNED_BYTE, NULL);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

	glGenFramebuffers(1, &layer_fb);
	glBindFramebuffer(GL_FRAMEBUFFER, layer_fb);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
			       GL_TEXTURE_2D, layer_tex, 0);

	prog_color = piglit_build_simple_program(vs_source, fs_color_source);
	color_loc = glGetUniformLocation(prog_color, "color");
	prog_texture = piglit_build_simple_program(vs_source,
						   fs_texture_source);

	strip(vertices[QUAD_LAYER], 0, 0, LAYER, LAYER, LAYER, LAYER);
	for (int i = 0; i < CELLS; i++) {
		cell_layer_rect(i, &x, &y);
		strip(vertices[QUAD_CELL_LAYER(i)], x, y, LAYER, LAYER,
		      WIDTH, HEIGHT);
		cell_solid_rect(i, &x, &y);
		strip(vertices[QUAD_CELL_SOLID(i)], x, y, 48, 16,
		      WIDTH, HEIGHT);
	}
	glGenBuffers(1, &buffer);
	glBindBuffer(GL_ARRAY_BUFFER, buffer);
	glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices,
		     GL_STATIC_DRAW);
	glEnableVertexAttribArray(PIGLIT_ATTRIB_POS);
}
