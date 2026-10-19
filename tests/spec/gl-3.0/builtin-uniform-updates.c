/* SPDX-License-Identifier: MIT */

/**
 * Built-in uniforms of the compatibility profile must follow every way the
 * state behind them can change: direct calls, matrix stack pops, attribute
 * pops, display lists, color material, clears between the change and the
 * draw, and changes that switch the lighting space.
 *
 * A program is drawn many times across these changes, and after each change
 * the built-ins it reads, captured with transform feedback, must equal those
 * of a newly linked copy of the same program drawn in the same state.
 */

#include "piglit-util-gl.h"
#include "../gl-1.0/state-update-common.h"

static enum piglit_result run_scenario(void *data);

static void setup_push_matrix(void);
static void setup_push_transform(void);
static void setup_list_color(void);
static void setup_list_state(void);

static void change_modelview(void);
static void change_projection(void);
static void change_several(void);
static void change_pop_matrix(void);
static void change_texture_matrix(void);
static void change_clear_only(void);
static void change_light_position(void);
static void change_spot(void);
static void change_material(void);
static void change_call_color_list(void);
static void change_call_state_list(void);
static void change_clip_plane(void);
static void change_light_model(void);
static void change_enable_light1(void);
static void change_other_program(void);
static void change_relink(void);

static const struct piglit_subtest subtests[] = {
	SCENARIO("modelview", "modelview", setup_nothing, change_modelview),
	SCENARIO("projection", "projection", setup_nothing, change_projection),
	SCENARIO("several changes before a draw", "several", setup_nothing,
		 change_several),
	SCENARIO("pop matrix", "pop-matrix", setup_push_matrix,
		 change_pop_matrix),
	SCENARIO("texture matrix of another unit", "texture-matrix",
		 setup_nothing, change_texture_matrix),
	SCENARIO("change then clear", "change-clear", setup_nothing,
		 change_then_clear),
	SCENARIO("clear only", "clear", setup_nothing, change_clear_only),
	SCENARIO("light position", "light-position", setup_nothing,
		 change_light_position),
	SCENARIO("spot light", "spot", setup_nothing, change_spot),
	SCENARIO("material", "material", setup_nothing, change_material),
	SCENARIO("color material", "color-material", setup_color_material,
		 change_color),
	SCENARIO("color material in a display list", "color-material-list",
		 setup_list_color, change_call_color_list),
	SCENARIO("display list", "list", setup_list_state,
		 change_call_state_list),
	SCENARIO("pop lighting attributes", "pop-lighting",
		 setup_push_lighting, change_pop_attrib),
	SCENARIO("pop transform attributes", "pop-transform",
		 setup_push_transform, change_pop_attrib),
	SCENARIO("clip plane", "clip-plane", setup_nothing, change_clip_plane),
	SCENARIO("light model", "light-model", setup_nothing,
		 change_light_model),
	SCENARIO("local viewer", "local-viewer", setup_nothing,
		 change_local_viewer),
	SCENARIO("eye-linear texgen", "texgen", setup_nothing,
		 change_texgen_eye),
	SCENARIO("point attenuation", "point-attenuation", setup_nothing,
		 change_point_attenuation),
	SCENARIO("rescale normal", "rescale-normal", setup_rescale, change_scale),
	SCENARIO("non-uniform scale", "nonuniform-scale", setup_nothing,
		 change_nonuniform_scale),
	SCENARIO("enable another light", "enable-light", setup_nothing,
		 change_enable_light1),
	SCENARIO("another program in between", "other-program", setup_nothing,
		 change_other_program),
	SCENARIO("relink", "relink", setup_nothing, change_relink),
	{ NULL }
};

static const struct piglit_gl_test_config *piglit_config;

PIGLIT_GL_TEST_CONFIG_BEGIN

	config.supports_gl_compat_version = 30;
	config.window_visual = PIGLIT_GL_VISUAL_RGBA | PIGLIT_GL_VISUAL_DOUBLE;
	config.khr_no_error_support = PIGLIT_NO_ERRORS;
	config.subtests = subtests;
	piglit_config = &config;

PIGLIT_GL_TEST_CONFIG_END

/* Each group stays within the 60 varying components OpenGL 3.0 guarantees. */
#define MAX_OUTPUTS 12

struct group {
	const char *outputs[MAX_OUTPUTS];
};

static const struct group groups[] = {
	{ {
		"gl_ModelViewMatrix[0]", "gl_ModelViewMatrix[1]",
		"gl_ModelViewMatrix[2]", "gl_ModelViewMatrix[3]",
		"gl_ProjectionMatrix[0]", "gl_ProjectionMatrix[1]",
		"gl_ProjectionMatrix[2]", "gl_ProjectionMatrix[3]",
		"gl_ModelViewProjectionMatrix[0]",
		"gl_ModelViewProjectionMatrix[1]",
		"gl_ModelViewProjectionMatrix[2]",
		"gl_ModelViewProjectionMatrix[3]",
	} },
	{ {
		"gl_ModelViewMatrixInverse[0]", "gl_ModelViewMatrixInverse[3]",
		"gl_ModelViewMatrixInverseTranspose[0]",
		"gl_ModelViewMatrixInverseTranspose[3]",
		"gl_ProjectionMatrixInverse[0]", "gl_ProjectionMatrixInverse[3]",
		"gl_ModelViewProjectionMatrixInverse[2]",
		"gl_ModelViewProjectionMatrixTranspose[1]",
		"vec4(gl_NormalMatrix[0], gl_NormalScale)",
		"vec4(gl_NormalMatrix[1], 0.0)",
		"vec4(gl_NormalMatrix[2], 0.0)",
	} },
	{ {
		"gl_TextureMatrix[0][0]", "gl_TextureMatrix[0][3]",
		"gl_TextureMatrix[1][0]", "gl_TextureMatrix[1][1]",
		"gl_TextureMatrix[1][2]", "gl_TextureMatrix[1][3]",
		"gl_TextureMatrixInverse[1][3]", "gl_TextureMatrixTranspose[1][0]",
		"gl_ClipPlane[0]", "gl_ClipPlane[1]",
		"gl_EyePlaneS[0]",
		"vec4(gl_Point.size, gl_Point.distanceConstantAttenuation, "
		"gl_Point.distanceLinearAttenuation, "
		"gl_Point.distanceQuadraticAttenuation)",
	} },
	{ {
		"gl_LightSource[0].position", "gl_LightSource[0].halfVector",
		"vec4(gl_LightSource[0].spotDirection, gl_LightSource[0].spotExponent)",
		"vec4(gl_LightSource[0].spotCutoff, gl_LightSource[0].spotCosCutoff, "
		"gl_LightSource[0].constantAttenuation, "
		"gl_LightSource[0].quadraticAttenuation)",
		"gl_LightSource[0].diffuse",
		"gl_LightSource[1].position", "gl_LightSource[1].halfVector",
		"gl_LightModel.ambient",
		"gl_FrontLightModelProduct.sceneColor",
		"gl_BackLightModelProduct.sceneColor",
	} },
	{ {
		"gl_FrontMaterial.ambient", "gl_FrontMaterial.diffuse",
		"gl_FrontMaterial.specular", "gl_FrontMaterial.emission",
		"vec4(gl_FrontMaterial.shininess, gl_BackMaterial.shininess, 0.0, 0.0)",
		"gl_BackMaterial.diffuse",
		"gl_FrontLightProduct[0].ambient", "gl_FrontLightProduct[0].diffuse",
		"gl_FrontLightProduct[0].specular", "gl_FrontLightProduct[1].diffuse",
		"gl_BackLightProduct[1].diffuse",
	} },
};

#define NUM_GROUPS ARRAY_SIZE(groups)

static unsigned
num_outputs(const struct group *g)
{
	unsigned n = 0;

	while (n < MAX_OUTPUTS && g->outputs[n])
		n++;
	return n;
}

static GLuint warm[NUM_GROUPS], tf_buf, color_list, state_list;

static GLuint
build_program(const struct group *g)
{
	char vs[4096], names[MAX_OUTPUTS][8];
	const char *varyings[MAX_OUTPUTS];
	int len;
	GLuint prog, shader;

	len = snprintf(vs, sizeof(vs), "#version 120\n");
	for (unsigned i = 0; i < num_outputs(g); i++)
		len += snprintf(vs + len, sizeof(vs) - len, "varying vec4 v%u;\n", i);
	len += snprintf(vs + len, sizeof(vs) - len,
			"void main()\n{\n\tgl_Position = vec4(0.0);\n");
	for (unsigned i = 0; i < num_outputs(g); i++)
		len += snprintf(vs + len, sizeof(vs) - len, "\tv%u = %s;\n", i,
				g->outputs[i]);
	snprintf(vs + len, sizeof(vs) - len, "}\n");

	for (unsigned i = 0; i < num_outputs(g); i++) {
		snprintf(names[i], sizeof(names[i]), "v%u", i);
		varyings[i] = names[i];
	}

	shader = piglit_compile_shader_text(GL_VERTEX_SHADER, vs);
	prog = glCreateProgram();
	glAttachShader(prog, shader);
	glDeleteShader(shader);
	glTransformFeedbackVaryings(prog, num_outputs(g), varyings,
				    GL_INTERLEAVED_ATTRIBS);
	glLinkProgram(prog);
	if (!piglit_link_check_status(prog))
		piglit_report_result(PIGLIT_FAIL);
	return prog;
}

/* Draws one point with prog and returns its outputs in out, if not NULL. */
static void
capture(GLuint prog, unsigned n, float *out)
{
	GLint old_prog;

	glGetIntegerv(GL_CURRENT_PROGRAM, &old_prog);
	glUseProgram(prog);
	glBindBufferBase(GL_TRANSFORM_FEEDBACK_BUFFER, 0, tf_buf);
	glEnable(GL_RASTERIZER_DISCARD);
	glBeginTransformFeedback(GL_POINTS);
	glBegin(GL_POINTS);
	glVertex2f(0, 0);
	glEnd();
	glEndTransformFeedback();
	glDisable(GL_RASTERIZER_DISCARD);
	if (out)
		glGetBufferSubData(GL_TRANSFORM_FEEDBACK_BUFFER, 0,
				   n * 4 * sizeof(float), out);
	glUseProgram(old_prog);
}

static void
draw_warm(void)
{
	for (unsigned g = 0; g < NUM_GROUPS; g++)
		capture(warm[g], num_outputs(&groups[g]), NULL);
}

static void
set_base_state(void)
{
	static const double plane0[4] = { 1, 0, 0, 0.5 };
	static const double plane1[4] = { 0, 1, 1, -0.25 };

	glMatrixMode(GL_PROJECTION);
	glLoadIdentity();
	glOrtho(-2, 2, -2, 2, -4, 4);
	for (int unit = 1; unit >= 0; unit--) {
		glActiveTexture(GL_TEXTURE0 + unit);
		glMatrixMode(GL_TEXTURE);
		glLoadIdentity();
		glTranslatef(0.25 * (unit + 1), 0, 0);
	}
	glMatrixMode(GL_MODELVIEW);
	glLoadIdentity();
	glTranslatef(0.1, 0.2, -0.5);
	glRotatef(20, 0, 1, 0);

	glClipPlane(GL_CLIP_PLANE0, plane0);
	glClipPlane(GL_CLIP_PLANE1, plane1);
	set_base_lighting_state();
}

static bool
compare(unsigned g, const float *got, const float *expected)
{
	bool pass = true;

	for (unsigned i = 0; i < num_outputs(&groups[g]) * 4; i++) {
		float e = expected[i];

		if (fabsf(got[i] - e) <= 1e-5f * fmaxf(1.0f, fabsf(e)))
			continue;
		printf("  %s.%c is %g, a new program reads %g\n",
		       groups[g].outputs[i / 4], "xyzw"[i % 4], got[i], e);
		pass = false;
	}
	return pass;
}

static enum piglit_result
run_scenario(void *data)
{
	const struct scenario *s = data;
	float got[NUM_GROUPS][MAX_OUTPUTS * 4];
	bool pass = true;

	set_base_state();
	s->setup();
	/* several draws so the programs have used this state before */
	for (int i = 0; i < 3; i++)
		draw_warm();
	s->change();

	/* Capture every warm program before anything is linked, so a link
	 * can't flush state they would have used.
	 */
	for (unsigned g = 0; g < NUM_GROUPS; g++)
		capture(warm[g], num_outputs(&groups[g]), got[g]);

	for (unsigned g = 0; g < NUM_GROUPS; g++) {
		float expected[MAX_OUTPUTS * 4], again[MAX_OUTPUTS * 4];
		GLuint fresh = build_program(&groups[g]);

		capture(fresh, num_outputs(&groups[g]), expected);
		pass &= compare(g, got[g], expected);
		glDeleteProgram(fresh);

		/* and again, now that another program has drawn */
		capture(warm[g], num_outputs(&groups[g]), again);
		pass &= compare(g, again, expected);
	}

	return piglit_check_gl_error(GL_NO_ERROR) && pass ? PIGLIT_PASS :
							    PIGLIT_FAIL;
}

static void
setup_push_matrix(void)
{
	glPushMatrix();
	glRotatef(45, 1, 0, 0);
	glMatrixMode(GL_PROJECTION);
	glPushMatrix();
	glScalef(0.5, 0.5, 0.5);
	glMatrixMode(GL_MODELVIEW);
}

static void
change_pop_matrix(void)
{
	glMatrixMode(GL_PROJECTION);
	glPopMatrix();
	glMatrixMode(GL_MODELVIEW);
	glPopMatrix();
}

static void
setup_push_transform(void)
{
	static const double plane[4] = { -1, 2, -3, 4 };

	glPushAttrib(GL_TRANSFORM_BIT);
	glClipPlane(GL_CLIP_PLANE0, plane);
	glMatrixMode(GL_PROJECTION);
}

static void
setup_list_color(void)
{
	setup_color_material();
	glNewList(color_list, GL_COMPILE);
	glColor4f(0.3, 0.9, 0.2, 1);
	glEndList();
}

static void
setup_list_state(void)
{
	static const float pos[4] = { 0, 0, 1, 0 };
	static const float color[4] = { 0.25, 0.75, 0.5, 1 };
	static const double plane[4] = { 0.5, 0.5, 0, 1 };

	glNewList(state_list, GL_COMPILE);
	glRotatef(30, 0, 0, 1);
	glLightfv(GL_LIGHT0, GL_POSITION, pos);
	glMaterialfv(GL_FRONT, GL_SPECULAR, color);
	glClipPlane(GL_CLIP_PLANE1, plane);
	glMatrixMode(GL_PROJECTION);
	glTranslatef(0.1, 0, 0);
	glMatrixMode(GL_MODELVIEW);
	glEndList();
}

static void
change_call_color_list(void)
{
	glCallList(color_list);
}

static void
change_call_state_list(void)
{
	glCallList(state_list);
}

static void
change_modelview(void)
{
	glRotatef(30, 1, 2, 3);
}

static void
change_projection(void)
{
	glMatrixMode(GL_PROJECTION);
	glFrustum(-1, 1, -1, 1, 1, 10);
	glMatrixMode(GL_MODELVIEW);
}

static void
change_several(void)
{
	static const float color[4] = { 0.1, 0.2, 0.9, 1 };

	glTranslatef(1, 2, 3);
	glMaterialfv(GL_FRONT, GL_AMBIENT, color);
	glMatrixMode(GL_PROJECTION);
	glScalef(1, 2, 1);
	glMatrixMode(GL_MODELVIEW);
	glLightfv(GL_LIGHT1, GL_DIFFUSE, color);
	glScalef(3, 3, 3);
}

static void
change_texture_matrix(void)
{
	glActiveTexture(GL_TEXTURE1);
	glMatrixMode(GL_TEXTURE);
	glScalef(2, 3, 4);
	glActiveTexture(GL_TEXTURE0);
	glMatrixMode(GL_MODELVIEW);
}

static void
change_clear_only(void)
{
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

static void
change_light_position(void)
{
	static const float pos[4] = { 1, 1, 0, 0 };
	static const float pos1[4] = { 0, -2, 1, 1 };

	glRotatef(15, 0, 1, 0);
	glLightfv(GL_LIGHT0, GL_POSITION, pos);
	glLightfv(GL_LIGHT1, GL_POSITION, pos1);
}

static void
change_spot(void)
{
	static const float dir[3] = { 0, -1, -1 };

	glLightfv(GL_LIGHT0, GL_SPOT_DIRECTION, dir);
	glLightf(GL_LIGHT0, GL_SPOT_CUTOFF, 30);
	glLightf(GL_LIGHT0, GL_SPOT_EXPONENT, 4);
	glLightf(GL_LIGHT0, GL_QUADRATIC_ATTENUATION, 0.5);
}

static void
change_material(void)
{
	static const float emission[4] = { 0.05, 0.1, 0.15, 1 };
	static const float diffuse[4] = { 0.9, 0.8, 0.1, 0.5 };

	glMaterialfv(GL_FRONT, GL_EMISSION, emission);
	glMaterialfv(GL_FRONT, GL_DIFFUSE, diffuse);
	glMaterialf(GL_BACK, GL_SHININESS, 64);
}

static void
change_clip_plane(void)
{
	static const double plane[4] = { 0, 0, 1, -1 };

	glRotatef(90, 1, 0, 0);
	glClipPlane(GL_CLIP_PLANE0, plane);
}

static void
change_light_model(void)
{
	static const float ambient[4] = { 0.4, 0.3, 0.2, 1 };

	glLightModelfv(GL_LIGHT_MODEL_AMBIENT, ambient);
}

static void
change_enable_light1(void)
{
	glEnable(GL_LIGHT1);
	glRotatef(10, 1, 0, 0);
}

static void
change_other_program(void)
{
	/* each warm program draws once between two changes */
	glRotatef(25, 0, 0, 1);
	draw_warm();
	glTranslatef(0, 1, 0);
	glMaterialf(GL_FRONT, GL_SHININESS, 3);
}

static void
change_relink(void)
{
	glRotatef(5, 0, 1, 0);
	for (unsigned g = 0; g < NUM_GROUPS; g++)
		glLinkProgram(warm[g]);
	glTranslatef(0, 0, 1);
}

enum piglit_result
piglit_display(void)
{
	return run_subtests(subtests, piglit_config);
}

void
piglit_init(int argc, char **argv)
{
	for (unsigned g = 0; g < NUM_GROUPS; g++)
		warm[g] = build_program(&groups[g]);
	glGenBuffers(1, &tf_buf);
	glBindBuffer(GL_TRANSFORM_FEEDBACK_BUFFER, tf_buf);
	glBufferData(GL_TRANSFORM_FEEDBACK_BUFFER, MAX_OUTPUTS * 4 * sizeof(float),
		     NULL, GL_STREAM_READ);
	color_list = glGenLists(2);
	state_list = color_list + 1;
}
