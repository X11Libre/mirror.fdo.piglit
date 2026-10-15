/*
 * SPDX-License-Identifier: MIT
 */

/*
 * Overwrites a buffer through a blocking write map, writes it with a kernel,
 * and reads it back through a blocking read map, several times in a row.
 * The read map must see the kernel's writes.
 *
 * This is the sequence of the CL CTS test
 * multiple_device_context/context_four_contexts_same_device.
 */

#include "piglit-framework-cl-custom.h"

PIGLIT_CL_CUSTOM_TEST_CONFIG_BEGIN

	config.name = "Map write, kernel, map read";
	config.run_per_device = true;

PIGLIT_CL_CUSTOM_TEST_CONFIG_END

#define SIZE 512
#define ITERATIONS 4
#define FILL 0xdeaddeadu

static const char *source =
	"kernel void test(global uint *src, global uint *dst, uint mul)\n"
	"{\n"
	"	size_t i = get_global_id(0);\n"
	"	dst[i] = src[i] * mul;\n"
	"}\n";

/* fill through a host map rather than clEnqueueFillBuffer, since a GPU fill
 * hides the missing synchronization this test was written for on zink
 */
static bool
fill(cl_command_queue queue, cl_mem buf, const cl_uint *data)
{
	cl_int err;
	cl_uint *map = clEnqueueMapBuffer(queue, buf, CL_TRUE, CL_MAP_WRITE, 0,
					  SIZE * sizeof(cl_uint), 0, NULL, NULL,
					  &err);
	if (!piglit_cl_check_error(err, CL_SUCCESS))
		return false;
	for (unsigned i = 0; i < SIZE; i++)
		map[i] = data ? data[i] : FILL;
	err = clEnqueueUnmapMemObject(queue, buf, map, 0, NULL, NULL);
	return piglit_cl_check_error(err, CL_SUCCESS);
}

enum piglit_result
piglit_cl_test(const int argc,
	       const char **argv,
	       const struct piglit_cl_custom_test_config *config,
	       const struct piglit_cl_custom_test_env *env)
{
	enum piglit_result result = PIGLIT_PASS;
	cl_uint src[SIZE];
	cl_int err;

	for (unsigned i = 0; i < SIZE; i++)
		src[i] = i * 7 + 1;

	piglit_cl_context ctx = piglit_cl_create_context(env->platform_id,
							 &env->device_id, 1);
	if (!ctx)
		return PIGLIT_FAIL;
	cl_command_queue queue = ctx->command_queues[0];
	cl_program program = piglit_cl_build_program_with_source(ctx, 1,
								 (char **)&source, "");
	cl_kernel kernel = program ? piglit_cl_create_kernel(program, "test") : NULL;
	cl_mem src_buf = piglit_cl_create_buffer(ctx, CL_MEM_READ_WRITE, sizeof(src));
	cl_mem dst_buf = piglit_cl_create_buffer(ctx, CL_MEM_READ_WRITE, sizeof(src));
	if (!kernel || !src_buf || !dst_buf || !fill(queue, src_buf, src)) {
		result = PIGLIT_FAIL;
		goto out;
	}

	for (unsigned iter = 0; iter < ITERATIONS && result == PIGLIT_PASS; iter++) {
		const cl_uint mul = iter + 2;
		const size_t global_size = SIZE;

		if (!fill(queue, dst_buf, NULL) ||
		    !piglit_cl_set_kernel_buffer_arg(kernel, 0, &src_buf) ||
		    !piglit_cl_set_kernel_buffer_arg(kernel, 1, &dst_buf) ||
		    !piglit_cl_set_kernel_arg(kernel, 2, sizeof(mul), &mul) ||
		    !piglit_cl_enqueue_ND_range_kernel(queue, kernel, 1, NULL,
						       &global_size, NULL, NULL)) {
			result = PIGLIT_FAIL;
			break;
		}

		cl_uint *map = clEnqueueMapBuffer(queue, dst_buf, CL_TRUE, CL_MAP_READ,
						  0, sizeof(src), 0, NULL, NULL, &err);
		if (!piglit_cl_check_error(err, CL_SUCCESS)) {
			result = PIGLIT_FAIL;
			break;
		}
		for (unsigned i = 0; i < SIZE; i++) {
			if (map[i] != src[i] * mul) {
				fprintf(stderr, "iteration %u: dst[%u] = 0x%08x, expected 0x%08x\n",
					iter, i, map[i], src[i] * mul);
				result = PIGLIT_FAIL;
				break;
			}
		}
		clEnqueueUnmapMemObject(queue, dst_buf, map, 0, NULL, NULL);
	}

	clFinish(queue);
out:
	if (dst_buf)
		clReleaseMemObject(dst_buf);
	if (src_buf)
		clReleaseMemObject(src_buf);
	if (kernel)
		clReleaseKernel(kernel);
	if (program)
		clReleaseProgram(program);
	piglit_cl_release_context(ctx);
	return result;
}
