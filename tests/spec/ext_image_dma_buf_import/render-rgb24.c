/*
 * Copyright © 2025 Amazon.com, Inc. or its affiliates
 * Copyright © 2019-2021 Intel Corporation
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
 * @file render-rgb24.c
 *
 * Test verifies that we can successfully texture from and render to an
 * imported dmabuf for RGB888 and BGR888 formats.
 */

PIGLIT_GL_TEST_CONFIG_BEGIN

	config.supports_gl_es_version = 20;
	config.window_visual = PIGLIT_GL_VISUAL_RGB;

PIGLIT_GL_TEST_CONFIG_END

PFNEGLQUERYDMABUFFORMATSEXTPROC dmabuf_query_formats;
PFNEGLEXPORTDMABUFIMAGEQUERYMESAPROC dmabuf_query;
PFNEGLEXPORTDMABUFIMAGEMESAPROC dmabuf_export;

enum piglit_result
piglit_display(void)
{
	return PIGLIT_PASS;
}

static bool
skip_format(uint32_t format)
{
	switch (format) {
	case DRM_FORMAT_RGB888:
	case DRM_FORMAT_BGR888:
		return false;
	default:
		return true;
	}
}

static bool
eglImage_to_dma_buf(EGLDisplay egl_dpy, EGLImageKHR img,
		    int *fourcc, int *num_planes, EGLuint64KHR *modifiers,
		    int *fd, EGLint *stride, EGLint *offset)
{
	/* Query the image properties, verify fourcc and num planes. */
	if (!dmabuf_query(egl_dpy, img, fourcc, num_planes, modifiers))
		return false;

	if (!piglit_check_egl_error(EGL_SUCCESS))
		return false;

	if (*num_planes != 1) {
		fprintf(stderr, "Test only supports single plane\n");
		piglit_report_result(PIGLIT_SKIP);
	}

	/* Export the image, verify success. */
	if (!dmabuf_export(egl_dpy, img, fd, stride, offset))
		return false;

	if (!piglit_check_egl_error(EGL_SUCCESS))
		return false;

	/* Verify that we got a valid stride and offset for the fd. */
	if (*fd != -1 && (*stride < 1 || *offset < 0)) {
		fprintf(stderr, "invalid data from driver: "
			"fd %d stride %d offset %d\n",
			*fd, *stride, *offset);
		return false;
	}

	return true;
}

void
piglit_init(int argc, char **argv)
{
	EGLDisplay egl_dpy = eglGetCurrentDisplay();

	piglit_require_egl_extension(egl_dpy, "EGL_EXT_image_dma_buf_import");
	piglit_require_egl_extension(egl_dpy, "EGL_MESA_image_dma_buf_export");

	dmabuf_query =
		(PFNEGLEXPORTDMABUFIMAGEQUERYMESAPROC) eglGetProcAddress(
			"eglExportDMABUFImageQueryMESA");
	dmabuf_export =
		(PFNEGLEXPORTDMABUFIMAGEMESAPROC) eglGetProcAddress(
			"eglExportDMABUFImageMESA");

	if (!dmabuf_query || !dmabuf_export) {
		fprintf(stderr, "could not find extension entrypoints\n");
		piglit_report_result(PIGLIT_FAIL);
	}

	dmabuf_query_formats =
		(PFNEGLQUERYDMABUFFORMATSEXTPROC) eglGetProcAddress(
			"eglQueryDmaBufFormatsEXT");

	if (!dmabuf_query_formats) {
		fprintf(stderr, "could not find extension entrypoints\n");
		piglit_report_result(PIGLIT_FAIL);
	}

#define MAX_FORMATS 256

	/* First query all available formats. */
	EGLint formats[MAX_FORMATS];
	EGLint num_formats = 0;

	dmabuf_query_formats(egl_dpy, MAX_FORMATS, formats, &num_formats);

	enum piglit_result result = PIGLIT_SKIP;

	for (unsigned i = 0; i < num_formats; i++) {
		if (skip_format(formats[i]))
			continue;

		int fourcc = formats[i];

		/* Create piglit_dma_buf and EGLimage. */
		struct piglit_dma_buf *buf;

		const unsigned char src[] = {
			10, 20, 30, 40,
			50, 60, 70, 80,
			11, 22, 33, 44,
			55, 66, 77, 88 };

		result = piglit_create_dma_buf(2, 2, fourcc, src, &buf);
		if (result != PIGLIT_PASS)
			piglit_report_result(result);

		EGLImageKHR img;
		result = egl_image_for_dma_buf_fd(buf, buf->fd, fourcc, &img);
		if (result != PIGLIT_PASS)
			piglit_report_result(result);

		/* Export the buffer and query properties. */
		int prop_fourcc = -1;
		int num_planes = -1;
		EGLuint64KHR modifiers[64] = { -1, };
		int fd = -1;
		EGLint stride = -1;
		EGLint offset = -1;

		/* Export DMABUF from EGLImage */
		if (!eglImage_to_dma_buf(egl_dpy, img, &prop_fourcc, &num_planes,
					 modifiers, &fd, &stride, &offset)) {
			fprintf(stderr, "image export failed!\n");
			result = PIGLIT_FAIL;
			piglit_report_result(result);
		}

		if (prop_fourcc != fourcc) {
			fprintf(stderr,
				"fourcc mismatch, got %d expected %d\n",
				prop_fourcc, fourcc);
			result = PIGLIT_FAIL;
			piglit_report_result(result);
		}

		/* Draw EGLImage contents TODO: verify result */
		GLuint tex;
		texture_for_egl_image(img, &tex, true);
		sample_tex(tex, 0, 0, 1, piglit_height, true);

		piglit_destroy_dma_buf(buf);
	}

	piglit_report_result(result);
}
