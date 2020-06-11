#!/usr/bin/env bash

set -ex

BUILD_TYPE="$1"; shift
ABI="$1"; shift
BLOSC="$1"; shift
SIMD="$1"; shift
CMAKE_EXTRA="$@"

# github actions runners have 2 threads
# https://help.github.com/en/actions/reference/virtual-environments-for-github-hosted-runners
export CMAKE_BUILD_PARALLEL_LEVEL=2

# DebugNoInfo is a custom CMAKE_BUILD_TYPE - no optimizations, no symbols, asserts enabled

mkdir -p $HOME/install
mkdir build
cd build

# print version
bash --version
if [ $CXX ]; then echo $CXX -v; fi
cmake --version

cmake \
    -DCMAKE_CXX_FLAGS_DebugNoInfo="" \
    -DCMAKE_BUILD_TYPE=${BUILD_TYPE} \
    -DOPENVDB_ABI_VERSION_NUMBER=${ABI} \
    -DOPENVDB_USE_DEPRECATED_ABI_5=ON \
    -DUSE_BLOSC=${BLOSC} \
    -DOPENVDB_BUILD_PYTHON_MODULE=ON \
    -DOPENVDB_BUILD_UNITTESTS=ON \
    -DOPENVDB_BUILD_BINARIES=ON \
    -DOPENVDB_BUILD_VDB_PRINT=ON \
    -DOPENVDB_BUILD_VDB_LOD=ON \
    -DOPENVDB_BUILD_VDB_RENDER=ON \
    -DOPENVDB_BUILD_VDB_VIEW=ON \
    -DOPENVDB_SIMD=${SIMD} \
    -DCMAKE_INSTALL_PREFIX=$HOME/install \
    ${CMAKE_EXTRA} \
    ..

cmake --build . --config $BUILD_TYPE --target install
