/* Copyright © 2024 Advanced Micro Devices, Inc.
 *
 * SPDX-License-Identifier: MIT
 */

#include <inttypes.h>
#include "image_common.h"

/**
 * @file tex-modifier.c
 *
 * Verfify texture modifier exported by eglExportDMABUFImageQueryMESA()
 * are in the supported modifier list queried by eglQueryDmaBufModifiersEXT().
 *
 * Driver may use different modifier for different texture format and size. This
 * test make sure we don't miss any of them in the supported modifier list.
 */

PIGLIT_GL_TEST_CONFIG_BEGIN

config.supports_gl_es_version = 20;

PIGLIT_GL_TEST_CONFIG_END

/* Set to be true if we expect eglExportDMABUFImageQueryMESA() should not return
 * invalid modifiers.
 */
static bool force_valid_modifier = false;
static bool logged_invalid_modifier = false;

static GLint test_formats[] = {
	GL_RGB,
	GL_RGBA,
};

static unsigned min_texture_size = 16;
static unsigned max_texture_size = 1024;

PFNEGLEXPORTDMABUFIMAGEQUERYMESAPROC dmabuf_query = NULL;
PFNEGLQUERYDMABUFFORMATSEXTPROC dmabuf_query_formats = NULL;
PFNEGLQUERYDMABUFMODIFIERSEXTPROC dmabuf_query_modifiers = NULL;

enum piglit_result
piglit_display(void)
{
	return PIGLIT_PASS;
}

bool
test(EGLDisplay egl_dpy, GLint format, unsigned w, unsigned h)
{
	bool ret = false;

	GLuint tex;
	glGenTextures(1, &tex);
	glBindTexture(GL_TEXTURE_2D, tex);
	glTexImage2D(GL_TEXTURE_2D, 0, format, w, h, 0, format, GL_UNSIGNED_BYTE, NULL);

	EGLImageKHR img =
		eglCreateImageKHR(egl_dpy, eglGetCurrentContext(),
				  EGL_GL_TEXTURE_2D_KHR,
				  (EGLClientBuffer)(uintptr_t)tex,
				  NULL);
	if (img == EGL_NO_IMAGE) {
		fprintf(stderr, "create egl image fail\n");
		goto out0;
	}

	int fourcc = 0;
	int num_planes = 0;
	EGLuint64KHR modifiers[3] = {0};
	if (!dmabuf_query(egl_dpy, img, &fourcc, &num_planes, modifiers)) {
		fprintf(stderr, "query modifier fail\n");
		goto out1;
	}

	if (modifiers[0] == DRM_FORMAT_MOD_INVALID) {
		if (!logged_invalid_modifier) {
			fprintf(stderr, "invalid modifier\n");
			logged_invalid_modifier = true;
		}
		ret = !force_valid_modifier;
		goto out1;
	}

#define MAX_MODS 256
	EGLuint64KHR supported_mods[MAX_MODS];
	EGLBoolean external_only[MAX_MODS];
	int num_modifiers = 0;
	if (!dmabuf_query_modifiers(egl_dpy, fourcc, MAX_MODS, supported_mods,
				    external_only, &num_modifiers)) {
		fprintf(stderr, "query supported modifier fail\n");
		goto out1;
	}

	for (unsigned i = 0; i < num_modifiers; i++) {
		if (modifiers[0] == supported_mods[i]) {
			ret = true;
			break;
		}
	}

	if (!ret)
		fprintf(stderr, "modifier %" PRIx64 " is not supported\n", modifiers[0]);

 out1:
	eglDestroyImage(egl_dpy, img);
 out0:
	glDeleteTextures(1, &tex);
	return ret;
}

void
piglit_init(int argc, char **argv)
{
	EGLDisplay egl_dpy = eglGetCurrentDisplay();

	piglit_require_egl_extension(egl_dpy, "EGL_MESA_image_dma_buf_export");
	piglit_require_egl_extension(egl_dpy, "EGL_EXT_image_dma_buf_import_modifiers");
	piglit_require_egl_extension(egl_dpy, "EGL_KHR_gl_texture_2D_image");

	dmabuf_query =
		(PFNEGLEXPORTDMABUFIMAGEQUERYMESAPROC) eglGetProcAddress(
			"eglExportDMABUFImageQueryMESA");

	dmabuf_query_formats =
		(PFNEGLQUERYDMABUFFORMATSEXTPROC) eglGetProcAddress(
			"eglQueryDmaBufFormatsEXT");

	dmabuf_query_modifiers =
		(PFNEGLQUERYDMABUFMODIFIERSEXTPROC) eglGetProcAddress(
			"eglQueryDmaBufModifiersEXT");

	if (!dmabuf_query || !dmabuf_query_formats || !dmabuf_query_modifiers) {
		fprintf(stderr, "could not find extension entrypoints\n");
		piglit_report_result(PIGLIT_FAIL);
	}

	for (unsigned i = 0; i < argc; i++) {
		if (!strcmp(argv[i], "-force-valid-modifier")) {
			force_valid_modifier = true;
			continue;
		}
	}

	/* limit the max texture size */
	GLint max_size = 0;
	glGetIntegerv(GL_MAX_TEXTURE_SIZE, &max_size);
	max_texture_size = MIN2(max_size, max_texture_size);

	EGLint num_formats = 0;
	dmabuf_query_formats(egl_dpy, 0, NULL, &num_formats);
	if (!num_formats) {
		fprintf(stderr, "no supported modifier list\n");
		piglit_report_result(PIGLIT_SKIP);
	}

	for (unsigned i = 0; i < sizeof(test_formats) / sizeof(test_formats[0]); i++) {
		for (unsigned w = min_texture_size; w <= max_texture_size; w <<= 1) {
			for (unsigned h = min_texture_size; h <= max_texture_size; h <<= 1) {
				if (!test(egl_dpy, test_formats[i], w, h)) {
					fprintf(stderr, "test fail for format %x width %u height %u\n",
						test_formats[i], w, h);
					piglit_report_result(PIGLIT_FAIL);
				}
			}
		}
	}

	piglit_report_result(PIGLIT_PASS);
}
