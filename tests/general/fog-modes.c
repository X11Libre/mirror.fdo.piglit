/*
 * Copyright © 2010 Intel Corporation
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

/** @file fog-modes.c
 *
 * Tests that the three fog modes work with fog enabled using the depth value.
 */

#include "piglit-util-gl.h"

PIGLIT_GL_TEST_CONFIG_BEGIN

	config.supports_gl_compat_version = 10;

	config.window_visual = PIGLIT_GL_VISUAL_RGBA | PIGLIT_GL_VISUAL_DOUBLE;

PIGLIT_GL_TEST_CONFIG_END

static bool arbfp = false;

enum piglit_result
piglit_display(void)
{
	GLboolean pass = GL_TRUE;
	int i, mode;
	float zNear = 0.0, zFar = 1.0;
	GLfloat fogcolor[4] = {1, 1, 1, 1};

	glEnable(GL_FOG);
	glFogfv(GL_FOG_COLOR, fogcolor);

	/* Set the color coming out of the FF fragment stage -- red (unused) for when we should
	 * be using an ARB program, black otherwise.
	 */
	if (arbfp)
		glColor4f(0, 1, 0, 0);
	else
		glColor4f(0, 0, 0, 0.5);

	glMatrixMode(GL_PROJECTION);
	glLoadIdentity();
	glOrtho(0, 1, 0, 1, -zNear, -zFar);

	GLuint arb_program = 0;
	if (arbfp) {
		glGenProgramsARB(1, &arb_program);
		glBindProgramARB(GL_FRAGMENT_PROGRAM_ARB, arb_program);
		glEnable(GL_FRAGMENT_PROGRAM_ARB);
	}

	for (mode = 0; mode < 3; mode++) {
		float y = mode / 3.0;
		float h = 1.0 / 3.0;

		if (arbfp) {
			static const char *fog_modes[] = {
			    "ARB_fog_linear",
			    "ARB_fog_exp",
			    "ARB_fog_exp2",
			};
			char *fp_source = NULL;
			int ret = asprintf(&fp_source,
					   "!!ARBfp1.0\n"
					   "OPTION %s;\n"
					   "MOV result.color, {0.0, 0.0, 0.0, 0.5};\n"
					   "END\n",
					   fog_modes[mode]);
			if (ret < 0)
				return PIGLIT_FAIL;

			glProgramStringARB(GL_FRAGMENT_PROGRAM_ARB,
					GL_PROGRAM_FORMAT_ASCII_ARB,
					strlen(fp_source),
					(const GLubyte *) fp_source);
			free(fp_source);
		}

		switch (mode) {
		case 0:
			if (!arbfp)
				glFogi(GL_FOG_MODE, GL_LINEAR);
			glFogf(GL_FOG_START, zNear);
			glFogf(GL_FOG_END, zFar);
			break;
		case 1:
			if (!arbfp)
				glFogi(GL_FOG_MODE, GL_EXP);
			glFogf(GL_FOG_DENSITY, 2);
			break;
		case 2:
			if (!arbfp)
				glFogi(GL_FOG_MODE, GL_EXP2);
			glFogf(GL_FOG_DENSITY, 2);
			break;
		}

		for (i = 0; i < 5; i++) {
			float x = i / 5.0;
			float w = 1.0 / 5.0;
			float z = zNear + (zFar - zNear) * i / 5.0;

			piglit_draw_rect_z(z, x, y, w, h);
		}
	}

	if (arbfp)
		glDeleteProgramsARB(1, &arb_program);

	for (mode = 0; mode < 3; mode++) {
		float y = (mode + 0.5) / 3.0 * piglit_height;
		for (i = 0; i < 5; i++) {
			float x = (i + 0.5) / 5.0 * piglit_width;
			float z = zNear + (zFar - zNear) * i / 5.0;
			float f;
			float color[4];

			switch (mode) {
			case 0:
				f = (zFar - z) / (zFar - zNear);
				break;
			case 1:
				f = expf(-(2.0 * z));
				break;
			case 2:
				f = expf(-(2.0 * z) * (2.0 * z));
				break;
			}
			if (f > 1.0)
				f = 1.0;
			if (f < 0.0)
				f = 0.0;

			color[0] = 1.0 - f;
			color[1] = 1.0 - f;
			color[2] = 1.0 - f;
			color[3] = 0.5;

			pass = pass & piglit_probe_pixel_rgba(x, y, color);
		}
	}

	piglit_present_results();

	return pass ? PIGLIT_PASS : PIGLIT_FAIL;
}

void piglit_init(int argc, char**argv)
{
	for (int i = 0; i < argc; i++) {
		if (strcmp(argv[i], "-arbfp") == 0) {
			piglit_require_extension("GL_ARB_fragment_program");
			arbfp = true;
		}
	}
}
