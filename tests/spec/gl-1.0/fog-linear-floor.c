/*
 * Copyright (C) 2026 Valve Corporation
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
 */

/**
 * @file fog-linear-floor.c
 *
 * Test fixed-function linear fog on a large, coarsely-tessellated floor quad
 * that straddles the eye plane -- the classic "ground you stand on" case.
 * Regression test for mesa issue #15407.
 *
 * The default fog coordinate for GL_FRAGMENT_DEPTH is the eye-space distance,
 * i.e. abs(Ze) (the GL_EYE_PLANE_ABSOLUTE_NV distance mode).
 *
 * A ground plane the camera stands on has vertices both in front of the eye
 * (Ze < 0) and behind it (Ze > 0), i.e. it straddles the eye plane.  If a
 * driver computes abs(Ze) per vertex and interpolates the result, the pre-abs
 * endpoint values are all large while the true surface passes close to the eye
 * in between; interpolation then over-estimates the fog distance and fogs the
 * whole quad, even the part right under the camera that should be unfogged.
 *
 * The NV_fog_distance spec says:
 *
 *    "When the fog distance mode is EYE_PLANE_ABSOLUTE_NV, the fog
 *    distance z is approximated by abs(ze) [where ze is the Z component
 *    of the fragment's eye position]."
 *
 * The correct behaviour is to interpolate the signed eye-space Z and take the
 * absolute value per fragment, so the near part of the floor stays clear.
 *
 * The scene mirrors the reporter's application: a single quad stored in the XZ
 * plane (all Y = 0) scaled up with glScaled(64, 0, 64) and drawn with client
 * arrays, under black GL_LINEAR fog with start=15, end=45.
 */

#include <math.h>
#include "piglit-util-gl.h"

PIGLIT_GL_TEST_CONFIG_BEGIN
	config.supports_gl_compat_version = 10;
	config.window_width  = 256;
	config.window_height = 256;
	config.window_visual = PIGLIT_GL_VISUAL_RGB | PIGLIT_GL_VISUAL_DOUBLE |
	                       PIGLIT_GL_VISUAL_DEPTH;
	config.khr_no_error_support = PIGLIT_NO_ERRORS;
PIGLIT_GL_TEST_CONFIG_END

#define FOG_START 15.0f
#define FOG_END   45.0f

static const GLfloat FOG_COLOR[4] = { 0.0f, 0.0f, 0.0f, 1.0f };

/* Floor: unit quad in the XZ plane (Y == 0 for every vertex), exactly as in
 * the bug reporter's application. */
static const GLfloat VERT_FLOOR[] = {
	-1.0f, 0.0f, -1.0f,
	 1.0f, 0.0f, -1.0f,
	 1.0f, 0.0f,  1.0f,
	-1.0f, 0.0f,  1.0f,
};
static const GLushort INDICES_FLOOR[] = { 1, 0, 3, 3, 2, 1 };

/* Projection/view helpers. */
static void
perspective(GLfloat fovy_deg, GLfloat aspect, GLfloat near_val, GLfloat far_val)
{
	const GLfloat fovy_rad = fovy_deg * (GLfloat)M_PI / 180.0f;
	const GLfloat top = near_val * tanf(fovy_rad * 0.5f);
	const GLfloat right = top * aspect;
	glFrustum(-right, right, -top, top, near_val, far_val);
}

static void
look_at(GLfloat ex, GLfloat ey, GLfloat ez,
        GLfloat cx, GLfloat cy, GLfloat cz,
        GLfloat ux, GLfloat uy, GLfloat uz)
{
	GLfloat fx = cx - ex, fy = cy - ey, fz = cz - ez, inv_len;

	inv_len = 1.0f / sqrtf(fx*fx + fy*fy + fz*fz);
	fx *= inv_len; fy *= inv_len; fz *= inv_len;

	GLfloat sx = fy*uz - fz*uy;
	GLfloat sy = fz*ux - fx*uz;
	GLfloat sz = fx*uy - fy*ux;
	inv_len = 1.0f / sqrtf(sx*sx + sy*sy + sz*sz);
	sx *= inv_len; sy *= inv_len; sz *= inv_len;

	GLfloat vx = sy*fz - sz*fy;
	GLfloat vy = sz*fx - sx*fz;
	GLfloat vz = sx*fy - sy*fx;

	GLfloat m[16] = {
		 sx,  vx, -fx, 0.0f,
		 sy,  vy, -fy, 0.0f,
		 sz,  vz, -fz, 0.0f,
		0.0f, 0.0f, 0.0f, 1.0f,
	};
	glMultMatrixf(m);
	glTranslatef(-ex, -ey, -ez);
}

static void
enable_fog(void)
{
	glEnable(GL_FOG);
	glFogi(GL_FOG_MODE, GL_LINEAR);
	glHint(GL_FOG_HINT, GL_NICEST);
	glFogfv(GL_FOG_COLOR, FOG_COLOR);
	glFogf(GL_FOG_START, FOG_START);
	glFogf(GL_FOG_END, FOG_END);
	glEnable(GL_DEPTH_TEST);
	glDepthFunc(GL_LEQUAL);
}

static void
draw_floor(void)
{
	glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	/* White floor so that any fogging shows up as a darkening. */
	glColor3f(1.0f, 1.0f, 1.0f);

	glPushMatrix();
	glScaled(64.0, 0.0, 64.0); /* matches the reporter's application */
	glEnableClientState(GL_VERTEX_ARRAY);
	glVertexPointer(3, GL_FLOAT, 0, VERT_FLOOR);
	glDrawElements(GL_TRIANGLES, ARRAY_SIZE(INDICES_FLOOR),
	               GL_UNSIGNED_SHORT, INDICES_FLOOR);
	glDisableClientState(GL_VERTEX_ARRAY);
	glPopMatrix();
}

/*
 * The regression sub-test.
 *
 * The camera sits just above the floor (eye height 4) looking forward and
 * slightly down, so the quad straddles the eye plane.  The lower part of the
 * framebuffer shows floor close to the camera (eye distance well below
 * FOG_START), which must therefore be unfogged (bright).  The buggy per-vertex
 * abs() path fogs the entire floor to the black fog colour.
 */
static bool
test_straddling_floor_near_is_unfogged(void)
{
	glViewport(0, 0, piglit_width, piglit_height);
	glMatrixMode(GL_PROJECTION);
	glLoadIdentity();
	perspective(60.0f, (GLfloat)piglit_width / piglit_height, 1.0f, 100.0f);
	glMatrixMode(GL_MODELVIEW);
	glLoadIdentity();
	look_at(0.0f, 4.0f,  20.0f,
	        0.0f, 0.0f, -20.0f,
	        0.0f, 1.0f,  0.0f);
	enable_fog();
	draw_floor();

	/* A point low in the frame is floor close to the camera: it must be
	 * bright (unfogged).  Bug #15407 makes it black. */
	const GLfloat white[3] = { 1.0f, 1.0f, 1.0f };
	const GLfloat tol = 0.1f;
	GLfloat px[3];
	glReadPixels(piglit_width / 2, piglit_height / 8, 1, 1,
	             GL_RGB, GL_FLOAT, px);

	bool pass = piglit_compare_pixels_float(px, white, &tol, 3);
	if (!pass)
		fprintf(stderr,
		        "near floor pixel = (%.3f, %.3f, %.3f), expected white.\n"
		        "  A black pixel is the #15407 regression: fixed-function\n"
		        "  fog computed abs(eye Z) per vertex over-fogs a floor\n"
		        "  quad that straddles the eye plane.\n",
		        px[0], px[1], px[2]);

	piglit_report_subtest_result(pass ? PIGLIT_PASS : PIGLIT_FAIL,
	                             "straddling-floor-near-unfogged");
	piglit_present_results();
	return pass;
}

/*
 * Sanity sub-test: fog is genuinely being applied.
 *
 * Look straight down from high above (eye height 60), so the entire floor lies
 * beyond FOG_END and its eye distance is uniform.  The whole floor must be the
 * fog colour (black).  This guards against a false pass of the test above
 * caused by fog being effectively disabled.
 */
static bool
test_distant_floor_is_fully_fogged(void)
{
	glViewport(0, 0, piglit_width, piglit_height);
	glMatrixMode(GL_PROJECTION);
	glLoadIdentity();
	perspective(60.0f, (GLfloat)piglit_width / piglit_height, 1.0f, 200.0f);
	glMatrixMode(GL_MODELVIEW);
	glLoadIdentity();
	look_at(0.0f, 60.0f, 0.0f,
	        0.0f,  0.0f, 0.0f,
	        0.0f,  0.0f, -1.0f);
	enable_fog();
	draw_floor();

	const GLfloat black[3] = { 0.0f, 0.0f, 0.0f };
	const GLfloat tol = 0.1f;
	GLfloat px[3];
	glReadPixels(piglit_width / 2, piglit_height / 2, 1, 1,
	             GL_RGB, GL_FLOAT, px);

	bool pass = piglit_compare_pixels_float(px, black, &tol, 3);
	if (!pass)
		fprintf(stderr,
		        "distant floor pixel = (%.3f, %.3f, %.3f), expected black "
		        "(fully fogged beyond FOG_END).\n",
		        px[0], px[1], px[2]);

	piglit_report_subtest_result(pass ? PIGLIT_PASS : PIGLIT_FAIL,
	                             "distant-floor-fully-fogged");
	piglit_present_results();
	return pass;
}

void
piglit_init(int argc, char **argv)
{
	(void)argc; (void)argv;
	piglit_require_gl_version(10);
}

enum piglit_result
piglit_display(void)
{
	bool pass = true;

	pass &= test_distant_floor_is_fully_fogged();
	pass &= test_straddling_floor_near_is_unfogged();

	return pass ? PIGLIT_PASS : PIGLIT_FAIL;
}
