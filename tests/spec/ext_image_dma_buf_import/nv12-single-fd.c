/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: MIT
 */

/*
 * Author:
 *    Tupili Krishna Gowtham Reddy <tupilikr@qti.qualcomm.com>
 */

/**
 * @file nv12-single-fd.c
 *
 * Test EGL_EXT_image_dma_buf_import + GL_OES_EGL_image_external: sample an
 * NV12 buffer imported two different ways and verify both ways sample
 * identically.
 *
 *  - dual-fd: the Y and UV planes live in two independent dma-bufs, each
 *             with its own file descriptor
 *             (EGL_DMA_BUF_PLANE0_FD_EXT != EGL_DMA_BUF_PLANE1_FD_EXT).
 *             Built via piglit_create_dma_buf(), which for DRM_FORMAT_NV12
 *             actually allocates a plain GBM_FORMAT_GR88 buffer and slices
 *             it in two - it never asks the driver for a native two-plane
 *             NV12 resource.
 *
 *  - single-fd: the Y and UV planes live in one dma-buf/BO, imported with
 *               both plane descriptors referencing the same fd at
 *               different offsets. Unlike the dual-fd path, this is built
 *               by calling gbm_bo_create() with the *real*
 *               GBM_FORMAT_NV12, which is exactly the capability the
 *               freedreno single-BO NV12 series adds.
 *
 * Native single-BO NV12 allocation is not mandated by any spec -
 * EGL_EXT_image_dma_buf_import only governs import, and GBM treats
 * per-format allocation support as optional and driver-advertised
 * (gbm_device_is_format_supported()). So a driver that doesn't advertise
 * it isn't violating anything and this test SKIPs; only a driver that
 * *does* advertise support but still fails to allocate is a genuine bug,
 * which is reported as FAIL. See create_single_fd_source() for the exact
 * SKIP/FAIL split.
 *
 * The single-fd buffer is forced to GBM_BO_USE_LINEAR so that a plain CPU
 * mmap + memcpy per plane (bracketed by DMA_BUF_IOCTL_SYNC for cache
 * coherency) is a valid way to fill it; this test does not attempt to
 * validate any UBWC/compressed-modifier layout.
 *
 * Both buffers are CPU-filled with the same non-trivial YUV test pattern.
 * Each is imported as an EGLImage, bound as a GL_TEXTURE_EXTERNAL_OES
 * texture and sampled by a plain samplerExternalOES into its own tile of
 * the window framebuffer - no format-conversion shader logic involved,
 * whatever the driver's external-sampler does automatically is what gets
 * compared. The two tiles are then probed against each other: if the
 * single-BO import produces the same sampled output as the long-standing
 * dual-BO import, the test passes.
 */

#include "piglit-util-egl.h"
#include "piglit-util-gl.h"
#include "piglit-framework-gl/piglit_drm_dma_buf.h"
#include "drm-uapi/drm_fourcc.h"

#ifdef PIGLIT_HAS_GBM_BO_MAP
#include <gbm.h>
#endif
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <linux/dma-buf.h>

#define TILE_W 4
#define TILE_H 4

PIGLIT_GL_TEST_CONFIG_BEGIN

	config.supports_gl_es_version = 30;
	config.window_width  = TILE_W * 2;
	config.window_height = TILE_H;
	config.window_visual = PIGLIT_GL_VISUAL_RGBA;

PIGLIT_GL_TEST_CONFIG_END

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
 * Plain external-texture sample - whatever YUV -> RGB conversion the
 * driver's samplerExternalOES implementation applies is exactly what we
 * are comparing between the two import paths.
 */
static const char fs_src[] =
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
 * NV12 4x4 test pattern - a Y ramp with varying chroma, so a broken
 * plane offset/stride would show up as a visibly different sample
 * instead of being masked by a flat colour.
 */
static const unsigned char y_pattern[TILE_W * TILE_H] = {
	 50,  70,  90, 110,
	 50,  70,  90, 110,
	 50,  70,  90, 110,
	 50,  70,  90, 110,
};

/* 2x2 chroma samples, interleaved Cb,Cr per sample */
static const unsigned char uv_pattern[(TILE_W / 2) * (TILE_H / 2) * 2] = {
	120, 130, 140, 130,
	120, 160, 140, 160,
};

/* Create an NV12 EGLImage backed by two independent dma-bufs (two fds). */
static bool
create_dual_fd_source(struct piglit_dma_buf **out_buf_y,
                      struct piglit_dma_buf **out_buf_uv,
                      EGLImageKHR *out_img)
{
	struct piglit_dma_buf *buf_y = NULL, *buf_uv = NULL;
	EGLDisplay egl_dpy = eglGetCurrentDisplay();

	if (piglit_create_dma_buf(TILE_W, TILE_H, DRM_FORMAT_R8,
	                         y_pattern, &buf_y) != PIGLIT_PASS)
		return false;

	if (piglit_create_dma_buf(TILE_W / 2, TILE_H / 2, DRM_FORMAT_GR88,
	                         uv_pattern, &buf_uv) != PIGLIT_PASS) {
		piglit_destroy_dma_buf(buf_y);
		return false;
	}

	EGLint img_attrs[] = {
		EGL_WIDTH,                     TILE_W,
		EGL_HEIGHT,                    TILE_H,
		EGL_LINUX_DRM_FOURCC_EXT,      DRM_FORMAT_NV12,
		EGL_DMA_BUF_PLANE0_FD_EXT,     buf_y->fd,
		EGL_DMA_BUF_PLANE0_OFFSET_EXT, (EGLint)buf_y->offset[0],
		EGL_DMA_BUF_PLANE0_PITCH_EXT,  (EGLint)buf_y->stride[0],
		EGL_DMA_BUF_PLANE1_FD_EXT,     buf_uv->fd,
		EGL_DMA_BUF_PLANE1_OFFSET_EXT, (EGLint)buf_uv->offset[0],
		EGL_DMA_BUF_PLANE1_PITCH_EXT,  (EGLint)buf_uv->stride[0],
		EGL_NONE
	};

	*out_img = peglCreateImageKHR(egl_dpy, EGL_NO_CONTEXT,
	                              EGL_LINUX_DMA_BUF_EXT,
	                              (EGLClientBuffer)0, img_attrs);
	*out_buf_y  = buf_y;
	*out_buf_uv = buf_uv;

	if (*out_img == EGL_NO_IMAGE_KHR) {
		fprintf(stderr, "eglCreateImageKHR failed (dual-fd NV12): 0x%x\n",
		        eglGetError());
		return false;
	}

	return true;
}

#ifdef PIGLIT_HAS_GBM_BO_MAP

/*
 * Open a GBM device against the same render node piglit's own dma-buf
 * helper would pick (WAFFLE_GBM_DEVICE if set, else /dev/dri/renderD128).
 * Kept open for the life of the process, matching piglit_drm_dma_buf.c's
 * own convention of never tearing its singleton driver fd back down.
 */
static struct gbm_device *
get_native_gbm_device(void)
{
	static struct gbm_device *gbm;
	static bool tried;
	char *nodename;
	int fd;

	if (tried)
		return gbm;
	tried = true;

	nodename = getenv("WAFFLE_GBM_DEVICE");
	fd = (nodename && strlen(nodename)) ?
		open(nodename, O_RDWR) : open("/dev/dri/renderD128", O_RDWR);
	if (fd < 0)
		return NULL;

	gbm = gbm_create_device(fd);
	return gbm;
}

/*
 * CPU-write "y_data"/"uv_data" straight into the dma-buf backing "bo" at
 * the offsets/strides the driver actually reports for each plane,
 * bracketing the write with DMA_BUF_IOCTL_SYNC so caches are coherent on
 * SoCs where the CPU map isn't automatically coherent with the GPU.
 * Returns the dma-buf fd (owned by the caller) on success.
 */
static bool
write_native_nv12(struct gbm_bo *bo, unsigned w, unsigned h,
                  const unsigned char *y_data, const unsigned char *uv_data,
                  int *out_fd)
{
	uint32_t off0, off1, stride0, stride1;
	size_t map_size, size1;
	void *map;
	unsigned i;
	struct dma_buf_sync sync;
	int fd = gbm_bo_get_fd(bo);

	if (fd < 0)
		return false;

	off0 = gbm_bo_get_offset(bo, 0);
	off1 = gbm_bo_get_offset(bo, 1);
	stride0 = gbm_bo_get_stride_for_plane(bo, 0);
	stride1 = gbm_bo_get_stride_for_plane(bo, 1);

	map_size = (size_t)off0 + (size_t)stride0 * h;
	size1 = (size_t)off1 + (size_t)stride1 * (h / 2);
	if (size1 > map_size)
		map_size = size1;

	map = mmap(NULL, map_size, PROT_WRITE, MAP_SHARED, fd, 0);
	if (map == MAP_FAILED) {
		close(fd);
		return false;
	}

	sync.flags = DMA_BUF_SYNC_START | DMA_BUF_SYNC_WRITE;
	ioctl(fd, DMA_BUF_IOCTL_SYNC, &sync);

	for (i = 0; i < h; i++)
		memcpy((char *)map + off0 + i * stride0, y_data + i * w, w);
	for (i = 0; i < h / 2; i++)
		memcpy((char *)map + off1 + i * stride1, uv_data + i * w, w);

	sync.flags = DMA_BUF_SYNC_END | DMA_BUF_SYNC_WRITE;
	ioctl(fd, DMA_BUF_IOCTL_SYNC, &sync);

	munmap(map, map_size);

	*out_fd = fd;
	return true;
}

#endif /* PIGLIT_HAS_GBM_BO_MAP */

/*
 * Create an NV12 EGLImage backed by a single, genuinely two-plane GBM
 * buffer object (GBM_FORMAT_NV12): both EGL plane descriptors reference
 * the same fd, distinguished only by offset/pitch.
 *
 * Native single-BO NV12 allocation is not mandated by any spec -
 * EGL_EXT_image_dma_buf_import only governs import, not how a buffer was
 * allocated, and GBM treats per-format allocation support as an optional,
 * driver-advertised capability (gbm_device_is_format_supported()) rather
 * than something every driver must implement. So a driver that doesn't
 * advertise it isn't violating anything - that's PIGLIT_SKIP. Only a
 * driver that *does* advertise support but still fails to allocate is a
 * genuine bug - that's PIGLIT_FAIL.
 */
static enum piglit_result
create_single_fd_source(struct gbm_bo **out_bo, int *out_fd,
                        EGLImageKHR *out_img)
{
#ifdef PIGLIT_HAS_GBM_BO_MAP
	struct gbm_device *gbm = get_native_gbm_device();
	struct gbm_bo *bo;
	int fd;
	uint32_t off0, off1, stride0, stride1;
	uint32_t usage = GBM_BO_USE_RENDERING | GBM_BO_USE_LINEAR;
	EGLDisplay egl_dpy = eglGetCurrentDisplay();

	if (!gbm) {
		fprintf(stderr, "failed to open a GBM device for native NV12 "
		        "allocation\n");
		return PIGLIT_SKIP;
	}

	if (!gbm_device_is_format_supported(gbm, GBM_FORMAT_NV12, usage))
		return PIGLIT_SKIP;

	bo = gbm_bo_create(gbm, TILE_W, TILE_H, GBM_FORMAT_NV12, usage);
	if (!bo) {
		fprintf(stderr, "gbm_bo_create(GBM_FORMAT_NV12) failed despite "
		        "gbm_device_is_format_supported() reporting support\n");
		return PIGLIT_FAIL;
	}

	if (!write_native_nv12(bo, TILE_W, TILE_H, y_pattern, uv_pattern,
	                       &fd)) {
		gbm_bo_destroy(bo);
		return PIGLIT_FAIL;
	}

	off0 = gbm_bo_get_offset(bo, 0);
	off1 = gbm_bo_get_offset(bo, 1);
	stride0 = gbm_bo_get_stride_for_plane(bo, 0);
	stride1 = gbm_bo_get_stride_for_plane(bo, 1);

	EGLint img_attrs[] = {
		EGL_WIDTH,                     TILE_W,
		EGL_HEIGHT,                    TILE_H,
		EGL_LINUX_DRM_FOURCC_EXT,      DRM_FORMAT_NV12,
		EGL_DMA_BUF_PLANE0_FD_EXT,     fd,
		EGL_DMA_BUF_PLANE0_OFFSET_EXT, (EGLint)off0,
		EGL_DMA_BUF_PLANE0_PITCH_EXT,  (EGLint)stride0,
		EGL_DMA_BUF_PLANE1_FD_EXT,     fd,
		EGL_DMA_BUF_PLANE1_OFFSET_EXT, (EGLint)off1,
		EGL_DMA_BUF_PLANE1_PITCH_EXT,  (EGLint)stride1,
		EGL_NONE
	};

	*out_img = peglCreateImageKHR(egl_dpy, EGL_NO_CONTEXT,
	                              EGL_LINUX_DMA_BUF_EXT,
	                              (EGLClientBuffer)0, img_attrs);
	*out_bo = bo;
	*out_fd = fd;

	if (*out_img == EGL_NO_IMAGE_KHR) {
		fprintf(stderr, "eglCreateImageKHR failed (single-fd NV12): 0x%x\n",
		        eglGetError());
		return PIGLIT_FAIL;
	}

	return PIGLIT_PASS;
#else
	(void)out_bo;
	(void)out_fd;
	(void)out_img;
	fprintf(stderr, "gbm_bo_map() support not detected at build time\n");
	return PIGLIT_SKIP;
#endif
}

/*
 * Bind "img" as a GL_TEXTURE_EXTERNAL_OES texture and sample it into the
 * window tile starting at x=tile_x. On success, *out_tex receives the
 * texture name (left bound so the caller can defer its destruction).
 */
static bool
sample_into_tile(EGLImageKHR img, GLuint *out_tex, int tile_x)
{
	GLuint tex, prog;

	glGenTextures(1, &tex);
	glBindTexture(GL_TEXTURE_EXTERNAL_OES, tex);
	pglEGLImageTargetTexture2DOES(GL_TEXTURE_EXTERNAL_OES, (GLeglImageOES)img);
	if (!piglit_check_gl_error(GL_NO_ERROR)) {
		glDeleteTextures(1, &tex);
		return false;
	}
	glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

	prog = piglit_build_simple_program(vs_src, fs_src);
	glUseProgram(prog);
	glUniform1i(glGetUniformLocation(prog, "sampler"), 0);

	glViewport(tile_x, 0, TILE_W, TILE_H);
	piglit_draw_rect_tex(-1, -1, 2, 2, 0, 0, 1, 1);

	glDeleteProgram(prog);
	glUseProgram(0);

	*out_tex = tex;
	return true;
}

enum piglit_result
piglit_display(void)
{
	enum piglit_result result = PIGLIT_PASS;
	enum piglit_result single_result;
	struct piglit_dma_buf *dual_y = NULL, *dual_uv = NULL;
	struct gbm_bo *single_bo = NULL;
	int single_fd = -1;
	EGLImageKHR dual_img = EGL_NO_IMAGE_KHR, single_img = EGL_NO_IMAGE_KHR;
	GLuint tex_dual = 0, tex_single = 0;
	EGLDisplay egl_dpy = eglGetCurrentDisplay();
	bool ok;

	if (!create_dual_fd_source(&dual_y, &dual_uv, &dual_img)) {
		result = PIGLIT_SKIP;
		goto cleanup;
	}

	single_result = create_single_fd_source(&single_bo, &single_fd,
	                                        &single_img);
	if (single_result != PIGLIT_PASS) {
		result = single_result;
		goto cleanup;
	}

	/* dual-fd tile occupies [0, TILE_W); single-fd tile [TILE_W, 2*TILE_W) */
	ok = sample_into_tile(dual_img, &tex_dual, 0);
	if (ok)
		ok = sample_into_tile(single_img, &tex_single, TILE_W);

	if (!ok) {
		result = PIGLIT_FAIL;
	} else {
		/*
		 * The point of this test: sampling a single-BO NV12 buffer must
		 * produce the same result as sampling the long-standing dual-BO
		 * layout, whatever that result is.
		 */
		if (!piglit_probe_rects_equal(0, 0, TILE_W, 0, TILE_W, TILE_H,
		                              GL_RGB))
			result = PIGLIT_FAIL;
	}

cleanup:
	if (tex_dual)
		glDeleteTextures(1, &tex_dual);
	if (tex_single)
		glDeleteTextures(1, &tex_single);
	if (dual_img != EGL_NO_IMAGE_KHR)
		peglDestroyImageKHR(egl_dpy, dual_img);
	if (single_img != EGL_NO_IMAGE_KHR)
		peglDestroyImageKHR(egl_dpy, single_img);
	piglit_destroy_dma_buf(dual_y);
	piglit_destroy_dma_buf(dual_uv);
#ifdef PIGLIT_HAS_GBM_BO_MAP
	if (single_fd >= 0)
		close(single_fd);
	if (single_bo)
		gbm_bo_destroy(single_bo);
#endif

	piglit_present_results();
	return result;
}

void
piglit_init(int argc, char **argv)
{
	EGLDisplay egl_dpy = eglGetCurrentDisplay();
	if (!egl_dpy)
		piglit_report_result(PIGLIT_SKIP);

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
