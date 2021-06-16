// Copyright Contributors to the OpenVDB Project
// SPDX-License-Identifier: MPL-2.0

#include "util2.h"

#include <cppunit/extensions/HelperMacros.h>
#include <openvdb/openvdb.h>
#include <openvdb/points/PointCount.h>
#include <openvdb/points/PointReplicate.h>

using namespace openvdb;
using namespace openvdb::points;

class TestPointReplicate : public CppUnit::TestFixture
{
public:
    CPPUNIT_TEST_SUITE(TestPointReplicate);
    CPPUNIT_TEST(testReplicate);
    CPPUNIT_TEST(testReplicateScale);
    CPPUNIT_TEST(testReplicateZero);
    CPPUNIT_TEST_SUITE_END();

    void testReplicate();
    void testReplicateScale();
    void testReplicateZero();
}; // class TestPointReplicate

CPPUNIT_TEST_SUITE_REGISTRATION(TestPointReplicate);


////////////////////////////////////////

template <typename ValueT>
inline void
getAttribute(const PointDataGrid& grid,
            const std::string& attr,
            std::vector<ValueT>& values)
{
    for (auto leaf = grid.tree().cbeginLeaf(); leaf; ++leaf) {
        AttributeHandle<ValueT> handle(leaf->constAttributeArray(attr));
        for (auto iter = leaf->beginIndexAll(); iter; ++iter)
            values.emplace_back(handle.get(*iter));
    }
}

inline void
getP(const PointDataGrid& grid,
     std::vector<Vec3f>& values)
{
    for (auto leaf = grid.tree().cbeginLeaf(); leaf; ++leaf) {
        AttributeHandle<Vec3f> handle(leaf->constAttributeArray("P"));
        auto iter = leaf->beginIndexAll();
        for (; iter; ++iter) {
            auto pos = Vec3d(handle.get(*iter)) + iter.getCoord().asVec3d();
            values.emplace_back(grid.indexToWorld(pos));
        }
    }
}

template <typename ValueT>
inline void
checkReplicatedAttribute(const PointDataGrid& grid,
            const std::string& attr,
            const std::vector<ValueT>& originals, // original values
            const size_t ppp)                     // points per point
{
    std::vector<ValueT> results;
    getAttribute(grid, attr, results);
    CPPUNIT_ASSERT_EQUAL(results.size(), originals.size() * ppp);

    auto iter = results.begin();
    for (const auto& o : originals) {
        for (size_t i = 0; i < ppp; ++i, ++iter) {
            CPPUNIT_ASSERT(iter != results.end());
            CPPUNIT_ASSERT_EQUAL(o, *iter);
        }
    }
}

template <typename ValueT>
inline void
checkReplicatedAttribute(const PointDataGrid& grid,
            const std::string& attr,
            const std::vector<ValueT>& originals)
{
    std::vector<ValueT> results;
    getAttribute(grid, attr, results);
    CPPUNIT_ASSERT_EQUAL(results.size(), originals.size());

    auto iter1 = results.begin();
    auto iter2 = originals.begin();
    for (; iter1 != results.end(); ++iter1, ++iter2) {
        CPPUNIT_ASSERT_EQUAL(*iter1, *iter2);
    }
}

template <typename ValueT>
inline void
checkReplicatedAttribute(const PointDataGrid& grid,
            const std::string& attr,
            const ValueT& v)
{
    auto count = points::pointCount(grid.tree());
    const std::vector<ValueT> filled(count, v);
    checkReplicatedAttribute<ValueT>(grid, attr, filled, 1);
}

inline void
checkReplicatedP(const PointDataGrid& grid,
            const std::vector<Vec3f>& originals, // original values
            const size_t ppp)                    // points per point
{
    std::vector<Vec3f> results;
    getP(grid, results);
    CPPUNIT_ASSERT_EQUAL(results.size(), originals.size() * ppp);

    auto iter = results.begin();
    for (const auto& o : originals) {
        for (size_t i = 0; i < ppp; ++i, ++iter) {
            CPPUNIT_ASSERT(iter != results.end());
            CPPUNIT_ASSERT_EQUAL(o, *iter);
        }
    }
}

inline void
checkReplicatedP(const PointDataGrid& grid,
            const Vec3f& v)
{
    auto count = points::pointCount(grid.tree());
    const std::vector<Vec3f> filled(count, v);
    checkReplicatedP(grid, filled, 1);
}

void
TestPointReplicate::testReplicate()
{
    // Test no points
    {
        const auto points = PointBuilder({}).get();
        const auto repl = points::replicate(*points, 2);
        CPPUNIT_ASSERT(repl);
        CPPUNIT_ASSERT(repl->empty());
    }

    // Test 1 to many, only position attribute
    {
        const auto points = PointBuilder({Vec3f(1.0f,-2.0f, 3.0f)}).get();

        // 2 points
        auto repl = points::replicate(*points, 2);
        CPPUNIT_ASSERT(repl);
        CPPUNIT_ASSERT(repl->tree().isValueOn({10,-20,30}));
        CPPUNIT_ASSERT(repl->tree().hasSameTopology(points->tree()));
        CPPUNIT_ASSERT_EQUAL(Index64(1), repl->tree().activeVoxelCount());
        CPPUNIT_ASSERT_EQUAL(Index64(2), points::pointCount(repl->tree()));
        checkReplicatedP(*repl, Vec3f(1.0f,-2.0f, 3.0f));

        // 10 points
        repl = points::replicate(*points, 10);
        CPPUNIT_ASSERT(repl);
        CPPUNIT_ASSERT(repl->tree().isValueOn({10,-20,30}));
        CPPUNIT_ASSERT(repl->tree().hasSameTopology(points->tree()));
        CPPUNIT_ASSERT_EQUAL(Index64(1), repl->tree().activeVoxelCount());
        CPPUNIT_ASSERT_EQUAL(Index64(10), points::pointCount(repl->tree()));
        checkReplicatedP(*repl, Vec3f(1.0f,-2.0f, 3.0f));
    }

    // Test 1 to many, arbitrary attributes
    {
        const auto points = PointBuilder({Vec3f(1.0f,-2.0f, 3.0f)})
            .callback([](PointDataTree& tree, const tools::PointIndexTree& index) {
                const std::vector<int32_t> in = {5};
                const PointAttributeVector<int32_t> rwrap(in);
                appendAttribute<int32_t>(tree, "inttest");
                populateAttribute(tree, index, "inttest", rwrap);
            })
            .callback([](PointDataTree& tree, const tools::PointIndexTree& index) {
                const std::vector<float> in = {0.3f};
                const PointAttributeVector<float> rwrap(in);
                appendAttribute<float>(tree, "floattest");
                populateAttribute(tree, index, "floattest", rwrap);
            })
            .callback([](PointDataTree& tree, const tools::PointIndexTree& index) {
                const std::vector<double> in = {-1.3};
                const PointAttributeVector<double> rwrap(in);
                appendAttribute<double>(tree, "doubletest");
                populateAttribute(tree, index, "doubletest", rwrap);
            })
            .get();

        // 2 points
        auto repl = points::replicate(*points, 2);
        CPPUNIT_ASSERT(repl);
        CPPUNIT_ASSERT(repl->tree().isValueOn({10,-20,30}));
        CPPUNIT_ASSERT(repl->tree().hasSameTopology(points->tree()));
        CPPUNIT_ASSERT_EQUAL(Index64(1), repl->tree().activeVoxelCount());
        CPPUNIT_ASSERT_EQUAL(Index64(2), points::pointCount(repl->tree()));
        checkReplicatedP(*repl, { Vec3f(1.0f,-2.0f, 3.0f) });
        checkReplicatedAttribute(*repl, "inttest", int32_t(5));
        checkReplicatedAttribute(*repl, "floattest", float(0.3f));
        checkReplicatedAttribute(*repl, "doubletest", double(-1.3));

        // 10 points
        repl = points::replicate(*points, 10);
        CPPUNIT_ASSERT(repl);
        CPPUNIT_ASSERT(repl->tree().isValueOn({10,-20,30}));
        CPPUNIT_ASSERT(repl->tree().hasSameTopology(points->tree()));
        CPPUNIT_ASSERT_EQUAL(Index64(1), repl->tree().activeVoxelCount());
        CPPUNIT_ASSERT_EQUAL(Index64(10), points::pointCount(repl->tree()));
        checkReplicatedP(*repl, Vec3f(1.0f,-2.0f, 3.0f));
        checkReplicatedAttribute(*repl, "inttest", int32_t(5));
        checkReplicatedAttribute(*repl, "floattest", float(0.3f));
        checkReplicatedAttribute(*repl, "doubletest", double(-1.3));
    }

    // Test box points, arbitrary attributes
    {
        const std::vector<int32_t> int1 = {-3,2,1,0,3,-2,-1,0};
        const std::vector<int32_t> int2 = {-10,-5,-9,-1,-2,-2,-1,-2};
        const std::vector<float> float1 = {-4.3f,5.1f,-1.1f,0.0f,9.5f,-10.2f,3.4f,6.2f};
        const std::vector<Vec3f> vec = {
            Vec3f(0.0f), Vec3f(-0.0f), Vec3f(0.3f),
            Vec3f(1.0f,-0.5f,-0.2f), Vec3f(0.2f),
            Vec3f(0.2f, 0.5f, 0.1f), Vec3f(-0.1f),
            Vec3f(0.1f),
        };
        const auto positions = getBoxPoints();

        const auto points = PointBuilder(positions) // 8 points
            .callback([&](PointDataTree& tree, const tools::PointIndexTree& index) {
                PointAttributeVector<int32_t> rwrap(int1);
                appendAttribute<int32_t>(tree, "inttest1");
                populateAttribute(tree, index, "inttest1", rwrap);
            })
            .callback([&](PointDataTree& tree, const tools::PointIndexTree& index) {
                PointAttributeVector<int32_t> rwrap(int2);
                appendAttribute<int32_t>(tree, "inttest2");
                populateAttribute(tree, index, "inttest2", rwrap);
            })
            .callback([&](PointDataTree& tree, const tools::PointIndexTree& index) {
                PointAttributeVector<float> rwrap(float1);
                appendAttribute<float>(tree, "floattest");
                populateAttribute(tree, index, "floattest", rwrap);
            })
            .callback([&](PointDataTree& tree, const tools::PointIndexTree& index) {
                PointAttributeVector<Vec3f> rwrap(vec);
                appendAttribute<Vec3f>(tree, "vectest");
                populateAttribute(tree, index, "vectest", rwrap);
            })
            .get();

        // 2 points
        auto repl = points::replicate(*points, 2);
        CPPUNIT_ASSERT(repl);
        CPPUNIT_ASSERT(repl->tree().hasSameTopology(points->tree()));
        CPPUNIT_ASSERT_EQUAL(Index64(8), repl->tree().activeVoxelCount());
        CPPUNIT_ASSERT_EQUAL(Index64(16), points::pointCount(repl->tree()));
        checkReplicatedP(*repl, positions, 2);
        checkReplicatedAttribute(*repl, "inttest1", int1, 2);
        checkReplicatedAttribute(*repl, "inttest2", int2, 2);
        checkReplicatedAttribute(*repl, "floattest", float1, 2);
        checkReplicatedAttribute(*repl, "vectest", vec, 2);

        // 10 points
        repl = points::replicate(*points, 10);
        CPPUNIT_ASSERT(repl);
        CPPUNIT_ASSERT(repl->tree().hasSameTopology(points->tree()));
        CPPUNIT_ASSERT_EQUAL(Index64(8), repl->tree().activeVoxelCount());
        CPPUNIT_ASSERT_EQUAL(Index64(80), points::pointCount(repl->tree()));
        checkReplicatedP(*repl, positions, 10);
        checkReplicatedAttribute(*repl, "inttest1", int1, 10);
        checkReplicatedAttribute(*repl, "inttest2", int2, 10);
        checkReplicatedAttribute(*repl, "floattest", float1, 10);
        checkReplicatedAttribute(*repl, "vectest", vec, 10);

        // 10 points, specific attributes
        repl = points::replicate(*points, 10, std::vector<std::string>());
        CPPUNIT_ASSERT(repl);
        CPPUNIT_ASSERT(repl->tree().hasSameTopology(points->tree()));
        CPPUNIT_ASSERT(repl->tree().cbeginLeaf());
        CPPUNIT_ASSERT_EQUAL(size_t(1), repl->tree().cbeginLeaf()->attributeSet().size());
        CPPUNIT_ASSERT_EQUAL(Index64(8), repl->tree().activeVoxelCount());
        CPPUNIT_ASSERT_EQUAL(Index64(80), points::pointCount(repl->tree()));
        checkReplicatedP(*repl, positions, 10);

        // 10 points, specific attributes
        const std::vector<std::string> attrs = { "P", "floattest" };
        repl = points::replicate(*points, 10, attrs);
        CPPUNIT_ASSERT(repl);
        CPPUNIT_ASSERT(repl->tree().hasSameTopology(points->tree()));
        CPPUNIT_ASSERT(repl->tree().cbeginLeaf());
        CPPUNIT_ASSERT_EQUAL(size_t(2), repl->tree().cbeginLeaf()->attributeSet().size());
        CPPUNIT_ASSERT_EQUAL(Index64(8), repl->tree().activeVoxelCount());
        CPPUNIT_ASSERT_EQUAL(Index64(80), points::pointCount(repl->tree()));
        checkReplicatedP(*repl, positions, 10);
        checkReplicatedAttribute(*repl, "floattest", float1, 10);
    }
}


void
TestPointReplicate::testReplicateScale()
{
    // Test box points, arbitrary attributes
    {
        const std::vector<float> scales = {-3,2,1,0,3,-2,-1,0};
        const std::vector<int32_t> int2 = {-10,-5,-9,-1,-2,-2,-1,-2};
        const std::vector<float> float1 = {-4.3f,5.1f,-1.1f,0.0f,9.5f,-10.2f,3.4f,6.2f};
        const auto positions = getBoxPoints();

        const auto points = PointBuilder(positions) // 8 points
            .callback([&](PointDataTree& tree, const tools::PointIndexTree& index) {
                PointAttributeVector<float> rwrap(scales);
                appendAttribute<float>(tree, "scale");
                populateAttribute(tree, index, "scale", rwrap);
            })
            .callback([&](PointDataTree& tree, const tools::PointIndexTree& index) {
                PointAttributeVector<int32_t> rwrap(int2);
                appendAttribute<int32_t>(tree, "inttest1");
                populateAttribute(tree, index, "inttest1", rwrap);
            })
            .callback([&](PointDataTree& tree, const tools::PointIndexTree& index) {
                PointAttributeVector<float> rwrap(float1);
                appendAttribute<float>(tree, "floattest");
                populateAttribute(tree, index, "floattest", rwrap);
            })
            .get();

        // 2 points
        auto repl = points::replicate(*points, 2, "scale");
        size_t expectedTotal = 0;
        for (auto& scale : scales) expectedTotal += 2*size_t(scale < 0 ? 0 : scale);
        CPPUNIT_ASSERT(repl);
        CPPUNIT_ASSERT(repl->tree().isValueOn({10,-10,-10}));
        CPPUNIT_ASSERT(repl->tree().isValueOn({-10,10,-10}));
        CPPUNIT_ASSERT(repl->tree().isValueOn({-10,-10,10}));
        CPPUNIT_ASSERT_EQUAL(Index64(3), repl->tree().activeVoxelCount());
        CPPUNIT_ASSERT_EQUAL(Index64(expectedTotal), points::pointCount(repl->tree()));
        checkReplicatedAttribute<int32_t>(*repl, "inttest1", {-5,-5,-5,-5,-9,-9,-2,-2,-2,-2,-2,-2});
        checkReplicatedAttribute<float>(*repl, "scale", {2,2,2,2,1,1,3,3,3,3,3,3});
        checkReplicatedAttribute<float>(*repl, "floattest",
            {5.1f,5.1f,5.1f,5.1f,-1.1f,-1.1f,9.5f,9.5f,9.5f,9.5f,9.5f,9.5f});

        // 3 points with repl id
        repl = points::replicate(*points, 3, {"inttest1"}, "scale", "replid");
        expectedTotal = 0;
        for (auto& scale : scales) expectedTotal += 3*size_t(scale < 0 ? 0 : scale);
        CPPUNIT_ASSERT(repl);
        CPPUNIT_ASSERT(repl->tree().isValueOn({10,-10,-10}));
        CPPUNIT_ASSERT(repl->tree().isValueOn({-10,10,-10}));
        CPPUNIT_ASSERT(repl->tree().isValueOn({-10,-10,10}));
        CPPUNIT_ASSERT(repl->tree().cbeginLeaf());
        CPPUNIT_ASSERT_EQUAL(size_t(3), repl->tree().cbeginLeaf()->attributeSet().size());
        CPPUNIT_ASSERT(repl->tree().cbeginLeaf()->attributeSet().getConst("P"));
        CPPUNIT_ASSERT(repl->tree().cbeginLeaf()->attributeSet().getConst("inttest1"));
        CPPUNIT_ASSERT(repl->tree().cbeginLeaf()->attributeSet().getConst("replid"));
        CPPUNIT_ASSERT_EQUAL(Index64(3), repl->tree().activeVoxelCount());
        CPPUNIT_ASSERT_EQUAL(Index64(expectedTotal), points::pointCount(repl->tree()));
        checkReplicatedAttribute<int32_t>(*repl, "inttest1",
            {-5,-5,-5,-5,-5,-5,-9,-9,-9,-2,-2,-2,-2,-2,-2,-2,-2,-2});
        // check the repl id based on which points were replicated
        checkReplicatedAttribute<int32_t>(*repl, "replid",
            {0,1,2,3,4,5,0,1,2,0,1,2,3,4,5,6,7,8});
    }
}

void
TestPointReplicate::testReplicateZero()
{
    // Test box points, arbitrary attributes
    {
        const std::vector<float> scales = {0.4,0.4};
        const std::vector<openvdb::Vec3f> positions = {
            openvdb::Vec3f(0.0f, 0.0f, 0.0f),
            openvdb::Vec3f(0.0f, 0.0f, 0.0f)
        };

        const auto points = PointBuilder(positions) // 2 points
            .callback([&](PointDataTree& tree, const tools::PointIndexTree& index) {
                PointAttributeVector<float> rwrap(scales);
                appendAttribute<float>(tree, "scale");
                populateAttribute(tree, index, "scale", rwrap);
            })
            .get();

        //2 points
        auto repl = points::replicate(*points, 0);
        const size_t expectedTotal = 0;
        CPPUNIT_ASSERT(repl);
        CPPUNIT_ASSERT_EQUAL(Index64(expectedTotal), points::pointCount(repl->tree()));

        repl = points::replicate(*points, 1, "scale");
        CPPUNIT_ASSERT(repl);
        CPPUNIT_ASSERT_EQUAL(Index64(expectedTotal), points::pointCount(repl->tree()));
    }
}

