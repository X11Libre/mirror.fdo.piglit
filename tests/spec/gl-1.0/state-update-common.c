/* SPDX-License-Identifier: MIT */

#include "state-update-common.h"

enum piglit_result
run_subtests(const struct piglit_subtest *subtests,
	     const struct piglit_gl_test_config *config)
{
	return piglit_run_selected_subtests(subtests,
					    config->selected_subtests,
					    config->num_selected_subtests,
					    PIGLIT_SKIP);
}

static void
set_light(GLenum light, const float *pos, const float *color)
{
	glLightfv(light, GL_POSITION, pos);
	glLightfv(light, GL_DIFFUSE, color);
	glLightfv(light, GL_SPECULAR, color);
}

void
set_base_lighting_state(void)
{
	static const float l0_pos[4] = { 0.3, 0.5, 0.8, 0 };
	static const float l1_pos[4] = { -1, 2, 3, 1 };
	static const float l0_color[4] = { 0.5, 0.4, 0.3, 1 };
	static const float l1_color[4] = { 0.2, 0.3, 0.4, 1 };
	static const float black[4] = { 0, 0, 0, 1 };
	static const float light_ambient[4] = { 0.1, 0.1, 0.1, 1 };
	static const float model_ambient[4] = { 0.1, 0.1, 0.15, 1 };
	static const float mat_ambient[4] = { 0.4, 0.4, 0.4, 1 };
	static const float mat_diffuse[4] = { 0.6, 0.5, 0.4, 1 };
	static const float mat_specular[4] = { 0.4, 0.4, 0.4, 1 };

	glEnable(GL_LIGHTING);
	glEnable(GL_NORMALIZE);
	glDisable(GL_RESCALE_NORMAL);
	glShadeModel(GL_SMOOTH);
	glEnable(GL_LIGHT0);
	glDisable(GL_LIGHT1);
	glLightModelfv(GL_LIGHT_MODEL_AMBIENT, model_ambient);
	glLightModeli(GL_LIGHT_MODEL_LOCAL_VIEWER, GL_FALSE);
	glLightModeli(GL_LIGHT_MODEL_TWO_SIDE, GL_FALSE);
	glLightModeli(GL_LIGHT_MODEL_COLOR_CONTROL, GL_SINGLE_COLOR);

	for (int i = 0; i < NUM_LIGHTS; i++) {
		GLenum light = GL_LIGHT0 + i;

		set_light(light, i ? l1_pos : l0_pos, i ? l1_color : l0_color);
		glLightfv(light, GL_AMBIENT, light_ambient);
		glLightfv(light, GL_SPOT_DIRECTION, (const float[]){ 0, 0, -1 });
		glLightf(light, GL_SPOT_EXPONENT, 0);
		glLightf(light, GL_SPOT_CUTOFF, 180);
		glLightf(light, GL_CONSTANT_ATTENUATION, 1);
		glLightf(light, GL_LINEAR_ATTENUATION, 0);
		glLightf(light, GL_QUADRATIC_ATTENUATION, 0);
	}

	glDisable(GL_COLOR_MATERIAL);
	glMaterialfv(GL_FRONT_AND_BACK, GL_EMISSION, black);
	glMaterialfv(GL_FRONT_AND_BACK, GL_AMBIENT, mat_ambient);
	glMaterialfv(GL_FRONT_AND_BACK, GL_DIFFUSE, mat_diffuse);
	glMaterialfv(GL_FRONT_AND_BACK, GL_SPECULAR, mat_specular);
	glMaterialf(GL_FRONT_AND_BACK, GL_SHININESS, 8);

	glActiveTexture(GL_TEXTURE0);
	glDisable(GL_TEXTURE_GEN_S);
	glTexGeni(GL_S, GL_TEXTURE_GEN_MODE, GL_OBJECT_LINEAR);
	glPointParameterfv(GL_POINT_DISTANCE_ATTENUATION,
			   (const float[]){ 1, 0, 0 });
	glColor4f(1, 1, 1, 1);
}

void
setup_nothing(void)
{
}

void
setup_color_material(void)
{
	glColorMaterial(GL_FRONT_AND_BACK, GL_AMBIENT_AND_DIFFUSE);
	glEnable(GL_COLOR_MATERIAL);
	glColor4f(0.2, 0.4, 0.6, 1);
}

void
setup_rescale(void)
{
	glDisable(GL_NORMALIZE);
	glEnable(GL_RESCALE_NORMAL);
	glScalef(1.5, 1.5, 1.5);
}

void
setup_push_lighting(void)
{
	static const float pos[4] = { 2, -1, 1, 1 };
	static const float color[4] = { 0.7, 0.2, 0.1, 1 };

	glPushAttrib(GL_LIGHTING_BIT);
	set_light(GL_LIGHT0, pos, color);
	glMaterialfv(GL_FRONT, GL_AMBIENT, color);
	glMaterialfv(GL_FRONT, GL_DIFFUSE, color);
	glLightModelfv(GL_LIGHT_MODEL_AMBIENT, color);
	glLightModeli(GL_LIGHT_MODEL_LOCAL_VIEWER, GL_TRUE);
}

void
change_color(void)
{
	glColor4f(0.7, 0.1, 0.3, 0.5);
}

void
change_scale(void)
{
	glScalef(0.25, 0.25, 0.25);
}

void
change_pop_attrib(void)
{
	glPopAttrib();
}

void
change_local_viewer(void)
{
	glLightModeli(GL_LIGHT_MODEL_LOCAL_VIEWER, GL_TRUE);
}

void
change_texgen_eye(void)
{
	glTexGeni(GL_S, GL_TEXTURE_GEN_MODE, GL_EYE_LINEAR);
	glEnable(GL_TEXTURE_GEN_S);
}

void
change_point_attenuation(void)
{
	glPointParameterfv(GL_POINT_DISTANCE_ATTENUATION,
			   (const float[]){ 1, 0.5, 0 });
}

void
change_nonuniform_scale(void)
{
	glScalef(1, 3, 0.5);
}

void
change_then_clear(void)
{
	glRotatef(25, 1, 0, 0);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}
