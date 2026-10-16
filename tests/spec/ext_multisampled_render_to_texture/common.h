/*
 * SPDX-License-Identifier: MIT
 */

/**
 * Helpers shared by the EXT_multisampled_render_to_texture and
 * EXT_multisampled_render_to_texture2 tests.
 */

#include "piglit-util-gl.h"

/* A program drawing a solid color at a given NDC depth. */
struct msrtt_program {
	GLuint prog;
	GLint color;
	GLint depth;
};

static inline struct msrtt_program
msrtt_color_program(void)
{
	static const char *vs_source =
		"attribute vec2 piglit_vertex;\n"
		"uniform float depth;\n"
		"void main()\n"
		"{\n"
		"	gl_Position = vec4(piglit_vertex, depth, 1.0);\n"
		"}\n";
	static const char *fs_source =
		"precision mediump float;\n"
		"uniform vec4 color;\n"
		"void main()\n"
		"{\n"
		"	gl_FragColor = color;\n"
		"}\n";
	struct msrtt_program p;

	p.prog = piglit_build_simple_program(vs_source, fs_source);
	p.color = glGetUniformLocation(p.prog, "color");
	p.depth = glGetUniformLocation(p.prog, "depth");
	return p;
}

static inline void
msrtt_use(const struct msrtt_program *p, const float *color, float depth)
{
	glUseProgram(p->prog);
	glUniform4fv(p->color, 1, color);
	glUniform1f(p->depth, depth);
}

static inline GLuint
msrtt_texture(GLenum internalformat, GLenum format, GLenum type,
	      int width, int height)
{
	GLuint tex;

	glGenTextures(1, &tex);
	glBindTexture(GL_TEXTURE_2D, tex);
	glTexImage2D(GL_TEXTURE_2D, 0, internalformat, width, height, 0,
		     format, type, NULL);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	return tex;
}

static inline GLuint
msrtt_rgba_texture(int width, int height)
{
	return msrtt_texture(GL_RGBA, GL_RGBA, GL_UNSIGNED_BYTE, width, height);
}

static inline GLint
msrtt_max_samples(void)
{
	GLint max = 0;

	glGetIntegerv(GL_MAX_SAMPLES_EXT, &max);
	return max;
}

/* The most samples internalformat supports. A format may support fewer
 * than MAX_SAMPLES_EXT, which OpenGL ES 3.0 reports through
 * GetInternalformativ; OpenGL ES 2.0 has no query, so MAX_SAMPLES_EXT is
 * all that is known there.
 */
static inline GLint
msrtt_max_samples_for(GLenum internalformat)
{
	GLint max = msrtt_max_samples(), format_max = 0;

	if (piglit_get_gl_version() < 30)
		return max;
	glGetInternalformativ(GL_RENDERBUFFER, internalformat, GL_SAMPLES, 1,
			      &format_max);
	return MIN2(max, format_max);
}

/* Whether internalformat's GetInternalformativ SAMPLES list has samples.
 * Requesting a listed count allocates exactly that count; others round up.
 */
static inline bool
msrtt_format_has_samples(GLenum internalformat, GLint samples)
{
	GLint num = 0, counts[32];

	glGetInternalformativ(GL_RENDERBUFFER, internalformat,
			      GL_NUM_SAMPLE_COUNTS, 1, &num);
	num = MIN2(num, (GLint)ARRAY_SIZE(counts));
	glGetInternalformativ(GL_RENDERBUFFER, internalformat, GL_SAMPLES, num,
			      counts);
	for (int i = 0; i < num; i++) {
		if (counts[i] == samples)
			return true;
	}
	return false;
}

/* The largest sample count that every format these tests attach supports,
 * for tests that combine attachments or don't care which count they use.
 * OpenGL ES 2.0 has no query, so MAX_SAMPLES_EXT is all that is known there.
 */
static inline GLint
msrtt_samples(void)
{
	static const GLenum formats[] = {
		GL_RGBA8, GL_RGBA4, GL_RGB565, GL_DEPTH_COMPONENT16,
		GL_DEPTH_COMPONENT24, GL_DEPTH_COMPONENT32F,
		GL_DEPTH24_STENCIL8, GL_STENCIL_INDEX8,
	};
	static GLint samples;

	if (samples)
		return samples;
	if (piglit_get_gl_version() < 30)
		return samples = msrtt_max_samples();
	for (GLint s = msrtt_max_samples(); s > 0; s--) {
		unsigned i = 0;

		while (i < ARRAY_SIZE(formats) &&
		       msrtt_format_has_samples(formats[i], s))
			i++;
		if (i == ARRAY_SIZE(formats))
			return samples = s;
	}
	printf("no sample count is supported by every format\n");
	piglit_report_result(PIGLIT_SKIP);
	return 0;
}

/* RenderbufferStorageMultisampleEXT itself: piglit's dispatch would call
 * the core RenderbufferStorageMultisample it aliases in the registry, whose
 * renderbuffers are not implicitly resolved on OpenGL ES.
 */
static inline void
msrtt_renderbuffer_storage(GLenum target, GLsizei samples,
			   GLenum internalformat, GLsizei width, GLsizei height)
{
	static PFNGLRENDERBUFFERSTORAGEMULTISAMPLEEXTPROC storage;

	if (!storage)
		storage = (PFNGLRENDERBUFFERSTORAGEMULTISAMPLEEXTPROC)
			piglit_dispatch_resolve_extension_function(
				"glRenderbufferStorageMultisampleEXT");
	storage(target, samples, internalformat, width, height);
}

/* The attachment's FRAMEBUFFER_ATTACHMENT_TEXTURE_SAMPLES_EXT. */
static inline GLint
msrtt_texture_samples(GLenum target, GLenum attachment)
{
	GLint samples = -1;

	glGetFramebufferAttachmentParameteriv(target, attachment,
					      GL_FRAMEBUFFER_ATTACHMENT_TEXTURE_SAMPLES_EXT,
					      &samples);
	return samples;
}

static inline GLint
msrtt_renderbuffer_samples(void)
{
	GLint samples = -1;

	glGetRenderbufferParameteriv(GL_RENDERBUFFER, GL_RENDERBUFFER_SAMPLES_EXT,
				     &samples);
	return samples;
}

/* A framebuffer with tex level 0 attached single-sampled, for reading. */
static inline GLuint
msrtt_read_framebuffer(GLuint tex)
{
	GLuint fb;

	glGenFramebuffers(1, &fb);
	glBindFramebuffer(GL_FRAMEBUFFER, fb);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
			       GL_TEXTURE_2D, tex, 0);
	return fb;
}

/* The edge is two lines of a w×h target, and the drawn part is below and
 * left of them: a shallow one from (0, A_Y0 * h) to (w, A_Y1 * h) and a steep
 * one from (B_X0 * w, 0) to (B_X1 * w, h). Their slopes and offsets don't
 * line up with the pixel grid, so they cross pixels at varying offsets, and
 * their different orientations split the samples of some edge pixels for any
 * pattern with more than one sample position.
 */
#define MSRTT_EDGE_A_Y0 0.30f
#define MSRTT_EDGE_A_Y1 0.67f
#define MSRTT_EDGE_B_X0 0.71f
#define MSRTT_EDGE_B_X1 0.43f

/* Fills the drawn part. */
static inline void
msrtt_draw_edge(void)
{
	/* where the lines cross, in [0, 1] coordinates */
	const float ka = MSRTT_EDGE_A_Y1 - MSRTT_EDGE_A_Y0;
	const float kb = MSRTT_EDGE_B_X1 - MSRTT_EDGE_B_X0;
	const float y = (MSRTT_EDGE_A_Y0 + ka * MSRTT_EDGE_B_X0) / (1 - ka * kb);
	const float x = MSRTT_EDGE_B_X0 + kb * y;
	const float v[] = {
		-1, -1,  MSRTT_EDGE_B_X0 * 2 - 1, -1,
		x * 2 - 1, y * 2 - 1,  -1, MSRTT_EDGE_A_Y0 * 2 - 1,
	};

	glVertexAttribPointer(PIGLIT_ATTRIB_POS, 2, GL_FLOAT, GL_FALSE, 0, v);
	glEnableVertexAttribArray(PIGLIT_ATTRIB_POS);
	glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
}

/* Distance in pixels from the center of pixel (x, y) to the line through
 * (x0, y0) and (x1, y1), positive on its left going from the first point.
 */
static inline float
msrtt_line_distance(int x, int y, float x0, float y0, float x1, float y1)
{
	float dx = x1 - x0, dy = y1 - y0;

	return (dx * (y + 0.5f - y0) - dy * (x + 0.5f - x0)) /
	       sqrtf(dx * dx + dy * dy);
}

/* Distance in pixels from the center of pixel (x, y) to the drawn part,
 * positive outside it; inside, it is at most the distance to the border.
 */
static inline float
msrtt_edge_distance(int x, int y, int w, int h)
{
	float a = msrtt_line_distance(x, y, 0, MSRTT_EDGE_A_Y0 * h,
				      w, MSRTT_EDGE_A_Y1 * h);
	float b = msrtt_line_distance(x, y, MSRTT_EDGE_B_X1 * w, h,
				      MSRTT_EDGE_B_X0 * w, 0);

	return fmaxf(a, b);
}

/**
 * Checks w×h RGBA8 pixels of msrtt_draw_edge() drawn in fg on bg:
 * pixels more than a pixel away from the edge must be exactly fg or bg, and
 * some pixel near the edge must be blended, which only multisampled
 * rendering produces.
 */
static inline bool
msrtt_check_edge(const GLubyte *pixels, int w, int h, const float *fg,
		 const float *bg, const char *what)
{
	int on_edge = 0, blended = 0, bad = 0;

	for (int y = 0; y < h; y++) {
		for (int x = 0; x < w; x++) {
			const GLubyte *p = &pixels[(y * w + x) * 4];
			float d = msrtt_edge_distance(x, y, w, h);
			const float *expect = d > 0 ? bg : fg;

			if (fabsf(d) > 1.0f) {
				for (int c = 0; c < 4; c++) {
					if (abs(p[c] - (int)(expect[c] * 255 + 0.5f)) > 1) {
						if (bad++ < 4)
							printf("%s: pixel (%d, %d) is "
							       "(%d, %d, %d, %d), expected "
							       "(%.2f, %.2f, %.2f, %.2f)\n",
							       what, x, y, p[0], p[1],
							       p[2], p[3], expect[0],
							       expect[1], expect[2],
							       expect[3]);
						break;
					}
				}
			} else {
				/* the channel with the largest fg/bg difference */
				int c = 0;

				for (int i = 1; i < 4; i++) {
					if (fabsf(fg[i] - bg[i]) > fabsf(fg[c] - bg[c]))
						c = i;
				}
				float lo = fminf(fg[c], bg[c]) * 255, hi = fmaxf(fg[c], bg[c]) * 255;

				on_edge++;
				if (p[c] > lo + 2 && p[c] < hi - 2)
					blended++;
			}
		}
	}

	if (bad)
		printf("%s: %d pixels away from the edge are wrong\n", what, bad);
	if (!blended)
		printf("%s: none of %d edge pixels are blended, so rendering "
		       "was not multisampled\n", what, on_edge);
	return !bad && blended;
}

static inline bool
msrtt_check_edge_framebuffer(int w, int h, const float *fg, const float *bg,
			     const char *what)
{
	GLubyte *pixels = malloc(w * h * 4);
	bool pass;

	glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
	pass = msrtt_check_edge(pixels, w, h, fg, bg, what);
	free(pixels);
	return pass;
}

/* Runs the selected subtests and reports the combined result, using the
 * test's piglit_config pointer to its config.
 */
#define msrtt_run_subtests(subtests)                                         \
	piglit_run_selected_subtests(subtests,                               \
				     piglit_config->selected_subtests,       \
				     piglit_config->num_selected_subtests,   \
				     PIGLIT_SKIP)
