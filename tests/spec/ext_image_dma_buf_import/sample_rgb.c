/*
 * Copyright © 2013 Intel Corporation
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

#include "sample_common.h"
#include "image_common.h"

/**
 * @file sample_rgb.c
 *
 * Create EGL images out of RGB formatted dma buffers, set them as external
 * textures, set texture filters for avoiding need for other mipmap-levels and
 * sample the textures using a shader program.
 */

PIGLIT_GL_TEST_CONFIG_BEGIN

	config.supports_gl_es_version = 20;
	config.window_visual = PIGLIT_GL_VISUAL_RGBA;

PIGLIT_GL_TEST_CONFIG_END

static bool force_alpha_to_one = false;

/* Where a channel is in the pixel, taken as a little-endian word. */
struct channel {
	unsigned shift, size;
};

static const struct format {
	uint32_t fourcc;
	unsigned cpp;
	bool is_float;
	struct channel chan[4]; /* R, G, B, A */
} formats[] = {
	{ DRM_FORMAT_R8,            1, false, { {  0,  8 } } },
	{ DRM_FORMAT_GR88,          2, false, { {  0,  8 }, {  8,  8 } } },
	{ DRM_FORMAT_R16,           2, false, { {  0, 16 } } },
	{ DRM_FORMAT_GR1616,        4, false, { {  0, 16 }, { 16, 16 } } },
	{ DRM_FORMAT_RGB565,        2, false, { { 11,  5 }, {  5,  6 }, {  0,  5 } } },
	{ DRM_FORMAT_ARGB1555,      2, false, { { 10,  5 }, {  5,  5 }, {  0,  5 }, { 15,  1 } } },
	{ DRM_FORMAT_ABGR1555,      2, false, { {  0,  5 }, {  5,  5 }, { 10,  5 }, { 15,  1 } } },
	{ DRM_FORMAT_ARGB4444,      2, false, { {  8,  4 }, {  4,  4 }, {  0,  4 }, { 12,  4 } } },
	{ DRM_FORMAT_ABGR4444,      2, false, { {  0,  4 }, {  4,  4 }, {  8,  4 }, { 12,  4 } } },
	{ DRM_FORMAT_XRGB8888,      4, false, { { 16,  8 }, {  8,  8 }, {  0,  8 } } },
	{ DRM_FORMAT_ARGB8888,      4, false, { { 16,  8 }, {  8,  8 }, {  0,  8 }, { 24,  8 } } },
	{ DRM_FORMAT_XBGR8888,      4, false, { {  0,  8 }, {  8,  8 }, { 16,  8 } } },
	{ DRM_FORMAT_ABGR8888,      4, false, { {  0,  8 }, {  8,  8 }, { 16,  8 }, { 24,  8 } } },
	{ DRM_FORMAT_XRGB2101010,   4, false, { { 20, 10 }, { 10, 10 }, {  0, 10 } } },
	{ DRM_FORMAT_ARGB2101010,   4, false, { { 20, 10 }, { 10, 10 }, {  0, 10 }, { 30,  2 } } },
	{ DRM_FORMAT_XBGR2101010,   4, false, { {  0, 10 }, { 10, 10 }, { 20, 10 } } },
	{ DRM_FORMAT_ABGR2101010,   4, false, { {  0, 10 }, { 10, 10 }, { 20, 10 }, { 30,  2 } } },
	{ DRM_FORMAT_XBGR16161616,  8, false, { {  0, 16 }, { 16, 16 }, { 32, 16 } } },
	{ DRM_FORMAT_ABGR16161616,  8, false, { {  0, 16 }, { 16, 16 }, { 32, 16 }, { 48, 16 } } },
	{ DRM_FORMAT_XBGR16161616F, 8, true,  { {  0, 16 }, { 16, 16 }, { 32, 16 } } },
	{ DRM_FORMAT_ABGR16161616F, 8, true,  { {  0, 16 }, { 16, 16 }, { 32, 16 }, { 48, 16 } } },
};

static const struct format *format;

enum piglit_result
piglit_display(void)
{
	/* Multiples of 0.25, so that half_float[] can encode them. */
	static const float colors[4][4] = {
		{ 1.00, 0.00, 0.50, 1.00 },
		{ 0.00, 1.00, 0.25, 0.00 },
		{ 0.25, 0.50, 1.00, 0.50 },
		{ 0.75, 0.25, 0.00, 1.00 },
	};
	static const uint16_t half_float[] = {
		0x0000, 0x3400, 0x3800, 0x3a00, 0x3c00
	};
	unsigned char src[4 * 8], expected[4 * 4];
	enum piglit_result res;

	for (unsigned p = 0; p < 4; p++) {
		uint64_t pixel = 0;

		for (unsigned c = 0; c < 4; c++) {
			const struct channel *chan = &format->chan[c];
			float value = colors[p][c];

			if (!chan->size) {
				value = c == 3 ? 1.0 : 0.0;
			} else if (format->is_float) {
				pixel |= (uint64_t)half_float[(int)(value * 4)]
					 << chan->shift;
			} else {
				const unsigned max = (1u << chan->size) - 1;
				const unsigned bits = lroundf(value * max);

				pixel |= (uint64_t)bits << chan->shift;
				value = (float)bits / max;
			}

			if (c == 3 && force_alpha_to_one)
				value = 1.0;

			expected[p * 4 + c] = lroundf(value * 255);
		}

		for (unsigned i = 0; i < format->cpp; i++)
			src[p * format->cpp + i] = pixel >> (8 * i);
	}

	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	res = dma_buf_create_and_sample_32bpp(2, 2, format->fourcc, src);
	if (res != PIGLIT_PASS)
		return res;

	if (!piglit_probe_image_ubyte(0, 0, 2, 2, GL_RGBA, expected))
		res = PIGLIT_FAIL;

	piglit_present_results();

	return res;
}

static const struct format *
parse_format(const char *s)
{
	if (strlen(s) != 4)
		return NULL;

	for (unsigned i = 0; i < ARRAY_SIZE(formats); i++) {
		if (formats[i].fourcc == fourcc_code(s[0], s[1], s[2], s[3]))
			return &formats[i];
	}

	return NULL;
}

void
piglit_init(int argc, char **argv)
{
	unsigned i;
	EGLDisplay egl_dpy = eglGetCurrentDisplay();

	piglit_require_egl_extension(egl_dpy, "EGL_EXT_image_dma_buf_import");
	piglit_require_extension("GL_OES_EGL_image_external");

	for (i = 1; i < argc; i++) {
		static const char fmt[] = "-fmt=";

		if (!strcmp(argv[i], "-alpha-one")) {
			force_alpha_to_one = true;
			continue;
		}

		if (strncmp(argv[i], fmt, sizeof(fmt) - 1)) {
			fprintf(stderr, "unknown argument %s\n", argv[i]);
			continue;
		}

		format = parse_format(argv[i] + sizeof(fmt) - 1);
		if (!format) {
			fprintf(stderr, "invalid format: %s\n", argv[i]);
			usage(argv[0], "RGB");
			piglit_report_result(PIGLIT_SKIP);
		}
	}

	if (!format) {
		fprintf(stderr, "format not specified\n");
		usage(argv[0], "RGB");
		piglit_report_result(PIGLIT_SKIP);
	}
}
