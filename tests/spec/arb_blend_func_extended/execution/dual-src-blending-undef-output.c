/*
 * Copyright 2026 Valve Corporation
 * SPDX-License-Identifier: MIT
 */

#include "piglit-util-gl.h"

static const int render_width = 128;
static const int render_height = 128;

PIGLIT_GL_TEST_CONFIG_BEGIN
    config.supports_gl_compat_version = 33;
    config.window_width = render_width;
    config.window_height = render_height;
    config.window_visual = PIGLIT_GL_VISUAL_DOUBLE | PIGLIT_GL_VISUAL_RGBA;
    config.khr_no_error_support = PIGLIT_NO_ERRORS;
PIGLIT_GL_TEST_CONFIG_END

static const char *vs_text =
    "#version 330\n"
    "layout (location = 0) in vec4 vertex;\n"
    "void main() { gl_Position = vertex; }\n"
    ;

static const char *fs_mrt0_undef_text =
    "#version 330\n"
    "layout (location = 0, index = 0) out vec4 col0;\n"
    "layout (location = 0, index = 1) out vec4 col1;\n"
    "void main() {\n"
    "    col1 = vec4(0.0, 1.0, 0.0, 1.0);\n"
    "}\n"
    ;

static const char *fs_mrt1_undef_text =
    "#version 330\n"
    "layout (location = 0, index = 0) out vec4 col0;\n"
    "layout (location = 0, index = 1) out vec4 col1;\n"
    "void main() {\n"
    "    col0 = vec4(0.0, 1.0, 0.0, 1.0);\n"
    "}\n"
    ;

static const char *fs_both_undef_text =
    "#version 330\n"
    "layout (location = 0, index = 0) out vec4 col0;\n"
    "layout (location = 0, index = 1) out vec4 col1;\n"
    "void main() {\n"
    "}\n"
    ;

static GLuint prog_mrt0_undef, prog_mrt1_undef, prog_both_undef;

enum piglit_result
piglit_display(void)
{
    static const GLfloat expected_color[4] = { 0.0, 1.0, 0.0, 1.0 };

    glUseProgram(prog_both_undef);
    glEnable(GL_BLEND);
    glBlendFuncSeparate(GL_ONE, GL_SRC1_COLOR, GL_ONE, GL_SRC1_ALPHA);
    glBlendEquationSeparate(GL_FUNC_ADD, GL_FUNC_ADD);
    piglit_draw_rect(-1, -1, 2, 2);

    glEnable(GL_SCISSOR_TEST);

    glUseProgram(prog_mrt0_undef);
    glScissor(0 ,0, render_width / 2, render_height);
    glClearColor(1.0, 1.0, 1.0, 1.0);
    glClear(GL_COLOR_BUFFER_BIT);
    glEnable(GL_BLEND);
    glBlendFuncSeparate(GL_ZERO, GL_SRC1_COLOR, GL_ZERO, GL_SRC1_ALPHA);
    glBlendEquationSeparate(GL_FUNC_ADD, GL_FUNC_ADD);
    piglit_draw_rect(-1, -1, 1, 2);

    glUseProgram(prog_mrt1_undef);
    glScissor(render_width / 2, 0, render_width / 2, render_height);
    glClearColor(0.0, 0.0, 0.0, 0.0);
    glClear(GL_COLOR_BUFFER_BIT);
    glEnable(GL_BLEND);
    glBlendFuncSeparate(GL_ONE, GL_SRC1_COLOR, GL_ONE, GL_SRC1_ALPHA);
    glBlendEquationSeparate(GL_FUNC_ADD, GL_FUNC_ADD);
    piglit_draw_rect(0, -1, 1, 2);

    bool pass = piglit_probe_rect_rgba(0, 0, render_width, render_height, expected_color);

    piglit_present_results();

    return pass ? PIGLIT_PASS : PIGLIT_FAIL;
}

void
piglit_init(int argc, char**argv)
{
    piglit_require_extension("GL_ARB_blend_func_extended");
    prog_mrt0_undef = piglit_build_simple_program(vs_text, fs_mrt0_undef_text);
    prog_mrt1_undef = piglit_build_simple_program(vs_text, fs_mrt1_undef_text);
    prog_both_undef = piglit_build_simple_program(vs_text, fs_both_undef_text);
}
