/*
 * Copyright © 2026 Timothy Arceri
 *
 * SPDX-License-Identifier: MIT
 */

/**
 * \file compute-image-store.c
 *
 * Clear a texture with glClearTexImage, overwrite every texel with
 * imageStore from a compute shader, and check that texture fetches only
 * see the values written by the shader.  The texture is checked by drawing
 * it to the window, as the application in the bug report did.
 *
 * On drivers that implement the clear by clearing compression metadata,
 * the image stores must not be combined with the metadata from before
 * the clear.  The sequence is repeated a few times.
 *
 * Reported in https://gitlab.freedesktop.org/mesa/mesa/-/work_items/13598
 */

#include "piglit-util-gl.h"

PIGLIT_GL_TEST_CONFIG_BEGIN

	config.supports_gl_core_version = 43;
	config.window_visual = PIGLIT_GL_VISUAL_RGBA |
			       PIGLIT_GL_VISUAL_DOUBLE;
	config.khr_no_error_support = PIGLIT_NO_ERRORS;

PIGLIT_GL_TEST_CONFIG_END

#define ITERATIONS 32

static const char *store_src =
	"#version 430\n"
	"layout(local_size_x = 8, local_size_y = 8) in;\n"
	"layout(binding = 0, rgba16ui) writeonly restrict\n"
	"	uniform uimage2D img;\n"
	"uniform uint value;\n"
	"void main() {\n"
	"	ivec2 coord = ivec2(gl_GlobalInvocationID.xy);\n"
	"	imageStore(img, coord, uvec4(value));\n"
	"}\n";

static const char *vs_src =
	"#version 430\n"
	"in vec4 piglit_vertex;\n"
	"void main() {\n"
	"	gl_Position = piglit_vertex;\n"
	"}\n";

/* Green where the texel has the stored value.  Elsewhere red, with the
 * texel's value in the green channel, so texels that still have the clear
 * value (0) are pure red.
 */
static const char *fs_src =
	"#version 430\n"
	"layout(binding = 0) uniform usampler2D tex;\n"
	"uniform uint value;\n"
	"out vec4 color;\n"
	"void main() {\n"
	"	uvec4 v = texelFetch(tex, ivec2(gl_FragCoord.xy), 0);\n"
	"	if (v == uvec4(value))\n"
	"		color = vec4(0.0, 1.0, 0.0, 1.0);\n"
	"	else\n"
	"		color = vec4(1.0, float(v.x) / 64.0, 0.0, 1.0);\n"
	"}\n";

static GLuint store_prog, draw_prog, tex;
static GLint store_value_loc, draw_value_loc;

void
piglit_init(int argc, char **argv)
{
	piglit_require_extension("GL_ARB_clear_texture");

	store_prog = piglit_build_compute_program(store_src);
	draw_prog = piglit_build_simple_program(vs_src, fs_src);
	store_value_loc = glGetUniformLocation(store_prog, "value");
	draw_value_loc = glGetUniformLocation(draw_prog, "value");

	glGenTextures(1, &tex);
	glBindTexture(GL_TEXTURE_2D, tex);
	glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA16UI, piglit_width,
		       piglit_height);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
}

enum piglit_result
piglit_display(void)
{
	static const GLushort zeros[4] = {0, 0, 0, 0};
	static const float green[4] = {0.0, 1.0, 0.0, 1.0};
	bool pass = true;

	for (unsigned i = 0; i < ITERATIONS && pass; i++) {
		const GLuint value = i + 1;

		glClearTexImage(tex, 0, GL_RGBA_INTEGER, GL_UNSIGNED_SHORT,
				zeros);

		glUseProgram(store_prog);
		glUniform1ui(store_value_loc, value);
		glBindImageTexture(0, tex, 0, GL_FALSE, 0, GL_WRITE_ONLY,
				   GL_RGBA16UI);
		glDispatchCompute((piglit_width + 7) / 8,
				  (piglit_height + 7) / 8, 1);

		glMemoryBarrier(GL_TEXTURE_FETCH_BARRIER_BIT);

		/* The application in the bug report cleared its framebuffer
		 * here, which makes the bug much more likely to show up.
		 */
		glClear(GL_COLOR_BUFFER_BIT);

		glUseProgram(draw_prog);
		glUniform1ui(draw_value_loc, value);
		glBindTexture(GL_TEXTURE_2D, tex);
		piglit_draw_rect(-1, -1, 2, 2);

		if (!piglit_probe_rect_rgba(0, 0, piglit_width, piglit_height,
					    green)) {
			printf("Failed at iteration %u\n", i);
			pass = false;
		}
	}

	pass = piglit_check_gl_error(GL_NO_ERROR) && pass;

	piglit_present_results();

	return pass ? PIGLIT_PASS : PIGLIT_FAIL;
}
