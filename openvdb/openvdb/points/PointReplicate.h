// Copyright Contributors to the OpenVDB Project
// SPDX-License-Identifier: MPL-2.0

/// @author Nick Avramoussis
///
/// @file PointReplicate.h
///
/// @brief

#ifndef OPENVDB_POINTS_POINT_REPLICATE_HAS_BEEN_INCLUDED
#define OPENVDB_POINTS_POINT_REPLICATE_HAS_BEEN_INCLUDED

#include <openvdb/points/PointDataGrid.h>
#include <openvdb/tools/Prune.h>

namespace openvdb {
OPENVDB_USE_VERSION_NAMESPACE
namespace OPENVDB_VERSION_NAME {
namespace points {


/// @brief Replicates points provided in a source grid into a new grid,
///        transfering and creating attributes found in a provided
///        attribute vector. If an attribute doesn't exist, it is ignored.
///        Position is always replicated, leaving the new points exactly
///        over the top of the source points.
/// @todo  Add group transfer support
/// @param source      The source grid to replicate points from
/// @param multiplier  The base number of points to replicate per point
/// @param attributes  Attributes to transfer to the new grid
/// @param scaleAttribute  A scale float attribute which multiplies the base
///                        multiplier to vary the point count per point.
/// @param replicationIndex  When provided, creates a replication attribute
///                          of the given name which holds the replication
///                          index.
inline openvdb::points::PointDataGrid::Ptr
replicate(const openvdb::points::PointDataGrid& source,
          const size_t multiplier,
          const std::vector<std::string>& attributes,
          const std::string& scaleAttribute = "",
          const std::string& replicationIndex = "");

/// @brief  Same as above, but transfers all attributes.
inline openvdb::points::PointDataGrid::Ptr
replicate(const openvdb::points::PointDataGrid& source,
          const size_t multiplier,
          const std::string& scaleAttribute = "",
          const std::string& replicationIndex = "");


////////////////////////////////////////


inline openvdb::points::PointDataGrid::Ptr
replicate(const openvdb::points::PointDataGrid& source,
          const size_t multiplier,
          const std::vector<std::string>& attributes,
          const std::string& scaleAttribute,
          const std::string& replicationIndex)
{
    // The copy iterator, used to transfer array values from the source grid
    // to the target (replicated grid).
    struct CopyIter
    {
        using GetIncrementCB = std::function<Index(const Index)>;

        CopyIter(const Index end, const GetIncrementCB& cb)
            : mIt(0), mEnd(0), mSource(0), mSourceEnd(end), mCallback(cb) {
            mEnd = mCallback(mSource);
         }

        operator bool() const { return mSource < mSourceEnd; }

        CopyIter& operator++()
        {
            ++mIt;
            // If we have hit the end for current source idx, increment the source idx
            // moving end to the new position for the next source with a non zero
            // number of new values
            while (mIt >= mEnd) {
                ++mSource;
                if (*this) mEnd += mCallback(mSource);
                else return *this;
            }

            return *this;
        }

        Index sourceIndex() const { assert(*this); return mSource; }
        Index targetIndex() const { assert(*this); return mIt; }

    private:
        Index mIt, mEnd, mSource;
        const Index mSourceEnd;
        const GetIncrementCB& mCallback;
    }; // struct CopyIter


    // We want the topology and index values of the leaf nodes, but we
    // DON'T want to copy the arrays. This should only shallow copy the
    // descriptor and arrays
    PointDataGrid::Ptr points = source.deepCopy();

    auto iter = source.tree().cbeginLeaf();
    if (!iter) return points;

    const AttributeSet::Descriptor& sourceDescriptor =
        iter->attributeSet().descriptor();

    // verify position

    const size_t ppos = sourceDescriptor.find("P");
    if (ppos == AttributeSet::INVALID_POS) {
        throw std::runtime_error("Unable to find position attribute.");
    }

    // build new dummy attribute set

    AttributeSet::Ptr set;
    std::vector<size_t> attribsToDrop;
    if (!attributes.empty()) {
        for (const auto& nameIdxPair : sourceDescriptor.map()) {
            if (std::find(attributes.begin(), attributes.end(), nameIdxPair.first) != attributes.end()) continue;
            if (nameIdxPair.first == "P") continue;
            attribsToDrop.emplace_back(nameIdxPair.second);
        }
        set.reset(new AttributeSet(iter->attributeSet(), 1));
    }
    else {
        set.reset(new AttributeSet(AttributeSet::Descriptor::create(sourceDescriptor.type(ppos))));
    }

    if (!replicationIndex.empty()) {
        // don't copy replicationIndex attribute if it exists
        // as it'll be overwritten and we create it after
        const size_t replIdxIdx = sourceDescriptor.find(replicationIndex);
        if (replIdxIdx != AttributeSet::INVALID_POS) attribsToDrop.emplace_back(replIdxIdx);
    }
    set->dropAttributes(attribsToDrop);

    // validate replication attributes

    size_t replicationIdx = AttributeSet::INVALID_POS;
    if (!replicationIndex.empty()) {
        set->appendAttribute(replicationIndex, TypedAttributeArray<int32_t>::attributeType());
        replicationIdx = set->descriptor().find(replicationIndex);
    }

    AttributeSet::DescriptorPtr descriptor = set->descriptorPtr();

    const size_t scaleIdx = !scaleAttribute.empty() ?
        sourceDescriptor.find(scaleAttribute) : AttributeSet::INVALID_POS;

    openvdb::tree::LeafManager<const PointDataTree> sourceManager(source.tree());
    openvdb::tree::LeafManager<openvdb::points::PointDataTree> manager(points->tree());

    manager.foreach(
        [&](PointDataTree::LeafNodeType& leaf, size_t pos)
    {
        using ValueType = PointDataTree::ValueType;

        const auto& sourceLeaf = sourceManager.leaf(pos);
        const openvdb::Index64 sourceCount = sourceLeaf.pointCount();
        size_t uniformMultiplier = multiplier;
        AttributeHandle<float>::Ptr scaleHandle(nullptr);
        bool useScale = scaleIdx != AttributeSet::INVALID_POS;
        if (useScale) {
            scaleHandle = AttributeHandle<float>::create
                (sourceLeaf.constAttributeArray(scaleIdx));
        }
        // small lambda that returns the amount of points to generate
        // based on a scale. Should only be called if useScale is true,
        // otherwise the scaleHandle will be reset or null
        auto getPointsToGenerate = [&](const size_t index) -> size_t {
            const float scale = std::max(0.0f, scaleHandle->get(index));
            return static_cast<size_t>
                (math::Round(static_cast<float>(multiplier) * scale));
        };

        // if uniform, update the multiplier and don't bother using the scale attribute

        if (useScale && scaleHandle->isUniform()) {
            uniformMultiplier = getPointsToGenerate(0);
            scaleHandle.reset();
            useScale = false;
        }

        // get the new count and build the new offsets - do this in this loop so we
        // don't have to cache the offset vector. Note that the leaf offsets become
        // invalid until leaf.replaceAttributeSet is called and should not be used

        openvdb::Index64 total = 0;

        if (useScale) {
            for (auto iter = sourceLeaf.cbeginValueAll(); iter; ++iter) {
                for (auto piter = sourceLeaf.beginIndexVoxel(iter.getCoord());
                     piter; ++piter) { total += getPointsToGenerate(*piter); }
                leaf.setOffsetOnly(iter.pos(), total);
            }
        }
        else {
            total = uniformMultiplier * sourceCount;

            // with a uniform multiplier, just multiply each voxel value
            auto* data = leaf.buffer().data();
            for (size_t i = 0; i < leaf.buffer().size(); ++i) {
                const ValueType::IntType value = data[i];
                data[i] = value * uniformMultiplier;
            }
        }

        // turn voxels off if no points
        leaf.updateValueMask();
        const AttributeSet& sourceSet = sourceLeaf.attributeSet();

        std::unique_ptr<openvdb::points::AttributeSet> newSet
            (new AttributeSet(*set, total));
        auto copy = [&](const std::string& name)
        {
            const auto* sourceArray = sourceSet.getConst(name);
            assert(sourceArray);
            // manually expand so that set() doesn't expand and fill the array.
            // there's no function signature for setUnsafe() which takes a source array
            auto* array = newSet->get(name);
            assert(array);
            array->expand(/*fill*/false);

            if (useScale) {
                const CopyIter iter(sourceCount, [&](const Index i) { return getPointsToGenerate(i); });
                array->copyValues(*sourceArray, iter);
            }
            else {
                const CopyIter iter(sourceCount, [&](const Index) { return uniformMultiplier; });
                array->copyValues(*sourceArray, iter);
            }
        };
        copy("P");
        for (const auto& iter : descriptor->map()) {
            if (iter.first == "P")              continue;
            if (iter.first == replicationIndex) continue;
            copy(iter.first);
        }

        // assign the replication idx if requested

        if (replicationIdx != AttributeSet::INVALID_POS && total > 0) {
            AttributeWriteHandle<int32_t>
                idxHandle(*newSet->get(replicationIdx), /*expand*/false);
            idxHandle.expand(/*fill*/false);
            assert(idxHandle.size() == total);

            if (useScale) {
                size_t offset = 0;
                for (size_t i = 0; i < sourceCount; ++i) {
                    const size_t pointRepCount = getPointsToGenerate(i);
                    for (size_t j = 0; j < pointRepCount; ++j, ++offset) {
                        idxHandle.set(offset, j);
                    }
                }
            }
            else {
                for (size_t i = 0; i < total;) {
                    for (size_t j = 0; j < uniformMultiplier; ++j, ++i) {
                        idxHandle.set(i, j);
                    }
                }
            }
        }
        leaf.replaceAttributeSet(newSet.release(), /*mismatch*/true);
    });

    if (!scaleAttribute.empty()) {
        tools::pruneInactive(points->tree());
    }

    return points;
}

inline openvdb::points::PointDataGrid::Ptr
replicate(const openvdb::points::PointDataGrid& source,
          const size_t multiplier,
          const std::string& scaleAttribute,
          const std::string& replicationIndex)
{
    auto iter = source.tree().cbeginLeaf();
    if (!iter) return source.deepCopy();

    const openvdb::points::AttributeSet::Descriptor& sourceDescriptor =
        iter->attributeSet().descriptor();

    std::vector<std::string> attribs;
    attribs.reserve(sourceDescriptor.map().size());
    for (const auto& namepos : sourceDescriptor.map()) {
        attribs.emplace_back(namepos.first);
    }

    return replicate(source, multiplier, attribs, scaleAttribute, replicationIndex);
}


} // namespace points
} // namespace OPENVDB_VERSION_NAME
} // namespace openvdb

#endif // OPENVDB_POINTS_POINT_REPLICATE_HAS_BEEN_INCLUDED
