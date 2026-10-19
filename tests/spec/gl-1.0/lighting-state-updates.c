/* SPDX-License-Identifier: MIT */

/**
 * Fixed-function lighting must follow every way the lighting state can
 * change between draws: matrix changes that move lights relative to the
 * object, light and material changes, color material, display lists,
 * attribute and matrix stack pops, and changes that switch the space
 * lighting is computed in (local viewer, non-uniform scale, eye-linear
 * texgen, point attenuation).
 *
 * Each check draws single points and compares their color with the
 * lighting equation of the OpenGL 2.1 specification, section 2.14.1,
 * evaluated with the state read back through glGet.
 */

#include "piglit-util-gl.h"
#include "piglit-matrix.h"
#include "state-update-common.h"

static enum piglit_result run_scenario(void *data);

static void setup_positional(void);
static void setup_spot(void);
static void setup_list(void);
static void setup_push_matrix(void);

static void change_rotate(void);
static void change_light_position(void);
static void change_spot(void);
static void change_call_list(void);
static void change_pop_matrix(void);
static void change_toggle_light(void);

static const struct piglit_subtest subtests[] = {
	SCENARIO("rotate", "rotate", setup_nothing, change_rotate),
	SCENARIO("positional light", "positional", setup_positional,
		 change_light_position),
	SCENARIO("spot light", "spot", setup_spot, change_spot),
	SCENARIO("local viewer", "local-viewer", setup_positional,
		 change_local_viewer),
	SCENARIO("rescale normal", "rescale-normal", setup_rescale,
		 change_scale),
	SCENARIO("non-uniform scale", "nonuniform-scale", setup_positional,
		 change_nonuniform_scale),
	SCENARIO("color material", "color-material", setup_color_material,
		 change_color),
	SCENARIO("display list", "list", setup_list, change_call_list),
	SCENARIO("pop lighting attributes", "pop-lighting",
		 setup_push_lighting, change_pop_attrib),
	SCENARIO("pop matrix", "pop-matrix", setup_push_matrix,
		 change_pop_matrix),
	SCENARIO("eye-linear texgen", "texgen", setup_positional,
		 change_texgen_eye),
	SCENARIO("point attenuation", "point-attenuation", setup_positional,
		 change_point_attenuation),
	SCENARIO("toggle a second light", "toggle-light", setup_positional,
		 change_toggle_light),
	SCENARIO("change then clear", "change-clear", setup_spot,
		 change_then_clear),
	{ NULL }
};

static const struct piglit_gl_test_config *piglit_config;

PIGLIT_GL_TEST_CONFIG_BEGIN

	config.supports_gl_compat_version = 14;
	config.window_visual = PIGLIT_GL_VISUAL_RGBA | PIGLIT_GL_VISUAL_DOUBLE;
	config.khr_no_error_support = PIGLIT_NO_ERRORS;
	config.subtests = subtests;
	piglit_config = &config;

PIGLIT_GL_TEST_CONFIG_END

static GLuint list;
static bool pass;

static float
dot3(const float *a, const float *b)
{
	return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

static void
normalize3(float *v)
{
	float len = sqrtf(dot3(v, v));

	for (int i = 0; i < 3; i++)
		v[i] /= len;
}

/* The lighting equation for a front-facing vertex at eye position v with
 * the unit eye normal n. The specification has a light's specular term
 * when dot(n, VP) isn't 0, and implementations commonly only when it's
 * positive; lit_from_behind selects the specification's reading.
 */
static void
expected_color(const float *v, const float *n, bool lit_from_behind,
	       float *color)
{
	float emission[4], ambient[4], diffuse[4], specular[4], shininess;
	float model_ambient[4];
	GLint local_viewer;

	glGetMaterialfv(GL_FRONT, GL_EMISSION, emission);
	glGetMaterialfv(GL_FRONT, GL_AMBIENT, ambient);
	glGetMaterialfv(GL_FRONT, GL_DIFFUSE, diffuse);
	glGetMaterialfv(GL_FRONT, GL_SPECULAR, specular);
	glGetMaterialfv(GL_FRONT, GL_SHININESS, &shininess);
	if (glIsEnabled(GL_COLOR_MATERIAL)) {
		/* the tests track GL_AMBIENT_AND_DIFFUSE */
		glGetFloatv(GL_CURRENT_COLOR, diffuse);
		memcpy(ambient, diffuse, sizeof(ambient));
	}
	glGetFloatv(GL_LIGHT_MODEL_AMBIENT, model_ambient);
	glGetIntegerv(GL_LIGHT_MODEL_LOCAL_VIEWER, &local_viewer);

	for (int c = 0; c < 3; c++)
		color[c] = emission[c] + ambient[c] * model_ambient[c];

	for (int i = 0; i < NUM_LIGHTS; i++) {
		GLenum light = GL_LIGHT0 + i;
		float la[4], ld[4], ls[4], pos[4], spot_dir[3], vp[3], h[3];
		float exponent, cutoff, k[3], att = 1, spot = 1;

		if (!glIsEnabled(light))
			continue;
		glGetLightfv(light, GL_AMBIENT, la);
		glGetLightfv(light, GL_DIFFUSE, ld);
		glGetLightfv(light, GL_SPECULAR, ls);
		glGetLightfv(light, GL_POSITION, pos);
		glGetLightfv(light, GL_SPOT_DIRECTION, spot_dir);
		glGetLightfv(light, GL_SPOT_EXPONENT, &exponent);
		glGetLightfv(light, GL_SPOT_CUTOFF, &cutoff);
		glGetLightfv(light, GL_CONSTANT_ATTENUATION, &k[0]);
		glGetLightfv(light, GL_LINEAR_ATTENUATION, &k[1]);
		glGetLightfv(light, GL_QUADRATIC_ATTENUATION, &k[2]);

		if (pos[3] == 0) {
			memcpy(vp, pos, sizeof(vp));
		} else {
			for (int c = 0; c < 3; c++)
				vp[c] = pos[c] / pos[3] - v[c];
			float d = sqrtf(dot3(vp, vp));
			att = 1 / (k[0] + k[1] * d + k[2] * d * d);
		}
		normalize3(vp);

		if (cutoff != 180) {
			float t;

			normalize3(spot_dir);
			t = -dot3(vp, spot_dir);
			spot = t >= cosf(cutoff * M_PI / 180) ?
			       powf(t, exponent) : 0;
		}

		if (local_viewer) {
			float e[3] = { -v[0], -v[1], -v[2] };

			normalize3(e);
			for (int c = 0; c < 3; c++)
				h[c] = vp[c] + e[c];
		} else {
			for (int c = 0; c < 3; c++)
				h[c] = vp[c] + (c == 2);
		}
		normalize3(h);

		float ndotvp = dot3(n, vp);
		float ndoth = fmaxf(dot3(n, h), 0);
		bool has_spec = lit_from_behind ? ndotvp != 0 : ndotvp > 0;
		float spec = has_spec ? powf(ndoth, shininess) : 0;

		for (int c = 0; c < 3; c++)
			color[c] += att * spot *
				    (ambient[c] * la[c] +
				     fmaxf(ndotvp, 0) * diffuse[c] * ld[c] +
				     spec * specular[c] * ls[c]);
	}

	for (int c = 0; c < 3; c++)
		color[c] = CLAMP(color[c], 0, 1);
}

/* within 3 steps of an 8-bit channel */
static bool
matches(const float *got, const float *expected)
{
	for (int c = 0; c < 3; c++) {
		if (fabsf(got[c] - expected[c]) > 3.0 / 255)
			return false;
	}
	return true;
}

/* Draws points at a few positions and checks their colors. */
static void
check(const char *when)
{
	static const float points[][4] = {
		{ 0.5, 0.3, 0, 1 },
		{ -0.4, -0.6, 0.2, 1 },
		{ 0.1, -0.2, -0.3, 1 },
	};
	static const float normals[][3] = {
		{ 0, 0, 1 },
		{ 0.3, 0.4, 0.866 },
		{ -0.5, 0.2, 0.8426 },
	};
	float mv[16], proj[16], inv[16], normal_matrix[16];

	glGetFloatv(GL_MODELVIEW_MATRIX, mv);
	glGetFloatv(GL_PROJECTION_MATRIX, proj);
	piglit_matrix_inverse(inv, mv);
	piglit_matrix_transpose(normal_matrix, inv);

	for (unsigned i = 0; i < ARRAY_SIZE(points); i++) {
		const float *nm = normals[i];
		float eye[4], n[4], win[3], expected[2][3], got[4];

		if (!piglit_project_to_window(win, points[i], mv, proj, 0, 0,
					      piglit_width, piglit_height)) {
			printf("%s: point %u is outside the window\n", when, i);
			pass = false;
			continue;
		}

		piglit_matrix_mul_vector(eye, mv, points[i]);
		piglit_matrix_mul_vector(n, normal_matrix,
					 (float[]){ nm[0], nm[1], nm[2], 0 });
		normalize3(n);

		glNormal3fv(normals[i]);
		glBegin(GL_POINTS);
		glVertex4fv(points[i]);
		glEnd();

		expected_color(eye, n, false, expected[0]);
		expected_color(eye, n, true, expected[1]);
		glReadPixels(win[0], win[1], 1, 1, GL_RGBA, GL_FLOAT, got);
		if (matches(got, expected[0]) || matches(got, expected[1]))
			continue;
		printf("%s: point %u is (%.3f, %.3f, %.3f), expected "
		       "(%.3f, %.3f, %.3f)\n", when, i, got[0], got[1], got[2],
		       expected[0][0], expected[0][1], expected[0][2]);
		pass = false;
	}
}

static void
set_base_state(void)
{
	glMatrixMode(GL_PROJECTION);
	glLoadIdentity();
	glOrtho(-2, 2, -2, 2, -10, 10);
	glMatrixMode(GL_MODELVIEW);
	glLoadIdentity();
	glRotatef(10, 0, 1, 0);

	set_base_lighting_state();
	glClear(GL_COLOR_BUFFER_BIT);
}

static enum piglit_result
run_scenario(void *data)
{
	const struct scenario *s = data;

	pass = true;
	set_base_state();
	s->setup();
	/* several draws so the generated program has used this state */
	for (int i = 0; i < 3; i++)
		check("before the change");
	s->change();
	check("after the change");

	return piglit_check_gl_error(GL_NO_ERROR) && pass ? PIGLIT_PASS :
							    PIGLIT_FAIL;
}

static void
setup_positional(void)
{
	static const float pos[4] = { 1, 1, 2, 1 };

	glLightfv(GL_LIGHT0, GL_POSITION, pos);
	glLightf(GL_LIGHT0, GL_LINEAR_ATTENUATION, 0.2);
}

static void
setup_spot(void)
{
	static const float pos[4] = { 0, 0, 3, 1 };

	glLightfv(GL_LIGHT0, GL_POSITION, pos);
	glLightf(GL_LIGHT0, GL_SPOT_CUTOFF, 60);
	glLightf(GL_LIGHT0, GL_SPOT_EXPONENT, 2);
}

static void
setup_list(void)
{
	static const float pos[4] = { -0.5, 0.2, 1, 0 };
	static const float diffuse[4] = { 0.3, 0.7, 0.5, 1 };

	glNewList(list, GL_COMPILE);
	glRotatef(35, 1, 0, 0);
	glLightfv(GL_LIGHT0, GL_POSITION, pos);
	glMaterialfv(GL_FRONT, GL_DIFFUSE, diffuse);
	glEndList();
}

static void
setup_push_matrix(void)
{
	glPushMatrix();
	glRotatef(50, 1, 0, 1);
}

static void
change_rotate(void)
{
	glRotatef(40, 1, 1, 0);
}

static void
change_light_position(void)
{
	static const float pos[4] = { -1, 0.5, 1.5, 1 };

	glLightfv(GL_LIGHT0, GL_POSITION, pos);
}

static void
change_spot(void)
{
	static const float dir[3] = { 0.2, -0.1, -1 };

	glRotatef(15, 0, 1, 0);
	glLightfv(GL_LIGHT0, GL_SPOT_DIRECTION, dir);
	glLightf(GL_LIGHT0, GL_SPOT_EXPONENT, 5);
}

static void
change_call_list(void)
{
	glCallList(list);
}

static void
change_pop_matrix(void)
{
	glPopMatrix();
}

/* Each light count gets its own generated program, so the one-light
 * program comes back after drawing with two.
 */
static void
change_toggle_light(void)
{
	glEnable(GL_LIGHT1);
	check("with the second light");
	glRotatef(30, 0, 0, 1);
	check("with the second light, rotated");
	glDisable(GL_LIGHT1);
	glRotatef(-20, 1, 0, 0);
}

enum piglit_result
piglit_display(void)
{
	return run_subtests(subtests, piglit_config);
}

void
piglit_init(int argc, char **argv)
{
	glPointSize(5);
	glDisable(GL_DITHER);
	list = glGenLists(1);
}
