# Copyright Contributors to the OpenVDB Project
# SPDX-License-Identifier: MPL-2.0
#
#[=======================================================================[.rst:

OpenVDBHoudiniSetup
-------------------

Wraps the call the FindPackage ( Houdini ) for OpenVDB builds. This
ensures that all dependencies that are included with a Houdini
distribution are configured to load from that installation.

This CMake searches for the HoudiniConfig.cmake module provided by
SideFX to configure the OpenVDB Houdini base and DSO libraries. Users
can provide paths to the location of their Houdini Installation by
setting HOUDINI_ROOT either as an environment variable or by passing it
to CMake. This module also reads the value of $HFS, usually set by
sourcing the Houdini Environment. Note that as long as you provide a
path to your Houdini Installation you do not need to source the
Houdini Environment.

Use this module by invoking include with the form::

  include ( OpenVDBHoudiniSetup )

Result Variables
^^^^^^^^^^^^^^^^

This will define the following variables:

``Houdini_FOUND``
  True if the system has Houdini installed.
``Houdini_VERSION``
  The version of the Houdini which was found.
``OPENVDB_HOUDINI_ABI``
  The ABI version that Houdini uses for it's own OpenVDB installation.
``HOUDINI_INCLUDE_DIR``
  The Houdini include directory.
``HOUDINI_LIB_DIR``
  The Houdini lib directory.

A variety of variables will also be set from HoudiniConfig.cmake.

Additionally, the following imported targets will be created:

``TBB::tbb`` aliased to ``Houdini::Dep::tbb``
``TBB::tbbmalloc`` aliased to ``Houdini::Dep::tbbmalloc``
``ZLIB::ZLIB`` aliased to ``Houdini::Dep::z``
``Blosc::blosc`` aliased to ``Houdini::Dep::blosc``
``Jemalloc::jemalloc`` aliased to ``Houdini::Dep::jemalloc`` if available

Hints
^^^^^

Instead of explicitly setting the cache variables, the following variables
may be provided to tell this module where to look.

``ENV{HFS}``
  Preferred installation prefix.
``Houdini_ROOT``
  Preferred installation prefix.
``DISABLE_CMAKE_SEARCH_PATHS``
  Disable CMakes default search paths for find_xxx calls in this module

#]=======================================================================]

# Find the Houdini installation and use Houdini's CMake to initialize
# the Houdini lib

cmake_minimum_required(VERSION 3.18)

# Include utility functions for version information
include(${CMAKE_CURRENT_LIST_DIR}/OpenVDBUtils.cmake)

set(_FIND_HOUDINI_ADDITIONAL_OPTIONS "")
if(DISABLE_CMAKE_SEARCH_PATHS)
  set(_FIND_HOUDINI_ADDITIONAL_OPTIONS NO_DEFAULT_PATH)
endif()

# Set _HOUDINI_ROOT based on a user provided root var. Xxx_ROOT and ENV{Xxx_ROOT}
# are prioritised over the legacy capitalized XXX_ROOT variables for matching
# CMake 3.12 behaviour
# @todo  decide on what variable to support, CMake 3.27 added back support for
#   both <UPPERCASE>_ROOT and <Lowercase>_ROOT variables...
if(Houdini_ROOT)
  set(_HOUDINI_ROOT ${Houdini_ROOT})
elseif(DEFINED ENV{Houdini_ROOT})
  set(_HOUDINI_ROOT $ENV{Houdini_ROOT})
elseif(HOUDINI_ROOT)
  set(_HOUDINI_ROOT ${HOUDINI_ROOT})
elseif(DEFINED ENV{HOUDINI_ROOT})
  set(_HOUDINI_ROOT $ENV{HOUDINI_ROOT})
endif()

set(_HOUDINI_ROOT_SEARCH_DIR)

if(_HOUDINI_ROOT)
  list(APPEND _HOUDINI_ROOT_SEARCH_DIR ${_HOUDINI_ROOT})
endif()

if(DEFINED ENV{HFS})
  list(APPEND _HOUDINI_ROOT_SEARCH_DIR $ENV{HFS})
endif()

# ------------------------------------------------------------------------
#  Search for Houdini
# ------------------------------------------------------------------------

set(_HOUDINI_CMAKE_PATH_SUFFIXES)

if(APPLE)
  list(APPEND _HOUDINI_CMAKE_PATH_SUFFIXES
    Frameworks/Houdini.framework/Versions/Current/Resources/toolkit/cmake
    Houdini.framework/Versions/Current/Resources/toolkit/cmake
    Versions/Current/Resources/toolkit/cmake
    Current/Resources/toolkit/cmake
    Resources/toolkit/cmake
  )
endif()

list(APPEND _HOUDINI_CMAKE_PATH_SUFFIXES
  toolkit/cmake
  cmake
)

find_package(Houdini
  ${_FIND_HOUDINI_ADDITIONAL_OPTIONS}
  PATHS ${_HOUDINI_ROOT_SEARCH_DIR}
  PATH_SUFFIXES ${_HOUDINI_CMAKE_PATH_SUFFIXES}
  REQUIRED)

# Note that passing MINIMUM_HOUDINI_VERSION into find_package(Houdini) doesn't work
if(NOT Houdini_FOUND)
  message(FATAL_ERROR "Unable to locate Houdini Installation.")
elseif(MINIMUM_HOUDINI_VERSION)
  if(Houdini_VERSION VERSION_LESS MINIMUM_HOUDINI_VERSION)
    message(FATAL_ERROR "Unsupported Houdini Version ${Houdini_VERSION}. Minimum "
      "supported is ${MINIMUM_HOUDINI_VERSION}."
    )
  endif()
endif()

set(Houdini_VERSION_MAJOR_MINOR "${Houdini_VERSION_MAJOR}.${Houdini_VERSION_MINOR}")

find_package(PackageHandleStandardArgs)
find_package_handle_standard_args(Houdini
  REQUIRED_VARS _houdini_install_root Houdini_FOUND
  VERSION_VAR Houdini_VERSION
)

# ------------------------------------------------------------------------
#  Check support for older versions of Houdini
# ------------------------------------------------------------------------

if(OPENVDB_FUTURE_DEPRECATION AND FUTURE_MINIMUM_HOUDINI_VERSION)
  if(Houdini_VERSION VERSION_LESS ${FUTURE_MINIMUM_HOUDINI_VERSION})
    message(DEPRECATION "Support for Houdini versions < ${FUTURE_MINIMUM_HOUDINI_VERSION} "
      "is deprecated and will be removed.")
  endif()
endif()

# ------------------------------------------------------------------------
#  Configure imported Houdini target
# ------------------------------------------------------------------------

# Set the relative directory containing Houdini libs and populate an extra list
# of Houdini dependencies for _houdini_create_libraries.

if(NOT HOUDINI_DSOLIB_DIR)
  if(APPLE)
    set(HOUDINI_DSOLIB_DIR Frameworks/Houdini.framework/Versions/Current/Libraries)
  elseif(UNIX)
    set(HOUDINI_DSOLIB_DIR dsolib)
  elseif(WIN32)
    set(HOUDINI_DSOLIB_DIR custom/houdini/dsolib)
  endif()
endif()

# Set Houdini lib and include directories

set(HOUDINI_INCLUDE_DIR ${_houdini_include_dir})
set(HOUDINI_LIB_DIR ${_houdini_install_root}/${HOUDINI_DSOLIB_DIR})

if(APPLE)
  # Additionally create extra deps. These are created automatically on most
  # platforms except MacOS which just uses the framework path (but we always
  # want these as we don't really want to link libopenvdb to the entire
  # Houdini core framework.
  set(_HOUDINI_EXTRA_LIBRARIES libz.dylib libblosc.dylib libtbb.dylib libtbbmalloc.dylib)
  set(_HOUDINI_EXTRA_TARGET_NAMES z blosc tbb tbbmalloc)

  foreach(LIB_INFO IN ZIP_LISTS _HOUDINI_EXTRA_LIBRARIES _HOUDINI_EXTRA_TARGET_NAMES)
    set(LIB_NAME ${LIB_INFO_0})
    set(TARGET_NAME ${LIB_INFO_1})

    if(TARGET Houdini::Dep::${TARGET_NAME})
      continue()
    endif()

    # This does NOT link the above to the Houdini imported target. It simply
    # makes sure that these libs have specific targets that the openvdb core
    # library can link against
    _houdini_create_libraries(
      PATHS ${HOUDINI_DSOLIB_DIR}/${LIB_NAME}
      TARGET_NAMES ${TARGET_NAME}
      TYPE SHARED
      EXTRA_DEP)

    # Not done automatically
    target_include_directories(Houdini::Dep::${TARGET_NAME}
      INTERFACE ${HOUDINI_INCLUDE_DIR})
  endforeach()

  unset(_HOUDINI_EXTRA_LIBRARIES)
  unset(_HOUDINI_EXTRA_TARGET_NAMES)
endif()

# ------------------------------------------------------------------------
#  Configure imported targets for the OpenVDB core components to use
# ------------------------------------------------------------------------

# ZLIB

add_library(ZLIB::ZLIB INTERFACE IMPORTED)
target_link_libraries(ZLIB::ZLIB INTERFACE Houdini::Dep::z)

# TBB

add_library(TBB::tbb INTERFACE IMPORTED)
target_link_libraries(TBB::tbb INTERFACE Houdini::Dep::tbb)

add_library(TBB::tbbmalloc INTERFACE IMPORTED)
target_link_libraries(TBB::tbbmalloc INTERFACE Houdini::Dep::tbbmalloc)

# Blosc

add_library(Blosc::blosc INTERFACE IMPORTED)
target_link_libraries(Blosc::blosc INTERFACE Houdini::Dep::blosc)

# Jemalloc (not available on some platforms)
# * On Mac OSX, linking against Jemalloc < 4.3.0 seg-faults with this error:
#     malloc: *** malloc_zone_unregister() failed for 0xaddress
#   As of Houdini 20, it still ships with Jemalloc 3.6.0, so don't expose it
#   on Mac OSX (https://github.com/jemalloc/jemalloc/issues/420).
if(NOT APPLE AND TARGET Houdini::Dep::jemalloc)
  add_library(Jemalloc::jemalloc INTERFACE IMPORTED)
  target_link_libraries(Jemalloc::jemalloc INTERFACE Houdini::Dep::jemalloc)
endif()

# Boost - currently must be provided as VDB is not fully configured to
# use Houdini's namespaced hboost

# EXR/Imath - optional and must be provided externally if requested

# ------------------------------------------------------------------------
#  Configure OpenVDB ABI
# ------------------------------------------------------------------------

if(NOT OPENVDB_USE_DELAYED_LOADING)
  message(FATAL_ERROR "Delay loading (OPENVDB_USE_DELAYED_LOADING) must be "
    "enabled when building against Houdini for ABI compatibility with Houdini's "
    "namespaced version of OpenVDB.")
endif()

# Explicitly configure the OpenVDB ABI version depending on the Houdini
# version.

find_file(_houdini_openvdb_version_file "openvdb/version.h"
  PATHS ${HOUDINI_INCLUDE_DIR}
  NO_DEFAULT_PATH)
if(_houdini_openvdb_version_file)
  OPENVDB_VERSION_FROM_HEADER("${_houdini_openvdb_version_file}"
    ABI OPENVDB_HOUDINI_ABI)
endif()
unset(_houdini_openvdb_version_file)
if(NOT OPENVDB_HOUDINI_ABI)
  message(WARNING "Unknown version of Houdini, assuming OpenVDB ABI=${OpenVDB_MAJOR_VERSION}, "
    "but if this not correct, the CMake flag -DOPENVDB_HOUDINI_ABI=<N> can override this value.")
  set(OPENVDB_HOUDINI_ABI ${OpenVDB_MAJOR_VERSION})
endif()

# ------------------------------------------------------------------------
#  Configure libstc++ CXX11 ABI
# ------------------------------------------------------------------------

if(UNIX AND NOT APPLE)
  # Assume we're using libstdc++
  message(STATUS "Configuring CXX11 ABI for Houdini compatibility...")

  execute_process(COMMAND echo "#include <string>"
    COMMAND ${CMAKE_CXX_COMPILER} "-x" "c++" "-E" "-dM" "-"
    COMMAND grep "-F" "_GLIBCXX_USE_CXX11_ABI"
    TIMEOUT 10
    RESULT_VARIABLE QUERIED_GCC_CXX11_ABI_SUCCESS
    OUTPUT_VARIABLE _GCC_CXX11_ABI)

  set(GLIBCXX_USE_CXX11_ABI "UNKNOWN")

  if(NOT QUERIED_GCC_CXX11_ABI_SUCCESS)
    string(FIND ${_GCC_CXX11_ABI} "_GLIBCXX_USE_CXX11_ABI 0" GCC_OLD_CXX11_ABI)
    string(FIND ${_GCC_CXX11_ABI} "_GLIBCXX_USE_CXX11_ABI 1" GCC_NEW_CXX11_ABI)
    if(NOT (${GCC_OLD_CXX11_ABI} EQUAL -1))
      set(GLIBCXX_USE_CXX11_ABI 0)
    endif()
    if(NOT (${GCC_NEW_CXX11_ABI} EQUAL -1))
      set(GLIBCXX_USE_CXX11_ABI 1)
    endif()
  endif()

  # Try and query the Houdini CXX11 ABI. Allow it to be provided by users to
  # override this logic should Houdini's CMake ever change

  if(NOT DEFINED HOUDINI_CXX11_ABI)
    get_target_property(houdini_interface_compile_options
      Houdini INTERFACE_COMPILE_OPTIONS)
    set(HOUDINI_CXX11_ABI "UNKNOWN")
    if("-D_GLIBCXX_USE_CXX11_ABI=0" IN_LIST houdini_interface_compile_options)
      set(HOUDINI_CXX11_ABI 0)
    elseif("-D_GLIBCXX_USE_CXX11_ABI=1" IN_LIST houdini_interface_compile_options)
      set(HOUDINI_CXX11_ABI 1)
    endif()
  endif()

  message(STATUS "  GNU CXX11 ABI     : ${GLIBCXX_USE_CXX11_ABI}")
  message(STATUS "  Houdini CXX11 ABI : ${HOUDINI_CXX11_ABI}")

  if(${HOUDINI_CXX11_ABI} STREQUAL "UNKNOWN")
    message(WARNING "Unable to determine Houdini CXX11 ABI. Assuming newer ABI "
      "has been used.")
    set(HOUDINI_CXX11_ABI 1)
  endif()

  if(${GLIBCXX_USE_CXX11_ABI} EQUAL ${HOUDINI_CXX11_ABI})
    message(STATUS "  Current CXX11 ABI matches Houdini configuration "
      "(_GLIBCXX_USE_CXX11_ABI=${HOUDINI_CXX11_ABI}).")
  else()
    message(WARNING "A potential mismatch was detected between the CXX11 ABI "
      "of libstdc++ and Houdini. The following ABI configuration will be used: "
      "-D_GLIBCXX_USE_CXX11_ABI=${HOUDINI_CXX11_ABI}. See: "
      "https://gcc.gnu.org/onlinedocs/libstdc++/manual/using_dual_abi.html and "
      "https://vfxplatform.com/#footnote-gcc6 for more information.")
  endif()

  add_definitions(-D_GLIBCXX_USE_CXX11_ABI=${HOUDINI_CXX11_ABI})
endif()

