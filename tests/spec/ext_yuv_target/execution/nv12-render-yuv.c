/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: MIT
 */

/**
 * @file nv12-render-yuv.c
 *
 * Test GL_EXT_YUV_target: render to an NV12 EGL image using
 * "layout(yuv) out" and verify the written pixels by sampling the result.
 *
 * Flow:
 *   Pass 1 – render-to-YUV:
 *     - Create a GL_TEXTURE_EXTERNAL_OES texture backed by an NV12 dma-buf.
 *     - Attach it to an FBO as GL_COLOR_ATTACHMENT0.
 *     - Run a fragment shader that writes a hardcoded BT.601 narrow-range
 *       YUV white (Y≈235/255, Cb≈128/255, Cr≈128/255) via
 *       "layout(yuv) out vec4 color".
 *
 *   Pass 2 – sample-and-convert:
 *     - Bind the same texture as a __samplerExternal2DY2YEXT sampler.
 *     - Sample it and convert back to RGB with yuv_2_rgb(itu_601).
 *     - Render to the default (RGBA) framebuffer.
 *     - Probe the result: expect approximately white (1, 1, 1).
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

static PFNEGLCREATEIMAGEKHRPROC  peglCreateImageKHR  = NULL;
static PFNEGLDESTROYIMAGEKHRPROC peglDestroyImageKHR = NULL;
static PFNGLEGLIMAGETARGETTEXTURE2DOESPROC pglEGLImageTargetTexture2DOES = NULL;

/* ── Pass 1: write a solid YUV colour into an NV12 render target ─────────── */
static const char vs_passthrough[] =
	"#version 300 es\n"
	"in vec4 piglit_vertex;\n"
	"void main() { gl_Position = piglit_vertex; }\n";

/*
 * BT.601 narrow-range white: Y=235/255≈0.9216, Cb=Cr=128/255≈0.5020.
 * The yuv layout qualifier maps: .r = Y, .g = Cb, .b = Cr.
 */
static const char fs_render_yuv[] =
	"#version 300 es\n"
	"#extension GL_EXT_YUV_target : require\n"
	"precision mediump float;\n"
	"layout(yuv) out vec4 color;\n"
	"void main()\n"
	"{\n"
	"    color = vec4(0.9216, 0.5020, 0.5020, 1.0);\n"
	"}\n";

/* ── Pass 2: sample the NV12 result and convert back to RGB ──────────────── */
static const char vs_tex[] =
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
 * Use a plain samplerExternalOES for Pass 2 — this sampler does automatic
 * YUV→RGB conversion and is independent of the GL_EXT_YUV_target extension.
 * Using the extension's own __samplerExternal2DY2YEXT here would mean we are
 * verifying the render with the very feature we are testing, which would mask
 * complementary bugs.  A standard external sampler provides an independent
 * check that the correct YUV bytes ended up in the NV12 buffer.
 */
static const char fs_sample_oes[] =
	"#version 300 es\n"
	"#extension GL_OES_EGL_image_external_essl3 : require\n"
	"precision mediump float;\n"
	"uniform samplerExternalOES sampler;\n"
	"in  vec2 texcoords;\n"
	"out vec4 color;\n"
	"void main()\n"
	"{\n"
	"    color = texture(sampler, texcoords);\n"
	"}\n";

/*
 * The Y and UV planes are allocated as two independent dma-bufs (via two
 * separate piglit_create_dma_buf() calls, DRM_FORMAT_R8 and DRM_FORMAT_GR88
 * respectively) and imported into the EGLImage as two distinct
 * EGL_DMA_BUF_PLANE*_FD_EXT descriptors.
 * Both placeholders are zero-initialised; Pass 1 overwrites them via the GPU.
 */
static const unsigned char y_zero[4*4] = { 0 };
static const unsigned char uv_zero[2*2*2] = { 0 };

enum piglit_result
piglit_display(void)
{
	enum piglit_result result = PIGLIT_PASS;
	struct piglit_dma_buf *buf_y = NULL, *buf_uv = NULL;
	EGLImageKHR img = EGL_NO_IMAGE_KHR;
	GLuint tex_yuv, fbo;
	EGLDisplay egl_dpy = eglGetCurrentDisplay();

	result = piglit_create_dma_buf(4, 4, DRM_FORMAT_R8, y_zero, &buf_y);
	if (result != PIGLIT_PASS)
		return result;

	result = piglit_create_dma_buf(2, 2, DRM_FORMAT_GR88, uv_zero, &buf_uv);
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

	/* Bind EGL image as GL_TEXTURE_EXTERNAL_OES */
	glGenTextures(1, &tex_yuv);
	glBindTexture(GL_TEXTURE_EXTERNAL_OES, tex_yuv);
	pglEGLImageTargetTexture2DOES(GL_TEXTURE_EXTERNAL_OES,
	                              (GLeglImageOES)img);
	if (!piglit_check_gl_error(GL_NO_ERROR)) {
		result = PIGLIT_FAIL;
		goto cleanup_tex;
	}
	glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_MIN_FILTER,
	                GL_NEAREST);
	glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_MAG_FILTER,
	                GL_NEAREST);

	/* ── Pass 1: render YUV into the NV12 texture via FBO ───────────────── */
	glGenFramebuffers(1, &fbo);
	glBindFramebuffer(GL_FRAMEBUFFER, fbo);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
	                       GL_TEXTURE_EXTERNAL_OES, tex_yuv, 0);

	if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
		/* Driver may not support rendering to external YUV targets */
		fprintf(stderr, "FBO with NV12 external texture not complete – "
		        "extension may not be fully supported\n");
		result = PIGLIT_SKIP;
		glBindFramebuffer(GL_FRAMEBUFFER, 0);
		glDeleteFramebuffers(1, &fbo);
		goto cleanup_tex;
	}

	{
		GLuint prog1 = piglit_build_simple_program(vs_passthrough,
		                                           fs_render_yuv);
		glUseProgram(prog1);
		glViewport(0, 0, 4, 4);
		piglit_draw_rect(-1, -1, 2, 2);
		glDeleteProgram(prog1);
		glUseProgram(0);
	}

	/* Ensure the render is complete before we sample */
	glFinish();

	/* ── Pass 2: sample NV12 → convert to RGB → render to window ─────────── */
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	glDeleteFramebuffers(1, &fbo);

	{
		GLuint prog2 = piglit_build_simple_program(vs_tex, fs_sample_oes);
		glUseProgram(prog2);
		glBindTexture(GL_TEXTURE_EXTERNAL_OES, tex_yuv);
		glUniform1i(glGetUniformLocation(prog2, "sampler"), 0);
		glViewport(0, 0, piglit_width, piglit_height);
		piglit_draw_rect_tex(-1, -1, 2, 2, 0, 0, 1, 1);
		glDeleteProgram(prog2);
		glUseProgram(0);
	}

	{
		/*
		 * The standard OES sampler converts NV12 YUV→RGB automatically.
		 * BT.601 narrow-range white (Y=235, Cb=128, Cr=128) must come
		 * back as RGB ~(1, 1, 1).  A small tolerance covers 8-bit
		 * quantisation in the NV12 planes.
		 */
		const float white[] = {1.0f, 1.0f, 1.0f};
		piglit_set_tolerance_for_bits(5, 5, 5, 8);
		if (!piglit_probe_rect_rgb(0, 0, piglit_width, piglit_height,
		                           white))
			result = PIGLIT_FAIL;
	}

cleanup_tex:
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
	piglit_require_extension("GL_OES_EGL_image_external_essl3");
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
