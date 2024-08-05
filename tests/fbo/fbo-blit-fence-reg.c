/*`
 * Based on
 * Copyright © 2024 Collabora Ltd
 * SPDX-License-Identifier: MIT
 *
 * Author:
 *    GKraats <vd.kraats@hccnet.nl>
 */

/** @file fbo-blit-fence-reg.c
 *
 * Test fence register shortage at (i915) driver.
 * Test that glBlitFramebufferEXT with GL_NEAREST does
 * not use too many fence registers at the blit,
 * causing loss of current batchbuffer and missing data
 * on the screen or causing an abort at an assert,
 * if compiled with debug.
 */

#include "piglit-util-gl.h"

PIGLIT_GL_TEST_CONFIG_BEGIN

config.supports_gl_compat_version = 10;
config.window_width = 256;
config.window_height = 256;

config.window_visual = PIGLIT_GL_VISUAL_RGB | PIGLIT_GL_VISUAL_DOUBLE;
config.khr_no_error_support = PIGLIT_NO_ERRORS;

PIGLIT_GL_TEST_CONFIG_END

/* size of texture/renderbuffer (power of two) */
#define FBO_SIZE 16

static GLuint target = GL_TEXTURE_2D;

static bool make_fbo(int i, GLuint *fbos, int w, int h) {
  GLuint tex;
  GLuint fb;
  GLenum status;
  bool ret = false;

  glGenFramebuffersEXT(1, &fb);
  glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, fb);

  glGenTextures(1, &tex);
  glBindTexture(target, tex);
  glTexImage2D(target, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);

  glTexParameteri(target, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(target, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

  glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT, GL_COLOR_ATTACHMENT0_EXT,
                            target, tex, 0);
  if (!piglit_check_gl_error(GL_NO_ERROR))
    piglit_report_result(PIGLIT_FAIL);

  status = glCheckFramebufferStatusEXT(GL_FRAMEBUFFER_EXT);
  if (status != GL_FRAMEBUFFER_COMPLETE_EXT) {
    fprintf(stderr, "fbo incomplete (status = 0x%04x)\n", status);
    piglit_report_result(PIGLIT_SKIP);
  } else {
    fbos[i] = fb;
    ret = true;
  }
  return ret;
}

static GLboolean run_test(void) {
  GLboolean pass = GL_TRUE;
  int fbo_width = FBO_SIZE;
  int fbo_height = FBO_SIZE;
  float red[4] = {1.0, 0.0, 0.0, 0.0};
  float grey[4] = {0.5, 0.5, 0.5, 0.5};
  float black[4] = {0.0, 0.0, 0.0, 0.0};
  int i = 0;
  GLuint fbos_red[1];
  GLuint fbos_black[1];
  GLuint fbos_src[15];
  GLuint fbos_dst[15];

  glViewport(0, 0, piglit_width, piglit_height);
  piglit_ortho_projection(piglit_width, piglit_height, GL_FALSE);

  glClearColor(grey[0], grey[1], grey[2], grey[3]);
  glClear(GL_COLOR_BUFFER_BIT);

  glFlush();
  make_fbo(0, fbos_red, fbo_width, fbo_height);
  make_fbo(0, fbos_black, fbo_width, fbo_height);
  for (i = 0; i < 15; i++) {
    make_fbo(i, fbos_src, fbo_width, fbo_height);
  }
  for (i = 0; i < 15; i++) {
    make_fbo(i, fbos_dst, fbo_width, fbo_height);
  }

  glBindFramebufferEXT(GL_DRAW_FRAMEBUFFER_EXT, fbos_red[0]);
  glClearColor(red[0], red[1], red[2], red[3]);
  glClear(GL_COLOR_BUFFER_BIT);

  // make src red
  // GL_LINEAR not using fence register for fbos_src
  glBindFramebufferEXT(GL_READ_FRAMEBUFFER_EXT, fbos_red[0]);
  for (i = 0; i < 15; i++) {
    glBindFramebufferEXT(GL_DRAW_FRAMEBUFFER_EXT, fbos_src[i]);
    glBlitFramebufferEXT(0, 0, fbo_width, fbo_height, 0, 0, fbo_width,
                         fbo_height, GL_COLOR_BUFFER_BIT, GL_LINEAR);
  }
  glBindFramebufferEXT(GL_DRAW_FRAMEBUFFER_EXT, fbos_black[0]);
  glClearColor(black[0], black[1], black[2], black[3]);
  glClear(GL_COLOR_BUFFER_BIT);

  // make dst black
  // GL_LINEAR not using fence register for fbos_dest
  glBindFramebufferEXT(GL_READ_FRAMEBUFFER_EXT, fbos_black[0]);
  for (i = 0; i < 15; i++) {
    glBindFramebufferEXT(GL_DRAW_FRAMEBUFFER_EXT, fbos_dst[i]);
    glBlitFramebufferEXT(0, 0, fbo_width, fbo_height, 0, 0, fbo_width,
                         fbo_height, GL_COLOR_BUFFER_BIT, GL_LINEAR);
  }
  glFlush();

  // copy src to dst using copy_blit (GL_NEAREST)
  // this needs 2 fence registers per copy
  // this causes fence register shortage (ENOBUFS),
  // at 8 copies if bug not fixed at i915 driver
  for (i = 0; i < 15; i++) {
    glBindFramebufferEXT(GL_READ_FRAMEBUFFER_EXT, fbos_src[i]);
    glBindFramebufferEXT(GL_DRAW_FRAMEBUFFER_EXT, fbos_dst[i]);
    glBlitFramebufferEXT(0, 0, fbo_width, fbo_height, 0, 0, fbo_width,
                         fbo_height, GL_COLOR_BUFFER_BIT, GL_NEAREST);
  }
  glFlush();

  glBindFramebufferEXT(GL_DRAW_FRAMEBUFFER_EXT, piglit_winsys_fbo);
  for (i = 0; i < 15; i++) {
    glBindFramebufferEXT(GL_READ_FRAMEBUFFER_EXT, fbos_dst[i]);
    glBlitFramebufferEXT(0, 0, fbo_width, fbo_height, i * fbo_width,
                         i * fbo_height, (i + 1) * fbo_width,
                         (i + 1) * fbo_height, GL_COLOR_BUFFER_BIT, GL_NEAREST);
  }

  glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, piglit_winsys_fbo);
  // if all copies are successful 15 blocks should be red
  for (i = 0; i < 15; i++) {
    pass = piglit_probe_pixel_rgb(i * fbo_width + fbo_width / 2,
                                  i * fbo_height + fbo_height / 2, red) &&
           pass;
  }

  piglit_present_results();

  return pass;
}

enum piglit_result piglit_display(void) {
  GLboolean pass = run_test();

  return pass ? PIGLIT_PASS : PIGLIT_FAIL;
}

void piglit_init(int argc, char **argv) {
  piglit_ortho_projection(piglit_width, piglit_height, GL_FALSE);

  piglit_require_extension("GL_EXT_framebuffer_object");
  piglit_require_extension("GL_EXT_framebuffer_blit");
}
