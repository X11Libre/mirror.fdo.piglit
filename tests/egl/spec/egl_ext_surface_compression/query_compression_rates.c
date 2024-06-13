/*
 * Copyright 2024 Collabora Ltd
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

#include "common.h"
#include "piglit-util.h"
#include "piglit-util-egl.h"

int
main(int argc, char *argv[])
{
	EGLDisplay dpy;
	PFNEGLQUERYSUPPORTEDCOMPRESSIONRATESEXTPROC peglQuerySupportedCompressionRatesEXT = NULL;
	EGLint n_rates, *rates, n_configs;
	EGLint egl_major, egl_minor;
	EGLConfig *configs;
	EGLBoolean ret;

	/* Strip common piglit args. */
	piglit_strip_arg(&argc, argv, "-fbo");
	piglit_strip_arg(&argc, argv, "-auto");

	dpy = piglit_egl_get_default_display(EGL_NONE);
	if (!dpy) {
		piglit_loge("failed to get EGLDisplay\n");
		piglit_report_result(PIGLIT_SKIP);
	}

	ret = eglInitialize(dpy, &egl_major, &egl_minor);
	if (!ret) {
		EGLint egl_error = eglGetError();
		piglit_loge("failed to get EGLConfig: %s(0x%x)",
			    piglit_get_egl_error_name(egl_error), egl_error);
		piglit_report_result(PIGLIT_FAIL);
	}

	piglit_require_egl_extension(dpy, "EGL_EXT_surface_compression");

	peglQuerySupportedCompressionRatesEXT =
		(void *)eglGetProcAddress("eglQuerySupportedCompressionRatesEXT");

	if (!peglQuerySupportedCompressionRatesEXT) {
		piglit_loge("No display query entrypoint\n");
		piglit_report_result(PIGLIT_FAIL);
	}

	const EGLint config_attrs[] = {
		EGL_SURFACE_TYPE,	EGL_WINDOW_BIT,

		EGL_RED_SIZE,		EGL_DONT_CARE,
		EGL_GREEN_SIZE,		EGL_DONT_CARE,
		EGL_BLUE_SIZE,		EGL_DONT_CARE,
		EGL_ALPHA_SIZE,		EGL_DONT_CARE,
		EGL_DEPTH_SIZE, 	EGL_DONT_CARE,
		EGL_STENCIL_SIZE, 	EGL_DONT_CARE,
		EGL_RENDERABLE_TYPE, 	EGL_OPENGL_ES2_BIT,

		EGL_NONE,
	};
	const EGLAttrib attrib_list[] = { EGL_NONE };

	if (!eglChooseConfig(dpy, config_attrs, NULL, 0, &n_configs)) {
		printf("eglChooseConfig failed\n");
		piglit_report_result(PIGLIT_FAIL);
	}

	configs = calloc(n_configs, sizeof(*configs));
	eglChooseConfig(dpy, config_attrs, configs, n_configs, &n_configs);

	for (EGLint c = 0; c < n_configs; c++) {
		ret = peglQuerySupportedCompressionRatesEXT(dpy, configs[c], attrib_list,
				                            NULL, 0, &n_rates);
		if (!ret) {
			piglit_loge("Couldn't query the compression rates\n");
			piglit_report_result(PIGLIT_FAIL);
		}

		rates = calloc(n_rates, sizeof(*rates));
		peglQuerySupportedCompressionRatesEXT(dpy, configs[c], attrib_list,
				                      rates, n_rates, &n_rates);

		piglit_logd("Found %i rate(s) for config %p:", n_rates, configs[c]);
		for (EGLint r = 0; r < n_rates; r++) {
			piglit_logd("\t%i bpc", enum_to_rate(rates[r]));
		}
		free(rates);
	}

	free(configs);

	piglit_report_result(PIGLIT_PASS);
}
