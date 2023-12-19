/* Copyright 2017 Simon Zeni <simon.zeni@collabora.com>
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

#include <stdbool.h>

#include "piglit-util-egl.h"
#include "common.h"

#define BOOLSTR(x) ((x) ? "yes" : "no")

static void
check_extension(EGLint mask)
{
	if (!EGL_KHR_create_context_setup(mask))
		piglit_report_result(PIGLIT_SKIP);

	piglit_require_egl_extension(egl_dpy, "EGL_EXT_create_context_robustness");
	piglit_require_egl_extension(egl_dpy, "EGL_EXT_query_reset_notification_strategy");

	EGL_KHR_create_context_teardown();
}

static enum piglit_result
check_robustness(EGLenum api, bool robust, bool reset_notif)
{
	enum piglit_result result = PIGLIT_SKIP;
	EGLContext ctx;
	EGLint attribs[11];
	size_t ai = 0;
	EGLint mask = (api == EGL_OPENGL_API) ? EGL_OPENGL_BIT : EGL_OPENGL_ES2_BIT;

	if (!EGL_KHR_create_context_setup(mask))
		goto out;

	if (eglBindAPI(api) != EGL_TRUE)
		goto out;

	/* Always use OpenGL 2.0 or OpenGL ES 2.0 to keep this test reasonably
	 * simple; there are enough variants as-is.
	 */
	attribs[ai++] = EGL_CONTEXT_MAJOR_VERSION_KHR;
	attribs[ai++] = 2;
	attribs[ai++] = EGL_CONTEXT_MINOR_VERSION_KHR;
	attribs[ai++] = 0;

	if (api == EGL_OPENGL_ES_API) {
		attribs[ai++] = EGL_CONTEXT_CLIENT_VERSION;
		attribs[ai++] = 2;
	}

	if (robust) {
		attribs[ai++] = EGL_CONTEXT_OPENGL_ROBUST_ACCESS_EXT;
		attribs[ai++] = EGL_TRUE;
	}

	if (reset_notif) {
		attribs[ai++] = EGL_CONTEXT_OPENGL_RESET_NOTIFICATION_STRATEGY_EXT;
		attribs[ai++] = EGL_LOSE_CONTEXT_ON_RESET_EXT;
	}

	attribs[ai++] = EGL_NONE;

	ctx = eglCreateContext(egl_dpy, cfg, EGL_NO_CONTEXT, attribs);

	if (ctx == EGL_NO_CONTEXT) {
		/*
		 * [EGL_CONTEXT_OPENGL_RESET_NOTIFICATION_STRATEGY_EXT] is only
		 * meaningful for OpenGL ES contexts, and specifying it for
		 * other types of contexts will generate an EGL_BAD_ATTRIBUTE
		 * error.
		 */
		if (api == EGL_OPENGL_API && reset_notif) {
			if (!piglit_check_egl_error(EGL_BAD_ATTRIBUTE)) {
				piglit_loge("expected EGL_BAD_ATTRIBUTE");
				result = PIGLIT_FAIL;
				goto out;
			}

			result = PIGLIT_PASS;
			goto out;
		} else {
			piglit_loge("failed to create EGL context");
			result = PIGLIT_FAIL;
			goto out;
		}
	}

	if (eglMakeCurrent(egl_dpy, EGL_NO_SURFACE, EGL_NO_SURFACE,
			   ctx) != EGL_TRUE) {
		piglit_loge("error: failed to make context current");
		result = PIGLIT_FAIL;
		goto out;
	}

	EGLint strategy;
	EGLBoolean st = eglQueryContext(egl_dpy, ctx,
		EGL_CONTEXT_OPENGL_RESET_NOTIFICATION_STRATEGY_EXT, &strategy);
	if (st == EGL_FALSE || !piglit_check_egl_error(EGL_SUCCESS)) {
		piglit_loge("failed to query EGL context notification strategy");
		result = PIGLIT_FAIL;
		goto out;
	}

	if (reset_notif && strategy != EGL_LOSE_CONTEXT_ON_RESET_EXT) {
		piglit_loge("invalid reset strategy");
		result = PIGLIT_FAIL;
		goto out;
	} else if (!reset_notif && strategy != EGL_NO_RESET_NOTIFICATION_EXT) {
		piglit_loge("unexpected reset strategy");
		result = PIGLIT_FAIL;
		goto out;
	}

	/* Everything turned out to be fine */
	result = PIGLIT_PASS;
out:

	piglit_logi("%s robust=%s reset_notification=%s : %s",
		    (api == EGL_OPENGL_API) ? "OpenGL" : "OpenGL ES",
		    BOOLSTR(robust), BOOLSTR(reset_notif),
		    piglit_result_to_string(result));
	EGL_KHR_create_context_teardown();
	return result;
}

int main(int argc, char **argv)
{
	enum piglit_result result = PIGLIT_SKIP;

	check_extension(EGL_OPENGL_BIT);
	check_extension(EGL_OPENGL_ES2_BIT);

	piglit_merge_result(&result, check_robustness(EGL_OPENGL_API, true, true));
	piglit_merge_result(&result, check_robustness(EGL_OPENGL_API, true, false));
	piglit_merge_result(&result, check_robustness(EGL_OPENGL_API, false, true));
	piglit_merge_result(&result, check_robustness(EGL_OPENGL_API, false, false));
	piglit_merge_result(&result, check_robustness(EGL_OPENGL_ES_API, true, true));
	piglit_merge_result(&result, check_robustness(EGL_OPENGL_ES_API, true, false));
	piglit_merge_result(&result, check_robustness(EGL_OPENGL_ES_API, false, true));
	piglit_merge_result(&result, check_robustness(EGL_OPENGL_ES_API, false, false));

	piglit_report_result(result);
}
