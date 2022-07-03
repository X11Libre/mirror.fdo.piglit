/*
 * Copyright (c) 2019 Baldur Karlsson <baldurk@baldurk.org>
 * Copyright (c) 2022 Jonathan Strobl <jonathan.strobl@gmx.de>
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

/** @file glx-multithread-buffer-busy-tracking-bug.c
 *
 * Create a buffer shared between two contexts and see if data written in
 * one context is visible to the CPU when using the other context.
 *
 * This tests for a specific class of inter-context buffer busy tracking bug
 * in gallium/u_threaded_context.
 */

#include <unistd.h>

#include "piglit-util-gl.h"
#include "piglit-glx-util.h"

int piglit_width = 50, piglit_height = 50;

static Display *dpy;
static Window draw_win;
static GLXPixmap load_win;
static XVisualInfo *visinfo;


const char *g_computeShaderSource = "\
    #version 430\n\
    layout(local_size_x = 1) in;\
    layout(std430, binding = 1) readonly buffer inSSBO { int inData[]; };\
    layout(std430, binding = 2) writeonly buffer outSSBO { int outData[]; };\
    void main() { outData[gl_GlobalInvocationID.x] = inData[gl_GlobalInvocationID.x]; }\
";



enum piglit_result
draw(Display *dpy)
{
  GLXContext ctx1, ctx2;

  GLuint shader, program;
  GLuint staging_buf, broken_buf, inspect_buf;

  int ret;
  void *ptr;

  const int first_content = 0xDEADBEEF;
  const int second_content = 0x600DC0DE;
  int actual_content;

  ctx1 = piglit_get_glx_context_share(dpy, visinfo, NULL);
  ctx2 = piglit_get_glx_context_share(dpy, visinfo, ctx1);

  ret = glXMakeCurrent(dpy, draw_win, ctx1);
  assert(ret);

  piglit_dispatch_default_init(PIGLIT_DISPATCH_GL);
  piglit_require_gl_version(43);

  glClearColor(0, 0, 1, 1);
  glClear(GL_COLOR_BUFFER_BIT);
  glXSwapBuffers(dpy, draw_win);


  /* Create three single-int buffers: staging_buf, broken_buf, inspect_buf; initialize them */

  glGenBuffers(1, &staging_buf);
  glGenBuffers(1, &broken_buf);
  glGenBuffers(1, &inspect_buf);
  glBindBuffer(GL_UNIFORM_BUFFER, staging_buf);
  glBufferData(GL_UNIFORM_BUFFER, sizeof(int), &first_content, GL_DYNAMIC_DRAW);
  glBindBuffer(GL_UNIFORM_BUFFER, broken_buf);
  glBufferData(GL_UNIFORM_BUFFER, sizeof(int), &first_content, GL_DYNAMIC_DRAW);
  glBindBuffer(GL_UNIFORM_BUFFER, inspect_buf);
  glBufferData(GL_UNIFORM_BUFFER, sizeof(int), &first_content, GL_DYNAMIC_DRAW);
  glBindBuffer(GL_UNIFORM_BUFFER, 0);

  glFinish();

  if (!piglit_check_gl_error(GL_NO_ERROR))
    piglit_report_result(PIGLIT_FAIL);


  /* Context 2: Bind buffers, then copy broken_buf -> inspect_buf using compute.
   * Leave the bindings intact for later so TC can break them.
   * We don't actually care about the result of this copy; all we need is TC to
   * bind some buffers internally.
   */

  ret = glXMakeCurrent(dpy, draw_win, ctx2);
  assert(ret);

  shader = glCreateShader(GL_COMPUTE_SHADER);
  glShaderSource(shader, 1, &g_computeShaderSource, NULL);
  glCompileShader(shader);

  program = glCreateProgram();
  glAttachShader(program, shader);
  glLinkProgram(program);

  if (!piglit_check_gl_error(GL_NO_ERROR))
    piglit_report_result(PIGLIT_FAIL);

  if (!piglit_link_check_status(program))
    piglit_report_result(PIGLIT_FAIL);

  glUseProgram(program);
  glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, broken_buf);
  glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, inspect_buf);
  glDispatchCompute(1, 1, 1);

  /* inspect_buf is bound as the generic GL_SHADER_STORAGE_BUFFER from here
   * on in context 2
   */

  glFinish();

  if (!piglit_check_gl_error(GL_NO_ERROR))
    piglit_report_result(PIGLIT_FAIL);


  /* Context 1: Copy staging_buf -> broken_buf using compute.
   * Then, before flushing, write broken_buf = second_content with glBufferData.
   * That will force TC to invalidate the buffer using storage replacement since
   * it will be busy at the time of the glBufferData call.
   */

  ret = glXMakeCurrent(dpy, draw_win, ctx1);
  assert(ret);

  glUseProgram(program);
  glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, staging_buf);
  glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, broken_buf);
  glDispatchCompute(1, 1, 1);

  glBufferData(GL_SHADER_STORAGE_BUFFER, sizeof(int), &second_content, GL_DYNAMIC_DRAW);

  glFinish();

  if (!piglit_check_gl_error(GL_NO_ERROR))
    piglit_report_result(PIGLIT_FAIL);


  /* Context 2: Copy broken_buf -> inspect_buf again and see what we get
   * as a result in inspect_buf.
   */

  ret = glXMakeCurrent(dpy, draw_win, ctx2);
  assert(ret);

  glDispatchCompute(1, 1, 1);

  ptr = glMapBufferRange(GL_SHADER_STORAGE_BUFFER, 0, sizeof(int), GL_MAP_READ_BIT);
  memcpy(&actual_content, ptr, sizeof(int));
  glUnmapBuffer(GL_SHADER_STORAGE_BUFFER);

  if (actual_content != second_content) {
    printf("Expected 0x%x but got 0x%x\n", second_content, actual_content);
    piglit_report_result(PIGLIT_FAIL);
  }

  glXDestroyContext(dpy, ctx1);
  glXDestroyContext(dpy, ctx2);

  piglit_report_result(PIGLIT_PASS);
}

int
main(int argc, char **argv)
{
  Pixmap pixmap;

  piglit_automatic = 1;

  XInitThreads();
  dpy = XOpenDisplay(NULL);
  if (dpy == NULL) {
    fprintf(stderr, "couldn't open display\n");
    piglit_report_result(PIGLIT_FAIL);
  }
  visinfo = piglit_get_glx_visual(dpy);
  draw_win = piglit_get_glx_window(dpy, visinfo);
  pixmap = XCreatePixmap(dpy, DefaultRootWindow(dpy),
    piglit_width, piglit_height, visinfo->depth);
  load_win = glXCreateGLXPixmap(dpy, visinfo, pixmap);

  piglit_glx_event_loop(dpy, draw);

  return 0;
}
