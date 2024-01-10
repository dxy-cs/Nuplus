#!/bin/bash
set -e
sudo apt-get install -y make gcc cmake pkg-config libnl-3-dev libnl-route-3-dev  \
                        libnuma-dev uuid-dev libssl-dev libaio-dev libcunit1-dev \
                        libclang-dev libncurses-dev meson python-dev python3-pyelftools

bear make submodules -j`nproc`
make clean && bear make -j`nproc`
pushd ksched
make clean && make -j`nproc`
popd
pushd bindings/cc/
bear make -j`nproc`
popd
sudo ./scripts/setup_machine.sh
