/*
 * Copyright (C) 2025 James Hogan <james@albanarts.com>
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
 *
 */

/**
 * Test OVR_multiview multiview rendering.
 *
 * Render a triangle to multiple layers of two textures, first using separate
 * passes with glFramebufferTextureLayer() to the first texture, and then using
 * a single multiview pass with glFramebufferTextureMultiviewOVR() to the second
 * texture.
 *
 * The resulting texture layers are read back with glGetTexImage() and compared
 * to verify that the multiview drawing gives the same result as equivalent
 * separate passes.
 *
 * This tests the values of gl_ViewID_OVR when used for various things
 * (including both OVR_multiview and OVR_multiview2 cases), baseViewIndex
 * multiview draw behaviour, the use of multiview depth attachments, and display
 * lists with multiview.
 *
 * See usage() for command line arguments.
 */

#include "piglit-util-gl.h"

/*
 * nvidia has GL_MAX_VIEWS_OVR=32, so lets have enough width in texture to
 * offset the triangle up to 31 pixels.
 */
#define TEX_WIDTH 32
#define TEX_HEIGHT 4

#define _STRINGIFY(X) #X
#define STRINGIFY(X) _STRINGIFY(X)

PIGLIT_GL_TEST_CONFIG_BEGIN

	config.supports_gl_compat_version = 30;
	config.khr_no_error_support = PIGLIT_NO_ERRORS;

PIGLIT_GL_TEST_CONFIG_END

struct Options {
	unsigned int depth:1;
	unsigned int lists:1;
};

struct ShaderModeInfo {
	const char *name;
	const char *desc;
	bool require_ovr2;
	const char *vert_head;
	const char *vert_global;
	const char *vert_main;
	const char *frag_head;
	const char *frag_global;
	const char *frag_main;
};

struct ShaderModeInfo shader_modes[] = {
	/* basic (OVR_multiview) */
	{
		"basic",
		"simply gl_Position based on ViewID",
		false,
		/* vert_head */
		"#extension GL_OVR_multiview: enable\n",
		/* vert_global */
		"out vec3 color;\n",
		/* vert_main */
		"  color = inCol;\n",
		/* frag_head */
		"",
		/* frag_global */
		"in vec3 color;\n",
		/* frag_main */
		"  gl_FragColor = vec4(color, 1.0);\n",
	},
	/* vert_color (OVR_multiview2) */
	{
		"vert_color",
		"vert shader vary color based on ViewID",
		true,
		/* vert_head */
		"#extension GL_OVR_multiview2: enable\n",
		/* vert_global */
		"out vec3 color;\n",
		/* vert_main */
		"  color = vec3(1.0 - mod(float(ViewID)*4/NUM_VIEWS, 1.0),\n"
		"               1.0 - mod(float(ViewID)*2/NUM_VIEWS, 1.0),\n"
		"               1.0 - mod(float(ViewID)  /NUM_VIEWS, 1.0));\n",
		/* frag_head */
		"",
		/* frag_global */
		"in vec3 color;\n",
		/* frag_main */
		"  gl_FragColor = vec4(color, 1.0);\n",
	},
	/* frag_color (OVR_multiview2) */
	{
		"frag_color",
		"frag shader vary color based on ViewID",
		true,
		/* vert_head */
		"#extension GL_OVR_multiview: enable\n",
		/* vert_global */
		"",
		/* vert_main */
		"",
		/* frag_head */
		"#extension GL_OVR_multiview2: enable\n",
		/* frag_global */
		"",
		/* frag_main */
		"  gl_FragColor = vec4(1.0 - mod(float(ViewID)*4/NUM_VIEWS, 1.0),\n"
		"               1.0 - mod(float(ViewID)*2/NUM_VIEWS, 1.0),\n"
		"               1.0 - mod(float(ViewID)  /NUM_VIEWS, 1.0), 1.0);\n",
	},
	/* vert_index_uniform (OVR_multiview2) */
	{
		"vert_index_uniform",
		"vert shader index uniform array based on ViewID",
		true,
		/* vert_head */
		"#extension GL_OVR_multiview2: enable\n",
		/* vert_global */
		"out vec3 color;\n"
		"uniform vec3 colors[NUM_VIEWS];\n",
		/* vert_main */
		"  color = colors[ViewID];",
		/* frag_head */
		"",
		/* frag_global */
		"in vec3 color;\n",
		/* frag_main */
		"  gl_FragColor = vec4(color, 1.0);\n",
	},
	/* frag_index_uniform (OVR_multiview2) */
	{
		"frag_index_uniform",
		"frag shader index uniform array based on ViewID",
		true,
		/* vert_head */
		"#extension GL_OVR_multiview: enable\n",
		/* vert_global */
		"",
		/* vert_main */
		"",
		/* frag_head */
		"#extension GL_OVR_multiview2: enable\n",
		/* frag_global */
		"uniform vec3 colors[NUM_VIEWS];\n",
		/* frag_main */
		"  gl_FragColor = vec4(colors[ViewID], 1.0);\n",
	},

	/* so we know when we reach the end of the list */
	{ 0 },
};

#define SHADER_VERSION_STR "#version 130\n"

static const char *vertex_shader_multiview =
	SHADER_VERSION_STR
	"%s" /* vert_head */
	"#define NUM_VIEWS %u\n" /* num_views */
	"#define ViewID gl_ViewID_OVR\n"
	"layout (num_views = NUM_VIEWS) in;\n"
	"in vec3 inPos;\n"
	"in vec3 inCol;\n"
	"%s" /* vert_global */
	"void main()\n"
	"{\n"
	"  gl_Position = vec4(inPos, 1.0);\n"
	"  gl_Position.x = gl_Position.x + float(ViewID)*2.0/"STRINGIFY(TEX_WIDTH)";\n"
	"%s" /* vert_main */
	"}\n";

static const char *vertex_shader_normal =
	SHADER_VERSION_STR
	"%s" /* vert_head */
	"#define NUM_VIEWS %u\n" /* num_views */
	"in vec3 inPos;\n"
	"in vec3 inCol;\n"
	"uniform uint ViewID;\n"
	"%s" /* vert_global */
	"void main()\n"
	"{\n"
	"  gl_Position = vec4(inPos, 1.0);\n"
	"  gl_Position.x = gl_Position.x + float(ViewID)*2.0/"STRINGIFY(TEX_WIDTH)";\n"
	"%s" /* vert_main */
	"}\n";

static const char *fragment_shader_multiview =
	SHADER_VERSION_STR
	"%s" /* frag_head */
	"#define NUM_VIEWS %u\n" /* num_views */
	"#define ViewID gl_ViewID_OVR\n"
	"%s" /* frag_global */
	"void main()\n"
	"{\n"
	"%s" /* frag_main */
	"}\n";

static const char *fragment_shader_normal =
	SHADER_VERSION_STR
	"%s" /* frag_head */
	"#define NUM_VIEWS %u\n" /* num_views */
	"uniform uint ViewID;\n"
	"%s" /* frag_global */
	"void main()\n"
	"{\n"
	"%s" /* frag_main */
	"}\n";

/* Set up the scene rendering (multiview) shader */
static GLuint
build_scene_program(const struct ShaderModeInfo *mode,
		    bool multiview, unsigned int num_views)
{
	const char *name = multiview ? "multiview" : "normal";
	const GLchar *vert_src[1], *frag_src[1];
	GLint success;
	GLuint shader_vert, shader_frag, prog;
	GLchar info_log[512];

	GLchar vert_src_buf[1024];
	/* Inject num_views into multiview/normal shader */
	snprintf(vert_src_buf, sizeof(vert_src_buf),
		 multiview ? vertex_shader_multiview : vertex_shader_normal,
		 mode->vert_head, num_views, mode->vert_global,
		 mode->vert_main);
	vert_src[0] = vert_src_buf;

	GLchar frag_src_buf[1024];
	/* Inject num_views into multiview shader */
	snprintf(frag_src_buf, sizeof(frag_src_buf),
		 multiview ? fragment_shader_multiview : fragment_shader_normal,
		 mode->frag_head, num_views, mode->frag_global,
		 mode->frag_main);
	frag_src[0] = frag_src_buf;

	/* Vertex shader */
	shader_vert = glCreateShader(GL_VERTEX_SHADER);
	glShaderSource(shader_vert, 1, vert_src, NULL);
	glCompileShader(shader_vert);
	glGetShaderiv(shader_vert, GL_COMPILE_STATUS, &success);
	if (!success) {
		glGetShaderInfoLog(shader_vert, sizeof(info_log), NULL,
				   info_log);
		printf("%s vertex shader compilation failed: %s\n",
		       name, info_log);
		piglit_report_result(PIGLIT_FAIL);
	}

	/* Fragment shader */
	shader_frag = glCreateShader(GL_FRAGMENT_SHADER);
	glShaderSource(shader_frag, 1, frag_src, NULL);
	glCompileShader(shader_frag);
	glGetShaderiv(shader_frag, GL_COMPILE_STATUS, &success);
	if (!success) {
		glGetShaderInfoLog(shader_frag, sizeof(info_log), NULL,
				   info_log);
		printf("%s fragment shader compilation failed: %s\n",
		       name, info_log);
		piglit_report_result(PIGLIT_FAIL);
	}

	/* Shader program */
	prog = glCreateProgram();
	glBindAttribLocation(prog, 0, "inPos");
	glBindAttribLocation(prog, 1, "inCol");
	glAttachShader(prog, shader_vert);
	glAttachShader(prog, shader_frag);
	glLinkProgram(prog);
	glGetProgramiv(prog, GL_LINK_STATUS, &success);
	if (!success) {
		glGetProgramInfoLog(prog, sizeof(info_log), NULL, info_log);
		printf("%s shader link failed: %s\n", name, info_log);
		piglit_report_result(PIGLIT_FAIL);
	}

	glDeleteShader(shader_vert);
	glDeleteShader(shader_frag);

	return prog;
}

/* Render 2 triangles */
static void
render_triangles()
{
	float verts[6][3] = {
		/* x      y     z */
		{-1.0f, -1.0f, 1.0f},
		{ 0.5f, -1.0f, 0.0f},
		{-1.0f,  1.0f, 1.0f},

		{ 0.5f, -1.0f, 1.0f},
		{ 0.5f,  1.0f, 1.0f},
		{-1.0f, -1.0f, 0.0f},
	};
	float col[6][3] = {
		/* r      g     b */
		{ 1.0f,  0.0f, 0.0f},
		{ 0.0f,  1.0f, 0.0f},
		{ 0.0f,  0.0f, 1.0f},

		{ 1.0f,  0.0f, 0.0f},
		{ 0.0f,  0.0f, 1.0f},
		{ 0.0f,  1.0f, 0.0f},
	};
	glEnableVertexAttribArray(0);
	glEnableVertexAttribArray(1);
	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, &verts[0][0]);
	glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 0, &col[0][0]);
	glDrawArrays(GL_TRIANGLES, 0, 6);
	glDisableVertexAttribArray(0);
	glDisableVertexAttribArray(1);
}

static void
render_triangles_list(struct Options *opt)
{
	if (opt->lists) {
		static GLuint tri_list = 0;
		if (!tri_list) {
			tri_list = glGenLists(1);
			glNewList(tri_list, GL_COMPILE);
			render_triangles();
			glEndList();
		}
		glCallList(tri_list);
	} else {
		render_triangles();
	}
}

static void
usage(const char *arg0, enum piglit_result result)
{
	struct ShaderModeInfo *mode_info = shader_modes;

	printf("usage: %s <shader-mode> <options> [<views> [<base> [<layers>]]]\n"
	       "  <shader-mode>:   One of the following (default=basic):\n",
	       arg0);
	for (mode_info = shader_modes; mode_info->name; ++mode_info) {
		printf("                   '%s': %s\n",
		       mode_info->name, mode_info->desc);
	}
	printf("  <options>: Comma separated list of the following options:\n"
	       "                   none: No options\n"
	       "                   depth: Add a depth attachment\n"
	       "                   lists: Use display lists\n"
	       "  <views>  (=max): Number of multiview views\n"
	       "                   1 <= <number> <= GL_MAX_VIEWS_OVR\n"
	       "                   'max' = GL_MAX_VIEWS_OVR\n"
	       "  <base>   (=0):   Multiview base layer\n"
	       "                   0 <= <number> <= layers-views\n"
	       "                   'views' = views\n"
	       "                   'max' = layers - views\n"
	       "  <layers> (=min): Total number of layers\n"
	       "                   base + views <= <number> <= GL_MAX_ARRAY_TEXTURE_LAYERS\n"
	       "                   'min' = base + views\n"
	       "                   'max' = GL_MAX_ARRAY_TEXTURE_LAYERS\n");
	piglit_report_result(result);
}

void
piglit_init(int argc, char **argv)
{
	GLint max_layers = 256, max_views = 2;
	GLuint tex[2], depth[2], fbo;
	GLuint normal_prog, multiview_prog;
	GLint uniform_ViewID;
	GLint uniform_colors[2];
	GLenum fbstatus;
	bool ok = true;
	unsigned int i;
	unsigned int layer, x, y;
	int num_views;
	int num_layers;
	int first_multiview_layer = 0;
	struct ShaderModeInfo *chosen_mode = NULL;
	struct Options options = { 0 };

	piglit_require_extension("GL_OVR_multiview");

	/* argument: shader mode */
	if (argc > 1) {
		struct ShaderModeInfo *mode_info = shader_modes;
		for (mode_info = shader_modes; mode_info->name; ++mode_info) {
			if (!strcmp(mode_info->name, argv[1])) {
				chosen_mode = mode_info;
				break;
			}
		}
		if (!chosen_mode) {
			printf("unknown shader mode '%s'\n",
			       argv[1]);
			usage(argv[0], PIGLIT_FAIL);
		}
	} else {
		chosen_mode = &shader_modes[0];
	}

	if (chosen_mode->require_ovr2)
		piglit_require_extension("GL_OVR_multiview2");

	/* argument: options */
	if (argc > 2) {
		char *ptr = argv[2];
		char *comma_pos;
		while (ptr[0] != '\0') {
			comma_pos = strchr(ptr, ',');
			if (comma_pos)
				*comma_pos = '\0';
			if (!strcmp(ptr, "none")) {
			} else if (!strcmp(ptr, "depth")) {
				options.depth = true;
			} else if (!strcmp(ptr, "lists")) {
				options.lists = true;
			} else {
				printf("unknown option '%s'\n",
				       ptr);
				usage(argv[0], PIGLIT_FAIL);
			}
			if (comma_pos)
				ptr = comma_pos + 1;
			else
				break;
		}
	}

	printf("test: %s (%s)\n", chosen_mode->name, chosen_mode->desc);
	printf("options:\n"
	       "\tdepth attachment: %s\n"
	       "\tdisplay lists:    %s\n",
	       options.depth ? "YES" : "no",
	       options.lists ? "YES" : "no");

	/* get limits */
	glGetIntegerv(GL_MAX_ARRAY_TEXTURE_LAYERS, &max_layers);
	glGetIntegerv(GL_MAX_VIEWS_OVR, &max_views);
	printf("GL_MAX_ARRAY_TEXTURE_LAYERS = %d\n", max_layers);
	printf("GL_MAX_VIEWS_OVR = %d\n", max_views);

	if (!piglit_check_gl_error(GL_NO_ERROR) || max_views < 2)
		max_views = 2;

	/* argument: number of views */
	num_views = max_views;
	if (argc > 3) {
		if (!strcmp(argv[3], "max")) {
			/* already done above */
		} else {
			num_views = atoi(argv[3]);
			if (num_views <= 0) {
				printf("views %u must be > 0\n",
				       num_views);
				usage(argv[0], PIGLIT_FAIL);
			} else if (num_views > max_views) {
				printf("views %u must be < %u (GL_MAX_VIEWS_OVR)\n",
				       num_views, max_views);
				usage(argv[0], PIGLIT_SKIP);
			} else if (num_views > max_layers) {
				printf("views %u must be <= %u (GL_MAX_ARRAY_TEXTURE_LAYERS)\n",
				       num_views, max_layers);
				usage(argv[0], PIGLIT_SKIP);
			}
		}
	}
	if (num_views > max_layers)
		num_views = max_layers;
	/* Second argument: base layer */
	first_multiview_layer = 0;
	if (argc > 4) {
		if (!strcmp(argv[4], "max")) {
			first_multiview_layer = -1;
		} else if (!strcmp(argv[4], "views")) {
			first_multiview_layer = num_views;
		} else {
			first_multiview_layer = atoi(argv[4]);
			if (first_multiview_layer < 0) {
				printf("base %u must be >= 0\n",
				       first_multiview_layer);
				usage(argv[0], PIGLIT_FAIL);
			} else if (first_multiview_layer + num_views > max_layers) {
				printf("base %u must be <= %u (GL_MAX_ARRAY_TEXTURE_LAYERS-GL_MAX_VIEWS_OVR)\n",
				       first_multiview_layer,
				       max_layers - max_views);
				usage(argv[0], PIGLIT_SKIP);
			}
		}
	}
	/* Third argument: number of layers */
	if (first_multiview_layer < 0)
		num_layers = num_views;
	else
		num_layers = first_multiview_layer + num_views;
	if (argc > 5) {
		if (!strcmp(argv[5], "min")) {
			/* already done above */
		} else if (!strcmp(argv[5], "max")) {
			num_layers = max_layers;
		} else {
			if (argv[5][0] == '+') {
				if (first_multiview_layer >= 0)
					num_layers = first_multiview_layer + num_views + atoi(argv[5] + 1);
				else
					num_layers = num_views + atoi(argv[5] + 1);
			} else {
				num_layers = atoi(argv[5]);
			}
			if (first_multiview_layer >= 0 &&
			    num_layers < first_multiview_layer + num_views) {
				printf("layers %u must be >= %u (base+views)\n",
				       num_layers,
				       first_multiview_layer + num_views);
				usage(argv[0], PIGLIT_FAIL);
			} else if (num_layers > max_layers) {
				printf("layers %u must be <= %u GL_MAX_ARRAY_TEXTURE_LAYERS\n",
				       num_layers, max_layers);
				usage(argv[0], PIGLIT_SKIP);
			}
		}
	}
	if (first_multiview_layer < 0)
		first_multiview_layer = num_layers - num_views;
	if (argc > 6)
		usage(argv[0], PIGLIT_FAIL);
	printf("layers = %u\n", num_layers);
	printf("views = %u\n", num_views);
	printf("view layer range = %u..%u\n",
	       first_multiview_layer,
	       first_multiview_layer + num_views - 1);

	/* build shaders */
	normal_prog = build_scene_program(chosen_mode, false, num_views);
	multiview_prog = build_scene_program(chosen_mode, true, num_views);
	if (!piglit_check_gl_error(GL_NO_ERROR))
		piglit_report_result(PIGLIT_FAIL);

	/* get uniform locations */
	uniform_ViewID = glGetUniformLocation(normal_prog, "ViewID");
	uniform_colors[0] = glGetUniformLocation(normal_prog, "colors");
	uniform_colors[1] = glGetUniformLocation(multiview_prog, "colors");

	/*
	 * generate 2d array textures
	 * first for normal rendering
	 * second for multiview rendering
	 */
	GLsizei layer_size = TEX_WIDTH*TEX_HEIGHT;
	GLsizei normal_size = layer_size * num_views;
	GLsizei multiview_size = layer_size * num_layers;
	GLuint *normal_pixels = calloc(normal_size, sizeof(*normal_pixels));
	GLuint *multiview_pixels = calloc(multiview_size,
					  sizeof(*multiview_pixels));
	GLushort *normal_depth = NULL, *multiview_depth = NULL;

	glGenTextures(2, tex);
	glBindTexture(GL_TEXTURE_2D_ARRAY, tex[0]);
	glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, GL_RGBA8, TEX_WIDTH, TEX_HEIGHT,
		     num_views, 0, GL_RGBA, GL_UNSIGNED_INT_8_8_8_8,
		     normal_pixels);
	glBindTexture(GL_TEXTURE_2D_ARRAY, tex[1]);
	glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, GL_RGBA8, TEX_WIDTH, TEX_HEIGHT,
		     num_layers, 0, GL_RGBA, GL_UNSIGNED_INT_8_8_8_8,
		     multiview_pixels);
	if (options.depth) {
		normal_depth = calloc(normal_size, sizeof(*normal_depth));
		multiview_depth = calloc(multiview_size,
					 sizeof(*multiview_depth));
		glGenTextures(2, depth);
		glBindTexture(GL_TEXTURE_2D_ARRAY, depth[0]);
		glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, GL_DEPTH_COMPONENT16,
			     TEX_WIDTH, TEX_HEIGHT, num_views, 0,
			     GL_DEPTH_COMPONENT, GL_UNSIGNED_SHORT,
			     normal_depth);
		glBindTexture(GL_TEXTURE_2D_ARRAY, depth[1]);
		glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, GL_DEPTH_COMPONENT16,
			     TEX_WIDTH, TEX_HEIGHT, num_layers, 0,
			     GL_DEPTH_COMPONENT, GL_UNSIGNED_SHORT,
			     multiview_depth);
	}
	glBindTexture(GL_TEXTURE_2D_ARRAY, 0);

	/* generate FBO */
	glGenFramebuffers(1, &fbo);
	glBindFramebuffer(GL_FRAMEBUFFER, fbo);

	GLfloat *view_colors = malloc(3*num_views*sizeof(GLfloat));
	for (i = 0; i < num_views; ++i) {
		view_colors[i*3 + 0] = 1.0f - fmod((float)i*4/num_views, 1.0f);
		view_colors[i*3 + 1] = 1.0f - fmod((float)i*2/num_views, 1.0f);
		view_colors[i*3 + 2] = 1.0f - fmod((float)i  /num_views, 1.0f);
	}

	glClearColor(0.2f, 0.2f, 0.2f, 1.0f);
	GLbitfield clear_mask = GL_COLOR_BUFFER_BIT;
	if (options.depth) {
		clear_mask |= GL_DEPTH_BUFFER_BIT;
		glEnable(GL_DEPTH_TEST);
	}
	glViewport(0, 0, TEX_WIDTH, TEX_HEIGHT);
	glUseProgram(normal_prog);
	if (uniform_colors[0] != -1)
		glUniform3fv(uniform_colors[0], num_views, view_colors);
	for (i = 0; i < num_views; ++i) {
		/* set up FBO to normal texture layer */
		glFramebufferTextureLayer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
					  tex[0], 0, i);
		if (options.depth)
			glFramebufferTextureLayer(GL_FRAMEBUFFER,
						  GL_DEPTH_ATTACHMENT, depth[0],
						  0, i);
		fbstatus = glCheckFramebufferStatus(GL_FRAMEBUFFER);
		if (fbstatus != GL_FRAMEBUFFER_COMPLETE) {
			printf("Framebuffer (layer %u) not complete: %s\n",
			       i, piglit_get_gl_enum_name(fbstatus));
			piglit_report_result(PIGLIT_FAIL);
		}

		/* draw normal triangle */
		glClear(clear_mask);
		glUniform1ui(uniform_ViewID, i);
		render_triangles_list(&options);
	}

	if (!piglit_check_gl_error(GL_NO_ERROR))
		piglit_report_result(PIGLIT_FAIL);

	/* switch FBO to multiview layers */
	glFramebufferTextureMultiviewOVR(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
					 tex[1], 0, first_multiview_layer,
					 num_views);
	if (options.depth)
		glFramebufferTextureMultiviewOVR(GL_FRAMEBUFFER,
						 GL_DEPTH_ATTACHMENT, depth[1],
						 0, first_multiview_layer,
						 num_views);
	fbstatus = glCheckFramebufferStatus(GL_FRAMEBUFFER);
	if (fbstatus != GL_FRAMEBUFFER_COMPLETE) {
		printf("Framebuffer (multiview layers %u..%u) not complete: %s\n",
		       first_multiview_layer,
		       first_multiview_layer + num_views - 1,
		       piglit_get_gl_enum_name(fbstatus));
		piglit_report_result(PIGLIT_FAIL);
	}
	if (!piglit_check_gl_error(GL_NO_ERROR))
		piglit_report_result(PIGLIT_FAIL);

	/* draw multiview triangles */
	glUseProgram(multiview_prog);
	if (uniform_colors[1] != -1)
		glUniform3fv(uniform_colors[1], num_views, view_colors);
	free(view_colors);
	glClear(clear_mask);
	render_triangles_list(&options);
	if (!piglit_check_gl_error(GL_NO_ERROR))
		piglit_report_result(PIGLIT_FAIL);

	/* read back the pixel data */
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	/* read normal color */
	glBindTexture(GL_TEXTURE_2D_ARRAY, tex[0]);
	glGetTexImage(GL_TEXTURE_2D_ARRAY, 0, GL_RGBA, GL_UNSIGNED_INT_8_8_8_8,
		      normal_pixels);
	if (!piglit_check_gl_error(GL_NO_ERROR))
		piglit_report_result(PIGLIT_FAIL);
	/* read multiview color */
	glBindTexture(GL_TEXTURE_2D_ARRAY, tex[1]);
	glGetTexImage(GL_TEXTURE_2D_ARRAY, 0, GL_RGBA, GL_UNSIGNED_INT_8_8_8_8,
		      multiview_pixels);
	if (!piglit_check_gl_error(GL_NO_ERROR))
		piglit_report_result(PIGLIT_FAIL);
	/* read normal depth */
	if (options.depth) {
		glBindTexture(GL_TEXTURE_2D_ARRAY, depth[0]);
		glGetTexImage(GL_TEXTURE_2D_ARRAY, 0, GL_DEPTH_COMPONENT,
			      GL_UNSIGNED_SHORT, normal_depth);
		if (!piglit_check_gl_error(GL_NO_ERROR))
			piglit_report_result(PIGLIT_FAIL);
		/* read multiview depth */
		glBindTexture(GL_TEXTURE_2D_ARRAY, depth[1]);
		glGetTexImage(GL_TEXTURE_2D_ARRAY, 0, GL_DEPTH_COMPONENT,
			      GL_UNSIGNED_SHORT, multiview_depth);
		if (!piglit_check_gl_error(GL_NO_ERROR))
			piglit_report_result(PIGLIT_FAIL);
	}

	/* compare normal layers with multiview layers */
	for (layer = 0; layer < num_views; ++layer) {
		GLuint *norm_ptr = normal_pixels + layer_size*layer;
		GLuint *mv_ptr = multiview_pixels + layer_size*(first_multiview_layer + layer);
		GLushort *norm_depth_ptr = normal_depth + layer_size*layer;
		GLushort *mv_depth_ptr = multiview_depth + layer_size*(first_multiview_layer + layer);
		unsigned int layer_diff = 0;
		unsigned int layer_diff_depth = 0;
		for (y = 0; y < TEX_HEIGHT; ++y) {
			for (x = 0; x < TEX_WIDTH; ++x) {
				if (*norm_ptr != *mv_ptr) {
					++layer_diff;
					ok = false;
					printf("mismatch in view %u at (%u, %u): normal: #%08x multiview: #%08x\n",
					       layer, x, y,
					       *norm_ptr, *mv_ptr);
				}
				if (options.depth && *norm_depth_ptr != *mv_depth_ptr) {
					++layer_diff_depth;
					ok = false;
					printf("mismatch in view %u depth at (%u, %u): normal: #%04x multiview: #%04x\n",
					       layer, x, y,
					       *norm_depth_ptr, *mv_depth_ptr);
				}
				++norm_ptr;
				++mv_ptr;
				++norm_depth_ptr;
				++mv_depth_ptr;
			}
		}
		printf("view %u diff: %u/%u px (%.1f%%)\n",
		       layer,
		       layer_diff, TEX_WIDTH*TEX_HEIGHT,
		       100.0f * layer_diff / (TEX_WIDTH*TEX_HEIGHT));
		if (options.depth) {
			printf("view %u depth diff: %u/%u px (%.1f%%)\n",
			       layer,
			       layer_diff_depth, TEX_WIDTH*TEX_HEIGHT,
			       100.0f * layer_diff_depth / (TEX_WIDTH*TEX_HEIGHT));
		}
	}

	/* check other multiview layers are blank */
	for (layer = 0; layer < num_layers; ++layer) {
		if (layer < first_multiview_layer ||
		    layer >= first_multiview_layer + num_views) {
			GLuint *mv_ptr = multiview_pixels + layer_size*layer;
			GLushort *mv_depth_ptr = multiview_depth + layer_size*layer;
			unsigned int layer_diff = 0;
			unsigned int layer_diff_depth = 0;
			for (y = 0; y < TEX_HEIGHT; ++y) {
				for (x = 0; x < TEX_WIDTH; ++x) {
					if (*mv_ptr != 0x00000000) {
						++layer_diff;
						ok = false;
						printf("modified in layer %u at (%u, %u): #%08x\n",
						       layer, x, y,
						       *mv_ptr);
					}
					if (options.depth &&
					    *mv_depth_ptr != 0x0000) {
						++layer_diff_depth;
						ok = false;
						printf("depth modified in layer %u at (%u, %u): #%04x\n",
						       layer, x, y,
						       *mv_depth_ptr);
					}
					++mv_ptr;
				}
			}
			printf("layer %u changes: %u/%u px (%.1f%%)\n",
			       layer,
			       layer_diff, TEX_WIDTH*TEX_HEIGHT,
			       100.0f * layer_diff / (TEX_WIDTH*TEX_HEIGHT));
			if (options.depth) {
				printf("layer %u depth changes: %u/%u px (%.1f%%)\n",
				       layer,
				       layer_diff_depth, TEX_WIDTH*TEX_HEIGHT,
				       100.0f * layer_diff_depth / (TEX_WIDTH*TEX_HEIGHT));
			}
		}
	}

	piglit_report_result(ok ? PIGLIT_PASS : PIGLIT_FAIL);
}

enum piglit_result
piglit_display(void)
{
	/* Should never be reached */
	return PIGLIT_FAIL;
}
