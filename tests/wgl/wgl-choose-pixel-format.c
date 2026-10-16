/* SPDX-License-Identifier: MIT */

/**
 * ChoosePixelFormat on a window DC with PFD_DRAW_TO_WINDOW must return a
 * format that can be set on the window and used for rendering, whatever
 * bit depths are requested. Applications like apitrace's glretrace ask for
 * minimal depths, and an implementation that also matches its pbuffer-only
 * formats (16-bit, float) hands back one that SetPixelFormat rejects.
 */

#include <stdio.h>
#include "piglit-util-gl.h"
#include "piglit-wgl-util.h"

struct request {
	const char *name;
	BYTE color, alpha, depth, stencil;
};

static const struct request requests[] = {
	{ "minimal (glretrace)", 3, 1, 1, 1 },
	{ "minimal, no alpha", 3, 0, 1, 1 },
	{ "16-bit color", 16, 0, 16, 0 },
	{ "16-bit color with alpha", 16, 1, 16, 0 },
	{ "16-bit color with 4-bit alpha", 16, 4, 0, 0 },
	{ "24-bit color", 24, 0, 24, 8 },
	{ "32-bit color with alpha", 32, 8, 24, 8 },
	{ "128-bit color", 128, 32, 24, 8 },
};

static LRESULT CALLBACK
wnd_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
	return DefWindowProc(hwnd, msg, wparam, lparam);
}

/* a pixel format can only be set once on a window, so each request gets one */
static HWND
create_window(void)
{
	static const char *name = "wgl-choose-pixel-format";
	static bool registered;
	HINSTANCE inst = GetModuleHandle(NULL);

	if (!registered) {
		WNDCLASS wc = {
			.style = CS_OWNDC,
			.lpfnWndProc = wnd_proc,
			.hInstance = inst,
			.hCursor = LoadCursor(NULL, IDC_ARROW),
			.lpszClassName = name,
		};
		RegisterClass(&wc);
		registered = true;
	}

	return CreateWindowEx(0, name, name,
			      WS_OVERLAPPEDWINDOW | WS_CLIPSIBLINGS | WS_CLIPCHILDREN,
			      0, 0, piglit_width, piglit_height,
			      NULL, NULL, inst, NULL);
}

static bool
test_format(HDC dc, const struct request *req)
{
	const PIXELFORMATDESCRIPTOR pfd = {
		.nSize = sizeof(pfd),
		.nVersion = 1,
		.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL |
			   PFD_DOUBLEBUFFER,
		.iPixelType = PFD_TYPE_RGBA,
		.cColorBits = req->color,
		.cAlphaBits = req->alpha,
		.cDepthBits = req->depth,
		.cStencilBits = req->stencil,
		.iLayerType = PFD_MAIN_PLANE,
	};
	const DWORD needed = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL;
	PIXELFORMATDESCRIPTOR got;

	int format = ChoosePixelFormat(dc, &pfd);
	if (!format) {
		printf("%s: ChoosePixelFormat found nothing\n", req->name);
		return false;
	}

	if (!DescribePixelFormat(dc, format, sizeof(got), &got)) {
		printf("%s: DescribePixelFormat(%d) failed\n", req->name,
		       format);
		return false;
	}
	if ((got.dwFlags & needed) != needed) {
		printf("%s: chose format %d with flags 0x%lx, which can't "
		       "draw to a window\n", req->name, format,
		       (unsigned long)got.dwFlags);
		return false;
	}

	if (!SetPixelFormat(dc, format, &got)) {
		printf("%s: SetPixelFormat(%d) failed\n", req->name, format);
		return false;
	}

	HGLRC ctx = wglCreateContext(dc);
	if (!ctx) {
		printf("%s: wglCreateContext on format %d failed\n",
		       req->name, format);
		return false;
	}
	bool pass = wglMakeCurrent(dc, ctx);
	if (!pass)
		printf("%s: wglMakeCurrent on format %d failed\n",
		       req->name, format);
	wglMakeCurrent(NULL, NULL);
	wglDeleteContext(ctx);
	return pass;
}

static bool
test_request(const struct request *req)
{
	HWND hwnd = create_window();
	if (!hwnd) {
		printf("%s: failed to create a window\n", req->name);
		return false;
	}

	HDC dc = GetDC(hwnd);
	bool pass = test_format(dc, req);
	ReleaseDC(hwnd, dc);
	DestroyWindow(hwnd);
	return pass;
}

int
main(int argc, char **argv)
{
	bool pass = true;

	for (unsigned i = 0; i < ARRAY_SIZE(requests); i++)
		pass &= test_request(&requests[i]);

	piglit_report_result(pass ? PIGLIT_PASS : PIGLIT_FAIL);
	return 0;
}
