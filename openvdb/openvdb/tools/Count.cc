
#include "CountExample.h"
#include <openvdb/tree/Tree.h>

namespace openvdb {
OPENVDB_USE_VERSION_NAMESPACE
namespace OPENVDB_VERSION_NAME {
namespace tools {

namespace count_internal
{

Index64 memoryUsageUntyped(const tree::TreeBase& tree, bool threaded)
{
    Index64 result;
    const bool success = TreeTypes::apply([&](const auto& typed) {
        result = count_internal::memoryUsage(typed, threaded);
    }, tree);
    assert(success);
    return result;
}

}

} // namespace tools
} // namespace OPENVDB_VERSION_NAME
} // namespace openvdb
