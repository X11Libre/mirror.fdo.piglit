/* SPDX-License-Identifier: MIT */

/**
 * Swap several frames, then read the back buffer before drawing anything
 * new. The back buffer contents are undefined after a swap, so the read is
 * not checked, but it must not change what the window shows: the window
 * and the front buffer must both still hold the frame that was swapped last.
 *
 * An application grabbing frames does this, and it caught a driver that
 * presented every older swapchain image again to get the last one back.
 *
 * The read uses GL_RGB, which a driver usually serves by mapping the window
 * buffer; -rgba reads GL_RGBA, which usually goes through a copy instead.
 */

#include "piglit-util-gl.h"
#include "piglit-glx-util.h"

#include <unistd.h>

static Window win;
static GLenum read_format = GL_RGB;

static const float colors[][4] = {
	{ 1.0f, 0.0f, 0.0f, 1.0f },
	{ 0.0f, 1.0f, 0.0f, 1.0f },
	{ 0.0f, 0.0f, 1.0f, 1.0f },
	{ 1.0f, 1.0f, 0.0f, 1.0f },
	{ 0.0f, 1.0f, 1.0f, 1.0f },
};

static float
channel(unsigned long pixel, unsigned long mask)
{
	if (!mask)
		return 0.0f;
	unsigned shift = ffsl(mask) - 1;
	unsigned long max = mask >> shift;
	return (float)((pixel & mask) >> shift) / max;
}

/* the window's center pixel as the X server has it */
static bool
probe_window(Display *dpy, const float *expected, const char *step,
	     unsigned round)
{
	XWindowAttributes attr;

	/* give any presentation time to land before looking at the window */
	glFinish();
	usleep(300000);

	XGetWindowAttributes(dpy, win, &attr);
	XImage *img = XGetImage(dpy, win, piglit_width / 2, piglit_height / 2,
				1, 1, AllPlanes, ZPixmap);
	if (!img) {
		printf("round %u: XGetImage failed\n", round);
		return false;
	}
	unsigned long pixel = XGetPixel(img, 0, 0);
	float got[3] = {
		channel(pixel, attr.visual->red_mask),
		channel(pixel, attr.visual->green_mask),
		channel(pixel, attr.visual->blue_mask),
	};
	XDestroyImage(img);

	for (unsigned c = 0; c < 3; c++) {
		if (fabsf(got[c] - expected[c]) > 0.05f) {
			printf("round %u, %s: window shows %f %f %f, expected %f %f %f\n",
			       round, step, got[0], got[1], got[2],
			       expected[0], expected[1], expected[2]);
			return false;
		}
	}
	return true;
}

static enum piglit_result
draw(Display *dpy)
{
	const unsigned last = ARRAY_SIZE(colors) - 1;
	GLubyte *pixels = malloc(piglit_width * piglit_height * 4);
	bool pass = true;

	glPixelStorei(GL_PACK_ALIGNMENT, 1);
	glDrawBuffer(GL_BACK);
	for (unsigned round = 0; round < 3; round++) {
		/* more frames than a swapchain has images, each one different */
		for (unsigned i = 0; i <= last; i++) {
			glClearColor(colors[i][0], colors[i][1], colors[i][2],
				     colors[i][3]);
			glClear(GL_COLOR_BUFFER_BIT);
			glXSwapBuffers(dpy, win);
		}

		pass &= probe_window(dpy, colors[last], "after swapping", round);

		glReadBuffer(GL_BACK);
		glReadPixels(0, 0, piglit_width, piglit_height, read_format,
			     GL_UNSIGNED_BYTE, pixels);
		/* undefined contents, only reported */
		const unsigned cpp = read_format == GL_RGB ? 3 : 4;
		const GLubyte *c = pixels + cpp * (piglit_height / 2 * piglit_width +
						   piglit_width / 2);
		printf("round %u: back read returned %u %u %u\n",
		       round, c[0], c[1], c[2]);
		pass &= probe_window(dpy, colors[last], "after reading the back",
				     round);

		glReadBuffer(GL_FRONT);
		if (!piglit_probe_rect_rgb(0, 0, piglit_width, piglit_height,
					   colors[last])) {
			printf("round %u: front buffer is wrong\n", round);
			pass = false;
		}
		/* and in the format of the back read, which can take another path */
		glReadPixels(0, 0, piglit_width, piglit_height, read_format,
			     GL_UNSIGNED_BYTE, pixels);
		for (unsigned ch = 0; ch < 3; ch++) {
			if (abs(c[ch] - (int)(colors[last][ch] * 255.0f)) > 2) {
				printf("round %u: front read in the back read's "
				       "format returned %u %u %u\n",
				       round, c[0], c[1], c[2]);
				pass = false;
				break;
			}
		}
		pass &= probe_window(dpy, colors[last], "after reading the front",
				     round);
	}

	free(pixels);
	pass &= piglit_check_gl_error(GL_NO_ERROR);
	return pass ? PIGLIT_PASS : PIGLIT_FAIL;
}

int
main(int argc, char **argv)
{
	for (int i = 1; i < argc; i++) {
		if (strcmp(argv[i], "-auto") == 0)
			piglit_automatic = 1;
		else if (strcmp(argv[i], "-rgba") == 0)
			read_format = GL_RGBA;
	}

	Display *dpy = XOpenDisplay(NULL);
	if (!dpy) {
		fprintf(stderr, "Failed to open display\n");
		piglit_report_result(PIGLIT_FAIL);
	}

	XVisualInfo *visinfo = piglit_get_glx_visual(dpy);
	win = piglit_get_glx_window(dpy, visinfo);
	GLXContext ctx = piglit_get_glx_context(dpy, visinfo);

	glXMakeCurrent(dpy, win, ctx);
	piglit_dispatch_default_init(PIGLIT_DISPATCH_GL);

	piglit_glx_event_loop(dpy, draw);

	return 0;
}
