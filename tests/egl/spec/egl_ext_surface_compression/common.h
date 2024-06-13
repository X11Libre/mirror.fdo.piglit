/*
 * Copyright © 2024 Collabora Ltd.
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

#include <EGL/egl.h>

#ifndef COMMON_H
#define COMMON_H

#ifndef EGL_EXT_surface_compression
#define EGL_EXT_surface_compression 1
#define EGL_SURFACE_COMPRESSION_EXT       0x34B0
#define EGL_SURFACE_COMPRESSION_PLANE1_EXT 0x328E
#define EGL_SURFACE_COMPRESSION_PLANE2_EXT 0x328F
#define EGL_SURFACE_COMPRESSION_FIXED_RATE_NONE_EXT 0x34B1
#define EGL_SURFACE_COMPRESSION_FIXED_RATE_DEFAULT_EXT 0x34B2
#define EGL_SURFACE_COMPRESSION_FIXED_RATE_1BPC_EXT 0x34B4
#define EGL_SURFACE_COMPRESSION_FIXED_RATE_2BPC_EXT 0x34B5
#define EGL_SURFACE_COMPRESSION_FIXED_RATE_3BPC_EXT 0x34B6
#define EGL_SURFACE_COMPRESSION_FIXED_RATE_4BPC_EXT 0x34B7
#define EGL_SURFACE_COMPRESSION_FIXED_RATE_5BPC_EXT 0x34B8
#define EGL_SURFACE_COMPRESSION_FIXED_RATE_6BPC_EXT 0x34B9
#define EGL_SURFACE_COMPRESSION_FIXED_RATE_7BPC_EXT 0x34BA
#define EGL_SURFACE_COMPRESSION_FIXED_RATE_8BPC_EXT 0x34BB
#define EGL_SURFACE_COMPRESSION_FIXED_RATE_9BPC_EXT 0x34BC
#define EGL_SURFACE_COMPRESSION_FIXED_RATE_10BPC_EXT 0x34BD
#define EGL_SURFACE_COMPRESSION_FIXED_RATE_11BPC_EXT 0x34BE
#define EGL_SURFACE_COMPRESSION_FIXED_RATE_12BPC_EXT 0x34BF
typedef EGLBoolean (EGLAPIENTRYP PFNEGLQUERYSUPPORTEDCOMPRESSIONRATESEXTPROC) (EGLDisplay dpy, EGLConfig config, const EGLAttrib *attrib_list, EGLint *rates, EGLint rate_size, EGLint *num_rates);
#endif /* EGL_EXT_surface_compression */

static int
enum_to_rate(EGLint value)
{
	switch (value) {
	case EGL_SURFACE_COMPRESSION_FIXED_RATE_1BPC_EXT: return 1;
	case EGL_SURFACE_COMPRESSION_FIXED_RATE_2BPC_EXT: return 2;
	case EGL_SURFACE_COMPRESSION_FIXED_RATE_3BPC_EXT: return 3;
	case EGL_SURFACE_COMPRESSION_FIXED_RATE_4BPC_EXT: return 4;
	case EGL_SURFACE_COMPRESSION_FIXED_RATE_5BPC_EXT: return 5;
	case EGL_SURFACE_COMPRESSION_FIXED_RATE_6BPC_EXT: return 6;
	case EGL_SURFACE_COMPRESSION_FIXED_RATE_7BPC_EXT: return 7;
	case EGL_SURFACE_COMPRESSION_FIXED_RATE_8BPC_EXT: return 8;
	case EGL_SURFACE_COMPRESSION_FIXED_RATE_9BPC_EXT: return 9;
	case EGL_SURFACE_COMPRESSION_FIXED_RATE_10BPC_EXT: return 10;
	case EGL_SURFACE_COMPRESSION_FIXED_RATE_11BPC_EXT: return 11;
	case EGL_SURFACE_COMPRESSION_FIXED_RATE_12BPC_EXT: return 12;
	default: return 0;
	}
}


#endif /* COMMON_H */
