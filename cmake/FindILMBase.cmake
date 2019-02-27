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

FindILMBase
---------

Find OpenEXR include dirs and ilmimf library::

  ILMBASE_FOUND              - True if headers and requested libraries were found
  ILMBASE_INCLUDE_DIRS       - OpenEXR include directories
  ILMBASE_LIBRARY_DIRS       - Link directories for OpenEXR libraries
  Ilmbase_HALF_LIBRARY       - ILMBASE's Half libraries
  Ilmbase_IEX_LIBRARY        - ILMBASE's Iex libraries
  Ilmbase_IEXMATH_LIBRARY    - ILMBASE's IexMath libraries
  Ilmbase_ILMTHREAD_LIBRARY  - ILMBASE's IlmThread libraries
  Ilmbase_IMATH_LIBRARY      - ILMBASE's Imath libraries
  Ilmbase_HALF_DLL
  Ilmbase_IEX_DLL
  Ilmbase_IEXMATH_DLL
  Ilmbase_ILMTHREAD_DLL
  Ilmbase_IMATH_DLL

This module reads hints about search locations from variables::

  ILMBASE_ROOT             - Preferred installation prefix
  ILMBASE_INCLUDEDIR       - Preferred include directory e.g. <prefix>/include
  ILMBASE_LIBRARYDIR       - Preferred library directory e.g. <prefix>/lib
  SYSTEM_LIBRARY_PATHS     - Paths appended to all include and lib searches

#]=======================================================================]

FIND_PACKAGE ( PackageHandleStandardArgs )

# Append ILMBASE_ROOT or $ENV{ILMBASE_ROOT} if set (prioritize the direct cmake var)
SET ( _ILMBASE_ROOT_SEARCH_DIR "" )

IF ( ILMBASE_ROOT )
  LIST ( APPEND _ILMBASE_ROOT_SEARCH_DIR ${ILMBASE_ROOT} )
ELSE ( _ENV_ILMBASE_ROOT )
  SET ( _ENV_ILMBASE_ROOT $ENV{ILMBASE_ROOT} )
  IF ( _ENV_ILMBASE_ROOT )
    LIST ( APPEND _ILMBASE_ROOT_SEARCH_DIR ${_ENV_ILMBASE_ROOT} )
  ENDIF ()
ENDIF ()

# ------------------------------------------------------------------------
#  Search for ILMBase include DIR
# ------------------------------------------------------------------------

# Skip if ILMBASE_INCLUDE_DIR has been manually provided

IF ( NOT ILMBASE_INCLUDE_DIRS )
  SET ( _ILMBASE_INCLUDE_SEARCH_DIRS "" )

  # Append to _ILMBASE_INCLUDE_SEARCH_DIRS in priority order

  IF ( ILMBASE_INCLUDEDIR )
    LIST ( APPEND _ILMBASE_INCLUDE_SEARCH_DIRS ${ILMBASE_INCLUDEDIR} )
  ENDIF ()
  LIST ( APPEND _ILMBASE_INCLUDE_SEARCH_DIRS ${_ILMBASE_ROOT_SEARCH_DIR} )
  LIST ( APPEND _ILMBASE_INCLUDE_SEARCH_DIRS ${SYSTEM_LIBRARY_PATHS} )

  # Look for a standard OpenEXR header file.
  FIND_PATH ( ILMBASE_INCLUDE_DIRS OpenEXR/IlmBaseConfig.h
    NO_DEFAULT_PATH
    PATHS ${_ILMBASE_INCLUDE_SEARCH_DIRS}
    PATH_SUFFIXES include
    )
ENDIF ()

# Get the ILMBASE version information from the config header

FILE ( STRINGS "${ILMBASE_INCLUDE_DIRS}/OpenEXR/IlmBaseConfig.h"
  _ilmbase_version_major_string REGEX "#define ILMBASE_VERSION_MAJOR "
  )
STRING ( REGEX REPLACE "#define ILMBASE_VERSION_MAJOR" ""
  _ilmbase_version_major_string "${_ilmbase_version_major_string}"
  )
STRING ( STRIP "${_ilmbase_version_major_string}" ILMBASE_VERSION_MAJOR )

FILE ( STRINGS "${ILMBASE_INCLUDE_DIRS}/OpenEXR/IlmBaseConfig.h"
   _ilmbase_version_minor_string REGEX "#define ILMBASE_VERSION_MINOR "
  )
STRING ( REGEX REPLACE "#define ILMBASE_VERSION_MINOR" ""
  _ilmbase_version_minor_string "${_ilmbase_version_minor_string}"
  )
STRING ( STRIP "${_ilmbase_version_minor_string}" ILMBASE_VERSION_MINOR )

UNSET ( _ilmbase_version_major_string )
UNSET ( _ilmbase_version_minor_string )

SET ( ILMBASE_VERSION ${ILMBASE_VERSION_MAJOR}.${ILMBASE_VERSION_MINOR} )

# ------------------------------------------------------------------------
#  Search for ILMBASE lib DIR
# ------------------------------------------------------------------------

SET ( IEX_LIBRARY_NAME Iex )
# @todo don't think IexMath is needed, but it's in the windows build
SET ( IEXMATH_LIBRARY_NAME IexMath )
SET ( ILMTHREAD_LIBRARY_NAME IlmThread )
SET ( IMATH_LIBRARY_NAME Imath )

IF ( ILMBASE_NAMESPACE_VERSIONING )
  SET ( IEX_LIBRARY_NAME ${IEX_LIBRARY_NAME}-${ILMBASE_VERSION_MAJOR}_${ILMBASE_VERSION_MINOR} )
  SET ( IEXMATH_LIBRARY_NAME ${IEXMATH_LIBRARY_NAME}-${ILMBASE_VERSION_MAJOR}_${ILMBASE_VERSION_MINOR} )
  SET ( ILMTHREAD_LIBRARY_NAME ${ILMTHREAD_LIBRARY_NAME}-${ILMBASE_VERSION_MAJOR}_${ILMBASE_VERSION_MINOR} )
  SET ( IMATH_LIBRARY_NAME ${IMATH_LIBRARY_NAME}-${ILMBASE_VERSION_MAJOR}_${ILMBASE_VERSION_MINOR} )
ENDIF ( ILMBASE_NAMESPACE_VERSIONING )


SET ( _ILMBASE_LIBRARYDIR_SEARCH_DIRS "" )

# Append to _ILMBASE_LIBRARYDIR_SEARCH_DIRS in priority order

IF ( ILMBASE_LIBRARYDIR )
  LIST ( APPEND _ILMBASE_LIBRARYDIR_SEARCH_DIRS ${ILMBASE_LIBRARYDIR} )
ENDIF ()
LIST ( APPEND _ILMBASE_LIBRARYDIR_SEARCH_DIRS ${_ILMBASE_ROOT_SEARCH_DIR} )
LIST ( APPEND _ILMBASE_LIBRARYDIR_SEARCH_DIRS ${SYSTEM_LIBRARY_PATHS} )

# Build suffix directories

SET ( ILMBASE_PATH_SUFFIXES
  lib64
  lib
)

IF ( ${CMAKE_CXX_COMPILER_ID} STREQUAL GNU )
  LIST ( INSERT ILMBASE_PATH_SUFFIXES 0 lib/x86_64-linux-gnu )
ENDIF ()

SET ( _ILMBASE_ORIG_CMAKE_FIND_LIBRARY_SUFFIXES ${CMAKE_FIND_LIBRARY_SUFFIXES} )

# library suffix handling

IF ( WIN32 )
  SET ( CMAKE_FIND_LIBRARY_SUFFIXES ".lib" )
ENDIF ()

IF ( ILMBASE_USE_STATIC_LIBS )
  IF ( UNIX )
    SET ( CMAKE_FIND_LIBRARY_SUFFIXES ".a" )
  ENDIF ()
ELSE ()
  IF ( APPLE )
    SET( CMAKE_FIND_LIBRARY_SUFFIXES ".dylib" )
  ENDIF ()
ENDIF ()

FIND_LIBRARY ( Ilmbase_HALF_LIBRARY Half
  NO_DEFAULT_PATH
  PATHS ${_ILMBASE_LIBRARYDIR_SEARCH_DIRS}
  PATH_SUFFIXES ${ILMBASE_PATH_SUFFIXES}
  )
FIND_LIBRARY ( Ilmbase_IEX_LIBRARY ${IEX_LIBRARY_NAME}
  NO_DEFAULT_PATH
  PATHS ${_ILMBASE_LIBRARYDIR_SEARCH_DIRS}
  PATH_SUFFIXES ${ILMBASE_PATH_SUFFIXES}
  )
FIND_LIBRARY ( Ilmbase_ILMTHREAD_LIBRARY ${ILMTHREAD_LIBRARY_NAME}
  NO_DEFAULT_PATH
  PATHS ${_ILMBASE_LIBRARYDIR_SEARCH_DIRS}
  PATH_SUFFIXES ${ILMBASE_PATH_SUFFIXES}
  )
FIND_LIBRARY ( Ilmbase_IMATH_LIBRARY ${IMATH_LIBRARY_NAME}
  NO_DEFAULT_PATH
  PATHS ${_ILMBASE_LIBRARYDIR_SEARCH_DIRS}
  PATH_SUFFIXES ${ILMBASE_PATH_SUFFIXES}
  )

IF ( NOT ILMBASE_USE_STATIC_LIBS AND WIN32 )
  # Load library
  SET ( CMAKE_FIND_LIBRARY_SUFFIXES ".dll" )
  FIND_LIBRARY ( Ilmbase_HALF_DLL Half
    NO_DEFAULT_PATH
    PATHS ${_ILMBASE_LIBRARYDIR_SEARCH_DIRS}
    PATH_SUFFIXES bin
    )
  FIND_LIBRARY ( Ilmbase_IEX_DLL ${IEX_LIBRARY_NAME}
    NO_DEFAULT_PATH
    PATHS ${_ILMBASE_LIBRARYDIR_SEARCH_DIRS}
    PATH_SUFFIXES bin
    )
  FIND_LIBRARY ( Ilmbase_IEXMATH_DLL ${IEXMATH_LIBRARY_NAME}
    NO_DEFAULT_PATH
    PATHS ${_ILMBASE_LIBRARYDIR_SEARCH_DIRS}
    PATH_SUFFIXES bin
    )
  FIND_LIBRARY ( Ilmbase_ILMTHREAD_DLL ${ILMTHREAD_LIBRARY_NAME}
    NO_DEFAULT_PATH
    PATHS ${_ILMBASE_LIBRARYDIR_SEARCH_DIRS}
    PATH_SUFFIXES bin
    )
  FIND_LIBRARY ( Ilmbase_IMATH_DLL ${IMATH_LIBRARY_NAME}
    NO_DEFAULT_PATH
    PATHS ${_ILMBASE_LIBRARYDIR_SEARCH_DIRS}
    PATH_SUFFIXES bin
    )
ENDIF ()

# reset lib suffix

SET ( CMAKE_FIND_LIBRARY_SUFFIXES ${_ILMBASE_ORIG_CMAKE_FIND_LIBRARY_SUFFIXES})

GET_FILENAME_COMPONENT ( ILMBASE_LIBRARY_DIRS ${Ilmbase_HALF_LIBRARY} DIRECTORY )

# ------------------------------------------------------------------------
#  Cache and set ILMBASE_FOUND
# ------------------------------------------------------------------------

FIND_PACKAGE_HANDLE_STANDARD_ARGS ( IlmBase
  REQUIRED_VARS ILMBASE_INCLUDE_DIRS ILMBASE_LIBRARY_DIRS Ilmbase_HALF_LIBRARY Ilmbase_IEX_LIBRARY Ilmbase_ILMTHREAD_LIBRARY Ilmbase_IMATH_LIBRARY
  VERSION_VAR ILMBASE_VERSION
  )

IF ( ILMBASE_FOUND )
  SET ( ILMBASE_INCLUDE_DIRS
    ${ILMBASE_INCLUDE_DIRS}
    ${ILMBASE_INCLUDE_DIRS}/OpenEXR
    CACHE STRING "IlmBase include directory"
  )
  SET ( ILMBASE_LIBRARY_DIRS ${ILMBASE_LIBRARY_DIRS}
    CACHE STRING "IlmBase library directory"
  )
  SET ( OPENEXR_ILMIMF_LIBRARY ${OPENEXR_ILMIMF_LIBRARY}
    CACHE STRING "IlmBase library"
  )
ELSE ()
  MESSAGE ( FATAL_ERROR "Unable to find IlmBase")
ENDIF ()
