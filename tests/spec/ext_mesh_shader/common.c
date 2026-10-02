/*
 * Copyright © 2026 Valve Corporation
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

#include "common.h"

static const char *fs_text =
	"#version 450\n"
	"\n"
	"layout(location = 0) in vec3 color;\n"
	"layout(location = 0) out vec4 out_color;\n"
	"\n"
	"void main()\n"
	"{\n"
	"	out_color = vec4(color, 1.0);\n"
	"}\n";

GLuint
generate_ms_prog(const char *block_decl)
{
	GLuint ms, fs, prog, vao;
	char *ms_text;

	(void)!asprintf(&ms_text,
		"#version 450\n"
		"#extension GL_EXT_mesh_shader : require\n"
		"\n"
		"layout(local_size_x = 1) in;\n"
		"layout(triangles, max_vertices = 4, max_primitives = 2) out;\n"
		"\n"
		"%s;\n"
		"\n"
		"layout(location = 0) out vec3 color[];\n"
		"\n"
		"void main()\n"
		"{\n"
		"	SetMeshOutputsEXT(4u, 2u);\n"
		"\n"
		"	gl_MeshVerticesEXT[0].gl_Position = vec4(x_offset - 0.4, -0.4, 0.0, 1.0);\n"
		"	gl_MeshVerticesEXT[1].gl_Position = vec4(x_offset + 0.4, -0.4, 0.0, 1.0);\n"
		"	gl_MeshVerticesEXT[2].gl_Position = vec4(x_offset - 0.4,  0.4, 0.0, 1.0);\n"
		"	gl_MeshVerticesEXT[3].gl_Position = vec4(x_offset + 0.4,  0.4, 0.0, 1.0);\n"
		"\n"
		"	color[0] = vec3(0.0, 1.0, 0.0);\n"
		"	color[1] = vec3(0.0, 1.0, 0.0);\n"
		"	color[2] = vec3(0.0, 1.0, 0.0);\n"
		"	color[3] = vec3(0.0, 1.0, 0.0);\n"
		"\n"
		"	gl_PrimitiveTriangleIndicesEXT[0] = uvec3(0, 1, 2);\n"
		"	gl_PrimitiveTriangleIndicesEXT[1] = uvec3(1, 3, 2);\n"
		"}\n",
		block_decl);

	ms = piglit_compile_shader_text(GL_MESH_SHADER_EXT, ms_text);
	fs = piglit_compile_shader_text(GL_FRAGMENT_SHADER, fs_text);
	free(ms_text);

	prog = glCreateProgram();
	glAttachShader(prog, ms);
	glAttachShader(prog, fs);
	glLinkProgram(prog);
	if (!piglit_link_check_status(prog))
		piglit_report_result(PIGLIT_FAIL);

	glGenVertexArrays(1, &vao);
	glBindVertexArray(vao);

	return prog;
}

void
ms_begin_frame(GLuint prog)
{
	glClearColor(0.0, 0.0, 0.0, 1.0);
	glClear(GL_COLOR_BUFFER_BIT);
	glUseProgram(prog);
}

enum piglit_result
ms_probe_both_halves(void)
{
	static const float green[4] = {0.0, 1.0, 0.0, 1.0};
	bool pass = true;

	pass = piglit_probe_pixel_rgba(piglit_width / 4, piglit_height / 2,
				       green) && pass;
	pass = piglit_probe_pixel_rgba(piglit_width * 3 / 4, piglit_height / 2,
				       green) && pass;

	piglit_present_results();

	return pass ? PIGLIT_PASS : PIGLIT_FAIL;
}
