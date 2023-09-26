// Copyright Contributors to the OpenVDB Project
// SPDX-License-Identifier: MPL-2.0

/// @author Nick Avramoussis
///
/// @file PrincipalComponentAnalysisVoxelImpl.h
///

#ifndef OPENVDB_POINTS_POINT_PRINCIPAL_COMPONENT_ANALYSIS_VOXEL_IMPL_HAS_BEEN_INCLUDED
#define OPENVDB_POINTS_POINT_PRINCIPAL_COMPONENT_ANALYSIS_VOXEL_IMPL_HAS_BEEN_INCLUDED

#include "PrincipalComponentAnalysisImpl.h"

namespace openvdb {
OPENVDB_USE_VERSION_NAMESPACE
namespace OPENVDB_VERSION_NAME {
namespace points {

namespace pca_internal {

template <typename Vec4fTreeT, typename PointDataTreeT>
struct WeightPosVoxelSumsTransfer
    : public VolumeTransfer<PointDataTreeT, Vec4fTreeT>
{
    using BaseT = VolumeTransfer<PointDataTreeT, Vec4fTreeT>;

    static const Index DIM = Vec4fTreeT::LeafNodeType::DIM;
    static const Index LOG2DIM = Vec4fTreeT::LeafNodeType::LOG2DIM;
    static const Index NUM_VALUES = Vec4fTreeT::LeafNodeType::NUM_VALUES;

    WeightPosVoxelSumsTransfer(const float searchRadiusIS,
                          const int32_t neighbourThreshold,
                          PointDataTreeT& points,
                          Vec4fTreeT& tree)
        : BaseT(points, tree)
        , mMaxSearchIS(searchRadiusIS)
        , mMaxSearchSqIS(searchRadiusIS * searchRadiusIS)
        , mSearchInvIS(1.0f/searchRadiusIS)
        , mNeighbourThreshold(neighbourThreshold)
        , mCounts() {}

    WeightPosVoxelSumsTransfer(const WeightPosVoxelSumsTransfer& other)
        : BaseT(other)
        , mMaxSearchIS(other.mMaxSearchIS)
        , mMaxSearchSqIS(other.mMaxSearchSqIS)
        , mSearchInvIS(other.mSearchInvIS)
        , mNeighbourThreshold(other.mNeighbourThreshold)
        , mCounts() {}

    Vec3i range(const Coord&, size_t) const { return Vec3i(math::Floor(mMaxSearchIS)); }

    inline void initialize(const Coord& origin, const size_t idx, const CoordBBox& bounds)
    {
        BaseT::initialize(origin, idx, bounds);
        mCounts.fill(0);
    }

    bool startPointLeaf(const typename Vec4fTreeT::LeafNodeType&) { return true; }
    bool endPointLeaf(const typename Vec4fTreeT::LeafNodeType&) { return true; }

    inline void rasterizePoint(const Coord& ijk,
                    const Index,
                    const CoordBBox& bounds)
    {
        CoordBBox intersectBox(Coord::ceil(ijk.asVec3s() - mMaxSearchIS), Coord::floor(ijk.asVec3s() + mMaxSearchIS));
        intersectBox.intersect(bounds);
        if (intersectBox.empty()) return;

        Vec4f* const data = this->template buffer<1>();
        const auto& mask = *(this->template mask<0>()); // point mask

        const Coord& a(intersectBox.min());
        const Coord& b(intersectBox.max());
        for (Coord c = a; c.x() <= b.x(); ++c.x()) {
            const size_t x2 = static_cast<size_t>(math::Pow2(c.x() - ijk.x()));
            const Index i = ((c.x() & (DIM-1u)) << 2*LOG2DIM); // unsigned bit shift mult
            for (c.y() = a.y(); c.y() <= b.y(); ++c.y()) {
                const size_t x2y2 = static_cast<size_t>(x2 + math::Pow2(c.y() - ijk.y()));
                const Index ij = i + ((c.y() & (DIM-1u)) << LOG2DIM);
                for (c.z() = a.z(); c.z() <= b.z(); ++c.z()) {
                    const float x2y2z2 = static_cast<float>(x2y2 + math::Pow2(c.z() - ijk.z()));
                    if (x2y2z2 >= mMaxSearchSqIS) continue; //outside search distance

                    const Index offset = ij + /*k*/(c.z() & (DIM-1u));
                    if (!mask.isOn(offset)) continue;

                    const float weight = 1.0f - math::Pow3(math::Sqrt(x2y2z2) * mSearchInvIS);
                    assert(weight > 0.0f && weight < 1.0f);
                    const Vec3f w = ijk.asVec3s() * weight;
                    data[offset] += Vec4f(w.x(), w.y(), w.z(), weight);
                    ++mCounts[offset];
                }
            }
        } // outer sdf voxel
    } // point idx

    bool finalize(const Coord&, size_t)
    {
        auto& mask = *(this->template mask<1>());
        Vec4f* const data = this->template buffer<1>();

        for (Index i = 0; i < NUM_VALUES; ++i)
        {
            if (!mask.isOn(i)) continue;

            mCounts[i] -= 1; // self contribution

            if (mCounts[i] < mNeighbourThreshold) { mask.setOff(i); }
            if (mCounts[i] <= 0) continue;

            float* p = data[i].asPointer();
            Vec3f* weightedPosition = reinterpret_cast<Vec3f*>(p);

            // Normalize
            assert(p[3] > 0.0f);
            p[3] = 1.0f / p[3];
            (*weightedPosition) *= p[3];
        }

        return true;
    }

private:
    const float mMaxSearchIS;
    const float mMaxSearchSqIS;
    const float mSearchInvIS;
    const int32_t mNeighbourThreshold;
    std::array<int32_t, NUM_VALUES> mCounts;
};

template <typename Vec4fTreeT,
          typename Vec3fTreeT,
          typename QuatfTreeT>
struct CovarianceVoxelTransfer
    : public VolumeTransfer<Vec4fTreeT, Vec3fTreeT, QuatfTreeT>
{
    using BaseT = VolumeTransfer<Vec4fTreeT, Vec3fTreeT, QuatfTreeT>;

    static const Index DIM = Vec4fTreeT::LeafNodeType::DIM;
    static const Index LOG2DIM = Vec4fTreeT::LeafNodeType::LOG2DIM;
    static const Index NUM_VALUES = Vec4fTreeT::LeafNodeType::NUM_VALUES;

    CovarianceVoxelTransfer(const float searchRadiusIS,
                            const PcaSettings& settings,
                           Vec4fTreeT& weights,
                           Vec3fTreeT& stretches,
                           QuatfTreeT& quats)
        : BaseT(weights, stretches, quats)
        , mMaxSearchIS(searchRadiusIS)
        , mMaxSearchSqIS(searchRadiusIS * searchRadiusIS)
        , mSearchInvIS(1.0f/searchRadiusIS)
        , mSettings(settings)
        , mCovs() {}

    CovarianceVoxelTransfer(const CovarianceVoxelTransfer& other)
        : BaseT(other)
        , mMaxSearchIS(other.mMaxSearchIS)
        , mMaxSearchSqIS(other.mMaxSearchSqIS)
        , mSearchInvIS(other.mSearchInvIS)
        , mSettings(other.mSettings)
        , mCovs() {}

    Vec3i range(const Coord&, size_t) const { return Vec3i(math::Floor(mMaxSearchIS)); }

    inline void initialize(const Coord& origin, const size_t idx, const CoordBBox& bounds)
    {
        BaseT::initialize(origin, idx, bounds);
        auto& mask = *(this->template mask<1>());
        for (Index i = 0; i < NUM_VALUES; ++i) {
            if (mask.isOn(i)) mCovs[i] = Mat3s::zero();
        }
    }

    bool startPointLeaf(const typename Vec4fTreeT::LeafNodeType&) { return true; }
    bool endPointLeaf(const typename Vec4fTreeT::LeafNodeType&) { return true; }

    inline void rasterizePoint(const Coord& ijk,
                    const Index,
                    const CoordBBox& bounds)
    {
        CoordBBox intersectBox(Coord::ceil(ijk.asVec3s() - mMaxSearchIS), Coord::floor(ijk.asVec3s() + mMaxSearchIS));
        intersectBox.intersect(bounds);
        if (intersectBox.empty()) return;

        Vec4f* const data = this->template buffer<0>();
        const auto& mask = *(this->template mask<0>());

        const Coord& a(intersectBox.min());
        const Coord& b(intersectBox.max());
        for (Coord c = a; c.x() <= b.x(); ++c.x()) {
            const size_t x2 = static_cast<size_t>(math::Pow2(c.x() - ijk.x()));
            const Index i = ((c.x() & (DIM-1u)) << 2*LOG2DIM); // unsigned bit shift mult
            for (c.y() = a.y(); c.y() <= b.y(); ++c.y()) {
                const size_t x2y2 = static_cast<size_t>(x2 + math::Pow2(c.y() - ijk.y()));
                const Index ij = i + ((c.y() & (DIM-1u)) << LOG2DIM);
                for (c.z() = a.z(); c.z() <= b.z(); ++c.z()) {
                    float x2y2z2 = static_cast<float>(x2y2 + math::Pow2(c.z() - ijk.z()));
                    if (x2y2z2 >= mMaxSearchSqIS) continue; //outside search distance
                    const Index offset = ij + /*k*/(c.z() & (DIM-1u));
                    if (!mask.isOn(offset)) continue; // inside existing level set or not in range

                    const float* const p = data[offset].asPointer();

                    const float totalWeightInv = p[3];
                    const Vec3f currWeightSum = *reinterpret_cast<const Vec3f*>(p);

                    const float weight = 1.0f - math::Pow3(float(math::Sqrt(x2y2z2)) * mSearchInvIS);
                    const Vec3f posMeanDiff = ijk.asVec3s() - currWeightSum;
                    const Vec3f x = (totalWeightInv * weight) * posMeanDiff;

                    float* const m = mCovs[offset].asPointer();
                    /// @note: equal to:
                    // mat.setCol(0, mat.col(0) + (x * posMeanDiff[0]));
                    // mat.setCol(1, mat.col(1) + (x * posMeanDiff[1]));
                    // mat.setCol(2, mat.col(2) + (x * posMeanDiff[2]));
                    m[0] += x[0] * posMeanDiff[0];
                    m[1] += x[0] * posMeanDiff[1];
                    m[2] += x[0] * posMeanDiff[2];
                    //
                    m[3] += x[1] * posMeanDiff[0];
                    m[4] += x[1] * posMeanDiff[1];
                    m[5] += x[1] * posMeanDiff[2];
                    //
                    m[6] += x[2] * posMeanDiff[0];
                    m[7] += x[2] * posMeanDiff[1];
                    m[8] += x[2] * posMeanDiff[2];
                }
            }
        } // outer sdf voxel
    } // point idx

    bool finalize(const Coord&, size_t)
    {
        auto& mask = *(this->template mask<0>());
        Vec3f* const stretches = this->template buffer<1>();
        Quats* const quats = this->template buffer<2>();

        for (Index i = 0; i < NUM_VALUES; ++i)
        {
            if (!mask.isOn(i)) continue;

            // get singular values of the covariance matrix
            math::Mat3s u;
            Vec3s sigma;
            decomposeSymmetricMatrix(mCovs[i], u, sigma);

            // fix sigma values, the principal lengths
            auto maxs = sigma[0] * mSettings.allowedAnisotropyRatio;
            sigma[1] = std::max(sigma[1], maxs);
            sigma[2] = std::max(sigma[2], maxs);

            // should only happen if all neighbours are coincident
            // @note  have to manually construct the tolerance because
            //   math::Tolerance<Vec3f> resolves to 0.0
            // @todo  fix this in the math lib
            if (math::isApproxZero(sigma, Vec3f(math::Tolerance<float>::value()))) {
                sigma = Vec3f::ones();
            }

            // https://math.stackexchange.com/questions/36565/sign-of-detuv-in-svd
            // https://www.researchgate.net/post/How_to_convert_a_3x3_matrix_to_a_rotation_matrix
            // https://stackoverflow.com/questions/30562692/rotation-matrix-to-quaternion-and-back-what-is-wrong
            if (u.det() < 0) {
                u = -u;
                assert(u.det() > 0); // should be 1
            }

            stretches[i] = sigma;
            quats[i] = Quats(u, Quats::kUnsafeConstruct);
        }

        return true;
    }

private:
    const float mMaxSearchIS;
    const float mMaxSearchSqIS;
    const float mSearchInvIS;
    const PcaSettings& mSettings;
    std::array<Mat3s, NUM_VALUES> mCovs;
};

template <typename PointDataTreeT,
    typename InterrupterT>
inline void
computeVoxelBasedWeights(tree::LeafManager<PointDataTreeT>& manager,
    const PcaSettings& settings,
    const AttrIndices& indices,
    Real vs,
    InterrupterT* interrupt)
{
    using LeafNodeT = typename PointDataTreeT::LeafNodeType;
    using Vec3fTreeT = typename PointDataTreeT::template ValueConverter<Vec3f>::Type;
    using Vec4fTreeT = typename PointDataTreeT::template ValueConverter<Vec4f>::Type;
    using QuatfTreeT = typename PointDataTreeT::template ValueConverter<Quats>::Type;

    using namespace pca_internal;

    const float searchRadiusIS = static_cast<float>(settings.searchRadius / vs);

    Vec4fTreeT weights;
    Vec3fTreeT stretches;
    QuatfTreeT quats;

    weights.topologyUnion(manager.tree());

    PcaTimer timer;

    // 4) Init temporary attributes and calculate:
    //        sum_j w_{i,j} * x_j / (sum_j w_j)
    //    And neighbour counts for each point.
    // simultaneously calculates the sum of weighted vector positions (sum w_{i,j} * x_i)
    // weighted against the inverse sum of weights (1.0 / sum w_{i,j}). Also counts number
    // of neighours each point has and updates the ellipses group based on minimum
    // neighbour threshold. Those points which are "included" but which lack sufficient
    // neighbours will be marked as "not included".
    timer.start("Compute position weights");
    {
        WeightPosVoxelSumsTransfer<Vec4fTreeT, PointDataTreeT> transfer(
            searchRadiusIS,
            int32_t(settings.neighbourThreshold),
            manager.tree(),
            weights);

        points::rasterize<Vec4fTreeT,
            decltype(transfer),
            NullFilter,
            InterrupterT>(weights, transfer, NullFilter(), interrupt);
    }

    timer.stop();
    if (util::wasInterrupted(interrupt)) return;

    stretches.topologyUnion(weights);
    quats.topologyUnion(weights);

    // 5) Principal axes define the rotation matrix of the ellipsoid.
    //    Calculates covariance matrices given weighted sums of positions and
    //    sums of weights per-particle
    timer.start("Compute covariance matrices");
    {
        CovarianceVoxelTransfer<Vec4fTreeT, Vec3fTreeT, QuatfTreeT>
            transfer(searchRadiusIS, settings, weights, stretches, quats);

        points::rasterize<Vec4fTreeT,
            decltype(transfer),
            NullFilter,
            InterrupterT>(weights, transfer, NullFilter(), interrupt);
    }

    timer.stop();
    if (util::wasInterrupted(interrupt)) return;

    static auto interpolatePca = [](auto& acc, const auto& center, const Vec3R& uvw, const Vec3i& inIdx)
    {
        using ValueT = std::decay_t<decltype(center)>;

        ValueT weights[2][2][2];
        const uint8_t mask = tools::BoxSampler::probeValues(weights, acc, Coord(inIdx));
        if (!(mask & (1<<0))) weights[0][0][0] = center;
        if (!(mask & (1<<1))) weights[0][0][1] = center;
        if (!(mask & (1<<2))) weights[0][1][1] = center;
        if (!(mask & (1<<3))) weights[0][1][0] = center;
        if (!(mask & (1<<4))) weights[1][0][0] = center;
        if (!(mask & (1<<5))) weights[1][0][1] = center;
        if (!(mask & (1<<6))) weights[1][1][1] = center;
        if (!(mask & (1<<7))) weights[1][1][0] = center;

        if constexpr (std::is_same<Quats, ValueT>::value)
        {
            return math::slerp(
                math::slerp(
                    math::slerp(weights[0][0][0], weights[0][0][1], float(uvw[2])),
                    math::slerp(weights[0][1][0], weights[0][1][1], float(uvw[2])),
                    float(uvw[1])),
                math::slerp(
                    math::slerp(weights[1][0][0], weights[1][0][1], float(uvw[2])),
                    math::slerp(weights[1][1][0], weights[1][1][1], float(uvw[2])),
                    float(uvw[1])),
                float(uvw[0]));
        }
        else {
            return tools::BoxSampler::trilinearInterpolation(weights, uvw);
        }
    };

    // Sample onto points
    manager.foreach([&](LeafNodeT& leafnode, size_t)
    {
        AttributeHandle<Vec3d, NullCodec> pHandle(leafnode.attributeArray(indices.mPWsIndex));
        AttributeWriteHandle<WeightSumT, NullCodec> weightsHandle(leafnode.attributeArray(indices.mWeightSumIndex));
        AttributeWriteHandle<WeightedPositionSumT, NullCodec> weightedPosSumHandle(leafnode.attributeArray(indices.mPosSumIndex));
        AttributeWriteHandle<math::Mat3s, NullCodec> rotHandle(leafnode.attributeArray(indices.mCovMatrixIndex));
        AttributeWriteHandle<Vec3f, NullCodec> stretchHandle(leafnode.attributeArray(indices.mStretchIndex));
        points::GroupWriteHandle group(leafnode.groupWriteHandle(indices.mEllipsesGroupIndex));

        tree::ValueAccessor<const Vec4fTreeT, /*IsSafe=*/false> weightsAcc(weights);
        tree::ValueAccessor<const QuatfTreeT, /*IsSafe=*/false> quatsAcc(quats);
        tree::ValueAccessor<const Vec3fTreeT, /*IsSafe=*/false> stretchAcc(stretches);

        Mat3s rotation;
        Vec3f strecth;
        Vec4f cw, sampledWeights;

        for (Index i = 0; i < pHandle.size(); ++i)
        {
            const Vec3d Pis = pHandle.get(i) / vs; // @todo transform
            const Coord ijk = Coord::round(Pis); // voxel point resides in
            const Vec3i inIdx = tools::local_util::floorVec3(Pis); // bottom xyz voxel
            const Vec3R uvw = Pis - inIdx; // trilinear weights

            const bool valid = weightsAcc.probeValue(ijk, cw);

            if (valid) {
                const Quats qw = quatsAcc.getValue(ijk);
                const Vec3f sw = stretchAcc.getValue(ijk);
                rotation = Mat3s(interpolatePca(quatsAcc, qw, uvw, inIdx));
                strecth = interpolatePca(stretchAcc, sw, uvw, inIdx);
            }
            else {
                group.set(i, false);
                rotation.setIdentity();
                strecth.init(1.0f, 1.0f, 1.0f);
            }

            // Weights are always updated
            sampledWeights =
                interpolatePca(weightsAcc, cw, uvw, inIdx);

            rotHandle.set(i, rotation);
            stretchHandle.set(i, strecth);
            weightsHandle.set(i, sampledWeights[3]);
            weightedPosSumHandle.set(i,
                *reinterpret_cast<Vec3f*>(sampledWeights.asPointer()));
        }
    });
}

}


}
}
}

#endif // OPENVDB_POINTS_POINT_PRINCIPAL_COMPONENT_ANALYSIS_VOXEL_IMPL_HAS_BEEN_INCLUDED
