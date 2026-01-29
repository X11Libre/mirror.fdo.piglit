# coding=utf-8
#
# Copyright © Collabora Ltd.
# SPDX-License-Identifier: MIT


""" Module providing an ANGLE dump backend for replayer """

from os import chdir, path, rename
from typing import List, Union

from framework import core, exceptions

from .abstract import DumpBackend, dump_handler
from .register import Registry

__all__ = [
    'REGISTRY',
    'ANGLETraceBackend',
]


class ANGLETraceBackend(DumpBackend):
    """ replayer's ANGLE dump backend

    This backend uses ANGLE for replaying its traces.
    """

    _get_last_frame_call = None  # this silences the abstract-not-subclassed warning

    def __init__(self, trace_path: str, output_dir: Union[str, None] = None,
                 calls: Union[List[str], None] = None, **kwargs: str) -> None:
        super().__init__(trace_path, output_dir, calls, **kwargs)
        extension: str = path.splitext(self._trace_path)[1]

        if extension == '.so':
            angle_bin: str = core.get_option('PIGLIT_REPLAY_ANGLE_BINARY',
                                             ('replay', 'angle_bin'),
                                             default='./angle_trace_tests')
            self._retrace_cmd = [angle_bin]
        else:
            raise exceptions.PiglitFatalError(
                f'Invalid trace_path: "{self._trace_path}" tried to be dumped '
                'by the ANGLETraceBackend.\n')

    @dump_handler
    def dump(self):
        '''dumps screenshots'''
        # we start from library, including path and we need only the test name here
        lib_name: str = self._trace_path.split("libangle_restricted_traces_")[1]
        test_name: str = lib_name[:-3]

        # change working directory into where .so is placed
        angle_path: str = path.dirname(self._trace_path)
        chdir(angle_path)

        cmd = self._retrace_cmd + ['--one-frame-only',
                                   '--gtest_filter=TraceTest.' + test_name,
                                   '--use-angle=vulkan',
                                   '--screenshot-dir', self._output_dir,
                                   '--save-screenshots']
        self._run_logged_command(cmd, None)

        angle_screenshot: str = path.join(self._output_dir, 'angle_vulkan_' + test_name + '.png')
        piglit_screenshot: str = f'{path.join(self._output_dir, path.basename(self._trace_path))}-.png'
        rename(angle_screenshot, piglit_screenshot)


REGISTRY = Registry(
    extensions=['.so'],
    backend=ANGLETraceBackend,
)
