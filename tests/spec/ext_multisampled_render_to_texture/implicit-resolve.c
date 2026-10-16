/*
 * SPDX-License-Identifier: MIT
 */

/**
 * Tests rendering that continues after an operation that may resolve and
 * discard the implicit multisample buffer:
 *
 *    "When the texture level is used as a source or destination for any
 *    operation, the attachment is flushed, or when the attachment is broken,
 *    the GL implementation may discard the contents of the implicit
 *    multisample buffer. If the contents are discarded, the subsequent
 *    operations on the multisample buffer will behave as if all samples
 *    within a pixel have the value most recently written to the color buffer
 *    for that pixel."
 *
 * and, from issue 7, "the multisample buffer and the color buffer are always
 * 'in sync'": an update to the texture shows up in later rendering.
 *
 * Each subtest draws an antialiased edge, does one of the operations the spec
 * lists, and then adds a second color with additive blending.  Without
 * clamping, adding to every sample and resolving gives the same result as
 * adding to the resolved value, so whether or not the implementation
 * discarded the samples, the result has to match the same rendering done
 * without the operation.
 */

#include "common.h"

#define SIZE 64

enum op {
	OP_NONE,
	OP_FLUSH,
	OP_FINISH,
	OP_BIND_FRAMEBUFFER,
	OP_READPIXELS,
	OP_COPYTEXIMAGE,
	OP_SAMPLE,
	OP_TEXSUBIMAGE,
	OP_GENERATE_MIPMAP,
};

static enum piglit_result after_op(void *data);
static enum piglit_result texture_update(void *data);

static const struct piglit_subtest subtests[] = {
	{ "Flush", "flush", after_op, (void *)OP_FLUSH },
	{ "Finish", "finish", after_op, (void *)OP_FINISH },
	{ "BindFramebuffer", "bindframebuffer", after_op, (void *)OP_BIND_FRAMEBUFFER },
	{ "ReadPixels", "readpixels", after_op, (void *)OP_READPIXELS },
	{ "CopyTexImage2D", "copyteximage", after_op, (void *)OP_COPYTEXIMAGE },
	{ "sampling the texture", "sample", after_op, (void *)OP_SAMPLE },
	{ "TexSubImage2D", "texsubimage", after_op, (void *)OP_TEXSUBIMAGE },
	{ "GenerateMipmap", "generatemipmap", after_op, (void *)OP_GENERATE_MIPMAP },
	{ "texture update shows up in rendering", "texture-update", texture_update },
	{ NULL }
};

static const struct piglit_gl_test_config *piglit_config;

PIGLIT_GL_TEST_CONFIG_BEGIN

	config.supports_gl_es_version = 20;
	config.window_visual = PIGLIT_GL_VISUAL_RGBA;
	config.khr_no_error_support = PIGLIT_NO_ERRORS;
	config.subtests = subtests;
	piglit_config = &config;

PIGLIT_GL_TEST_CONFIG_END

static const float bg[4] = { 0.0, 0.0, 0.0, 1.0 };
static const float edge_color[4] = { 0.5, 0.5, 0.5, 1.0 };
static const float added[4] = { 0.25, 0.25, 0.25, 0.0 };

static struct msrtt_program prog;
static GLuint tex_prog;
static GLubyte *reference;

static const char *vs_texture_source =
	"attribute vec2 piglit_vertex;\n"
	"void main()\n"
	"{\n"
	"	gl_Position = vec4(piglit_vertex, 0.0, 1.0);\n"
	"}\n";

static const char *fs_texture_source =
	"precision mediump float;\n"
	"uniform sampler2D tex;\n"
	"void main()\n"
	"{\n"
	"	gl_FragColor = texture2D(tex, vec2(0.5));\n"
	"}\n";

static void
do_op(enum op op, GLuint fb, GLuint tex)
{
	static const GLubyte black[4] = { 0, 0, 0, 255 };
	GLubyte pixel[4];
	GLuint other, other_tex;

	switch (op) {
	case OP_NONE:
		break;
	case OP_FLUSH:
		glFlush();
		break;
	case OP_FINISH:
		glFinish();
		break;
	case OP_BIND_FRAMEBUFFER:
		glBindFramebuffer(GL_FRAMEBUFFER, piglit_winsys_fbo);
		glBindFramebuffer(GL_FRAMEBUFFER, fb);
		break;
	case OP_READPIXELS:
		glReadPixels(0, 0, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
		break;
	case OP_COPYTEXIMAGE:
		glGenTextures(1, &other_tex);
		glBindTexture(GL_TEXTURE_2D, other_tex);
		glCopyTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 0, 0, SIZE, SIZE, 0);
		glDeleteTextures(1, &other_tex);
		break;
	case OP_SAMPLE:
		/* a draw elsewhere with the texture bound */
		other_tex = msrtt_rgba_texture(4, 4);
		other = msrtt_read_framebuffer(other_tex);
		glViewport(0, 0, 4, 4);
		glUseProgram(tex_prog);
		glBindTexture(GL_TEXTURE_2D, tex);
		piglit_draw_rect(-1, -1, 2, 2);
		glDeleteFramebuffers(1, &other);
		glDeleteTextures(1, &other_tex);
		glBindFramebuffer(GL_FRAMEBUFFER, fb);
		glViewport(0, 0, SIZE, SIZE);
		break;
	case OP_TEXSUBIMAGE:
		/* the top left pixel, which already is black */
		glBindTexture(GL_TEXTURE_2D, tex);
		glTexSubImage2D(GL_TEXTURE_2D, 0, 0, SIZE - 1, 1, 1, GL_RGBA,
				GL_UNSIGNED_BYTE, black);
		break;
	case OP_GENERATE_MIPMAP:
		glBindTexture(GL_TEXTURE_2D, tex);
		glGenerateMipmap(GL_TEXTURE_2D);
		break;
	}
}

/* Renders the edge, the operation, and the added color into a new texture,
 * and returns the result read through a single-sampled framebuffer.
 */
static GLubyte *
render(enum op op, const GLubyte *update, int ux, int uy, int uw, int uh)
{
	GLubyte *pixels = malloc(SIZE * SIZE * 4);
	GLuint fb, tex = msrtt_rgba_texture(SIZE, SIZE);

	glGenFramebuffers(1, &fb);
	glBindFramebuffer(GL_FRAMEBUFFER, fb);
	glFramebufferTexture2DMultisampleEXT(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
					     GL_TEXTURE_2D, tex, 0,
					     msrtt_samples());
	glViewport(0, 0, SIZE, SIZE);
	glClearColor(bg[0], bg[1], bg[2], bg[3]);
	glClear(GL_COLOR_BUFFER_BIT);
	msrtt_use(&prog, edge_color, 0);
	msrtt_draw_edge();

	do_op(op, fb, tex);
	if (update) {
		glBindTexture(GL_TEXTURE_2D, tex);
		glTexSubImage2D(GL_TEXTURE_2D, 0, ux, uy, uw, uh, GL_RGBA,
				GL_UNSIGNED_BYTE, update);
	}

	glEnable(GL_BLEND);
	glBlendFunc(GL_ONE, GL_ONE);
	msrtt_use(&prog, added, 0);
	piglit_draw_rect(-1, -1, 2, 2);
	glDisable(GL_BLEND);

	glBindFramebuffer(GL_FRAMEBUFFER, piglit_winsys_fbo);
	glDeleteFramebuffers(1, &fb);
	fb = msrtt_read_framebuffer(tex);
	glReadPixels(0, 0, SIZE, SIZE, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
	glBindFramebuffer(GL_FRAMEBUFFER, piglit_winsys_fbo);
	glDeleteFramebuffers(1, &fb);
	glDeleteTextures(1, &tex);
	return pixels;
}

static bool
compare(const GLubyte *got, const GLubyte *expected, const char *what)
{
	int bad = 0;

	for (int i = 0; i < SIZE * SIZE * 4; i++) {
		if (abs(got[i] - expected[i]) > 2 && bad++ < 4)
			printf("%s: pixel (%d, %d) channel %d is %d, expected %d\n",
			       what, (i / 4) % SIZE, (i / 4) / SIZE, i % 4, got[i],
			       expected[i]);
	}
	if (bad)
		printf("%s: %d channels differ\n", what, bad);
	return !bad;
}

static enum piglit_result
after_op(void *data)
{
	enum op op = (enum op)(intptr_t)data;
	GLubyte *pixels = render(op, NULL, 0, 0, 0, 0);
	bool pass = compare(pixels, reference, "after the operation");

	free(pixels);
	return piglit_check_gl_error(GL_NO_ERROR) && pass ? PIGLIT_PASS : PIGLIT_FAIL;
}

/* Replaces a block inside the drawn part with TexSubImage2D before the added
 * color is drawn; the block must show the new contents plus the added color.
 */
static enum piglit_result
texture_update(void *data)
{
	enum { X = 8, Y = 0, W = 16, H = 16 };
	GLubyte block[W * H * 4];
	GLubyte *expected = malloc(SIZE * SIZE * 4), *pixels;
	bool pass;

	for (int i = 0; i < W * H; i++) {
		block[i * 4 + 0] = 0;
		block[i * 4 + 1] = 128;
		block[i * 4 + 2] = 0;
		block[i * 4 + 3] = 255;
	}

	memcpy(expected, reference, SIZE * SIZE * 4);
	for (int y = Y; y < Y + H; y++) {
		for (int x = X; x < X + W; x++) {
			GLubyte *p = &expected[(y * SIZE + x) * 4];

			p[0] = 64;
			p[1] = 128 + 64;
			p[2] = 64;
			p[3] = 255;
		}
	}

	pixels = render(OP_NONE, block, X, Y, W, H);
	pass = compare(pixels, expected, "after TexSubImage2D");
	free(pixels);
	free(expected);
	return piglit_check_gl_error(GL_NO_ERROR) && pass ? PIGLIT_PASS : PIGLIT_FAIL;
}

enum piglit_result
piglit_display(void)
{
	return msrtt_run_subtests(subtests);
}

void
piglit_init(int argc, char **argv)
{
	float ref_fg[4], ref_bg[4];

	piglit_require_extension("GL_EXT_multisampled_render_to_texture");

	prog = msrtt_color_program();
	tex_prog = piglit_build_simple_program(vs_texture_source,
					       fs_texture_source);
	glUseProgram(tex_prog);
	glUniform1i(glGetUniformLocation(tex_prog, "tex"), 0);

	/* the reference has to be antialiased for the comparisons to mean
	 * anything
	 */
	reference = render(OP_NONE, NULL, 0, 0, 0, 0);
	for (int c = 0; c < 4; c++) {
		ref_fg[c] = edge_color[c] + added[c];
		ref_bg[c] = bg[c] + added[c];
	}
	if (!msrtt_check_edge(reference, SIZE, SIZE, ref_fg, ref_bg, "reference"))
		piglit_report_result(PIGLIT_FAIL);
}
