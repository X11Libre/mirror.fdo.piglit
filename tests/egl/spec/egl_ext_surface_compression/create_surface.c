/*
 * Copyright © 2024 Collabora Ltd
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
#include "piglit-util-gl.h"
#include "../../egl-util.h"
#include "../../egl-wayland.h"

static enum piglit_result
draw(void)
{
	/* Green for a pass */
	glClearColor(0.0, 1.0, 0.0, 1.0);
	glClear(GL_COLOR_BUFFER_BIT);

	return PIGLIT_PASS;
}

int
main(int argc, char *argv[])
{
	EGLDisplay dpy;
	PFNEGLQUERYSUPPORTEDCOMPRESSIONRATESEXTPROC peglQuerySupportedCompressionRatesEXT = NULL;
	EGLint n_rates, *rates, n_configs;
	EGLint egl_major, egl_minor;
	EGLConfig *configs;
	EGLContext ctx;
	EGLSurface surf;
	EGLBoolean ret;
	struct display *display;
	struct wl_display *native_display;

	display = create_wayland_display();
	if (!display) {
		piglit_loge("failed to connect to Wayland display\n");
		piglit_report_result(PIGLIT_SKIP);
	}

	native_display = get_wayland_native_display(display);
	dpy = piglit_egl_get_display(EGL_PLATFORM_WAYLAND_EXT, native_display);
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
	EGLint surface_attrs[] = {
		EGL_SURFACE_COMPRESSION_EXT,	EGL_SURFACE_COMPRESSION_FIXED_RATE_DEFAULT_EXT,
		EGL_NONE,
	};
	const EGLAttrib query_attrs[] = { EGL_NONE };

	if (!eglChooseConfig(dpy, config_attrs, NULL, 0, &n_configs)) {
		printf("eglChooseConfig failed\n");
		piglit_report_result(PIGLIT_FAIL);
	}

	configs = calloc(n_configs, sizeof(*configs));
	eglChooseConfig(dpy, config_attrs, configs, n_configs, &n_configs);

	for (EGLint c = 0; c < n_configs; c++) {
		ret = peglQuerySupportedCompressionRatesEXT(dpy, configs[c], query_attrs,
				                            NULL, 0, &n_rates);
		if (!ret) {
			piglit_loge("Couldn't query the compression rates\n");
			piglit_report_result(PIGLIT_FAIL);
		}

		piglit_logd("Found %i rate(s) for config %p:", n_rates, configs[c]);
		if (n_rates == 0)
			continue;

		ctx = eglCreateContext(dpy, configs[c], EGL_NO_CONTEXT, NULL);
		if (ctx == EGL_NO_CONTEXT) {
			fprintf(stderr, "eglCreateContext() failed\n");
			piglit_report_result(PIGLIT_FAIL);
		}

		rates = calloc(n_rates, sizeof(*rates));
		peglQuerySupportedCompressionRatesEXT(dpy, configs[c], query_attrs,
				                      rates, n_rates, &n_rates);

		for (EGLint r = 0; r < n_rates; r++) {
			const float color[] = { 0.0, 1.0, 0.0 };

			piglit_logd("\t%i bpc", enum_to_rate(rates[r]));
			surface_attrs[1] = rates[r];

			EGLNativeWindowType window = (EGLNativeWindowType) create_wayland_window(display, 256, 256);
			surf = eglCreateWindowSurface(dpy, configs[c], window, surface_attrs);
			if (surf == EGL_NO_SURFACE) {
				fprintf(stderr, "eglCreateWindowSurface() failed\n");
				piglit_report_result(PIGLIT_FAIL);
			}

			eglMakeCurrent(dpy, surf, surf, ctx);
			piglit_dispatch_default_init(PIGLIT_DISPATCH_ES2);
			draw();

			if (!piglit_probe_pixel_rgb(10, 10, color))
				piglit_report_result(PIGLIT_FAIL);

			eglSwapBuffers(dpy, surf);
		}
		free(rates);

		eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
		eglDestroyContext(dpy, ctx);
	}

	free(configs);

	piglit_report_result(PIGLIT_PASS);
}
