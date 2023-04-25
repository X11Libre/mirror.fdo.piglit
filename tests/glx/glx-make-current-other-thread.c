/*
 * Copyright (c) 2023 Intel Corporation
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

/** @file glx-make-current-other-thread.c
 *
 * According to GLX spec BadAccess is generated if ctx was current
 * to another thread at the time glXMakeCurrent was called.
 */

#include <unistd.h>
#include "piglit-util-gl.h"
#include "piglit-glx-util.h"
#include "pthread.h"

static Display *dpy;
static Window win;
static XVisualInfo *visinfo;

unsigned char glx_error = Success;
int
expect_badvalue(Display *dpy, XErrorEvent *e)
{
   glx_error = e->error_code;
   return 0;
}

static void *
thread_func(void *arg)
{
   GLXContext ctx = arg;
   int ret = glXMakeCurrent(dpy, win, ctx);
   glXDestroyContext(dpy, ctx);
   return (void *)(intptr_t)ret;
}

int
main(int argc, char **argv)
{
   int i;
   int ret;
   void *retval;
   pthread_t thread;
   int (*old_handler)(Display *, XErrorEvent *);

   for (i = 1; i < argc; ++i) {
      if (!strcmp(argv[i], "-auto"))
         piglit_automatic = 1;
      else
         fprintf(stderr, "Unknown option: %s\n", argv[i]);
   }

   dpy = XOpenDisplay(NULL);
   if (dpy == NULL) {
      fprintf(stderr, "couldn't open display\n");
      piglit_report_result(PIGLIT_FAIL);
   }
   visinfo = piglit_get_glx_visual(dpy);
   win = piglit_get_glx_window(dpy, visinfo);

   old_handler = XSetErrorHandler(expect_badvalue);

   GLXContext ctx;
   ctx = piglit_get_glx_context(dpy, visinfo);
   ret = glXMakeCurrent(dpy, win, ctx);

   if (ret != True) {
      printf("First glXMakeCurrent has failed.\n");
      piglit_report_result(PIGLIT_FAIL);
   }

   ret = glXMakeCurrent(dpy, win, ctx);

   if (ret != True) {
      printf("Second glXMakeCurrent in same thread has failed.\n");
      piglit_report_result(PIGLIT_FAIL);
   }

   pthread_create(&thread, NULL, thread_func, ctx);

   ret = pthread_join(thread, &retval);
   if (retval != False) {
      printf("Second glXMakeCurrent in other thread has NOT failed.\n");
      piglit_report_result(PIGLIT_FAIL);
   }

   if (glx_error != BadAccess) {
      printf(
          "Failed to get BadAccess from glXMakeCurrent() in other thread.\n");
      piglit_report_result(PIGLIT_FAIL);
   }

   /* Free our resources when we're done. */
   XSetErrorHandler(old_handler);
   glXMakeCurrent(dpy, None, NULL);

   if (glx_error == BadAccess)
      piglit_report_result(PIGLIT_PASS);
   else
      piglit_report_result(PIGLIT_FAIL);

   return 0;
}
