/*
 * Copyright (C) 2025 Valve Corporation.
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use,
 * copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following
 * conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY
 * KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE
 * WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
 * PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL ALLEN AKIN BE
 * LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN
 * AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF
 * OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 */

/*
 * Test that GL uses the original texture matrix after:
 *
 *   glMatrixMode(GL_TEXTURE);
 *   glLoadMatrixf(textureMatrix);
 *   [Draw]
 *   glPushMatrix();
 *   glLoadIdentity();
 *   glPopMatrix();
 *   [Draw]
 *
 * The second draw should be identical to the first.
 */

#include "piglit-util-gl.h"

PIGLIT_GL_TEST_CONFIG_BEGIN
    config.supports_gl_compat_version = 10;
    config.window_visual = PIGLIT_GL_VISUAL_RGBA | PIGLIT_GL_VISUAL_DOUBLE;
    config.window_width = 200;
    config.window_height = 100;
PIGLIT_GL_TEST_CONFIG_END

/* 
 * Scale by 5 and translate by (3,2).
 * This moves all texcoords FAR outside [0,1] so the sampling
 * will produce the GL_CLAMP border color (black).
 */
static const GLfloat textureMatrix[16] = {
    5.0f, 0.0f, 0.0f, 0.0f,
    0.0f, 5.0f, 0.0f, 0.0f,
    0.0f, 0.0f, 1.0f, 0.0f,
    3.0f, 2.0f, 0.0f, 1.0f
};

/* Draw a vertical half-screen quad from x0 to x1 */
static void draw_quad(float x0, float x1)
{
    glBegin(GL_QUADS);
        glTexCoord2f(0.0f, 0.0f); glVertex2f(x0, -1.0f);
        glTexCoord2f(1.0f, 0.0f); glVertex2f(x1, -1.0f);
        glTexCoord2f(1.0f, 1.0f); glVertex2f(x1,  1.0f);
        glTexCoord2f(0.0f, 1.0f); glVertex2f(x0,  1.0f);
    glEnd();
}

enum piglit_result
piglit_display(void)
{
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);

    /* --- Set up projection/modelview matrices --- */
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(-1, 1, -1, 1, -1, 1);

    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();

    /* --- Load the strong texture matrix --- */
    glMatrixMode(GL_TEXTURE);
    glLoadMatrixf(textureMatrix);

    /* --- Draw #1 (left half) --- */
    draw_quad(-1.0f, 0.0f);

    /* --- Push/pop with identity inside --- */
    glPushMatrix();
    glLoadIdentity(); /* Temporarily break the matrix */
    glPopMatrix();    /* Should restore textureMatrix */

    /* --- Draw #2 (right half) --- */
    draw_quad(0.0f, 1.0f);

    piglit_present_results();

    if (!piglit_probe_rect_halves_equal_rgba(0, 0, 200, 100))
       return PIGLIT_FAIL;

    return PIGLIT_PASS;
}


void piglit_init(int argc, char **argv)
{
    /* 2×2 texture with bright distinct colors */
    static const GLubyte texture[4][4] = {
        {255,   0,   0, 255}, /* Red */
        {255, 255,   0, 255}, /* Yellow */
        {  0, 255,   0, 255}, /* Green */
        {  0,   0, 255, 255}  /* Blue */
    };
    GLuint tex;

    glGenTextures(1, &tex);

    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

    /* Clamp so sampling outside produces black. */
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);

    static const GLfloat border[4] = {0, 0, 0, 1};
    glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, border);

    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA,
                 2, 2, 0, GL_RGBA,
                 GL_UNSIGNED_BYTE, texture);

    glEnable(GL_TEXTURE_2D);
}
