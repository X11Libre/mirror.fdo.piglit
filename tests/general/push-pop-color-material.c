/*
 * Copyright © 2025 Valve Corporation.
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

/**
 * Tests the correct color is used when lighting is enabled and in combination
 * with pop attributes.
 *
 * https://gitlab.freedesktop.org/mesa/mesa/-/issues/7122
 */

#include "piglit-util-gl.h"

PIGLIT_GL_TEST_CONFIG_BEGIN
	config.supports_gl_compat_version = 10;
	config.window_width = 512;
	config.window_height = 512;
	config.window_visual = PIGLIT_GL_VISUAL_RGB | PIGLIT_GL_VISUAL_DOUBLE;
PIGLIT_GL_TEST_CONFIG_END

struct Vertex
{
    GLfloat v[3];
};

static struct Vertex g_vertices[] = {
    {{ -0.5, -0.5, 0.0 }},
    {{ -0.5, 0.5, 0.0 }},
    {{ 0.0, 0.0, 0.0 }},
    {{ -0.5, 0.5, 0.0 }},
    {{ 0.5, 0.5, 0.0 }},
    {{ 0.0, 0.0, 0.0 }},
    {{ 0.5, 0.5, 0.0 }},
    {{ 0.5, -0.5, 0.0 }},
    {{ 0.1, -0.1, 0.0 }},
    {{ 0.5, -0.5, 0.0 }},
    {{ -0.5, -0.5, 0.0 }},
    {{ 0.1, -0.1, 0.0 }},
};
static struct Vertex g_normals[] = {
    {{ 0.0, 0.0, 1.0 }},
    {{ 0.0, 0.0, 1.0 }},
    {{ 0.0, 0.0, 1.0 }},
    {{ 0.0, 0.0, 1.0 }},
    {{ 0.0, 0.0, 1.0 }},
    {{ 0.0, 0.0, 1.0 }},
    {{ 0.0, 0.0, 1.0 }},
    {{ 0.0, 0.0, 1.0 }},
    {{ 0.0, 0.0, 1.0 }},
    {{ 0.0, 0.0, 1.0 }},
    {{ 0.0, 0.0, 1.0 }},
    {{ 0.0, 0.0, 1.0 }},
};
static GLuint g_indices1[] = {
    0, 1, 2,
};
static GLuint g_indices2[] = {
    3, 4, 5,
};
static GLuint g_indices3[] = {
    6, 7, 8, 9, 10, 11,
};

void setupLighting()
{
   glEnable(GL_LIGHTING);
   glColorMaterial(GL_FRONT_AND_BACK, GL_AMBIENT_AND_DIFFUSE);
   glEnable(GL_COLOR_MATERIAL);
   glEnable(GL_LIGHT0);
   glLightf(GL_LIGHT0, GL_CONSTANT_ATTENUATION, 1.0f);
   GLfloat specular[] = { 0, 0, 0, 0 };
   GLfloat diffuse[] = { 1, 1, 1, 1 };
   glLightfv(GL_LIGHT0, GL_SPECULAR, specular);
   glLightfv(GL_LIGHT0, GL_DIFFUSE, diffuse);
}

void loadGeometry()
{
   glVertexPointer(3, GL_FLOAT, sizeof(struct Vertex), g_vertices);
   glNormalPointer(GL_FLOAT, sizeof(struct Vertex), g_normals);
   glEnableClientState(GL_VERTEX_ARRAY);
   glEnableClientState(GL_NORMAL_ARRAY);
}

enum piglit_result
piglit_display(void)
{
   static const GLfloat red_float[3] = {1.0f, 0.0f, 0.0f};
   static const GLfloat green_float[3] = {0.0f, 1.0f, 0.0f};
   static const GLfloat blue_float[3] = {0.0f, 0.0f, 1.0f};
   static const GLuint red = 0xff0000ff;
   static const GLuint green = 0xff00ff00;
   static const GLuint blue = 0xffff0000;

   /* Set color to red and then use glPushAttrib to store that on the stack. */
   glColor4ubv((GLubyte*)&red);
   glPushAttrib(GL_CURRENT_BIT);

   /* Change the color to green and draw a green triangle */
   glColor4ubv((GLubyte*)&green);
   glDrawElements(GL_TRIANGLES, 3, GL_UNSIGNED_INT, g_indices1);

   /* Pop green off the attrib stack (back to red) */
   glPopAttrib();
   /* Push (red) back onto the attrib stack */
   glPushAttrib(GL_CURRENT_BIT);

   /* Change the color to blue and draw two blue triangles */
   glColor4ubv((GLubyte*)&blue);
   glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, g_indices3);

   /* Pop blue off the attrib stack (back to red) */
   glPopAttrib();
   /* Draw our last triangle. This triangle should be red, but in issue number
    * 7122 on Mesa's gitlab it was incorrectly colored blue.
    */
   glDrawElements(GL_TRIANGLES, 3, GL_UNSIGNED_INT, g_indices2);

   GLboolean pass = GL_TRUE;
   int x0 = piglit_width * 3 / 8;
   int x1 = piglit_width * 5 / 8;
   int y0 = piglit_height * 1 / 2;
   int y1 = piglit_height * 5 / 8;

   /* Probe each triagle and make sure they are the correct colors */
   pass = piglit_probe_pixel_rgb(x0, y0, green_float) && pass;
   pass = piglit_probe_pixel_rgb(x1, y0, blue_float) && pass;
   pass = piglit_probe_pixel_rgb(x0, y1, red_float) && pass;

   piglit_present_results();

   return pass ? PIGLIT_PASS : PIGLIT_FAIL;
}

void
piglit_init(int argc, char **argv)
{
   glViewport(0.0, 0.0, 512.0, 512.0);
   glClearColor(0.5, 0.5, 0.5, 1.0);
   glClear(GL_COLOR_BUFFER_BIT);
   glMatrixMode(GL_PROJECTION);
   glLoadIdentity();
   glOrtho(-1.0, 1.0, -1.0, 1.0, -1.0, 1.0);
   setupLighting();
   loadGeometry();
}
