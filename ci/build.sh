#!/usr/bin/env bash

set -e

# print versions
bash --version
if [ ! -z "$CXX" ]; then $CXX -v; fi
cmake --version

################################################

# Each option may be followed by one colon to indicate it
# has a required argument, and by two colons to indicate it
# has an optional argument

OPTS_ARGS+=("t:")  ## See --target
OPTS_ARGS+=("j:")  ## Thread usage
OPTS_ARGS+=("c:")  ## See --cargs
OPTS_ARGS+=("v")   ## See --verbose
OPTL_ARGS+=("components:")  ## Specify cmake component(s) to enable
OPTL_ARGS+=("config:")      ## Specify cmake configuration during the build step
OPTL_ARGS+=("target:")      ## Specify target(s) to build
OPTL_ARGS+=("cargs:")       ## args to pass directly to cmake generation step
OPTL_ARGS+=("build-type:")  ## Release, Debug, etc.
OPTL_ARGS+=("verbose")      ## Verbose build output

# Defaults
declare -A PARMS
PARMS[--components]=core,bin
PARMS[--target]=install
# github actions runners have 2 threads
# https://help.github.com/en/actions/reference/virtual-environments-for-github-hosted-runners
PARMS[-j]=2

# Available options for --components
declare -A COMPONENTS
COMPONENTS['core']='OPENVDB_BUILD_CORE'
COMPONENTS['python']='OPENVDB_BUILD_PYTHON_MODULE'
COMPONENTS['test']='OPENVDB_BUILD_UNITTESTS'
COMPONENTS['bin']='OPENVDB_BUILD_BINARIES'
COMPONENTS['hou']='OPENVDB_BUILD_HOUDINI_PLUGIN'
COMPONENTS['doc']='OPENVDB_BUILD_DOCS'
COMPONENTS['axcore']='OPENVDB_BUILD_AX'
COMPONENTS['axgr']='OPENVDB_BUILD_AX_GRAMMAR'
COMPONENTS['axbin']='OPENVDB_BUILD_AX_BINARIES'
COMPONENTS['axtest']='OPENVDB_BUILD_AX_UNITTESTS'

################################################

HAS_PARM() {
    if [ -z "${PARMS[$1]}" ]; then return 1
    else return 0; fi
}

CONTAINS () {
    local e match="$1"
    shift
    for e; do [[ "$e" == "$match" ]] && return 0; done
    return 1
}

# Format to string and replace spaces with commas
LARGS_STR="${OPTL_ARGS[@]}"
LARGS_STR=${LARGS_STR// /,}
SARGS_STR="${OPTS_ARGS[@]}"
SARGS_STR=${SARGS_STR// /,}

# Parse all arguments and store them in an array, split by whitespace. Error if unsupported
ARGS="$(eval getopt --options=$SARGS_STR --longoptions=$LARGS_STR -- $@)"
eval "ARGS=($ARGS)"

# Parse all user supplied command line arguments into an assosiate array
NUM_ARGS=${#ARGS[@]}
USED_ARGS=0
for ARG in "${ARGS[@]}"; do
    USED_ARGS=$((USED_ARGS+1))
    if [[ $ARG == "--" ]]; then break; fi
    if [[ $ARG == "--"* ]] || [[ $ARG == "-"* ]]; then
        if CONTAINS "${ARG:1}:" "${OPTS_ARGS[@]}"; then
            key=$ARG
        elif CONTAINS "${ARG:2}:" "${OPTL_ARGS[@]}"; then
            key=$ARG
        elif [ ! -z $key ]; then
            PARMS[$key]="$ARG"
            key=""
        else
            PARMS[$ARG]=1
        fi
    elif [ ! -z $key ]; then
        PARMS[$key]=$ARG
        key=""
    fi
done

if [ $USED_ARGS -ne $NUM_ARGS ]; then
    for ARG in $(seq $USED_ARGS $NUM_ARGS); do
        IGNORED+=${ARGS[$ARG]}
    done
    echo "Warning: The following arguments were ignored -- $IGNORED"
fi

################################################

# extract arguments

if HAS_PARM -t; then TARGET=${PARMS[-t]}; fi
if HAS_PARM --target; then
    if [ -z $TARGET ]; then TARGET=${PARMS[--target]}
    else TARGET+=","${PARMS[--target]}; fi
fi
if HAS_PARM -c; then CMAKE_EXTRA=${PARMS[-c]}; fi
if HAS_PARM --cargs; then
    if [ -z $CMAKE_EXTRA ]; then CMAKE_EXTRA=${PARMS[--cargs]}
    else CMAKE_EXTRA+=" "${PARMS[--cargs]}; fi
fi
eval "CMAKE_EXTRA=($CMAKE_EXTRA)"

# Using CMAKE_VERBOSE_MAKEFILE rather than `cmake --verbose` to support older
# versions of CMake. Always run verbose make to have the full logs available
if HAS_PARM -v; then CMAKE_EXTRA+=("-DCMAKE_VERBOSE_MAKEFILE=ON"); fi
if HAS_PARM --verbose; then CMAKE_EXTRA+=("-DCMAKE_VERBOSE_MAKEFILE=ON"); fi
if HAS_PARM --build-type; then CMAKE_EXTRA+=("-DCMAKE_BUILD_TYPE=${PARMS[--build-type]}"); fi

# Available components. If a component is not provided it is
# explicitly set to OFF.
IN_COMPONENTS=${PARMS[--components]}
IFS=', ' read -r -a IN_COMPONENTS <<< "$IN_COMPONENTS"
for comp in "${IN_COMPONENTS[@]}"; do
    if [ -z ${COMPONENTS[$comp]} ]; then
        echo "Invalid component passed to build \"$comp\""; exit -1
    fi
done
# Build Components command
for comp in "${!COMPONENTS[@]}"; do
    found=false
    for in in "${IN_COMPONENTS[@]}"; do
        if [[ $comp == "$in" ]]; then
            found=true; break
        fi
    done

    if $found; then
        CMAKE_EXTRA+=("-D${COMPONENTS[$comp]}=ON")
    else
        CMAKE_EXTRA+=("-D${COMPONENTS[$comp]}=OFF")
    fi
done

################################################

export CMAKE_BUILD_PARALLEL_LEVEL=${PARMS[-j]}

mkdir -p build
cd build

# Report the cmake commands
set -x

# Note:
# - all sub binary options are always on and can be toggles with: OPENVDB_BUILD_BINARIES=ON/OFF
cmake \
    -DOPENVDB_USE_DEPRECATED_ABI_6=ON \
    -DOPENVDB_USE_FUTURE_ABI_9=ON \
    -DOPENVDB_BUILD_VDB_PRINT=ON \
    -DOPENVDB_BUILD_VDB_LOD=ON \
    -DOPENVDB_BUILD_VDB_RENDER=ON \
    -DOPENVDB_BUILD_VDB_VIEW=ON \
    -DMSVC_MP_THREAD_COUNT=${PARMS[-j]} \
    "${CMAKE_EXTRA[@]}" \
    ..

# NOTE: --parallel only effects the number of projects build, not t-units.
# We support this with out own MSVC_MP_THREAD_COUNT option for MSVC.
# Alternatively it is mentioned that the following should work:
#   cmake --build . --  /p:CL_MPcount=8
# However it does not seem to for our project.
# https://gitlab.kitware.com/cmake/cmake/-/issues/20564

# cmake 3.14 and later required for --verbose
if HAS_PARM --config; then
    cmake --build . --parallel ${PARMS[-j]} --config ${PARMS[--config]} --target "$TARGET"
else
    cmake --build . --parallel ${PARMS[-j]} --target "$TARGET"
fi
