/* SPDX-License-Identifier: MIT */

/**
 * Helpers shared by the tests that change compatibility state between draws
 * (gl-1.0-lighting-state-updates and gl-3.0-builtin-uniform-updates): the
 * subtest table plumbing, the base lighting state, and the setup and change
 * steps both tests use.
 */

#ifndef STATE_UPDATE_COMMON_H
#define STATE_UPDATE_COMMON_H

#include "piglit-util-gl.h"

#define NUM_LIGHTS 2

struct scenario {
	void (*setup)(void);
	void (*change)(void);
};

/* Each test defines run_scenario to run a scenario passed as its data. */
#define SCENARIO(name, option, setup, change) \
	{ name, option, run_scenario, \
	  (void *)&(const struct scenario){ setup, change } }

enum piglit_result
run_subtests(const struct piglit_subtest *subtests,
	     const struct piglit_gl_test_config *config);

/* Known values for the lighting, material, texgen, and point state, not the
 * defaults, so that a value left over from an earlier subtest shows up. It
 * leaves texture unit 0 active.
 */
void set_base_lighting_state(void);

void setup_nothing(void);
void setup_color_material(void);
void setup_rescale(void);
void setup_push_lighting(void);

void change_color(void);
void change_scale(void);
void change_pop_attrib(void);
void change_local_viewer(void);
void change_texgen_eye(void);
void change_point_attenuation(void);
void change_nonuniform_scale(void);
void change_then_clear(void);

#endif
