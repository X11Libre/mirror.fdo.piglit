/*
 * Copyright © 2025 Collabora Ltd.
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

/** @file pixel-local-tests.c
 * Tests for features of EXT_shader_pixel_local_storage described in GLES spec
 */

#include "piglit-util-egl.h"
#include "piglit-util-gl.h"

#ifndef GL_SHADER_PIXEL_LOCAL_STORAGE_EXT
#define GL_SHADER_PIXEL_LOCAL_STORAGE_EXT 0x8F64
#endif

static const struct piglit_subtest subtests[];
static struct piglit_gl_test_config *piglit_config;

PIGLIT_GL_TEST_CONFIG_BEGIN

	piglit_config = &config;
	config.subtests = subtests;
	config.supports_gl_es_version = 30;
	config.window_visual = PIGLIT_GL_VISUAL_RGBA | PIGLIT_GL_VISUAL_DOUBLE;

PIGLIT_GL_TEST_CONFIG_END

#define MAX_TEXTURES 3
#define FS_SIZE      2048
#define VARS_SIZE    1024
#define MAIN_SIZE    1024

enum pls_mode {
	PIXEL_LOCAL_IN,
	PIXEL_LOCAL_OUT,
	PIXEL_LOCAL_INOUT,
};

enum pls_format_index {
	PLS_R11F_G11F_B10F,
	PLS_R32F,
	PLS_RG16F,
	PLS_RGB10_A2,
	PLS_RGBA8,
	PLS_RGBA8I,
	PLS_RG16I,
	PLS_RGB10_A2UI,
	PLS_RGBA8UI,
	PLS_RG16UI,
	PLS_R32UI,
	PLS_FORMAT_COUNT,
};

static const float green[] = { 0.0, 1.0, 0.0 };

struct test_data {
	bool expected_pass;
	const char *vars;
	const char *main;
	/* This will be ignored if num_formats == PLS_FORMAT_COUNT. */
	enum pls_format_index format_indices[PLS_FORMAT_COUNT];
	int num_formats;
};

struct pls_format_data {
	GLint internal_format;
	GLenum format;
	GLenum type;
	const char *layout;
	const char *data_type;
	const char *precision;
	int num_components;
	union {
		struct {
			GLfloat value;
			GLfloat tolerance;
		} f[4];
		GLint i[4];
		GLuint u[4];
	};
};

static void
make_fragment_shader_str(char *fs, const char *precision, const char *vars,
			 const char *main, bool extension)
{
	static const char* fs_template =
		"#version 300 es\n"
		"%s"
		"precision %s;\n"
		"%s"
		"void main()\n"
		"{\n"
		"%s"
		"}\n";

	static const char *require_pls =
		"#extension GL_EXT_shader_pixel_local_storage : require\n";

	snprintf(fs, FS_SIZE, fs_template, extension ? require_pls : "",
		 precision, vars, main);
}

/* Create a string containing a pixel_local_storage block and an optional
 * out variable.
 */
static void
make_vars_str(char *dst, const char *layout, const char *data_type,
	      const char *pls_name, const char *out_var,
	      const char *out_data_type, enum pls_mode mode,
	      int n_pls_members)
{
	char tmp[VARS_SIZE];

	assert(mode <= PIXEL_LOCAL_INOUT);
	snprintf(dst, VARS_SIZE, "layout(%s)__pixel_local%sEXT mydata {\n",
		 layout,
		 (mode == PIXEL_LOCAL_IN) ? "_in" :
		  ((mode == PIXEL_LOCAL_OUT) ? "_out" : ""));
	for (int i = 0; i < n_pls_members; i++) {
		snprintf(tmp, VARS_SIZE, "	%s pls_color%d;\n",
			 data_type, i);
		strncat(dst, tmp, VARS_SIZE - strlen(dst));
	}
	snprintf(tmp, VARS_SIZE, "} %s;\n", pls_name);
	strncat(dst, tmp, VARS_SIZE - strlen(dst));

	if (out_var) {
		char *out_type;
		if (strstr(out_data_type, "ivec") ||
		    strstr(out_data_type, "int") ||
		    strstr(out_data_type, "uvec") ||
		    strstr(out_data_type, "uint"))
			out_type = "int";
		else
		       out_type = "float";
		snprintf(tmp, VARS_SIZE, "precision mediump %s;\nout %s %s;\n",
			 out_type, out_data_type, out_var);
		strncat(dst, tmp, VARS_SIZE - strlen(dst));
	}
}

/* Check that EXT_shader_pixel_local_storage is disabled. */
static enum piglit_result
is_pixel_local_disabled(void *data)
{
	if (!glIsEnabled(GL_SHADER_PIXEL_LOCAL_STORAGE_EXT))
		return PIGLIT_PASS;
	else
		return PIGLIT_FAIL;
}

static void
draw_triangle(float x1, float y1, float x2, float y2, float x3, float y3)
{
	GLfloat vertices[] = { x1, y1, 0.0,
			       x2, y2, 0.0,
			       x3, y3, 0.0, };

	glViewport(0, 0, piglit_width, piglit_height);
	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, vertices);
	glEnableVertexAttribArray(0);
	glDrawArrays(GL_TRIANGLES, 0, 3);
}

static enum piglit_result
disable_pls_and_return(enum piglit_result res)
{
	glDisable(GL_SHADER_PIXEL_LOCAL_STORAGE_EXT);
	return res;
}

static enum piglit_result
cleanup_and_return(enum piglit_result res,
		   GLsizei n_texts, GLuint *texts,
		   GLsizei n_rbs, GLuint *rbs,
		   GLsizei n_fbs, GLuint *fbs)
{
	glDisable(GL_SHADER_PIXEL_LOCAL_STORAGE_EXT);
	if (n_texts > 0)
		glDeleteTextures(n_texts, texts);
	if (n_rbs > 0)
		glDeleteRenderbuffers(n_rbs, rbs);
	if (n_fbs > 0)
		glDeleteFramebuffers(n_fbs, fbs);
	return res;
}

static GLint
build_program_with_basic_vs(char *fs)
{
	const char* vs =
		"#version 300 es\n"
		"in vec4 vPosition;\n"
		"void main()\n"
		"{\n"
		"	gl_Position = vPosition;\n"
		"}\n";
	return piglit_build_simple_program(vs, fs);
}

static enum piglit_result
validate_result(bool expected_pass, GLint prog)
{
	if (expected_pass != (bool) prog) {
		if (!prog && expected_pass)
			piglit_loge("unexpected failure\n");
		if (prog && !expected_pass) {
			piglit_loge("unexpected pass\n");
			glDeleteShader(prog);
		}
		return PIGLIT_FAIL;
	}
	return PIGLIT_PASS;
}

static enum piglit_result
_run_test(void *_data, bool extension)
{
	char fs[FS_SIZE];
	const struct test_data *test = (const struct test_data*) _data;

	make_fragment_shader_str(fs, "mediump float", test->vars,
				 test->main, extension);
	GLint prog;

	if (!test->expected_pass)
		prog = piglit_compile_shader_text_nothrow(GL_FRAGMENT_SHADER, fs,
							  test->expected_pass);
	else
		prog = build_program_with_basic_vs(fs);

	return validate_result(test->expected_pass, prog);
}

/* Attempt to render to a pixel_local_storage variable when
 * EXT_shader_pixel_local_storage is disabled.
 */
static enum piglit_result
run_no_enable_test(void *_data)
{
	glDisable(GL_SHADER_PIXEL_LOCAL_STORAGE_EXT);

	char fs[FS_SIZE];
	const struct test_data *test = (const struct test_data*) _data;

	make_fragment_shader_str(fs, "mediump float", test->vars,
				 test->main, true);
	GLint prog = build_program_with_basic_vs(fs);
	glUseProgram(prog);

	draw_triangle( 0.0,  0.5,
		      -0.5, -0.5,
		       0.5, -0.5 );

	if (!piglit_check_gl_error(GL_INVALID_OPERATION))
		return PIGLIT_FAIL;
	return PIGLIT_PASS;
}

/* Try to use shaders containing pixel_local_storage variables that don't have
 * #extension GL_EXT_shader_pixel_local_storage .
 */
static enum piglit_result
run_no_ext_test(void *data)
{
	glEnable(GL_SHADER_PIXEL_LOCAL_STORAGE_EXT);
	return disable_pls_and_return(_run_test(data, false));
}

static enum piglit_result
run_test(void *_data)
{
	glEnable(GL_SHADER_PIXEL_LOCAL_STORAGE_EXT);
	return disable_pls_and_return(_run_test(_data, true));
}

/* Try to use pixel_local_storage variables in a vertex shader. */
static enum piglit_result
run_vs_test(void *_data)
{
	glEnable(GL_SHADER_PIXEL_LOCAL_STORAGE_EXT);
	const char* vs =
		"#version 300 es\n"
		"#extension GL_EXT_shader_pixel_local_storage : require\n"
		"in vec4 vPosition;\n"
		"__pixel_localEXT mydata {\n"
		"	layout(rgba8) vec4 pls_color;\n"
		"} outp;\n"
		"void main()\n"
		"{\n"
		"	gl_Position = vPosition;\n"
		"	outp.pls_color = vec4(1, 2, 3, 4);\n"
		"}\n";

	const struct test_data *test = (const struct test_data*) _data;

	GLint prog =
		piglit_compile_shader_text_nothrow(GL_VERTEX_SHADER, vs,
						   test->expected_pass);

	return disable_pls_and_return(validate_result(test->expected_pass,
						      prog));
}

/* Check for required features of MAX_SHADER_PIXEL_LOCAL_STORAGE_FAST_SIZE_EXT
 * and MAX_SHADER_PIXEL_LOCAL_STORAGE_SIZE_EXT.
 */
static enum piglit_result
run_pls_size_fast_size_check(void *data)
{
	glEnable(GL_SHADER_PIXEL_LOCAL_STORAGE_EXT);

	GLint max_pls_size, max_pls_fast_size;
	glGetIntegerv(GL_MAX_SHADER_PIXEL_LOCAL_STORAGE_SIZE_EXT,
		      &max_pls_size);
	glGetIntegerv(GL_MAX_SHADER_PIXEL_LOCAL_STORAGE_FAST_SIZE_EXT,
		      &max_pls_fast_size);

	if (max_pls_size < 16) {
		piglit_loge("MAX_SHADER_PIXEL_LOCAL_STORAGE_SIZE_EXT too "
			    "small\n");
		return disable_pls_and_return(PIGLIT_FAIL);
	}

	if (max_pls_fast_size < 16) {
		piglit_loge("MAX_SHADER_PIXEL_LOCAL_STORAGE_FAST_SIZE_EXT too "
			    "small\n");
		return disable_pls_and_return(PIGLIT_FAIL);
	}

	if (max_pls_fast_size > max_pls_size) {
		piglit_loge("MAX_SHADER_PIXEL_LOCAL_STORAGE_FAST_SIZE_EXT "
			    "cannot be larger than \n"
			    "MAX_SHADER_PIXEL_LOCAL_STORAGE_SIZE_EXT\n");
		return disable_pls_and_return(PIGLIT_FAIL);
	}

	return disable_pls_and_return(run_test(data));
}

static void
attach_textures(GLsizei n_atts, GLenum *atts, GLuint *textures)
{
	assert(n_atts <= MAX_TEXTURES);
	glGenTextures(n_atts, textures);

	for (unsigned i = 0; i < n_atts; i++) {
		glBindTexture(GL_TEXTURE_2D, textures[i]);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 4, 4, 0, GL_RGBA,
			     GL_UNSIGNED_BYTE, NULL);

		glFramebufferTexture2D(GL_FRAMEBUFFER_EXT, atts[i],
				       GL_TEXTURE_2D, textures[i], 0);
	}
}

static GLuint
create_fbo(GLsizei n_atts, GLenum *atts, GLuint *textures)
{
	GLuint fbo;
	glGenFramebuffers(1, &fbo);
	glBindFramebuffer(GL_FRAMEBUFFER, fbo);
	attach_textures(n_atts, atts, textures);
	return fbo;
}

static enum piglit_result
run_draw_buffers_check(void *_data)
{
	glDisable(GL_SHADER_PIXEL_LOCAL_STORAGE_EXT);
	GLenum att[] = {GL_COLOR_ATTACHMENT0};
	GLuint textures[MAX_TEXTURES];
	GLenum color_attachments[] = {
		GL_COLOR_ATTACHMENT0,
		GL_COLOR_ATTACHMENT1,
		GL_COLOR_ATTACHMENT2,
	};
	GLuint fbos[3];

	fbos[0] = create_fbo(1, att, textures);

	/* Attempt to modify an attachment of the framebuffer. */
	glEnable(GL_SHADER_PIXEL_LOCAL_STORAGE_EXT);
	attach_textures(1, &color_attachments[1], textures);
	if (!piglit_check_gl_error(GL_INVALID_OPERATION))
		goto fail;
	glDisable(GL_SHADER_PIXEL_LOCAL_STORAGE_EXT);

	glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
	if (!fbos[0] || !piglit_check_gl_error(GL_NO_ERROR))
		goto fail;

	/* Attempt to bind a new draw framebuffer. */
	glEnable(GL_SHADER_PIXEL_LOCAL_STORAGE_EXT);
	glBindFramebuffer(GL_FRAMEBUFFER, fbos[0]);
	if (!piglit_check_gl_error(GL_INVALID_OPERATION))
		goto fail;

	/* Retry with pixel local storage disabled. */
	glDisable(GL_SHADER_PIXEL_LOCAL_STORAGE_EXT);
	glBindFramebuffer(GL_FRAMEBUFFER, fbos[0]);
	if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
		goto fail;

	glDrawBuffers(1, att);
	if (!piglit_check_gl_error(GL_NO_ERROR))
		goto fail;

	glEnable(GL_SHADER_PIXEL_LOCAL_STORAGE_EXT);

	/* Attempt to change color buffer selection of draw framebuffer. */
	att[0] = GL_NONE;
	glDrawBuffers(1, att);
	if (!piglit_check_gl_error(GL_INVALID_OPERATION))
		goto fail;
	att[0] = GL_COLOR_ATTACHMENT0;

	/* Attempt to delete a bound draw framebuffer. */
	glDeleteFramebuffers(1, fbos);
	if (!piglit_check_gl_error(GL_INVALID_OPERATION))
		goto fail;

	/* Retry deleting draw framebuffer with
	 * pixel local storage disabled. */
	glDisable(GL_SHADER_PIXEL_LOCAL_STORAGE_EXT);
	glDeleteFramebuffers(1, fbos);
	if (!piglit_check_gl_error(GL_NO_ERROR))
		goto fail;

	fbos[1] = create_fbo(0, color_attachments, textures);
	if (!fbos[1] || !piglit_check_gl_error(GL_NO_ERROR))
		goto fail;

	/* Try enabling pixel local storage while the
	 * draw framebuffer is incomplete. */
	if (glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE)
		goto fail;
	glEnable(GL_SHADER_PIXEL_LOCAL_STORAGE_EXT);
	if (!piglit_check_gl_error(GL_INVALID_FRAMEBUFFER_OPERATION))
		goto fail;

	/* Create a complete framebuffer with multiple attachments and retry. */
	fbos[1] = create_fbo(3, color_attachments, textures);
	glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
	if (!fbos[1] || !piglit_check_gl_error(GL_NO_ERROR))
		goto fail;
	glBindFramebuffer(GL_FRAMEBUFFER, fbos[1]);
	if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
		goto fail;
	glDrawBuffers(1, att);
	glEnable(GL_SHADER_PIXEL_LOCAL_STORAGE_EXT);
	if (!piglit_check_gl_error(GL_INVALID_OPERATION))
		goto fail;

	/* Try enabling pixel local storage while
	 * there are multiple draw buffers. */
	fbos[2] = create_fbo(1, att, textures);
	glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
	if (!fbos[2] || !piglit_check_gl_error(GL_NO_ERROR))
		goto fail;
	glBindFramebuffer(GL_FRAMEBUFFER, fbos[2]);
	if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
		goto fail;
	glDrawBuffers(3, color_attachments);
	glEnable(GL_SHADER_PIXEL_LOCAL_STORAGE_EXT);
	if (!piglit_check_gl_error(GL_INVALID_OPERATION))
		goto fail;

	return cleanup_and_return(PIGLIT_PASS, MAX_TEXTURES, textures, 0, NULL,
				  3, fbos);
fail:
	return cleanup_and_return(PIGLIT_FAIL, MAX_TEXTURES, textures, 0, NULL,
				  3, fbos);
}

/* Attempt to enable pixel_local_storage while SAMPLE_BUFFERS == 1. */
static enum piglit_result
run_multisample_test(void *_data)
{
	GLuint rb;
	GLenum format = GL_R8;
	int samples = 4;
	GLuint fbo = 0;
	int w = 128, h = 128;
	enum piglit_result res = PIGLIT_PASS;

	glDisable(GL_SHADER_PIXEL_LOCAL_STORAGE_EXT);
	glGenFramebuffers(1, &fbo);
	glBindFramebuffer(GL_DRAW_FRAMEBUFFER, fbo);

	glGenRenderbuffers(1, &rb);
	glBindRenderbuffer(GL_RENDERBUFFER, rb);

	glRenderbufferStorageMultisample(GL_RENDERBUFFER, samples,
					 format, w, h);

	glFramebufferRenderbuffer(GL_DRAW_FRAMEBUFFER,
				  GL_COLOR_ATTACHMENT0,
				  GL_RENDERBUFFER, rb);

	glEnable(GL_SHADER_PIXEL_LOCAL_STORAGE_EXT);
	if (!piglit_check_gl_error(GL_INVALID_OPERATION))
		res = PIGLIT_FAIL;

	return cleanup_and_return(res, 0, NULL, 1, &rb, 1, &fbo);
}

static void
do_blend(const float *rect, const float *src, const float *dst,
	 const float *blendcol, GLenum blendsrc, GLenum blenddst,
	 GLuint uColor1, bool blend)
{
	glUniform4f(uColor1, dst[0], dst[1], dst[2], dst[3]);

	piglit_draw_rect(rect[0], rect[1], rect[2], rect[3]);
	if (blend) {
		glEnable(GL_BLEND);
		glBlendFunc(blendsrc, blenddst);
		if (blendcol)
			glBlendColor(blendcol[0], blendcol[1], blendcol[2], blendcol[3]);
	}
	glUniform4f(uColor1, src[0], src[1], src[2], src[3]);
	piglit_draw_rect(rect[0], rect[1], rect[2], rect[3]);
	glDisable(GL_BLEND);
}

static void
do_dither(const float *rect, const float *src, const float *dst,
	  GLuint uColor1, bool dither)
{
	glUniform4f(uColor1, dst[0], dst[1], dst[2], dst[3]);

	piglit_draw_rect(rect[0], rect[1], rect[2], rect[3]);
	if (dither)
		glEnable(GL_DITHER);

	glUniform4f(uColor1, src[0], src[1], src[2], src[3]);
	piglit_draw_rect(rect[0], rect[1], rect[2], rect[3]);
	glDisable(GL_DITHER);
}

/* Check that the same result is obtained with and without blending and/or
 * dithering.
 */
static enum piglit_result
_run_blend_dither_test(bool blend, bool dither)
{
	char fs[] =
		"#version 300 es\n"
		"#extension GL_EXT_shader_pixel_local_storage : require\n"
		"precision mediump float;\n"
		"uniform vec4 uColor;\n"
		"__pixel_localEXT mydata {\n"
		"	layout(rgba8) vec4 pls_color;\n"
		"} outp;\n"
		"void main()\n"
		"{\n"
		"	outp.pls_color = uColor;\n"
		"}\n";

	GLint prog = build_program_with_basic_vs(fs);
	glUseProgram(prog);

	enum piglit_result res = PIGLIT_PASS;
	GLenum status;
	GLuint tex;
	GLuint fbos[2];

	GLfloat off[4];
	GLfloat on[4];
	float pos[] = {-0.66, -1.0, 0.33, 2.0};
	float src[] = {0.4, 0.9, 0.8, 1.0};
	float dst[] = {0.5, 0.4, 0.6, 1.0};
	float con[] = {0.2, 0.8, 0.4, 1.0};

	glGenFramebuffers(2, fbos);
	for (int enabled = 0; enabled < 2; enabled++) {
		glBindFramebuffer(GL_FRAMEBUFFER, fbos[enabled]);
		glViewport(0, 0, piglit_width, piglit_height);

		glGenTextures(1, &tex);
		glBindTexture(GL_TEXTURE_2D, tex);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8,
			     piglit_width, piglit_height, 0,
			     GL_RGBA, GL_UNSIGNED_BYTE, NULL);

		glFramebufferTexture2D(GL_FRAMEBUFFER,
				       GL_COLOR_ATTACHMENT0,
				       GL_TEXTURE_2D,
				       tex, 0);
		glBindFramebuffer(GL_FRAMEBUFFER, 0);
		if (!piglit_check_gl_error(GL_NO_ERROR)) {
			res = PIGLIT_FAIL;
			goto end;
		}

		status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
		if (status != GL_FRAMEBUFFER_COMPLETE) {
			res = PIGLIT_SKIP;
			goto end;
		}

		glEnable(GL_SHADER_PIXEL_LOCAL_STORAGE_EXT);
		glClearColor(0.0, 0.0, 0.0, 0.0);
		glClear(GL_COLOR_BUFFER_BIT);

		GLuint uColor1 = glGetUniformLocation(prog, "uColor");
		glUniform4f(uColor1, 0.3, 0.3, 0.3, 1.0);
		piglit_draw_rect(-1.0, -1.0, 0.33, 2.0);

		if (blend)
			do_blend(pos, src, dst, con, GL_CONSTANT_COLOR,
				 GL_ONE_MINUS_CONSTANT_COLOR, uColor1, enabled);
		if (dither)
			do_dither(pos, src, dst, uColor1, enabled);

		if (enabled)
			piglit_read_pixels_float(piglit_width * 3 / 12, 0,
						 1, 1, GL_RGBA, on);
		else
			piglit_read_pixels_float(piglit_width * 3 / 12, 0,
						 1, 1, GL_RGBA, off);

		glDisable(GL_SHADER_PIXEL_LOCAL_STORAGE_EXT);
	}

	if (!piglit_compare_pixels_float(off, on, piglit_tolerance, 4))
		res = PIGLIT_FAIL;

end:
	return cleanup_and_return(res, 1, &tex, 0, NULL, 2, fbos);
}


static enum piglit_result
run_blend_dither_test(void *_data)
{
	enum piglit_result res = PIGLIT_PASS;

	res &= _run_blend_dither_test(true, false);
	res &= _run_blend_dither_test(false, true);
	res &= _run_blend_dither_test(true, true);

	return res;
}

/* Use the given format data to create a string that assigns a value to a
 * pixel_local_storage variable.
 */
static enum piglit_result
make_draw_main_str(char *dst, struct pls_format_data format_data)
{
	char tmp[MAIN_SIZE];
	static const char *lhs = "	outp.pls_color0 = ";
	if (format_data.num_components > 1)
		snprintf(dst, MAIN_SIZE, "%s%s(", lhs, format_data.data_type);
	else
		snprintf(dst, MAIN_SIZE, lhs);

	for (int i = 0; i < format_data.num_components; i++) {
		switch(format_data.internal_format) {
		case GL_R11F_G11F_B10F:
		case GL_R32F:
		case GL_RG16F:
		case GL_RGB10_A2:
		case GL_RGBA8:
			snprintf(tmp, MAIN_SIZE, "%.12e", format_data.f[i].value);
			break;
		case GL_RGBA8I:
		case GL_RG16I:
			snprintf(tmp, MAIN_SIZE, "%i", format_data.i[i]);
			break;
		case GL_RGB10_A2UI:
		case GL_RGBA8UI:
		case GL_RG16UI:
		case GL_R32UI:
			snprintf(tmp, MAIN_SIZE, "%uu", format_data.u[i]);
			break;
		default:
			piglit_loge("invalid format");
			return PIGLIT_FAIL;
		};
		if (i < (format_data.num_components - 1)) {
			strncat(tmp, ",", MAIN_SIZE - strlen(tmp));
		}
		strncat(dst, tmp, MAIN_SIZE - strlen(dst));
	}
	if (format_data.num_components > 1) {
		strncat(dst, ")", MAIN_SIZE - strlen(dst));
	}
	strncat(dst, ";\n", MAIN_SIZE - strlen(dst));
	return PIGLIT_PASS;
}

static enum piglit_result
make_test_main_str(char *dst, struct pls_format_data format_data)
{
	char *index = (format_data.num_components > 1) ? "[i]" : "";
	static const char *float_template =
		"	vec4 tolerance = vec4(%.12e, %.12e, %.12e, %.12e);\n"
		"	vec4 expected = vec4(%.12e, %.12e, %.12e, %.12e);\n"
		"	outColor = vec4(0.0, 1.0, 0.0, 0.0);\n"
		"	for (int i = 0; i < %d; i++) {\n"
		"		if (abs(outp.pls_color0%s - expected[i]) > tolerance[i])\n"
		"			outColor = vec4(1.0, 0.0, 0.0, 0.0);\n"
		"	}\n";

	static const char *int_template =
		"	ivec4 expected = ivec4(%i, %i, %i, %i);\n"
		"	outColor = vec4(0.0, 1.0, 0.0, 0.0);\n"
		"	for (int i = 0; i < %d; i++) {\n"
		"		if (outp.pls_color0%s != expected[i])\n"
		"			outColor = vec4(1.0, 0.0, 0.0, 0.0);\n"
		"	}\n";

	static const char *uint_template =
		"	uvec4 expected = uvec4(%uu, %uu, %uu, %uu);\n"
		"	outColor = vec4(0.0, 1.0, 0.0, 0.0);\n"
		"	for (int i = 0; i < %d; i++) {\n"
		"		if (outp.pls_color0%s != expected[i])\n"
		"			outColor = vec4(1.0, 0.0, 0.0, 0.0);\n"
		"	}\n";

	switch(format_data.internal_format) {
	case GL_R11F_G11F_B10F:
	case GL_R32F:
	case GL_RG16F:
	case GL_RGB10_A2:
	case GL_RGBA8: {
		float tolerance[4];
		float expected[4];
		for (int i = 0; i < 4; i++) {
			if (i < format_data.num_components) {
				tolerance[i] = format_data.f[i].tolerance;
				expected[i] = format_data.f[i].value;
			} else {
				tolerance[i] = 0.0;
				expected[i] = 0.0;
			}
		}
		snprintf(dst, MAIN_SIZE, float_template,
			 tolerance[0], tolerance[1], tolerance[2], tolerance[3],
			 expected[0], expected[1], expected[2], expected[3],
			 format_data.num_components, index);
		break;
	}
	case GL_RGBA8I:
	case GL_RG16I: {
		int expected[4];
		for (int i = 0; i < 4; i++) {
			if (i < format_data.num_components)
				expected[i] = format_data.i[i];
			else
				expected[i] = 0;
		}
		snprintf(dst, MAIN_SIZE, int_template,
			 expected[0], expected[1], expected[2], expected[3],
			 format_data.num_components, index);
		break;
	}
	case GL_RGB10_A2UI:
	case GL_RGBA8UI:
	case GL_RG16UI:
	case GL_R32UI: {
		uint expected[4];
		for (int i = 0; i < 4; i++) {
			if (i < format_data.num_components)
				expected[i] = format_data.u[i];
			else
				expected[i] = 0u;
		}
		snprintf(dst, MAIN_SIZE, uint_template,
			 expected[0], expected[1], expected[2], expected[3],
			 format_data.num_components, index);
		break;
	}
	default:
		piglit_loge("invalid format");
		return PIGLIT_FAIL;
	};

	return PIGLIT_PASS;
}

/* Check that an accurate/precise value can be read from a pixel_local_storage
 * variable with the given format data.
 */
static enum piglit_result
run_check_format(struct pls_format_data format_data)
{
	enum piglit_result res = PIGLIT_PASS;
	char vars[VARS_SIZE];
	make_vars_str(vars, format_data.layout, format_data.data_type,
		      "outp", NULL, NULL, PIXEL_LOCAL_INOUT, 1);
	char draw_fs[FS_SIZE];
	char main[MAIN_SIZE];
	res = make_draw_main_str(main, format_data);
	if (res != PIGLIT_PASS)
		goto end;

	make_fragment_shader_str(draw_fs, format_data.precision, vars, main,
				 true);

	make_vars_str(vars, format_data.layout, format_data.data_type,
		      "outp", "outColor", "vec4", PIXEL_LOCAL_INOUT, 1);
	char test_fs[FS_SIZE];
	res = make_test_main_str(main, format_data);
	if (res != PIGLIT_PASS)
		goto end;

	make_fragment_shader_str(test_fs, format_data.precision, vars, main,
				 true);

	GLint draw_prog = build_program_with_basic_vs(draw_fs);
	GLint test_prog = build_program_with_basic_vs(test_fs);

	GLenum status;
	GLuint tex;
	GLuint fbo;

	glGenFramebuffers(1, &fbo);

	glBindFramebuffer(GL_FRAMEBUFFER, fbo);
	glViewport(0, 0, piglit_width, piglit_height);

	glGenTextures(1, &tex);
	glBindTexture(GL_TEXTURE_2D, tex);
	glTexImage2D(GL_TEXTURE_2D, 0, format_data.internal_format,
		     piglit_width, piglit_height, 0, format_data.format,
		     format_data.type, NULL);

	glFramebufferTexture2D(GL_FRAMEBUFFER,
			       GL_COLOR_ATTACHMENT0,
			       GL_TEXTURE_2D,
			       tex, 0);
	glBindFramebuffer(GL_FRAMEBUFFER, 0);

	if (!piglit_check_gl_error(GL_NO_ERROR)) {
		res = PIGLIT_FAIL;
		goto end;
	}

	status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
	if (status != GL_FRAMEBUFFER_COMPLETE) {
		res = PIGLIT_SKIP;
		goto end;
	}

	glEnable(GL_SHADER_PIXEL_LOCAL_STORAGE_EXT);
	glClearColor(0.0, 0.0, 0.0, 0.0);
	glClear(GL_COLOR_BUFFER_BIT);

	glUseProgram(draw_prog);
	piglit_draw_rect(-1.0, -1.0, 1.0, 1.0);

	glUseProgram(test_prog);
	piglit_draw_rect(-1.0, -1.0, 1.0, 1.0);

	glDisable(GL_SHADER_PIXEL_LOCAL_STORAGE_EXT);

	if (!piglit_probe_rect_rgb(0, 0, 1, 1, green))
		res = PIGLIT_FAIL;

end:
	return cleanup_and_return(res, 1, &tex, 0, NULL, 1, &fbo);
}

/* Check that pixel_local_storage variables with the given formats produce
 * accurate/precise results.
 */
static enum piglit_result
run_check_data(void *_data)
{
	struct pls_format_data format_data[] = {
		[PLS_R11F_G11F_B10F] =
			{
				.internal_format = GL_R11F_G11F_B10F,
				.format = GL_RGB,
				.type = GL_FLOAT,
				.layout = "r11f_g11f_b10f",
				.data_type = "vec3",
				.precision = "mediump float",
				.num_components = 3,
				.f =
				{
					{
						.value = 713.0 / 0x7ff,
						.tolerance = 2.0 / 0x3f,
					},
					{
						.value = 1111.0 / 0x7ff,
						.tolerance = 2.0 / 0x3f,
					},
					{
						.value = 423.0 / 0x3ff,
						.tolerance = 2.0 / 0x1f,
					},
				},
			},
		[PLS_R32F] =
			{
				.internal_format = GL_R32F,
				.format = GL_RED,
				.type = GL_FLOAT,
				.layout = "r32f",
				.data_type = "float",
				.precision = "highp float",
				.num_components = 1,
				.f =
				{
					{
						.value = 107370.0 / 0xffffffff,
						.tolerance = 2.0 / 0x7fffff,
					},
				},
			},
		[PLS_RG16F] =
			{
				.internal_format = GL_RG16F,
				.format = GL_RG,
				.type = GL_FLOAT,
				.layout = "rg16f",
				.data_type = "vec2",
				.precision = "mediump float",
				.num_components = 2,
				.f =
				{
					{
						.value = 20154.0 / 0xffff,
						.tolerance = 2.0 / 0x3ff,
					},
					{
						.value = 40521.0 / 0xffff,
						.tolerance = 2.0 / 0x3ff,
					},
				},
			},
		[PLS_RGB10_A2] =
			{
				.internal_format = GL_RGB10_A2,
				.format = GL_RGBA,
				.type = GL_UNSIGNED_INT_2_10_10_10_REV,
				.layout = "rgb10_a2",
				.data_type = "vec4",
				.precision = "mediump float",
				.num_components = 4,
				.f =
				{
					{
						.value = 555.0 / 0x3ff,
						.tolerance = 2.0 / 0x1f,
					},
					{
						.value = 743.0 / 0x3ff,
						.tolerance = 2.0 / 0x1f,
					},
					{
						.value = 214.0 / 0x3ff,
						.tolerance = 2.0 / 0x1f,
					},
					{
						.value = 2.0 / 3.0,
						.tolerance = 0.01,
					},
				},
			},
		[PLS_RGBA8] =
			{
				.internal_format = GL_RGBA8,
				.format = GL_RGBA,
				.type = GL_UNSIGNED_BYTE,
				.layout = "rgba8",
				.data_type = "vec4",
				.precision = "mediump float",
				.num_components = 4,
				.f =
				{
					{
						.value = 53.0 / 0xff,
						.tolerance = 2.0 / 0xff,
					},
					{
						.value = 155.0 / 0xff,
						.tolerance = 2.0 / 0xff,
					},
					{
						.value = 200.0 / 0xff,
						.tolerance = 2.0 / 0xff,
					},
					{
						.value = 220.0 / 0xff,
						.tolerance = 2.0 / 0xff,
					},
				},
			},
		[PLS_RGBA8I] =
			{
				.internal_format = GL_RGBA8I,
				.format = GL_RGBA_INTEGER,
				.type = GL_BYTE,
				.layout = "rgba8i",
				.data_type = "ivec4",
				.precision = "mediump int",
				.num_components = 4,
				.i = {-26, 77, 100, 110,},
			},
		[PLS_RG16I] =
			{
				.internal_format = GL_RG16I,
				.format = GL_RG_INTEGER,
				.type = GL_SHORT,
				.layout = "rg16i",
				.data_type = "ivec2",
				.precision = "mediump int",
				.num_components = 2,
				.i = {-10077, 20260,},
			},
		[PLS_RGB10_A2UI] =
			{
				.internal_format = GL_RGB10_A2UI,
				.format = GL_RGBA_INTEGER,
				.type = GL_UNSIGNED_INT_2_10_10_10_REV,
				.layout = "rgb10_a2ui",
				.data_type = "uvec4",
				.precision = "mediump int",
				.num_components = 4,
				.u = {555u, 743u, 214u, 2u,},
			},
		[PLS_RGBA8UI] =
			{
				.internal_format = GL_RGBA8UI,
				.format = GL_RGBA_INTEGER,
				.type = GL_UNSIGNED_BYTE,
				.layout = "rgba8ui",
				.data_type = "uvec4",
				.precision = "mediump int",
				.num_components = 4,
				.u = {53u, 155u, 200u, 220u,},
			},
		[PLS_RG16UI] =
			{
				.internal_format = GL_RG16UI,
				.format = GL_RG_INTEGER,
				.type = GL_UNSIGNED_SHORT,
				.layout = "rg16ui",
				.data_type = "uvec2",
				.precision = "mediump int",
				.num_components = 2,
				.u = {20154u, 40521u,},
			},
		[PLS_R32UI] =
			{
				.internal_format = GL_R32UI,
				.format = GL_RED_INTEGER,
				.type = GL_UNSIGNED_INT,
				.layout = "r32ui",
				.data_type = "uint",
				.precision = "highp int",
				.num_components = 1,
				.u = {4294967290u,},
			},
	};

	enum piglit_result res = PIGLIT_PASS;
	const struct test_data *test = (const struct test_data*) _data;
	assert(test->num_formats <= PLS_FORMAT_COUNT);

	for (int i = 0; i < test->num_formats; i++) {
		int format_index = (test->num_formats == PLS_FORMAT_COUNT) ?
			i : test->format_indices[i];

		res = run_check_format(format_data[format_index]);
		if (res != PIGLIT_PASS)
			return res;
	}
	return res;
}

/* Check that color buffer clears correctly set PLS variables to 0. */
static enum piglit_result
run_check_clear(void *_data)
{
	char fs[] =
		"#version 300 es\n"
		"#extension GL_EXT_shader_pixel_local_storage : require\n"
		"precision mediump float;\n"
		"__pixel_local_inEXT mydata {\n"
		"	layout(rgba8) vec4 pls_color;\n"
		"} inp;\n"
		"out vec4 final_color;\n"
		"void main()\n"
		"{\n"
		"	if (inp.pls_color.r != 0.0\n"
		"	    || inp.pls_color.g != 0.0\n"
		"	    || inp.pls_color.b != 0.0\n"
		"	    || inp.pls_color.a != 0.0)\n"
		"	{\n"
		"		final_color = vec4(1.0, 0.0, 0.0, 1.0);\n"
		"	} else {\n"
		"		final_color = vec4(0.0, 1.0, 0.0, 1.0);\n"
		"	}\n"
		"}\n";

	GLint prog = build_program_with_basic_vs(fs);

	enum piglit_result res = PIGLIT_PASS;
	GLenum status;
	GLuint tex;
	GLuint fbo;

	glGenFramebuffers(1, &fbo);

	glBindFramebuffer(GL_FRAMEBUFFER, fbo);
	glViewport(0, 0, piglit_width, piglit_height);

	glGenTextures(1, &tex);
	glBindTexture(GL_TEXTURE_2D, tex);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8,
		     piglit_width, piglit_height, 0, GL_RGBA,
		     GL_UNSIGNED_BYTE, NULL);

	glFramebufferTexture2D(GL_FRAMEBUFFER,
			       GL_COLOR_ATTACHMENT0,
			       GL_TEXTURE_2D,
			       tex, 0);

	glBindFramebuffer(GL_FRAMEBUFFER, 0);

	if (!piglit_check_gl_error(GL_NO_ERROR)) {
		res = PIGLIT_FAIL;
		goto end;
	}

	status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
	if (status != GL_FRAMEBUFFER_COMPLETE) {
		res = PIGLIT_SKIP;
		goto end;
	}

	glEnable(GL_SHADER_PIXEL_LOCAL_STORAGE_EXT);
	glClearColor(0.0, 0.0, 0.0, 0.0);
	glClear(GL_COLOR_BUFFER_BIT);

	glUseProgram(prog);

	/* draw a triangle that covers the whole screen */
	draw_triangle( -1.0, -1.0,
			3.0, -1.0,
		       -1.0,  3.0 );

	glDisable(GL_SHADER_PIXEL_LOCAL_STORAGE_EXT);

	if (!piglit_probe_rect_rgb(0, 0, 1, 1, green))
		res = PIGLIT_FAIL;

end:
	return cleanup_and_return(res, 1, &tex, 0, NULL, 1, &fbo);
}

static char eq_pls_size[VARS_SIZE];
static char gt_pls_size[VARS_SIZE];

static const struct piglit_subtest subtests[] = {
	{
		"check if disabled in initial state",
		"disabled_by_default",
		is_pixel_local_disabled,
		&((struct test_data){NULL,}),
	},
	{
		"render to a pixel_local_storage variable when extension is disabled",
		"use_when_disabled",
		run_no_enable_test,
		&((struct test_data){
			false,
			"__pixel_localEXT mydata {\n"
			"	layout(rgba8) vec4 pls_color;\n"
			"} outp;\n",
			"	outp.pls_color = vec4(0.0, 1.0, 0.0, 0.0);\n",
			{},
			0,
		}),
	},
	{
		"use in shaders without the required #extension",
		"use_without_ext",
		run_no_ext_test,
		&((struct test_data){
			false,
			"__pixel_localEXT mydata {\n"
			"	layout(rgba8) vec4 pls_color;\n"
			"} outp;\n",
			"	outp.pls_color = vec4(1, 2, 3, 4);\n",
			{},
			0,
		}),
	},
	{
		"use in a vertex shader",
		"use_in_vs",
		run_vs_test,
		&((struct test_data){
			false, "", "", {}, 0,
		}),
	},
	{
		"valid shader with named pixel_local_storage block",
		"named_block",
		run_test,
		&((struct test_data){
			true,
			"__pixel_localEXT mydata {\n"
			"	layout(rgba8) vec4 pls_color;\n"
			"} outp;\n",
			"	outp.pls_color = vec4(1, 2, 3, 4);\n",
			{},
			0,
		}),
	},
	{
		"valid shader with anonymous pixel_local_storage block",
		"anon_block",
		run_test,
		&((struct test_data){
			true,
			"__pixel_localEXT mydata {\n"
			"	layout(rgba8) vec4 pls_color;\n"
			"};\n",
			"	pls_color = vec4(1, 2, 3, 4);\n",
			{},
			0,
		}),
	},
	{
		"write to both outputs and pixel_local_storage variables",
		"out_and_pls_write",
		run_test,
		&((struct test_data){
			false,
			"__pixel_localEXT mydata {\n"
			"	layout(rgba8) vec4 pls_color;\n"
			"} outp;\n"
			"out vec4 outColor;\n",
			"	outp.pls_color = vec4(1, 2, 3, 4);\n"
			"	outColor = vec4(0, 0, 0, 1);\n",
			{},
			0,
		}),
	},
	{
		"write to outputs and read pixel_local_storage variables",
		"out_and_pls_read",
		run_test,
		&((struct test_data){
			true,
			"__pixel_localEXT mydata {\n"
			"	layout(rgba8) vec4 pls_color;\n"
			"} outp;\n"
			"out vec4 outColor;\n",
			"	outColor = outp.pls_color;\n",
			{},
			0,
		}),
	},
	{
		"write to read-only pixel_local_storage variables",
		"write_to_plsin",
		run_test,
		&((struct test_data){
			false,
			"__pixel_local_inEXT mydata {\n"
			"	layout(rgba8) vec4 pls_color;\n"
			"} outp;\n",
			"	outp.pls_color = vec4(1, 2, 3, 4);\n",
			{},
			0,
		}),
	},
	{
		"read write-only pixel_local_storage variables",
		"read_plsout",
		run_test,
		&((struct test_data){
			false,
			"__pixel_local_outEXT mydata {\n"
			"	layout(rgba8) vec4 pls_color;\n"
			"} outp;\n"
			"out vec4 outColor;\n",
			"	outColor = outp.pls_color;\n",
			{},
			0,
		}),
	},
	{
		"declare an inout pixel_local_storage block with an input and output variable",
		"plsinout_in_out_vars",
		run_test,
		&((struct test_data){
			true,
			"__pixel_localEXT mydata {\n"
			"	layout(rgba8) vec4 pls_color0;\n"
			"	layout(rgba8) vec4 pls_color1;\n"
			"} outp;\n",
			"	outp.pls_color0 = outp.pls_color1;\n",
			{},
			0,
		}),
	},
	{
		"declare a pixel_local_storage variable outside a block",
		"pls_not_in_block",
		run_test,
		&((struct test_data){
			false,
			"__pixel_local_inEXT vec4 pls_color;\n"
			"out vec4 outColor;\n",
			"	outColor = pls_color;\n",
			{},
			0,
		}),
	},
	{
		"declare a pixel_local_storage variable inside function",
		"pls_in_func",
		run_test,
		&((struct test_data){
			false,
			"out vec4 outColor;\n",
			"	__pixel_localEXT mydata {\n"
			"		layout(rgba8) vec4 pls_color;\n"
			"	} outp;\n"
			" outColor = outp.pls_color;\n",
			{},
			0,
		}),
	},
	{
		"declare a shader with multiple input pixel_local_storage blocks",
		"multi_pls",
		run_test,
		&((struct test_data){
			false,
			"__pixel_local_inEXT mydata {\n"
			"	layout(rgba8) vec4 pls_color;\n"
			"} outp;\n"
			"__pixel_localEXT mydata2 {\n"
			"	layout(rgba8) vec4 pls_color;\n"
			"} outp2;\n"
			"out vec4 outColor;\n",
			"	outColor = outp.pls_color;\n",
			{},
			0,
		}),
	},
	{
		"declare a shader with multiple output pixel_local_storage blocks",
		"multi_plsout",
		run_test,
		&((struct test_data){
			false,
			"__pixel_local_outEXT mydata {\n"
			"	layout(rgba8) vec4 pls_color;\n"
			"} outp;\n"
			"__pixel_local_outEXT mydata2 {\n"
			"	layout(rgba8) vec4 pls_color;\n"
			"} outp2;\n",
			"	outp.pls_color = vec4(1, 2, 3, 4);\n",
			{},
			0,
		}),
	},
	{
		"declare a shader with multiple inout pixel_local_storage blocks",
		"multi_plsinout",
		run_test,
		&((struct test_data){
			false,
			"__pixel_local_EXT mydata {\n"
			"	layout(rgba8) vec4 pls_color;\n"
			"} outp;\n"
			"__pixel_local_EXT indata {\n"
			"	layout(rgba8) vec4 pls_color;\n"
			"} inp;\n",
			"	outp.pls_color = inp.pls_color;\n",
			{},
			0,
		}),
	},
	{
		"declare a shader with 1 input and 1 output pixel_local_storage block",
		"one_plsin_one_plsout",
		run_test,
		&((struct test_data){
			true,
			"__pixel_local_outEXT mydata {\n"
			"	layout(rgba8) vec4 pls_color;\n"
			"} outp;\n"
			"__pixel_local_inEXT indata {\n"
			"	layout(rgba8) vec4 pls_color;\n"
			"} inp;\n",
			"	outp.pls_color = inp.pls_color;\n",
			{},
			0,
		}),
	},
	{
		"redundant qualifiers",
		"redundant_qualifiers",
		run_test,
		&((struct test_data){
			true,
			"__pixel_local_outEXT mydata {\n"
			"	layout(rgba8) __pixel_local_outEXT vec4 pls_color;\n"
			"} outp;\n"
			"__pixel_local_inEXT indata {\n"
			"	layout(rgba8) __pixel_local_inEXT vec4 pls_color;\n"
			"} inp;\n",
			"	outp.pls_color = inp.pls_color;\n",
			{},
			0,
		}),
	},
	{
		"inconsistent qualifiers",
		"inconsistent_qualifiers",
		run_test,
		&((struct test_data){
			false,
			"__pixel_local_inEXT mydata {\n"
			"	layout(rgba8) __pixel_local_outEXT vec4 pls_color1;\n"
			"	layout(rgba8) __pixel_localEXT vec4 pls_color2;\n"
			"} inp;\n"
			"out vec4 outColor;\n",
			"	outColor = inp.pls_color2;\n",
			{},
			0,
		}),
	},
	{
		"inconsistent formats",
		"inconsistent_formats",
		run_test,
		&((struct test_data){
			false,
			"__pixel_local_inEXT mydata {\n"
			"	layout(rgba8ui) vec4 pls_color1;\n"
			"	layout(rg16) vec4 pls_color2;\n"
			"} inp;\n"
			"out vec4 outColor;\n",
			"	outColor = inp.pls_color2;\n",
			{},
			0,
		}),
	},
	{
		"inconsistent formats 2",
		"inconsistent_formats_2",
		run_test,
		&((struct test_data){
			false,
			"layout(rgba8ui) __pixel_local_inEXT mydata {\n"
			"	vec4 pls_color1;\n"
			"	layout(rg16) vec4 pls_color2;\n"
			"} inp;\n"
			"out vec4 outColor;\n",
			"	outColor = inp.pls_color2;\n",
			{},
			0,
		}),
	},
	{
		"inconsistent formats 3",
		"inconsistent_formats_3",
		run_test,
		&((struct test_data){
			false,
			"layout(rgba8)__pixel_local_inEXT mydata {\n"
			"	vec4 pls_color1;\n"
			"	vec2 pls_color2;\n"
			"} inp;\n"
			"out vec4 outColor;\n",
			"	outColor = inp.pls_color1;\n",
			{},
			0,
		}),
	},
	{
		"override block layout",
		"override_block_layout",
		run_test,
		&((struct test_data){
			true,
			"layout(rgba8)__pixel_local_inEXT mydata {\n"
			"	vec4 pls_color1;\n"
			"	layout(rg16) vec2 pls_color2;\n"
			"} inp;\n"
			"out vec4 outColor;\n",
			"	outColor = inp.pls_color1;\n",
			{},
			0,
		}),
	},
	{
		"check for required features of max size and max fast size",
		"max_size",
		run_pls_size_fast_size_check,
		&((struct test_data){
			true,
			eq_pls_size,
			"	outColor = inp.pls_color1;\n",
			{},
			0,
		}),
	},
	{
		"block larger than MAX_SHADER_PIXEL_LOCAL_STORAGE_SIZE_EXT bytes",
		"block_too_large",
		run_test,
		&((struct test_data){
			false,
			gt_pls_size,
			"	outColor = inp.pls_color1;\n",
			{},
			0,
		}),
	},
	{
		"check for required features of using draw framebuffers",
		"req_feat_for_draw_fb",
		run_draw_buffers_check,
		&((struct test_data){
			false,
			"__pixel_localEXT mydata {\n"
			"	layout(rgba8) vec4 pls_color;\n"
			"} outp;\n",
			"	outp.pls_color = vec4(1, 2, 3, 4);\n",
			{},
			0,
		}),
	},
	{
		"enable pixel_local_storage when SAMPLE_BUFFERS == 1",
		"sample_bufs_one",
		run_multisample_test,
		&((struct test_data){NULL,}),
	},
	{
		"check if blending or dithering changes pixel_local_storage output",
		"blend_or_dither",
		run_blend_dither_test,
		&((struct test_data){NULL,}),
	},
	{
		"use initializers",
		"use_initializers",
		run_test,
		&((struct test_data){
			false,
			"__pixel_localEXT mydata {\n"
			"	layout(rgba8) vec4 pls_color = vec4(1, 2, 3, 4);\n"
			"} inp;\n",
			"	inp.pls_color = vec4(1, 2, 3, 4);\n",
			{},
			0,
		}),
	},
	{
		"write to and read from r11f_g11f_b10f layout qualifier",
		"test_r11f_g11f_b10f",
		run_check_data,
		&((struct test_data){
			true,
			"",
			"",
			{PLS_R11F_G11F_B10F,},
			1,
		}),
	},
	{
		"write to and read from r32f layout qualifier",
		"test_r32f",
		run_check_data,
		&((struct test_data){
			true,
			"",
			"",
			{PLS_R32F,},
			1,
		}),
	},
	{
		"write to and read from rg16f layout qualifier",
		"test_rg16f",
		run_check_data,
		&((struct test_data){
			true,
			"",
			"",
			{PLS_RG16F,},
			1,
		}),
	},
	{
		"write to and read from rgb10_a2 layout qualifier",
		"test_rgb10_a2",
		run_check_data,
		&((struct test_data){
			true,
			"",
			"",
			{PLS_RGB10_A2,},
			1,
		}),
	},
	{
		"write to and read from rgba8 layout qualifier",
		"test_rgba8",
		run_check_data,
		&((struct test_data){
			true,
			"",
			"",
			{PLS_RGBA8,},
			1,
		}),
	},
	{
		"write to and read from float layout qualifiers",
		"test_float_qualifiers",
		run_check_data,
		&((struct test_data){
			true,
			"",
			"",
			{
				PLS_R11F_G11F_B10F,
				PLS_R32F,
				PLS_RG16F,
				PLS_RGB10_A2,
				PLS_RGBA8,
			},
			5,
		}),
	},
	{
		"write to and read from rgba8i layout qualifier",
		"test_rgba8i",
		run_check_data,
		&((struct test_data){
			true,
			"",
			"",
			{PLS_RGBA8I,},
			1,
		}),
	},
	{
		"write to and read from rg16i layout qualifier",
		"test_rg16i",
		run_check_data,
		&((struct test_data){
			true,
			"",
			"",
			{PLS_RG16I,},
			1,
		}),
	},
	{
		"write to and read from int layout qualifiers",
		"test_int_qualifiers",
		run_check_data,
		&((struct test_data){
			true,
			"",
			"",
			{
				PLS_RGBA8I,
				PLS_RG16I,
			},
			2,
		}),
	},
	{
		"write to and read from rgb10_a2ui layout qualifier",
		"test_rgb10_a2ui",
		run_check_data,
		&((struct test_data){
			true,
			"",
			"",
			{PLS_RGB10_A2UI,},
			1,
		}),
	},
	{
		"write to and read from rgba8ui layout qualifier",
		"test_rgba8ui",
		run_check_data,
		&((struct test_data){
			true,
			"",
			"",
			{PLS_RGBA8UI,},
			1,
		}),
	},
	{
		"write to and read from rg16ui layout qualifier",
		"test_rg16ui",
		run_check_data,
		&((struct test_data){
			true,
			"",
			"",
			{PLS_RG16UI,},
			1,
		}),
	},
	{
		"write to and read from r32ui layout qualifier",
		"test_r32ui",
		run_check_data,
		&((struct test_data){
			true,
			"",
			"",
			{PLS_R32UI,},
			1,
		}),
	},
	{
		"write to and read from uint layout qualifiers",
		"test_uint_qualifiers",
		run_check_data,
		&((struct test_data){
			true,
			"",
			"",
			{
				PLS_RGB10_A2UI,
				PLS_RGBA8UI,
				PLS_RG16UI,
				PLS_R32UI,
			},
			4,
		}),
	},
	{
		"write to and read from each of the supported layout qualifiers",
		"test_layout_qualifiers",
		run_check_data,
		&((struct test_data){
			true,
			"",
			"",
			{},
			PLS_FORMAT_COUNT,
		}),
	},
	{
		"read from color buffer after clearing",
		"read_after_clear",
		run_check_clear,
		&((struct test_data){NULL,}),
	},
	{ /* sentinel */ }
};

void
piglit_init(int argc, char **argv)
{
	piglit_require_extension("GL_EXT_shader_pixel_local_storage");

	GLint max_pls_size;
	glGetIntegerv(GL_MAX_SHADER_PIXEL_LOCAL_STORAGE_SIZE_EXT,
		      &max_pls_size);

	make_vars_str(eq_pls_size, "rgba8", "vec4", "inp", "outColor", "vec4",
		      PIXEL_LOCAL_IN, max_pls_size / 4);

	make_vars_str(gt_pls_size, "rgba8", "vec4", "inp", "outColor", "vec4",
		      PIXEL_LOCAL_IN, (max_pls_size / 4) + 1);

	enum piglit_result result;

	result = piglit_run_selected_subtests(subtests,
					      piglit_config->selected_subtests,
					      piglit_config->num_selected_subtests,
					      PIGLIT_SKIP);

	piglit_report_result(result);
}

enum piglit_result
piglit_display(void)
{
	return PIGLIT_FAIL;
}
