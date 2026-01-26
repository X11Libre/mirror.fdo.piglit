#!/bin/bash
set -eux

export DEBIAN_FRONTEND=noninteractive

apt-get update
apt-get install -y \
  ca-certificates

sed -i -e 's/http:\/\/deb/https:\/\/deb/g' /etc/apt/sources.list

apt-get update

# Ephemeral packages (installed for this script and removed again at the end)
EPHEMERAL=(
  bzip2
  curl
  libpciaccess-dev
  meson
  unzip
)

apt-get install -y \
  bison \
  ccache \
  cmake \
  codespell \
  flake8 \
  flex \
  freeglut3-dev \
  g++-multilib \
  gcc-multilib \
  gettext \
  git \
  glslang-tools \
  isort \
  jq \
  libdrm-dev \
  libegl1-mesa-dev \
  libgbm-dev \
  libglvnd-dev \
  libvulkan-dev \
  libwaffle-dev \
  libwayland-dev \
  libxcb-dri2-0-dev \
  libxkbcommon-dev \
  libxrender-dev \
  mingw-w64 \
  mypy \
  ninja-build \
  ocl-icd-opencl-dev \
  pkg-config \
  python3 \
  python3-dev \
  python3-packaging \
  python3-pip \
  python3-psutil \
  python3-wheel \
  pylint \
  tox \
  waffle-utils \
  "${EPHEMERAL[@]}"

pip3 install jsonschema==4.25.1
pip3 install mako==1.3.10
pip3 install mock==5.2.0
pip3 install numpy==2.0.2
pip3 install pillow==11.3.0
pip3 install pytest==8.4.2
pip3 install pytest-mock==3.15.1
pip3 install pytest-raises==0.11
pip3 install pytest-timeout==2.4.0
pip3 install pyyaml==6.0.3
pip3 install requests==2.32.5
pip3 install requests-mock==1.12.1
pip3 install setuptools==80.10.2

# Download Waffle artifacts.  See also
# https://gitlab.freedesktop.org/mesa/waffle/-/merge_requests/89
# https://docs.gitlab.com/ee/ci/pipelines/job_artifacts.html#downloading-the-latest-artifacts
for target in mingw32 mingw64
do
    mkdir -p /opt/waffle/$target
    curl -s -L "https://gitlab.freedesktop.org/mesa/waffle/-/jobs/artifacts/${WAFFLE_BRANCH:-maint-1.7}/raw/publish/$target/waffle-$target.zip?job=cmake-mingw" -o /tmp/waffle-$target.zip
    unzip -qo /tmp/waffle-$target.zip -d /opt/waffle/$target
    test -d /opt/waffle/$target/waffle
    rm /tmp/waffle-$target.zip
done

.gitlab-ci/build-wayland.sh

apt-get purge -y "${EPHEMERAL[@]}"
apt-get autoremove -y --purge
