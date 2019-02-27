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
PROJECT ( OpenVDB_Houdini )
CMAKE_MINIMUM_REQUIRED ( VERSION 3.1 )

OPTION ( OPENVDB_HOUDINI_SUBDIR "Install plugins to <prefix>/houdini/<version> instead of <prefix>/houdini<version>" OFF )
OPTION ( OPENVDB_HOUDINI_SHORT_VERSION "User major.minor as <version> instead of major.minor.patch" ON )
OPTION ( OPENVDB_HOUDINI_INSTALL_LIBRARY "Install headers and place the shared library in a lib directory." OFF )

IF ( NOT OPENVDB_BUILD_CORE )
  FIND_PACKAGE ( OpenVDB REQUIRED )

  # Set the variables that are otherwise defined in the core module.
  SET ( OPENVDB_MAJOR_VERSION_NUMBER ${OpenVDB_MAJOR_VERSION} )
  SET ( OPENVDB_MINOR_VERSION_NUMBER ${OpenVDB_MINOR_VERSION} )
  SET ( OPENVDB_PATCH_VERSION_NUMBER ${OpenVDB_PATCH_VERSION} )

  # Use the library installed
  SET ( OPENVDB_SHARED_LIB ${OpenVDB_OPENVDB_LIBRARY} )
  INCLUDE_DIRECTORIES ( ${OpenVDB_INCLUDE_DIR} )
ELSE ()
  # Use the target
  SET ( OPENVDB_SHARED_LIB openvdb_shared )
  INCLUDE_DIRECTORIES ( ../ )
ENDIF ( NOT OPENVDB_BUILD_CORE )

FIND_PACKAGE ( HDK REQUIRED )

SET ( HOUDINI_FOLDER_VERSION "${HDK_VERSION_MAJOR}.${HDK_VERSION_MINOR}" )
IF ( NOT OPENVDB_HOUDINI_SHORT_VERSION )
    SET ( HOUDINI_FOLDER_VERSION "${HOUDINI_FOLDER_VERSION}.${HDK_VERSION_BUILD}" )
ENDIF ()

IF ( OPENVDB_HOUDINI_SUBDIR )
    SET ( HOUDINI_SUB_DIRECTORY "houdini/${HOUDINI_FOLDER_VERSION}" )
ELSE ()
    SET ( HOUDINI_SUB_DIRECTORY "houdini${HOUDINI_FOLDER_VERSION}" )
ENDIF ()

SET ( OPENVDB_HOUDINI_INSTALL_BASE_DIR
  ${CMAKE_INSTALL_PREFIX}/${HOUDINI_SUB_DIRECTORY}
  CACHE PATH "Base install path for OpenVDB Houdini nodes and icons."
  )
SET ( OPENVDB_HOUDINI_INSTALL_LIB_BASE_DIR
  ${OPENVDB_HOUDINI_INSTALL_BASE_DIR}
  CACHE PATH "Base install path for OpenVDB Houdini shared libraries and headers."
  )

# Checking if hdk version is >= 16.5
# Houdini 16.5 stopped shipping boost.
IF (( HDK_VERSION_MAJOR GREATER 16 ) OR
    (( HDK_VERSION_MAJOR EQUAL 16 ) AND ( HDK_VERSION_MINOR EQUAL 5 )))

  FIND_PACKAGE ( Boost ${MINIMUM_BOOST_VERSION} REQUIRED )
  INCLUDE_DIRECTORIES ( SYSTEM ${Boost_INCLUDE_DIR} )

ENDIF ()

# This is to work around DWA way of doing stuff - REPEATED WITH VARIATION
FILE ( GLOB HOUDINI_UTILS_HEADER RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} houdini/*.h )

# Copy the files belonging to houdini_utils
SET ( OPENVDB_HOUDINI_UTILS_DIR ${PROJECT_BINARY_DIR}/houdini_utils )
FILE ( MAKE_DIRECTORY ${OPENVDB_HOUDINI_UTILS_DIR} )
FILE ( COPY ${HOUDINI_UTILS_HEADER} DESTINATION ${OPENVDB_HOUDINI_UTILS_DIR}
  FILES_MATCHING
  PATTERN "geometry.h"
  PATTERN "ParmFactory.h"
  PATTERN "OP_NodeChain.h"
  )

# Copy the files not belonging to houdini_utils to openvdb_houdini
SET  ( OPENVDB_HOUDINI_LOCAL_DIR ${PROJECT_BINARY_DIR}/openvdb_houdini )
FILE ( MAKE_DIRECTORY ${OPENVDB_HOUDINI_LOCAL_DIR} )
FILE ( COPY ${HOUDINI_UTILS_HEADER} DESTINATION ${OPENVDB_HOUDINI_LOCAL_DIR}
  PATTERN "geometry.h" EXCLUDE
  PATTERN "ParmFactory.h" EXCLUDE
  PATTERN "OP_NodeChain.h" EXCLUDE
  )

INCLUDE_DIRECTORIES (
  # houdini
  ${OPENVDB_HOUDINI_UTILS_DIR}
  ${OPENVDB_HOUDINI_LOCAL_DIR}
  ${PROJECT_BINARY_DIR}
  )

INCLUDE_DIRECTORIES ( SYSTEM ${HDK_INCLUDE_DIR} )
# MESSAGE ( "HDK_DEFINITIONS = ${HDK_DEFINITIONS}" )

LINK_DIRECTORIES ( $ENV{HFS}/dsolib )

IF (NOT WIN32)
  ADD_DEFINITIONS ( -pthread -fPIC )
ENDIF ()

# RPath handling
IF ( OPENVDB_ENABLE_RPATH )

  # when building, don't use the install RPATH already
  # (but later on when installing)
  SET ( CMAKE_BUILD_WITH_INSTALL_RPATH TRUE )

  # Use $ORIGIN for relative referencing
  # The second $ORIGIN is present for the openvdb_houdini shared library.
  IF ( OPENVDB_HOUDINI_INSTALL_LIBRARY )
    SET ( CMAKE_INSTALL_RPATH "$ORIGIN/../../lib:$ORIGIN/../lib" )
  ELSE ()
    SET ( CMAKE_INSTALL_RPATH "$ORIGIN/../../lib:$ORIGIN" )
  ENDIF ()

ENDIF ( OPENVDB_ENABLE_RPATH )

ADD_LIBRARY ( openvdb_houdini SHARED
  houdini/GEO_PrimVDB.cc
  houdini/GEO_VDBTranslator.cc
  houdini/geometry.cc
  houdini/GeometryUtil.cc
  houdini/GT_GEOPrimCollectVDB.cc
  houdini/GU_PrimVDB.cc
  houdini/GU_VDBPointTools.cc
  houdini/ParmFactory.cc
  houdini/PointUtils.cc
  houdini/SOP_NodeVDB.cc
  houdini/UT_VDBUtils.cc
  houdini/Utils.cc
  )

# @todo use hcustom -m (like other methods in FindHDK.cmake)
TARGET_LINK_LIBRARIES ( openvdb_houdini
  ${OPENVDB_SHARED_LIB}
  HoudiniPRM
  HoudiniGEO
  HoudiniUT
  HoudiniOP2
  HoudiniOP3
  HoudiniAPPS3
  )

SET_TARGET_PROPERTIES(
  openvdb_houdini
  PROPERTIES
  SOVERSION ${OPENVDB_MAJOR_VERSION_NUMBER}.${OPENVDB_MINOR_VERSION_NUMBER}
  VERSION ${OPENVDB_MAJOR_VERSION_NUMBER}.${OPENVDB_MINOR_VERSION_NUMBER}.${OPENVDB_PATCH_VERSION_NUMBER}
  )

SET ( OPENVDB_DSO_NAMES
  GR_PrimVDBPoints
  SHOP_OpenVDB_Points
  SOP_OpenVDB_Advect
  SOP_OpenVDB_Advect_Points
  SOP_OpenVDB_Analysis
  SOP_OpenVDB_Clip
  SOP_OpenVDB_Combine
  SOP_OpenVDB_Convert
  SOP_OpenVDB_Create
  SOP_OpenVDB_Densify
  SOP_OpenVDB_Diagnostics
  SOP_OpenVDB_Fill
  SOP_OpenVDB_Filter
  SOP_OpenVDB_Filter_Level_Set
  SOP_OpenVDB_Fracture
  SOP_OpenVDB_From_Particles
  SOP_OpenVDB_From_Polygons
  SOP_OpenVDB_LOD
  SOP_OpenVDB_Metadata
  SOP_OpenVDB_Morph_Level_Set
  SOP_OpenVDB_Noise
  SOP_OpenVDB_Occlusion_Mask
  SOP_OpenVDB_Platonic
  SOP_OpenVDB_Points_Convert
  SOP_OpenVDB_Points_Delete
  SOP_OpenVDB_Points_Group
  SOP_OpenVDB_Potential_Flow
  SOP_OpenVDB_Prune
  SOP_OpenVDB_Rasterize_Points
  SOP_OpenVDB_Ray
  SOP_OpenVDB_Read
  SOP_OpenVDB_Rebuild_Level_Set
  SOP_OpenVDB_Remap
  SOP_OpenVDB_Remove_Divergence
  SOP_OpenVDB_Resample
  SOP_OpenVDB_Sample_Points
  SOP_OpenVDB_Scatter
  SOP_OpenVDB_Segment
  SOP_OpenVDB_Sort_Points
  SOP_OpenVDB_To_Polygons
  SOP_OpenVDB_To_Spheres
  SOP_OpenVDB_Topology_To_Level_Set
  SOP_OpenVDB_Transform
  SOP_OpenVDB_Vector_Merge
  SOP_OpenVDB_Vector_Split
  SOP_OpenVDB_Visualize
  SOP_OpenVDB_Write
  VRAY_OpenVDB_Points
  )

ADD_CUSTOM_TARGET ( Houdini_DSOs
  COMMENT "Group target which corresponds to all Houdini Nodes and DSOs "
  "which aren't part of the shared library i.e. all targets in OPENVDB_DSO_NAMES."
  )

FOREACH ( DSO_NAME ${OPENVDB_DSO_NAMES} )

  HDK_ADD_LIBRARY ( ${DSO_NAME}
    houdini/${DSO_NAME}.cc
    )

  TARGET_COMPILE_OPTIONS ( ${DSO_NAME} PRIVATE -fext-numeric-literals )

  TARGET_LINK_LIBRARIES ( ${DSO_NAME}
    openvdb_houdini
    ${OPENVDB_SHARED_LIB}
    )

  ADD_DEPENDENCIES ( Houdini_DSOs ${DSO_NAME} )

ENDFOREACH ()

# Additionally link HoudiniRAY to VRAY_OpenVDB_Points

TARGET_LINK_LIBRARIES ( VRAY_OpenVDB_Points
  HoudiniRAY
  )

##########
# Installs
##########

IF ( OPENVDB_HOUDINI_INSTALL_LIBRARY )

  INSTALL ( DIRECTORY
    ${OPENVDB_HOUDINI_UTILS_DIR}
    ${OPENVDB_HOUDINI_LOCAL_DIR}
    DESTINATION ${OPENVDB_HOUDINI_INSTALL_LIB_BASE_DIR}/include
    )

  INSTALL ( TARGETS
    openvdb_houdini
    DESTINATION ${OPENVDB_HOUDINI_INSTALL_LIB_BASE_DIR}/lib
    )

ELSE ()

  INSTALL ( TARGETS
    openvdb_houdini
    DESTINATION ${OPENVDB_HOUDINI_INSTALL_LIB_BASE_DIR}/dso
    )

ENDIF ()

FOREACH ( DSO_NAME ${OPENVDB_DSO_NAMES} )

  INSTALL ( TARGETS
    ${DSO_NAME}
    DESTINATION ${OPENVDB_HOUDINI_INSTALL_BASE_DIR}/dso
    )

ENDFOREACH ()

INSTALL ( TARGETS
  VRAY_OpenVDB_Points
  DESTINATION ${OPENVDB_HOUDINI_INSTALL_BASE_DIR}/dso/mantra
  )

INSTALL ( FILES
  houdini/SOP_OpenVDB.svg
  DESTINATION ${OPENVDB_HOUDINI_INSTALL_BASE_DIR}/config/Icons
  )

INSTALL ( FILES
  houdini/DW_OpenVDBRasterizePoints.cmd
  DESTINATION ${OPENVDB_HOUDINI_INSTALL_BASE_DIR}/scripts/sop
  )
