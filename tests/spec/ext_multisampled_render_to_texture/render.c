/*
 * SPDX-License-Identifier: MIT
 */

/**
 * Tests that rendering through FramebufferTexture2DMultisampleEXT and
 * RenderbufferStorageMultisampleEXT is multisampled and resolved into the
 * single-sampled image:
 *
 *    "While the implicit multisample buffer is attached, color sample values
 *    are automatically resolved to a single color in the texture level each
 *    time a pixel is updated."
 *
 * Each subtest draws an antialiasable edge and checks that pixels on the edge
 * are blends of the two colors, and that pixels away from it are exact.  The
 * results are read back each way the spec allows (ReadPixels from the
 * multisampled framebuffer, issue 5, sampling the texture, and
 * CopyTexImage2D), and the subtests also cover cube map faces, mipmap levels
 * other than 0, and renderbuffers.
 */

#include "common.h"

#define SIZE 64

static enum piglit_result readpixels(void *data);
static enum piglit_result sample_texture(void *data);
static enum piglit_result copyteximage(void *data);
static enum piglit_result cube_faces(void *data);
static enum piglit_result mipmap_level(void *data);
static enum piglit_result renderbuffer(void *data);

static const struct piglit_subtest subtests[] = {
	{ "ReadPixels", "readpixels", readpixels },
	{ "texture sampling", "texture", sample_texture },
	{ "CopyTexImage2D", "copyteximage", copyteximage },
	{ "cube map faces", "cube-faces", cube_faces },
	{ "mipmap level 1", "mipmap-level", mipmap_level },
	{ "renderbuffer", "renderbuffer", renderbuffer },
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

static const float fg[4] = { 1.0, 1.0, 1.0, 1.0 };
static const float bg[4] = { 0.0, 0.0, 0.0, 1.0 };

static struct msrtt_program color_prog;
static GLuint tex_prog;

static const char *fs_texture_source =
	"precision mediump float;\n"
	"uniform sampler2D tex;\n"
	"varying vec2 coord;\n"
	"void main()\n"
	"{\n"
	"	gl_FragColor = texture2D(tex, coord);\n"
	"}\n";

static const char *vs_texture_source =
	"attribute vec2 piglit_vertex;\n"
	"varying vec2 coord;\n"
	"void main()\n"
	"{\n"
	"	gl_Position = vec4(piglit_vertex, 0.0, 1.0);\n"
	"	coord = piglit_vertex * 0.5 + 0.5;\n"
	"}\n";

/* Draws the edge into the currently bound framebuffer. */
static void
draw(void)
{
	glViewport(0, 0, SIZE, SIZE);
	glClearColor(bg[0], bg[1], bg[2], bg[3]);
	glClear(GL_COLOR_BUFFER_BIT);
	msrtt_use(&color_prog, fg, 0);
	msrtt_draw_edge();
}

/* A framebuffer rendering multisampled to a new SIZE×SIZE texture. */
static GLuint
msrtt_framebuffer(GLuint *tex)
{
	GLuint fb;

	*tex = msrtt_rgba_texture(SIZE, SIZE);
	glGenFramebuffers(1, &fb);
	glBindFramebuffer(GL_FRAMEBUFFER, fb);
	glFramebufferTexture2DMultisampleEXT(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
					     GL_TEXTURE_2D, *tex, 0,
					     msrtt_samples());
	return fb;
}

static enum piglit_result
finish(bool pass, GLuint fb, GLuint tex)
{
	glBindFramebuffer(GL_FRAMEBUFFER, piglit_winsys_fbo);
	glDeleteFramebuffers(1, &fb);
	glDeleteTextures(1, &tex);
	return piglit_check_gl_error(GL_NO_ERROR) && pass ? PIGLIT_PASS : PIGLIT_FAIL;
}

static enum piglit_result
readpixels(void *data)
{
	GLuint tex, fb = msrtt_framebuffer(&tex);

	draw();
	return finish(msrtt_check_edge_framebuffer(SIZE, SIZE, fg, bg, "ReadPixels"),
		      fb, tex);
}

static enum piglit_result
sample_texture(void *data)
{
	static const float quad[] = { -1, -1,  1, -1,  1, 1,  -1, 1 };
	GLuint tex, fb = msrtt_framebuffer(&tex);
	GLuint dst, dst_fb;
	bool pass;

	draw();

	/* copy it with a draw sampling it, which resolves it */
	dst = msrtt_rgba_texture(SIZE, SIZE);
	dst_fb = msrtt_read_framebuffer(dst);
	glUseProgram(tex_prog);
	glBindTexture(GL_TEXTURE_2D, tex);
	glVertexAttribPointer(PIGLIT_ATTRIB_POS, 2, GL_FLOAT, GL_FALSE, 0, quad);
	glDrawArrays(GL_TRIANGLE_FAN, 0, 4);

	pass = msrtt_check_edge_framebuffer(SIZE, SIZE, fg, bg, "sampled");
	glDeleteFramebuffers(1, &dst_fb);
	glDeleteTextures(1, &dst);
	return finish(pass, fb, tex);
}

static enum piglit_result
copyteximage(void *data)
{
	GLuint tex, fb = msrtt_framebuffer(&tex);
	GLuint dst, dst_fb;
	bool pass;

	draw();

	glGenTextures(1, &dst);
	glBindTexture(GL_TEXTURE_2D, dst);
	glCopyTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 0, 0, SIZE, SIZE, 0);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	pass = piglit_check_gl_error(GL_NO_ERROR);

	dst_fb = msrtt_read_framebuffer(dst);
	pass &= msrtt_check_edge_framebuffer(SIZE, SIZE, fg, bg, "CopyTexImage2D");
	glDeleteFramebuffers(1, &dst_fb);
	glDeleteTextures(1, &dst);
	return finish(pass, fb, tex);
}

/* Renders to each face, then checks each face got its own rendering. */
static enum piglit_result
cube_faces(void *data)
{
	GLuint tex, fb;
	bool pass = true;

	glGenTextures(1, &tex);
	glBindTexture(GL_TEXTURE_CUBE_MAP, tex);
	for (int f = 0; f < 6; f++)
		glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + f, 0, GL_RGBA,
			     SIZE, SIZE, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
	glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_NEAREST);

	glGenFramebuffers(1, &fb);
	glBindFramebuffer(GL_FRAMEBUFFER, fb);
	glViewport(0, 0, SIZE, SIZE);
	for (int f = 0; f < 6; f++) {
		/* a different foreground per face */
		const float face_fg[4] = { (f & 1) ? 1.0 : 0.0, (f & 2) ? 1.0 : 0.0,
					   (f & 4) ? 1.0 : 0.5, 1.0 };

		glFramebufferTexture2DMultisampleEXT(GL_FRAMEBUFFER,
						     GL_COLOR_ATTACHMENT0,
						     GL_TEXTURE_CUBE_MAP_POSITIVE_X + f,
						     tex, 0, msrtt_samples());
		if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
			printf("face %d is incomplete\n", f);
			pass = false;
			continue;
		}
		glClearColor(bg[0], bg[1], bg[2], bg[3]);
		glClear(GL_COLOR_BUFFER_BIT);
		msrtt_use(&color_prog, face_fg, 0);
		msrtt_draw_edge();
	}

	for (int f = 0; f < 6; f++) {
		const float face_fg[4] = { (f & 1) ? 1.0 : 0.0, (f & 2) ? 1.0 : 0.0,
					   (f & 4) ? 1.0 : 0.5, 1.0 };
		char what[32];

		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
				       GL_TEXTURE_CUBE_MAP_POSITIVE_X + f, tex, 0);
		snprintf(what, sizeof(what), "face %d", f);
		pass &= msrtt_check_edge_framebuffer(SIZE, SIZE, face_fg, bg, what);
	}
	return finish(pass, fb, tex);
}

/* Renders to level 1, then checks level 1 and that level 0 is untouched. */
static enum piglit_result
mipmap_level(void *data)
{
	static const float red[4] = { 1.0, 0.0, 0.0, 1.0 };
	GLuint tex, fb;
	GLubyte *level0;
	bool pass = true;

	/* attaching a level other than 0 needs OpenGL ES 3.0 or this */
	if (piglit_get_gl_version() < 30 &&
	    !piglit_is_extension_supported("GL_OES_fbo_render_mipmap"))
		return PIGLIT_SKIP;

	level0 = malloc(SIZE * 2 * SIZE * 2 * 4);
	for (int i = 0; i < SIZE * 2 * SIZE * 2; i++) {
		level0[i * 4 + 0] = 255;
		level0[i * 4 + 1] = level0[i * 4 + 2] = 0;
		level0[i * 4 + 3] = 255;
	}

	glGenTextures(1, &tex);
	glBindTexture(GL_TEXTURE_2D, tex);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, SIZE * 2, SIZE * 2, 0, GL_RGBA,
		     GL_UNSIGNED_BYTE, level0);
	glGenerateMipmap(GL_TEXTURE_2D);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	free(level0);

	glGenFramebuffers(1, &fb);
	glBindFramebuffer(GL_FRAMEBUFFER, fb);
	glFramebufferTexture2DMultisampleEXT(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
					     GL_TEXTURE_2D, tex, 1,
					     msrtt_samples());
	draw();

	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
			       GL_TEXTURE_2D, tex, 1);
	pass &= msrtt_check_edge_framebuffer(SIZE, SIZE, fg, bg, "level 1");
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
			       GL_TEXTURE_2D, tex, 0);
	pass &= piglit_probe_rect_rgba(0, 0, SIZE * 2, SIZE * 2, red);
	return finish(pass, fb, tex);
}

static enum piglit_result
renderbuffer(void *data)
{
	GLuint rb, fb;
	bool pass;

	glGenRenderbuffers(1, &rb);
	glBindRenderbuffer(GL_RENDERBUFFER, rb);
	msrtt_renderbuffer_storage(GL_RENDERBUFFER, msrtt_samples(),
					    GL_RGBA4, SIZE, SIZE);
	glGenFramebuffers(1, &fb);
	glBindFramebuffer(GL_FRAMEBUFFER, fb);
	glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
				  GL_RENDERBUFFER, rb);
	draw();

	/* RGBA4 has 4 bits per channel, which is plenty for blends */
	pass = msrtt_check_edge_framebuffer(SIZE, SIZE, fg, bg, "renderbuffer");
	glDeleteRenderbuffers(1, &rb);
	return finish(pass, fb, 0);
}

enum piglit_result
piglit_display(void)
{
	return msrtt_run_subtests(subtests);
}

void
piglit_init(int argc, char **argv)
{
	piglit_require_extension("GL_EXT_multisampled_render_to_texture");

	color_prog = msrtt_color_program();
	tex_prog = piglit_build_simple_program(vs_texture_source,
					       fs_texture_source);
	glUseProgram(tex_prog);
	glUniform1i(glGetUniformLocation(tex_prog, "tex"), 0);
}
