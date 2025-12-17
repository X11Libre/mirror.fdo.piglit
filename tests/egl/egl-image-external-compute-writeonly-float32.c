/*
 * Copyright © 2025 Qualcomm Technologies, Inc.
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
 * @file egl-image-external-compute-writeonly-float32.c
 *
 * Tests EGL images with various high-precision float formats
 * used in compute shader image store operations.
 *
 * Tests that DMA-BUF imports with float formats get correct sized internal
 * formats and work with shader image store operations (write only).
 */

#include <unistd.h>
#include "piglit-util-egl.h"
#include "piglit-util-gl.h"

PIGLIT_GL_TEST_CONFIG_BEGIN
	config.supports_gl_es_version = 31;
PIGLIT_GL_TEST_CONFIG_END

/* Format test configuration */
struct format_info {
	const char *name;
	GLenum gl_internal_format;
	const char *image_format_qualifier;
	int num_components;
	const char *test_color;  /* GLSL vec4 literal */
	float expected[4];       /* Expected values for each channel */
};

static const struct format_info formats[] = {
	/* Single channel float32 formats */
	{
		.name = "R32F",
		.gl_internal_format = GL_R32F,
		.image_format_qualifier = "r32f",
		.num_components = 1,
		.test_color = "vec4(0.75, 0.0, 0.0, 0.0)",
		.expected = {0.75f, 0.0f, 0.0f, 0.0f}
	},
	/* Two channel float32 formats */
	{
		.name = "RG32F",
		.gl_internal_format = GL_RG32F,
		.image_format_qualifier = "rgba32f",
		.num_components = 2,
		.test_color = "vec4(0.25, 0.50, 0.75, 1.0)",
		.expected = {0.25f, 0.50f, 0.75f, 1.0f}
	},
	/* Four channel float32 formats */
	{
		.name = "RGBA32F",
		.gl_internal_format = GL_RGBA32F,
		.image_format_qualifier = "rgba32f",
		.num_components = 4,
		.test_color = "vec4(0.25, 0.50, 0.75, 1.0)",
		.expected = {0.25f, 0.50f, 0.75f, 1.0f}
	},
};

enum piglit_result
piglit_display(void)
{
	return PIGLIT_FAIL;
}

static char *
generate_compute_shader(const struct format_info *fmt)
{
	char *shader;
	asprintf(&shader,
		"#version 310 es\n"
		"layout(local_size_x = 8, local_size_y = 8) in;\n"
		"layout(%s, binding = 0) writeonly uniform highp image2D img;\n"
		"\n"
		"void main() {\n"
		"    ivec2 coord = ivec2(gl_GlobalInvocationID.xy);\n"
		"    vec4 color = %s;\n"
		"    imageStore(img, coord, color);\n"
		"}\n",
		fmt->image_format_qualifier,
		fmt->test_color);
	return shader;
}

static enum piglit_result
test_format(EGLDisplay dpy, EGLContext ctx, const struct format_info *fmt)
{
	const int width = 64;
	const int height = 64;
	bool pass = true;

	piglit_logi("=== Testing format: %s ===", fmt->name);

	/* Get EGL function pointers */
	PFNEGLCREATEIMAGEKHRPROC peglCreateImageKHR =
		(PFNEGLCREATEIMAGEKHRPROC)eglGetProcAddress("eglCreateImageKHR");
	PFNEGLDESTROYIMAGEKHRPROC peglDestroyImageKHR =
		(PFNEGLDESTROYIMAGEKHRPROC)eglGetProcAddress("eglDestroyImageKHR");
	PFNEGLEXPORTDMABUFIMAGEQUERYMESAPROC peglExportDMABUFImageQueryMESA =
		(PFNEGLEXPORTDMABUFIMAGEQUERYMESAPROC)eglGetProcAddress("eglExportDMABUFImageQueryMESA");
	PFNEGLEXPORTDMABUFIMAGEMESAPROC peglExportDMABUFImageMESA =
		(PFNEGLEXPORTDMABUFIMAGEMESAPROC)eglGetProcAddress("eglExportDMABUFImageMESA");

	/* Create source texture */
	GLuint tex_src;
	glGenTextures(1, &tex_src);
	glBindTexture(GL_TEXTURE_2D, tex_src);
	glTexStorage2D(GL_TEXTURE_2D, 1, fmt->gl_internal_format, width, height);

	if (!piglit_check_gl_error(GL_NO_ERROR)) {
		piglit_logi("SKIP: Failed to create texture with format %s", fmt->name);
		return PIGLIT_SKIP;
	}

	/* Export as DMA-BUF */
	EGLImageKHR egl_image_export = peglCreateImageKHR(dpy, ctx,
		EGL_GL_TEXTURE_2D,
		(EGLClientBuffer)(uintptr_t)tex_src,
		NULL);

	if (!egl_image_export) {
		piglit_logi("SKIP: Failed to create EGL image for %s", fmt->name);
		glDeleteTextures(1, &tex_src);
		return PIGLIT_SKIP;
	}

	int fourcc, num_planes;
	EGLuint64KHR modifier;
	if (!peglExportDMABUFImageQueryMESA(dpy, egl_image_export,
					    &fourcc, &num_planes, &modifier)) {
		piglit_logi("SKIP: Failed to query DMA-BUF for %s", fmt->name);
		peglDestroyImageKHR(dpy, egl_image_export);
		glDeleteTextures(1, &tex_src);
		return PIGLIT_SKIP;
	}

	int fd;
	EGLint stride, offset;
	if (!peglExportDMABUFImageMESA(dpy, egl_image_export,
				       &fd, &stride, &offset)) {
		piglit_logi("SKIP: Failed to export DMA-BUF for %s", fmt->name);
		peglDestroyImageKHR(dpy, egl_image_export);
		glDeleteTextures(1, &tex_src);
		return PIGLIT_SKIP;
	}

	peglDestroyImageKHR(dpy, egl_image_export);
	glDeleteTextures(1, &tex_src);

	/* Import DMA-BUF as EGL image */
	const EGLint import_attrs[] = {
		EGL_WIDTH, width,
		EGL_HEIGHT, height,
		EGL_LINUX_DRM_FOURCC_EXT, fourcc,
		EGL_DMA_BUF_PLANE0_FD_EXT, fd,
		EGL_DMA_BUF_PLANE0_OFFSET_EXT, offset,
		EGL_DMA_BUF_PLANE0_PITCH_EXT, stride,
		EGL_DMA_BUF_PLANE0_MODIFIER_LO_EXT, 0x0,
		EGL_DMA_BUF_PLANE0_MODIFIER_HI_EXT, 0x0,
		EGL_NONE
	};

	EGLImageKHR egl_image = peglCreateImageKHR(dpy, EGL_NO_CONTEXT,
		EGL_LINUX_DMA_BUF_EXT,
		NULL,
		import_attrs);

	if (!egl_image) {
		piglit_logi("SKIP: Failed to import DMA-BUF for %s", fmt->name);
		close(fd);
		return PIGLIT_SKIP;
	}

	/* Bind EGL image to texture */
	GLuint tex;
	glGenTextures(1, &tex);
	glBindTexture(GL_TEXTURE_2D, tex);
	glEGLImageTargetTexture2DOES(GL_TEXTURE_2D, egl_image);

	if (!piglit_check_gl_error(GL_NO_ERROR)) {
		piglit_loge("FAIL: Failed to bind EGL image for %s", fmt->name);
		pass = false;
		goto cleanup;
	}

	/* Bind texture to image unit */
	glBindImageTexture(0, tex, 0, GL_FALSE, 0, GL_WRITE_ONLY, fmt->gl_internal_format);

	if (!piglit_check_gl_error(GL_NO_ERROR)) {
		piglit_loge("FAIL: glBindImageTexture failed for %s", fmt->name);
		pass = false;
		goto cleanup;
	}

	piglit_logi("glBindImageTexture completed");

	/* Create and compile compute shader */
	char *shader_source = generate_compute_shader(fmt);
	GLuint prog = glCreateProgram();
	GLuint shader = glCreateShader(GL_COMPUTE_SHADER);
	glShaderSource(shader, 1, (const char **)&shader_source, NULL);
	glCompileShader(shader);
	free(shader_source);

	GLint status;
	glGetShaderiv(shader, GL_COMPILE_STATUS, &status);
	if (!status) {
		piglit_loge("FAIL: Compute shader compilation failed for %s", fmt->name);
		GLchar log[1000];
		GLsizei len;
		glGetShaderInfoLog(shader, 1000, &len, log);
		piglit_loge("  Shader log:\n%s", log);
		pass = false;
		goto cleanup_shader;
	}

	glAttachShader(prog, shader);
	glLinkProgram(prog);
	glGetProgramiv(prog, GL_LINK_STATUS, &status);
	if (!status) {
		piglit_loge("FAIL: Compute shader linking failed for %s", fmt->name);
		pass = false;
		goto cleanup_shader;
	}

	glUseProgram(prog);

	/* Dispatch compute shader */
	glDispatchCompute(width / 8, height / 8, 1);
	glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);

	if (!piglit_check_gl_error(GL_NO_ERROR)) {
		piglit_loge("FAIL: glDispatchCompute failed for %s", fmt->name);
		pass = false;
		goto cleanup_shader;
	}

	piglit_logi("glDispatchCompute completed");

	/* Read back and verify (simplified - just check not all zeros) */
	float *pixels = malloc(width * height * 4 * sizeof(float));
	GLuint fbo;
	glGenFramebuffers(1, &fbo);
	glBindFramebuffer(GL_FRAMEBUFFER, fbo);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
			       GL_TEXTURE_2D, tex, 0);

	if (glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE) {
		glReadPixels(0, 0, width, height, GL_RGBA, GL_FLOAT, pixels);

		bool values_match = true;
		for (int y = 0; y < height; y++) {
			for (int x = 0; x < width; x++) {
				int pixel_idx = (y * width + x) * 4;  /* 4 components per pixel */

				/* Check only the components that should have data */
				for (int c = 0; c < fmt->num_components; c++) {
					float actual = pixels[pixel_idx + c];
					float expected = fmt->expected[c];

					/*Compare whether actual matches with expected or not*/
					if (actual != expected) {
						piglit_logd("FAIL: Pixel (%d,%d) channel %d: expected %.2f, got %.2f",
							    x, y, c, expected, actual);
						values_match = false;
						pass = false;
						break;
					}
				}
			}
		}

		if (values_match) {
			piglit_logi("  PASS: Actual Pixel values matches with Expected values");
		} else {
			piglit_loge("FAIL: Actual Pixel values do not match Expected for %s", fmt->name);
                }
	}

	glDeleteFramebuffers(1, &fbo);
	free(pixels);

cleanup_shader:
	glDeleteShader(shader);
	glDeleteProgram(prog);

cleanup:
	peglDestroyImageKHR(dpy, egl_image);
	glDeleteTextures(1, &tex);
	close(fd);

	return pass ? PIGLIT_PASS : PIGLIT_FAIL;
}

void
piglit_init(int argc, char **argv)
{
	piglit_require_extension("GL_OES_EGL_image");

	/* Require EGL_MESA_platform_surfaceless */
	const char *exts = eglQueryString(EGL_NO_DISPLAY, EGL_EXTENSIONS);
	if (!strstr(exts, "EGL_MESA_platform_surfaceless")) {
		piglit_logi("EGL_MESA_platform_surfaceless not supported");
		piglit_report_result(PIGLIT_SKIP);
	}

	EGLint major, minor;
	EGLDisplay dpy = piglit_egl_get_default_display(EGL_PLATFORM_SURFACELESS_MESA);

	if (!eglInitialize(dpy, &major, &minor)) {
		piglit_loge("Failed to initialize EGL");
		piglit_report_result(PIGLIT_FAIL);
	}

	piglit_require_egl_extension(dpy, "EGL_MESA_configless_context");

	if (!piglit_is_egl_extension_supported(dpy, "EGL_MESA_image_dma_buf_export") ||
	    !piglit_is_egl_extension_supported(dpy, "EGL_EXT_image_dma_buf_import")) {
		piglit_logi("DMA-BUF extensions not available");
		piglit_report_result(PIGLIT_SKIP);
	}

	EGLint ctx_attr[] = {
		EGL_CONTEXT_CLIENT_VERSION, 3,
		EGL_CONTEXT_MINOR_VERSION, 1,
		EGL_NONE
	};

	EGLContext ctx = eglCreateContext(dpy, EGL_NO_CONFIG_KHR,
					  EGL_NO_CONTEXT, ctx_attr);
	if (ctx == EGL_NO_CONTEXT) {
		piglit_loge("Failed to create EGL context\n");
		piglit_report_result(PIGLIT_FAIL);
	}

	eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, ctx);

	bool all_pass = true;

	/* Test all formats - report result for each */
	for (unsigned i = 0; i < ARRAY_SIZE(formats); i++) {
		enum piglit_result result = test_format(dpy, ctx, &formats[i]);

		/* Track if any test failed or was skipped */
		if (result == PIGLIT_FAIL) {
			all_pass = false;
		} else if (result == PIGLIT_SKIP) {
			all_pass = false;
		}

		/* Report subtest result for this format */
		piglit_report_subtest_result(result, "%s", formats[i].name);
	}

	/* Report overall result */
	piglit_report_result(all_pass ? PIGLIT_PASS : PIGLIT_FAIL);
}
