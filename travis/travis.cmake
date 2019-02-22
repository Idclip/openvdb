#!/usr/bin/env bash
#
# Copyright (c) 2012-2019 DreamWorks Animation LLC
#
# All rights reserved. This software is distributed under the
# Mozilla Public License 2.0 ( http://www.mozilla.org/MPL/2.0/ )
#
# Redistributions of source code must retain the above copyright
# and license notice and the following restrictions and disclaimer.
#
# *     Neither the name of DreamWorks Animation nor the names of
# its contributors may be used to endorse or promote products derived
# from this software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
# "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
# LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
# A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
# OWNER OR CONTRIBUTORS BE LIABLE FOR ANY INDIRECT, INCIDENTAL,
# SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
# LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
# DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
# THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
# IN NO EVENT SHALL THE COPYRIGHT HOLDERS' AND CONTRIBUTORS' AGGREGATE
# LIABILITY FOR ALL CLAIMS REGARDLESS OF THEIR BASIS EXCEED US$250.00.
#
#
# Travis build script
#
# Builds OpenVDB and all dependent libraries on a Travis VM for a specific build combination
#
# TASK (install/script):
#        * extras (standalone - builds the Python module, tests and binaries)
#        * core (standalone - builds the Python module, tests and binaries)
#        * test (standalone - builds the Python module, tests and binaries)
#        * houdini (standalone - builds the Python module, tests and binaries)
#        * run (standalone - builds the Python module, tests and binaries)
# ABI (3/4/5) - the ABI version of OpenVDB
# MODE (release/debug/header):
#        * release (standalone - builds all core library components and runs all tests)
#        * debug (builds all core library components in debug mode)
# HOUDINI (yes/no) - use Houdini libraries
#
# (Note Travis instances allow only 7.5GB of memory per VM, so limit concurrent builds to 4)
#
# Author: Dan Bailey

set -ex

TASK="$1"
ABI="$2"
MODE="$4"
HOUDINI_MAJOR="$5"
COMPILER="$6"

export ILMBASE_ROOT=$BOB_WORLD_SLOT_ilmbase
export OPENEXR_ROOT=$BOB_WORLD_SLOT_openexr
export BOOST_ROOT=$BOB_WORLD_SLOT_boost
export TBB_ROOT=$BOB_WORLD_SLOT_tbb
export CPPUNIT_ROOT=$HOME/cppunit/
export GLFW3_ROOT=
export OPENVDB_ROOT=$BOB_WORLD_SLOT_openvdb
export BLOSC_ROOT=$BOB_WORLD_SLOT_blosc


declare -A CMAKE_DICT

CMAKE_DICT["CMAKE_BUILD_TYPE"]=$MODE
CMAKE_DICT["OPENVDB_ABI_VERSION_NUMBER"]=$ABI
CMAKE_DICT["MINIMUM_BOOST_VERSION"]="1.55"
CMAKE_DICT["ILMBASE_NAMESPACE_VERSIONING"]="OFF"
CMAKE_DICT["OPENEXR_NAMESPACE_VERSIONING"]="OFF"
CMAKE_DICT["OPENVDB_BUILD_DOCS"]="ON"
CMAKE_DICT["OPENVDB_BUILD_PYTHON_MODULE"]="ON"
CMAKE_DICT["PYOPENVDB_INSTALL_DIRECTORY"]="lib/python"
CMAKE_DICT["OPENVDB_BUILD_HOUDINI_SOPS"]="OFF"
CMAKE_DICT["OPENVDB_BUILD_MAYA_PLUGIN"]="OFF"
CMAKE_DICT["OPENVDB_BUILD_UNITTESTS"]="ON"
CMAKE_DICT["OPENVDB_BUILD_CORE"]="ON"
CMAKE_DICT["USE_GLFW3"]="ON"
CMAKE_DICT["GLFW3_USE_STATIC_LIBS"]="ON"
CMAKE_DICT["OPENVDB_CXX_STRICT"]="ON"

# format arguments for command line

CMD_STR=""
for i in "${!PARMS[@]}"; do
    CMD_STR+="-D $i ${PARMS[$i]} ";
done

mkdir .build
cd .build

cmake $CMD_STR ../

cd -



# Location of third-party dependencies for standalone and houdini builds
STANDALONE_ARGS="   BOOST_LIB_DIR=/usr/lib/x86_64-linux-gnu\
                    EXR_INCL_DIR=/usr/include/OpenEXR\
                    EXR_LIB_DIR=/usr/local/lib\
                    TBB_LIB_DIR=/usr/lib\
                    CONCURRENT_MALLOC_LIB=\
                    CPPUNIT_INCL_DIR=$HOME/cppunit/include\
                    CPPUNIT_LIB_DIR=$HOME/cppunit/lib\
                    LOG4CPLUS_INCL_DIR=/usr/include\
                    LOG4CPLUS_LIB_DIR=/usr/lib/x86_64-linux-gnu\
                    GLFW_INCL_DIR=/usr/include/GL\
                    GLFW_LIB_DIR=/usr/lib/x86_64-linux-gnu\
                    PYTHON_INCL_DIR=/usr/include/python2.7\
                    PYTHON_LIB_DIR=/usr/lib/x86_64-linux-gnu\
                    BOOST_PYTHON_LIB_DIR=/usr/lib/x86_64-linux-gnu\
                    BOOST_PYTHON_LIB=-lboost_python\
                    NUMPY_INCL_DIR=/usr/lib/python2.7/dist-packages/numpy/core/include/numpy\
                    PYTHON_WRAP_ALL_GRID_TYPES=yes\
                    EPYDOC=/usr/bin/epydoc\
                    DOXYGEN=/usr/bin/doxygen"

HOUDINI_ARGS="      BOOST_INCL_DIR=/test/hou/toolkit/include\
                    BOOST_LIB_DIR=/test/hou/dsolib\
                    TBB_LIB_DIR=/test/hou/dsolib\
                    EXR_INCL_DIR=/test/hou/toolkit/include\
                    EXR_LIB_DIR=/test/hou/dsolib\
                    LOG4CPLUS_INCL_DIR=\
                    GLFW_INCL_DIR=\
                    PYTHON_INCL_DIR=\
                    DOXYGEN="

# Blosc
if [ "$BLOSC" = "yes" ]; then
    STANDALONE_ARGS =" BLOSC_INCL_DIR=$HOME/blosc/include\
                       BLOSC_LIB_DIR=$HOME/blosc/lib"
    HOUDINI_ARGS+=" BLOSC_INCL_DIR=/test/hou/toolkit/include\
                    BLOSC_LIB_DIR=/test/hou/dsolib"
else
    STANDALONE_ARGS=" BLOSC_INCL_DIR=\
                      BLOSC_LIB_DIR="
    HOUDINI_ARGS+=" BLOSC_INCL_DIR=\
                    BLOSC_LIB_DIR="
fi

MAKE_ARGS="$COMMON_ARGS"

if [ "$HOUDINI_MAJOR" = "none" ]; then
    MAKE_ARGS+=" $STANDALONE_ARGS"
else
    # source houdini_setup
    cd hou
    source houdini_setup
    cd -
    MAKE_ARGS+=" $HOUDINI_ARGS"
fi

# zero ccache stats
ccache -z

mkdir -p $HOME/builds/$TRAVIS_BUILD_ID

if [ "$TASK" = "extras" ]; then
    # build OpenVDB core library, OpenVDB Python module and all binaries
    if [ "$COMPILER" = "gcc" ]; then
        # for GCC, pre-build vdb_view and python using fewer threads in order to reduce memory consumption
        make -C openvdb $MAKE_ARGS install_lib -j4
        make -C openvdb $MAKE_ARGS vdb_view -j2
        make -C openvdb $MAKE_ARGS python -j2
    fi
    make -C openvdb $MAKE_ARGS install -j4
    cp openvdb/libopenvdb.so* $HOME/builds/$TRAVIS_BUILD_ID
elif [ "$TASK" = "core" ]; then
    if [ "$MODE" = "header" ]; then
        # check for any indirect includes
        make -C openvdb $MAKE_ARGS header_test -j4
    else
        # build OpenVDB core library
        make -C openvdb $MAKE_ARGS install_lib -j4
        cp openvdb/libopenvdb.so* $HOME/builds/$TRAVIS_BUILD_ID
    fi
elif [ "$TASK" = "test" ]; then
    sed -E -i.bak "s/vdb_test: \\$\(LIBOPENVDB\) /vdb_test: /g" openvdb/Makefile
    cp $HOME/builds/$TRAVIS_BUILD_ID/libopenvdb.so* openvdb
    # build OpenVDB core library and unit tests
    make -C openvdb $MAKE_ARGS vdb_test -j4
    if [ "$MODE" = "release" ]; then
        cp openvdb/vdb_test $HOME/builds/$TRAVIS_BUILD_ID
    else
        # cleanup (we don't run them in debug)
        rm -r $HOME/builds/$TRAVIS_BUILD_ID
    fi
elif [ "$TASK" = "houdini" ]; then
    if [ ! "$MODE" = "header" ]; then
        # install only
        cp $HOME/builds/$TRAVIS_BUILD_ID/libopenvdb.so* openvdb
        sed -i.bak 's/install_lib: lib/install_lib: /g' openvdb/Makefile
    fi
    make -C openvdb $MAKE_ARGS install_lib -j4
    # disable hcustom tagging to remove timestamps in Houdini DSOs for ccache
    # note that this means the DSO can no longer be loaded in Houdini
    sed -i.bak 's/\/hcustom/\/hcustom -t/g' openvdb_houdini/Makefile
    make -C openvdb_houdini $MAKE_ARGS houdinilib -j4
    # manually install OpenVDB houdini headers and lib
    mkdir houdini_utils
    cp openvdb_houdini/houdini/*.h openvdb_houdini
    cp openvdb_houdini/houdini/*.h houdini_utils
    cp openvdb_houdini/libopenvdb_houdini* /tmp/OpenVDB/lib
    if [ "$MODE" = "header" ]; then
        # check for any indirect includes
        make -C openvdb_houdini $MAKE_ARGS header_test -j4
    else
        # build OpenVDB Houdini SOPs
        make -C openvdb_houdini $MAKE_ARGS install -j4
    fi
    # cleanup
    rm -r $HOME/builds/$TRAVIS_BUILD_ID
elif [ "$TASK" = "run" ]; then
    export LD_LIBRARY_PATH=${LD_LIBRARY_PATH}:$HOME/builds/$TRAVIS_BUILD_ID; $HOME/builds/$TRAVIS_BUILD_ID/vdb_test -v
    # cleanup
    rm -r $HOME/builds/$TRAVIS_BUILD_ID
fi

# output ccache stats
ccache -s
