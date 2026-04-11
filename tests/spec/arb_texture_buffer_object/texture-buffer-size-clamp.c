/*
 * Copyright © 2020, 2021, 2022 Intel Corporation
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

#include <inttypes.h>
#include "piglit-util-gl.h"

/**
 * @file texture-buffer-size-clamp.c
 *
 * Tests that texture buffer's size cannot exceed
 * GL_MAX_TEXTURE_BUFFER_SIZE_ARB even if underlying buffer allows it
 * to be bigger. In other words the size of texture buffer must be clamped
 * to the GL_MAX_TEXTURE_BUFFER_SIZE_ARB by the driver.
 */

static struct piglit_gl_test_config *piglit_config = NULL;

static enum piglit_result test_buffer(void *param);

struct test_parameter
{
	int32_t texel_byte_size;
	GLuint program, texel_format;
	GLuint buffer_id, texture_id;

	enum piglit_result (*test_func)(struct test_parameter *self,
					GLsizeiptr texel_count);
};

static const struct texture_format
{
	/** Size format. */
	GLint size;

	/** Format enum. */
	GLenum format;

} texture_formats[] = {
	{sizeof(uint8_t),     GL_R8UI},
	{sizeof(uint8_t) * 2, GL_RG8UI},
	{sizeof(uint8_t) * 4, GL_RGBA8UI}
};

static struct test_parameter test_parameters[2 * ARRAY_SIZE(texture_formats)];

static struct piglit_subtest
	subtests_config[1 + ARRAY_SIZE(test_parameters)] = {
	{"r8ui_texture_buffer_size_via_sampler",
	 "sampler-r8ui",
	 test_buffer},
	{"r8ui_texture_buffer_size_via_image",
	 "image-r8ui",
	 test_buffer},
	{"rg8ui_texture_buffer_size_via_sampler",
	 "sampler-rg8ui",
	 test_buffer},
	{"rg8ui_texture_buffer_size_via_image",
	 "image-rg8ui",
	 test_buffer},
	{"rgba8ui_texture_buffer_size_via_sampler",
	 "sampler-rgba8ui",
	 test_buffer},
	{"rgba8ui_texture_buffer_size_via_image",
	 "image-rgba8ui",
	 test_buffer}
};

PIGLIT_GL_TEST_CONFIG_BEGIN
	config.supports_gl_core_version = 31;
	config.window_visual =
		PIGLIT_GL_VISUAL_DOUBLE | PIGLIT_GL_VISUAL_RGBA;
	config.khr_no_error_support = PIGLIT_NO_ERRORS;
	config.subtests = subtests_config;
	piglit_config = &config;
PIGLIT_GL_TEST_CONFIG_END

static GLint
get_max_texture_buffer_size()
{
	static GLint max_size = 0;
	if (!max_size)
		glGetIntegerv(GL_MAX_TEXTURE_BUFFER_SIZE_ARB, &max_size);
	return max_size;
}

enum piglit_result
piglit_display()
{
	return PIGLIT_FAIL; /* UNREACHED */
}

static enum piglit_result
test_sampler_buffer_internal(struct test_parameter *parameters,
			     GLsizeiptr texel_count)
{
	glUseProgram(parameters->program);

	glTexBuffer(GL_TEXTURE_BUFFER, parameters->texel_format,
		    parameters->buffer_id);
	glBufferData(GL_TEXTURE_BUFFER,
		     texel_count * parameters->texel_byte_size,
		     NULL, GL_STREAM_COPY);

	GLuint error = glGetError();
	if (error == GL_OUT_OF_MEMORY)
		return PIGLIT_SKIP;

	glUniform1i(glGetUniformLocation(parameters->program, "size"),
		    texel_count);
	glUniform1i(glGetUniformLocation(parameters->program, "maxSize"),
		    get_max_texture_buffer_size());

	glClearColor(0.2, 0.2, 0.2, 0.2);
	glClear(GL_COLOR_BUFFER_BIT);

	piglit_draw_rect(-1, -1, 2, 2);

	static const float green[4] = {0, 1, 0, 1};
	bool pass;
	pass = piglit_probe_rect_rgba(0, 0, piglit_width,
				      piglit_height, green);

	piglit_present_results();
	return pass ? PIGLIT_PASS : PIGLIT_FAIL;
}

static enum piglit_result
test_image_buffer_internal(struct test_parameter *parameters,
			   GLsizeiptr texel_count)
{
	if (!piglit_is_extension_supported("GL_ARB_compute_shader")) {
		return PIGLIT_SKIP;
	}

	glUseProgram(parameters->program);

	glTexBuffer(GL_TEXTURE_BUFFER, parameters->texel_format,
		    parameters->buffer_id);
	glBufferData(GL_TEXTURE_BUFFER,
		     texel_count * parameters->texel_byte_size,
		     NULL, GL_STREAM_COPY);
	GLuint error = glGetError();
	if (error == GL_OUT_OF_MEMORY)
		return PIGLIT_SKIP;

	glBindImageTexture(0, parameters->texture_id, 0, GL_FALSE,
			   0, GL_READ_ONLY, parameters->texel_format);

	GLuint ssbo;
	glGenBuffers(1, &ssbo);
	glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, ssbo);
	glBufferData(GL_SHADER_STORAGE_BUFFER, sizeof(uint32_t),
		     NULL, GL_STREAM_READ);
	glBindBuffer(GL_SHADER_STORAGE_BUFFER, ssbo);

	glDispatchCompute(1,1,1);

	glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);

	int32_t *result = (int32_t *) glMapBuffer(GL_SHADER_STORAGE_BUFFER,
						  GL_READ_ONLY);
	int32_t result_value = *result;

	glUnmapBuffer(GL_SHADER_STORAGE_BUFFER);
	glDeleteBuffers(1, &ssbo);

	size_t expected_value = MIN2(texel_count,
				     get_max_texture_buffer_size());
	return (result_value == expected_value) ?  PIGLIT_PASS : PIGLIT_FAIL;
}

static enum piglit_result
test_buffer(void *param)
{
	struct test_parameter *parameter = param;

	int32_t offsets[] =  {-1, 0, 1};
	int32_t max_size = get_max_texture_buffer_size();
	enum piglit_result pass = PIGLIT_PASS;

	for (size_t i = 0; i < ARRAY_SIZE(offsets); i++) {
		GLsizeiptr texel_count = (int64_t)max_size + offsets[i];

		enum piglit_result ret = parameter->test_func(parameter,
							      texel_count);

		if (ret == PIGLIT_FAIL) {
			piglit_loge("Wrong shader texel count result, "
				    "max buffer texture size %d, "
				    "texture buffer size: %" PRIdPTR,
				    max_size, (intptr_t)texel_count);
			return ret;
		} else if (ret == PIGLIT_SKIP) {
			return ret;
		}
	}

	return pass;
}

static void
generate_parameters_data(GLuint prog_sampler, GLuint prog_image,
			 GLuint buf_id, GLuint tex_id)
{
	size_t subtests_count = ARRAY_SIZE(test_parameters);
	for (size_t i = 0; i < subtests_count; i++) {
		const size_t format_idx = i / 2;
		const bool is_image = i % 2;

		struct test_parameter *parameter = &test_parameters[i];
		struct piglit_subtest *subtest = &subtests_config[i];

		parameter->buffer_id = buf_id;
		parameter->texture_id = tex_id;
		parameter->program = is_image ? prog_image : prog_sampler;
		parameter->texel_format = texture_formats[format_idx].format;
		parameter->texel_byte_size = texture_formats[format_idx].size;
		parameter->test_func = is_image ? test_image_buffer_internal :
					test_sampler_buffer_internal;

		subtest->data = parameter;
	}
}

void
piglit_init(int argc, char **argv)
{
	static const char vert[] = "#version 140\n"
	"in vec4 piglit_vertex;\n"
	"void  main() {gl_Position = piglit_vertex;}";

	static const char frag[]  = "#version 140\n"
	"uniform usamplerBuffer s;\n"
	"uniform int size, maxSize;\n"
	"void main()\n"
	"{\n"
	"	if (textureSize(s) != min(size, maxSize))\n"
	"		gl_FragColor = vec4(1, 0, 0, 1);\n"
	"	else\n"
	"		gl_FragColor = vec4(0, 1, 0, 1);\n"
	"}";

	static const char comp[] = "#version 420\n"
	"#extension GL_ARB_compute_shader: enable\n"
	"#extension GL_ARB_shader_storage_buffer_object: require\n"
	"#extension GL_ARB_shader_image_size: require\n"
	"layout(local_size_x = 1) in;\n"
	"writeonly readonly uniform uimageBuffer s;\n"
	"layout(std430, binding=0) writeonly buffer SZ\n"
	"{\n"
	"	int size;\n"
	"};\n"
	"void main()\n"
	"{\n"
	"	size = imageSize(s);\n"
	"}";

	GLuint prog_sampler = piglit_build_simple_program(vert, frag);

	GLuint prog_image = 0;

	bool es;
	int major, minor;
	piglit_get_glsl_version(&es, &major, &minor);
	if (piglit_is_extension_supported("GL_ARB_compute_shader") && major * 100 + minor >= 420) {
		GLuint shader =
			piglit_compile_shader_text(GL_COMPUTE_SHADER, comp);

		/* Creating the openGL program */
		prog_image = glCreateProgram();
		if (prog_image == 0) {
			fprintf(stderr,
				"Couldn't create the shader program.\n");
			glDeleteShader(shader);
			piglit_report_result(PIGLIT_FAIL);
		}

		/* Attaching the shaders to the program */
		glAttachShader(prog_image, shader);

		/* Linking the program */
		glLinkProgram(prog_image);

		glDeleteShader(shader);

		if (!piglit_link_check_status(prog_image)) {
			/* Details of the error have already been printed. */
			glDeleteProgram(prog_image);
			piglit_report_result(PIGLIT_FAIL);
		}
	}

	GLuint buf_id, tex_id;

	glGenBuffers(1, &buf_id);
	glBindBuffer(GL_TEXTURE_BUFFER, buf_id);

	glGenTextures(1, &tex_id);
	glBindTexture(GL_TEXTURE_BUFFER, tex_id);

	generate_parameters_data(prog_sampler, prog_image, buf_id, tex_id);

	enum piglit_result result = piglit_run_selected_subtests(
		piglit_config->subtests,
		piglit_config->selected_subtests,
		piglit_config->num_selected_subtests,
		PIGLIT_SKIP);

	glDeleteBuffers(1, &buf_id);
	glDeleteTextures(1, &tex_id);
	glDeleteProgram(prog_sampler);
	glDeleteProgram(prog_image);

	piglit_report_result(result);
}
