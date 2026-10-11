/* SPDX-License-Identifier: MIT */

#ifndef ARB_SYNC_WAIT_COMMON_H
#define ARB_SYNC_WAIT_COMMON_H

#include "piglit-util-gl.h"

#define ONE_MS 1000000ull
#define ONE_SECOND 1000000000ull

void wait_workload_init(void);
double wait_workload_calibrate(double target_ms);
void wait_workload_draw(unsigned draws, bool flush_each);
void wait_workload_set_color(const float *color);
GLuint wait_workload_texture(void);
double wait_cpu_time_ms(void);
GLint64 wait_timestamp_now(void);
GLuint wait_timestamp_queue(void);
GLint64 wait_timestamp_result(GLuint query);
bool wait_check_completed(GLuint query, const char *what, unsigned iteration);
void wait_verify_workload(const char *what);

#endif
