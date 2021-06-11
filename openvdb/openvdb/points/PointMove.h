// Copyright Contributors to the OpenVDB Project
// SPDX-License-Identifier: MPL-2.0

/// @author Dan Bailey
///
/// @file PointMove.h
///
/// @brief Ability to move VDB Points using a custom deformer.
///
/// Deformers used when moving points are in world space by default and must adhere
/// to the interface described in the example below:
/// @code
/// struct MyDeformer
/// {
///     // A reset is performed on each leaf in turn before the points in that leaf are
///     // deformed. A leaf and leaf index (standard leaf traversal order) are supplied as
///     // the arguments, which matches the functor interface for LeafManager::foreach().
///     template <typename LeafNoteType>
///     void reset(LeafNoteType& leaf, size_t idx);
///
///     // Evaluate the deformer and modify the given position to generate the deformed
///     // position. An index iterator is supplied as the argument to allow querying the
///     // point offset or containing voxel coordinate.
///     template <typename IndexIterT>
///     void apply(Vec3d& position, const IndexIterT& iter) const;
/// };
/// @endcode
///
/// @note The DeformerTraits struct (defined in PointMask.h) can be used to configure
/// a deformer to evaluate in index space.

#ifndef OPENVDB_POINTS_POINT_MOVE_HAS_BEEN_INCLUDED
#define OPENVDB_POINTS_POINT_MOVE_HAS_BEEN_INCLUDED

#include <openvdb/openvdb.h>

#include "PointDataGrid.h"
#include "PointMask.h"

#include <tbb/concurrent_vector.h>
#include <tbb/task_group.h>

#include <algorithm>
#include <iterator> // for std::begin(), std::end()
#include <map>
#include <numeric> // for std::iota()
#include <tuple>
#include <unordered_map>
#include <vector>

class TestPointMove;


namespace openvdb {
OPENVDB_USE_VERSION_NAMESPACE
namespace OPENVDB_VERSION_NAME {
namespace points {

// dummy object for future use
namespace future { struct Advect { }; }


/// @brief Move points in a PointDataGrid using a custom deformer
/// @param points           the PointDataGrid containing the points to be moved.
/// @param deformer         a custom deformer that defines how to move the points.
/// @param filter           an optional index filter
/// @param objectNotInUse   for future use, this object is currently ignored
/// @param threaded         enable or disable threading  (threading is enabled by default)
template <typename PointDataGridT, typename DeformerT, typename FilterT = NullFilter>
inline void movePoints(PointDataGridT& points,
                       DeformerT& deformer,
                       const FilterT& filter = NullFilter(),
                       future::Advect* objectNotInUse = nullptr,
                       bool threaded = true);


/// @brief Move points in a PointDataGrid using a custom deformer and a new transform
/// @param points           the PointDataGrid containing the points to be moved.
/// @param transform        target transform to use for the resulting points.
/// @param deformer         a custom deformer that defines how to move the points.
/// @param filter           an optional index filter
/// @param objectNotInUse   for future use, this object is currently ignored
/// @param threaded         enable or disable threading  (threading is enabled by default)
template <typename PointDataGridT, typename DeformerT, typename FilterT = NullFilter>
inline void movePoints(PointDataGridT& points,
                       const math::Transform& transform,
                       DeformerT& deformer,
                       const FilterT& filter = NullFilter(),
                       future::Advect* objectNotInUse = nullptr,
                       bool threaded = true);


// define leaf index in use as 32-bit
namespace point_move_internal { using LeafIndex = Index32; }


/// @brief A Deformer that caches the resulting positions from evaluating another Deformer
template <typename T>
class CachedDeformer
{
public:
    using LeafIndex = point_move_internal::LeafIndex;
    using Vec3T = typename math::Vec3<T>;
    using LeafVecT = std::vector<Vec3T>;
    using LeafMapT = std::unordered_map<LeafIndex, Vec3T>;

    // Internal data cache to allow the deformer to offer light-weight copying
    struct Cache
    {
        struct Leaf
        {
            /// @brief clear data buffers and reset counter
            void clear() {
                vecData.clear();
                mapData.clear();
                totalSize = 0;
            }

            LeafVecT vecData;
            LeafMapT mapData;
            Index totalSize = 0;
        }; // struct Leaf

        std::vector<Leaf> leafs;
    }; // struct Cache

    /// Cache is expected to be persistent for the lifetime of the CachedDeformer
    explicit CachedDeformer(Cache& cache);

    /// Caches the result of evaluating the supplied point grid using the deformer and filter
    /// @param grid         the points to be moved
    /// @param deformer     the deformer to apply to the points
    /// @param filter       the point filter to use when evaluating the points
    /// @param threaded     enable or disable threading  (threading is enabled by default)
    template <typename PointDataGridT, typename DeformerT, typename FilterT>
    void evaluate(PointDataGridT& grid, DeformerT& deformer, const FilterT& filter,
        bool threaded = true);

    /// Stores pointers to the vector or map and optionally expands the map into a vector
    /// @throw IndexError if idx is out-of-range of the leafs in the cache
    template <typename LeafT>
    void reset(const LeafT& leaf, size_t idx);

    /// Retrieve the new position from the cache
    template <typename IndexIterT>
    void apply(Vec3d& position, const IndexIterT& iter) const;

private:
    friend class ::TestPointMove;

    Cache& mCache;
    const LeafVecT* mLeafVec = nullptr;
    const LeafMapT* mLeafMap = nullptr;
}; // class CachedDeformer


////////////////////////////////////////


namespace point_move_internal {

using IndexArray = std::vector<Index>;

using IndexTriple = std::tuple<LeafIndex, Index, Index>;
using IndexTripleArray = tbb::concurrent_vector<IndexTriple>;
using GlobalPointIndexMap = std::vector<IndexTripleArray>;
using GlobalPointIndexIndices = std::vector<IndexArray>;

using IndexPair = std::pair<Index, Index>;
using IndexPairArray = std::vector<IndexPair>;
using LocalPointIndexMap = std::vector<IndexPairArray>;

using LeafIndexArray = std::vector<LeafIndex>;
using LeafOffsetArray = std::vector<LeafIndexArray>;
using LeafMap = std::unordered_map<Coord, LeafIndex>;


template <typename DeformerT, typename TreeT, typename FilterT>
struct BuildMoveMapsOp
{
    using LeafT = typename TreeT::LeafNodeType;
    using LeafArrayT = std::vector<LeafT*>;
    using LeafManagerT = typename tree::LeafManager<TreeT>;

    BuildMoveMapsOp(const DeformerT& deformer,
                    GlobalPointIndexMap& globalMoveLeafMap,
                    LocalPointIndexMap& localMoveLeafMap,
                    IndexArray& staticLeafs,
                    const LeafMap& targetLeafMap,
                    const math::Transform& targetTransform,
                    const math::Transform& sourceTransform,
                    const FilterT& filter)
        : mDeformer(deformer)
        , mGlobalMoveLeafMap(globalMoveLeafMap)
        , mLocalMoveLeafMap(localMoveLeafMap)
        , mStaticLeafs(staticLeafs)
        , mTargetLeafMap(targetLeafMap)
        , mTargetTransform(targetTransform)
        , mSourceTransform(sourceTransform)
        , mFilter(filter) { }

    void operator()(LeafT& leaf, size_t idx) const
    {
        constexpr bool useIndexSpace = DeformerTraits<DeformerT>::IndexSpace;

        // Don't bother applying any transformations if the transforms match and
        // we're operating purely in index space

        const bool applyTransform = useIndexSpace && mSourceTransform != mTargetTransform;

        DeformerT deformer(mDeformer);
        deformer.reset(leaf, idx);

        // determine source leaf node origin and offset in the source leaf vector

        const Coord& sourceLeafOrigin = leaf.origin();

        // Pull out this leaf node's local move map if it has a matching leaf if the
        // target tree - this is a common case (i.e. points moving between voxels in
        // the same tree) and avoids having to perform a find for every point that
        // exhibits this behaviour

        IndexPairArray* localArray = nullptr;
        {
            const auto iter = mTargetLeafMap.find(sourceLeafOrigin);
            if (iter != mTargetLeafMap.cend()) {
                localArray = &(mLocalMoveLeafMap[iter->second]);
            }
        }

        AttributeWriteHandle<Vec3f> sourceHandle(leaf.attributeArray("P"));

        // If the transforms are the same and no points in the leaf change voxel
        // then we provisionally mark this leaf as "static". Later we must determine
        // if any points move _into it_ from outside leaves.

        bool isStatic = true;

        FilterT local(mFilter);
        local.reset(leaf);

        for (auto iter = leaf.beginIndexOn(); iter; iter++) {

            if (!local.valid(iter)) {
                // If this point isn't being moved (delete) we have re-alloc this
                // leaf and can't steal it
                isStatic = false;
                continue;
            }

            const Coord coord = iter.getCoord();

            // extract index-space position
            Vec3d positionIS = sourceHandle.get(*iter) + coord.asVec3d();

            if (useIndexSpace) {
                // apply index-space deformation
                deformer.apply(positionIS, iter);
                // only apply index/world transforms if necessary
                if (applyTransform) {
                    positionIS = mTargetTransform.worldToIndex(mSourceTransform.indexToWorld(positionIS));
                }
            }
            else {
                // transform to world-space position and apply world-space deformation
                Vec3d positionWS = mSourceTransform.indexToWorld(positionIS);
                deformer.apply(positionWS, iter);
                // transform to index-space position of target grid
                positionIS = mTargetTransform.worldToIndex(positionWS);
            }

            // determine target voxel and offset

            Coord targetVoxel = Coord::round(positionIS);
            Index targetOffset = LeafT::coordToOffset(targetVoxel);

            // set new local position in source transform space (if point has been deformed)

            const Vec3d voxelPosition(positionIS - targetVoxel.asVec3d());
            sourceHandle.set(*iter, voxelPosition);

            // determine target leaf node origin and offset in the target leaf vector

            Coord targetLeafOrigin = targetVoxel & ~(LeafT::DIM - 1);
            assert(mTargetLeafMap.find(targetLeafOrigin) != mTargetLeafMap.end());

            // insert into move map based on whether point ends up in a new leaf node or not

            if (targetLeafOrigin == sourceLeafOrigin) {
                // stays in current leaf
                assert(localArray);
                localArray->emplace_back(targetOffset, *iter);
                if (isStatic) isStatic &= (targetVoxel == coord);
            }
            else {
                // moves to different leaf
                const LeafIndex targetLeafOffset(mTargetLeafMap.at(targetLeafOrigin));
                mGlobalMoveLeafMap[targetLeafOffset].emplace_back(
                    static_cast<LeafIndex>(idx), targetOffset, *iter);
                isStatic = false;
            }
        }

        mStaticLeafs[idx] = static_cast<Index>(isStatic);
    }

private:
    const DeformerT& mDeformer;
    GlobalPointIndexMap& mGlobalMoveLeafMap;
    LocalPointIndexMap& mLocalMoveLeafMap;
    IndexArray& mStaticLeafs;
    const LeafMap& mTargetLeafMap;
    const math::Transform& mTargetTransform;
    const math::Transform& mSourceTransform;
    const FilterT& mFilter;
}; // struct BuildMoveMapsOp

template <typename LeafT>
inline Index
indexOffsetFromVoxel(const Index voxelOffset, const LeafT& leaf, IndexArray& offsets)
{
    // compute the target point index by summing the point index of the previous
    // voxel with the current number of points added to this voxel, tracked by the
    // offsets array

    Index targetOffset = offsets[voxelOffset]++;
    if (voxelOffset > 0) {
        targetOffset += static_cast<Index>(leaf.getValue(voxelOffset - 1));
    }
    return targetOffset;
}


template <typename TreeT>
struct GlobalMovePointsOp
{
    using LeafT = typename TreeT::LeafNodeType;
    using LeafArrayT = std::vector<LeafT*>;
    using LeafManagerT = typename tree::LeafManager<TreeT>;
    using AttributeArrays = std::vector<AttributeArray*>;

    GlobalMovePointsOp(LeafOffsetArray& offsetMap,
                       const LeafManagerT& sourceLeafManager,
                       const AttributeSet::Descriptor& desc,
                       const GlobalPointIndexMap& moveLeafMap,
                       const GlobalPointIndexIndices& moveLeafIndices)
        : mOffsetMap(offsetMap)
        , mSourceLeafManager(sourceLeafManager)
        , mDescriptor(desc)
        , mMoveLeafMap(moveLeafMap)
        , mMoveLeafIndices(moveLeafIndices) { }

    // A CopyIterator is designed to use the indices in a GlobalPointIndexMap for this leaf
    // and match the interface required for AttributeArray::copyValues()
    struct CopyIterator
    {
        CopyIterator(const LeafT& leaf, const IndexArray& sortedIndices,
            const IndexTripleArray& moveIndices, IndexArray& offsets)
            : mLeaf(leaf)
            , mSortedIndices(sortedIndices)
            , mMoveIndices(moveIndices)
            , mOffsets(offsets) { }

        operator bool() const { return bool(mIt); }

        void reset(Index startIndex, Index endIndex)
        {
            mIndex = startIndex;
            mEndIndex = endIndex;
            this->advance();
        }

        CopyIterator& operator++()
        {
            this->advance();
            return *this;
        }

        Index leafIndex(Index i) const
        {
            if (i < mSortedIndices.size()) {
                return std::get<0>(this->leafIndexTriple(i));
            }
            return std::numeric_limits<Index>::max();
        }

        Index sourceIndex() const
        {
            assert(mIt);
            return std::get<2>(*mIt);
        }

        Index targetIndex() const
        {
            assert(mIt);
            return indexOffsetFromVoxel(std::get<1>(*mIt), mLeaf, mOffsets);
        }

    private:
        void advance()
        {
            if (mIndex >= mEndIndex || mIndex >= mSortedIndices.size()) {
                mIt = nullptr;
            }
            else {
                mIt = &this->leafIndexTriple(mIndex);
            }
            ++mIndex;
        }

        const IndexTriple& leafIndexTriple(Index i) const
        {
            return mMoveIndices[mSortedIndices[i]];
        }

    private:
        const LeafT& mLeaf;
        Index mIndex;
        Index mEndIndex;
        const IndexArray& mSortedIndices;
        const IndexTripleArray& mMoveIndices;
        IndexArray& mOffsets;
        const IndexTriple* mIt = nullptr;
    }; // struct CopyIterator

    void operator()(LeafT& leaf, size_t idx) const
    {
        const IndexTripleArray& moveIndices = mMoveLeafMap[idx];
        if (moveIndices.empty())  return;
        const IndexArray& sortedIndices = mMoveLeafIndices[idx];

        // Store offsets per attribute
        // @todo These will all be computed to be the same - maybe just do
        //   one attribute first then read those offsets?

        std::vector<LeafIndexArray> offsets;
        offsets.resize(mDescriptor.map().size());
        LeafIndexArray* offset = &(offsets.front());

        tbb::task_group tasks;

        for (auto& it : mDescriptor.map()) {
            const size_t index = it.second;

            tasks.run([&, index, offset]() {

                // extract per-voxel offsets for this leaf and set to 0
                offset->resize(LeafT::SIZE, 0);

                // extract target array and ensure data is out-of-core and non-uniform

                auto& targetArray = leaf.attributeArray(index);
                targetArray.loadData();
                targetArray.expand();

                // perform the copy

                CopyIterator copyIterator(leaf, sortedIndices, moveIndices, *offset);

                // use the sorted indices to track the index of the source leaf

                Index sourceLeafIndex = copyIterator.leafIndex(0);
                Index startIndex = 0;

                for (size_t i = 1; i <= sortedIndices.size(); i++) {
                    Index endIndex = static_cast<Index>(i);
                    Index newSourceLeafIndex = copyIterator.leafIndex(endIndex);

                    // when it changes, do a batch-copy of all the indices that lie within this range
                    // TODO: this step could use nested parallelization for cases where there are a
                    // large number of points being moved per attribute

                    if (newSourceLeafIndex > sourceLeafIndex) {
                        copyIterator.reset(startIndex, endIndex);

                        const LeafT& sourceLeaf = mSourceLeafManager.leaf(sourceLeafIndex);
                        const auto& sourceArray = sourceLeaf.constAttributeArray(index);
                        sourceArray.loadData();

                        targetArray.copyValuesUnsafe(sourceArray, copyIterator);

                        sourceLeafIndex = newSourceLeafIndex;
                        startIndex = endIndex;
                    }
                }
            });

            ++offset;
        }

        tasks.wait();

        // Set the main offset array to one of the computed offsets (they will all
        // be the same) for the subsequent local move task

        mOffsetMap[idx] = offsets.front();
    }

private:
    LeafOffsetArray& mOffsetMap;
    const LeafManagerT& mSourceLeafManager;
    const AttributeSet::Descriptor& mDescriptor;
    const GlobalPointIndexMap& mMoveLeafMap;
    const GlobalPointIndexIndices& mMoveLeafIndices;
}; // struct GlobalMovePointsOp


template <typename TreeT>
struct LocalMovePointsOp
{
    using LeafT = typename TreeT::LeafNodeType;
    using LeafArrayT = std::vector<LeafT*>;
    using LeafManagerT = typename tree::LeafManager<TreeT>;
    using AttributeArrays = std::vector<AttributeArray*>;

    LocalMovePointsOp( LeafOffsetArray& offsetMap,
                       const LeafIndexArray& sourceIndices,
                       const LeafManagerT& sourceLeafManager,
                       const AttributeSet::Descriptor& desc,
                       const LocalPointIndexMap& moveLeafMap)
        : mOffsetMap(offsetMap)
        , mSourceIndices(sourceIndices)
        , mSourceLeafManager(sourceLeafManager)
        , mDescriptor(desc)
        , mMoveLeafMap(moveLeafMap) { }

    // A CopyIterator is designed to use the indices in a LocalPointIndexMap for this leaf
    // and match the interface required for AttributeArray::copyValues()
    struct CopyIterator
    {
        CopyIterator(const LeafT& leaf, const IndexPairArray& indices, IndexArray& offsets)
            : mLeaf(leaf)
            , mIndices(indices)
            , mOffsets(offsets) { }

        operator bool() const { return mIndex < static_cast<int>(mIndices.size()); }

        CopyIterator& operator++() { ++mIndex; return *this; }

        Index sourceIndex() const
        {
            return mIndices[mIndex].second;
        }

        Index targetIndex() const
        {
            return indexOffsetFromVoxel(mIndices[mIndex].first, mLeaf, mOffsets);
        }

    private:
        const LeafT& mLeaf;
        const IndexPairArray& mIndices;
        IndexArray& mOffsets;
        int mIndex = 0;
    }; // struct CopyIterator

    void operator()(LeafT& leaf, size_t idx) const
    {
        const IndexPairArray& moveIndices = mMoveLeafMap[idx];
        if (moveIndices.empty())  return;

        // extract source array that has the same origin as the target leaf

        assert(idx < mSourceIndices.size());
        const Index sourceLeafOffset(mSourceIndices[idx]);
        const LeafT& sourceLeaf = mSourceLeafManager.leaf(sourceLeafOffset);

        tbb::task_group tasks;

        for (auto& it : mDescriptor.map()) {
            const size_t index = it.second;

            tasks.run([&, index]() {
                // @todo These will all be computed to be the same - maybe just do
                //   one attribute first then read those offsets?
                LeafIndexArray offsets = mOffsetMap[idx];
                if (offsets.empty()) offsets.resize(LeafT::SIZE, 0);

                const auto& sourceArray = sourceLeaf.constAttributeArray(index);
                sourceArray.loadData();

                // extract target array and ensure data is out-of-core and non-uniform

                auto& targetArray = leaf.attributeArray(index);
                targetArray.loadData();
                targetArray.expand();

                // perform the copy

                CopyIterator copyIterator(leaf, moveIndices, offsets);
                targetArray.copyValuesUnsafe(sourceArray, copyIterator);
            });
        }

        tasks.wait();
    }

private:
    LeafOffsetArray& mOffsetMap;
    const LeafIndexArray& mSourceIndices;
    const LeafManagerT& mSourceLeafManager;
    const AttributeSet::Descriptor& mDescriptor;
    const LocalPointIndexMap& mMoveLeafMap;
}; // struct LocalMovePointsOp

} // namespace point_move_internal


////////////////////////////////////////


template <typename PointDataGridT, typename DeformerT, typename FilterT>
inline void movePoints( PointDataGridT& points,
                        const math::Transform& transform,
                        DeformerT& deformer,
                        const FilterT& filter,
                        future::Advect* objectNotInUse,
                        bool threaded)
{
    using LeafIndex = point_move_internal::LeafIndex;
    using PointDataTreeT = typename PointDataGridT::TreeType;
    using LeafT = typename PointDataTreeT::LeafNodeType;
    using LeafManagerT = typename tree::LeafManager<PointDataTreeT>;

    using namespace point_move_internal;

    // this object is for future use only
    assert(!objectNotInUse);
    (void)objectNotInUse;

    PointDataTreeT& tree = points.tree();

    // early exit if no LeafNodes

    auto iter = tree.cbeginLeaf();

    if (!iter)      return;

    // build voxel topology taking into account any point group deletion

    auto newPoints = point_mask_internal::convertPointsToScalar<PointDataGridT>(
        points, transform, filter, deformer, threaded);
    auto& newTree = newPoints->tree();

    // create leaf managers for both trees

    LeafManagerT sourceLeafManager(tree);
    LeafManagerT targetLeafManager(newTree);

    // extract the existing attribute set
    const auto& existingAttributeSet = points.tree().cbeginLeaf()->attributeSet();

    // build a coord -> index map for looking up target leafs by origin and a faster
    // unordered map for finding the source index from a target index

    LeafMap targetLeafMap;
    LeafIndexArray sourceIndices(targetLeafManager.leafCount(),
        std::numeric_limits<LeafIndex>::max());

    {
        LeafMap sourceLeafMap;

        tbb::task_group tasks;
        tasks.run([&]() {
            sourceLeafMap.reserve(sourceLeafManager.leafCount());
            sourceLeafManager.foreach([&](const auto& leaf, const size_t idx) {
                sourceLeafMap.emplace(leaf.origin(), LeafIndex(idx));
            }, /*threaded=*/false);
        });

        if (!threaded) tasks.wait();
        tasks.run([&]() {
            targetLeafMap.reserve(targetLeafManager.leafCount());
            targetLeafManager.foreach([&](const auto& leaf, const size_t idx) {
                targetLeafMap.emplace(leaf.origin(), LeafIndex(idx));
            }, /*threaded=*/false);
        });

        if (!threaded) tasks.wait();
        tasks.run([&]() {
            AttributeArray::ScopedRegistryLock lock;
            targetLeafManager.foreach(
                [&](LeafT& leaf, size_t) {
                    // map frequency => cumulative histogram
                    auto* buffer = leaf.buffer().data();
                    for (Index i = 1; i < leaf.buffer().size(); i++) {
                        buffer[i] = buffer[i-1] + buffer[i];
                    }
                    // replace attribute set with a copy of the existing one
                    leaf.replaceAttributeSet(
                        new AttributeSet(existingAttributeSet, leaf.getLastValue(), &lock),
                        /*allowMismatchingDescriptors=*/true);
                },
            threaded);
        });

        tasks.wait(); // requires sourceLeafMap
        targetLeafManager.foreach(
            [&](const LeafT& leaf, size_t idx) {
                // store the index of the source leaf in a corresponding target leaf array
                const auto it = sourceLeafMap.find(leaf.origin());
                if (it != sourceLeafMap.end()) {
                    sourceIndices[idx] = it->second;
                }
            },
        threaded);
    }

    // moving leaf

    GlobalPointIndexMap globalMoveLeafMap(targetLeafManager.leafCount());
    LocalPointIndexMap localMoveLeafMap(targetLeafManager.leafCount());

    // This vector will mark the set of leafs in the source tree which are "static".
    // Static leafs are leafs whose voxel data doesn't change during the move, i.e.
    // points can move inside their original voxels, but they can't move into new
    // voxels or have new points move into their voxels from outside the leaf.

    IndexArray staticLeafs(sourceLeafManager.leafCount());

    // build global and local move leaf maps and update local positions

    if (filter.state() == index::ALL) {
        NullFilter nullFilter;
        BuildMoveMapsOp<DeformerT, PointDataTreeT, NullFilter> op(deformer,
            globalMoveLeafMap, localMoveLeafMap, staticLeafs, targetLeafMap,
            transform, points.transform(), nullFilter);
        sourceLeafManager.foreach(op, threaded);
    } else {
        BuildMoveMapsOp<DeformerT, PointDataTreeT, FilterT> op(deformer,
            globalMoveLeafMap, localMoveLeafMap, staticLeafs, targetLeafMap,
            transform, points.transform(), filter);
        sourceLeafManager.foreach(op, threaded);
    }

    // At this point, staticLeafs only marks leafs which don't have points moving
    // out of their original voxels. However, it doesn't mark leafs which may also
    // have points moving into them from other leafs. We now correct this.

    sourceLeafManager.foreach([&](const auto& leaf, const size_t idx) {
        if (!staticLeafs[idx]) return; // not static
        const auto iter = targetLeafMap.find(leaf.origin());
        assert(iter != targetLeafMap.end()); // should exist as it's marked as static
        const Index targetLeafIndex = iter->second;

        if (!globalMoveLeafMap[targetLeafIndex].empty()) {
            // this means that points are moving _into_ this leaf, so it should not be
            // marked as static
            staticLeafs[idx] = 0;
        }
        else {
            // nothing moves into the leaf - as it's already marked as static,
            // nothing moves out or out of voxel bounds either. keep as static
            // and clear the move indices.
            localMoveLeafMap[targetLeafIndex].clear();
        }
    }, threaded);

    // build a sorted index vector for each leaf that references the global move map
    // indices in order of their source leafs and voxels to ensure determinism in the
    // resulting point orders

    GlobalPointIndexIndices globalMoveLeafIndices(globalMoveLeafMap.size());

    targetLeafManager.foreach(
        [&](LeafT& /*leaf*/, size_t idx) {
            const IndexTripleArray& moveIndices = globalMoveLeafMap[idx];
            if (moveIndices.empty())  return;

            IndexArray& sortedIndices = globalMoveLeafIndices[idx];
            sortedIndices.resize(moveIndices.size());
            std::iota(std::begin(sortedIndices), std::end(sortedIndices), 0);
            std::sort(std::begin(sortedIndices), std::end(sortedIndices),
                [&](int i, int j)
                {
                    const Index& indexI0(std::get<0>(moveIndices[i]));
                    const Index& indexJ0(std::get<0>(moveIndices[j]));
                    if (indexI0 < indexJ0)          return true;
                    if (indexI0 > indexJ0)          return false;
                    return std::get<2>(moveIndices[i]) < std::get<2>(moveIndices[j]);
                }
            );
        },
    threaded);

    const auto& descriptor = existingAttributeSet.descriptor();

    {
        LeafOffsetArray offsetMap(targetLeafManager.leafCount());

        // move points between leaf nodes and update the offsetMap

        GlobalMovePointsOp<PointDataTreeT> globalMoveOp(offsetMap,
            sourceLeafManager, descriptor, globalMoveLeafMap, globalMoveLeafIndices);

        targetLeafManager.foreach(globalMoveOp, threaded);

        // move points within leaf nodes

        LocalMovePointsOp<PointDataTreeT> localMoveOp(offsetMap,
            sourceIndices, sourceLeafManager, descriptor, localMoveLeafMap);

        targetLeafManager.foreach(localMoveOp, threaded);
    }

    // start stealing static leaf nodes - this can be done while attributes
    // are being copied as leaf pointers remain consistent, but only for
    // ABI >= 6 (as the older branch uses the sourceLeafManager). This is minor
    // so can be part of the task_group when the branching is removed.

    const auto background = tree.background();
    sourceLeafManager.foreach([&](const auto& leaf, size_t idx) {
        if (!staticLeafs[idx]) return;
        newTree.addLeaf(tree.template stealNode<LeafT>(leaf.origin(), background, false));
    }, /*threaded=*/false);

    points.setTree(newPoints->treePtr());
}


template <typename PointDataGridT, typename DeformerT, typename FilterT>
inline void movePoints( PointDataGridT& points,
                        DeformerT& deformer,
                        const FilterT& filter,
                        future::Advect* objectNotInUse,
                        bool threaded)
{
    movePoints(points, points.transform(), deformer, filter, objectNotInUse, threaded);
}


////////////////////////////////////////


template <typename T>
CachedDeformer<T>::CachedDeformer(Cache& cache)
    : mCache(cache) { }


template <typename T>
template <typename PointDataGridT, typename DeformerT, typename FilterT>
void CachedDeformer<T>::evaluate(PointDataGridT& grid, DeformerT& deformer, const FilterT& filter,
    bool threaded)
{
    using TreeT = typename PointDataGridT::TreeType;
    using LeafT = typename TreeT::LeafNodeType;
    using LeafManagerT = typename tree::LeafManager<TreeT>;
    LeafManagerT leafManager(grid.tree());

    // initialize cache
    auto& leafs = mCache.leafs;
    leafs.resize(leafManager.leafCount());

    const auto& transform = grid.transform();

    // insert deformed positions into the cache

    auto cachePositionsOp = [&](const LeafT& leaf, size_t idx) {

        const Index64 totalPointCount = leaf.pointCount();
        if (totalPointCount == 0)   return;

        // deformer is copied to ensure that it is unique per-thread

        DeformerT newDeformer(deformer);

        newDeformer.reset(leaf, idx);

        auto handle = AttributeHandle<Vec3f>::create(leaf.constAttributeArray("P"));

        auto& cache = leafs[idx];
        cache.clear();

        // only insert into a vector directly if the filter evaluates all points
        // and all points are stored in active voxels
        const bool useVector = filter.state() == index::ALL &&
            (leaf.isDense() || (leaf.onPointCount() == leaf.pointCount()));
        if (useVector) {
            cache.vecData.resize(totalPointCount);
        }

        for (auto iter = leaf.beginIndexOn(filter); iter; iter++) {

            // extract index-space position and apply index-space deformation (if defined)

            Vec3d position = handle->get(*iter) + iter.getCoord().asVec3d();

            // if deformer is designed to be used in index-space, perform deformation prior
            // to transforming position to world-space, otherwise perform deformation afterwards

            if (DeformerTraits<DeformerT>::IndexSpace) {
                newDeformer.apply(position, iter);
                position = transform.indexToWorld(position);
            }
            else {
                position = transform.indexToWorld(position);
                newDeformer.apply(position, iter);
            }

            // insert new position into the cache

            if (useVector) {
                cache.vecData[*iter] = static_cast<Vec3T>(position);
            }
            else {
                cache.mapData.insert({*iter, static_cast<Vec3T>(position)});
            }
        }

        // store the total number of points to allow use of an expanded vector on access

        if (!cache.mapData.empty()) {
            cache.totalSize = static_cast<Index>(totalPointCount);
        }
    };

    leafManager.foreach(cachePositionsOp, threaded);
}


template <typename T>
template <typename LeafT>
void CachedDeformer<T>::reset(const LeafT& /*leaf*/, size_t idx)
{
    if (idx >= mCache.leafs.size()) {
        if (mCache.leafs.empty()) {
            throw IndexError("No leafs in cache, perhaps CachedDeformer has not been evaluated?");
        } else {
            throw IndexError("Leaf index is out-of-range of cache leafs.");
        }
    }
    auto& cache = mCache.leafs[idx];
    if (!cache.mapData.empty()) {
        mLeafMap = &cache.mapData;
        mLeafVec = nullptr;
    }
    else {
        mLeafVec = &cache.vecData;
        mLeafMap = nullptr;
    }
}


template <typename T>
template <typename IndexIterT>
void CachedDeformer<T>::apply(Vec3d& position, const IndexIterT& iter) const
{
    assert(*iter >= 0);

    if (mLeafMap) {
        auto it = mLeafMap->find(*iter);
        if (it == mLeafMap->end())      return;
        position = static_cast<openvdb::Vec3d>(it->second);
    }
    else {
        assert(mLeafVec);

        if (mLeafVec->empty())          return;
        assert(*iter < mLeafVec->size());
        position = static_cast<openvdb::Vec3d>((*mLeafVec)[*iter]);
    }
}


} // namespace points
} // namespace OPENVDB_VERSION_NAME
} // namespace openvdb

#endif // OPENVDB_POINTS_POINT_MOVE_HAS_BEEN_INCLUDED
