/*
 * Copyright © 2020 Intel Corporation
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

#include "piglit-util-compressed-grays.h"
#include "sample_common.h"
#include "image_common.h"
#include "drm-uapi/drm_fourcc.h"

#include <inttypes.h>

#define PRIMARY_REFERENCE_MOD DRM_FORMAT_MOD_LINEAR

#define W 257
#define H 257
#define MAX_PLANES 4

/**
 * @file modifiers.c
 *
 * Test various operations on imported dmabufs with supported modifiers.
 */

PIGLIT_GL_TEST_CONFIG_BEGIN

	config.supports_gl_es_version = 20;
	config.window_visual = PIGLIT_GL_VISUAL_RGBA;
	config.window_height = 240;
	config.window_width = 960;

PIGLIT_GL_TEST_CONFIG_END

PFNEGLEXPORTDMABUFIMAGEQUERYMESAPROC dmabuf_query;
PFNEGLEXPORTDMABUFIMAGEMESAPROC dmabuf_export;
PFNEGLQUERYDMABUFFORMATSEXTPROC dmabuf_query_formats;
PFNEGLQUERYDMABUFMODIFIERSEXTPROC dmabuf_query_modifiers;

struct dma_buf_info {
	int fd;
	uint32_t w;
	uint32_t h;
	uint32_t n_planes;
	uint32_t stride[4]; /* pitch for each plane */
	uint32_t offset[4]; /* offset of each plane */
};

static void
delete_tex(GLuint *tex)
{
	if (*tex != 0) {
		glDeleteTextures(1, tex);
		*tex = 0;
	}
}

static void
destroy_img(EGLImageKHR *img)
{
	if (*img != EGL_NO_IMAGE_KHR) {
		eglDestroyImageKHR(eglGetCurrentDisplay(), *img);
		*img = EGL_NO_IMAGE_KHR;
	}
}

static int
cpp_for_fourcc(uint32_t format)
{
	switch (format) {
	case DRM_FORMAT_R8:
		return 1;
	case DRM_FORMAT_R16:
		return 2;
	case DRM_FORMAT_XBGR8888:
	case DRM_FORMAT_XRGB8888:
	case DRM_FORMAT_ABGR8888:
	case DRM_FORMAT_ARGB8888:
		return 4;
	default:
		fprintf(stderr, "invalid fourcc: %.4s\n", (char *)&format);
		return 0;
	}
}

struct modifier_name {
	uint64_t mod;
	const char *name;
};

#define CASE(mod) { mod, #mod }

static const struct modifier_name known_modifiers[] = {
	CASE(DRM_FORMAT_MOD_LINEAR),
	CASE(I915_FORMAT_MOD_X_TILED),
	CASE(I915_FORMAT_MOD_Y_TILED),
	CASE(I915_FORMAT_MOD_Yf_TILED),
	CASE(I915_FORMAT_MOD_Y_TILED_CCS),
	CASE(I915_FORMAT_MOD_Yf_TILED_CCS),
	CASE(I915_FORMAT_MOD_Y_TILED_GEN12_RC_CCS),
	CASE(I915_FORMAT_MOD_Y_TILED_GEN12_MC_CCS),
	CASE(I915_FORMAT_MOD_Y_TILED_GEN12_RC_CCS_CC),
	CASE(DRM_FORMAT_MOD_SAMSUNG_64_32_TILE),
	CASE(DRM_FORMAT_MOD_SAMSUNG_16_16_TILE),
	CASE(DRM_FORMAT_MOD_QCOM_COMPRESSED),
	CASE(DRM_FORMAT_MOD_QCOM_TILED3),
	CASE(DRM_FORMAT_MOD_VIVANTE_TILED),
	CASE(DRM_FORMAT_MOD_VIVANTE_SUPER_TILED),
	CASE(DRM_FORMAT_MOD_VIVANTE_SPLIT_TILED),
	CASE(DRM_FORMAT_MOD_VIVANTE_SPLIT_SUPER_TILED),
	CASE(DRM_FORMAT_MOD_NVIDIA_TEGRA_TILED),
	CASE(DRM_FORMAT_MOD_NVIDIA_16BX2_BLOCK_ONE_GOB),
	CASE(DRM_FORMAT_MOD_NVIDIA_16BX2_BLOCK_TWO_GOB),
	CASE(DRM_FORMAT_MOD_NVIDIA_16BX2_BLOCK_FOUR_GOB),
	CASE(DRM_FORMAT_MOD_NVIDIA_16BX2_BLOCK_EIGHT_GOB),
	CASE(DRM_FORMAT_MOD_NVIDIA_16BX2_BLOCK_SIXTEEN_GOB),
	CASE(DRM_FORMAT_MOD_NVIDIA_16BX2_BLOCK_THIRTYTWO_GOB),
	CASE(DRM_FORMAT_MOD_BROADCOM_VC4_T_TILED),
	CASE(DRM_FORMAT_MOD_BROADCOM_SAND32),
	CASE(DRM_FORMAT_MOD_BROADCOM_SAND64),
	CASE(DRM_FORMAT_MOD_BROADCOM_SAND128),
	CASE(DRM_FORMAT_MOD_BROADCOM_SAND256),
	CASE(DRM_FORMAT_MOD_ALLWINNER_TILED),
};
#undef CASE

const char *
modifier_str(EGLuint64KHR mod)
{
	for (int i = 0; i < ARRAY_SIZE(known_modifiers); i++) {
		if (known_modifiers[i].mod == mod)
			return known_modifiers[i].name;
	}
	return NULL;
}

static EGLuint64KHR
str_to_modifier(const char *mod)
{
	for (int i = 0; i < ARRAY_SIZE(known_modifiers); i++) {
		if (strcmp(known_modifiers[i].name, mod) == 0)
			return known_modifiers[i].mod;
	}
	return DRM_FORMAT_MOD_INVALID;
}

/* Shorten fourcc "strings" (e.g., "R8  " -> "R8") */
static int
format_no_space(int fmt)
{
	int fmt_no_space = fmt;
	char *fmt_str = (char *)&fmt_no_space;
	for (int i = 0; i < 4; i++)
		if (fmt_str[i] == ' ')
			fmt_str[i] = '\0';

	return fmt_no_space;
}

static void
report_result(enum piglit_result res, int fmt, EGLuint64KHR mod, bool autogen,
	      const char *fn)
{
	const char *mod_str = modifier_str(mod);
	const int fmt_no_space = format_no_space(fmt);
	const char *gen_str = autogen ? "autogen" : "pregen";

	if (mod_str)
		piglit_report_subtest_result(res, "%s-%.4s-%s-%s",
					     gen_str, (char*)&fmt_no_space,
					     mod_str, fn);
	else
		piglit_report_subtest_result(res, "%s-%.4s-0x%"PRIx64"-%s",
					     gen_str, (char*)&fmt_no_space,
					     mod, fn);
}

static enum piglit_result
egl_image_for_dma_buf_fd_mod(struct dma_buf_info *buf, int fourcc,
			     EGLImageKHR *out_img, EGLuint64KHR modifier)
{
#define DMA_BUF_ATTRS \
	EGL_IMAGE_PRESERVED, EGL_TRUE, \
	EGL_WIDTH, buf->w, \
	EGL_HEIGHT, buf->h, \
	EGL_LINUX_DRM_FOURCC_EXT, fourcc
#define PLANE_ATTRS \
	EGL_NONE, EGL_NONE, \
	EGL_NONE, EGL_NONE, \
	EGL_NONE, EGL_NONE, \
	EGL_NONE, EGL_NONE, \
	EGL_NONE, EGL_NONE
#define LIST_SIZE(type, list) ARRAY_SIZE(((type []) { list }))
#define DMA_BUF_ATTRS_LEN LIST_SIZE(EGLint, DMA_BUF_ATTRS)
#define PLANE_ATTRS_LEN	LIST_SIZE(EGLint, PLANE_ATTRS)
#define FILL_PLANE(attr, buf, fourcc, mod, p) \
	if (p < buf->n_planes) { \
		const EGLint plane_attr[PLANE_ATTRS_LEN] = { \
		       EGL_DMA_BUF_PLANE ## p ## _FD_EXT, \
		       buf->fd, \
		       EGL_DMA_BUF_PLANE ## p ## _OFFSET_EXT, \
		       buf->offset[p], \
		       EGL_DMA_BUF_PLANE ## p ## _PITCH_EXT, \
		       buf->stride[p], \
		       EGL_DMA_BUF_PLANE ## p ## _MODIFIER_LO_EXT, \
		       mod, \
		       EGL_DMA_BUF_PLANE ## p ## _MODIFIER_HI_EXT, \
		       mod >> 32, \
		}; \
		const unsigned plane_attr_offset = \
			DMA_BUF_ATTRS_LEN + PLANE_ATTRS_LEN * p; \
		assert(plane_attr_offset + PLANE_ATTRS_LEN < \
			ARRAY_SIZE(attr)); \
		memcpy(attr + plane_attr_offset, plane_attr, \
			sizeof(plane_attr)); \
	}

	EGLint attr[] = {
		DMA_BUF_ATTRS,
		PLANE_ATTRS,
		PLANE_ATTRS,
		PLANE_ATTRS,
		PLANE_ATTRS,
		EGL_NONE,
        };
	FILL_PLANE(attr, buf, fourcc, modifier, 0)
	FILL_PLANE(attr, buf, fourcc, modifier, 1)
	FILL_PLANE(attr, buf, fourcc, modifier, 2)
	FILL_PLANE(attr, buf, fourcc, modifier, 3)

#undef FILL_PLANE
#undef PLANE_ATTRS_LEN
#undef DMA_BUF_ATTRS_LEN
#undef LIST_SIZE
#undef PLANE_ATTRS
#undef DMA_BUF_ATTRS

	EGLImageKHR img = eglCreateImageKHR(eglGetCurrentDisplay(),
					    EGL_NO_CONTEXT,
					    EGL_LINUX_DMA_BUF_EXT,
					    (EGLClientBuffer)0, attr);
	EGLint error = eglGetError();

	/* EGL may not support the format, this is not an error. */
	if (!img && error == EGL_BAD_MATCH)
		return PIGLIT_SKIP;

	if (error != EGL_SUCCESS) {
		printf("eglCreateImageKHR() failed: %s 0x%x\n",
		       piglit_get_egl_error_name(error), error);
		return PIGLIT_FAIL;
	}

	*out_img = img;
	return PIGLIT_PASS;
}

/* This function can be implemented locally to make this test load files
 * that contain or point to dmabuf data. It's intended to be left
 * unimplemented by default.
 */
static bool
load_dma_buf_from_file(uint32_t format, EGLuint64KHR modifier,
		       struct dma_buf_info *buf)
{
	return false;
}

static bool
create_dma_buf(uint32_t format, EGLuint64KHR modifier,
	       struct dma_buf_info *buf)
{
	/* Use 4KB worth of interesting data to initialize the dmabuf. */
	assert(sizeof(piglit_fxt1_grayscale_blocks) == 4 * 1024);
	const char *src_data = (const char*)piglit_fxt1_grayscale_blocks;
	const int num_pixels = sizeof(piglit_fxt1_grayscale_blocks) /
			       cpp_for_fourcc(format);
	const int dim = sqrt((double)num_pixels);

	struct piglit_dma_buf *drm_buf = NULL;
	enum piglit_result result =
		piglit_drm_create_dma_buf_modifiers(dim, dim, format,
						    modifier, src_data,
						    &drm_buf);

	if (result != PIGLIT_PASS) {
		piglit_drm_destroy_dma_buf(drm_buf);
		return false;
	}

	buf->n_planes = drm_buf->n_planes;
	assert(buf->n_planes <= ARRAY_SIZE(drm_buf->offset));
	for (int i = 0; i < buf->n_planes; i++) {
		buf->offset[i] = drm_buf->offset[i];
		buf->stride[i] = drm_buf->stride[i];
	}
	buf->fd = drm_buf->fd;
	buf->w = drm_buf->w;
	buf->h = drm_buf->h;

	piglit_drm_destroy_dma_buf(drm_buf);
	return true;
}

static bool
get_dma_buf(uint32_t format, EGLuint64KHR modifier, bool external_only,
	    struct dma_buf_info *buf, bool autogen)
{
	if (autogen) {
		/* GL drivers are generally unable to create external-only
		 * images. We can load them from external sources, however.
		 */
		if (external_only)
			return false;

		return create_dma_buf(format, modifier, buf);
	} else {
		return load_dma_buf_from_file(format, modifier, buf);
	}
}

static int
gl_cpp_for_fourcc(uint32_t format)
{
	switch (format) {
	case DRM_FORMAT_R8:
		return 1;
	case DRM_FORMAT_R16:
		return 2;
	case DRM_FORMAT_XRGB8888:
	case DRM_FORMAT_XBGR8888:
		return 3;
	case DRM_FORMAT_ARGB8888:
	case DRM_FORMAT_ABGR8888:
		return 4;
	default:
		fprintf(stderr, "invalid fourcc: %.4s\n", (char *)&format);
		return 0;
	}
}

struct view_class
{
	uint8_t cpp;
	uint8_t num_formats;
	GLenum formats[15];
} static const fmt_views[] = {
	{
		.cpp = 32 / 8,
		.num_formats = 15,
		.formats = {
				GL_RG16F, GL_R11F_G11F_B10F, GL_R32F,
				GL_RGB10_A2UI, GL_RGBA8UI, GL_RG16UI,
				GL_R32UI, GL_RGBA8I, GL_RG16I, GL_R32I,
				GL_RGB10_A2, GL_RGBA8, GL_RGBA8_SNORM,
				GL_SRGB8_ALPHA8, GL_RGB9_E5
		}
	},
	{
		.cpp = 24 / 8,
		.num_formats = 5,
		.formats = {
				GL_RGB8UI, GL_RGB8I, GL_RGB8, GL_RGB8_SNORM,
				GL_SRGB8
		}
	},
	{
		.cpp = 16 / 8,
		.num_formats = 7,
		.formats = {
				GL_R16F, GL_RG8UI, GL_R16UI, GL_RG8I, GL_R16I,
				GL_RG8, GL_RG8_SNORM
		}
	},
	{
		.cpp = 8 / 8,
		.num_formats = 4,
		.formats = {
				GL_R8UI, GL_R8I, GL_R8, GL_R8_SNORM
		}
	},
};

static GLenum
clear_color_type(GLenum format)
{
	switch (format) {
	default:
		assert(!"unhandled clear format");
	case GL_RGBA8I:
	case GL_RG16I:
	case GL_R32I:
	case GL_RG8I:
	case GL_R16I:
	case GL_R8I:
		return GL_INT;
	case GL_RGB10_A2UI:
	case GL_RGBA8UI:
	case GL_RG16UI:
	case GL_R32UI:
	case GL_RG8UI:
	case GL_R16UI:
	case GL_R8UI:
		return GL_UNSIGNED_INT;
	case GL_RG16F:
	case GL_R11F_G11F_B10F:
	case GL_R32F:
	case GL_RGB10_A2:
	case GL_SRGB8_ALPHA8:
	case GL_RGB9_E5:
	case GL_RGBA8:
	case GL_RGB8:
	case GL_RGBA8_SNORM:
	case GL_R16F:
	case GL_RG8:
	case GL_RG8_SNORM:
	case GL_R8:
	case GL_R8_SNORM:
		return GL_FLOAT;
	}
}

static void
clear_buffer(GLenum type)
{
	GLuint color_uint[] = {1, 1, 1, 1};
	GLint color_int[] = {1, 1, 1, 1};
	GLfloat color_float[] = {0.25 , 0.125, 0.0675, 0.25};
	switch (type) {
	case GL_INT:
		glClearBufferiv(GL_COLOR, 0, color_int);
		return;
	case GL_UNSIGNED_INT:
		glClearBufferuiv(GL_COLOR, 0, color_uint);
		return;
	case GL_FLOAT:
		glClearBufferfv(GL_COLOR, 0, color_float);
		return;
	default:
		assert(!"unhandled clear type");
	}
}

static bool
color_renderable(GLenum format)
{
	return format != GL_RGB9_E5 &&
	       format != GL_RGB8UI &&
	       format != GL_RGB8I &&
	       format != GL_RGB8_SNORM &&
	       format != GL_SRGB8;
}

static bool
sample_compare(GLuint tex, GLuint tex_ref, bool external_only)
{
	const int piglit_width_half = piglit_width / 2;
	sample_tex(tex, 0, 0, piglit_width_half, piglit_height,
		   external_only);
	sample_tex(tex_ref, piglit_width_half, 0, piglit_width_half,
		   piglit_height, external_only);
	return piglit_probe_rects_equal(0, 0, piglit_width_half, 0,
					piglit_width_half, piglit_height,
					GL_RGBA);
}
static bool
wrapped_sample_compare(GLuint tex, GLuint tex_ref, GLenum format)
{
	return sample_compare(tex, tex_ref, false);
}

static bool
clear_textures(GLuint tex, GLuint tex_ref, GLenum type)
{
	GLuint fbo;
	glGenFramebuffers(1, &fbo);
	glViewport(0, 0, piglit_width, piglit_height);
	glBindFramebuffer(GL_FRAMEBUFFER, fbo);

	GLuint textures[] = {tex, tex_ref};
	for (int i = 0; i < ARRAY_SIZE(textures); i++) {
		glBindTexture(GL_TEXTURE_2D, textures[i]);
		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
				       GL_TEXTURE_2D, textures[i], 0);
		clear_buffer(type);
		glBindTexture(GL_TEXTURE_2D, 0);
	}

	glBindFramebuffer(GL_FRAMEBUFFER, piglit_winsys_fbo);
	glDeleteFramebuffers(1, &fbo);
	return sample_compare(tex, tex_ref, false);
}

static bool
wrapped_clear_textures(GLuint tex, GLuint tex_ref, GLenum format)
{
	GLenum type = clear_color_type(format);
	return clear_textures(tex, tex_ref, type);
}

static enum piglit_result
test_export(EGLImageKHR img, struct dma_buf_info *buf,
	    int format, EGLuint64KHR modifier)
{
	/* Export the buffer and query properties. */
	int prop_fourcc = -1;
	int num_planes = -1;

	/* Query the image properties, verify fourcc and num planes. */
	EGLDisplay egl_dpy = eglGetCurrentDisplay();
	if (!dmabuf_query(egl_dpy, img, &prop_fourcc, &num_planes, NULL)) {
		fprintf(stderr, "export dmabuf image query failed!\n");
		return PIGLIT_FAIL;
	}

	if (!piglit_check_egl_error(EGL_SUCCESS)) {
		fprintf(stderr, "image export failed!\n");
		return PIGLIT_FAIL;
	}

	if (prop_fourcc != format) {
		fprintf(stderr,
			"fourcc mismatch, got %.4s expected %.4s\n",
			(char *)&prop_fourcc, (char *)&format);
		return PIGLIT_FAIL;
	}

	if (num_planes != buf->n_planes) {
		fprintf(stderr, "planes mismatch, got %d expected %d\n",
			num_planes, buf->n_planes);
		return PIGLIT_FAIL;
	}


	/* Export the image, verify success. */
	EGLint strides[MAX_PLANES] = {0,};
	EGLint offsets[MAX_PLANES] = {0,};
	if (!dmabuf_export(egl_dpy, img, NULL, strides, offsets)) {
		fprintf(stderr, "image export failed!\n");
		return PIGLIT_FAIL;
	}

	if (!piglit_check_egl_error(EGL_SUCCESS)) {
		fprintf(stderr, "image export failed!\n");
		return PIGLIT_FAIL;
	}

	/* Check if we got an expected stride and offset for each plane.
	 * The export spec doesn't require that the output stride and offset
	 * match the input, so don't fail if they're different. At this time
	 * though, drivers are expected to return the same values.
	 */
	for (uint32_t i = 0; i < num_planes; i++) {
		if (strides[i] != buf->stride[i] ||
		    offsets[i] != buf->offset[i]) {
			fprintf(stderr, "suspect data from driver: "
				"stride %d (expected %d)  "
				"offset %d (expected %d)\n",
				strides[i], buf->stride[i], offsets[i],
				buf->offset[i]);

			/* TODO - propagate this warning up the stack (?) */

			return PIGLIT_WARN;
		}
	}

	return PIGLIT_PASS;
}

bool (*view_fn)(GLuint tex, GLuint tex_ref, GLenum format);

static bool
test_views(bool (*view_fn)(GLuint tex, GLuint tex_ref, GLenum format),
	   unsigned cpp, GLuint tex, GLuint tex_ref, bool render_test)
{
	for (int i = 0; i < ARRAY_SIZE(fmt_views); i++) {
		if (fmt_views[i].cpp != cpp)
			continue;
		for (int j = 0; j < fmt_views[i].num_formats; j++) {
			const GLenum format = fmt_views[i].formats[j];
			if (render_test && !color_renderable(format))
				continue;

			/* Create the texture views. */
			GLuint view, view_ref;
			glGenTextures(1, &view);
			glGenTextures(1, &view_ref);
			glTextureViewOES(view, GL_TEXTURE_2D, tex,
					 format, 0, 1, 0, 1);
			glTextureViewOES(view_ref, GL_TEXTURE_2D, tex_ref,
					 format, 0, 1, 0, 1);

			/* Run the view test. */
			const bool pass = view_fn(view, view_ref, format);

			glDeleteTextures(1, &view);
			glDeleteTextures(1, &view_ref);

			if (!pass)
				return false;
		}
	}

	return true;
}

static bool
test_gl_advanced(GLuint tex, GLuint tex_ref, int fourcc)
{
	const unsigned cpp = gl_cpp_for_fourcc(fourcc);

	/* Attempt various operations using the original texture or a
	 * textureview from the corresponding view class.
	 */
	piglit_logd("Testing view sampling");
	if (!test_views(wrapped_sample_compare, cpp, tex, tex_ref, false))
		return false;

	/* DRI formats are non-integer, clear with floating point values. */
	piglit_logd("Testing clears");
	if (!clear_textures(tex, tex_ref, GL_FLOAT))
		return false;

	piglit_logd("Testing view clears");
	if (!test_views(wrapped_clear_textures, cpp, tex, tex_ref, true))
		return false;

	return true;
}

static enum piglit_result
clear_reimport(uint32_t format, EGLuint64KHR reference_modifier,
	       EGLuint64KHR modifier, EGLBoolean external_only, bool autogen)
{
	GLuint tex = 0;
	GLuint tex_ref = 0;
	EGLImageKHR img = EGL_NO_IMAGE_KHR;
	EGLImageKHR img_ref = EGL_NO_IMAGE_KHR;
	struct dma_buf_info buf = { .fd = -1 };
	struct dma_buf_info buf_ref = { .fd = -1 };
	enum piglit_result res = PIGLIT_SKIP;

	if (external_only) {
		piglit_logd("External only format + modifier");
		return PIGLIT_SKIP;
	}

	/* Create dma_buf_info. */
	if (!get_dma_buf(format, modifier, external_only, &buf, autogen)) {
		piglit_logd("No data found");
		return PIGLIT_SKIP;
	}

	/* Create reference dma_buf_info. */
	if (!get_dma_buf(format, reference_modifier, external_only,
			  &buf_ref, autogen)) {
		piglit_logd("No data found");
		goto destroy;
	}

	/* Perform EGL testing */
	piglit_logd("Testing import");
	res = egl_image_for_dma_buf_fd_mod(&buf, format, &img, modifier);
	if (res != PIGLIT_PASS)
		goto destroy;

	res = egl_image_for_dma_buf_fd_mod(&buf_ref, format, &img_ref,
					   reference_modifier);
	if (res != PIGLIT_PASS)
		goto destroy;

	/* Create textures */
	res = texture_for_egl_image(img, &tex, external_only);
	if (res != PIGLIT_PASS)
		goto destroy;

	res = texture_for_egl_image(img_ref, &tex_ref, external_only);
	if (res != PIGLIT_PASS)
		goto destroy;

	piglit_logd("Testing clear, reimport, sample");
	if (!clear_textures(tex, tex_ref, GL_FLOAT)) {
		res = PIGLIT_FAIL;
		goto destroy;
	}

	glFinish();
	delete_tex(&tex);
	delete_tex(&tex_ref);
	destroy_img(&img);
	destroy_img(&img_ref);

	res = egl_image_for_dma_buf_fd_mod(&buf, format, &img, modifier);
	if (res != PIGLIT_PASS)
		goto destroy;

	res = egl_image_for_dma_buf_fd_mod(&buf_ref, format, &img_ref,
					   reference_modifier);
	if (res != PIGLIT_PASS)
		goto destroy;

	res = texture_for_egl_image(img, &tex, external_only);
	if (res != PIGLIT_PASS)
		goto destroy;

	res = texture_for_egl_image(img_ref, &tex_ref, external_only);
	if (res != PIGLIT_PASS)
		goto destroy;

	if (!sample_compare(tex, tex_ref, external_only)) {
		res = PIGLIT_FAIL;
		goto destroy;
	}

destroy:
	delete_tex(&tex);
	delete_tex(&tex_ref);
	destroy_img(&img);
	destroy_img(&img_ref);
	close(buf.fd);
	close(buf_ref.fd);

	if (res == PIGLIT_FAIL)
		piglit_present_results();

	return res;
}

static enum piglit_result
stress_test(uint32_t format, EGLuint64KHR reference_modifier,
	    EGLuint64KHR modifier, EGLBoolean external_only, bool autogen)
{
	GLuint tex = 0;
	GLuint tex_ref = 0;
	EGLImageKHR img = EGL_NO_IMAGE_KHR;
	EGLImageKHR img_ref = EGL_NO_IMAGE_KHR;
	struct dma_buf_info buf = { .fd = -1 };
	struct dma_buf_info buf_ref = { .fd = -1 };
	enum piglit_result res = PIGLIT_SKIP;

	/* Create dma_buf_info structs. */
	if (!get_dma_buf(format, modifier, external_only, &buf, autogen)) {
		piglit_logd("No data found");
		return PIGLIT_SKIP;
	}

	if (!get_dma_buf(format, reference_modifier, external_only,
			  &buf_ref, autogen)) {
		piglit_logd("No data found");
		goto destroy;
	}

	/* Perform EGL testing */
	piglit_logd("Testing import");
	res = egl_image_for_dma_buf_fd_mod(&buf, format, &img, modifier);
	if (res != PIGLIT_PASS)
		goto destroy;

	res = egl_image_for_dma_buf_fd_mod(&buf_ref, format, &img_ref,
					   reference_modifier);
	if (res != PIGLIT_PASS)
		goto destroy;

	piglit_logd("Testing export");
	res = test_export(img, &buf, format, modifier);
	if (res == PIGLIT_FAIL)
		goto destroy;

	/* Create textures */
	res = texture_for_egl_image(img, &tex, external_only);
	if (res != PIGLIT_PASS)
		goto destroy;

	res = texture_for_egl_image(img_ref, &tex_ref, external_only);
	if (res != PIGLIT_PASS)
		goto destroy;

	/* Perform GL testing */
	piglit_logd("Testing sampling");
	if (!sample_compare(tex, tex_ref, external_only)) {
		res = PIGLIT_FAIL;
		goto destroy;
	}

	/* Display the result of sampling the test and reference textures. */
	piglit_present_results();

	if (!external_only && !test_gl_advanced(tex, tex_ref, format)) {
		res = PIGLIT_FAIL;
		goto destroy;
	}

destroy:
	delete_tex(&tex);
	delete_tex(&tex_ref);
	destroy_img(&img);
	destroy_img(&img_ref);
	close(buf.fd);
	close(buf_ref.fd);

	if (res == PIGLIT_FAIL)
		piglit_present_results();

	return res;
}

static enum piglit_result
modifier_test(uint32_t format, EGLuint64KHR modifier, bool external_only,
	      bool autogen)
{
	GLuint tex = 0;
	EGLImageKHR img = EGL_NO_IMAGE_KHR;
	struct dma_buf_info buf = { .fd = -1 };
	enum piglit_result res = PIGLIT_SKIP;

	/* Create dma_buf_info. */
	if (!get_dma_buf(format, modifier, external_only, &buf, autogen)) {
		piglit_logd("No data found");
		return PIGLIT_SKIP;
	}

	/* Create EGLImage. */
	res = egl_image_for_dma_buf_fd_mod(&buf, format, &img, modifier);

	if (!img) {
		/* Close the descriptor also, EGL does not have ownership */
		close(buf.fd);
	}

	if (res != PIGLIT_PASS) {
		piglit_logd("Failed EGL import");
		goto destroy;
	}

	res = texture_for_egl_image(img, &tex, true);
	if (res != PIGLIT_PASS) {
		piglit_logd("Failed GL import");
		goto destroy;
	}

	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	/* TODO - verify results (?) */

	sample_tex(tex, 0, 0, W, H, true);

destroy:
	delete_tex(&tex);
	destroy_img(&img);
	close(buf.fd);
	return res;
}

static int arg_fmt = DRM_FORMAT_INVALID;
static EGLuint64KHR arg_mod = DRM_FORMAT_MOD_INVALID;

static bool
skip_format(uint32_t format)
{
	if (arg_fmt != DRM_FORMAT_INVALID && arg_fmt != format)
		return true;

	switch (format) {
	case DRM_FORMAT_R8:
	case DRM_FORMAT_R16:
	case DRM_FORMAT_XRGB8888:
	case DRM_FORMAT_XBGR8888:
	case DRM_FORMAT_ARGB8888:
	case DRM_FORMAT_ABGR8888:
	case DRM_FORMAT_YUYV:
	case DRM_FORMAT_UYVY:
	case DRM_FORMAT_P010:
	case DRM_FORMAT_P012:
	case DRM_FORMAT_P016:
	case DRM_FORMAT_NV12:
		return false;
	default:
		return true;
	}
}

static bool
skip_modifier(EGLuint64KHR mod)
{
	if (arg_mod != DRM_FORMAT_MOD_INVALID && arg_mod != mod)
		return true;

	return false;
}

enum piglit_result
piglit_display(void)
{
	EGLDisplay egl_dpy = eglGetCurrentDisplay();

#define MAX_FORMATS 256
#define MAX_MODIFIERS 256

	/* First query all available formats. */
	EGLint formats[MAX_FORMATS];
	EGLuint64KHR modifiers[MAX_MODIFIERS];
	EGLBoolean external_only[MAX_MODIFIERS];
	EGLint num_formats = 0;
	EGLint num_modifiers = 0;

	dmabuf_query_formats(egl_dpy, MAX_FORMATS, formats, &num_formats);

	printf("found %d supported formats\n", num_formats);

	enum piglit_result result = PIGLIT_SKIP;

	bool autogen[] = { false, true };

	for (unsigned g = 0; g < ARRAY_SIZE(autogen); g++) {
		printf(autogen[g] ?
		       "\n\nTesting with autogenerated dmabufs\n\n" :
		       "\n\nTesting with pregenerated binaries\n\n");
		for (unsigned i = 0; i < num_formats; i++) {
			if (skip_format(formats[i]))
				continue;

			int32_t fmt = formats[i];

			dmabuf_query_modifiers(egl_dpy, fmt, MAX_MODIFIERS,
					       modifiers, external_only,
					       &num_modifiers);

			printf("format %.4s has %d supported modifiers\n",
			       (char *)&fmt, num_modifiers);

			for (unsigned j = 0; j < num_modifiers; j++) {
				if (skip_modifier(modifiers[j]))
					continue;

				enum piglit_result r;
				r = modifier_test(fmt, modifiers[j],
						  external_only[j],
						  autogen[g]);
				report_result(r, fmt, modifiers[j],
					      autogen[g], "modifiers_test");
				piglit_merge_result(&result, r);

				r = stress_test(fmt, PRIMARY_REFERENCE_MOD,
						modifiers[j],
						external_only[j],
						autogen[g]);
				report_result(r, fmt, modifiers[j],
					      autogen[g], "stress_test");
				piglit_merge_result(&result, r);

				r = clear_reimport(fmt, PRIMARY_REFERENCE_MOD,
						   modifiers[j],
						   external_only[j],
						   autogen[g]);
				report_result(r, fmt, modifiers[j],
					      autogen[g], "clear_reimport");
				piglit_merge_result(&result, r);
			}
		}
        }

	return result;
}

void
piglit_init(int argc, char **argv)
{
	for (int i = 1; i < argc; i++) {

		/* Init with spaces for codes with less than four chars. */
		int tmp_fmt = fourcc_code(' ',' ',' ',' ');
		if (sscanf(argv[i], "-fmt=%4c", (char *)&tmp_fmt) > 0) {

			arg_fmt = tmp_fmt;

			/* The invalid format is reserved for this test */
			assert(arg_fmt != DRM_FORMAT_INVALID);
			continue;
		}

		char mod_str[4096] = {0};
		if (sscanf(argv[i], "-mod=%4096s", mod_str) > 0) {
			arg_mod = str_to_modifier(mod_str);

			/* The invalid modifier is reserved for this test */
			assert(arg_mod != DRM_FORMAT_MOD_INVALID);
			continue;
		}

		fprintf(stderr,"e.g., %s [-fmt=AR24] "
			       "[-mod=DRM_FORMAT_MOD_LINEAR]\n", argv[0]);
		piglit_report_result(PIGLIT_FAIL);
	}

	EGLDisplay egl_dpy = eglGetCurrentDisplay();

	piglit_require_egl_extension(
			egl_dpy, "EGL_EXT_image_dma_buf_import_modifiers");
	piglit_require_egl_extension(
			egl_dpy, "EGL_MESA_image_dma_buf_export");
	piglit_require_extension("GL_EXT_EGL_image_storage");
	piglit_require_extension("GL_OES_texture_view");

	dmabuf_query =
		(PFNEGLEXPORTDMABUFIMAGEQUERYMESAPROC) eglGetProcAddress(
			"eglExportDMABUFImageQueryMESA");

	dmabuf_export =
		(PFNEGLEXPORTDMABUFIMAGEMESAPROC) eglGetProcAddress(
			"eglExportDMABUFImageMESA");

	if (!dmabuf_query || !dmabuf_export) {
		fprintf(stderr, "could not find extension entrypoints\n");
		piglit_report_result(PIGLIT_FAIL);
	}

	dmabuf_query_formats =
		(PFNEGLQUERYDMABUFFORMATSEXTPROC) eglGetProcAddress(
			"eglQueryDmaBufFormatsEXT");

	dmabuf_query_modifiers =
		(PFNEGLQUERYDMABUFMODIFIERSEXTPROC) eglGetProcAddress(
			"eglQueryDmaBufModifiersEXT");

	if (!dmabuf_query_formats || !dmabuf_query_modifiers) {
		fprintf(stderr, "could not find extension entrypoints\n");
		piglit_report_result(PIGLIT_FAIL);
	}
}
