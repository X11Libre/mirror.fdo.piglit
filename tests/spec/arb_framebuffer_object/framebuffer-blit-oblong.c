/*
 * Copyright © 2026 Collabora Ltd
 *
 * Based on framebuffer-blit-levels.c, which has
 * Copyright © 2012 Intel Corporation
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

/** \file framebuffer-blit-oblong.c
 *
 * This test verifies that glBlitFramebuffer operates correctly when
 * doing oblong blits.
 *
 * The test can be run in two modes: "wide" and "tall". The difference
 * between these modes are that "wide" perform blits of n x 1, and "tall"
 * perform blits of 1 x n pixels, where n is 75% of max_texture_size.
 *
 * This test is meant to validate a work-around for a hardware-issue on some
 * Mali GPUs, where varying precision can drop down below what's needed to
 * correctly blit 1:1.
 */

#include "piglit-util-gl.h"

PIGLIT_GL_TEST_CONFIG_BEGIN

    config.supports_gl_compat_version = 10;

    config.window_visual = PIGLIT_GL_VISUAL_RGB;
    config.khr_no_error_support = PIGLIT_NO_ERRORS;

PIGLIT_GL_TEST_CONFIG_END

GLuint read_framebuffer;
GLuint draw_framebuffer;

GLuint read_texture;
GLuint draw_texture;

GLint width = 1, height = 1;


/**
 * Generate a block of test data in which each pixel has a unique RGBA
 * color.  Different values of the \c level parameter produce
 * different unique sets of pixels.
 *
 * This takes advantage of the Chinese Remainder Theorem to produce a
 * unique color for each pixel--we produce the R, G, B, and A values
 * by taking an integer mod four different primes.
 */
static void
create_test_data(GLfloat *data, unsigned width, unsigned height)
{
    unsigned pixel;
    unsigned num_pixels = width * height;
    for (pixel = 0; pixel < num_pixels; ++pixel) {
       unsigned unique_value = pixel;
       data[4 * pixel + 0] = (unique_value % 233) / 233.0;
       data[4 * pixel + 1] = (unique_value % 239) / 239.0;
       data[4 * pixel + 2] = (unique_value % 241) / 241.0;
       data[4 * pixel + 3] = (unique_value % 251) / 251.0;
    }
}

static void
print_usage_and_exit(char *prog_name)
{
    printf("Usage: %s <test_mode>\n"
           "  where <test_mode> is one of:\n"
           "    wide: test performing wide blits\n"
           "    tall: test performing tall blits\n",
           prog_name);
    piglit_report_result(PIGLIT_FAIL);
}

void
piglit_init(int argc, char **argv)
{
    if (argc != 2) {
       print_usage_and_exit(argv[0]);
    }

    GLint maxsize = 0;
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maxsize);

    /*
     * The issue does not happen at power of two sizes; move the size to
     * the middle of max-size and the half of max-size
     */
    GLint size = (maxsize * 3) / 4;

    if (strcmp(argv[1], "wide") == 0)
        width = size;
    else if (strcmp(argv[1], "tall") == 0)
        height = size;
    else
       print_usage_and_exit(argv[0]);

    piglit_require_extension("GL_ARB_framebuffer_object");

    /* Set up read framebuffer, populated with data. */
    GLfloat *data = malloc(width * height * 4 * sizeof(GLfloat));
    create_test_data(data, width, height);

    glGenFramebuffers(1, &read_framebuffer);
    glGenTextures(1, &read_texture);
    glBindTexture(GL_TEXTURE_2D, read_texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0,
                 GL_RGBA, GL_FLOAT, data);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, read_framebuffer);
    glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                           GL_TEXTURE_2D, read_texture, 0);

    /* Set up draw framebuffer */
    glGenFramebuffers(1, &draw_framebuffer);
    glGenTextures(1, &draw_texture);
    glBindTexture(GL_TEXTURE_2D, draw_texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0,
                 GL_RGBA, GL_FLOAT, NULL);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, draw_framebuffer);
    glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                           GL_TEXTURE_2D, draw_texture, 0);
}

enum piglit_result
piglit_display()
{
    printf("Testing size %d x %d\n", width, height);

    GLfloat *data = malloc(width * height * 4 * sizeof(GLfloat));
    create_test_data(data, width, height);

    glBindFramebuffer(GL_READ_FRAMEBUFFER, read_framebuffer);
    glClear(GL_COLOR_BUFFER_BIT);

    glBlitFramebuffer(0, 0, width, height,
                      0, 0, width, height,
                      GL_COLOR_BUFFER_BIT,
                      GL_NEAREST);

    /* Verify aux texture */
    glBindFramebuffer(GL_READ_FRAMEBUFFER, draw_framebuffer);
    bool pass = piglit_probe_image_color(0, 0, width, height, GL_RGBA, data);

    free(data);

    return pass ? PIGLIT_PASS : PIGLIT_FAIL;
}
