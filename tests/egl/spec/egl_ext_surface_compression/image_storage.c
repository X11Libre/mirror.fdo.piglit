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
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#include "piglit-util-egl.h"
#include "piglit-util-gl.h"

PIGLIT_GL_TEST_CONFIG_BEGIN

	config.supports_gl_es_version = 30;

PIGLIT_GL_TEST_CONFIG_END

/* dummy */
enum piglit_result
piglit_display(void)
{
	return PIGLIT_FAIL;
}

static bool
verify_rgbw_texture()
{
	int width, height;
	glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &width);
	glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &height);

	float *expect = piglit_rgbw_image(GL_RGBA, width, height, true,
					  GL_UNSIGNED_NORMALIZED);
	unsigned hf = width / 2;
	unsigned color_stride = hf * 4; // one color width in image
	unsigned box = color_stride * hf; // one color box

	float *r = expect;
	float *g = expect + color_stride;
	float *b = expect + 2 * box;
	float *w = b + color_stride;

	bool pass = true;

	/* Verify texture contents by probing each color box. */
	pass = piglit_probe_texel_rect_rgba(GL_TEXTURE_2D, 0, 0, 0, hf, hf, r) && pass;
	pass = piglit_probe_texel_rect_rgba(GL_TEXTURE_2D, 0, hf, 0, hf, hf, g) && pass;
	pass = piglit_probe_texel_rect_rgba(GL_TEXTURE_2D, 0, 0, hf, hf, hf, b) && pass;
	pass = piglit_probe_texel_rect_rgba(GL_TEXTURE_2D, 0, hf, hf, hf, hf, w) && pass;

	free(expect);
	return pass;
}

void
piglit_init(int argc, char **argv)
{
	piglit_require_extension("GL_EXT_EGL_image_storage");
	piglit_require_extension("GL_EXT_EGL_image_storage_compression");
	piglit_require_extension("GL_EXT_texture_storage_compression");

	PFNEGLCREATEIMAGEKHRPROC peglCreateImageKHR = NULL;
	peglCreateImageKHR = (PFNEGLCREATEIMAGEKHRPROC)
		eglGetProcAddress("eglCreateImageKHR");
	if (!peglCreateImageKHR) {
		fprintf(stderr, "eglCreateImageKHR missing\n");
		piglit_report_result(PIGLIT_SKIP);
        }

	PFNEGLDESTROYIMAGEKHRPROC peglDestroyImageKHR = NULL;
	peglDestroyImageKHR = (PFNEGLDESTROYIMAGEKHRPROC)
		eglGetProcAddress("eglDestroyImageKHR");
	if (!peglDestroyImageKHR) {
		fprintf(stderr, "eglDestroyImageKHR missing\n");
		piglit_report_result(PIGLIT_SKIP);
        }

	/* Require EGL_MESA_platform_surfaceless extension. */
	const char *exts = eglQueryString(EGL_NO_DISPLAY, EGL_EXTENSIONS);
	if (!strstr(exts, "EGL_MESA_platform_surfaceless"))
		piglit_report_result(PIGLIT_SKIP);

	EGLint major, minor;
	EGLDisplay dpy;

	dpy = piglit_egl_get_default_display(EGL_PLATFORM_SURFACELESS_MESA);

	if (!eglInitialize(dpy, &major, &minor))
		piglit_report_result(PIGLIT_FAIL);

	piglit_require_egl_extension(dpy, "EGL_MESA_configless_context");

	EGLint ctx_attr[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
	EGLContext ctx =
		eglCreateContext(dpy, EGL_NO_CONFIG_KHR, EGL_NO_CONTEXT,
				 ctx_attr);
	if (ctx == EGL_NO_CONTEXT) {
		fprintf(stderr, "could not create EGL context\n");
		piglit_report_result(PIGLIT_FAIL);
	}

	eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, ctx);

	/* Select a supported compression rate. */
	GLint num_rates = 0, rate;

	glGetInternalformativ(GL_RENDERBUFFER, GL_RGBA8,
			      GL_NUM_SURFACE_COMPRESSION_FIXED_RATES_EXT, 1, &num_rates);
	glGetInternalformativ(GL_RENDERBUFFER, GL_RGBA8,
			      GL_SURFACE_COMPRESSION_EXT, 1, &rate);
	if (num_rates == 0)
		piglit_report_result(PIGLIT_SKIP);

	/* Create a compressed texture. */
	GLuint texture_a;
	glGenTextures(1, &texture_a);
	glBindTexture(GL_TEXTURE_2D, texture_a);

	const GLint attribs[3] = {GL_SURFACE_COMPRESSION_EXT, rate, GL_NONE};
	glTexStorageAttribs2DEXT(GL_TEXTURE_2D, 1, GL_RGBA8, 256, 256, attribs);
	if (!piglit_check_gl_error(GL_NO_ERROR))
		piglit_report_result(PIGLIT_FAIL);

	GLubyte *data = piglit_rgbw_image_ubyte(256, 256, GL_TRUE);
	glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 256, 256, GL_RGBA, GL_UNSIGNED_BYTE, data);
	free(data);
	if (!piglit_check_gl_error(GL_NO_ERROR))
		piglit_report_result(PIGLIT_FAIL);

	/* Create EGLImage from texture.  */
	EGLint img_attribs[] = { EGL_NONE };
	EGLImageKHR egl_image;
	egl_image = peglCreateImageKHR(dpy, ctx, EGL_GL_TEXTURE_2D,
				       (EGLClientBuffer) (intptr_t) texture_a,
				       img_attribs);
	if (!egl_image) {
		fprintf(stderr, "failed to create ImageKHR\n");
		piglit_report_result(PIGLIT_FAIL);
	}

	/* Create another texture. */
	GLuint texture_b;
	glGenTextures(1, &texture_b);
	glBindTexture(GL_TEXTURE_2D, texture_b);

	/* Specify texture from EGLImage, invalid params.  */
	const int none_attr[] = {
		GL_SURFACE_COMPRESSION_EXT, GL_SURFACE_COMPRESSION_FIXED_RATE_NONE_EXT,
		GL_NONE };
	glEGLImageTargetTexStorageEXT(GL_TEXTURE_2D, egl_image, none_attr);
	if (!piglit_check_gl_error(GL_INVALID_OPERATION))
		piglit_report_result(PIGLIT_FAIL);

	/* Specify texture from EGLImage, valid params.  */
	const int default_attr[] = {
		GL_SURFACE_COMPRESSION_EXT, GL_SURFACE_COMPRESSION_FIXED_RATE_DEFAULT_EXT,
		GL_NONE };
	glEGLImageTargetTexStorageEXT(GL_TEXTURE_2D, egl_image, default_attr);
	if (!piglit_check_gl_error(GL_NO_ERROR))
		piglit_report_result(PIGLIT_FAIL);

	if (!verify_rgbw_texture())
		piglit_report_result(PIGLIT_FAIL);

	/* Expected to be immutable. */
	GLint immutable_format;
	glGetTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_IMMUTABLE_FORMAT,
			    &immutable_format);
	if (immutable_format != 1)
		piglit_report_result(PIGLIT_FAIL);


	glDeleteTextures(1, &texture_a);
	glDeleteTextures(1, &texture_b);
	peglDestroyImageKHR(dpy, egl_image);

	piglit_report_result(PIGLIT_PASS);
}
