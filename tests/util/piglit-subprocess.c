/*
 * Copyright © 2018 Intel Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * on the rights to use, copy, modify, merge, publish, distribute, sub
 * license, and/or sell copies of the Software, and to permit persons to whom
 * the Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT.  IN NO EVENT SHALL
 * VA LINUX SYSTEM, IBM AND/OR THEIR SUPPLIERS BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
 * USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#include "piglit-subprocess.h"

#include <stdio.h>

#ifndef _WIN32

#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <poll.h>
#include <limits.h>

static bool
stream_data(pid_t pid,
	    int to_child,
	    int from_child,
	    size_t input_size,
	    const uint8_t *input,
	    size_t *output_size,
	    uint8_t **output)
{
	bool ret = true;
	size_t buf_size = 128;

	*output = malloc(buf_size);
	*output_size = 0;

	while (true) {
		int n_pollfds = 0;
		struct pollfd pollfds[2];

		if (to_child != -1) {
			pollfds[n_pollfds].fd = to_child;
			pollfds[n_pollfds].events = POLLOUT;
			pollfds[n_pollfds].revents = 0;
			n_pollfds++;
		}

		pollfds[n_pollfds].fd = from_child;
		pollfds[n_pollfds].events = POLLIN;
		pollfds[n_pollfds].revents = 0;
		n_pollfds++;

		int res = poll(pollfds, n_pollfds, INT_MAX);

		if (res < 0) {
			if (errno == EINTR)
				continue;
			fprintf(stderr, "poll: %s\n", strerror(errno));
			ret = false;
			goto done;
		}

		for (int i = 0; i < n_pollfds; i++) {
			if (pollfds[i].revents &
			    ~(POLLIN | POLLOUT | POLLHUP)) {
				ret = false;
				goto done;
			}

			if (pollfds[i].fd == from_child &&
			    pollfds[i].revents) {
				if (buf_size - *output_size < 128) {
					buf_size *=2;
					*output = realloc(*output, buf_size);
				}
				res = read(from_child,
					   *output + *output_size,
					   buf_size - *output_size);
				if (res < 0) {
					if (errno != EINTR) {
						ret = false;
						goto done;
					}
				} else if (res == 0) {
					if (to_child != -1)
						ret = false;
					goto done;
				} else {
					*output_size += res;
				}
			} else if (pollfds[i].fd == to_child &&
				   pollfds[i].revents) {
				res = write(to_child, input, input_size);
				if (res < 0) {
					ret = false;
					goto done;
				}
				input += res;
				input_size -= res;

				if (input_size <= 0) {
					close(to_child);
					to_child = -1;
				}
			}
		}
	}

	done:
	if (to_child != -1)
		close(to_child);
	close(from_child);

	if (!ret)
		free(*output);

	return ret;
}

bool
piglit_subprocess(char * const *arguments,
		  size_t input_size,
		  const uint8_t *input,
		  size_t *output_size,
		  uint8_t **output)
{
	pid_t pid;
	int stdin_pipe[2];
	int stdout_pipe[2];

	if (pipe(stdin_pipe) == -1) {
		fprintf(stderr, "pipe: %s\n", strerror(errno));
		return false;
	}
	if (pipe(stdout_pipe) == -1) {
		fprintf(stderr, "pipe: %s\n", strerror(errno));
		close(stdin_pipe[0]);
		close(stdin_pipe[1]);
		return false;
	}

	pid = fork();

	if (pid < 0) {
		fprintf(stderr, "fork failed: %s\n", strerror(errno));
		return false;
	} else if (pid == 0) {
		dup2(stdin_pipe[0], STDIN_FILENO);
		dup2(stdout_pipe[1], STDOUT_FILENO);
		for (int i = 3; i < 256; i++)
			close(i);
		execvp(arguments[0], arguments);
		fprintf(stderr, "%s: %s\n", arguments[0], strerror(errno));
		_exit(EXIT_FAILURE);
	} else {
		close(stdin_pipe[0]);
		close(stdout_pipe[1]);

		bool ret = stream_data(pid,
				       stdin_pipe[1],
				       stdout_pipe[0],
				       input_size, input,
				       output_size, output);

		int status;
		while (waitpid(pid, &status, 0 /* options */) == -1);

		if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
			if (ret)
				free(*output);
			return false;
		}

		return ret;
	}
}

#else /* _WIN32 */

static bool
stream_data(HANDLE to_child,
	    HANDLE from_child,
	    size_t input_size,
	    const uint8_t *input,
	    size_t *output_size,
	    uint8_t **output)
{
	DWORD bytes = 0;
	if (!WriteFile(to_child, input, (DWORD)input_size, &bytes, NULL)) {
		fprintf(stderr, "failed to write to child stdin\n");
		return false;
	}
	CloseHandle(to_child);

	size_t buf_size = 128;

	*output = malloc(buf_size);
	*output_size = 0;

	while (true) {
		if (buf_size - *output_size < 128) {
			buf_size *=2;
			*output = realloc(*output, buf_size);
		}
		if (ReadFile(from_child, *output + *output_size, buf_size - *output_size, &bytes, NULL) && bytes) {
			*output_size += bytes;
		} else {
			CloseHandle(from_child);
			return true;
		}
	}
}

bool
piglit_subprocess(char * const *arguments,
		  size_t input_size,
		  const uint8_t *input,
		  size_t *output_size,
		  uint8_t **output)
{
	char *app_name = arguments[0];
	char cmdline[1024];
	char *append_loc = cmdline;
	for (char * const *arg = arguments; *arg != NULL; ++arg) {
		bool has_space = strchr(*arg, ' ') != NULL;
		bool has_quotes = strchr(*arg, '"') != NULL;
		int ret = sprintf(append_loc,
				  has_space && !has_quotes ? "%s\"%s\"" : "%s%s",
				  arg == arguments ? "" : " ",
				  *arg);
		if (ret < 0) {
			fprintf(stderr, "failed to construct command line\n");
			return false;
		}
		append_loc += ret;
	}

	HANDLE stdin_read = NULL;
	HANDLE stdin_write = NULL;
	HANDLE stdout_read = NULL;
	HANDLE stdout_write = NULL;

	SECURITY_ATTRIBUTES sattr;
	sattr.nLength = sizeof(SECURITY_ATTRIBUTES);
	sattr.bInheritHandle = TRUE;
	sattr.lpSecurityDescriptor = NULL;
	bool success = false;

	PROCESS_INFORMATION pinfo = { 0 };
	STARTUPINFO startup_info = { 0 };
	startup_info.cb = sizeof(STARTUPINFO);

	if (!CreatePipe(&stdin_read, &stdin_write, &sattr, 0) ||
	    !CreatePipe(&stdout_read, &stdout_write, &sattr, 0) ||
	    /* Child process doesn't need to write to stdin or read from stdout */
	    !SetHandleInformation(stdin_write, HANDLE_FLAG_INHERIT, 0) ||
	    !SetHandleInformation(stdout_read, HANDLE_FLAG_INHERIT, 0)) {
		fprintf(stderr, "CreatePipe or SetHandleInformation failed\n");
		goto cleanup;
	}

	startup_info.hStdInput = stdin_read;
	startup_info.hStdOutput = stdout_write;
	startup_info.hStdError = NULL;
	startup_info.dwFlags = STARTF_USESTDHANDLES;
	if (!CreateProcessA(
		app_name,
		cmdline,
		NULL, /* Security attributes */
		NULL, /* Thread attributes */
		TRUE, /* Inherit handles */
		0, /* Creation flags */
		NULL, /* Environment (use parent) */
		NULL, /* Working directory (use parent) */
		&startup_info,
		&pinfo
	)) {
		fprintf(stderr, "CreateProcess failed\n");
		goto cleanup;
	}

	/* Close unneeded handles */
	CloseHandle(pinfo.hThread);
	CloseHandle(stdin_read);
	stdin_read = NULL;
	CloseHandle(stdout_write);
	stdout_write = NULL;

	success = stream_data(stdin_write, stdout_read, input_size, input, output_size, output);
	stdin_write = NULL;
	stdout_read = NULL;
	WaitForSingleObject(pinfo.hProcess, INFINITE);
	DWORD exit_code;
	if (!GetExitCodeProcess(pinfo.hProcess, &exit_code) || exit_code != 0) {
		if (success)
			free(*output);
		success = false;
	}

cleanup:
	if (stdin_read)
		CloseHandle(stdin_read);
	if (stdin_write)
		CloseHandle(stdin_write);
	if (stdout_read)
		CloseHandle(stdout_read);
	if (stdout_write)
		CloseHandle(stdout_write);
	if (pinfo.hProcess)
		CloseHandle(pinfo.hProcess);
	return success;
}

#endif /* _WIN32 */
