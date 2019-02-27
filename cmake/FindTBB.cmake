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
#[=======================================================================[

FindTBB
---------

Find TBB include dirs and ilmimf library::

  TBB_FOUND                - True if headers and requested libraries were found
  TBB_INCLUDE_DIRS         - TBB include directories
  TBB_LIBRARY_DIRS         - Link directories for TBB libraries
  Tbb_TBB_LIBRARY          - TBB libraries
  Tbb_TBBMALLOC_LIBRARY    - TBB malloc libraries (Mulitple Rendering Context)

This module reads hints about search locations from variables::

  TBB_ROOT                 - Preferred installation prefix
  TBB_INCLUDEDIR           - Preferred include directory e.g. <prefix>/include
  TBB_LIBRARYDIR           - Preferred library directory e.g. <prefix>/lib
  SYSTEM_LIBRARY_PATHS     - Paths appended to all include and lib searches

#]=======================================================================]

FIND_PACKAGE ( PackageHandleStandardArgs )

# Append TBB_ROOT or $ENV{TBB_ROOT} if set (prioritize the direct cmake var)
SET ( _TBB_ROOT_SEARCH_DIR "" )

IF ( TBB_ROOT )
  LIST ( APPEND _TBB_ROOT_SEARCH_DIR ${TBB_ROOT} )
ELSE ()
  SET ( _ENV_TBB_ROOT $ENV{TBB_ROOT} )
  IF ( _ENV_TBB_ROOT )
    LIST ( APPEND _TBB_ROOT_SEARCH_DIR ${_ENV_TBB_ROOT} )
  ENDIF ()
ENDIF ()

# ------------------------------------------------------------------------
#  Search for TBB include DIR
# ------------------------------------------------------------------------

# Skip if TBB_INCLUDE_DIR has been manually provided

IF ( NOT TBB_INCLUDE_DIRS )
  SET ( _TBB_INCLUDE_SEARCH_DIRS "" )

  # Append to _TBB_INCLUDE_SEARCH_DIRS in priority order

  IF ( TBB_INCLUDEDIR )
    LIST ( APPEND _TBB_INCLUDE_SEARCH_DIRS ${TBB_INCLUDEDIR} )
  ENDIF ()
  LIST ( APPEND _TBB_INCLUDE_SEARCH_DIRS ${_TBB_ROOT_SEARCH_DIR} )
  LIST ( APPEND _TBB_INCLUDE_SEARCH_DIRS ${SYSTEM_LIBRARY_PATHS} )

  # Look for a standard TBB header file.
  FIND_PATH ( TBB_INCLUDE_DIRS include/tbb/tbb.h
    NO_DEFAULT_PATH
    PATHS ${_TBB_INCLUDE_SEARCH_DIRS}
    PATH_SUFFIXES include
    )
ENDIF ()

# ------------------------------------------------------------------------
#  Search for TBB lib DIR
# ------------------------------------------------------------------------

SET ( _TBB_LIBRARYDIR_SEARCH_DIRS "" )

# Append to _TBB_LIBRARYDIR_SEARCH_DIRS in priority order

IF ( TBB_LIBRARYDIR )
  LIST ( APPEND _TBB_LIBRARYDIR_SEARCH_DIRS ${TBB_LIBRARYDIR} )
ENDIF ()
LIST ( APPEND _TBB_LIBRARYDIR_SEARCH_DIRS ${_TBB_ROOT_SEARCH_DIR} )
LIST ( APPEND _TBB_LIBRARYDIR_SEARCH_DIRS ${SYSTEM_LIBRARY_PATHS} )

SET ( TBB_PATH_SUFFIXES
  lib64
  lib
)

SET ( _TBB_ORIG_CMAKE_FIND_LIBRARY_SUFFIXES ${CMAKE_FIND_LIBRARY_SUFFIXES} )

# platform branching

IF (APPLE)
  IF (TBB_FOR_CLANG)
    LIST ( INSERT TBB_PATH_SUFFIXES 0 lib/libc++ )
  ENDIF ()
  SET ( CMAKE_FIND_LIBRARY_SUFFIXES ".dylib" )
ELSEIF ( WIN32 )
  IF ( MSVC10 )
    SET ( TBB_VC_DIR vc10 )
  ELSEIF ( MSVC11 )
    SET ( TBB_VC_DIR vc11 )
  ELSEIF ( MSVC12 )
    SET ( TBB_VC_DIR vc12 )
  ENDIF ()
  LIST ( INSERT TBB_PATH_SUFFIXES 0 lib/intel64/${TBB_VC_DIR} )
ELSE ()
  IF ( ${CMAKE_CXX_COMPILER_ID} STREQUAL GNU )
    IF ( TBB_MATCH_COMPILER_VERSION )
      STRING ( REGEX MATCHALL "[0-9]+" GCC_VERSION_COMPONENTS ${CMAKE_CXX_COMPILER_VERSION} )
      LIST ( GET GCC_VERSION_COMPONENTS 0 GCC_MAJOR )
      LIST ( GET GCC_VERSION_COMPONENTS 1 GCC_MINOR )
      LIST ( INSERT TBB_PATH_SUFFIXES 0 lib/intel64/gcc${GCC_MAJOR}.${GCC_MINOR} )
    ELSE ()
      LIST ( INSERT TBB_PATH_SUFFIXES 0 lib/intel64/gcc4.4 )
    ENDIF ()
    LIST ( INSERT TBB_PATH_SUFFIXES 1 lib/x86_64-linux-gnu )
  ELSE ()
    MESSAGE ( FATAL_ERROR "Can't handle non-GCC compiler")
  ENDIF ()
ENDIF ()

FIND_LIBRARY ( Tbb_TBB_LIBRARY tbb
  NO_DEFAULT_PATH
  PATHS ${_TBB_LIBRARYDIR_SEARCH_DIRS}
  PATH_SUFFIXES ${TBB_PATH_SUFFIXES}
)

FIND_LIBRARY ( Tbb_TBBMALLOC_LIBRARY tbbmalloc
  NO_DEFAULT_PATH
  PATHS ${_TBB_LIBRARYDIR_SEARCH_DIRS}
  PATH_SUFFIXES ${TBB_PATH_SUFFIXES}
)

# reset lib suffix

SET ( CMAKE_FIND_LIBRARY_SUFFIXES ${_TBB_ORIG_CMAKE_FIND_LIBRARY_SUFFIXES})

GET_FILENAME_COMPONENT ( TBB_LIBRARY_DIRS ${Tbb_TBB_LIBRARY} DIRECTORY )

# ------------------------------------------------------------------------
#  Cache and set TBB_FOUND
# ------------------------------------------------------------------------

FIND_PACKAGE_HANDLE_STANDARD_ARGS ( Tbb
  REQUIRED_VARS TBB_INCLUDE_DIRS TBB_LIBRARY_DIRS Tbb_TBB_LIBRARY Tbb_TBBMALLOC_LIBRARY
  )

IF ( TBB_FOUND )
  SET ( Tbb_INCLUDE_DIR
    ${TBB_INCLUDE_DIRS}
    CACHE STRING "TBB include directory (deprecated)"
  )
  SET ( TBB_INCLUDE_DIRS
    ${TBB_INCLUDE_DIRS}
    CACHE STRING "TBB include directory"
  )
  SET ( TBB_LIBRARY_DIRS ${TBB_LIBRARY_DIRS}
    CACHE STRING "TBB library directory"
  )
  SET ( Tbb_LIBRARY_DIR ${TBB_LIBRARY_DIRS}
    CACHE STRING "TBB library directory (deprecated)"
  )
  SET ( Tbb_TBB_LIBRARY ${Tbb_TBB_LIBRARY}
    CACHE STRING "TBB library"
  )
  SET ( Tbb_TBBMALLOC_LIBRARY ${Tbb_TBBMALLOC_LIBRARY}
    CACHE STRING "TBB Malloc library"
  )
ELSE ()
  MESSAGE ( FATAL_ERROR "Unable to find TBB")
ENDIF ()





