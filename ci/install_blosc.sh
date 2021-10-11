#!/usr/bin/env bash

set -ex

BLOSC_VERSION="$1"

git clone https://github.com/Blosc/c-blosc.git
cd c-blosc

if [ "$BLOSC_VERSION" != "latest" ]; then
    git checkout tags/v${BLOSC_VERSION} -b v${BLOSC_VERSION}
fi

mkdir build
cd build

cmake -DBUILD_STATIC=OFF -DBUILD_TESTS=OFF -DBUILD_FUZZERS=OFF -DBUILD_BENCHMARKS=OFF ../.
make -j4
make install
