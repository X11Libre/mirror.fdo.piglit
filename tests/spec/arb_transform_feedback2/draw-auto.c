/*
 * Copyright © 2011 Marek Olšák <maraeo@gmail.com>
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
 * Basic ARB_transform_feedback2 test.
 *
 * Test Pause, Resume, and DrawTF.
 *
 * With the "offset" argument the transform feedback buffer is bound with
 * glBindBufferRange() at a non-zero offset instead of glBindBufferBase().
 * The vertex count DrawTF derives from the transform feedback counter is
 * relative to the start of the bound range, so the bytes the offset skips
 * must not be counted as vertices.  A driver that counts them draws
 * OFFSET_VERTICES vertices too many, taken from whatever follows the
 * recorded data, so the buffer is pre-filled with a trap triangle exactly
 * there and the area it would cover is probed for the clear colour.
 */

#include "piglit-util-gl.h"

PIGLIT_GL_TEST_CONFIG_BEGIN

	config.supports_gl_compat_version = 10;

	config.window_width = 64;
	config.window_height = 128;
	config.window_visual = PIGLIT_GL_VISUAL_DOUBLE | PIGLIT_GL_VISUAL_RGBA;
	config.khr_no_error_support = PIGLIT_NO_ERRORS;

PIGLIT_GL_TEST_CONFIG_END

static const char *vstext = {
	"void main() {"
	"  gl_Position = gl_ModelViewProjectionMatrix * gl_Vertex;"
	"  gl_FrontColor = gl_Color;"
	"}"
};
static const char *vstext_notransform = {
	"void main() {"
	"  gl_Position = gl_Vertex + vec4(0.0, 0.32, 0.0, 0.0);"
	"  gl_FrontColor = gl_Color;"
	"}"
};
static const char *vstext_notransform_instanced = {
	"#extension GL_ARB_draw_instanced : enable\n"
	"void main() {"
	"  gl_Position = gl_Vertex + vec4(0.0, 0.32 * float(gl_InstanceID+1), 0.0, 0.0);"
	"  gl_FrontColor = gl_Color;"
	"}"
};

static const char *varyings[] = {"gl_FrontColor", "gl_Position"};
GLuint buf;
GLuint prog, prog_notransform, prog_notransform_instanced;
GLuint tfb;
GLboolean instanced;
GLboolean use_offset;

/* One vertex is gl_FrontColor and gl_Position. */
#define VERTEX_SIZE	(8 * sizeof(float))
/* Two quads are recorded, as triangles. */
#define NUM_RECORDED	12
/* A multiple of three, so that the extra vertices an unfixed driver draws
 * add up to whole triangles and cannot be dropped as an incomplete
 * primitive.
 */
#define OFFSET_VERTICES	3
#define NUM_VERTICES	(OFFSET_VERTICES + NUM_RECORDED + OFFSET_VERTICES)

/* Sits where the first vertex past the recorded ones is read from, and
 * covers the top half of the window, which nothing else draws to.
 */
static const float trap[] = {
	1, 0, 1, 1,   -0.8, -0.05, 0, 1,
	1, 0, 1, 1,    0.8, -0.05, 0, 1,
	1, 0, 1, 1,    0.8,  0.50, 0, 1,
};

void piglit_init(int argc, char **argv)
{
	GLuint vs;
	GLint maxcomps;
	float data[NUM_VERTICES * 8];

	if (argc == 2 && strcmp(argv[1], "instanced") == 0)
		instanced = GL_TRUE;
	if (argc == 2 && strcmp(argv[1], "offset") == 0)
		use_offset = GL_TRUE;

	piglit_ortho_projection(piglit_width, piglit_height, GL_FALSE);

	/* Check the driver. */
	piglit_require_gl_version(15);
	piglit_require_vertex_shader();
	piglit_require_extension("GL_EXT_transform_feedback");
	piglit_require_extension("GL_ARB_transform_feedback2");
	if (instanced)
		piglit_require_extension("GL_ARB_transform_feedback_instanced");

	glGetIntegerv(GL_MAX_TRANSFORM_FEEDBACK_INTERLEAVED_COMPONENTS_EXT, &maxcomps);
	if (maxcomps < 8) {
		fprintf(stderr, "Not enough interleaved components supported by transform feedback.\n");
		piglit_report_result(PIGLIT_SKIP);
	}

	/* Create shaders. */
	vs = piglit_compile_shader_text(GL_VERTEX_SHADER, vstext);
	prog = glCreateProgram();
	glAttachShader(prog, vs);
	glTransformFeedbackVaryingsEXT(prog, sizeof(varyings)/sizeof(varyings[0]),
				       varyings, GL_INTERLEAVED_ATTRIBS_EXT);
	glLinkProgram(prog);
	if (!piglit_link_check_status(prog)) {
		glDeleteProgram(prog);
		piglit_report_result(PIGLIT_FAIL);
	}

	vs = piglit_compile_shader_text(GL_VERTEX_SHADER, vstext_notransform);
	prog_notransform = piglit_link_simple_program(vs, 0);
	if (instanced) {
		vs = piglit_compile_shader_text(GL_VERTEX_SHADER, vstext_notransform_instanced);
		prog_notransform_instanced = piglit_link_simple_program(vs, 0);
	}

	/* Set up transform feedback. */
	memset(data, 0, sizeof(data));
	if (use_offset) {
		memcpy((char *)data + OFFSET_VERTICES * VERTEX_SIZE +
		       NUM_RECORDED * VERTEX_SIZE, trap, sizeof(trap));
	}

	glGenBuffers(1, &buf);
	glBindBuffer(GL_TRANSFORM_FEEDBACK_BUFFER_EXT, buf);
	glBufferData(GL_TRANSFORM_FEEDBACK_BUFFER_EXT,
		     sizeof(data), data, GL_STREAM_READ);

	glGenTransformFeedbacks(1, &tfb);
	glBindTransformFeedback(GL_TRANSFORM_FEEDBACK, tfb);
	if (use_offset) {
		/* Exactly the recorded vertices, so that transform feedback
		 * itself cannot overwrite the trap triangle.
		 */
		glBindBufferRange(GL_TRANSFORM_FEEDBACK_BUFFER_EXT, 0, buf,
				  OFFSET_VERTICES * VERTEX_SIZE,
				  NUM_RECORDED * VERTEX_SIZE);
	} else {
		glBindBufferBaseEXT(GL_TRANSFORM_FEEDBACK_BUFFER_EXT, 0, buf);
	}
	glBindTransformFeedback(GL_TRANSFORM_FEEDBACK, 0);

	if (!piglit_check_gl_error(GL_NO_ERROR))
		piglit_report_result(PIGLIT_FAIL);

	glClearColor(0.2, 0.2, 0.2, 1.0);
	glEnableClientState(GL_VERTEX_ARRAY);
}

enum piglit_result piglit_display(void)
{
	GLboolean pass = GL_TRUE;
	static const float verts[] = {
		10, 10,
		10, 20,
		20, 20,
		20, 10
	};
	static const float red[] = {1, 0, 0};
	static const float green[] = {0, 1, 0};
	static const float blue[] = {0, 0, 1};
	static const float clearcolor[] = {0.2, 0.2, 0.2};
	const intptr_t base_offset =
		use_offset ? OFFSET_VERTICES * VERTEX_SIZE : 0;

	glClear(GL_COLOR_BUFFER_BIT);

	/* Render into the TFBO. */
	glUseProgram(prog);
	glLoadIdentity();

	glVertexPointer(2, GL_FLOAT, 0, verts);

	glBindTransformFeedback(GL_TRANSFORM_FEEDBACK, tfb);
	glBeginTransformFeedbackEXT(GL_TRIANGLES);
	glColor3f(1, 0, 0);
	glDrawArrays(GL_QUADS, 0, 4);
	glPauseTransformFeedback();
	glBindTransformFeedback(GL_TRANSFORM_FEEDBACK, 0);

	glTranslatef(20, 0, 0);
	glColor3f(0, 1, 0);
	glDrawArrays(GL_QUADS, 0, 4);

	glTranslatef(20, 0, 0);
	glBindTransformFeedback(GL_TRANSFORM_FEEDBACK, tfb);
	glResumeTransformFeedback();
	glColor3f(0, 0, 1);
	glDrawArrays(GL_QUADS, 0, 4);
	glEndTransformFeedbackEXT();
	glBindTransformFeedback(GL_TRANSFORM_FEEDBACK, 0);

	if (!piglit_check_gl_error(GL_NO_ERROR))
		piglit_report_result(PIGLIT_FAIL);

	glUseProgram(instanced ? prog_notransform_instanced : prog_notransform);
	glBindBuffer(GL_ARRAY_BUFFER, buf);
	glEnableClientState(GL_COLOR_ARRAY);
	glColorPointer(4, GL_FLOAT, sizeof(float)*8,
		       (void*)(intptr_t)(base_offset));
	glVertexPointer(4, GL_FLOAT, sizeof(float)*8,
			(void*)(intptr_t)(base_offset + 4*sizeof(float)));
	if (instanced) {
		glDrawTransformFeedbackInstanced(GL_TRIANGLES, tfb, 4);
	} else {
		glDrawTransformFeedback(GL_TRIANGLES, tfb);
	}
	glDisableClientState(GL_COLOR_ARRAY);
	glBindBuffer(GL_ARRAY_BUFFER, 0);

	if (!piglit_check_gl_error(GL_NO_ERROR))
		piglit_report_result(PIGLIT_FAIL);

	pass = piglit_probe_pixel_rgb(15, 15, red) && pass;
	pass = piglit_probe_pixel_rgb(35, 15, green) && pass;
	pass = piglit_probe_pixel_rgb(55, 15, blue) && pass;

	pass = piglit_probe_pixel_rgb(15, 35, red) && pass;
	pass = piglit_probe_pixel_rgb(35, 35, clearcolor) && pass;
	pass = piglit_probe_pixel_rgb(55, 35, blue) && pass;

	/* Inside the trap triangle, which only an over-long draw reaches. */
	if (use_offset)
		pass = piglit_probe_pixel_rgb(44, 90, clearcolor) && pass;

	if (instanced) {
		unsigned i;
		for (i = 1; i < 4; i++) {
			pass = piglit_probe_pixel_rgb(15, 35 + 20*i, red) && pass;
			pass = piglit_probe_pixel_rgb(35, 35 + 20*i, clearcolor) && pass;
			pass = piglit_probe_pixel_rgb(55, 35 + 20*i, blue) && pass;
		}
	}

	piglit_present_results();

	return pass ? PIGLIT_PASS : PIGLIT_FAIL;
}
