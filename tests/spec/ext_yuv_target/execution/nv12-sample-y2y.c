/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: MIT
 */

/**
 * @file nv12-sample-y2y.c
 *
 * Test GL_EXT_YUV_target: sampling an NV12 EGL image via
 * __samplerExternal2DY2YEXT and writing the result to an RGBA framebuffer
 * after converting back to RGB with yuv_2_rgb().
 *
 * Flow:
 *   1. CPU-fill a 4x4 NV12 dma-buf with a known YUV colour (white: Y=235,
 *      Cb=128, Cr=128 in BT.601 narrow-range).
 *   2. Import it as an EGLImage and bind it as GL_TEXTURE_EXTERNAL_OES.
 *   3. Sample with __samplerExternal2DY2YEXT – no implicit conversion.
 *   4. Convert YUV → RGB with yuv_2_rgb(yuv_2_rgb, itu_601) in the shader.
 *   5. Render to the window framebuffer and readback with piglit_probe_rect_rgb.
 */

#include "piglit-util-egl.h"
#include "piglit-util-gl.h"
#include "piglit-framework-gl/piglit_drm_dma_buf.h"
#include "drm-uapi/drm_fourcc.h"

PIGLIT_GL_TEST_CONFIG_BEGIN

	config.supports_gl_es_version = 30;
	config.window_width  = 4;
	config.window_height = 4;
	config.window_visual = PIGLIT_GL_VISUAL_RGBA;

PIGLIT_GL_TEST_CONFIG_END

/* Function pointers loaded at init time */
static PFNEGLCREATEIMAGEKHRPROC  peglCreateImageKHR  = NULL;
static PFNEGLDESTROYIMAGEKHRPROC peglDestroyImageKHR = NULL;
static PFNGLEGLIMAGETARGETTEXTURE2DOESPROC pglEGLImageTargetTexture2DOES = NULL;

static const char vs_src[] =
	"#version 300 es\n"
	"in vec4 piglit_vertex;\n"
	"in vec4 piglit_texcoords;\n"
	"out vec2 texcoords;\n"
	"void main()\n"
	"{\n"
	"    texcoords   = piglit_texcoords.xy;\n"
	"    gl_Position = piglit_vertex;\n"
	"}\n";

/*
 * Sample the NV12 texture without conversion (Y->R, Cb->G, Cr->B),
 * then convert the YUV triplet back to RGB so we can probe an ordinary RGBA
 * render target.
 */
static const char fs_src[] =
	"#version 300 es\n"
	"#extension GL_EXT_YUV_target : require\n"
	"precision mediump float;\n"
	"uniform __samplerExternal2DY2YEXT sampler;\n"
	"in  vec2 texcoords;\n"
	"out vec4 color;\n"
	"void main()\n"
	"{\n"
	"    vec4 yuva = texture(sampler, texcoords);\n"
	"    color = vec4(yuv_2_rgb(yuva.rgb, itu_601), 1.0);\n"
	"}\n";

/*
 * NV12 4x4 frame filled with white in BT.601 narrow-range:
 *   Y  = 235  (all luma samples)
 *   Cb = 128  (chroma neutral)
 *   Cr = 128
 *
 * After yuv_2_rgb(itu_601) white maps to approximately (1, 1, 1).
 * We allow piglit's default tolerance.
 *
 * The Y and UV planes are allocated as two independent dma-bufs (via two
 * separate piglit_create_dma_buf() calls, DRM_FORMAT_R8 and DRM_FORMAT_GR88
 * respectively) and imported into the EGLImage as two distinct
 * EGL_DMA_BUF_PLANE*_FD_EXT descriptors.
 */
static const unsigned char y_white[4*4] = {
	235, 235, 235, 235,
	235, 235, 235, 235,
	235, 235, 235, 235,
	235, 235, 235, 235,
};

/* 2x2 chroma samples, interleaved Cb,Cr per sample */
static const unsigned char uv_white[2*2*2] = {
	128, 128, 128, 128,
	128, 128, 128, 128,
};

enum piglit_result
piglit_display(void)
{
	enum piglit_result result = PIGLIT_PASS;
	struct piglit_dma_buf *buf_y = NULL, *buf_uv = NULL;
	EGLImageKHR img = EGL_NO_IMAGE_KHR;
	GLuint tex, prog;
	EGLDisplay egl_dpy = eglGetCurrentDisplay();

	/* Create the Y and UV planes as two independent dma-bufs */
	result = piglit_create_dma_buf(4, 4, DRM_FORMAT_R8, y_white, &buf_y);
	if (result != PIGLIT_PASS)
		return result;

	result = piglit_create_dma_buf(2, 2, DRM_FORMAT_GR88, uv_white, &buf_uv);
	if (result != PIGLIT_PASS)
		goto cleanup_buf_y;

	EGLint img_attrs[] = {
		EGL_WIDTH,                     4,
		EGL_HEIGHT,                    4,
		EGL_LINUX_DRM_FOURCC_EXT,      DRM_FORMAT_NV12,
		EGL_DMA_BUF_PLANE0_FD_EXT,     buf_y->fd,
		EGL_DMA_BUF_PLANE0_OFFSET_EXT, (EGLint)buf_y->offset[0],
		EGL_DMA_BUF_PLANE0_PITCH_EXT,  (EGLint)buf_y->stride[0],
		EGL_DMA_BUF_PLANE1_FD_EXT,     buf_uv->fd,
		EGL_DMA_BUF_PLANE1_OFFSET_EXT, (EGLint)buf_uv->offset[0],
		EGL_DMA_BUF_PLANE1_PITCH_EXT,  (EGLint)buf_uv->stride[0],
		EGL_NONE
	};

	img = peglCreateImageKHR(egl_dpy, EGL_NO_CONTEXT,
	                         EGL_LINUX_DMA_BUF_EXT,
	                         (EGLClientBuffer)0, img_attrs);
	if (img == EGL_NO_IMAGE_KHR) {
		fprintf(stderr, "eglCreateImageKHR failed (NV12): 0x%x\n",
		        eglGetError());
		result = PIGLIT_SKIP;
		goto cleanup_buf_uv;
	}

	/* Bind as external texture */
	glGenTextures(1, &tex);
	glBindTexture(GL_TEXTURE_EXTERNAL_OES, tex);
	pglEGLImageTargetTexture2DOES(GL_TEXTURE_EXTERNAL_OES,
	                              (GLeglImageOES)img);
	if (!piglit_check_gl_error(GL_NO_ERROR)) {
		result = PIGLIT_FAIL;
		goto cleanup_img;
	}
	glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_MIN_FILTER,
	                GL_NEAREST);
	glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_MAG_FILTER,
	                GL_NEAREST);

	prog = piglit_build_simple_program(vs_src, fs_src);
	glUseProgram(prog);
	glUniform1i(glGetUniformLocation(prog, "sampler"), 0);

	glViewport(0, 0, piglit_width, piglit_height);
	piglit_draw_rect_tex(-1, -1, 2, 2, 0, 0, 1, 1);

	glDeleteProgram(prog);
	glUseProgram(0);

	{
		/* White in BT.601 narrow-range: after yuv_2_rgb expect ~(1,1,1) */
		const float white[] = {1.0f, 1.0f, 1.0f};
		if (!piglit_probe_rect_rgb(0, 0, piglit_width, piglit_height,
		                           white))
			result = PIGLIT_FAIL;
	}

	glDeleteTextures(1, &tex);

cleanup_img:
	peglDestroyImageKHR(egl_dpy, img);
cleanup_buf_uv:
	piglit_destroy_dma_buf(buf_uv);
cleanup_buf_y:
	piglit_destroy_dma_buf(buf_y);

	piglit_present_results();
	return result;
}

void
piglit_init(int argc, char **argv)
{
	EGLDisplay egl_dpy = eglGetCurrentDisplay();
	if (!egl_dpy)
		piglit_report_result(PIGLIT_SKIP);

	piglit_require_extension("GL_EXT_YUV_target");
	piglit_require_extension("GL_OES_EGL_image_external");
	piglit_require_egl_extension(egl_dpy, "EGL_KHR_image_base");
	piglit_require_egl_extension(egl_dpy, "EGL_EXT_image_dma_buf_import");

	peglCreateImageKHR = (PFNEGLCREATEIMAGEKHRPROC)
		eglGetProcAddress("eglCreateImageKHR");
	peglDestroyImageKHR = (PFNEGLDESTROYIMAGEKHRPROC)
		eglGetProcAddress("eglDestroyImageKHR");
	pglEGLImageTargetTexture2DOES = (PFNGLEGLIMAGETARGETTEXTURE2DOESPROC)
		eglGetProcAddress("glEGLImageTargetTexture2DOES");

	if (!peglCreateImageKHR || !peglDestroyImageKHR ||
	    !pglEGLImageTargetTexture2DOES)
		piglit_report_result(PIGLIT_SKIP);
}
