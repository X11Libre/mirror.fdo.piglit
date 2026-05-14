/* Copyright © 2005 Brian Paul
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

/** @file point-attenuation.c
 *
 * Test GL_ARB_point_parameters extension.
 */

#include <math.h>

#include "piglit-util-gl.h"

/* Max tested point size */
#define MAX_SIZE 24.0

/*
 * Number of (min, max, size) combinations drawn as columns per frame.
 * min in {1, 11, 21}, max in {min, min+10, ...} < MAX_SIZE, size in
 * {1, 9, 17}: that's 6 min/max pairs times 3 sizes = 18 columns.
 */
#define NUM_COLS 18
#define COL_WIDTH ((int)(MAX_SIZE) + 4)

#define windowWidth (NUM_COLS * COL_WIDTH)
#define windowHeight 503

PIGLIT_GL_TEST_CONFIG_BEGIN
	config.supports_gl_compat_version = 10;
	config.window_visual = PIGLIT_GL_VISUAL_DOUBLE | PIGLIT_GL_VISUAL_RGBA;
	config.window_width = windowWidth;
	config.window_height = windowHeight;
PIGLIT_GL_TEST_CONFIG_END

/* Clamp X to [MIN,MAX] */
#define CLAMP( X, MIN, MAX )  ( (X)<(MIN) ? (MIN) : ((X)>(MAX) ? (MAX) : (X)) )

GLfloat aliasedLimits[2];  /* min/max */
GLfloat smoothLimits[2];   /* min/max */

struct col_params {
	float min, max, size;
};

static void
reportFailure(GLfloat initSize, const GLfloat attenuation[3],
			  GLfloat min, GLfloat max, GLfloat eyeZ,
			  GLfloat expected, GLfloat actual)
{
	fprintf(stderr, "Expected size: %f Actual size: %f\n", expected, actual);
	fprintf(stderr, "Size: %f\n", initSize);
	fprintf(stderr, "Min: %f Max %f\n", min, max);
	fprintf(stderr, "Attenuation %f %f %f\n", attenuation[0] , attenuation[1], attenuation[2]);
	fprintf(stderr, "Eye Z: %f\n", eyeZ);
}


/* Compute the expected point size given various point state */
static GLfloat
expectedSize(GLfloat initSize,
			 const GLfloat attenuation[3],
			 GLfloat min, GLfloat max,
			 GLfloat eyeZ, GLboolean smooth)
{
	const GLfloat dist = fabs(eyeZ);
	const GLfloat atten = sqrt(1.0 / (attenuation[0] +
					  attenuation[1] * dist +
					  attenuation[2] * dist * dist));

	float size = initSize * atten;

	size = CLAMP(size, min, max);

	if (smooth)
		size = CLAMP(size, smoothLimits[0], smoothLimits[1]);
	else
		size = CLAMP(size, aliasedLimits[0], aliasedLimits[1]);
	return size;
}


/* Model-space X coordinate for a given column index */
static float
col_x(int col)
{
	return (col * COL_WIDTH + COL_WIDTH / 2.0f) /
		(float)windowWidth * 20.0f - 10.0f;
}


/* Measure size of rendered point at yPos within a single column */
static GLfloat
measureSize(float *pixels, GLfloat yPos, int col)
{
	assert(yPos >= -10.0);
	assert(yPos <= 10.0);
	float yNdc = (yPos + 10.0) / 20.0;  /* See glOrtho below */
	int y = (int) (yNdc * windowHeight);
	int x_start = col * COL_WIDTH;
	int x_end = x_start + COL_WIDTH;

	/* Add up colors in 3 rows, and use the row with the greatest sum.
	 * This helps gives us a bit of leeway in vertical point positioning.
	 * Colors should be white or shades of gray if smoothing is enabled.
     	 */
	float *image = pixels + (y - 1) * windowWidth * 3;
	float sum[3] = {0.0, 0.0, 0.0};
	for (int j = 0; j < 3; j++) {
		for (int i = x_start; i < x_end; i++) {
			int k = j * 3 * windowWidth + i * 3;
			sum[j] += (image[k+0] + image[k+1] + image[k+2]) / 3.0;
		}
	}

	/* find max of the row sums */
	if (sum[0] >= sum[1] && sum[0] >= sum[2])
		return sum[0];
	else if (sum[1] >= sum[0] && sum[1] >= sum[2])
		return sum[1];
	else
		return sum[2];
}


static bool
testPointRendering(bool smooth)
{
	/* epsilon is the allowed size difference in pixels between the
	 * expected and actual rendering.
     */
	const GLfloat epsilon = (smooth ? 1.5 : 1.0) + 0.0;
	GLfloat atten[3];
	struct col_params cols[NUM_COLS];

	if (smooth) {
		glEnable(GL_POINT_SMOOTH);
		glEnable(GL_BLEND);
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	}
	else {
		glDisable(GL_POINT_SMOOTH);
		glDisable(GL_BLEND);
	}

	float *pixels = malloc(piglit_width * piglit_height * 3 * sizeof(float));

	for (int a = 0; a < 3; a++) {
		atten[0] = pow(10.0, -a);
		for (int b = -2; b < 3; b++) {
			atten[1] = (b == -1) ? 0.0 : pow(10.0, -b);
			for (int c = -2; c < 3; c++) {
				atten[2] = (c == -1) ? 0.0 : pow(10.0, -c);
				glPointParameterfvARB(GL_POINT_DISTANCE_ATTENUATION_ARB, atten);

				/* draw all (min, max, size) combos as columns */
				glClear(GL_COLOR_BUFFER_BIT);
				int col = 0;
				for (float min = 1.0; min < MAX_SIZE; min += 10) {
					for (float max = min; max < MAX_SIZE; max += 10) {
						for (float size = 1.0; size < MAX_SIZE; size += 8) {
							cols[col].min = min;
							cols[col].max = max;
							cols[col].size = size;

							glPointParameterfARB(GL_POINT_SIZE_MIN_ARB, min);
							glPointParameterfARB(GL_POINT_SIZE_MAX_ARB, max);
							glPointSize(size);

							float mx = col_x(col);
							glBegin(GL_POINTS);
							for (float z = -6.0; z <= 6.0; z += 1.0)
								glVertex3f(mx, z, z);
							glEnd();

							col++;
						}
					}
				}

				glReadPixels(0, 0, piglit_width, piglit_height,
					     GL_RGB, GL_FLOAT, pixels);

				/* verify all columns */
				for (col = 0; col < NUM_COLS; col++) {
					for (float z = -6.0; z <= 6.0; z += 1.0) {
						float expected
							= expectedSize(cols[col].size,
									atten,
									cols[col].min,
									cols[col].max,
									z, smooth);
						float actual = measureSize(pixels, z, col);
						if (fabs(expected - actual) > epsilon) {
							reportFailure(cols[col].size,
								      atten,
								      cols[col].min,
								      cols[col].max,
								      z, expected,
								      actual);
							free(pixels);
							return false;
						}
					}
				}
			}
		}
	}

	free(pixels);

	if (!piglit_check_gl_error(0))
		return false;

	return true;
}

enum piglit_result
piglit_display(void)
{
	bool pass = testPointRendering(false);
	piglit_report_subtest_result(pass ? PIGLIT_PASS : PIGLIT_FAIL,
			"Aliased combinations");

	bool pass2 = testPointRendering(true);
	piglit_report_subtest_result(pass2 ? PIGLIT_PASS : PIGLIT_FAIL,
			"Antialiased combinations");

	return pass && pass2 ? PIGLIT_PASS : PIGLIT_FAIL;
}

void
piglit_init(int argc, char **argv)
{
	piglit_require_extension("GL_ARB_point_parameters");

	for (int i = 0; i < 2; i++) {
		aliasedLimits[i] = 0;
		smoothLimits[i] = 0;
	}

	glGetFloatv(GL_ALIASED_POINT_SIZE_RANGE, aliasedLimits);
	glGetFloatv(GL_SMOOTH_POINT_SIZE_RANGE, smoothLimits);
	glMatrixMode(GL_PROJECTION);
	glLoadIdentity();
	glOrtho(-10.0, 10.0, -10.0, 10.0, -10.0, 10.0);
	glMatrixMode(GL_MODELVIEW);
	glLoadIdentity();

	if (!piglit_check_gl_error(0))
		piglit_report_result(PIGLIT_FAIL);
}
