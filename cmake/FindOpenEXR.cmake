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

FindOpenEXR
---------

Find OpenEXR include dirs and ilmimf library::

  OPENEXR_FOUND            - True if headers and requested libraries were found
  OPENEXR_INCLUDE_DIRS     - OpenEXR include directories
  OPENEXR_LIBRARY_DIRS     - Link directories for OpenEXR libraries
  Openexr_ILMIMF_LIBRARY   - OpenEXR's IlmImf library
  Openexr_ILMIMF_DLL       - Windows runtime dll

This module reads hints about search locations from variables::

  OPENEXR_ROOT             - Preferred installation prefix
  OPENEXR_INCLUDEDIR       - Preferred include directory e.g. <prefix>/include
  OPENEXR_LIBRARYDIR       - Preferred library directory e.g. <prefix>/lib
  SYSTEM_LIBRARY_PATHS     - Paths appended to all include and lib searches

#]=======================================================================]

FIND_PACKAGE ( PackageHandleStandardArgs )

# Append OPENEXR_ROOT or $ENV{OPENEXR_ROOT} if set (prioritize the direct cmake var)
SET ( _OPENEXR_ROOT_SEARCH_DIR "" )

IF ( OPENEXR_ROOT )
  LIST ( APPEND _OPENEXR_ROOT_SEARCH_DIR ${OPENEXR_ROOT} )
ELSE ()
  SET ( _ENV_OPENEXR_ROOT $ENV{OPENEXR_ROOT} )
  IF ( _ENV_OPENEXR_ROOT )
    LIST ( APPEND _OPENEXR_ROOT_SEARCH_DIR ${_ENV_OPENEXR_ROOT} )
  ENDIF ()
ENDIF ()

# ------------------------------------------------------------------------
#  Search for OpenEXR include DIR
# ------------------------------------------------------------------------

# Skip if OPENEXR_INCLUDE_DIR has been manually provided

IF ( NOT OPENEXR_INCLUDE_DIRS )
  SET ( _OPENEXR_INCLUDE_SEARCH_DIRS "" )

  # Append to _OPENEXR_INCLUDE_SEARCH_DIRS in priority order

  IF ( OPENEXR_INCLUDEDIR )
    LIST ( APPEND _OPENEXR_INCLUDE_SEARCH_DIRS ${OPENEXR_INCLUDEDIR} )
  ENDIF ()
  LIST ( APPEND _OPENEXR_INCLUDE_SEARCH_DIRS ${_OPENEXR_ROOT_SEARCH_DIR} )
  LIST ( APPEND _OPENEXR_INCLUDE_SEARCH_DIRS ${SYSTEM_LIBRARY_PATHS} )

  # Look for a standard OpenEXR header file.
  FIND_PATH ( OPENEXR_INCLUDE_DIRS OpenEXR/OpenEXRConfig.h
    NO_DEFAULT_PATH
    PATHS ${_OPENEXR_INCLUDE_SEARCH_DIRS}
    PATH_SUFFIXES include
    )
ENDIF ()

# Get the EXR version information from the config header

FILE ( STRINGS "${OPENEXR_INCLUDE_DIRS}/OpenEXR/OpenEXRConfig.h"
  _openexr_version_major_string REGEX "#define OPENEXR_VERSION_MAJOR "
  )
STRING ( REGEX REPLACE "#define OPENEXR_VERSION_MAJOR" ""
  _openexr_version_major_string "${_openexr_version_major_string}"
  )
STRING ( STRIP "${_openexr_version_major_string}" OPENEXR_VERSION_MAJOR )

FILE ( STRINGS "${OPENEXR_INCLUDE_DIRS}/OpenEXR/OpenEXRConfig.h"
   _openexr_version_minor_string REGEX "#define OPENEXR_VERSION_MINOR "
  )
STRING ( REGEX REPLACE "#define OPENEXR_VERSION_MINOR" ""
  _openexr_version_minor_string "${_openexr_version_minor_string}"
  )
STRING ( STRIP "${_openexr_version_minor_string}" OPENEXR_VERSION_MINOR )

UNSET ( _openexr_version_major_string )
UNSET ( _openexr_version_minor_string )

SET ( OPENEXR_VERSION ${OPENEXR_VERSION_MAJOR}.${OPENEXR_VERSION_MINOR} )

# ------------------------------------------------------------------------
#  Search for OpenEXR lib DIR
# ------------------------------------------------------------------------

IF ( OPENEXR_NAMESPACE_VERSIONING )
  SET ( ILMIMF_LIBRARY_NAME IlmImf-${OPENEXR_VERSION_MAJOR}_${OPENEXR_VERSION_MINOR} )
ELSE ()
  SET ( ILMIMF_LIBRARY_NAME IlmImf )
ENDIF ()

SET ( _OPENEXR_LIBRARYDIR_SEARCH_DIRS "" )

# Append to _OPENEXR_LIBRARYDIR_SEARCH_DIRS in priority order

IF ( OPENEXR_LIBRARYDIR )
  LIST ( APPEND _OPENEXR_LIBRARYDIR_SEARCH_DIRS ${OPENEXR_LIBRARYDIR} )
ENDIF ()
LIST ( APPEND _OPENEXR_LIBRARYDIR_SEARCH_DIRS ${_OPENEXR_ROOT_SEARCH_DIR} )
LIST ( APPEND _OPENEXR_LIBRARYDIR_SEARCH_DIRS ${SYSTEM_LIBRARY_PATHS} )

# Build suffix directories

SET ( OPENEXR_PATH_SUFFIXES
  lib64
  lib
)

IF ( ${CMAKE_CXX_COMPILER_ID} STREQUAL GNU )
  LIST ( INSERT OPENEXR_PATH_SUFFIXES 0 lib/x86_64-linux-gnu )
ENDIF ()

SET ( _OPENEXR_ORIG_CMAKE_FIND_LIBRARY_SUFFIXES ${CMAKE_FIND_LIBRARY_SUFFIXES} )

# library suffix handling

IF ( WIN32 )
  SET ( CMAKE_FIND_LIBRARY_SUFFIXES ".lib" )
ENDIF ()

IF ( OPENEXR_USE_STATIC_LIBS )
  IF ( UNIX )
    SET ( CMAKE_FIND_LIBRARY_SUFFIXES ".a" )
  ENDIF ()
ELSE ()
  IF ( APPLE )
    SET(CMAKE_FIND_LIBRARY_SUFFIXES ".dylib")
  ENDIF ()
ENDIF ()

FIND_LIBRARY ( Openexr_ILMIMF_LIBRARY ${ILMIMF_LIBRARY_NAME}
  NO_DEFAULT_PATH
  PATHS ${_OPENEXR_LIBRARYDIR_SEARCH_DIRS}
  PATH_SUFFIXES ${OPENEXR_PATH_SUFFIXES}
  )

IF ( NOT OPENEXR_USE_STATIC_LIBS AND WIN32 )
  # Load library
  SET ( CMAKE_FIND_LIBRARY_SUFFIXES ".dll" )
  FIND_LIBRARY ( Openexr_ILMIMF_DLL ${ILMIMF_LIBRARY_NAME}
    NO_DEFAULT_PATH
    PATHS ${_OPENEXR_LIBRARYDIR_SEARCH_DIRS}
    PATH_SUFFIXES bin
    )
ENDIF ()

# reset lib suffix

SET ( CMAKE_FIND_LIBRARY_SUFFIXES ${_OPENEXR_ORIG_CMAKE_FIND_LIBRARY_SUFFIXES})

GET_FILENAME_COMPONENT ( OPENEXR_LIBRARY_DIRS ${Openexr_ILMIMF_LIBRARY} DIRECTORY )

# ------------------------------------------------------------------------
#  Cache and set OPENEXR_FOUND
# ------------------------------------------------------------------------

FIND_PACKAGE_HANDLE_STANDARD_ARGS ( OpenEXR
  REQUIRED_VARS OPENEXR_INCLUDE_DIRS OPENEXR_LIBRARY_DIRS Openexr_ILMIMF_LIBRARY
  VERSION_VAR OPENEXR_VERSION
  )

IF ( OPENEXR_FOUND )
  SET ( OPENEXR_INCLUDE_DIRS
    ${OPENEXR_INCLUDE_DIRS}
    ${OPENEXR_INCLUDE_DIRS}/OpenEXR
    CACHE STRING "Blosc include directory"
  )
  SET ( OPENEXR_LIBRARY_DIRS ${OPENEXR_LIBRARY_DIRS}
    CACHE STRING "Blosc library directory"
  )
  SET ( OPENEXR_ILMIMF_LIBRARY ${OPENEXR_ILMIMF_LIBRARY}
    CACHE STRING "Blosc library"
  )
ELSE ()
  MESSAGE ( FATAL_ERROR "Unable to find OpenEXR")
ENDIF ()

