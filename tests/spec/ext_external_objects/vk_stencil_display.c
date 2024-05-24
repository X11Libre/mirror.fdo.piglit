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
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Author:
 *    Eleni Maria Stea <estea@igalia.com>
 */

#include <piglit-util-gl.h>
#include "interop.h"
#include "params.h"
#include "helpers.h"

PIGLIT_GL_TEST_CONFIG_BEGIN

config.supports_gl_compat_version = 30;
config.window_visual = PIGLIT_GL_VISUAL_RGBA | PIGLIT_GL_VISUAL_DOUBLE | PIGLIT_GL_VISUAL_DEPTH;
config.khr_no_error_support = PIGLIT_HAS_ERRORS;

PIGLIT_GL_TEST_CONFIG_END

static bool
vk_init(void);

static bool
vk_subtest_init(int case_num);

static bool
gl_init(void);

static bool
gl_subtest_init(void);

static void
vk_cleanup(void);

static void
vk_subtest_cleanup(void);

static void
gl_subtest_cleanup(void);

static void
cleanup(void);

static enum piglit_result
run_subtest(int case_num);

static struct vk_ctx vk_core;
static struct vk_image_att vk_color_att;
static struct vk_image_att vk_depth_att;
static struct vk_renderer vk_rnd;

static GLuint gl_mem_obj;
static float vk_fb_color[4] = { 1.0, 0.0, 1.0, 1.0 };

static GLuint vkgl_stencil_tex;
static GLuint gl_fbo;
static GLuint gl_color_tex;

static GLint gl_prog;
static GLint gl_rnd2fbo_prog;

static char *vs_src;
static char *fs_src;
static unsigned int vs_sz;
static unsigned int fs_sz;

static const char vs[] =
	"#version 130\n"
	"in vec4 piglit_vertex;\n"
	"in vec2 piglit_texcoord;\n"
	"out vec2 tex_coords;\n"
	"void main()\n"
	"{\n"
	"    gl_Position = piglit_vertex;\n"
	"    tex_coords = piglit_texcoord;\n"
	"}\n";

static const char fs[] =
	"#version 130\n"
	"in vec2 tex_coords;\n"
	"uniform sampler2D tex; \n"
	"out vec4 color;\n"
	"void main()\n"
	"{\n"
	"    color = texture(tex, tex_coords);\n"
	"}\n";

static const char vs_rnd2fbo[] =
	"#version 130\n"
	"in vec4 piglit_vertex;\n"
	"void main()\n"
	"{\n"
	"    gl_Position = piglit_vertex;\n"
	"}\n";

static const char fs_rnd2fbo[] =
	"#version 130\n"
	"out vec4 color;\n"
	"void main()\n"
	"{\n"
	"    color = vec4(0.0, 0.0, 1.0, 1.0);\n"
	"}\n";

void
piglit_init(int argc, char **argv)
{
	piglit_require_extension("GL_ARB_texture_storage");
	piglit_require_extension("GL_EXT_memory_object");
	piglit_require_extension("GL_EXT_memory_object_fd");
	piglit_require_extension("GL_EXT_semaphore");
	piglit_require_extension("GL_EXT_semaphore_fd");

	atexit(cleanup);

	if (!vk_init()) {
		fprintf(stdout, "Failed to initialize Vulkan, skipping the test.\n");
		piglit_report_result(PIGLIT_SKIP);
	}

	if (!gl_init()) {
		fprintf(stderr, "Failed to initialize structs for GL rendering.\n");
		piglit_report_result(PIGLIT_FAIL);
	}
}

enum piglit_result
piglit_display(void)
{
	enum piglit_result test_res = PIGLIT_SKIP;
	enum piglit_result subtest_res;
	int i;

	glClear(GL_COLOR_BUFFER_BIT);

	/* skipping the depth only format from depth_stencil_formats */
	for (i = 0; i < ARRAY_SIZE(depth_stencil_formats); i++) {
		if (depth_stencil_formats[i].gl_ds_fmt == GL_DEPTH_COMPONENT16)
			continue;
		subtest_res = run_subtest(i);
		piglit_merge_result(&test_res, subtest_res);
	}

	return test_res;
}

static enum piglit_result
run_subtest(int case_num)
{
	float green[] = { 0.0, 1.0, 0.0 };
	float blue[] = { 0.0, 0.0, 1.0 };
	enum piglit_result res = PIGLIT_SKIP;

	/* Vulkan init */
	if (!vk_subtest_init(case_num)) {
		piglit_report_subtest_result(res, "%s", depth_stencil_formats[case_num].name);
		return res;
	}

	vk_draw(&vk_core,
		0, &vk_rnd,
		vk_fb_color,
		4, 0, false, false, &vk_depth_att, 1, 0, 0, piglit_width, piglit_height);

	if (!gl_create_mem_obj_from_vk_mem(&vk_core, &vk_depth_att.obj.mobj,
					   &gl_mem_obj)) {
		fprintf(stderr, "Failed to create GL memory object from Vulkan memory.\n");
		goto fail;
	}

	if (!gl_gen_tex_from_mem_obj(&vk_depth_att.props,
				     depth_stencil_formats[case_num].gl_ds_fmt,
				     gl_mem_obj, 0, &vkgl_stencil_tex)) {
		fprintf(stderr, "Failed to create GL texture from Vulkan memory object.\n");
		goto fail;
	}

	glDisable(GL_DEPTH_TEST);

	/* This may fail if the GL driver does not support the format with memory objects. */
	if (!gl_subtest_init()) {
		piglit_report_subtest_result(res, "%s", depth_stencil_formats[case_num].name);
		return res;
	}

	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	glClearColor(1.0, 0.0, 0.0, 1.0);
	glClear(GL_COLOR_BUFFER_BIT);

	glUseProgram(gl_prog);
	glBindTexture(GL_TEXTURE_2D, gl_color_tex);
	piglit_draw_rect_tex(-1, -1, 2, 2,
			     0, 0, 1, 1);

	assert(glGetError() == GL_NO_ERROR);
	if (!piglit_probe_pixel_rgb(piglit_width / 2, piglit_height / 2, blue))
		goto fail;

	if (!piglit_probe_pixel_rgb(0, 0, green) ||
	    !piglit_probe_pixel_rgb(piglit_width - 1, 0, green) ||
	    !piglit_probe_pixel_rgb(0, piglit_height - 1, green) ||
	    !piglit_probe_pixel_rgb(piglit_width - 1, piglit_height - 1, green))
		goto fail;

	piglit_present_results();

	vk_subtest_cleanup();
	gl_subtest_cleanup();

	piglit_report_subtest_result(PIGLIT_PASS, "%s", depth_stencil_formats[case_num].name);
	return PIGLIT_PASS;

fail:
	piglit_report_subtest_result(PIGLIT_FAIL, "%s", depth_stencil_formats[case_num].name);

	vk_subtest_cleanup();
	gl_subtest_cleanup();

	piglit_swap_buffers();
	return PIGLIT_FAIL;
}

static bool
vk_init(void)
{
	if (!vk_init_ctx_for_rendering(&vk_core)) {
		fprintf(stderr, "Failed to create Vulkan context.\n");
		return false;
	}

	if (!vk_check_gl_compatibility(&vk_core)) {
		fprintf(stderr, "Mismatch in driver/device UUID\n");
		return false;
	}

	if (!(vs_src = load_shader(VK_ZQUAD_VERT, &vs_sz))) {
		fprintf(stderr, "Failed to load VS source.\n");
		return false;
	}

	if (!(fs_src = load_shader(VK_ZQUAD_FRAG, &fs_sz))) {
		fprintf(stderr, "Failed to load FS source.\n");
		free(vs_src);
		return false;
	}

	return true;
}

static bool
vk_subtest_init(int case_num)
{
	if (!vk_fill_ext_image_props(&vk_core,
				     piglit_width, piglit_height, 1,
				     num_samples,
				     num_levels,
				     num_layers,
				     color_format,
				     color_tiling,
				     color_in_layout,
				     color_end_layout,
				     true,
				     &vk_color_att.props)) {
		fprintf(stderr, "Unsupported color image properties.\n");
		return false;
	}
	if (!vk_create_ext_image(&vk_core, &vk_color_att.props, &vk_color_att.obj)) {
		fprintf(stderr, "Failed to create color image.\n");
		return false;
	}

	if (!vk_fill_ext_image_props(&vk_core,
				     piglit_width, piglit_height, 1,
				     num_samples,
				     num_levels,
				     num_layers,
				     depth_stencil_formats[case_num].vk_ds_fmt,
				     depth_tiling,
				     depth_in_layout,
				     depth_end_layout,
				     true,
				     &vk_depth_att.props)) {
		fprintf(stderr, "Unsupported depth image properties.\n");
		vk_destroy_ext_image(&vk_core, &vk_color_att.obj);
		return false;
	}

	if (!vk_create_ext_image(&vk_core, &vk_depth_att.props, &vk_depth_att.obj)) {
		fprintf(stderr, "Failed to create depth image.\n");
		vk_destroy_ext_image(&vk_core, &vk_color_att.obj);
		return false;
	}

	if (!vk_create_renderer(&vk_core, vs_src, vs_sz, fs_src, fs_sz,
				false, true,
				&vk_color_att, &vk_depth_att, 0, &vk_rnd)) {
		fprintf(stderr, "Failed to create Vulkan renderer.\n");
		vk_destroy_ext_image(&vk_core, &vk_color_att.obj);
		vk_destroy_ext_image(&vk_core, &vk_depth_att.obj);
		return false;
	}

	return true;

}

static void
vk_subtest_cleanup(void)
{
	vk_destroy_ext_image(&vk_core, &vk_color_att.obj);
	vk_destroy_ext_image(&vk_core, &vk_depth_att.obj);

	vk_destroy_renderer(&vk_core, &vk_rnd);
}

static bool
gl_subtest_init(void)
{
	glBindFramebuffer(GL_FRAMEBUFFER, gl_fbo);
	glFramebufferTexture2D(GL_FRAMEBUFFER,
			       GL_DEPTH_STENCIL_ATTACHMENT,
			       GL_TEXTURE_2D, vkgl_stencil_tex, 0);

	glFramebufferTexture2D(GL_FRAMEBUFFER,
			       GL_COLOR_ATTACHMENT0,
			       GL_TEXTURE_2D, gl_color_tex, 0);

	if (!check_bound_fbo_status())
		return false;

	glClearColor(0.0, 1.0, 0.0, 1.0);
	glClear(GL_COLOR_BUFFER_BIT);

	/* render a quad using the gl_color_tex and the imported
	 * stencil buffer
	 */
	glEnable(GL_STENCIL_TEST);
	glStencilFunc(GL_EQUAL, 1, 0xFFFFFFFF);

	glUseProgram(gl_rnd2fbo_prog);
	piglit_draw_rect(-1, -1, 2, 2);

	glDisable(GL_STENCIL_TEST);
	return glGetError() == GL_NO_ERROR;
}


static bool
gl_init(void)
{
	gl_prog = piglit_build_simple_program(vs, fs);
	gl_rnd2fbo_prog = piglit_build_simple_program(vs_rnd2fbo, fs_rnd2fbo);

	glGenFramebuffers(1, &gl_fbo);
	glGenTextures(1, &gl_color_tex);
	glBindTexture(GL_TEXTURE_2D, gl_color_tex);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, piglit_width, piglit_height, 0, GL_RGBA, GL_UNSIGNED_BYTE, 0);

	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	glClearColor(1.0, 0.0, 1.0, 1.0);
	glClear(GL_COLOR_BUFFER_BIT);

	return glGetError() == GL_NO_ERROR;
}

static void
gl_subtest_cleanup(void)
{
	glDeleteTextures(1, &vkgl_stencil_tex);
	glDeleteMemoryObjectsEXT(1, &gl_mem_obj);
}

static void
vk_cleanup(void)
{
	free(vs_src);
	free(fs_src);

	vk_cleanup_ctx(&vk_core);
}

static void
cleanup(void)
{
	vk_cleanup();
}

