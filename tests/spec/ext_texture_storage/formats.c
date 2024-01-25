/*
 * Copyright © 2024 Collabora, Ltd.
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
 * @file
 * Tests glTexStorage2DEXT interactions with formats defined in other
 * extensions, which is not covered by the CTS.
 */

#include "piglit-util-gl.h"

static struct piglit_gl_test_config *piglit_config;

PIGLIT_GL_TEST_CONFIG_BEGIN

	piglit_config = &config;
	config.supports_gl_es_version = 20;
	config.window_visual = PIGLIT_GL_VISUAL_RGBA;
	config.khr_no_error_support = PIGLIT_HAS_ERRORS;

PIGLIT_GL_TEST_CONFIG_END

struct format_test {
	const char *ext_names[2];
	const GLenum formats[5];
};

static enum piglit_result
check_formats(void *data_)
{
	const struct format_test *data = data_;
	bool failed = false;

	for (int i = 0; i < ARRAY_SIZE(data->ext_names); i++) {
		if (data->ext_names[i] &&
		    !piglit_is_extension_supported(data->ext_names[i])) {
			return PIGLIT_SKIP;
		}
	}

	piglit_reset_gl_error();

	for (int i = 0; i < ARRAY_SIZE(data->formats); i++) {
		GLuint tex;
		GLint param;

		if (data->formats[i] == GL_NONE)
			continue;

		piglit_logi("checking %s",
			    piglit_get_gl_enum_name(data->formats[i]));

		glActiveTexture(GL_TEXTURE0);
		glGenTextures(1, &tex);
		glBindTexture(GL_TEXTURE_2D, tex);
		glTexStorage2DEXT(GL_TEXTURE_2D, 1, data->formats[i], 128, 128);

		if (!piglit_check_gl_error(GL_NO_ERROR))
			failed = true;

		glGetTexParameteriv(GL_TEXTURE_2D,
				    GL_TEXTURE_IMMUTABLE_FORMAT_EXT,
				    &param);
		if (param != GL_TRUE)
			failed = true;

		glDeleteTextures(1, &tex);
	}

	return failed ? PIGLIT_FAIL : PIGLIT_PASS;
}

static const struct format_test format_tests[] = {
	{
		{ },
		{ GL_ALPHA8_EXT, GL_LUMINANCE8_EXT, GL_LUMINANCE8_ALPHA8_EXT, },
	},
	{
		{ "GL_OES_texture_float", },
		{
		  GL_RGBA32F_EXT, GL_RGB32F_EXT,
		  GL_ALPHA32F_EXT, GL_LUMINANCE32F_EXT,
		  GL_LUMINANCE_ALPHA32F_EXT,
		},
	},
	{
		{ "GL_OES_texture_half_float", },
		{
		  GL_RGBA16F_EXT, GL_RGB16F_EXT,
		  GL_ALPHA16F_EXT, GL_LUMINANCE16F_EXT,
		  GL_LUMINANCE_ALPHA16F_EXT,
		},
	},
	{
		{ "GL_EXT_texture_type_2_10_10_10_REV", },
		{ GL_RGB10_A2_EXT, GL_RGB10_EXT, },
	},
	{
		{ "GL_EXT_texture_format_BGRA8888", },
		{ GL_BGRA8_EXT, },
	},
	{
		{ "GL_EXT_texture_rg", },
		{ GL_R8_EXT, GL_RG8_EXT, },
	},
	{
		{ "GL_EXT_texture_rg", "GL_OES_texture_float", },
		{ GL_R32F_EXT, GL_RG32F_EXT, },
	},
	{
		{ "GL_EXT_texture_rg", "GL_OES_texture_half_float", },
		{ GL_R16F_EXT, GL_RG16F_EXT, },
	},
	{
		{ "GL_APPLE_rgb_422", },
		{ GL_RGB_RAW_422_APPLE, },
	},
};

enum piglit_result
piglit_display(void)
{
	return PIGLIT_FAIL;
}

void
piglit_init(int argc, char **argv)
{
	enum piglit_result result;
	struct piglit_subtest subtests[ARRAY_SIZE(format_tests) + 1];

	piglit_require_extension("GL_EXT_texture_storage");

	subtests[0].name = strdup("Check core extension");
	subtests[0].option = strdup("core");
	subtests[0].subtest_func = check_formats;
	subtests[0].data = (void *) &format_tests[0];

	for (int i = 1; i < ARRAY_SIZE(format_tests); i++) {
		const char *lead = "Check interactions with ";
		char *name, *opt;
		int ext_len = 0;

		for (int j = 0; j < ARRAY_SIZE(format_tests[i].ext_names); j++) {
			if (!format_tests[i].ext_names[j])
				continue;
			ext_len += strlen(format_tests[i].ext_names[j]) + 1;
		}

		name = calloc(strlen(lead) + ext_len + 1, 1);
		opt = calloc(ext_len + 1, 1);
		if (!name || !opt)
			return;

		strcat(name, lead);

		for (int j = 0; j < ARRAY_SIZE(format_tests[i].ext_names); j++) {
			if (!format_tests[i].ext_names[j])
				continue;

			if (j > 0) {
				strcat(name, " ");
				strcat(opt, "-");
			}
			strcat(name, format_tests[i].ext_names[j]);
			strcat(opt, format_tests[i].ext_names[j]);
		}

		subtests[i].name = name;
		subtests[i].option = opt;
		subtests[i].subtest_func = check_formats;
		subtests[i].data = (void *) &format_tests[i];
	}

	memset(&subtests[ARRAY_SIZE(format_tests)], 0, sizeof(*subtests));

	result = piglit_run_selected_subtests(subtests,
					      piglit_config->selected_subtests,
					      piglit_config->num_selected_subtests,
					      PIGLIT_SKIP);

	piglit_report_result(result);

	for (int i = 0; i < ARRAY_SIZE(subtests); i++) {
		free((char *) subtests[i].name);
		free((char *) subtests[i].option);
	}
}
