/* SPDX-License-Identifier: MIT */

/**
 * Draw to the front buffer, swap, then flush the front buffer again, either
 * with glFlush or by making another context current on the same window.
 * The front buffer and the window must both show the frame that was swapped.
 *
 * An application switching contexts every frame does this, and it caught a
 * driver presenting a stale image for the second flush.
 */

#include "piglit-util-gl.h"
#include "piglit-glx-util.h"

#include <unistd.h>

static Window win;
static GLXContext ctx[2];

static const float marker[4] = { 0.5f, 0.5f, 0.5f, 1.0f };
static const float colors[][4] = {
	{ 1.0f, 0.0f, 0.0f, 1.0f },
	{ 0.0f, 1.0f, 0.0f, 1.0f },
	{ 0.0f, 0.0f, 1.0f, 1.0f },
	{ 1.0f, 1.0f, 0.0f, 1.0f },
	{ 0.0f, 1.0f, 1.0f, 1.0f },
	{ 1.0f, 0.0f, 1.0f, 1.0f },
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
probe_window(Display *dpy, const float *expected, unsigned frame)
{
	XWindowAttributes attr;
	XGetWindowAttributes(dpy, win, &attr);
	XImage *img = XGetImage(dpy, win, piglit_width / 2, piglit_height / 2,
				1, 1, AllPlanes, ZPixmap);
	if (!img) {
		printf("frame %u: XGetImage failed\n", frame);
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
			printf("frame %u: window shows %f %f %f, expected %f %f %f\n",
			       frame, got[0], got[1], got[2],
			       expected[0], expected[1], expected[2]);
			return false;
		}
	}
	return true;
}

static enum piglit_result
draw(Display *dpy)
{
	bool pass = true;

	for (unsigned i = 0; i < ARRAY_SIZE(colors); i++) {
		glXMakeCurrent(dpy, win, ctx[0]);

		/* draw to the front so that it gets flushed again after the swap */
		piglit_ortho_projection(piglit_width, piglit_height, GL_FALSE);
		glDrawBuffer(GL_FRONT);
		glColor4fv(marker);
		piglit_draw_rect(0, 0, piglit_width, piglit_height);

		glDrawBuffer(GL_BACK);
		glClearColor(colors[i][0], colors[i][1], colors[i][2], colors[i][3]);
		glClear(GL_COLOR_BUFFER_BIT);
		glXSwapBuffers(dpy, win);

		if (i % 2)
			glXMakeCurrent(dpy, win, ctx[1]);
		else
			glFlush();

		glReadBuffer(GL_FRONT);
		if (!piglit_probe_rect_rgb(0, 0, piglit_width, piglit_height, colors[i])) {
			printf("frame %u: front buffer is wrong\n", i);
			pass = false;
		}

		/* give the presentation time to land before looking at the window */
		glFinish();
		usleep(300000);
		pass &= probe_window(dpy, colors[i], i);
	}

	return pass ? PIGLIT_PASS : PIGLIT_FAIL;
}

int
main(int argc, char **argv)
{
	for (int i = 1; i < argc; i++) {
		if (strcmp(argv[i], "-auto") == 0)
			piglit_automatic = 1;
	}

	Display *dpy = XOpenDisplay(NULL);
	if (!dpy) {
		fprintf(stderr, "Failed to open display\n");
		piglit_report_result(PIGLIT_FAIL);
	}

	XVisualInfo *visinfo = piglit_get_glx_visual(dpy);
	win = piglit_get_glx_window(dpy, visinfo);
	ctx[0] = piglit_get_glx_context(dpy, visinfo);
	ctx[1] = piglit_get_glx_context(dpy, visinfo);

	glXMakeCurrent(dpy, win, ctx[0]);
	piglit_dispatch_default_init(PIGLIT_DISPATCH_GL);

	piglit_glx_event_loop(dpy, draw);

	return 0;
}
