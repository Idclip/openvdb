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

FindBlosc
---------

Find Blosc include dirs and libraries::

  BLOSC_FOUND            - True if headers and requested libraries were found
  BLOSC_INCLUDE_DIRS     - Blosc include directories
  BLOSC_LIBRARY_DIRS     - Link directories for Blosc libraries
  BLOSC_LIBRARIES        - Blosc libraries to be linked

This module reads hints about search locations from variables::

  BLOSC_ROOT             - Preferred installation prefix
  BLOSC_INCLUDEDIR       - Preferred include directory e.g. <prefix>/include
  BLOSC_LIBRARYDIR       - Preferred library directory e.g. <prefix>/lib
  SYSTEM_LIBRARY_PATHS   - Paths appended to all include and lib searches

#]=======================================================================]

FIND_PACKAGE ( PackageHandleStandardArgs )

# Append BLOSC_ROOT or $ENV{BLOSC_ROOT} if set (prioritize the direct cmake var)
SET ( _BLOSC_ROOT_SEARCH_DIR "" )

IF ( BLOSC_ROOT )
  LIST ( APPEND _BLOSC_ROOT_SEARCH_DIR ${BLOSC_ROOT} )
ELSE ()
  SET ( _ENV_BLOSC_ROOT $ENV{BLOSC_ROOT} )
  IF ( _ENV_BLOSC_ROOT )
    LIST ( APPEND _BLOSC_ROOT_SEARCH_DIR ${_ENV_BLOSC_ROOT} )
  ENDIF ()
ENDIF ()

# ------------------------------------------------------------------------
#  Search for blosc include DIR
# ------------------------------------------------------------------------

# Skip if BLOSC_INCLUDE_DIR has been manually provided

IF ( NOT BLOSC_INCLUDE_DIR )
  SET ( _BLOSC_INCLUDE_SEARCH_DIRS "" )

  # Append to _BLOSC_INCLUDE_SEARCH_DIRS in priority order

  IF ( BLOSC_INCLUDEDIR )
    LIST ( APPEND _BLOSC_INCLUDE_SEARCH_DIRS ${BLOSC_INCLUDEDIR} )
  ENDIF ()
  LIST ( APPEND _BLOSC_INCLUDE_SEARCH_DIRS ${_BLOSC_ROOT_SEARCH_DIR} )
  LIST ( APPEND _BLOSC_INCLUDE_SEARCH_DIRS ${SYSTEM_LIBRARY_PATHS} )

  # Look for a standard blosc header file.
  FIND_PATH ( BLOSC_INCLUDE_DIR blosc.h
    NO_DEFAULT_PATH
    PATHS ${_BLOSC_INCLUDE_SEARCH_DIRS}
    PATH_SUFFIXES include
    )
ENDIF ()

# ------------------------------------------------------------------------
#  Search for blosc lib DIR
# ------------------------------------------------------------------------

SET ( _BLOSC_LIBRARYDIR_SEARCH_DIRS "" )

# Append to _BLOSC_LIBRARYDIR_SEARCH_DIRS in priority order

IF ( BLOSC_LIBRARYDIR )
  LIST ( APPEND _BLOSC_LIBRARYDIR_SEARCH_DIRS ${BLOSC_LIBRARYDIR} )
ENDIF ()
LIST ( APPEND _BLOSC_LIBRARYDIR_SEARCH_DIRS ${_BLOSC_ROOT_SEARCH_DIR} )
LIST ( APPEND _BLOSC_LIBRARYDIR_SEARCH_DIRS ${SYSTEM_LIBRARY_PATHS} )

# Static library setup
IF ( BLOSC_USE_STATIC_LIBS )
  SET ( _BLOSC_ORIG_CMAKE_FIND_LIBRARY_SUFFIXES ${CMAKE_FIND_LIBRARY_SUFFIXES} )
  IF ( UNIX )
    SET ( CMAKE_FIND_LIBRARY_SUFFIXES ".a" )
  ENDIF ()
ENDIF ()

SET ( BLOSC_PATH_SUFFIXES
  lib64
  lib
)

FIND_LIBRARY ( BLOSC_LIBRARIES blosc
  NO_DEFAULT_PATH
  PATHS ${_BLOSC_LIBRARYDIR_SEARCH_DIRS}
  PATH_SUFFIXES ${BLOSC_PATH_SUFFIXES}
)

IF ( BLOSC_USE_STATIC_LIBS )
  SET ( CMAKE_FIND_LIBRARY_SUFFIXES ${_BLOSC_ORIG_CMAKE_FIND_LIBRARY_SUFFIXES} )
ENDIF ()

GET_FILENAME_COMPONENT ( BLOSC_LIBRARY_DIRS ${BLOSC_LIBRARIES} DIRECTORY )

# ------------------------------------------------------------------------
#  Cache and set BLOSC_FOUND
# ------------------------------------------------------------------------

FIND_PACKAGE_HANDLE_STANDARD_ARGS ( BLOSC
  REQUIRED_VARS BLOSC_INCLUDE_DIR BLOSC_LIBRARY_DIRS BLOSC_LIBRARIES
  )

IF ( BLOSC_FOUND )
  SET ( BLOSC_INCLUDE_DIR
    ${BLOSC_INCLUDE_DIR}
    CACHE STRING "Blosc include directory"
  )
  SET ( BLOSC_LIBRARY_DIRS ${BLOSC_LIBRARY_DIRS}
    CACHE STRING "Blosc library directory"
  )
  SET ( BLOSC_LIBRARIES ${BLOSC_LIBRARIES}
    CACHE STRING "Blosc library"
  )
ELSE ()
  MESSAGE ( FATAL_ERROR "Unable to find Blosc")
ENDIF ()
