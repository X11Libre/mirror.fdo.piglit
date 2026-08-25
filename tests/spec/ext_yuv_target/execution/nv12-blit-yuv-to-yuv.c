/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: MIT
 */

/**
 * @file nv12-blit-yuv-to-yuv.c
 *
 * Test GL_EXT_YUV_target, Issue 6: glBlitFramebuffer() must reject a YUV
 * source or destination with GL_INVALID_OPERATION, even when both the
 * source and destination are YUV (NV12, external, dma-buf backed)
 * surfaces.
 *
 * Flow:
 *   1. CPU-fill a source NV12 dma-buf with a known YUV colour (white:
 *      Y=235, Cb=Cr=128 in BT.601 narrow-range, as in nv12-sample-y2y.c)
 *      and a second, zero-initialised NV12 dma-buf as the destination.
 *   2. glBlitFramebuffer() from the source NV12 FBO into the destination
 *      NV12 FBO: expect GL_INVALID_OPERATION.
 *
 * \author Tupili Krishna Gowtham Reddy <tupilikr@qti.qualcomm.com>
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

#define TEX_W 4
#define TEX_H 4

static PFNEGLCREATEIMAGEKHRPROC  peglCreateImageKHR  = NULL;
static PFNEGLDESTROYIMAGEKHRPROC peglDestroyImageKHR = NULL;
static PFNGLEGLIMAGETARGETTEXTURE2DOESPROC pglEGLImageTargetTexture2DOES = NULL;

/* BT.601 narrow-range white, see nv12-sample-y2y.c */
static const unsigned char y_white[TEX_W * TEX_H] = {
	235, 235, 235, 235,
	235, 235, 235, 235,
	235, 235, 235, 235,
	235, 235, 235, 235,
};

/* 2x2 chroma samples, interleaved Cb,Cr per sample */
static const unsigned char uv_white[(TEX_W / 2) * (TEX_H / 2) * 2] = {
	128, 128, 128, 128,
	128, 128, 128, 128,
};

static const unsigned char y_zero[TEX_W * TEX_H] = { 0 };
static const unsigned char uv_zero[(TEX_W / 2) * (TEX_H / 2) * 2] = { 0 };

/**
 * Create an NV12 EGLImage/external-texture/FBO triple backed by a dma-buf
 * pre-filled with the given Y and UV plane contents.
 */
static enum piglit_result
make_nv12_fbo(EGLDisplay egl_dpy, const unsigned char *y_data,
             const unsigned char *uv_data, struct piglit_dma_buf **out_buf_y,
             struct piglit_dma_buf **out_buf_uv, EGLImageKHR *out_img,
             GLuint *out_tex, GLuint *out_fbo)
{
	enum piglit_result result;
	struct piglit_dma_buf *buf_y = NULL, *buf_uv = NULL;
	EGLImageKHR img = EGL_NO_IMAGE_KHR;
	GLuint tex, fbo;

	result = piglit_create_dma_buf(TEX_W, TEX_H, DRM_FORMAT_R8, y_data,
	                               &buf_y);
	if (result != PIGLIT_PASS)
		return result;

	result = piglit_create_dma_buf(TEX_W / 2, TEX_H / 2, DRM_FORMAT_GR88,
	                               uv_data, &buf_uv);
	if (result != PIGLIT_PASS) {
		piglit_destroy_dma_buf(buf_y);
		return result;
	}

	EGLint img_attrs[] = {
		EGL_WIDTH,                     TEX_W,
		EGL_HEIGHT,                    TEX_H,
		EGL_LINUX_DRM_FOURCC_EXT,      DRM_FORMAT_NV12,
		EGL_DMA_BUF_PLANE0_FD_EXT,     buf_y->fd,
		EGL_DMA_BUF_PLANE0_OFFSET_EXT, (EGLint)buf_y->offset[0],
		EGL_DMA_BUF_PLANE0_PITCH_EXT,  (EGLint)buf_y->stride[0],
		EGL_DMA_BUF_PLANE1_FD_EXT,     buf_uv->fd,
		EGL_DMA_BUF_PLANE1_OFFSET_EXT, (EGLint)buf_uv->offset[0],
		EGL_DMA_BUF_PLANE1_PITCH_EXT,  (EGLint)buf_uv->stride[0],
		EGL_NONE
	};

	img = peglCreateImageKHR(egl_dpy, EGL_NO_CONTEXT, EGL_LINUX_DMA_BUF_EXT,
	                         (EGLClientBuffer)0, img_attrs);
	if (img == EGL_NO_IMAGE_KHR) {
		fprintf(stderr, "eglCreateImageKHR failed (NV12): 0x%x\n",
		        eglGetError());
		piglit_destroy_dma_buf(buf_uv);
		piglit_destroy_dma_buf(buf_y);
		return PIGLIT_SKIP;
	}

	glGenTextures(1, &tex);
	glBindTexture(GL_TEXTURE_EXTERNAL_OES, tex);
	pglEGLImageTargetTexture2DOES(GL_TEXTURE_EXTERNAL_OES, (GLeglImageOES)img);
	if (!piglit_check_gl_error(GL_NO_ERROR)) {
		glDeleteTextures(1, &tex);
		peglDestroyImageKHR(egl_dpy, img);
		piglit_destroy_dma_buf(buf_uv);
		piglit_destroy_dma_buf(buf_y);
		return PIGLIT_FAIL;
	}
	glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

	glGenFramebuffers(1, &fbo);
	glBindFramebuffer(GL_FRAMEBUFFER, fbo);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
	                       GL_TEXTURE_EXTERNAL_OES, tex, 0);

	/* Incomplete here isn't a driver bug: no required extension
	 * guarantees NV12 import or render-to-external-texture support. */
	if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
		fprintf(stderr, "NV12 not supported\n");
		glBindFramebuffer(GL_FRAMEBUFFER, 0);
		glDeleteFramebuffers(1, &fbo);
		glDeleteTextures(1, &tex);
		peglDestroyImageKHR(egl_dpy, img);
		piglit_destroy_dma_buf(buf_uv);
		piglit_destroy_dma_buf(buf_y);
		return PIGLIT_SKIP;
	}

	*out_buf_y = buf_y;
	*out_buf_uv = buf_uv;
	*out_img = img;
	*out_tex = tex;
	*out_fbo = fbo;
	return PIGLIT_PASS;
}

enum piglit_result
piglit_display(void)
{
	enum piglit_result result;
	struct piglit_dma_buf *src_buf_y, *src_buf_uv;
	struct piglit_dma_buf *dst_buf_y, *dst_buf_uv;
	EGLImageKHR src_img, dst_img;
	GLuint src_tex, src_fbo;
	GLuint dst_tex, dst_fbo;
	EGLDisplay egl_dpy = eglGetCurrentDisplay();

	result = make_nv12_fbo(egl_dpy, y_white, uv_white, &src_buf_y,
	                       &src_buf_uv, &src_img, &src_tex, &src_fbo);
	if (result != PIGLIT_PASS)
		return result;

	result = make_nv12_fbo(egl_dpy, y_zero, uv_zero, &dst_buf_y,
	                       &dst_buf_uv, &dst_img, &dst_tex, &dst_fbo);
	if (result != PIGLIT_PASS)
		goto cleanup_src;

	/* ── NV12 -> NV12 (YUV source and destination) must be rejected ────── */
	glBindFramebuffer(GL_READ_FRAMEBUFFER, src_fbo);
	glBindFramebuffer(GL_DRAW_FRAMEBUFFER, dst_fbo);
	glBlitFramebuffer(0, 0, TEX_W, TEX_H, 0, 0, TEX_W, TEX_H,
	                  GL_COLOR_BUFFER_BIT, GL_NEAREST);
	if (!piglit_check_gl_error(GL_INVALID_OPERATION))
		result = PIGLIT_FAIL;

	glDeleteFramebuffers(1, &dst_fbo);
	glDeleteTextures(1, &dst_tex);
	peglDestroyImageKHR(egl_dpy, dst_img);
	piglit_destroy_dma_buf(dst_buf_uv);
	piglit_destroy_dma_buf(dst_buf_y);
cleanup_src:
	glDeleteFramebuffers(1, &src_fbo);
	glDeleteTextures(1, &src_tex);
	peglDestroyImageKHR(egl_dpy, src_img);
	piglit_destroy_dma_buf(src_buf_uv);
	piglit_destroy_dma_buf(src_buf_y);

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
