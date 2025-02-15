/*
 * Copyright © 2025 Collabora, Ltd.
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
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Author:
 *    Faith Ekstrand <faith.ekstrand@collabora.com>
 */

/* This is a port of crucible func.sync.semaphore-fd to use GL interop */

#include <piglit-util-gl.h>
#include "interop.h"
#include "params.h"
#include "helpers.h"

PIGLIT_GL_TEST_CONFIG_BEGIN

config.supports_gl_es_version = 31;
config.supports_gl_core_version = 42;
config.khr_no_error_support = PIGLIT_HAS_ERRORS;

PIGLIT_GL_TEST_CONFIG_END

/* This is odd so we start and end on the same queue */
#define NUM_HASH_ITERATIONS 255

#define LOCAL_WORKGROUP_SIZE 128
#define GLOBAL_WORKGROUP_SIZE 128

static const char cs[] =
	"#version 310 es\n"
	"layout(binding = 0, std430) buffer Storage {\n"
	"    ivec2 data[];\n"
	"} ssbo;\n"
	"layout (local_size_x = 128) in;\n"
	"void main()\n"
	"{\n"
	"    ivec2 data = ssbo.data[gl_LocalInvocationID.x];\n"
	"    data.y = data.y ^ data.x;\n"
	"    data.y = data.y * 0x01000193 + 0x0071f80c;\n"
	"    ssbo.data[gl_LocalInvocationID.x].y = data.y;\n"
	"}\n";

static void
cpu_process_data(uint32_t *data)
{
	for (unsigned k = 0; k < LOCAL_WORKGROUP_SIZE; k++) {
		uint32_t *x = &data[k * 2 + 0];
		uint32_t *y = &data[k * 2 + 1];
		for (unsigned i = 0; i < NUM_HASH_ITERATIONS; i++) {
			for (unsigned j = 0; j < GLOBAL_WORKGROUP_SIZE; j++) {
				if ((i & 1) == 0) {
					*x = (*x ^ *y) * 0x01000193 + 0x0050230f;
				} else {
					*y = (*y ^ *x) * 0x01000193 + 0x0071f80c;
				}
			}
		}
	}
}

static enum piglit_result
run_test(bool single_sem);

static bool
vk_init(void);

static void
vk_cleanup(void);

static bool
gl_init();

static void
gl_cleanup(void);

static void
cleanup(void);

static struct vk_ctx vk_core;
static struct vk_buf vk_bo;
struct vk_compute_pipeline vk_pipeline;
VkSemaphore vk_timeline;
PFN_vkSignalSemaphoreKHR _vkSignalSemaphoreKHR;

static GLuint gl_bo;
static GLint gl_prog;
static GLuint gl_mem_obj;
static VkBufferUsageFlagBits vk_bo_usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;

static struct gl_ext_semaphores gl_sem;
static struct vk_semaphores vk_sem;

void piglit_init(int argc, char **argv)
{
	piglit_require_extension("GL_ARB_compute_shader");
	piglit_require_extension("GL_ARB_shader_storage_buffer_object");
	piglit_require_extension("GL_EXT_memory_object");
	piglit_require_extension("GL_EXT_memory_object_fd");
	piglit_require_extension("GL_EXT_semaphore");
	piglit_require_extension("GL_EXT_semaphore_fd");

	atexit(cleanup);

	int single_sem = -1;
	for (int a = 1; a < argc; a++) {
		if (!strcmp(argv[a], "-single-sem"))
			single_sem = 1;
		else if (!strcmp(argv[a], "-multi-sem"))
			single_sem = 0;
	}

	if (!vk_init()) {
		fprintf(stderr, "Failed to initialize Vulkan, skipping the test.\n");
		piglit_report_result(PIGLIT_SKIP);
	}

	/* create memory object and gl buffer */
	if (!gl_create_mem_obj_from_vk_mem(&vk_core, &vk_bo.mobj, &gl_mem_obj)) {
		fprintf(stderr, "Failed to create GL memory object from Vulkan memory.\n");
		piglit_report_result(PIGLIT_FAIL);
	}

	if (!gl_gen_buf_from_mem_obj(gl_mem_obj, GL_SHADER_STORAGE_BUFFER,
				     vk_bo.mobj.mem_sz, 0, &gl_bo)) {
		fprintf(stderr, "Failed to create buffer from GL memory object.\n");
		piglit_report_result(PIGLIT_FAIL);
	}

	if (!gl_create_semaphores_from_vk(&vk_core, &vk_sem, &gl_sem)) {
		fprintf(stderr, "Failed to import semaphores from Vulkan.\n");
		piglit_report_result(PIGLIT_FAIL);
	}

	if (!gl_init()) {
		fprintf(stderr, "Failed to initialize structs for GL rendering.\n");
		piglit_report_result(PIGLIT_FAIL);
	}

	for (int s = 0; s < 2; s++) {
		if (single_sem >= 0 && single_sem != s)
			continue;

		enum piglit_result res = run_test(s);
		if (res != PIGLIT_PASS)
			piglit_report_result(res);
	}

	piglit_report_result(PIGLIT_PASS);
}

enum piglit_result
piglit_display(void)
{
	return PIGLIT_PASS;
}

static enum piglit_result
run_test(bool single_sem)
{
	/* First, set up the CPU pointer */
	uint32_t cpu_data[LOCAL_WORKGROUP_SIZE * 2];
	for (unsigned i = 0; i < LOCAL_WORKGROUP_SIZE; i++) {
		cpu_data[i * 2 + 0] = i * 37;
		cpu_data[i * 2 + 1] = 0;
	}

	vk_update_buffer_data(&vk_core, cpu_data, sizeof(cpu_data), &vk_bo);

	cpu_process_data(cpu_data);

	glUseProgram(gl_prog);
	glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, gl_bo);

	for (unsigned i = 0; i < NUM_HASH_ITERATIONS; i++) {
		if ((i & 1) == 0) {
			VkSubmitInfo submit_info;
			memset(&submit_info, 0, sizeof submit_info);
			submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
			submit_info.commandBufferCount = 1;
			submit_info.pCommandBuffers = &vk_core.cmd_buf;

			const VkPipelineStageFlagBits stage_flags =
				VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
			const uint64_t wait_value = 7;
			VkTimelineSemaphoreSubmitInfo timeline_info;

			if (i != 0) {
				submit_info.pWaitDstStageMask = &stage_flags;
				submit_info.waitSemaphoreCount = 1;
				submit_info.pWaitSemaphores = &vk_sem.gl_frame_done;
			} else if (vk_timeline != VK_NULL_HANDLE) {
				/* Do a little wait-before-signal on the first
				 * iteration to wake up the driver's submit
				 * thread.
				 */
				submit_info.pWaitDstStageMask = &stage_flags;
				submit_info.waitSemaphoreCount = 1;
				submit_info.pWaitSemaphores = &vk_timeline;

				memset(&timeline_info, 0, sizeof timeline_info);
				timeline_info.sType = VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO;
				timeline_info.waitSemaphoreValueCount = 1;
				timeline_info.pWaitSemaphoreValues = &wait_value;
				submit_info.pNext = &timeline_info;
			}

			if (i != NUM_HASH_ITERATIONS - 1) {
				submit_info.signalSemaphoreCount = 1;
				submit_info.pSignalSemaphores =
					single_sem ? &vk_sem.gl_frame_done
						   : &vk_sem.vk_frame_ready;
			}

			if (vkQueueSubmit(vk_core.queue, 1, &submit_info,
					  VK_NULL_HANDLE) != VK_SUCCESS) {
				fprintf(stderr, "Submit failed");
				return PIGLIT_FAIL;
			}

			if (i == 0 && vk_timeline != VK_NULL_HANDLE) {
				VkSemaphoreSignalInfo signal_info;
				memset(&signal_info, 0, sizeof signal_info);
				signal_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SIGNAL_INFO;
				signal_info.semaphore = vk_timeline;
				signal_info.value = wait_value;

				_vkSignalSemaphoreKHR(vk_core.dev, &signal_info);
			}
		} else {
			assert(i != 0);
			assert(i != NUM_HASH_ITERATIONS - 1);
			glWaitSemaphoreEXT(single_sem ? gl_sem.gl_frame_ready
						      : gl_sem.vk_frame_done,
					   1, &gl_bo,
					   0, NULL, NULL);

			for (unsigned j = 0; j < GLOBAL_WORKGROUP_SIZE; j++) {
				glDispatchCompute(1, 1, 1);
				glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
			}

			glSignalSemaphoreEXT(gl_sem.gl_frame_ready,
					     1, &gl_bo,
					     0, NULL, NULL);
		}
	}

	vkQueueWaitIdle(vk_core.queue);

	uint32_t gpu_data[LOCAL_WORKGROUP_SIZE * 2];
	vk_get_buffer_data(&vk_core, gpu_data, sizeof(gpu_data), &vk_bo);

	if (memcmp(cpu_data, gpu_data, sizeof(gpu_data))) {
		fprintf(stderr, "Data mismatch!\n");
		return PIGLIT_FAIL;
	}

	return PIGLIT_PASS;
}

static bool
vk_init()
{
	char *cs_src = NULL;
	unsigned int cs_size = 0;

	if (!vk_init_ctx_for_rendering(&vk_core)) {
		fprintf(stderr, "Failed to create Vulkan context.\n");
		return false;
	}

	if (!vk_check_gl_compatibility(&vk_core)) {
		fprintf(stderr, "Mismatch in driver/device UUID\n");
		return false;
	}

	if (!vk_create_semaphores(&vk_core, &vk_sem)) {
		fprintf(stderr, "Failed to create Vulkan semaphores.\n");
		goto fail;
	}

	if (vk_device_extension_supported(&vk_core, "VK_KHR_timeline_semaphore")) {
		_vkSignalSemaphoreKHR = (PFN_vkSignalSemaphoreKHR)
			vkGetDeviceProcAddr(vk_core.dev, "vkSignalSemaphoreKHR");

		VkSemaphoreTypeCreateInfo sema_type_info;
		memset(&sema_type_info, 0, sizeof sema_type_info);
		sema_type_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO;
		sema_type_info.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
		sema_type_info.initialValue = 0;

		VkSemaphoreCreateInfo sema_info;
		memset(&sema_info, 0, sizeof sema_info);
		sema_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
		sema_info.pNext = &sema_type_info;
		if (vkCreateSemaphore(vk_core.dev, &sema_info, 0, &vk_timeline) != VK_SUCCESS) {
			fprintf(stderr, "Failed to create timeline semaphore.\n");
			goto fail;
		}
	}

	if (!vk_create_ext_buffer(&vk_core, 1024 * 2 * sizeof(uint32_t), vk_bo_usage, &vk_bo)) {
		fprintf(stderr, "Failed to create Vulkan buffer.\n");
		goto fail;
	}

	if (!(cs_src = load_shader(VK_PING_PONG_COMP, &cs_size))) {
		fprintf(stderr, "Failed to load compute shader.\n");
		goto fail;
	}

	struct vk_buf_att bo_att;
	bo_att.buf = vk_bo;
	bo_att.offset = 0;
	bo_att.range = vk_bo.mobj.mem_sz;

	struct vk_descriptor descriptor;
	descriptor.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
	descriptor.buf_att = &bo_att;

	if (!vk_create_compute_pipeline(&vk_core, cs_src, cs_size,
					&descriptor, 1, &vk_pipeline))
		return false;

	VkCommandBufferBeginInfo cmd_begin_info;
	memset(&cmd_begin_info, 0, sizeof cmd_begin_info);
	cmd_begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	cmd_begin_info.flags = VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT;
	vkBeginCommandBuffer(vk_core.cmd_buf, &cmd_begin_info);

        VkBufferMemoryBarrier barrier;
	memset(&barrier, 0, sizeof barrier);
	barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
	barrier.buffer = vk_bo.buf;
	barrier.offset = 0;
	barrier.size = VK_WHOLE_SIZE,

	barrier.srcAccessMask = 0;
	barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT |
				VK_ACCESS_SHADER_WRITE_BIT;
	barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_EXTERNAL_KHR;
	barrier.dstQueueFamilyIndex = vk_core.qfam_idx;

	vkCmdPipelineBarrier(vk_core.cmd_buf,
			     VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
			     VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
			     0 /* flags */,
			     0, NULL,
			     1, &barrier,
			     0, NULL);

	vkCmdBindPipeline(vk_core.cmd_buf, VK_PIPELINE_BIND_POINT_COMPUTE,
			  vk_pipeline.pipeline);
	vkCmdBindDescriptorSets(vk_core.cmd_buf, VK_PIPELINE_BIND_POINT_COMPUTE,
				vk_pipeline.pipeline_layout, 0,
				1, &vk_pipeline.descriptor_set, 0, NULL);

	for (unsigned j = 0; j < GLOBAL_WORKGROUP_SIZE; j++) {
		vkCmdDispatch(vk_core.cmd_buf, 1, 1, 1);

		barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT |
					VK_ACCESS_SHADER_WRITE_BIT;
		barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT |
					VK_ACCESS_SHADER_WRITE_BIT;
		barrier.dstQueueFamilyIndex = vk_core.qfam_idx;
		barrier.srcQueueFamilyIndex = vk_core.qfam_idx;

		vkCmdPipelineBarrier(vk_core.cmd_buf,
				     VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
				     VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
				     0 /* flags */,
				     0, NULL,
				     1, &barrier,
				     0, NULL);
	}

	barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT |
				VK_ACCESS_SHADER_WRITE_BIT;
	barrier.dstAccessMask = 0;
	barrier.srcQueueFamilyIndex = vk_core.qfam_idx;
	barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_EXTERNAL_KHR;

	vkCmdPipelineBarrier(vk_core.cmd_buf,
			     VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
			     VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
			     0 /* flags */,
			     0, NULL,
			     1, &barrier,
			     0, NULL);

	vkEndCommandBuffer(vk_core.cmd_buf);

	free(cs_src);

	return true;

fail:
	free(cs_src);
	return false;
}

static bool
gl_init()
{
	gl_prog = piglit_build_compute_program(cs);
	return glGetError() == GL_NO_ERROR;
}

static void
vk_cleanup(void)
{
	vk_destroy_compute_pipeline(&vk_core, &vk_pipeline);
	vk_destroy_buffer(&vk_core, &vk_bo);
	if (vk_timeline != VK_NULL_HANDLE)
		vkDestroySemaphore(vk_core.dev, vk_timeline, NULL);
	vk_destroy_semaphores(&vk_core, &vk_sem);
	vk_cleanup_ctx(&vk_core);
}

static void
gl_cleanup(void)
{
	glDeleteProgram(gl_prog);
	glDeleteMemoryObjectsEXT(1, &gl_mem_obj);
	glDeleteBuffers(1, &gl_bo);
}

static void
cleanup(void)
{
	gl_cleanup();
	vk_cleanup();
}
