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
PROJECT ( OpenVDBBinaries )
CMAKE_MINIMUM_REQUIRED ( VERSION 3.1 )

IF ( NOT OPENVDB_BUILD_CORE )
  SET ( OPENVDB_LIB ${OpenVDB_OPENVDB_LIBRARY} )
  INCLUDE_DIRECTORIES ( ${OpenVDB_INCLUDE_DIR} )
ELSE ()
  SET ( OPENVDB_LIB openvdb_shared)
  INCLUDE_DIRECTORIES ( ../../ )
ENDIF ( NOT OPENVDB_BUILD_CORE )

FIND_PACKAGE ( CPPUnit REQUIRED )
FIND_PACKAGE ( ILMBase REQUIRED )
FIND_PACKAGE ( OpenEXR REQUIRED )
FIND_PACKAGE ( TBB REQUIRED )
FIND_PACKAGE ( ZLIB REQUIRED )

IF ( USE_BLOSC )
  FIND_PACKAGE ( Blosc REQUIRED )
ENDIF ()

IF ( UNIX )
  FIND_PACKAGE ( Threads REQUIRED )
ENDIF ()

# @todo thread is only needed for vdb_view
FIND_PACKAGE ( Boost ${MINIMUM_BOOST_VERSION} REQUIRED COMPONENTS iostreams system )
IF ( WIN32 AND OPENVDB_DISABLE_BOOST_IMPLICIT_LINKING )
  ADD_DEFINITIONS ( -DBOOST_ALL_NO_LIB )
ENDIF ()

INCLUDE_DIRECTORIES ( SYSTEM ${ILMBASE_INCLUDE_DIRS} )
INCLUDE_DIRECTORIES ( SYSTEM ${OPENEXR_INCLUDE_DIRS} )
INCLUDE_DIRECTORIES ( SYSTEM ${Boost_INCLUDE_DIR} )
INCLUDE_DIRECTORIES ( SYSTEM ${TBB_INCLUDE_DIR} )
INCLUDE_DIRECTORIES ( SYSTEM ${PYTHON_INCLUDE_DIR} )
INCLUDE_DIRECTORIES ( SYSTEM ${ZLIB_INCLUDE_DIR} )
INCLUDE_DIRECTORIES ( SYSTEM ${CPPUNIT_INCLUDE_DIR} )

IF ( USE_BLOSC )
  INCLUDE_DIRECTORIES ( SYSTEM ${BLOSC_INCLUDE_DIR} )
ENDIF ()

IF ( WIN32 )
  # Because of implicit linking!
  LINK_DIRECTORIES ( ${Boost_LIBRARY_DIR} )
ENDIF ()

IF ( WIN32 )
  ADD_DEFINITIONS ( -D_WIN32 -DNOMINMAX -DHALF_EXPORTS -DOPENVDB_DLL )
ELSE ()
  ADD_DEFINITIONS ( -pthread -fPIC )
ENDIF ()

##### VDB unit tests

SET ( UNITTEST_SOURCE_FILES
  main.cc
  TestAttributeArray.cc
  TestAttributeArrayString.cc
  TestAttributeGroup.cc
  TestAttributeSet.cc
  TestBBox.cc
  TestConjGradient.cc
  TestCoord.cc
  TestCpt.cc
  TestCurl.cc
  TestDense.cc
  TestDenseSparseTools.cc
  TestDiagnostics.cc
  TestDivergence.cc
  TestDoubleMetadata.cc
  TestExceptions.cc
  TestFile.cc
  TestFloatMetadata.cc
  TestGradient.cc
  TestGrid.cc
  TestGridBbox.cc
  TestGridDescriptor.cc
  TestGridIO.cc
  TestGridTransformer.cc
  TestIndexFilter.cc
  TestIndexIterator.cc
  TestInit.cc
  TestInt32Metadata.cc
  TestInt64Metadata.cc
  TestInternalOrigin.cc
  TestLaplacian.cc
  TestLeaf.cc
  TestLeafBool.cc
  TestLeafManager.cc
  TestLeafMask.cc
  TestLeafIO.cc
  TestLeafOrigin.cc
  TestLevelSetRayIntersector.cc
  TestLevelSetUtil.cc
  TestLinearInterp.cc
  TestMaps.cc
  TestMat4Metadata.cc
  TestMath.cc
  TestMeanCurvature.cc
  TestMeshToVolume.cc
  TestMetadata.cc
  TestMetadataIO.cc
  TestMetaMap.cc
  TestMultiResGrid.cc
  TestName.cc
  TestNodeIterator.cc
  TestNodeManager.cc
  TestNodeMask.cc
  TestParticleAtlas.cc
  TestParticlesToLevelSet.cc
  TestPointAttribute.cc
  TestPointConversion.cc
  TestPointCount.cc
  TestPointDataLeaf.cc
  TestPointGroup.cc
  TestPointIndexGrid.cc
  TestPointPartitioner.cc
  TestPointsToMask.cc
  TestPoissonSolver.cc
  TestPrePostAPI.cc
  TestQuadraticInterp.cc
  TestQuantizedUnitVec.cc
  TestQuat.cc
  TestRay.cc
  TestStats.cc
  TestStream.cc
  TestStreamCompression.cc
  TestStringMetadata.cc
  TestTools.cc
  TestTopologyToLevelSet.cc
  TestTransform.cc
  TestTree.cc
  TestTreeCombine.cc
  TestTreeGetSetValues.cc
  TestTreeIterators.cc
  TestTreeVisitor.cc
  TestTypes.cc
  TestUtil.cc
  TestValueAccessor.cc
  TestVec2Metadata.cc
  TestVec3Metadata.cc
  TestVolumeRayIntersector.cc
  TestVolumeToMesh.cc
  )

ADD_EXECUTABLE ( vdb_test ${UNITTEST_SOURCE_FILES} )

TARGET_LINK_LIBRARIES ( vdb_test
  ${OPENVDB_LIB}
  ${Boost_IOSTREAMS_LIBRARY}
  ${Boost_SYSTEM_LIBRARY}
  ${Tbb_TBB_LIBRARY}
  ${Ilmbase_HALF_LIBRARY}
  ${ZLIB_LIBRARY}
  ${CPPUnit_cppunit_LIBRARY}
  ${CMAKE_THREAD_LIBS_INIT}
  m
  stdc++
)

ADD_TEST ( vdb_unit_test vdb_test )
