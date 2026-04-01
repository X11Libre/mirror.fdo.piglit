/*
 * Copyright 2026 Mesa contributors
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
 */

/**
 * @file large-tex.c
 *
 * Tests various operations on textures of the maximum supported size.
 */

#include "piglit-util-gl.h"

PIGLIT_GL_TEST_CONFIG_BEGIN

	config.supports_gl_compat_version = 30;
	config.window_visual = PIGLIT_GL_VISUAL_RGB | PIGLIT_GL_VISUAL_DOUBLE;
	config.khr_no_error_support = PIGLIT_NO_ERRORS;

PIGLIT_GL_TEST_CONFIG_END

static bool have_pbo;
static bool have_copy_image;
static bool have_clear_texture;
static bool have_image_load_store;

static GLint tex_dim;

#define VAL_A 1
#define VAL_B 2
#define TEST_RECT_DIM 32
#define TEST_RECT_SIZE (TEST_RECT_DIM * TEST_RECT_DIM)

enum Quadrant
{
	BOTTOM_LEFT,
	BOTTOM_RIGHT,
	TOP_RIGHT,
	TOP_LEFT,
};

struct rect { uint32_t x, y, w, h; };
static struct rect
quadrant_rect(enum Quadrant q)
{
	uint32_t x, y;
	switch (q) {
		case BOTTOM_LEFT:
			x = tex_dim / 4;
			y = tex_dim / 4;
			break;
		case BOTTOM_RIGHT:
			x = 3 * tex_dim / 4;
			y = tex_dim / 4;
			break;
		case TOP_RIGHT:
			x = 3 * tex_dim / 4;
			y = 3 * tex_dim / 4;
			break;
		case TOP_LEFT:
			x = tex_dim / 4;
			y = 3 * tex_dim / 4;
			break;
	}
	return (struct rect) { x, y, TEST_RECT_DIM, TEST_RECT_DIM };
};

static const char *vs_src =
"#version 130\n"
"in vec2 pos;\n"
"void main() {"
"    gl_Position = vec4(pos, 0, 1);\n"
"}\n";
static const char *fs_src =
"#version 130\n"
"uniform float red;\n"
"out vec4 color;\n"
"void main() {"
"    color = vec4(red, 0, 0, 1);\n"
"}\n";
static const char *imgls_fs_src =
"#version 420\n"
"layout(r8ui, binding=0) readonly uniform uimage2D src_img;\n"
"layout(r8ui, binding=1) writeonly uniform uimage2D dst_img;\n"
"out vec4 dummy;\n"
"void main() {\n"
"    ivec2 c = ivec2(gl_FragCoord.xy);\n"
"    imageStore(dst_img, c, imageLoad(src_img, c));\n"
"    dummy = vec4(0.0);\n"
"}\n";

static GLuint
make_tex(void)
{
	GLint old_tex;
	glGetIntegerv(GL_TEXTURE_BINDING_2D, &old_tex);
	GLuint tex;
	glGenTextures(1, &tex);
	glBindTexture(GL_TEXTURE_2D, tex);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, tex_dim, tex_dim, 0, GL_RED, GL_UNSIGNED_BYTE, NULL);
	glBindTexture(GL_TEXTURE_2D, old_tex);
	return tex;
}

static GLuint
make_fbo(GLuint tex)
{
	GLint old_fbo;
	glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &old_fbo);
	GLuint fbo;
	glGenFramebuffers(1, &fbo);
	glBindFramebuffer(GL_DRAW_FRAMEBUFFER, fbo);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
	glBindFramebuffer(GL_DRAW_FRAMEBUFFER, old_fbo);
	return fbo;
}

static GLuint
make_buffer(uint64_t size, GLubyte *data, GLenum usage)
{
	GLint old_vbo;
	glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &old_vbo);
	GLuint vbo;
	glGenBuffers(1, &vbo);
	glBindBuffer(GL_ARRAY_BUFFER, vbo);
	glBufferData(GL_ARRAY_BUFFER, size, data, usage);
	glBindBuffer(GL_ARRAY_BUFFER, old_vbo);
	return vbo;
}

static void
clear_tex(GLuint tex, GLubyte color)
{
	GLint old_fbo;
	glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &old_fbo);
	GLuint fbo = make_fbo(tex);
	glBindFramebuffer(GL_DRAW_FRAMEBUFFER, fbo);
	glClearColor(color / 255.0f, 0, 0, 0);
	glClear(GL_COLOR_BUFFER_BIT);
	glBindFramebuffer(GL_DRAW_FRAMEBUFFER, old_fbo);
	glDeleteFramebuffers(1, &fbo);
}

static void
read_tex_pixels(GLuint tex, struct rect r, GLubyte *pixels)
{
	GLint old_fbo;
	glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &old_fbo);
	GLuint fbo = make_fbo(tex);
	glBindFramebuffer(GL_READ_FRAMEBUFFER, fbo);
	glReadPixels(r.x, r.y, r.w, r.h, GL_RED, GL_UNSIGNED_BYTE, pixels);
	glBindFramebuffer(GL_READ_FRAMEBUFFER, old_fbo);
	glDeleteFramebuffers(1, &fbo);
}

static bool
check_bytes(const GLubyte *bytes, uint64_t size, GLubyte expected)
{
	return size == 0 || (bytes[0] == expected && !memcmp(bytes, bytes + 1, size - 1));
}

static bool
verify_tex_pixels(GLuint tex, struct rect r, GLubyte expected)
{
	uint64_t size = r.w * r.h;
	GLubyte *pixels = malloc(size);
	read_tex_pixels(tex, r, pixels);
	bool result = check_bytes(pixels, size, expected);
	free(pixels);
	return result;
}

static void
draw_quad(struct rect r, GLint posAttr)
{
	float x0 = (float)r.x / tex_dim * 2 - 1;
	float y0 = (float)r.y / tex_dim * 2 - 1;
	float x1 = (float)(r.x + r.w) / tex_dim * 2 - 1;
	float y1 = (float)(r.y + r.h) / tex_dim * 2 - 1;
	float ndcCorners[12] = {
		x0, y0, x1, y0, x0, y1,
		x0, y1, x1, y0, x1, y1,
	};

	GLuint vbo = make_buffer(sizeof (ndcCorners), (GLubyte *)ndcCorners, GL_STATIC_DRAW);
	glBindBuffer(GL_ARRAY_BUFFER, vbo);
	GLuint vao;
	glGenVertexArrays(1, &vao);
	glBindVertexArray(vao);
	glVertexAttribPointer(posAttr, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), 0);
	glEnableVertexAttribArray(posAttr);

	glDrawArrays(GL_TRIANGLES, 0, 6);

	glDeleteBuffers(1, &vbo);
	glDeleteVertexArrays(1, &vao);
}

static enum piglit_result
test_clear_and_read_pixels(void)
{
	GLuint dst_tex = make_tex();
	clear_tex(dst_tex, VAL_A);
	bool pass = true;
	if (!piglit_check_gl_error(GL_NO_ERROR)) {
		pass = false;
		goto done;
	}

	for (uint32_t i = BOTTOM_LEFT; i <= TOP_LEFT; ++i) {
		struct rect r = quadrant_rect(i);
		if (!piglit_check_gl_error(GL_NO_ERROR) || !verify_tex_pixels(dst_tex, r, VAL_A)) {
			pass = false;
			break;
		}
	}

done:
	glDeleteTextures(1, &dst_tex);
	return pass ? PIGLIT_PASS : PIGLIT_FAIL;
}

static enum piglit_result
test_draw(void)
{
	GLuint prog = piglit_build_simple_program(vs_src, fs_src);
	GLint posAttr = glGetAttribLocation(prog, "pos");

	GLuint dst_tex = make_tex();
	clear_tex(dst_tex, VAL_A);
	GLuint dst_fbo = make_fbo(dst_tex);
	glBindFramebuffer(GL_DRAW_FRAMEBUFFER, dst_fbo);
	glViewport(0, 0, tex_dim, tex_dim);
	glUseProgram(prog);
	glUniform1f(glGetUniformLocation(prog, "red"), VAL_B / 255.0f);

	bool pass = true;
	for (uint32_t i = BOTTOM_LEFT; i <= TOP_LEFT; ++i) {
		struct rect r = quadrant_rect(i);
		draw_quad(r, posAttr);
		if (!piglit_check_gl_error(GL_NO_ERROR) || !verify_tex_pixels(dst_tex, r, VAL_B)) {
			pass = false;
			goto done;
		}
	}

	/* test clipped draw */
	clear_tex(dst_tex, VAL_A);
	struct rect r = {
		tex_dim - TEST_RECT_DIM / 2, tex_dim - TEST_RECT_DIM / 2,
		TEST_RECT_DIM, TEST_RECT_DIM
	};
	draw_quad(r, posAttr);
	r.w = r.h = TEST_RECT_DIM / 2;
	if (!piglit_check_gl_error(GL_NO_ERROR) || !verify_tex_pixels(dst_tex, r, VAL_B)) {
		pass = false;
	}

done:
	glDeleteFramebuffers(1, &dst_fbo);
	glDeleteTextures(1, &dst_tex);
	glDeleteProgram(prog);
	return pass ? PIGLIT_PASS : PIGLIT_FAIL;
}

static enum piglit_result
test_blit_framebuffer(void)
{
	GLuint src_tex = make_tex();
	GLuint dst_tex = make_tex();
	clear_tex(src_tex, VAL_A);
	clear_tex(dst_tex, VAL_B);
	GLuint src_fbo = make_fbo(src_tex);
	GLuint dst_fbo = make_fbo(dst_tex);
	glBindFramebuffer(GL_READ_FRAMEBUFFER, src_fbo);
	glBindFramebuffer(GL_DRAW_FRAMEBUFFER, dst_fbo);

	bool pass = true;
	for (uint32_t i = BOTTOM_LEFT; i <= TOP_LEFT; ++i) {
		struct rect r = quadrant_rect(i);
		glBlitFramebuffer(r.x, r.y, r.x + r.w, r.y + r.h,
				  r.x, r.y, r.x + r.w, r.y + r.h,
				  GL_COLOR_BUFFER_BIT, GL_NEAREST);
		if (!piglit_check_gl_error(GL_NO_ERROR) || !verify_tex_pixels(dst_tex, r, VAL_A)) {
			pass = false;
			break;
		}
	}

	glDeleteFramebuffers(1, &src_fbo);
	glDeleteFramebuffers(1, &dst_fbo);
	glDeleteTextures(1, &src_tex);
	glDeleteTextures(1, &dst_tex);
	return pass ? PIGLIT_PASS : PIGLIT_FAIL;
}

static enum piglit_result
test_copy_image_sub_data(void)
{
	if (!have_copy_image)
		return PIGLIT_SKIP;

	GLuint src_tex = make_tex();
	GLuint dst_tex = make_tex();
	clear_tex(src_tex, VAL_A);
	clear_tex(dst_tex, VAL_B);

	bool pass = true;
	for (uint32_t i = BOTTOM_LEFT; i <= TOP_LEFT; ++i) {
		struct rect r = quadrant_rect(i);
		glCopyImageSubData(src_tex, GL_TEXTURE_2D, 0, r.x, r.y, 0,
				   dst_tex, GL_TEXTURE_2D, 0, r.x, r.y, 0,
				   r.w, r.h, 1);
		if (!piglit_check_gl_error(GL_NO_ERROR) || !verify_tex_pixels(dst_tex, r, VAL_A)) {
			pass = false;
			break;
		}
	}

	glDeleteTextures(1, &src_tex);
	glDeleteTextures(1, &dst_tex);
	return pass ? PIGLIT_PASS : PIGLIT_FAIL;
}

static enum piglit_result
test_copy_tex_sub_image_2d(void)
{
	GLuint src_tex = make_tex();
	GLuint dst_tex = make_tex();
	clear_tex(src_tex, VAL_A);
	clear_tex(dst_tex, VAL_B);
	GLuint src_fbo = make_fbo(src_tex);
	glBindFramebuffer(GL_READ_FRAMEBUFFER, src_fbo);
	glBindTexture(GL_TEXTURE_2D, dst_tex);

	bool pass = true;
	for (uint32_t i = BOTTOM_LEFT; i <= TOP_LEFT; ++i) {
		struct rect r = quadrant_rect(i);
		glCopyTexSubImage2D(GL_TEXTURE_2D, 0, r.x, r.y, r.x, r.y, r.w, r.h);
		if (!piglit_check_gl_error(GL_NO_ERROR) || !verify_tex_pixels(dst_tex, r, VAL_A)) {
			pass = false;
			break;
		}
	}

	glDeleteFramebuffers(1, &src_fbo);
	glDeleteTextures(1, &src_tex);
	glDeleteTextures(1, &dst_tex);
	return pass ? PIGLIT_PASS : PIGLIT_FAIL;
}

static enum piglit_result
test_clear_tex_sub_image(void)
{
	if (!have_clear_texture)
		return PIGLIT_SKIP;

	GLuint dst_tex = make_tex();
	clear_tex(dst_tex, VAL_A);

	bool pass = true;
	for (uint32_t i = BOTTOM_LEFT; i <= TOP_LEFT; ++i) {
		struct rect r = quadrant_rect(i);
		GLubyte data = VAL_B;
		glClearTexSubImage(dst_tex, 0, r.x, r.y, 0, r.w, r.h, 1,
				   GL_RED, GL_UNSIGNED_BYTE, &data);
		if (!piglit_check_gl_error(GL_NO_ERROR) || !verify_tex_pixels(dst_tex, r, VAL_B)) {
			pass = false;
			break;
		}
	}

	glDeleteTextures(1, &dst_tex);
	return pass ? PIGLIT_PASS : PIGLIT_FAIL;
}

static enum piglit_result
test_tex_sub_image_2d(void)
{
	GLuint dst_tex = make_tex();
	clear_tex(dst_tex, VAL_A);
	glBindTexture(GL_TEXTURE_2D, dst_tex);
	GLubyte *pixels = malloc(TEST_RECT_SIZE);
	memset(pixels, VAL_B, TEST_RECT_SIZE);

	bool pass = true;
	for (uint32_t i = BOTTOM_LEFT; i <= TOP_LEFT; ++i) {
		struct rect r = quadrant_rect(i);
		glTexSubImage2D(GL_TEXTURE_2D, 0, r.x, r.y, r.w, r.h, GL_RED, GL_UNSIGNED_BYTE, pixels);
		if (!piglit_check_gl_error(GL_NO_ERROR) || !verify_tex_pixels(dst_tex, r, VAL_B)) {
			pass = false;
			break;
		}
	}

	free(pixels);
	glDeleteTextures(1, &dst_tex);
	return pass ? PIGLIT_PASS : PIGLIT_FAIL;
}

static enum piglit_result
test_tex_sub_image_2d_pbo(void)
{
	if (!have_pbo)
		return PIGLIT_SKIP;

	GLuint dst_tex = make_tex();
	clear_tex(dst_tex, VAL_A);
	glBindTexture(GL_TEXTURE_2D, dst_tex);
	GLubyte *pixels = malloc(TEST_RECT_SIZE);
	memset(pixels, VAL_B, TEST_RECT_SIZE);
	GLuint pbo = make_buffer(TEST_RECT_SIZE, pixels, GL_STREAM_DRAW);
	free(pixels);

	bool pass = true;
	for (uint32_t i = BOTTOM_LEFT; i <= TOP_LEFT; ++i) {
		struct rect r = quadrant_rect(i);
		glBindBuffer(GL_PIXEL_UNPACK_BUFFER, pbo);
		glTexSubImage2D(GL_TEXTURE_2D, 0, r.x, r.y, r.w, r.h, GL_RED, GL_UNSIGNED_BYTE, NULL);
		glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
		if (!piglit_check_gl_error(GL_NO_ERROR) || !verify_tex_pixels(dst_tex, r, VAL_B)) {
			pass = false;
			break;
		}
	}

	glDeleteBuffers(1, &pbo);
	glDeleteTextures(1, &dst_tex);
	return pass ? PIGLIT_PASS : PIGLIT_FAIL;
}

static enum piglit_result
test_get_tex_image(void)
{
	GLuint src_tex = make_tex();
	clear_tex(src_tex, VAL_A);
	glBindTexture(GL_TEXTURE_2D, src_tex);
	uint64_t size = (uint64_t)tex_dim * tex_dim;
	GLubyte *pixels = malloc(size);

	glGetTexImage(GL_TEXTURE_2D, 0, GL_RED, GL_UNSIGNED_BYTE, pixels);
	bool pass = piglit_check_gl_error(GL_NO_ERROR) && check_bytes(pixels, size, VAL_A);

	free(pixels);
	glDeleteTextures(1, &src_tex);
	return pass ? PIGLIT_PASS : PIGLIT_FAIL;
}

static enum piglit_result
test_get_tex_image_pbo(void)
{
	if (!have_pbo)
		return PIGLIT_SKIP;

	uint64_t size = (uint64_t)tex_dim * tex_dim;
	GLuint pbo = make_buffer(size, NULL, GL_STREAM_READ);
	if (glGetError() == GL_OUT_OF_MEMORY) {
		/* implementation must support 4GB buffers */
		return PIGLIT_SKIP;
	}
	GLuint src_tex = make_tex();
	clear_tex(src_tex, VAL_A);
	glBindTexture(GL_TEXTURE_2D, src_tex);

	glBindBuffer(GL_PIXEL_PACK_BUFFER, pbo);
	glGetTexImage(GL_TEXTURE_2D, 0, GL_RED, GL_UNSIGNED_BYTE, NULL);
	GLubyte *addr = glMapBuffer(GL_PIXEL_PACK_BUFFER, GL_READ_ONLY);
	bool pass = piglit_check_gl_error(GL_NO_ERROR) && check_bytes(addr, size, VAL_A);
	glUnmapBuffer(GL_PIXEL_PACK_BUFFER);
	glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);

	glDeleteBuffers(1, &pbo);
	glDeleteTextures(1, &src_tex);
	return pass ? PIGLIT_PASS : PIGLIT_FAIL;
}

static enum piglit_result
test_read_pixels_pbo(void)
{
	if (!have_pbo)
		return PIGLIT_SKIP;

	GLuint src_tex = make_tex();
	clear_tex(src_tex, VAL_A);
	GLuint pbo = make_buffer(TEST_RECT_SIZE, NULL, GL_STREAM_READ);

	bool pass = true;
	for (uint32_t i = BOTTOM_LEFT; pass && i <= TOP_LEFT; ++i) {
		struct rect r = quadrant_rect(i);
		glBindBuffer(GL_PIXEL_PACK_BUFFER, pbo);
		read_tex_pixels(src_tex, r, NULL);
		if (!piglit_check_gl_error(GL_NO_ERROR)) {
			pass = false;
		} else {
			GLubyte *addr = glMapBuffer(GL_PIXEL_PACK_BUFFER, GL_READ_ONLY);
			if (!check_bytes(addr, r.w * r.h, VAL_A)) {
				pass = false;
			}
			glUnmapBuffer(GL_PIXEL_PACK_BUFFER);
		}
		glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
	}

	glDeleteBuffers(1, &pbo);
	glDeleteTextures(1, &src_tex);
	return pass ? PIGLIT_PASS : PIGLIT_FAIL;
}

static enum piglit_result
test_draw_pixels(void)
{
	GLuint dst_tex = make_tex();
	clear_tex(dst_tex, VAL_A);
	GLuint dst_fbo = make_fbo(dst_tex);
	glBindFramebuffer(GL_DRAW_FRAMEBUFFER, dst_fbo);
	GLubyte *pixels = malloc(TEST_RECT_SIZE);
	memset(pixels, VAL_B, TEST_RECT_SIZE);
	glViewport(0, 0, tex_dim, tex_dim);
	glUseProgram(0);

	bool pass = true;
	for (uint32_t i = BOTTOM_LEFT; i <= TOP_LEFT; ++i) {
		struct rect r = quadrant_rect(i);
		glWindowPos2i(r.x, r.y);
		glDrawPixels(r.w, r.h, GL_RED, GL_UNSIGNED_BYTE, pixels);
		if (!piglit_check_gl_error(GL_NO_ERROR) || !verify_tex_pixels(dst_tex, r, VAL_B)) {
			pass = false;
			break;
		}
	}

	free(pixels);
	glDeleteFramebuffers(1, &dst_fbo);
	glDeleteTextures(1, &dst_tex);
	return pass ? PIGLIT_PASS : PIGLIT_FAIL;
}

static enum piglit_result
test_draw_pixels_pbo(void)
{
	if (!have_pbo)
		return PIGLIT_SKIP;

	GLuint dst_tex = make_tex();
	clear_tex(dst_tex, VAL_A);
	GLuint dst_fbo = make_fbo(dst_tex);
	glBindFramebuffer(GL_DRAW_FRAMEBUFFER, dst_fbo);
	GLubyte *pixels = malloc(TEST_RECT_SIZE);
	memset(pixels, VAL_B, TEST_RECT_SIZE);
	GLuint pbo = make_buffer(TEST_RECT_SIZE, pixels, GL_STREAM_DRAW);
	free(pixels);
	glViewport(0, 0, tex_dim, tex_dim);
	glUseProgram(0);

	bool pass = true;
	for (uint32_t i = BOTTOM_LEFT; i <= TOP_LEFT; ++i) {
		struct rect r = quadrant_rect(i);
		glBindBuffer(GL_PIXEL_UNPACK_BUFFER, pbo);
		glWindowPos2i(r.x, r.y);
		glDrawPixels(r.w, r.h, GL_RED, GL_UNSIGNED_BYTE, NULL);
		glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
		if (!piglit_check_gl_error(GL_NO_ERROR) || !verify_tex_pixels(dst_tex, r, VAL_B)) {
			pass = false;
			break;
		}
	}

	glDeleteFramebuffers(1, &dst_fbo);
	glDeleteTextures(1, &dst_tex);
	return pass ? PIGLIT_PASS : PIGLIT_FAIL;
}

static enum piglit_result
test_copy_pixels(void)
{
	GLuint src_tex = make_tex();
	GLuint dst_tex = make_tex();
	clear_tex(src_tex, VAL_A);
	clear_tex(dst_tex, VAL_B);
	GLuint src_fbo = make_fbo(src_tex);
	GLuint dst_fbo = make_fbo(dst_tex);
	glBindFramebuffer(GL_READ_FRAMEBUFFER, src_fbo);
	glBindFramebuffer(GL_DRAW_FRAMEBUFFER, dst_fbo);
	glViewport(0, 0, tex_dim, tex_dim);
	glUseProgram(0);

	bool pass = true;
	for (uint32_t i = BOTTOM_LEFT; i <= TOP_LEFT; ++i) {
		struct rect r = quadrant_rect(i);
		glWindowPos2i(r.x, r.y);
		glCopyPixels(r.x, r.y, r.w, r.h, GL_COLOR);
		if (!piglit_check_gl_error(GL_NO_ERROR) || !verify_tex_pixels(dst_tex, r, VAL_A)) {
			pass = false;
			break;
		}
	}

	glDeleteFramebuffers(1, &src_fbo);
	glDeleteFramebuffers(1, &dst_fbo);
	glDeleteTextures(1, &src_tex);
	glDeleteTextures(1, &dst_tex);
	return pass ? PIGLIT_PASS : PIGLIT_FAIL;
}

static enum piglit_result
test_image_load_store(void)
{
	if (!have_image_load_store)
		return PIGLIT_SKIP;

	GLuint prog = piglit_build_simple_program(vs_src, imgls_fs_src);
	GLint posAttr = glGetAttribLocation(prog, "pos");

	GLuint src_tex = make_tex();
	GLuint dst_tex = make_tex();
	clear_tex(src_tex, VAL_A);
	clear_tex(dst_tex, VAL_B);
	GLuint dummy_tex = make_tex();
	GLuint dummy_fbo = make_fbo(dummy_tex);
	glBindFramebuffer(GL_DRAW_FRAMEBUFFER, dummy_fbo);

	glViewport(0, 0, tex_dim, tex_dim);
	glUseProgram(prog);
	glBindImageTexture(0, src_tex, 0, GL_FALSE, 0, GL_READ_ONLY,  GL_R8);
	glBindImageTexture(1, dst_tex, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_R8);

	struct rect r = { 0, 0, tex_dim, tex_dim };
	draw_quad(r, posAttr);
	glMemoryBarrier(GL_ALL_BARRIER_BITS);

	bool pass = true;
	if (!piglit_check_gl_error(GL_NO_ERROR)) {
		pass = false;
		goto done;
	}

	for (uint32_t i = BOTTOM_LEFT; i <= TOP_LEFT; ++i) {
		struct rect r = quadrant_rect(i);
		if (!verify_tex_pixels(dst_tex, r, VAL_A)) {
			pass = false;
			break;
		}
	}

done:
	glDeleteTextures(1, &dummy_tex);
	glDeleteFramebuffers(1, &dummy_fbo);
	glDeleteTextures(1, &src_tex);
	glDeleteTextures(1, &dst_tex);
	glDeleteProgram(prog);
	return pass ? PIGLIT_PASS : PIGLIT_FAIL;
}

void
piglit_init(int argc, char **argv)
{
	piglit_require_extension("GL_ARB_framebuffer_object");

	have_copy_image = piglit_is_extension_supported("GL_ARB_copy_image");
	have_pbo = piglit_is_extension_supported("GL_ARB_pixel_buffer_object");
	have_clear_texture = piglit_is_extension_supported("GL_ARB_clear_texture");
	have_image_load_store = piglit_is_extension_supported("GL_ARB_shader_image_load_store");

	glGetIntegerv(GL_MAX_TEXTURE_SIZE, &tex_dim);
	enum piglit_result result = PIGLIT_PASS;

#define RUN(fn) \
	do { \
		enum piglit_result r = fn(); \
		piglit_report_subtest_result(r, #fn); \
		piglit_merge_result(&result, r); \
	} while (0)

	RUN(test_clear_and_read_pixels);
	/* subsequent tests rely on glClear and glReadPixels */
	if (result != PIGLIT_PASS) {
		goto done;
	}

	RUN(test_draw);
	RUN(test_blit_framebuffer);
	RUN(test_copy_image_sub_data);
	RUN(test_copy_tex_sub_image_2d);
	RUN(test_clear_tex_sub_image);
	RUN(test_tex_sub_image_2d);
	RUN(test_tex_sub_image_2d_pbo);
	RUN(test_get_tex_image);
	RUN(test_get_tex_image_pbo);
	RUN(test_read_pixels_pbo);
	RUN(test_draw_pixels);
	RUN(test_draw_pixels_pbo);
	RUN(test_copy_pixels);
	RUN(test_image_load_store);

#undef RUN

done:
	piglit_report_result(result);
}

enum piglit_result
piglit_display(void)
{
	/* unreached */
	return PIGLIT_FAIL;
}
