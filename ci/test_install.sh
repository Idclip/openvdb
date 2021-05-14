#!/usr/bin/env bash
set -ex

# Various tests to test the FindOpenVDB CMake modules and
# general VDB installation

# slow-ish but reliable
cmake --system-information &> sysinfo
os=$(cat sysinfo | grep CMAKE_HOST_SYSTEM_NAME | cut -f2 -d' ' | tr -d '"')

# 1) Test basic CMakeLists is able to build vdb_print with
# the expected VDB installation

cmakelists="
cmake_minimum_required(VERSION 3.12)
project(TestInstall LANGUAGES CXX)
add_compile_options(\"$<$<CXX_COMPILER_ID:MSVC>:/bigobj>\")
find_package(OpenVDB REQUIRED COMPONENTS openvdb)
add_executable(test_vdb_print \"../openvdb/openvdb/cmd/openvdb_print.cc\")
target_link_libraries(test_vdb_print OpenVDB::openvdb)
"
mkdir tmp
cd tmp
echo -e "$cmakelists" > CMakeLists.txt

if [ "$os" == "Windows" ]; then
    cmake -G "Visual Studio 16 2019" -A x64 \
      -DVCPKG_TARGET_TRIPLET="${VCPKG_DEFAULT_TRIPLET}" \
      -DCMAKE_TOOLCHAIN_FILE="${VCPKG_INSTALLATION_ROOT}\scripts\buildsystems\vcpkg.cmake" \
      -DCMAKE_MODULE_PATH="C:/Program Files/OpenVDB/lib/cmake/OpenVDB" .
else
    cmake -DCMAKE_MODULE_PATH="/usr/local/lib64/cmake/OpenVDB/" .
fi

cmake --build . --target test_vdb_print