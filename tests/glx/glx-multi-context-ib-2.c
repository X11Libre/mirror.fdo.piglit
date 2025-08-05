/*
 * Copyright © 2009-2011 Intel Corporation
 * Copyright © 2025 Valve Corporation
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

/** @file glx-multi-context-ib2.c
 *
 * Tests that multiple non-flushing contexts destroying index buffers work correctly.
 *
 * This should catch the scenario where:
   - the driver enqueues commands using an index buffer on context A
   - context A is not flushed
   - context B destroys the index buffer
   - context B flushes
   - context A flushes
   - the index buffer is magically destroyed without crashing
 */

#include <unistd.h>
#include "piglit-util-gl.h"
#include "piglit-glx-util.h"

int piglit_width = 50, piglit_height = 50;
static Display *dpy;
static Window win;
static GLXContext ctx0, ctx1;

static GLuint vb_c0, ib_c0;
static float green[] = {0, 1, 0, 0};
static float red[]   = {1, 0, 0, 0};
static PFNGLXCREATECONTEXTATTRIBSARBPROC CreateContextAttribs = NULL;

static void
context0_init()
{
	float vb_data[] = {
		-1, -1,
		 1, -1,
		 1,  1,
		-1,  1,
	};
	uint32_t ib_data[] = {
		0, 1, 2, 3,
	};

	glGenBuffersARB(1, &vb_c0);
	glGenBuffersARB(1, &ib_c0);
	glBindBufferARB(GL_ARRAY_BUFFER_ARB, vb_c0);
	glBindBufferARB(GL_ELEMENT_ARRAY_BUFFER_ARB, ib_c0);

	glBufferDataARB(GL_ARRAY_BUFFER_ARB, sizeof(vb_data), vb_data,
			GL_STATIC_DRAW);
	glBufferDataARB(GL_ELEMENT_ARRAY_BUFFER_ARB, sizeof(ib_data), ib_data,
			GL_STATIC_DRAW);
}

static void
context0_frame(void)
{
	glXMakeCurrent(dpy, win, ctx0);

	glColor4fv(red);

	glBindBufferARB(GL_ARRAY_BUFFER_ARB, vb_c0);
	glBindBufferARB(GL_ELEMENT_ARRAY_BUFFER_ARB, ib_c0);
	glEnableClientState(GL_VERTEX_ARRAY);
	glVertexPointer(2, GL_FLOAT, 0, (void *)0);

	glDrawElements(GL_TRIANGLE_FAN, 4, GL_UNSIGNED_INT, (void *)0);
	glBindBufferARB(GL_ARRAY_BUFFER_ARB, 0);
	glBindBufferARB(GL_ELEMENT_ARRAY_BUFFER_ARB, 0);
}

static void
context0_finish(void)
{
	glXMakeCurrent(dpy, win, ctx0);

	GLuint buffers[] = {vb_c0, ib_c0};
	glDeleteBuffersARB(2, buffers);
	glFinish();
}

static void
context1_frame(void)
{
	glXMakeCurrent(dpy, win, ctx1);

	glColor4fv(green);

	glBindBufferARB(GL_ARRAY_BUFFER_ARB, vb_c0);
	glBindBufferARB(GL_ELEMENT_ARRAY_BUFFER_ARB, ib_c0);
	glEnableClientState(GL_VERTEX_ARRAY);
	glVertexPointer(2, GL_FLOAT, 0, (void *)0);

	glDrawElements(GL_TRIANGLE_FAN, 4, GL_UNSIGNED_INT, (void *)0);

	glBindBufferARB(GL_ARRAY_BUFFER_ARB, 0);
	glBindBufferARB(GL_ELEMENT_ARRAY_BUFFER_ARB, 0);
}

static void
context1_finish(void)
{
	glXMakeCurrent(dpy, win, ctx1);
	glFinish();
}


enum piglit_result
draw(Display *dpy)
{
	bool pass;

	context0_frame();
	context1_frame();
	context0_finish();
	context1_finish();

	pass = piglit_probe_rect_rgb(0, 0, piglit_width, piglit_height, green);

	glXSwapBuffers(dpy, win);

	glXMakeCurrent(dpy, None, None);
	if (piglit_automatic) {
		glXDestroyContext(dpy, ctx0);
		glXDestroyContext(dpy, ctx1);
	}

	return pass ? PIGLIT_PASS : PIGLIT_FAIL;
}

int
main(int argc, char **argv)
{
	XVisualInfo *visinfo;
	GLXFBConfig config;
	int i;

	for(i = 1; i < argc; ++i) {
		if (!strcmp(argv[i], "-auto"))
			piglit_automatic = 1;
		else
			fprintf(stderr, "Unknown option: %s\n", argv[i]);
	}

	dpy = piglit_get_glx_display();

	visinfo = piglit_get_glx_visual(dpy);
	win = piglit_get_glx_window(dpy, visinfo);
	config = piglit_glx_get_fbconfig_for_visinfo(dpy, visinfo);


	CreateContextAttribs = (PFNGLXCREATECONTEXTATTRIBSARBPROC)
		glXGetProcAddressARB((GLubyte *) "glXCreateContextAttribsARB");
	int ctx_attribs[7] = {
		GLX_CONTEXT_MAJOR_VERSION_ARB, 1,
		GLX_CONTEXT_MINOR_VERSION_ARB, 5,
		GLX_CONTEXT_RELEASE_BEHAVIOR_ARB,
		GLX_CONTEXT_RELEASE_BEHAVIOR_NONE_ARB,
		0
	};
	ctx0 = CreateContextAttribs(dpy, config, None, True, ctx_attribs);
	ctx1 = CreateContextAttribs(dpy, config, ctx0, True, ctx_attribs);
	XFree(visinfo);

	glXMakeCurrent(dpy, win, ctx0);

	piglit_dispatch_default_init(PIGLIT_DISPATCH_GL);
	piglit_require_extension("GL_ARB_vertex_buffer_object");
	piglit_require_glx_extension(dpy, "GLX_ARB_get_proc_address");
	piglit_require_glx_extension(dpy, "GLX_ARB_create_context");
	piglit_require_glx_extension(dpy, "GLX_ARB_context_flush_control");

	piglit_require_extension("GL_KHR_context_flush_control");
	context0_init();
	piglit_glx_event_loop(dpy, draw);

	glXDestroyContext(dpy, ctx0);
	glXDestroyContext(dpy, ctx1);

	return 0;
}
