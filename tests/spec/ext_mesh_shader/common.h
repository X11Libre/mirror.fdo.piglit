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

#ifndef __PIGLIT_EXT_MESH_SHADER_COMMON_H__
#define __PIGLIT_EXT_MESH_SHADER_COMMON_H__

#include "piglit-util-gl.h"

#define MESH_UBO_BLOCK \
	"layout(std140, binding = 0) uniform Block { float x_offset; }"

#define MESH_SSBO_BLOCK \
	"layout(std430, binding = 0) readonly buffer Block { float x_offset; }"

#define MESH_X_LEFT (-0.5f)
#define MESH_X_RIGHT (0.5f)

GLuint generate_ms_prog(const char *block_decl);

void ms_begin_frame(GLuint prog);

enum piglit_result ms_probe_both_halves(void);

#endif /* __PIGLIT_EXT_MESH_SHADER_COMMON_H__ */
