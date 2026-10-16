/*
 * SPDX-License-Identifier: MIT
 */

/**
 * Helpers shared by the OVR_multiview, OVR_multiview2, and
 * OVR_multiview_multisampled_render_to_texture tests that run on both
 * OpenGL and OpenGL ES.
 */

#include "piglit-util-gl.h"

static inline const char *
mv_glsl_version(void)
{
	return piglit_is_gles() ? "#version 300 es\n" : "#version 330\n";
}

/* A default precision, which has to follow the extension directives. */
#define MV_PRECISION "precision highp float;\n"

/* Builds a program from a vertex and fragment shader body, each prefixed with
 * the GLSL version line, the given extension directives, and MV_PRECISION.
 */
static inline GLuint
mv_program(const char *extensions, const char *vs_body, const char *fs_body)
{
	char *vs, *fs;
	GLuint prog;

	(void)!asprintf(&vs, "%s%s" MV_PRECISION "%s", mv_glsl_version(),
			extensions, vs_body);
	(void)!asprintf(&fs, "%s%s" MV_PRECISION "%s", mv_glsl_version(),
			extensions, fs_body);
	prog = piglit_build_simple_program(vs, fs);
	free(vs);
	free(fs);
	return prog;
}

/* A 2D array texture of the given format, with NEAREST filtering; only
 * RGBA8, DEPTH_COMPONENT24, and DEPTH24_STENCIL8 are handled.
 */
static inline GLuint
mv_array_texture(GLenum internalformat, int w, int h, int layers)
{
	GLenum format = GL_RGBA, type = GL_UNSIGNED_BYTE;
	GLuint tex;

	if (internalformat == GL_DEPTH_COMPONENT24) {
		format = GL_DEPTH_COMPONENT;
		type = GL_UNSIGNED_INT;
	} else if (internalformat == GL_DEPTH24_STENCIL8) {
		format = GL_DEPTH_STENCIL;
		type = GL_UNSIGNED_INT_24_8;
	}

	glGenTextures(1, &tex);
	glBindTexture(GL_TEXTURE_2D_ARRAY, tex);
	glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, internalformat, w, h, layers, 0,
		     format, type, NULL);
	glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	return tex;
}

static inline GLint
mv_max_views(void)
{
	GLint max = 0;

	glGetIntegerv(GL_MAX_VIEWS_OVR, &max);
	return max;
}

/* Binds a new framebuffer with one layer of a level of tex, for reading it
 * back.
 */
static inline GLuint
mv_level_layer_framebuffer(GLuint tex, int level, int layer)
{
	GLuint fb;

	glGenFramebuffers(1, &fb);
	glBindFramebuffer(GL_FRAMEBUFFER, fb);
	glFramebufferTextureLayer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, tex, level, layer);
	return fb;
}

static inline GLuint
mv_layer_framebuffer(GLuint tex, int layer)
{
	return mv_level_layer_framebuffer(tex, 0, layer);
}

/* Probes a w×h rectangle of one layer of a level of tex, reading it through
 * its own framebuffer, and restores the draw and read framebuffer bindings.
 */
static inline bool
mv_probe_level_layer(GLuint tex, int level, int layer, int x, int y, int w,
		     int h, const float *expected)
{
	GLint draw, read;
	GLuint fb;
	bool pass;

	glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &draw);
	glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &read);
	fb = mv_level_layer_framebuffer(tex, level, layer);
	pass = piglit_probe_rect_rgba(x, y, w, h, expected);
	if (!pass)
		printf("  in level %d, layer %d\n", level, layer);
	glDeleteFramebuffers(1, &fb);
	glBindFramebuffer(GL_DRAW_FRAMEBUFFER, draw);
	glBindFramebuffer(GL_READ_FRAMEBUFFER, read);
	return pass;
}

static inline bool
mv_probe_layer(GLuint tex, int layer, int x, int y, int w, int h,
	       const float *expected)
{
	return mv_probe_level_layer(tex, 0, layer, x, y, w, h, expected);
}

static inline bool
mv_check_status(GLenum expected)
{
	GLenum status = glCheckFramebufferStatus(GL_DRAW_FRAMEBUFFER);

	if (status != expected)
		printf("framebuffer status is %s, expected %s\n",
		       piglit_get_gl_enum_name(status),
		       piglit_get_gl_enum_name(expected));
	return status == expected;
}

/* Runs the selected subtests with the test's piglit_config. */
#define mv_run_subtests(subtests)                                            \
	piglit_run_selected_subtests(subtests,                               \
				     piglit_config->selected_subtests,       \
				     piglit_config->num_selected_subtests,   \
				     PIGLIT_SKIP)
