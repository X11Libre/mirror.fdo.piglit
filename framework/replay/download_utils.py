# coding=utf-8
#
# Copyright © 2020, 2022 Collabora Ltd
# Copyright © 2020 Valve Corporation.
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

import base64
import hashlib
import hmac
import xml.etree.ElementTree as ET
from email.utils import formatdate
from os import path
from pathlib import Path
from time import time
from typing import Any, Dict
from urllib.parse import urlparse

import requests
from requests.adapters import HTTPAdapter, Retry
from requests.utils import requote_uri

from framework import core, exceptions
from framework.replay.local_file_adapter import LocalFileAdapter
from framework.replay.options import OPTIONS

__all__ = ['ensure_file']

minio_credentials = None


def sign_with_hmac(key, message):
    key = key.encode("UTF-8")
    message = message.encode("UTF-8")

    signature = hmac.new(key, message, hashlib.sha1).digest()

    return base64.encodebytes(signature).strip().decode()


def get_minio_credentials(url):
    global minio_credentials

    if minio_credentials is not None:
        return (minio_credentials['AccessKeyId'],
                minio_credentials['SecretAccessKey'],
                minio_credentials['SessionToken'])

    minio_credentials = {}

    params = {'Action': 'AssumeRoleWithWebIdentity',
              'Version': '2011-06-15',
              'RoleArn': 'arn:aws:iam::123456789012:role/FederatedWebIdentityRole',
              'RoleSessionName': OPTIONS.download['role_session_name'],
              'DurationSeconds': 3600,
              'WebIdentityToken': OPTIONS.download['jwt']}
    r = requests.post('https://%s' % OPTIONS.download['minio_host'], params=params)
    if r.status_code >= 400:
        print(r.text)
    r.raise_for_status()

    root = ET.fromstring(r.text)
    for attr in root.iter():
        if attr.tag == '{https://sts.amazonaws.com/doc/2011-06-15/}AccessKeyId':
            minio_credentials['AccessKeyId'] = attr.text
        elif attr.tag == '{https://sts.amazonaws.com/doc/2011-06-15/}SecretAccessKey':
            minio_credentials['SecretAccessKey'] = attr.text
        elif attr.tag == '{https://sts.amazonaws.com/doc/2011-06-15/}SessionToken':
            minio_credentials['SessionToken'] = attr.text

    return (minio_credentials['AccessKeyId'],
            minio_credentials['SecretAccessKey'],
            minio_credentials['SessionToken'])


def get_minio_authorization_headers(url, resource):
    minio_key, minio_secret, minio_token = get_minio_credentials(url)

    date = formatdate(timeval=None, localtime=False, usegmt=True)
    to_sign = "GET\n\n\n%s\nx-amz-security-token:%s\n/%s/%s" % (date,
                                                                minio_token,
                                                                OPTIONS.download['minio_bucket'],
                                                                requote_uri(resource))
    signature = sign_with_hmac(minio_secret, to_sign)

    headers = {'Host': OPTIONS.download['minio_host'],
               'Date': date,
               'Authorization': 'AWS %s:%s' % (minio_key, signature),
               'x-amz-security-token': minio_token}
    return headers


def get_jwt_authorization_headers(url, resource):
    date = formatdate(timeval=None, localtime=False, usegmt=True)
    jwt = OPTIONS.download['jwt']
    host = urlparse(url).netloc

    headers = {'Host': host,
               'Date': date,
               'Authorization': 'Bearer %s' % (jwt)}
    return headers


def download(url: str, file_path: str, headers: Dict[str, str], attempts: int = 2) -> None:
    """Downloads a URL content into a file

    :param url: URL to download
    :param file_path: Local file name to contain the data downloaded
    :param attempts: Number of attempts
    """
    retries = Retry(
        backoff_factor=30,
        connect=attempts,
        read=attempts,
        redirect=attempts,
        status_forcelist=[429, 500, 502, 503, 504],
        raise_on_redirect=False
    )
    session = requests.Session()
    for protocol in ["http://", "https://"]:
        adapter = HTTPAdapter(max_retries=retries)
        session.mount(protocol, adapter)
    for protocol in ["file://"]:
        file_adapter = LocalFileAdapter()
        session.mount(protocol, file_adapter)

    local_file_checksum = hashlib.md5()
    with session.get(url,
                     allow_redirects=True,
                     stream=True,
                     headers=headers) as response:
        with open(file_path, "wb") as file:
            response.raise_for_status()
            for chunk in response.iter_content(chunk_size=1048576):
                if chunk:
                    file.write(chunk)
                    local_file_checksum.update(chunk)

    verify_file_integrity(file_path, response.headers, local_file_checksum.hexdigest())


def verify_file_integrity(file_path: str, headers: Any, local_file_checksum: str) -> None:
    """
    :param file_path: path to the local file
    :param headers: reference to the request
    :param local_file_checksum: already generated MD5
    """
    try:
        remote_file_checksum: str = headers["etag"].strip('\"').lower()
        if remote_file_checksum != local_file_checksum:
            raise exceptions.PiglitFatalError(
                    f"MD5 checksum {local_file_checksum} "
                    f"doesn't match remote ETag MD5 {remote_file_checksum}"
            )
    except KeyError:
        print("ETag is missing from the HTTPS header. "
              "Fall back to Content-length verification.")

    try:
        remote_file_size = int(headers["Content-length"])
    except KeyError:
        print("Error getting Content-Length from server. "
              "Skipping file size check.")
        return

    if remote_file_size != path.getsize(file_path):
        raise exceptions.PiglitFatalError(
                f"Invalid filesize src {remote_file_size} "
                f"doesn't match {path.getsize(file_path)}"
        )


def verify_local_file_checksum(url, file_path, headers, destination_file_path):
    @core.timer_ms
    def check_md5():
        print(
            f"[check_image] Verifying already downloaded file {file_path}",
            end=" ",
            flush=True,
        )
        local_file_checksum = hashlib.md5(
            Path(destination_file_path).read_bytes()
        ).hexdigest()
        verify_file_integrity(
            destination_file_path, remote_headers, local_file_checksum
        )

    print(f"[check_image] Requesting headers for {file_path}", end=" ", flush=True)
    try:
        response = requests.head(url + file_path, timeout=60, headers=headers)
    except requests.exceptions.RequestException as err:
        print(f"Not verified! HTTP request failed with {err}", flush=True)
        return
    print(
        f"returned {response.status_code}.",
        f"Took {response.elapsed.microseconds / 1000} ms",
        flush=True,
    )
    remote_headers = response.headers

    check_md5()


def ensure_file(file_path):
    destination_file_path = path.join(OPTIONS.db_path, file_path)
    if OPTIONS.download['url'] is None:
        if not path.exists(destination_file_path):
            raise exceptions.PiglitFatalError(
                '{} missing'.format(destination_file_path))
        return

    url = OPTIONS.download['url'].geturl()

    if OPTIONS.download['caching_proxy_url'] is not None:
        url = OPTIONS.download['caching_proxy_url'].geturl() + url

    core.check_dir(path.dirname(destination_file_path))

    if OPTIONS.download['minio_host']:
        assert OPTIONS.download['minio_bucket']
        assert OPTIONS.download['role_session_name']
        assert OPTIONS.download['jwt']
        headers = get_minio_authorization_headers(url, file_path)
    elif OPTIONS.download['jwt']:
        headers = get_jwt_authorization_headers(url, file_path)
    else:
        headers = None

    if not OPTIONS.download['force'] and path.exists(destination_file_path):
        verify_local_file_checksum(url, file_path, headers, destination_file_path)
        return

    print(f"[check_image] Downloading file {file_path}", end=" ", flush=True)

    download_time = time()

    download(url + file_path, destination_file_path, headers)

    print('took %ds.' % (time() - download_time), flush=True)
