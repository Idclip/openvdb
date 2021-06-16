
#ifndef TEST_UTIL2
#define TEST_UTIL2

#include <openvdb/openvdb.h>
#include <openvdb/math/Math.h> // for math::Random01
#include <openvdb/tools/Prune.h>// for pruneLevelSet

#include <openvdb/openvdb.h>
#include <openvdb/tools/PointIndexGrid.h>
#include <openvdb/points/PointAttribute.h>
#include <openvdb/points/PointConversion.h>

//////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////

// get 8 corner points from a cube with a given scale
inline std::vector<openvdb::Vec3f> getBoxPoints(const float scale = 1.0)
{
    // This order is configured to be the same layout when
    // a vdb points grid is constructed and so matches methods
    // like setGroup or populateAttribute
    std::vector<openvdb::Vec3f> pos = {
        openvdb::Vec3f(-1.0f, -1.0f, -1.0f),
        openvdb::Vec3f(-1.0f, -1.0f, 1.0f),
        openvdb::Vec3f(-1.0f, 1.0f, -1.0f),
        openvdb::Vec3f(-1.0f, 1.0f, 1.0f),
        openvdb::Vec3f(1.0f, -1.0f, -1.0f),
        openvdb::Vec3f(1.0f, -1.0f, 1.0f),
        openvdb::Vec3f(1.0f, 1.0f, -1.0f),
        openvdb::Vec3f(1.0f, 1.0f, 1.0f)
    };

    for (auto& p : pos) p *= scale;
    return pos;
}

struct PointBuilder
{
    using CallbackT1 = std::function<void(openvdb::points::PointDataTree&, const openvdb::tools::PointIndexTree&)>;
    using CallbackT2 = std::function<void(openvdb::points::PointDataTree&)>;

    PointBuilder(const std::vector<openvdb::Vec3f>& pos) : positions(pos) {}

    PointBuilder& voxelsize(double in) { vs = in; return *this; }
    PointBuilder& group(const std::vector<short>& in,
        const std::string& name = "group")
    {
        callbacks.emplace_back([in, name](openvdb::points::PointDataTree& tree, const openvdb::tools::PointIndexTree& index) {
            openvdb::points::appendGroup(tree, name);
            openvdb::points::setGroup(tree, index, in, name);
        });
        return *this;
    }
    PointBuilder& callback(const CallbackT1& c)
    {
        callbacks.emplace_back(c); return *this;
    }
    PointBuilder& callback(const CallbackT2& c)
    {
        auto wrap = [c](openvdb::points::PointDataTree& tree, const openvdb::tools::PointIndexTree&) { c(tree); };
        callbacks.emplace_back(wrap); return *this;
    }
    openvdb::points::PointDataGrid::Ptr get()
    {
        openvdb::math::Transform::Ptr transform =
            openvdb::math::Transform::createLinearTransform(vs);
        openvdb::points::PointAttributeVector<openvdb::Vec3f> wrap(positions);
        auto index = openvdb::tools::createPointIndexGrid<openvdb::tools::PointIndexGrid>(wrap, vs);
        auto points = openvdb::points::createPointDataGrid<openvdb::points::NullCodec,
                openvdb::points::PointDataGrid>(*index, wrap, *transform);
        for (auto c : callbacks) c(points->tree(), index->tree());
        return points;
    }

private:
    double vs = 0.1;
    std::vector<openvdb::Vec3f> positions = {};
    std::vector<CallbackT1> callbacks = {};
};

#endif // TEST_UTIL2
