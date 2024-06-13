/*
 * Copyright © 2024 Collabora Ltd.
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
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#include "piglit-util-gl.h"

PIGLIT_GL_TEST_CONFIG_BEGIN

	config.supports_gl_es_version = 30;

	config.window_visual = PIGLIT_GL_VISUAL_RGBA | PIGLIT_GL_VISUAL_DOUBLE;

	config.khr_no_error_support = PIGLIT_HAS_ERRORS;

PIGLIT_GL_TEST_CONFIG_END

static int
enum_to_rate(GLint value)
{
	switch (value) {
	case GL_SURFACE_COMPRESSION_FIXED_RATE_NONE_EXT:	return 0;
	case GL_SURFACE_COMPRESSION_FIXED_RATE_1BPC_EXT:	return 1;
	case GL_SURFACE_COMPRESSION_FIXED_RATE_2BPC_EXT:	return 2;
	case GL_SURFACE_COMPRESSION_FIXED_RATE_3BPC_EXT:	return 3;
	case GL_SURFACE_COMPRESSION_FIXED_RATE_4BPC_EXT:	return 4;
	case GL_SURFACE_COMPRESSION_FIXED_RATE_5BPC_EXT:	return 5;
	case GL_SURFACE_COMPRESSION_FIXED_RATE_6BPC_EXT:	return 6;
	case GL_SURFACE_COMPRESSION_FIXED_RATE_7BPC_EXT:	return 7;
	case GL_SURFACE_COMPRESSION_FIXED_RATE_8BPC_EXT:	return 8;
	case GL_SURFACE_COMPRESSION_FIXED_RATE_9BPC_EXT:	return 9;
	case GL_SURFACE_COMPRESSION_FIXED_RATE_10BPC_EXT:	return 10;
	case GL_SURFACE_COMPRESSION_FIXED_RATE_11BPC_EXT:	return 11;
	case GL_SURFACE_COMPRESSION_FIXED_RATE_12BPC_EXT:	return 12;
	default:						return 0;
	}
}

enum piglit_result
piglit_display(void)
{
	return PIGLIT_FAIL;
}

static void
check_zero_texture(void)
{
	/* attempting to call TexStorageAttribs*EXT on the zero texture
	 * must fail with INVALID_OPERATION
	 */
	const GLint attribs[] = { GL_NONE };

	glBindTexture(GL_TEXTURE_2D, 0);
	glTexStorageAttribs2DEXT(GL_TEXTURE_2D, 1, GL_RGBA8, 64, 64, attribs);

	piglit_report_subtest_result(
		piglit_check_gl_error(GL_INVALID_OPERATION) ? PIGLIT_PASS : PIGLIT_FAIL,
		"zero-texture");
}

static void
check_unsized_format(void)
{
	/* attempting to call TexStorageAttribs*EXT with an unsized internalformat
	 * must fail with INVALID_ENUM
	 */

	const GLint attribs[] = { GL_NONE };
	GLuint tex;
	glGenTextures(1, &tex);
	glBindTexture(GL_TEXTURE_2D, tex);

	glTexStorageAttribs2DEXT(GL_TEXTURE_2D, 1, GL_RGBA, 64, 64, attribs);

	/* unsized formats may not be used with TexStorage* */
	piglit_report_subtest_result(
		piglit_check_gl_error(GL_INVALID_ENUM) ? PIGLIT_PASS : PIGLIT_FAIL,
		"unsized-format");
}

static void
check_immutable(void)
{
	GLuint tex;
	GLint param;
	const GLint attribs[] = { GL_NONE };

	glGenTextures(1, &tex);
	glBindTexture(GL_TEXTURE_2D, tex);

	/* specify storage for the texture, it will be set as immutable */
	glTexStorageAttribs2DEXT(GL_TEXTURE_2D, 1, GL_RGBA8, 64, 64, attribs);

	if (!piglit_check_gl_error(GL_NO_ERROR)) {
		piglit_report_subtest_result(PIGLIT_FAIL, "immutable");
		return;
	}

	/* should now have TEXTURE_IMMUTABLE_FORMAT */
	glGetTexParameteriv(GL_TEXTURE_2D,
			    GL_TEXTURE_IMMUTABLE_FORMAT, &param);

	if (!piglit_check_gl_error(GL_NO_ERROR)) {
		printf("failed to fetch texture parameter TEXTURE_IMMUTABLE_FORMAT\n");
		piglit_report_subtest_result(PIGLIT_FAIL, "immutable");
		return;
	}

	if (param != GL_TRUE) {
		printf("expected TEXTURE_IMMUTABLE_FORMAT to be true, got %d\n", param);
		piglit_report_subtest_result(PIGLIT_FAIL, "immutable");
		return;
	}

	/* calling TexStorageAttribs2D again on the same texture should fail */
	glTexStorageAttribs2DEXT(GL_TEXTURE_2D, 1, GL_RGBA8, 64, 64, attribs);

	if (!piglit_check_gl_error(GL_INVALID_OPERATION)) {
		printf("expected respecifying an immutable-format texture (with TexStorageAttribs*) to fail\n");
		piglit_report_subtest_result(PIGLIT_FAIL, "immutable");
		return;
	}

	piglit_report_subtest_result(PIGLIT_PASS, "immutable");
}

static void
check_compression(void)
{
	GLuint tex, fb;
	GLint rates[16], num_rates = 0, actual_rate;
	GLint attribs[3] = {GL_SURFACE_COMPRESSION_EXT, GL_SURFACE_COMPRESSION_FIXED_RATE_DEFAULT_EXT,
	                    GL_NONE};

	glGenTextures(1, &tex);
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, tex);

	glGetInternalformativ(GL_RENDERBUFFER, GL_RGBA8,
			      GL_NUM_SURFACE_COMPRESSION_FIXED_RATES_EXT, 1, &num_rates);
	glGetInternalformativ(GL_RENDERBUFFER, GL_RGBA8,
			      GL_SURFACE_COMPRESSION_EXT, num_rates, rates);

	/* Test default and none rates as well */
	rates[num_rates++] = GL_SURFACE_COMPRESSION_FIXED_RATE_DEFAULT_EXT;
	rates[num_rates++] = GL_SURFACE_COMPRESSION_FIXED_RATE_NONE_EXT;

	for (GLint i = 0; i < num_rates; i++) {
		glGenTextures(1, &tex);
		glBindTexture(GL_TEXTURE_2D, tex);

		/* specify storage for the texture, it will be set as immutable */
		attribs[1] = rates[i];
		glTexStorageAttribs2DEXT(GL_TEXTURE_2D, 1, GL_RGBA8, 64, 64, attribs);

		if (!piglit_check_gl_error(GL_NO_ERROR)) {
			piglit_report_subtest_result(PIGLIT_FAIL, "compression");
			return;
		}

		/* should now have TEXTURE_IMMUTABLE_FORMAT */
		glGetTexParameteriv(GL_TEXTURE_2D,
				    GL_SURFACE_COMPRESSION_EXT, &actual_rate);
		if (rates[i] == GL_SURFACE_COMPRESSION_FIXED_RATE_DEFAULT_EXT) {
			piglit_logd("actual default rate is %i bpc", enum_to_rate(actual_rate));
		} else if (rates[i] != actual_rate) {
			piglit_loge("actual rate (%i bpc) differs from expected rate (%i bpc)",
				    enum_to_rate(actual_rate), enum_to_rate(rates[i]));
			piglit_report_subtest_result(PIGLIT_FAIL, "compression");
			return;
		}

		GLubyte *data = piglit_rgbw_image_ubyte(64, 64, GL_FALSE);
		glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 64, 64, GL_RGBA, GL_UNSIGNED_BYTE, data);
		free(data);

		if (!piglit_check_gl_error(GL_NO_ERROR)) {
			piglit_loge("failed to upload texture data");
			piglit_report_subtest_result(PIGLIT_FAIL, "compression");
			return;
		}

		glGenFramebuffers(1, &fb);
		glBindFramebuffer(GL_FRAMEBUFFER, fb);
		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
		glClearColor(1.0, 1.0, 0.0, 1.0);
		glEnable(GL_SCISSOR_TEST);
		glScissor(0, 0, 32, 32);
		glClear(GL_COLOR_BUFFER_BIT);
		glDisable(GL_SCISSOR_TEST);

		const float yellow[] = {1.0, 1.0, 0.0, 1.0};
		const float green[] = {0.0, 1.0, 0.0, 1.0};
		const float white[] = {1.0, 1.0, 1.0, 1.0};
		glReadBuffer(GL_COLOR_ATTACHMENT0);
		if (!piglit_probe_pixel_rgba(16, 16, yellow) || !piglit_probe_pixel_rgba(48, 48, white) ||
		    !piglit_probe_pixel_rgba(48, 16, green)) {
		        piglit_loge("pixels are not acturate");
		        piglit_report_subtest_result(PIGLIT_FAIL, "compression");
		        return;
		}

		glBindFramebuffer(GL_FRAMEBUFFER, 0);
		glReadBuffer(GL_BACK);

		if (!piglit_check_gl_error(GL_NO_ERROR)) {
			piglit_report_subtest_result(PIGLIT_FAIL, "compression");
			return;
		}
	}

	piglit_report_subtest_result(PIGLIT_PASS, "compression");
}


void
piglit_init(int argc, char **argv)
{
	piglit_require_extension("GL_EXT_texture_storage_compression");

	check_zero_texture();
	check_immutable();
	check_unsized_format();
	check_compression();

	piglit_report_result(PIGLIT_SKIP);
}
