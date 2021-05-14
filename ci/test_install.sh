#!/usr/bin/env bash
set -ex

# Various tests to test the FindOpenVDB CMake modules and
# general VDB installation

# slow-ish but reliable
cmake --system-information &> sysinfo
os=$(cat sysinfo | grep CMAKE_HOST_SYSTEM_NAME | cut -f2 -d' ' | tr -d '"')
if [ "$os" == "Windows" ]; then
    module_path="C:/Program Files/OpenVDB/lib/cmake/OpenVDB"
else
    module_path="/usr/local/lib64/cmake/OpenVDB/"
fi

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
cmake -DCMAKE_MODULE_PATH="$module_path" .
cmake --build . --target test_vdb_print
