# coding=utf-8
#
# Copyright © 2020 Valve Corporation.
# Copyright © 2022 Collabora Ltd
#
# Permission is hereby granted, free of charge, to any person obtaining a
# copy of this software and associated documentation files (the "Software"),
# to deal in the Software without restriction, including without limitation
# the rights to use, copy, modify, merge, publish, distribute, sublicense,
# and/or sell copies of the Software, and to permit persons to whom the
# Software is furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included
# in all copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
# OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
# THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
# OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
# ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
# OTHER DEALINGS IN THE SOFTWARE.
#
# SPDX-License-Identifier: MIT


"""Tests for replayer's download_utils module."""

import os
from contextlib import contextmanager
from contextlib import nullcontext as does_not_raise
from dataclasses import dataclass
from hashlib import md5
from os import path
from pathlib import Path
from typing import Any
from urllib.parse import urlparse

import pytest
import requests
import requests_mock

from framework import exceptions
from framework.replay import download_utils
from framework.replay.options import OPTIONS

ASSUME_ROLE_RESPONSE = '''<?xml version="1.0" encoding="UTF-8"?>
    <AssumeRoleWithWebIdentityResponse
        xmlns="https://sts.amazonaws.com/doc/2011-06-15/">
        <AssumeRoleWithWebIdentityResult>
            <Credentials>
                <AccessKeyId>Key</AccessKeyId>
                <SecretAccessKey>Secret</SecretAccessKey>
                <Expiration>2021-03-25T13:59:58Z</Expiration>
                <SessionToken>token</SessionToken>
            </Credentials>
        </AssumeRoleWithWebIdentityResult>
    </AssumeRoleWithWebIdentityResponse>
'''

class MockedResponseData:
    binary_data: bytes = b"haxter"


@dataclass(frozen=True)
class MockedResponse:
    @staticmethod
    def header_scenarios():
        binary_data_md5: str = md5(MockedResponseData.binary_data).hexdigest()
        etag: dict[str, str] = {"etag": binary_data_md5}
        length: dict[str, Any] = {
            "ContextLength": str(len(MockedResponseData.binary_data))
        }
        return {
            "With ContextLength": length,
            "With etag": etag,
            "With ContextLength and etag": {**length, **etag},
            "Without integrity headers": {},
        }

    @staticmethod
    def stored_file_scenarios():
        return {
            "nothing stored": None,
            "already has file": MockedResponseData.binary_data,
            "already has wrong file": b"wrong_data",
        }

    @contextmanager
    def create_file(trace_file, data):
        if data:
            Path(trace_file).write_bytes(MockedResponseData.binary_data)
            yield
            Path(trace_file).unlink()
        else:
            yield
class TestDownloadUtils(object):
    """Tests for download_utils methods."""

    @pytest.fixture(autouse=True)
    def setup(self, requests_mock, tmpdir):
        self.url = 'https://unittest.piglit.org/'
        self.trace_path = 'KhronosGroup-Vulkan-Tools/amd/polaris10/vkcube.gfxr'
        self.full_url = self.url + self.trace_path
        self.trace_file = tmpdir.join(self.trace_path)
        OPTIONS.set_download_url(self.url)
        OPTIONS.download['force'] = False
        OPTIONS.db_path = tmpdir.strpath
        requests_mock.get(self.full_url, text='remote')
        requests_mock.head(self.full_url, text='remote')

    @staticmethod
    def check_same_file(path_local, expected_content, expected_mtime=None):
        assert path_local.read() == expected_content
        if expected_mtime is not None:
            m = path_local.mtime()
            assert m == expected_mtime

    @pytest.fixture
    def prepare_trace_file(self):
        # Make sure the temporary directory exists
        os.makedirs(path.dirname(self.trace_file), exist_ok=True)

    @pytest.fixture
    def create_mock_response(self, requests_mock):
        def inner(url, headers):
            kwargs = {
                "content": MockedResponseData.binary_data,
                "headers": headers,
            }
            requests_mock.get(url, **kwargs)
            requests_mock.head(url, **kwargs)

        return inner

    def test_ensure_file_exists(self,
                                prepare_trace_file):
        """download_utils.ensure_file: Check an existing file doesn't get overwritten"""

        self.trace_file.write("local")
        m = self.trace_file.mtime()
        download_utils.ensure_file(self.trace_path)
        TestDownloadUtils.check_same_file(self.trace_file, "local", m)

    def test_ensure_file_not_exists(self):
        """download_utils.ensure_file: Check a non existing file gets downloaded"""

        assert not self.trace_file.check()
        download_utils.ensure_file(self.trace_path)
        TestDownloadUtils.check_same_file(self.trace_file, "remote")

    def test_ensure_file_exists_force_download(self,
                                               prepare_trace_file):
        """download_utils.ensure_file: Check an existing file gets overwritten when forced"""

        OPTIONS.download['force'] = True
        self.trace_file.write("local")
        self.trace_file.mtime()
        download_utils.ensure_file(self.trace_path)
        TestDownloadUtils.check_same_file(self.trace_file, "remote")

    @pytest.mark.raises(exception=exceptions.PiglitFatalError)
    def test_ensure_file_not_exists_no_url(self):
        """download_utils.ensure_file: Check an exception raises when not passing an URL for a non existing file"""

        OPTIONS.set_download_url("")
        assert not self.trace_file.check()
        download_utils.ensure_file(self.trace_path)

    @pytest.mark.raises(exception=requests.exceptions.HTTPError)
    def test_ensure_file_not_exists_404(self, requests_mock):
        """download_utils.ensure_file: Check an exception raises when an URL returns a 404"""

        requests_mock.get(self.full_url, text='Not Found', status_code=404)
        assert not self.trace_file.check()
        download_utils.ensure_file(self.trace_path)

    @pytest.mark.raises(exception=requests.exceptions.ConnectTimeout)
    def test_ensure_file_not_exists_timeout(self, requests_mock):
        """download_utils.ensure_file: Check an exception raises when an URL returns a Connect Timeout"""

        requests_mock.get(self.full_url, exc=requests.exceptions.ConnectTimeout)
        assert not self.trace_file.check()
        download_utils.ensure_file(self.trace_path)

    @contextmanager
    def already_has_wrong_file(self):
        self.trace_file.write(b"this_is_not_correct_file")
        yield
        Path(self.trace_file).unlink()

    @pytest.mark.parametrize(
        "stored_data",
        MockedResponse.stored_file_scenarios().values(),
        ids=MockedResponse.stored_file_scenarios().keys(),
    )
    @pytest.mark.parametrize(
        "headers",
        MockedResponse.header_scenarios().values(),
        ids=MockedResponse.header_scenarios().keys(),
    )
    def test_ensure_file_checks_integrity(
        self, prepare_trace_file, create_mock_response, headers, stored_data
    ):
        create_mock_response(self.full_url, headers)
        with MockedResponse.create_file(self.trace_file, stored_data):
            stored_file_is_wrong: bool = (
                self.trace_file.check()
                and self.trace_file.read() != MockedResponseData.binary_data.decode()
            )
            expectation = (
                pytest.raises(exceptions.PiglitFatalError)
                if stored_file_is_wrong
                else does_not_raise()
            )
            with expectation:
                download_utils.ensure_file(self.trace_path)

    @pytest.mark.raises(exception=exceptions.PiglitFatalError)
    def test_download_with_invalid_content_length(self,
                                                  requests_mock,
                                                  prepare_trace_file):
        """download_utils.download: Check if an exception raises
        when filesize doesn't match"""

        headers = {"Content-Length": "1"}
        requests_mock.get(self.full_url,
                          headers=headers,
                          text="Binary file content")

        assert not self.trace_file.check()
        download_utils.download(self.full_url, self.trace_file, None)

    def test_download_works_at_last_retry(self,
                                          requests_mock,
                                          prepare_trace_file):
        """download_utils.download: Check download retry mechanism"""

        bad_headers = {"Content-Length": "1"}
        # Mock attempts - 1 bad requests and a working last one
        attempts = 3
        for _ in range(attempts - 1):
            requests_mock.get(self.full_url,
                              headers=bad_headers,
                              text="Binary file content")
        requests_mock.get(self.full_url,
                          text="Binary file content")

        assert not self.trace_file.check()
        download_utils.download(self.full_url, self.trace_file, None)

    def test_download_without_content_length(self,
                                             requests_mock,
                                             prepare_trace_file):
        """download_utils.download: Check an exception raises
        when response does not have a Context-Length header"""

        missing_headers = {}
        requests_mock.get(self.full_url,
                          headers=missing_headers,
                          text="Binary file content")

        assert not self.trace_file.check()
        download_utils.download(self.full_url, self.trace_file, None)

    def test_minio_authorization(self, requests_mock):
        """download_utils.ensure_file: Check we send the authentication headers to MinIO"""
        requests_mock.post(self.url, text=ASSUME_ROLE_RESPONSE)
        OPTIONS.download['minio_host'] = urlparse(self.url).netloc
        OPTIONS.download['minio_bucket'] = 'minio_bucket'
        OPTIONS.download['role_session_name'] = 'role_session_name'
        OPTIONS.download['jwt'] = 'jwt'

        assert not self.trace_file.check()
        download_utils.ensure_file(self.trace_path)
        TestDownloadUtils.check_same_file(self.trace_file, "remote")

        post_request = requests_mock.request_history[0]
        assert(post_request.method == 'POST')

        get_request = requests_mock.request_history[1]
        assert(get_request.method == 'GET')
        assert(requests_mock.request_history[1].headers['Authorization'].startswith('AWS Key'))

    def test_jwt_authorization(self, requests_mock):
        """download_utils.ensure_file: Check we send the authentication headers to the server"""
        # reset minio_host from previous tests
        OPTIONS.download['minio_host'] = ''
        OPTIONS.download['jwt'] = 'jwt'

        assert not self.trace_file.check()
        download_utils.ensure_file(self.trace_path)
        TestDownloadUtils.check_same_file(self.trace_file, "remote")

        get_request = requests_mock.request_history[0]
        assert(get_request.method == 'GET')
        assert(requests_mock.request_history[0].headers['Authorization'].startswith('Bearer'))
