// Copyright Contributors to the OpenVDB Project
// SPDX-License-Identifier: MPL-2.0
//
/// @file   Count.h
///
/// @brief Functions to count tiles, nodes or voxels in a grid
///
/// @author Dan Bailey
///

#ifndef OPENVDB_TOOLS_COUNT_EXAMPLE_HAS_BEEN_INCLUDED
#define OPENVDB_TOOLS_COUNT_EXAMPLE_HAS_BEEN_INCLUDED

#include <openvdb/version.h>
#include <openvdb/openvdb.h>
#include <openvdb/math/Stats.h>
#include <openvdb/tree/LeafManager.h>
#include <openvdb/tree/NodeManager.h>

namespace openvdb {
OPENVDB_USE_VERSION_NAMESPACE
namespace OPENVDB_VERSION_NAME {

namespace tree { class TreeBase; }

namespace tools {

/// @brief Return the total amount of memory in bytes occupied by this tree.
template <typename TreeT>
Index64 memoryUsage(const TreeT& tree, bool threaded = true);


////////////////////////////////////////

namespace count_internal {

/// @brief A DynamicNodeManager operator to sum the number of bytes of memory used
template<typename TreeType>
struct MemoryUsageOp
{
    using RootT = typename TreeType::RootNodeType;
    using LeafT = typename TreeType::LeafNodeType;

    MemoryUsageOp() = default;
    MemoryUsageOp(const MemoryUsageOp&, tbb::split) { }

    // accumulate size of the root node in bytes
    bool operator()(const RootT& root, size_t)
    {
        count += sizeof(root);
        return true;
    }

    // accumulate size of all child nodes in bytes
    template<typename NodeT>
    bool operator()(const NodeT& node, size_t)
    {
        count += NodeT::NUM_VALUES * sizeof(typename NodeT::UnionType) +
            node.getChildMask().memUsage() + node.getValueMask().memUsage() +
            sizeof(Coord);
        return true;
    }

    // accumulate size of leaf node in bytes
    bool operator()(const LeafT& leaf, size_t)
    {
        count += leaf.memUsage();
        return false;
    }

    void join(const MemoryUsageOp& other)
    {
        count += other.count;
    }

    openvdb::Index64 count{0};
}; // struct MemoryUsageOp



Index64 memoryUsageUntyped(const tree::TreeBase& tree, bool threaded);

template <typename TreeT, typename std::enable_if<TreeTypes::Contains<TreeT>, bool>::type = true>
Index64 memoryUsage(const TreeT& tree, bool threaded)
{
    return memoryUsageUntyped(static_cast<const tree::TreeBase&>(tree), threaded);
}

template <typename TreeT, typename std::enable_if<!TreeTypes::Contains<TreeT>, bool>::type = true>
Index64 memoryUsage(const TreeT& tree, bool threaded)
{
    count_internal::MemoryUsageOp<TreeT> op;
    tree::DynamicNodeManager<const TreeT> nodeManager(tree);
    nodeManager.reduceTopDown(op, threaded);
    return op.count + sizeof(tree);
}

} // count_internal


template <typename TreeT>
Index64 memoryUsage(const TreeT& tree, bool threaded)
{
    return count_internal::memoryUsage<TreeT>(tree, threaded);
}

} // namespace tools
} // namespace OPENVDB_VERSION_NAME
} // namespace openvdb

#endif // OPENVDB_TOOLS_COUNT_EXAMPLE_HAS_BEEN_INCLUDED
