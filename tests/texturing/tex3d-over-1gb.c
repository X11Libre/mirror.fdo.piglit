/* SPDX-License-Identifier: MIT */

/**
 * A 3D texture larger than 1 GB that is a small fraction of the GPU's
 * dedicated memory must allocate and sample. Volume rendering applications
 * use such textures, and a driver capping texture allocations at a fixed
 * 1 GB reported GL_OUT_OF_MEMORY for them on GPUs with many times that.
 *
 * The test skips unless the GPU reports at least 8 GB of dedicated memory
 * through GL_NVX_gpu_memory_info or free texture memory through
 * GL_ATI_meminfo.
 */

#include "piglit-util-gl.h"

PIGLIT_GL_TEST_CONFIG_BEGIN

	config.supports_gl_compat_version = 12;

	config.window_visual = PIGLIT_GL_VISUAL_RGBA | PIGLIT_GL_VISUAL_DOUBLE;
	config.khr_no_error_support = PIGLIT_NO_ERRORS;

PIGLIT_GL_TEST_CONFIG_END

/* 1280 MB of GL_RGBA8 */
#define W 1024
#define H 1024
#define D 320

#define MIN_VRAM_KB (8ull * 1024 * 1024)

static uint64_t
vram_kb(void)
{
	if (piglit_is_extension_supported("GL_NVX_gpu_memory_info")) {
		GLint kb = 0;
		glGetIntegerv(GL_GPU_MEMORY_INFO_DEDICATED_VIDMEM_NVX, &kb);
		return kb;
	}
	if (piglit_is_extension_supported("GL_ATI_meminfo")) {
		GLint info[4] = { 0 };
		glGetIntegerv(GL_TEXTURE_FREE_MEMORY_ATI, info);
		return info[0];
	}
	return 0;
}

enum piglit_result
piglit_display(void)
{
	static const GLubyte texel[4] = { 0, 255, 0, 255 };
	static const float green[4] = { 0.0f, 1.0f, 0.0f, 1.0f };
	GLint pw = 0, ph = 0, pd = 0;
	GLuint tex;
	bool pass = true;

	glTexImage3D(GL_PROXY_TEXTURE_3D, 0, GL_RGBA8, W, H, D, 0, GL_RGBA,
		     GL_UNSIGNED_BYTE, NULL);
	glGetTexLevelParameteriv(GL_PROXY_TEXTURE_3D, 0, GL_TEXTURE_WIDTH, &pw);
	glGetTexLevelParameteriv(GL_PROXY_TEXTURE_3D, 0, GL_TEXTURE_HEIGHT, &ph);
	glGetTexLevelParameteriv(GL_PROXY_TEXTURE_3D, 0, GL_TEXTURE_DEPTH, &pd);
	if (pw != W || ph != H || pd != D) {
		printf("proxy rejects %dx%dx%d GL_RGBA8 (got %dx%dx%d)\n",
		       W, H, D, pw, ph, pd);
		pass = false;
	}

	glGenTextures(1, &tex);
	glBindTexture(GL_TEXTURE_3D, tex);
	glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexImage3D(GL_TEXTURE_3D, 0, GL_RGBA8, W, H, D, 0, GL_RGBA,
		     GL_UNSIGNED_BYTE, NULL);
	if (!piglit_check_gl_error(GL_NO_ERROR)) {
		printf("allocating %dx%dx%d GL_RGBA8 failed\n", W, H, D);
		glDeleteTextures(1, &tex);
		return PIGLIT_FAIL;
	}

	/* the last texel, so the whole allocation has to be there */
	glTexSubImage3D(GL_TEXTURE_3D, 0, W - 1, H - 1, D - 1, 1, 1, 1,
			GL_RGBA, GL_UNSIGNED_BYTE, texel);

	glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT);
	glEnable(GL_TEXTURE_3D);
	glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
	glBegin(GL_QUADS);
	glTexCoord3f((W - 0.5f) / W, (H - 0.5f) / H, (D - 0.5f) / D);
	glVertex2f(-1.0f, -1.0f);
	glVertex2f(1.0f, -1.0f);
	glVertex2f(1.0f, 1.0f);
	glVertex2f(-1.0f, 1.0f);
	glEnd();
	glDisable(GL_TEXTURE_3D);

	pass &= piglit_probe_rect_rgba(0, 0, piglit_width, piglit_height, green);
	pass &= piglit_check_gl_error(GL_NO_ERROR);

	glDeleteTextures(1, &tex);
	piglit_present_results();
	return pass ? PIGLIT_PASS : PIGLIT_FAIL;
}

void
piglit_init(int argc, char **argv)
{
	GLint max_size = 0;

	/* D is not a power of two */
	piglit_require_extension("GL_ARB_texture_non_power_of_two");

	glGetIntegerv(GL_MAX_3D_TEXTURE_SIZE, &max_size);
	if (max_size < W) {
		printf("GL_MAX_3D_TEXTURE_SIZE %d is below %d\n", max_size, W);
		piglit_report_result(PIGLIT_SKIP);
	}

	uint64_t kb = vram_kb();
	if (kb < MIN_VRAM_KB) {
		printf("needs %llu MB of GPU memory, the GPU reports %llu MB\n",
		       (unsigned long long)(MIN_VRAM_KB / 1024),
		       (unsigned long long)(kb / 1024));
		piglit_report_result(PIGLIT_SKIP);
	}
}
