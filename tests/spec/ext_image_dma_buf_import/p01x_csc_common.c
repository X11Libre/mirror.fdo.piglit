/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: MIT
 */

/**
 * @file p01x_csc_common.c
 *
 * Shared code for the sample_p010-csc and sample_p012-csc tests.
 *
 * Fill an 8x4 image of 2x2 blocks, import it as a dma-buf with the
 * requested EGL_YUV_COLOR_SPACE_HINT_EXT / EGL_SAMPLE_RANGE_HINT_EXT,
 * sample it through samplerExternalOES into an RGB10_A2 FBO, and compare
 * every pixel against the ITU-T H.273 / BT.2100 equations for the
 * format's bit depth, rounded to 10 bits.
 *
 * \author Tupili Krishna Gowtham Reddy <tupilikr@qti.qualcomm.com>
 */

#include <endian.h>
#include <errno.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "piglit-util-egl.h"
#include "piglit-util-gl.h"
#include "piglit-framework-gl/piglit_drm_dma_buf.h"
#include "image_common.h"
#include "sample_common.h"
#include "p01x_csc_common.h"

#ifndef __BIG_ENDIAN__
/* Assume __LITTLE_ENDIAN__ */
#  ifndef HAVE_HTOLE16
#    define htole16(x) (uint16_t)(x)
#    define HAVE_HTOLE16
#  endif
#endif

#define W 8
#define H 4

enum cs { CS_601, CS_709, CS_2020 };

static enum cs color_space = CS_709;
static bool full_range = false;
static int tolerance = 2;

static NORETURN void
print_usage(const char *name)
{
	fprintf(stderr, "usage: %s [-cs=601|709|2020] "
		"[-range=narrow|full] [-tolerance=N]\n", name);
	piglit_report_result(PIGLIT_SKIP);
}

static bool
parse_tolerance(const char *s, int *out)
{
	char *end;
	long v;

	errno = 0;
	v = strtol(s, &end, 10);
	if (errno || end == s || *end != '\0' || v < 0 || v > 1023)
		return false;

	*out = (int)v;
	return true;
}

void
p01x_csc_init(int argc, char **argv)
{
	EGLDisplay egl_dpy = eglGetCurrentDisplay();

	piglit_require_egl_extension(egl_dpy, "EGL_EXT_image_dma_buf_import");
	piglit_require_extension("GL_OES_EGL_image_external");

	for (int i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "-cs=601")) {
			color_space = CS_601;
		} else if (!strcmp(argv[i], "-cs=709")) {
			color_space = CS_709;
		} else if (!strcmp(argv[i], "-cs=2020")) {
			color_space = CS_2020;
		} else if (!strcmp(argv[i], "-range=full")) {
			full_range = true;
		} else if (!strcmp(argv[i], "-range=narrow")) {
			full_range = false;
		} else if (!strncmp(argv[i], "-tolerance=", 11)) {
			if (!parse_tolerance(argv[i] + 11, &tolerance)) {
				fprintf(stderr, "invalid tolerance: %s\n",
					argv[i]);
				print_usage(argv[0]);
			}
		} else {
			fprintf(stderr, "unknown argument: %s\n", argv[i]);
			print_usage(argv[0]);
		}
	}
}

#ifdef HAVE_HTOLE16

/* 10-bit test codes. Each format scales them to its own bit depth. */
struct block {
	uint16_t u, v;
	uint16_t y[4];
};

static const struct block blocks[(W / 2) * (H / 2)] = {
	{ 512, 512, {  64, 940, 502, 503 } },
	{ 512, 512, { 500, 501, 504, 505 } },
	{ 408, 779, { 335, 337, 339, 341 } },
	{ 397, 265, { 526, 528, 530, 532 } },
	{ 785, 444, { 337, 339, 341, 343 } },
	{ 310, 552, { 556, 558, 560, 562 } },
	{ 584, 291, { 530, 532, 534, 536 } },
	{ 671, 755, { 415, 417, 419, 421 } },
};

static const char vs_src[] =
	"attribute vec4 piglit_vertex;\n"
	"attribute vec4 piglit_texcoords;\n"
	"varying vec2 texcoords;\n"
	"void main()\n"
	"{\n"
	"	texcoords = piglit_texcoords.xy;\n"
	"	gl_Position = piglit_vertex;\n"
	"}\n";

static const char fs_src[] =
	"#extension GL_OES_EGL_image_external : require\n"
	"precision highp float;\n"
	"uniform samplerExternalOES sampler;\n"
	"varying vec2 texcoords;\n"
	"void main()\n"
	"{\n"
	"	gl_FragColor = texture2D(sampler, texcoords);\n"
	"}\n";

static const struct block *
block_at(unsigned x, unsigned y)
{
	return &blocks[(y / 2) * (W / 2) + x / 2];
}

static unsigned
y_code(const struct p01x_csc_format *fmt, unsigned x, unsigned y)
{
	return fmt->y_code(x, y, block_at(x, y)->y[(y % 2) * 2 + (x % 2)]);
}

static unsigned
u_code(const struct p01x_csc_format *fmt, unsigned x, unsigned y)
{
	return fmt->c_code(x / 2, y / 2, block_at(x, y)->u);
}

static unsigned
v_code(const struct p01x_csc_format *fmt, unsigned x, unsigned y)
{
	return fmt->c_code(x / 2, y / 2, block_at(x, y)->v);
}

static double
clamp01(double v)
{
	return v < 0.0 ? 0.0 : (v > 1.0 ? 1.0 : v);
}

/* ITU-T H.273 / BT.2100 quantisation at the given bit depth, output
 * rounded to 10 bits.
 */
static void
reference_rgb(enum cs cs, bool full, unsigned bits,
	      unsigned Y, unsigned U, unsigned V, int out[3])
{
	const double max = (double)((1u << bits) - 1);
	const double mid = (double)(1u << (bits - 1));
	const double step = (double)(1u << (bits - 8));
	double kr, kb;

	switch (cs) {
	case CS_601:  kr = 0.299;  kb = 0.114;  break;
	case CS_709:  kr = 0.2126; kb = 0.0722; break;
	default:      kr = 0.2627; kb = 0.0593; break;
	}

	double y, cb, cr;
	if (full) {
		y = Y / max;
		cb = ((double)U - mid) / max;
		cr = ((double)V - mid) / max;
	} else {
		y = ((double)Y - 16.0 * step) / (219.0 * step);
		cb = ((double)U - mid) / (224.0 * step);
		cr = ((double)V - mid) / (224.0 * step);
	}

	double r = y + 2.0 * (1.0 - kr) * cr;
	double b = y + 2.0 * (1.0 - kb) * cb;
	double g = (y - kr * r - kb * b) / (1.0 - kr - kb);

	out[0] = (int)lround(clamp01(r) * 1023.0);
	out[1] = (int)lround(clamp01(g) * 1023.0);
	out[2] = (int)lround(clamp01(b) * 1023.0);
}

/* Samples are MSB-aligned in 16 bits. */
static void
fill(const struct p01x_csc_format *fmt, uint16_t *data)
{
	const unsigned shift = 16 - fmt->bits;
	uint16_t *yp = data;
	uint16_t *uvp = data + W * H;

	for (unsigned y = 0; y < H; y++)
		for (unsigned x = 0; x < W; x++)
			yp[y * W + x] = htole16(y_code(fmt, x, y) << shift);

	for (unsigned y = 0; y < H / 2; y++) {
		for (unsigned x = 0; x < W / 2; x++) {
			uvp[y * W + 2 * x + 0] =
				htole16(u_code(fmt, 2 * x, 2 * y) << shift);
			uvp[y * W + 2 * x + 1] =
				htole16(v_code(fmt, 2 * x, 2 * y) << shift);
		}
	}
}

static enum piglit_result
import_image(const struct p01x_csc_format *fmt, EGLImageKHR *out_img)
{
	uint16_t src[W * H + W * (H / 2)];
	struct piglit_dma_buf *buf;
	enum piglit_result res;

	fill(fmt, src);

	res = piglit_create_dma_buf(W, H, fmt->fourcc, src, &buf);
	if (res != PIGLIT_PASS)
		return res;

	static const EGLint cs_enum[] = {
		EGL_ITU_REC601_EXT, EGL_ITU_REC709_EXT, EGL_ITU_REC2020_EXT,
	};

	const EGLint attr[] = {
		EGL_WIDTH, W,
		EGL_HEIGHT, H,
		EGL_LINUX_DRM_FOURCC_EXT, fmt->fourcc,
		EGL_DMA_BUF_PLANE0_FD_EXT, buf->fd,
		EGL_DMA_BUF_PLANE0_OFFSET_EXT, buf->offset[0],
		EGL_DMA_BUF_PLANE0_PITCH_EXT, buf->stride[0],
		EGL_DMA_BUF_PLANE1_FD_EXT, buf->fd,
		EGL_DMA_BUF_PLANE1_OFFSET_EXT, buf->offset[1],
		EGL_DMA_BUF_PLANE1_PITCH_EXT, buf->stride[1],
		EGL_YUV_COLOR_SPACE_HINT_EXT, cs_enum[color_space],
		EGL_SAMPLE_RANGE_HINT_EXT,
			full_range ? EGL_YUV_FULL_RANGE_EXT : EGL_YUV_NARROW_RANGE_EXT,
		EGL_NONE
	};

	int fd = buf->fd;
	EGLImageKHR img = eglCreateImageKHR(eglGetCurrentDisplay(),
					    EGL_NO_CONTEXT,
					    EGL_LINUX_DMA_BUF_EXT,
					    (EGLClientBuffer)0, attr);
	EGLint error = eglGetError();

	/* EGL does not take ownership of the fd. */
	piglit_destroy_dma_buf(buf);
	close(fd);

	if (!img && error == EGL_BAD_MATCH) {
		printf("%s dma-buf import not supported\n", fmt->name);
		return PIGLIT_SKIP;
	}

	if (!img || error != EGL_SUCCESS) {
		printf("eglCreateImageKHR() failed: %s 0x%x\n",
		       piglit_get_egl_error_name(error), error);
		return PIGLIT_FAIL;
	}

	*out_img = img;
	return PIGLIT_PASS;
}

static GLuint
make_rgb10a2_fbo(void)
{
	GLuint fbo, rb;

	glGenRenderbuffers(1, &rb);
	glBindRenderbuffer(GL_RENDERBUFFER, rb);
	glRenderbufferStorage(GL_RENDERBUFFER, GL_RGB10_A2, W, H);

	glGenFramebuffers(1, &fbo);
	glBindFramebuffer(GL_FRAMEBUFFER, fbo);
	glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
				  GL_RENDERBUFFER, rb);

	if (glCheckFramebufferStatus(GL_FRAMEBUFFER) !=
	    GL_FRAMEBUFFER_COMPLETE) {
		printf("RGB10_A2 framebuffer incomplete\n");
		piglit_report_result(PIGLIT_FAIL);
	}

	return fbo;
}

static bool
matches(const int a[3], const int b[3], int tol)
{
	for (unsigned c = 0; c < 3; c++)
		if (abs(a[c] - b[c]) > tol)
			return false;
	return true;
}

static enum piglit_result
run(const struct p01x_csc_format *fmt)
{
	static const char *cs_name[] = { "BT.601", "BT.709", "BT.2020" };
	EGLImageKHR img;
	GLuint tex = 0, fbo, prog;
	uint32_t px[W * H];
	int max_err = 0;
	unsigned bad = 0, like_601 = 0;
	enum piglit_result res;

	res = import_image(fmt, &img);
	if (res != PIGLIT_PASS)
		return res;

	res = texture_for_egl_image(img, &tex, true);
	if (res != PIGLIT_PASS)
		goto destroy;

	fbo = make_rgb10a2_fbo();
	prog = piglit_build_simple_program(vs_src, fs_src);

	glUseProgram(prog);
	glBindTexture(GL_TEXTURE_EXTERNAL_OES, tex);
	glUniform1i(glGetUniformLocation(prog, "sampler"), 0);

	glViewport(0, 0, W, H);
	glClearColor(0.0, 0.0, 0.0, 0.0);
	glClear(GL_COLOR_BUFFER_BIT);
	piglit_draw_rect_tex(-1, -1, 2, 2, 0, 0, 1, 1);

	glReadPixels(0, 0, W, H, GL_RGBA, GL_UNSIGNED_INT_2_10_10_10_REV, px);
	if (!piglit_check_gl_error(GL_NO_ERROR)) {
		res = PIGLIT_FAIL;
		goto cleanup;
	}

	printf("%s -> %s %s range -> RGB10_A2, tolerance %d\n",
	       fmt->name, cs_name[color_space],
	       full_range ? "full" : "narrow", tolerance);

	for (unsigned y = 0; y < H; y++) {
		for (unsigned x = 0; x < W; x++) {
			const struct block *b = block_at(x, y);
			unsigned Y = y_code(fmt, x, y);
			unsigned U = u_code(fmt, x, y);
			unsigned V = v_code(fmt, x, y);
			uint32_t p = px[y * W + x];
			int got[3] = {
				(int)(p & 0x3ff),
				(int)((p >> 10) & 0x3ff),
				(int)((p >> 20) & 0x3ff),
			};
			int alpha = (int)(p >> 30);
			int want[3], exp601[3];

			reference_rgb(color_space, full_range, fmt->bits,
				      Y, U, V, want);
			reference_rgb(CS_601, full_range, fmt->bits,
				      Y, U, V, exp601);

			for (unsigned c = 0; c < 3; c++)
				if (abs(got[c] - want[c]) > max_err)
					max_err = abs(got[c] - want[c]);

			bool ok = matches(got, want, tolerance) && alpha == 3;
			if (!ok) {
				bad++;
				printf("  (%u,%u) YUV=(%u,%u,%u) "
				       "expected (%d,%d,%d,3) got (%d,%d,%d,%d)\n",
				       x, y, Y, U, V,
				       want[0], want[1], want[2],
				       got[0], got[1], got[2], alpha);
			}

			if (color_space != CS_601 && b->u != 512 &&
			    !matches(want, exp601, tolerance) &&
			    matches(got, exp601, tolerance))
				like_601++;
		}
	}

	printf("max abs error: %d (10-bit LSBs), %u/%u pixels out of "
	       "tolerance\n", max_err, bad, W * H);

	if (like_601)
		printf("%u chroma pixels match BT.601 instead: color space "
		       "hint appears to be ignored\n", like_601);

	res = bad ? PIGLIT_FAIL : PIGLIT_PASS;

cleanup:
	glUseProgram(0);
	glDeleteProgram(prog);
	glBindFramebuffer(GL_FRAMEBUFFER, piglit_winsys_fbo);
	glDeleteFramebuffers(1, &fbo);
destroy:
	glDeleteTextures(1, &tex);
	eglDestroyImageKHR(eglGetCurrentDisplay(), img);
	return res;
}

#endif /* HAVE_HTOLE16 */

enum piglit_result
p01x_csc_display(const struct p01x_csc_format *fmt)
{
#ifdef HAVE_HTOLE16
	return run(fmt);
#else
	printf("htole16() is not available, cannot build %s data\n",
	       fmt->name);
	return PIGLIT_SKIP;
#endif
}
