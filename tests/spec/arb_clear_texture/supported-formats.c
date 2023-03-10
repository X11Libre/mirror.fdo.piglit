/*
 * Copyright (c) 2023 Intel Corporation
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
 * @file supported-formats.c
 *
 * Test using GL_CLEAR_COLOR with a range of formats.
 */

#include "common.h"

PIGLIT_GL_TEST_CONFIG_BEGIN

	config.supports_gl_compat_version = 10;
	config.window_visual = PIGLIT_GL_VISUAL_RGB;
	config.khr_no_error_support = PIGLIT_NO_ERRORS;

PIGLIT_GL_TEST_CONFIG_END

static const GLenum valid_targets[] = {
	GL_TEXTURE_1D,
	GL_TEXTURE_1D_ARRAY,
	GL_TEXTURE_2D,
	GL_TEXTURE_2D_ARRAY,
	GL_TEXTURE_2D_MULTISAMPLE,
	GL_TEXTURE_2D_MULTISAMPLE_ARRAY,
	GL_TEXTURE_3D,
	GL_TEXTURE_CUBE_MAP,
	GL_TEXTURE_CUBE_MAP_ARRAY,
	GL_TEXTURE_RECTANGLE,
};

static const GLenum invalid_targets[] = {
	GL_RENDERBUFFER,
	GL_TEXTURE_BUFFER,
	GL_FRAMEBUFFER,
	GL_COLOR_ATTACHMENT0,
	GL_COLOR_ATTACHMENT1,
	GL_COLOR_ATTACHMENT2,
	GL_COLOR_ATTACHMENT3,
	GL_COLOR_ATTACHMENT4,
	GL_COLOR_ATTACHMENT5,
	GL_COLOR_ATTACHMENT6,
	GL_COLOR_ATTACHMENT7,
	GL_COLOR_ATTACHMENT8,
	GL_COLOR_ATTACHMENT9,
	GL_COLOR_ATTACHMENT10,
	GL_COLOR_ATTACHMENT11,
	GL_COLOR_ATTACHMENT12,
	GL_COLOR_ATTACHMENT13,
	GL_COLOR_ATTACHMENT14,
	GL_COLOR_ATTACHMENT15,
	GL_DEPTH_ATTACHMENT,
	GL_STENCIL_ATTACHMENT,
	GL_TEXTURE_4D_SGIS,
	GL_TEXTURE_RENDERBUFFER_NV,
};

static const GLenum valid_internal_formats[] = {
	/* Base/unsized internal format (from Table 8.11) */
	GL_DEPTH_COMPONENT,
	GL_DEPTH_STENCIL,
	GL_RED,
	GL_RG,
	GL_RGB,
	GL_RGBA,
	GL_STENCIL_INDEX,
	/* Table 8.12 */
	GL_R8,
	GL_R8_SNORM,
	GL_R16,
	GL_R16_SNORM,
	GL_RG8,
	GL_RG8_SNORM,
	GL_RG16,
	GL_RG16_SNORM,
	GL_R3_G3_B2,
	GL_RGB4,
	GL_RGB5,
	GL_RGB8,
	GL_RGB8_SNORM,
	GL_RGB10,
	GL_RGB12,
	GL_RGB16,
	GL_RGB16_SNORM,
	GL_RGBA2,
	GL_RGBA4,
	GL_RGB5_A1,
	GL_RGBA8,
	GL_RGBA8_SNORM,
	GL_RGB10_A2,
	GL_RGB10_A2UI,
	GL_RGBA12,
	GL_RGBA16,
	GL_RGBA16_SNORM,
	GL_SRGB8,
	GL_SRGB8_ALPHA8,
	GL_R16F,
	GL_RG16F,
	GL_RGB16F,
	GL_RGBA16F,
	GL_R32F,
	GL_RG32F,
	GL_RGB32F,
	GL_RGBA32F,
	GL_R11F_G11F_B10F,
	GL_RGB9_E5,
	GL_R8I,
	GL_R8UI,
	GL_R16I,
	GL_R16UI,
	GL_R32I,
	GL_R32UI,
	GL_RG8I,
	GL_RG16I,
	GL_RG16UI,
	GL_RG32I,
	GL_RG32UI,
	GL_RGB8I,
	GL_RGB8UI,
	GL_RGB16I,
	GL_RGB16UI,
	GL_RGB32I,
	GL_RGB32UI,
	GL_RGBA8I,
	GL_RGBA8UI,
	GL_RGBA16I,
	GL_RGBA16UI,
	GL_RGBA32I,
	GL_RGBA32UI,
	/* Table 8.13 */
	GL_DEPTH_COMPONENT16,
	GL_DEPTH_COMPONENT24,
	GL_DEPTH_COMPONENT32,
	GL_DEPTH_COMPONENT32F,
	GL_DEPTH24_STENCIL8,
	GL_DEPTH32F_STENCIL8,
	GL_STENCIL_INDEX1,
	GL_STENCIL_INDEX4,
	GL_STENCIL_INDEX8,
	GL_STENCIL_INDEX16
};

static const GLenum invalid_internal_formats[] = {
	/* Table 8.14 */
	GL_COMPRESSED_RED,
	GL_COMPRESSED_RG,
	GL_COMPRESSED_RGB,
	GL_COMPRESSED_RGBA,
	GL_COMPRESSED_SRGB,
	GL_COMPRESSED_SRGB_ALPHA,
	GL_COMPRESSED_RED_RGTC1,
	GL_COMPRESSED_SIGNED_RED_RGTC1,
	GL_COMPRESSED_RG_RGTC2,
	GL_COMPRESSED_SIGNED_RG_RGTC2,
	GL_COMPRESSED_RGBA_BPTC_UNORM,
	GL_COMPRESSED_SRGB_ALPHA_BPTC_UNORM,
	GL_COMPRESSED_RGB_BPTC_SIGNED_FLOAT,
	GL_COMPRESSED_RGB_BPTC_UNSIGNED_FLOAT,
	GL_COMPRESSED_RGB8_ETC2,
	GL_COMPRESSED_SRGB8_ETC2,
	GL_COMPRESSED_RGB8_PUNCHTHROUGH_ALPHA1_ETC2,
	GL_COMPRESSED_SRGB8_PUNCHTHROUGH_ALPHA1_ETC2,
	GL_COMPRESSED_RGBA8_ETC2_EAC,
	GL_COMPRESSED_SRGB8_ALPHA8_ETC2_EAC,
	GL_COMPRESSED_R11_EAC,
	GL_COMPRESSED_SIGNED_R11_EAC,
	GL_COMPRESSED_RG11_EAC,
	GL_COMPRESSED_SIGNED_RG11_EAC
};

enum piglit_result
piglit_display(void)
{
	return PIGLIT_FAIL;
}

static bool
try(const GLenum *targets, unsigned num_targets,
    const GLenum *pinternal_formats, unsigned num_formats,
    GLenum expected_result)
{
	bool pass = true;

	for (unsigned i = 0; i < num_targets; i++) {
		for (unsigned j = 0; j < num_formats; j++) {
			GLint param;
			GLint64 param64;

			glGetInternalformativ(targets[i],
					      pinternal_formats[j],
					      GL_CLEAR_TEXTURE,
					      1,
					      &param);

			glGetInternalformati64v(targets[i],
						pinternal_formats[j],
						GL_CLEAR_TEXTURE,
						1,
						&param64);

			bool test = (expected_result == param);
			bool test64 = (expected_result == param64);

			if (test && test64)
				continue;

			fprintf(stderr,
				"    Failing case was "
				"Result: %s NOT %s\n"
				"target = %s, internal format = %s. \n",
				piglit_get_gl_enum_name(param),
				piglit_get_gl_enum_name(expected_result),
				piglit_get_gl_enum_name(targets[i]),
				piglit_get_gl_enum_name(pinternal_formats[j]));

			if (!test)
				fprintf(stderr,
					"    Calling glGetInternalformativ\n");

			if (!test64)
				fprintf(stderr,
					"    Calling glGetInternalformati64v\n");

			pass = false;
		}
	}

	return pass;
}

static bool
check_supported_formats()
{
	bool pass = true;

	pass = try(valid_targets, ARRAY_SIZE(valid_targets), valid_internal_formats,
		   ARRAY_SIZE(valid_internal_formats), GL_FULL_SUPPORT)
		&& pass;
	pass = try(invalid_targets, ARRAY_SIZE(invalid_targets),
		   valid_internal_formats, ARRAY_SIZE(valid_internal_formats), GL_NONE)
		&& pass;
	pass = try(valid_targets, ARRAY_SIZE(valid_targets),
		   invalid_internal_formats, ARRAY_SIZE(invalid_internal_formats), GL_NONE)
		&& pass;

	return pass;
}

void
piglit_init(int argc, char **argv)
{
	piglit_require_extension("GL_ARB_clear_texture");
	piglit_require_extension("GL_ARB_internalformat_query2");

	piglit_report_result(check_supported_formats() ?
			     PIGLIT_PASS :
			     PIGLIT_FAIL);
}