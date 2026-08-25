/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: MIT
 */

/**
 * @file nv12-blit-roundtrip.c
 *
 * Test GL_EXT_YUV_target, Issue 6: glBlitFramebuffer() must reject a YUV
 * source or destination with GL_INVALID_OPERATION.
 *
 * Flow:
 *   1. Clear an RGBA FBO to white.
 *   2. glBlitFramebuffer() RGBA -> NV12 (external, dma-buf backed): the
 *      destination is YUV, so GL_INVALID_OPERATION is expected.
 *   3. glBlitFramebuffer() that NV12 FBO -> a second RGBA FBO: the source
 *      is YUV, so GL_INVALID_OPERATION is expected.
 *
 * Neither call is expected to transfer any pixel data, so there is nothing
 * to probe afterwards -- generating the correct error in both directions
 * is the entire test.
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

/*
 * The Y and UV planes are allocated as two independent dma-bufs (via two
 * separate piglit_create_dma_buf() calls, DRM_FORMAT_R8 and DRM_FORMAT_GR88
 * respectively) and imported into the EGLImage as two distinct
 * EGL_DMA_BUF_PLANE*_FD_EXT descriptors.
 */
static const unsigned char y_zero[TEX_W * TEX_H] = { 0 };
static const unsigned char uv_zero[(TEX_W / 2) * (TEX_H / 2) * 2] = { 0 };

static GLuint
make_rgba_fbo(GLuint *out_tex)
{
	GLuint tex, fbo;

	glGenTextures(1, &tex);
	glBindTexture(GL_TEXTURE_2D, tex);
	glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA8, TEX_W, TEX_H);

	glGenFramebuffers(1, &fbo);
	glBindFramebuffer(GL_FRAMEBUFFER, fbo);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
	                       GL_TEXTURE_2D, tex, 0);

	*out_tex = tex;
	return fbo;
}

enum piglit_result
piglit_display(void)
{
	enum piglit_result result = PIGLIT_PASS;
	struct piglit_dma_buf *buf_y = NULL, *buf_uv = NULL;
	EGLImageKHR img = EGL_NO_IMAGE_KHR;
	GLuint tex_yuv, fbo_yuv;
	GLuint tex_rgba1, fbo_rgba1;
	GLuint tex_rgba2, fbo_rgba2;
	EGLDisplay egl_dpy = eglGetCurrentDisplay();

	result = piglit_create_dma_buf(TEX_W, TEX_H, DRM_FORMAT_R8, y_zero,
	                               &buf_y);
	if (result != PIGLIT_PASS)
		return result;

	result = piglit_create_dma_buf(TEX_W / 2, TEX_H / 2, DRM_FORMAT_GR88,
	                               uv_zero, &buf_uv);
	if (result != PIGLIT_PASS)
		goto cleanup_buf_y;

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

	img = peglCreateImageKHR(egl_dpy, EGL_NO_CONTEXT,
	                         EGL_LINUX_DMA_BUF_EXT,
	                         (EGLClientBuffer)0, img_attrs);
	if (img == EGL_NO_IMAGE_KHR) {
		fprintf(stderr, "eglCreateImageKHR failed (NV12): 0x%x\n",
		        eglGetError());
		result = PIGLIT_SKIP;
		goto cleanup_buf_uv;
	}

	glGenTextures(1, &tex_yuv);
	glBindTexture(GL_TEXTURE_EXTERNAL_OES, tex_yuv);
	pglEGLImageTargetTexture2DOES(GL_TEXTURE_EXTERNAL_OES,
	                              (GLeglImageOES)img);
	if (!piglit_check_gl_error(GL_NO_ERROR)) {
		result = PIGLIT_FAIL;
		goto cleanup_tex_yuv;
	}
	glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_MIN_FILTER,
	                GL_NEAREST);
	glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_MAG_FILTER,
	                GL_NEAREST);

	glGenFramebuffers(1, &fbo_yuv);
	glBindFramebuffer(GL_FRAMEBUFFER, fbo_yuv);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
	                       GL_TEXTURE_EXTERNAL_OES, tex_yuv, 0);

	/* Incomplete here isn't a driver bug: no required extension
	 * guarantees NV12 import or render-to-external-texture support. */
	if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
		fprintf(stderr, "NV12 not supported\n");
		result = PIGLIT_SKIP;
		glBindFramebuffer(GL_FRAMEBUFFER, 0);
		glDeleteFramebuffers(1, &fbo_yuv);
		goto cleanup_tex_yuv;
	}

	/* ── Step 1: clear an RGBA FBO to white ─────────────────────────────── */
	fbo_rgba1 = make_rgba_fbo(&tex_rgba1);
	glBindFramebuffer(GL_FRAMEBUFFER, fbo_rgba1);
	glClearColor(1.0f, 1.0f, 1.0f, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT);

	/* ── Step 2: RGBA -> NV12 (YUV destination) must be rejected ────────── */
	glBindFramebuffer(GL_READ_FRAMEBUFFER, fbo_rgba1);
	glBindFramebuffer(GL_DRAW_FRAMEBUFFER, fbo_yuv);
	glBlitFramebuffer(0, 0, TEX_W, TEX_H, 0, 0, TEX_W, TEX_H,
	                  GL_COLOR_BUFFER_BIT, GL_NEAREST);
	if (!piglit_check_gl_error(GL_INVALID_OPERATION)) {
		result = PIGLIT_FAIL;
		goto cleanup_rgba1;
	}

	/* ── Step 3: NV12 -> RGBA (YUV source) must be rejected ─────────────── */
	fbo_rgba2 = make_rgba_fbo(&tex_rgba2);
	glBindFramebuffer(GL_READ_FRAMEBUFFER, fbo_yuv);
	glBindFramebuffer(GL_DRAW_FRAMEBUFFER, fbo_rgba2);
	glBlitFramebuffer(0, 0, TEX_W, TEX_H, 0, 0, TEX_W, TEX_H,
	                  GL_COLOR_BUFFER_BIT, GL_NEAREST);
	if (!piglit_check_gl_error(GL_INVALID_OPERATION)) {
		result = PIGLIT_FAIL;
		goto cleanup_rgba2;
	}

cleanup_rgba2:
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	glDeleteFramebuffers(1, &fbo_rgba2);
	glDeleteTextures(1, &tex_rgba2);
cleanup_rgba1:
	glDeleteFramebuffers(1, &fbo_rgba1);
	glDeleteTextures(1, &tex_rgba1);
	glDeleteFramebuffers(1, &fbo_yuv);
cleanup_tex_yuv:
	glDeleteTextures(1, &tex_yuv);
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
