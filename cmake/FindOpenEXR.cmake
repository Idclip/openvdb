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

# -*- cmake -*-
# - Find OpenEXR
#
# Author : Nicholas Yue yue.nicholas@gmail.com
#
# This module will define the following variables:
#  OPENEXR_INCLUDE_DIRS - Location of the openexr includes
#  OPENEXR_LIBRARY_DIR - the lib directory containing IlmImf
#  OPENEXR_FOUND - true if OPENEXR was found on the system
#  Openexr_ILMIMF_LIBRARY - OpenEXR's IlmImf library

FIND_PACKAGE ( PackageHandleStandardArgs )

SET ( OPENEXR_CONFIG_FILE include/OpenEXR/OpenEXRConfig.h
  CACHE STRING "The config file defining OpenEXR's version and used to detect the include installation path."
)

FIND_PATH ( OPENEXR_LOCATION ${OPENEXR_CONFIG_FILE}
  NO_DEFAULT_PATH
  NO_SYSTEM_ENVIRONMENT_PATH
  PATHS $ENV{OPENEXR_ROOT} ${SYSTEM_LIBRARY_PATHS}
  )

FIND_PACKAGE_HANDLE_STANDARD_ARGS ( OpenEXR
  REQUIRED_VARS OPENEXR_LOCATION
  )

IF ( OPENEXR_FOUND )

  FILE ( STRINGS "${OPENEXR_LOCATION}/${OPENEXR_CONFIG_FILE}" _openexr_version_major_string REGEX "#define OPENEXR_VERSION_MAJOR ")
  STRING ( REGEX REPLACE "#define OPENEXR_VERSION_MAJOR" "" _openexr_version_major_unstrip "${_openexr_version_major_string}")
  STRING ( STRIP "${_openexr_version_major_unstrip}" OPENEXR_VERSION_MAJOR )

  FILE ( STRINGS "${OPENEXR_LOCATION}/${OPENEXR_CONFIG_FILE}" _openexr_version_minor_string REGEX "#define OPENEXR_VERSION_MINOR ")
  STRING ( REGEX REPLACE "#define OPENEXR_VERSION_MINOR" "" _openexr_version_minor_unstrip "${_openexr_version_minor_string}")
  STRING ( STRIP "${_openexr_version_minor_unstrip}" OPENEXR_VERSION_MINOR )

  MESSAGE ( STATUS "Found OpenEXR v${OPENEXR_VERSION_MAJOR}.${OPENEXR_VERSION_MINOR} at ${OPENEXR_LOCATION}" )

  IF ( OPENEXR_NAMESPACE_VERSIONING )
    SET ( ILMIMF_LIBRARY_NAME IlmImf-${OPENEXR_VERSION_MAJOR}_${OPENEXR_VERSION_MINOR} )
  ELSE ( OPENEXR_NAMESPACE_VERSIONING )
    SET ( ILMIMF_LIBRARY_NAME IlmImf )
  ENDIF ( OPENEXR_NAMESPACE_VERSIONING )

  SET ( OPENEXR_BASE_LIB_DIRECTORIES
    ${OPENEXR_LOCATION}
    ${SYSTEM_LIBRARY_PATHS}
  )

  IF (Openexr_USE_STATIC_LIBS)
    IF (APPLE)
      SET(CMAKE_FIND_LIBRARY_SUFFIXES ".a")
      FIND_LIBRARY ( Openexr_ILMIMF_LIBRARY ${ILMIMF_LIBRARY_NAME}
        PATHS ${OPENEXR_BASE_LIB_DIRECTORIES}
        PATH_SUFFIXES lib64 lib
      )
    ELSEIF (WIN32)
      # Link library
      SET(CMAKE_FIND_LIBRARY_SUFFIXES ".lib")
      FIND_LIBRARY ( Openexr_ILMIMF_LIBRARY ${ILMIMF_LIBRARY_NAME}
        PATHS ${OPENEXR_BASE_LIB_DIRECTORIES}
        PATH_SUFFIXES lib64 lib
      )
    ELSE (APPLE)
      # MESSAGE ( "CMAKE_FIND_LIBRARY_SUFFIXES = " ${CMAKE_FIND_LIBRARY_SUFFIXES})
      SET ( ORIGINAL_CMAKE_FIND_LIBRARY_SUFFIXES ${CMAKE_FIND_LIBRARY_SUFFIXES})
      SET ( CMAKE_FIND_LIBRARY_SUFFIXES ".a")
      FIND_LIBRARY ( Openexr_ILMIMF_LIBRARY ${ILMIMF_LIBRARY_NAME}
        PATHS ${OPENEXR_BASE_LIB_DIRECTORIES}
        PATH_SUFFIXES lib64 lib
        NO_DEFAULT_PATH
        NO_SYSTEM_ENVIRONMENT_PATH
        )
      SET ( CMAKE_FIND_LIBRARY_SUFFIXES ${ORIGINAL_CMAKE_FIND_LIBRARY_SUFFIXES})
    ENDIF (APPLE)
  ELSE ()
    IF (APPLE)
      SET(CMAKE_FIND_LIBRARY_SUFFIXES ".dylib")
      FIND_LIBRARY ( Openexr_ILMIMF_LIBRARY ${ILMIMF_LIBRARY_NAME}
        PATHS ${OPENEXR_BASE_LIB_DIRECTORIES}
        PATH_SUFFIXES lib64 lib
      )
    ELSEIF (WIN32)
      # Link library
      SET(CMAKE_FIND_LIBRARY_SUFFIXES ".lib")
      FIND_LIBRARY ( Openexr_ILMIMF_LIBRARY ${ILMIMF_LIBRARY_NAME}
        PATHS ${OPENEXR_BASE_LIB_DIRECTORIES}
        PATH_SUFFIXES lib64 lib
      )
      # Load library
      SET(CMAKE_FIND_LIBRARY_SUFFIXES ".dll")
      FIND_LIBRARY ( Openexr_ILMIMF_DLL ${ILMIMF_LIBRARY_NAME}
        PATHS ${OPENEXR_LOCATION}/bin
        )
      # MUST reset
      SET(CMAKE_FIND_LIBRARY_SUFFIXES ".lib")
    ELSE (APPLE)
      FIND_LIBRARY ( Openexr_ILMIMF_LIBRARY ${ILMIMF_LIBRARY_NAME}
        PATHS ${OPENEXR_BASE_LIB_DIRECTORIES}
        PATH_SUFFIXES lib64 lib
        NO_DEFAULT_PATH
        NO_CMAKE_ENVIRONMENT_PATH
        NO_CMAKE_PATH
        NO_SYSTEM_ENVIRONMENT_PATH
        NO_CMAKE_SYSTEM_PATH
      )
    ENDIF (APPLE)
  ENDIF ()

  GET_FILENAME_COMPONENT ( OPENEXR_LIBRARY_DIR ${Openexr_ILMIMF_LIBRARY} DIRECTORY CACHE )
  SET ( OPENEXR_INCLUDE_DIRS
    ${OPENEXR_LOCATION}/include
    ${OPENEXR_LOCATION}/include/OpenEXR
    CACHE STRING "Openexr include directories"
  )

ENDIF ( OPENEXR_FOUND )
