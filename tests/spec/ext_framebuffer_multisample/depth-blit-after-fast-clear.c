/*
 * Copyright © 2026 Timothy Arceri
 *
 * SPDX-License-Identifier: MIT
 */

/**
 * \file depth-blit-after-fast-clear.c
 *
 * Clear a multisampled depth texture, draw a few rectangles into it, then
 * read the depth back through one of several paths and draw it as grey in
 * the left half of the window.  The right half shows the same rectangles
 * drawn directly as a reference, and the two halves must match.  The
 * sequence is repeated a few times, because that is how applications do it
 * each frame.
 *
 * Each subtest is one depth format with one of these read paths:
 *   blit        resolve with glBlitFramebuffer and sample the result
 *   copyimage   glCopyImageSubData to another multisample texture and
 *               sample that with texelFetch
 *   texelfetch  sample the depth texture with texelFetch
 *   bindless    sample the depth texture with texelFetch through a
 *               bindless handle made resident before rendering
 *
 * This is what OpenMW does with anti-aliasing enabled.  radeonsi on Navi1x
 * and Navi2x read stale HTILE for 8x MSAA depth after it had been fast
 * cleared, which corrupted the depth that was read back:
 * https://gitlab.freedesktop.org/mesa/mesa/-/work_items/14597
 *
 * Usage: depth-blit-after-fast-clear <num_samples>
 */

#include "piglit-util-gl.h"

#define SIZE 256
#define ITERATIONS 4

enum method {
	METHOD_BLIT,
	METHOD_COPYIMAGE,
	METHOD_TEXELFETCH,
	METHOD_BINDLESS,
};

struct format {
	GLenum internal_format;
	GLenum format;
	GLenum type;
	GLenum attachment;
};

static const struct format depth32f_stencil8 = {
	GL_DEPTH32F_STENCIL8, GL_DEPTH_STENCIL,
	GL_FLOAT_32_UNSIGNED_INT_24_8_REV, GL_DEPTH_STENCIL_ATTACHMENT
};
static const struct format depth32f = {
	GL_DEPTH_COMPONENT32F, GL_DEPTH_COMPONENT, GL_FLOAT,
	GL_DEPTH_ATTACHMENT
};
static const struct format depth24_stencil8 = {
	GL_DEPTH24_STENCIL8, GL_DEPTH_STENCIL, GL_UNSIGNED_INT_24_8,
	GL_DEPTH_STENCIL_ATTACHMENT
};
static const struct format depth24 = {
	GL_DEPTH_COMPONENT24, GL_DEPTH_COMPONENT, GL_UNSIGNED_INT,
	GL_DEPTH_ATTACHMENT
};

struct test {
	const struct format *format;
	enum method method;
};

static enum piglit_result run_test(void *data);

static const struct test tests[] = {
	{ &depth32f_stencil8, METHOD_BLIT },
	{ &depth32f_stencil8, METHOD_COPYIMAGE },
	{ &depth32f_stencil8, METHOD_TEXELFETCH },
	{ &depth32f_stencil8, METHOD_BINDLESS },
	{ &depth32f, METHOD_BLIT },
	{ &depth32f, METHOD_COPYIMAGE },
	{ &depth32f, METHOD_TEXELFETCH },
	{ &depth32f, METHOD_BINDLESS },
	{ &depth24_stencil8, METHOD_BLIT },
	{ &depth24_stencil8, METHOD_COPYIMAGE },
	{ &depth24_stencil8, METHOD_TEXELFETCH },
	{ &depth24_stencil8, METHOD_BINDLESS },
	{ &depth24, METHOD_BLIT },
	{ &depth24, METHOD_COPYIMAGE },
	{ &depth24, METHOD_TEXELFETCH },
	{ &depth24, METHOD_BINDLESS },
};

static const struct piglit_subtest subtests[] = {
	{ "depth32f_stencil8 blit", NULL, run_test, (void *)&tests[0] },
	{ "depth32f_stencil8 copyimage", NULL, run_test, (void *)&tests[1] },
	{ "depth32f_stencil8 texelfetch", NULL, run_test, (void *)&tests[2] },
	{ "depth32f_stencil8 bindless", NULL, run_test, (void *)&tests[3] },
	{ "depth32f blit", NULL, run_test, (void *)&tests[4] },
	{ "depth32f copyimage", NULL, run_test, (void *)&tests[5] },
	{ "depth32f texelfetch", NULL, run_test, (void *)&tests[6] },
	{ "depth32f bindless", NULL, run_test, (void *)&tests[7] },
	{ "depth24_stencil8 blit", NULL, run_test, (void *)&tests[8] },
	{ "depth24_stencil8 copyimage", NULL, run_test, (void *)&tests[9] },
	{ "depth24_stencil8 texelfetch", NULL, run_test, (void *)&tests[10] },
	{ "depth24_stencil8 bindless", NULL, run_test, (void *)&tests[11] },
	{ "depth24 blit", NULL, run_test, (void *)&tests[12] },
	{ "depth24 copyimage", NULL, run_test, (void *)&tests[13] },
	{ "depth24 texelfetch", NULL, run_test, (void *)&tests[14] },
	{ "depth24 bindless", NULL, run_test, (void *)&tests[15] },
	{ NULL }
};

static struct piglit_gl_test_config *piglit_config;

PIGLIT_GL_TEST_CONFIG_BEGIN

	piglit_config = &config;
	config.subtests = subtests;
	config.supports_gl_core_version = 32;
	config.window_width = 2 * SIZE;
	config.window_height = SIZE;
	config.window_visual = PIGLIT_GL_VISUAL_RGBA | PIGLIT_GL_VISUAL_DOUBLE;
	config.khr_no_error_support = PIGLIT_NO_ERRORS;

PIGLIT_GL_TEST_CONFIG_END

/* Rectangles drawn at depth 0.5, in pixels.  They leave most of the
 * framebuffer at the clear value and cover the bottom rows, where the
 * corruption showed up.
 */
static const int rects[][4] = {
	{ 0, 0, 96, 24 },
	{ 40, 10, 120, 100 },
	{ 170, 0, 50, 32 },
	{ 200, 150, 56, 106 },
};

static const char *vs_source =
	"#version 150\n"
	"uniform mat4 proj;\n"
	"in vec4 piglit_vertex;\n"
	"void main()\n"
	"{\n"
	"	gl_Position = proj * piglit_vertex;\n"
	"}\n";

static const char *fs_color_source =
	"#version 150\n"
	"uniform vec4 color;\n"
	"out vec4 frag_color;\n"
	"void main()\n"
	"{\n"
	"	frag_color = color;\n"
	"}\n";

/* The sampling shaders write the depth of sample 0 as grey. */
static const char *fs_sample_ms_source =
	"#version 150\n"
	"uniform sampler2DMS tex;\n"
	"out vec4 frag_color;\n"
	"void main()\n"
	"{\n"
	"	float d = texelFetch(tex, ivec2(gl_FragCoord.xy), 0).r;\n"
	"	frag_color = vec4(vec3(d), 1.0);\n"
	"}\n";

static const char *fs_sample_source =
	"#version 150\n"
	"uniform sampler2D tex;\n"
	"out vec4 frag_color;\n"
	"void main()\n"
	"{\n"
	"	float d = texelFetch(tex, ivec2(gl_FragCoord.xy), 0).r;\n"
	"	frag_color = vec4(vec3(d), 1.0);\n"
	"}\n";

static const char *fs_sample_bindless_source =
	"#version 150\n"
	"#extension GL_ARB_bindless_texture : require\n"
	"layout(bindless_sampler) uniform sampler2DMS tex;\n"
	"out vec4 frag_color;\n"
	"void main()\n"
	"{\n"
	"	float d = texelFetch(tex, ivec2(gl_FragCoord.xy), 0).r;\n"
	"	frag_color = vec4(vec3(d), 1.0);\n"
	"}\n";

static int samples;
static GLuint color_prog, sample_ms_prog, sample_prog;
static GLint color_loc;

static GLuint
build_program(const char *fs_source)
{
	GLuint prog = piglit_build_simple_program(vs_source, fs_source);

	glUseProgram(prog);
	piglit_ortho_uniform(glGetUniformLocation(prog, "proj"), SIZE, SIZE);
	glUseProgram(0);

	return prog;
}

static GLuint
create_depth_texture(int num_samples, const struct format *format)
{
	GLuint tex;

	glGenTextures(1, &tex);
	if (num_samples) {
		glBindTexture(GL_TEXTURE_2D_MULTISAMPLE, tex);
		glTexImage2DMultisample(GL_TEXTURE_2D_MULTISAMPLE,
					num_samples, format->internal_format,
					SIZE, SIZE, GL_TRUE);
		glBindTexture(GL_TEXTURE_2D_MULTISAMPLE, 0);
	} else {
		glBindTexture(GL_TEXTURE_2D, tex);
		glTexImage2D(GL_TEXTURE_2D, 0, format->internal_format, SIZE,
			     SIZE, 0, format->format, format->type, NULL);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
				GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER,
				GL_NEAREST);
		glBindTexture(GL_TEXTURE_2D, 0);
	}

	return tex;
}

/* An FBO with the given depth texture and a color attachment, like the
 * framebuffer of a scene pass.
 */
static GLuint
create_depth_fbo(int num_samples, GLuint depth_tex,
		 const struct format *format, GLuint *rb)
{
	GLenum target = num_samples ? GL_TEXTURE_2D_MULTISAMPLE :
				      GL_TEXTURE_2D;
	GLuint fbo;

	glGenFramebuffers(1, &fbo);
	glBindFramebuffer(GL_FRAMEBUFFER, fbo);
	glFramebufferTexture2D(GL_FRAMEBUFFER, format->attachment, target,
			       depth_tex, 0);

	glGenRenderbuffers(1, rb);
	glBindRenderbuffer(GL_RENDERBUFFER, *rb);
	glRenderbufferStorageMultisample(GL_RENDERBUFFER, num_samples,
					 GL_RGBA8, SIZE, SIZE);
	glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
				  GL_RENDERBUFFER, *rb);

	return fbo;
}

static bool
check_fbo(void)
{
	GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);

	if (status != GL_FRAMEBUFFER_COMPLETE) {
		printf("FBO incomplete: %s\n",
		       piglit_get_gl_enum_name(status));
		return false;
	}

	return true;
}

static void
draw_rects(void)
{
	for (unsigned i = 0; i < ARRAY_SIZE(rects); i++)
		piglit_draw_rect(rects[i][0], rects[i][1],
				 rects[i][2], rects[i][3]);
}

static bool
draw_and_check(const struct test *test, GLuint msaa_fbo, GLuint msaa_depth,
	       GLuint resolve_fbo, GLuint resolve_depth, GLuint copy_depth,
	       GLuint sample_bindless_prog)
{
	static const float green[4] = { 0.0, 1.0, 0.0, 1.0 };
	static const float black[4] = { 0.0, 0.0, 0.0, 1.0 };
	static const float grey[4] = { 0.5, 0.5, 0.5, 1.0 };
	GLuint tex = msaa_depth;
	GLuint prog = sample_ms_prog;

	glBindFramebuffer(GL_FRAMEBUFFER, msaa_fbo);
	glViewport(0, 0, SIZE, SIZE);

	/* Clear to 0 and draw with GL_GEQUAL like a reversed-Z renderer.
	 * The ortho projection maps z = 0 to a depth of 0.5.
	 */
	glClearColor(0.0, 0.0, 0.0, 1.0);
	glClearDepth(0.0);
	glClearStencil(0);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT |
		GL_STENCIL_BUFFER_BIT);

	glEnable(GL_DEPTH_TEST);
	glDepthFunc(GL_GEQUAL);
	glUseProgram(color_prog);
	glUniform4fv(color_loc, 1, green);
	draw_rects();
	glDisable(GL_DEPTH_TEST);
	glDepthFunc(GL_LESS);

	switch (test->method) {
	case METHOD_BLIT:
		glBindFramebuffer(GL_READ_FRAMEBUFFER, msaa_fbo);
		glBindFramebuffer(GL_DRAW_FRAMEBUFFER, resolve_fbo);
		glBlitFramebuffer(0, 0, SIZE, SIZE, 0, 0, SIZE, SIZE,
				  GL_DEPTH_BUFFER_BIT, GL_NEAREST);
		tex = resolve_depth;
		prog = sample_prog;
		break;
	case METHOD_COPYIMAGE:
		glCopyImageSubData(msaa_depth, GL_TEXTURE_2D_MULTISAMPLE, 0,
				   0, 0, 0,
				   copy_depth, GL_TEXTURE_2D_MULTISAMPLE, 0,
				   0, 0, 0, SIZE, SIZE, 1);
		tex = copy_depth;
		break;
	case METHOD_TEXELFETCH:
		break;
	case METHOD_BINDLESS:
		prog = sample_bindless_prog;
		break;
	}

	/* Left half: the depth that was read back.  Blue shows anything
	 * that wasn't drawn.
	 */
	glBindFramebuffer(GL_FRAMEBUFFER, piglit_winsys_fbo);
	glViewport(0, 0, piglit_width, piglit_height);
	glClearColor(0.0, 0.0, 1.0, 1.0);
	glClear(GL_COLOR_BUFFER_BIT);

	glViewport(0, 0, SIZE, SIZE);
	glUseProgram(prog);
	if (test->method == METHOD_BLIT)
		glBindTexture(GL_TEXTURE_2D, tex);
	else if (test->method != METHOD_BINDLESS)
		glBindTexture(GL_TEXTURE_2D_MULTISAMPLE, tex);
	piglit_draw_rect(0, 0, SIZE, SIZE);
	glBindTexture(GL_TEXTURE_2D, 0);
	glBindTexture(GL_TEXTURE_2D_MULTISAMPLE, 0);

	/* Right half: the reference, with the rectangles in the grey that
	 * a depth of 0.5 gives over the black of the cleared depth.
	 */
	glViewport(SIZE, 0, SIZE, SIZE);
	glUseProgram(color_prog);
	glUniform4fv(color_loc, 1, black);
	piglit_draw_rect(0, 0, SIZE, SIZE);
	glUniform4fv(color_loc, 1, grey);
	draw_rects();

	glUseProgram(0);
	glViewport(0, 0, piglit_width, piglit_height);

	return piglit_probe_rect_halves_equal_rgba(0, 0, piglit_width,
						   piglit_height);
}

static enum piglit_result
run_test(void *data)
{
	const struct test *test = data;
	GLuint msaa_depth, msaa_rb, msaa_fbo;
	GLuint resolve_depth = 0, resolve_rb = 0, resolve_fbo = 0;
	GLuint copy_depth = 0, sample_bindless_prog = 0;
	GLuint64 handle = 0;
	enum piglit_result result = PIGLIT_PASS;

	if (test->method == METHOD_COPYIMAGE &&
	    !piglit_is_extension_supported("GL_ARB_copy_image"))
		return PIGLIT_SKIP;
	if (test->method == METHOD_BINDLESS &&
	    !piglit_is_extension_supported("GL_ARB_bindless_texture"))
		return PIGLIT_SKIP;

	msaa_depth = create_depth_texture(samples, test->format);
	msaa_fbo = create_depth_fbo(samples, msaa_depth, test->format,
				    &msaa_rb);
	if (!check_fbo())
		result = PIGLIT_FAIL;

	if (test->method == METHOD_BLIT) {
		resolve_depth = create_depth_texture(0, test->format);
		resolve_fbo = create_depth_fbo(0, resolve_depth,
					       test->format, &resolve_rb);
		if (!check_fbo())
			result = PIGLIT_FAIL;
	} else if (test->method == METHOD_COPYIMAGE) {
		copy_depth = create_depth_texture(samples, test->format);
	} else if (test->method == METHOD_BINDLESS) {
		sample_bindless_prog =
			build_program(fs_sample_bindless_source);

		/* Make the handle resident before rendering to the
		 * texture, like the application.
		 */
		handle = glGetTextureHandleARB(msaa_depth);
		glMakeTextureHandleResidentARB(handle);
		glUseProgram(sample_bindless_prog);
		glUniformHandleui64ARB(glGetUniformLocation(
					       sample_bindless_prog, "tex"),
				       handle);
		glUseProgram(0);
	}

	for (int i = 0; i < ITERATIONS && result == PIGLIT_PASS; i++) {
		if (!draw_and_check(test, msaa_fbo, msaa_depth, resolve_fbo,
				    resolve_depth, copy_depth,
				    sample_bindless_prog)) {
			printf("Iteration %d failed\n", i);
			result = PIGLIT_FAIL;
		}
	}

	if (!piglit_check_gl_error(GL_NO_ERROR))
		result = PIGLIT_FAIL;

	piglit_present_results();

	if (handle)
		glMakeTextureHandleNonResidentARB(handle);
	glBindFramebuffer(GL_FRAMEBUFFER, piglit_winsys_fbo);
	glDeleteFramebuffers(1, &msaa_fbo);
	glDeleteFramebuffers(1, &resolve_fbo);
	glDeleteRenderbuffers(1, &msaa_rb);
	glDeleteRenderbuffers(1, &resolve_rb);
	glDeleteTextures(1, &msaa_depth);
	glDeleteTextures(1, &resolve_depth);
	glDeleteTextures(1, &copy_depth);
	glDeleteProgram(sample_bindless_prog);

	return result;
}

void
piglit_init(int argc, char **argv)
{
	int max_samples;

	if (argc != 2) {
		printf("Usage: %s <num_samples>\n", argv[0]);
		piglit_report_result(PIGLIT_FAIL);
	}

	samples = strtol(argv[1], NULL, 0);
	if (samples < 1) {
		printf("Invalid sample count %s\n", argv[1]);
		piglit_report_result(PIGLIT_FAIL);
	}

	glGetIntegerv(GL_MAX_DEPTH_TEXTURE_SAMPLES, &max_samples);
	if (samples > max_samples)
		piglit_report_result(PIGLIT_SKIP);

	color_prog = build_program(fs_color_source);
	color_loc = glGetUniformLocation(color_prog, "color");

	sample_ms_prog = build_program(fs_sample_ms_source);
	sample_prog = build_program(fs_sample_source);
}

enum piglit_result
piglit_display(void)
{
	return piglit_run_selected_subtests(
		subtests,
		piglit_config->selected_subtests,
		piglit_config->num_selected_subtests,
		PIGLIT_SKIP);
}
