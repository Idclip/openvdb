// Copyright Contributors to the OpenVDB Project
// SPDX-License-Identifier: MPL-2.0
//
/// @author Nick Avramoussis
///
/// @file PointRasterizeSDFImpl.h
///

#ifndef OPENVDB_POINTS_RASTERIZE_SDF_IMPL_HAS_BEEN_INCLUDED
#define OPENVDB_POINTS_RASTERIZE_SDF_IMPL_HAS_BEEN_INCLUDED

namespace openvdb {
OPENVDB_USE_VERSION_NAMESPACE
namespace OPENVDB_VERSION_NAME {
namespace points {

/// @cond OPENVDB_DOCS_INTERNAL

namespace rasterize_sdf_internal
{

/// @brief  Define a fixed index space radius for point rasterization
template <typename ValueT>
struct FixedRadius
{
    static constexpr bool Fixed = true;
    using ValueType = ValueT;

    FixedRadius(const ValueT ris) : mR(ris) {}
    inline void reset(const PointDataTree::LeafNodeType&) const {}
    inline const FixedRadius& eval(const Index) const { return *this; }
    inline ValueT get() const { return mR; }
private:
    const ValueT mR;
};

/// @brief  Define a fixed narrow band radius for point rasterization
/// @details  Pass in an index space radius (relative to a PointDataGrid voxel
///   size) and the desired half band width of the target surface. The minimum
///   radius of influence is clamped to zero.
template <typename ValueT>
struct FixedBandRadius : public FixedRadius<ValueT>
{
    static constexpr bool Fixed = true;
    using ValueType = ValueT;

    FixedBandRadius(const ValueT ris, const float hb)
        : FixedRadius<ValueT>(ris)
        , mMinSearchIS(math::Max(ValueT(0.0), ris - ValueT(hb)))
        , mMaxSearchIS(ris + ValueT(hb))
        , mMinSearchSqIS(mMinSearchIS*mMinSearchIS)
        , mMaxSearchSqIS(mMaxSearchIS*mMaxSearchIS)
        , mHalfBand(hb) {}

    inline void reset(const PointDataTree::LeafNodeType&) const {}

    inline const FixedBandRadius eval(const Index, const ValueT scale) const
    {
        if (scale == 1.0) return *this;
        return FixedBandRadius(this->get() * scale, this->halfband());
    }

    inline const FixedBandRadius& eval(const Index) const { return *this; }
    inline float halfband() const { return mHalfBand; }

    inline ValueT min() const { return mMinSearchIS; }
    inline ValueT minSq() const { return mMinSearchSqIS; }
    inline ValueT max() const { return mMaxSearchIS; }
    inline ValueT maxSq() const { return mMaxSearchSqIS; }
private:
    const ValueT mMinSearchIS, mMaxSearchIS;
    const ValueT mMinSearchSqIS, mMaxSearchSqIS;
    // @note  Could technically recompute this value from the rest here
    //   but storing it alleviates any potential precision issues
    const float mHalfBand;
};

/// @brief  A vector varying per point radius (used for the ellipsoid
///   rasterizer).
template <>
struct FixedBandRadius<Vec3f> : public FixedRadius<Vec3f>
{
    static constexpr bool Fixed = true;
    using ValueType = Vec3f;
    FixedBandRadius(const Vec3f ris, const float hb)
        : FixedRadius<Vec3f>(ris)
        , mHalfBand(hb) {}
    inline void reset(const PointDataTree::LeafNodeType&) const {}
    inline const FixedBandRadius& eval(const Index) const { return *this; }
    inline float halfband() const { return mHalfBand; }
private:
    const float mHalfBand;
};

/// @brief  A varying per point radius with an optional scale
template <typename ValueT, typename ScaleT = ValueT, typename CodecT = UnknownCodec>
struct VaryingRadius
{
    static constexpr bool Fixed = false;
    using ValueType = ValueT;

    using RadiusHandleT = AttributeHandle<ValueT, CodecT>;
    VaryingRadius(const size_t ridx, const ScaleT scale = ScaleT(1.0))
        : mRIdx(ridx), mRHandle(), mScale(scale) {}
    VaryingRadius(const VaryingRadius& other)
        : mRIdx(other.mRIdx), mRHandle(), mScale(other.mScale) {}

    inline size_t size() const { return mRHandle->size(); }

    inline void reset(const PointDataTree::LeafNodeType& leaf)
    {
        mRHandle.reset(new RadiusHandleT(leaf.constAttributeArray(mRIdx)));
    }

    /// @brief  Compute a fixed radius for a specific point
    inline auto eval(const Index id, const ScaleT scale = ScaleT(1.0)) const
    {
        assert(mRHandle);
        auto x = mRHandle->get(id) * mScale * scale;
        return FixedRadius<decltype(x)>(x);
    }

private:
    const size_t mRIdx;
    typename RadiusHandleT::UniquePtr mRHandle;
    const ScaleT mScale;
};

/// @brief  A varying per point narrow band radius with an optional scale
template <typename ValueT, typename ScaleT = ValueT, typename CodecT = UnknownCodec>
struct VaryingBandRadius : public VaryingRadius<ValueT, ScaleT, CodecT>
{
    static constexpr bool Fixed = false;
    using ValueType = ValueT;

    using BaseT = VaryingRadius<ValueT, ScaleT, CodecT>;
    VaryingBandRadius(const size_t ridx, const float halfband,
        const ScaleT scale = ScaleT(1.0))
        : BaseT(ridx, scale), mHalfBand(halfband) {}

    inline float halfband() const { return mHalfBand; }
    inline auto eval(const Index id, const ScaleT scale = ScaleT(1.0)) const
    {
        auto r = this->BaseT::eval(id, scale).get();
        return FixedBandRadius<decltype(r)>(r, mHalfBand);
    }

private:
    const float mHalfBand;
};

/// @brief  Base class for SDF transfers which consolidates member data and
///  some required interface methods.
/// @note   Derives from TransformTransfer for automatic transformation support
///   and VolumeTransfer<...T> for automatic buffer setup
template <typename SdfT,
    typename PositionCodecT,
    typename RadiusType,
    bool CPG>
struct SignedDistanceFieldTransfer :
    public TransformTransfer,
    public std::conditional<CPG,
        VolumeTransfer<typename SdfT::TreeType, Int64Tree>,
        VolumeTransfer<typename SdfT::TreeType>>::type
{
    using TreeT = typename SdfT::TreeType;
    using ValueT = typename TreeT::ValueType;
    static_assert(std::is_floating_point<ValueT>::value,
        "Spherical transfer only supports floating point values.");
    static_assert(!std::is_reference<RadiusType>::value && !std::is_pointer<RadiusType>::value,
        "Templated radius type must not be a reference or pointer");

    using VolumeTransferT = typename std::conditional<CPG,
        VolumeTransfer<TreeT, Int64Tree>,
        VolumeTransfer<TreeT>>::type;

    using PositionHandleT = AttributeHandle<Vec3f, PositionCodecT>;

    // typically the max radius of all points rounded up
    inline Vec3i range(const Coord&, size_t) const { return mMaxKernelWidth; }

    inline bool startPointLeaf(const PointDataTree::LeafNodeType& leaf)
    {
        mPosition.reset(new PositionHandleT(leaf.constAttributeArray(mPIdx)));
        mRadius.reset(leaf);
        // if CPG, store leaf id in upper 32 bits of mask
        if (CPG) mPLeafMask = (Index64(mIds->find(&leaf)->second) << 32);
        return true;
    }

protected:
    /// @brief Constructor to use when a closet point grid is not in use
    template <bool EnableT = CPG>
    SignedDistanceFieldTransfer(const size_t pidx,
            const Vec3i width,
            const RadiusType& rt,
            const math::Transform& source,
            SdfT& surface,
            Int64Tree* cpg,
            const std::unordered_map<const PointDataTree::LeafNodeType*, Index>* ids,
            typename std::enable_if<EnableT>::type* = 0)
        : TransformTransfer(source, surface.transform())
        , VolumeTransferT(&(surface.tree()), cpg)
        , mPIdx(pidx)
        , mPosition()
        , mMaxKernelWidth(width)
        , mRadius(rt)
        , mBackground(surface.background())
        , mDx(surface.voxelSize()[0])
        , mIds(ids)
        , mPLeafMask(0) {
            assert(cpg && ids);
        }

    /// @brief Constructor to use when a closet point grid is in use
    template <bool EnableT = CPG>
    SignedDistanceFieldTransfer(const size_t pidx,
            const Vec3i width,
            const RadiusType& rt,
            const math::Transform& source,
            SdfT& surface,
            Int64Tree*,
            const std::unordered_map<const PointDataTree::LeafNodeType*, Index>*,
            typename std::enable_if<!EnableT>::type* = 0)
        : TransformTransfer(source, surface.transform())
        , VolumeTransferT(surface.tree())
        , mPIdx(pidx)
        , mPosition()
        , mMaxKernelWidth(width)
        , mRadius(rt)
        , mBackground(surface.background())
        , mDx(surface.voxelSize()[0])
        , mIds(nullptr)
        , mPLeafMask(0) {}

    SignedDistanceFieldTransfer(const SignedDistanceFieldTransfer& other)
        : TransformTransfer(other)
        , VolumeTransferT(other)
        , mPIdx(other.mPIdx)
        , mPosition()
        , mMaxKernelWidth(other.mMaxKernelWidth)
        , mRadius(other.mRadius)
        , mBackground(other.mBackground)
        , mDx(other.mDx)
        , mIds(other.mIds)
        , mPLeafMask(0) {}

protected:
    const size_t mPIdx;
    typename PositionHandleT::UniquePtr mPosition;
    const Vec3i mMaxKernelWidth;
    RadiusType mRadius;
    const ValueT mBackground;
    const double mDx;
    const std::unordered_map<const PointDataTree::LeafNodeType*, Index>* mIds;
    Index64 mPLeafMask;
};

/// @brief  The transfer implementation for spherical stamping of narrow band
///   radius values.
template <typename SdfT,
    typename PositionCodecT,
    typename RadiusType,
    bool CPG>
struct SphericalTransfer :
    public SignedDistanceFieldTransfer<SdfT, PositionCodecT, RadiusType, CPG>
{
    using BaseT = SignedDistanceFieldTransfer<SdfT, PositionCodecT, RadiusType, CPG>;
    using typename BaseT::TreeT;
    using typename BaseT::ValueT;
    static const Index DIM = TreeT::LeafNodeType::DIM;
    static const Index LOG2DIM = TreeT::LeafNodeType::LOG2DIM;
    // The precision of the kernel arithmetic
    using RealT = double;

    using ElemT = typename VecTraits<typename RadiusType::ValueType>::ElementType;

    SphericalTransfer(const size_t pidx,
            const size_t width,
            const RadiusType& rt,
            const math::Transform& source,
            SdfT& surface,
            Int64Tree* cpg = nullptr,
            const std::unordered_map<const PointDataTree::LeafNodeType*, Index>* ids = nullptr)
        : SphericalTransfer(pidx, Vec3i(width), rt, source, surface, cpg, ids) {}

    /// @brief  For each point, stamp a sphere with a given radius by running
    ///   over all intersecting voxels and calculating if this point is closer
    ///   than the currently held distance value. Note that the default value
    ///   of the surface buffer should be the background value of the surface.
    inline void rasterizePoint(const Coord& ijk,
                    const Index id,
                    const CoordBBox& bounds)
    {
        // @note  GCC 6.3 warns here in optimised builds
#if defined(__GNUC__)  && !defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
#endif
        Vec3d P = ijk.asVec3d() + Vec3d(this->mPosition->get(id));
#if defined(__GNUC__)  && !defined(__clang__)
#pragma GCC diagnostic pop
#endif
        P = this->transformSourceToTarget(P);
        this->rasterizePoint(P, id, bounds, this->mRadius.eval(id));
    }

    /// @brief  This hook simply exists for the Ellipsoid transfer to allow it
    ///   to pass a different P and scaled FixedBandRadius from its ellipsoid
    ///   path (as isolated points are stamped as spheres with a different
    ///   scale and positions may have been smoothed).
    /// @todo   I would prefer this second function wasn't necessary but there
    ///   is no easy way to allow differently scaled radii to exist in a more
    ///   efficient manner, nor use a different P.
    inline void rasterizePoint(const Vec3d& P,
                    const Index id,
                    const CoordBBox& bounds,
                    const FixedBandRadius<ElemT>& r)
    {
        const RealT max = r.max();
        CoordBBox intersectBox(Coord::round(P - max), Coord::round(P + max));
        intersectBox.intersect(bounds);
        if (intersectBox.empty()) return;

        auto* const data = this->template buffer<0>();
        auto* const cpg = CPG ? this->template buffer<CPG ? 1 : 0>() : nullptr;
        auto& mask = *(this->template mask<0>());

        // If min2 == 0.0, then the index space radius is equal to or less than
        // the desired half band. In this case each sphere interior always needs
        // to be filled with distance values as we won't ever reach the negative
        // background value. If, however, a point overlaps a voxel coord exactly,
        // x2y2z2 will be 0.0. Forcing min2 to be less than zero here avoids
        // incorrectly setting these voxels to inactive -background values as
        // x2y2z2 will never be < 0.0. We still want the lteq logic in the
        // (x2y2z2 <= min2) check as this is valid when min2 > 0.0.
        const RealT min2 = r.minSq() == 0.0 ? -1.0 : r.minSq();
        const RealT max2 = r.maxSq();

        const Coord& a(intersectBox.min());
        const Coord& b(intersectBox.max());
        for (Coord c = a; c.x() <= b.x(); ++c.x()) {
            const RealT x2 = static_cast<RealT>(math::Pow2(c.x() - P[0]));
            const Index i = ((c.x() & (DIM-1u)) << 2*LOG2DIM); // unsigned bit shift mult
            for (c.y() = a.y(); c.y() <= b.y(); ++c.y()) {
                const RealT x2y2 = static_cast<RealT>(x2 + math::Pow2(c.y() - P[1]));
                const Index ij = i + ((c.y() & (DIM-1u)) << LOG2DIM);
                for (c.z() = a.z(); c.z() <= b.z(); ++c.z()) {
                    const Index offset = ij + /*k*/(c.z() & (DIM-1u));
                    if (!mask.isOn(offset)) continue; // inside existing level set or not in range

                    const RealT x2y2z2 = static_cast<RealT>(x2y2 + math::Pow2(c.z() - P[2]));
                    if (x2y2z2 >= max2) continue; //outside narrow band of particle in positive direction
                    if (x2y2z2 <= min2) { //outside narrow band of the particle in negative direction. can disable this to fill interior
                        data[offset] = -(this->mBackground);
                        mask.setOff(offset);
                        continue;
                    }

                    const ValueT d = ValueT(this->mDx * (math::Sqrt(x2y2z2) - r.get())); // back to world space
                    ValueT& v = data[offset];
                    if (d < v) {
                        v = d;
                        OPENVDB_NO_TYPE_CONVERSION_WARNING_BEGIN
                        if (CPG) cpg[offset] = Int64(this->mPLeafMask | Index64(id));
                        OPENVDB_NO_TYPE_CONVERSION_WARNING_END
                        // transfer attributes - we can't use this here as the exposed
                        // function signatures take vector of attributes (i.e. an unbounded
                        // size). If we instead clamped the attribute transfer to a fixed
                        // amount of attributes we could get rid of the closest point logic
                        // entirely. @todo consider adding another signature for increased
                        // performance
                        // this->foreach([&](auto* buffer, const size_t idx) {
                        //     using Type = typename std::remove_pointer<decltype(buffer)>::type;
                        //     buffer[offset] = mAttributes.template get<Type>(idx);
                        // })
                    }
                }
            }
        } // outer sdf voxel
    } // point idx

    /// @brief Allow early termination if all voxels in the surface have been
    ///   deactivated (all interior)
    inline bool endPointLeaf(const PointDataTree::LeafNodeType&)
    {
        // If the mask is off, terminate rasterization
        return !(this->template mask<0>()->isOff());
    }

    inline bool finalize(const Coord&, const size_t)
    {
        // loop over voxels in the outer cube diagonals which won't have been
        // hit by point rasterizations - these will be on because of the mask
        // fill technique and need to be turned off.
        auto& mask = *(this->template mask<0>());
        auto* const data = this->template buffer<0>();
        for (auto iter = mask.beginOn(); iter; ++iter) {
            if (data[iter.pos()] == this->mBackground) mask.setOff(iter.pos());
        }
        // apply sdf mask to other grids
        if (CPG) *(this->template mask<CPG ? 1 : 0>()) = mask;
        return true;
    }

protected:
    /// @brief  Allow derived transfer schemes to override the width with a
    ///   varying component (this transfer is explicitly for spheres so it
    ///   doesn't make sense to construct it directly, but derived transfers
    ///   may be utilizing this logic with other kernels).
    SphericalTransfer(const size_t pidx,
            const Vec3i width,
            const RadiusType& rt,
            const math::Transform& source,
            SdfT& surface,
            Int64Tree* cpg = nullptr,
            const std::unordered_map<const PointDataTree::LeafNodeType*, Index>* ids = nullptr)
        : BaseT(pidx, width, rt, source, surface, cpg, ids) {}
};

/// @brief  The transfer implementation for averaging of positions followed by
///   spherical stamping.
template <typename SdfT,
    typename PositionCodecT,
    typename RadiusType,
    bool CPG>
struct AveragePositionTransfer :
    public SignedDistanceFieldTransfer<SdfT, PositionCodecT, RadiusType, CPG>
{
    using BaseT = SignedDistanceFieldTransfer<SdfT, PositionCodecT, RadiusType, CPG>;
    using typename BaseT::TreeT;
    using typename BaseT::ValueT;

    using VolumeTransferT = typename std::conditional<CPG,
        VolumeTransfer<typename SdfT::TreeType, Int64Tree>,
        VolumeTransfer<typename SdfT::TreeType>>::type;

    static const Index DIM = TreeT::LeafNodeType::DIM;
    static const Index LOG2DIM = TreeT::LeafNodeType::LOG2DIM;
    static const Index NUM_VALUES = TreeT::LeafNodeType::NUM_VALUES;
    // The precision of the kernel arithmetic
    using RealT = double;

    struct PosRadPair
    {
        template <typename S> inline void addP(const math::Vec3<S>& v) { P += v; }
        template <typename S> inline void addR(const S r) { R += r; }
        template <typename S> inline void multR(const S w) { R *= w; }
        template <typename S> inline void multP(const S w) { P *= w; }
        inline RealT length() const { return P.length() - R; }
        math::Vec3<RealT> P = math::Vec3<RealT>(0.0);
        RealT R = 0.0;
    };

    AveragePositionTransfer(const size_t pidx,
            const size_t width,
            const RadiusType& rt,
            const RealT search,
            const math::Transform& source,
            SdfT& surface,
            Int64Tree* cpg = nullptr,
            const std::unordered_map<const PointDataTree::LeafNodeType*, Index>* ids = nullptr)
        : AveragePositionTransfer(pidx, Vec3i(width), rt, search, source, surface, cpg, ids) {}

    AveragePositionTransfer(const AveragePositionTransfer& other)
        : BaseT(other)
        , mMaxSearchIS(other.mMaxSearchIS)
        , mMaxSearchSqIS(other.mMaxSearchSqIS)
        , mWeights()
        , mDist() {}

    inline void initialize(const Coord& origin, const size_t idx, const CoordBBox& bounds)
    {
        // init buffers
        this->BaseT::initialize(origin, idx, bounds);
        mWeights.assign(NUM_VALUES, PosRadPair());
        if (CPG) mDist.assign(NUM_VALUES, std::numeric_limits<float>::max());
        // We use the surface buffer to store the intermediate weights as
        // defined by the sum of k(|x−xj|/R), where k(s) = max(0,(1−s^2)^3)
        // and R is the maximum search distance. The active buffer currently
        // holds background values. We could simply subtract the background away
        // from the final result - however if the background value increases
        // beyond 1, progressively larger floating point instabilities can be
        // observed with the weight calculation. Instead, reset all active
        // values to zero
        // @todo The surface buffer may not be at RealT precision. Should we
        //  enforce this by storing the weights in another vector?
        auto* const data = this->template buffer<0>();
        const auto& mask = *(this->template mask<0>());
        for (auto iter = mask.beginOn(); iter; ++iter) {
            data[iter.pos()] = ValueT(0);
        }
    }

    inline void rasterizePoint(const Coord& ijk,
                    const Index id,
                    const CoordBBox& bounds)
    {
#if defined(__GNUC__)  && !defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
#endif
        const Vec3d PWS = this->sourceTransform().indexToWorld(ijk.asVec3d() + Vec3d(this->mPosition->get(id)));
#if defined(__GNUC__)  && !defined(__clang__)
#pragma GCC diagnostic pop
#endif
        const Vec3d P = this->targetTransform().worldToIndex(PWS);

        CoordBBox intersectBox(Coord::round(P - mMaxSearchIS), Coord::round(P + mMaxSearchIS));
        intersectBox.intersect(bounds);
        if (intersectBox.empty()) return;

        auto* const data = this->template buffer<0>();
        auto* const cpg = CPG ? this->template buffer<CPG ? 1 : 0>() : nullptr;
        const auto& mask = *(this->template mask<0>());

        // index space radius
        const auto& r = this->mRadius.eval(id);
        const RealT rad = r.get();
        const RealT invsq = 1.0 / mMaxSearchSqIS;

        const Coord& a(intersectBox.min());
        const Coord& b(intersectBox.max());
        for (Coord c = a; c.x() <= b.x(); ++c.x()) {
            const RealT x2 = static_cast<RealT>(math::Pow2(c.x() - P[0]));
            const Index i = ((c.x() & (DIM-1u)) << 2*LOG2DIM); // unsigned bit shift mult
            for (c.y() = a.y(); c.y() <= b.y(); ++c.y()) {
                const RealT x2y2 = static_cast<RealT>(x2 + math::Pow2(c.y() - P[1]));
                const Index ij = i + ((c.y() & (DIM-1u)) << LOG2DIM);
                for (c.z() = a.z(); c.z() <= b.z(); ++c.z()) {
                    RealT x2y2z2 = static_cast<RealT>(x2y2 + math::Pow2(c.z() - P[2]));
                    if (x2y2z2 >= mMaxSearchSqIS) continue; //outside search distance
                    const Index offset = ij + /*k*/(c.z() & (DIM-1u));
                    if (!mask.isOn(offset)) continue; // inside existing level set or not in range

                    // @note this algorithm is unable to deactivate voxels within
                    // a computed narrow band during rasterization as all points must
                    // visit their affected voxels.

                    if (CPG) {
                        // CPG still computed directly with each individual point
                        // @note  Because voxels can't be discarded, it may be faster to
                        //  do this as a post process (and avoid the sqrt per lookup)
                        // @note  No need to scale back to world space
                        const float dist = static_cast<float>(math::Sqrt(x2y2z2) - r.get());
                        auto& d = mDist[offset];
                        if (dist < d) {
                            d = dist;
                            OPENVDB_NO_TYPE_CONVERSION_WARNING_BEGIN
                            cpg[offset] = Int64(this->mPLeafMask | Index64(id));
                            OPENVDB_NO_TYPE_CONVERSION_WARNING_END
                        }
                    }

                    x2y2z2 *= invsq; // x2y2z2 = (x - xi) / R
                    // k(s) = max(0,(1−s^2)^3). note that the max is unecessary
                    // as we early terminate above with x2y2z2 >= mMaxSearchSqIS
                    x2y2z2 = math::Pow3(1.0 - x2y2z2);
                    assert(x2y2z2 >= 0.0);
                    // @todo The surface buffer may not be at RealT precision. Should we
                    //  enforce this by storing the weights in another vector?
                    OPENVDB_NO_TYPE_CONVERSION_WARNING_BEGIN
                    data[offset] += x2y2z2;
                    OPENVDB_NO_TYPE_CONVERSION_WARNING_END
                    auto& wt = mWeights[offset];
                    wt.addP(PWS * x2y2z2);
                    wt.addR(rad * x2y2z2);
                }
            }
        } // outer sdf voxel
    } // point idx

    inline bool endPointLeaf(const PointDataTree::LeafNodeType&) { return true; }

    inline bool finalize(const Coord& origin, const size_t)
    {
        auto& mask = *(this->template mask<0>());
        auto* const data = this->template buffer<0>();

        for (auto iter = mask.beginOn(); iter; ++iter) {
            const Index idx = iter.pos();
            auto& w = data[idx];
            // if background, voxel was out of range. Guaranteed to be outside as
            // all interior voxels will have at least a single point contribution
            if (w == 0.0f) {
                mask.setOff(idx);
                w = this->mBackground;
            }
            else {
                const Coord ijk = origin + TreeT::LeafNodeType::offsetToLocalCoord(idx);
                const Vec3d ws = this->targetTransform().indexToWorld(ijk);
                const RealT wi = RealT(1.0) / RealT(w); // wi
                auto& wt = mWeights[idx];
                wt.multP(wi); // sum of weighted positions
                wt.multR(wi * this->mDx); // sum of weighted radii (scale to ws)
                wt.addP(-ws); // (x - xi) (instead doing (-x + xi))
                w = static_cast<typename SdfT::ValueType>(wt.length()); // (x - xi) - r
                // clamp active region and value range to requested narrow band
                if (std::fabs(w) >= this->mBackground) {
                    w = std::copysign(this->mBackground, w);
                    mask.setOff(idx);
                }
            }
        }

        // apply sdf mask to other grids
        if (CPG) *(this->template mask<CPG ? 1 : 0>()) = mask;
        return true;
    }

protected:
    /// @brief  Allow derived transfer schemes to override the width with a
    ///   varying component (this transfer is explicitly for spheres so it
    ///   doesn't make sense to construct it directly, but derived transfers
    ///   may be utilizing this logic with other kernels).
    AveragePositionTransfer(const size_t pidx,
            const Vec3i width,
            const RadiusType& rt,
            const RealT search,
            const math::Transform& source,
            SdfT& surface,
            Int64Tree* cpg = nullptr,
            const std::unordered_map<const PointDataTree::LeafNodeType*, Index>* ids = nullptr)
        : BaseT(pidx, width, rt, source, surface, cpg, ids)
        , mMaxSearchIS(search)
        , mMaxSearchSqIS(search*search)
        , mWeights()
        , mDist() {}

private:
    const RealT mMaxSearchIS, mMaxSearchSqIS;
    std::vector<PosRadPair> mWeights;
    std::vector<float> mDist;
};

/// @brief  Base class for surfacing mask initialization
template <typename MaskTreeT = MaskTree,
    typename InterrupterT = util::NullInterrupter>
struct SurfaceMaskOp
{
public:
    inline void join(SurfaceMaskOp& other)
    {
        if (mMask->leafCount() > other.mMask->leafCount()) {
            mMask->topologyUnion(*other.mMask);
            other.mMask.reset();
        }
        else {
            other.mMask->topologyUnion(*mMask);
            mMask.reset(other.mMask.release());
        }

        if (mMaskOff->leafCount() > other.mMaskOff->leafCount()) {
            mMaskOff->topologyUnion(*other.mMaskOff);
            other.mMaskOff.reset();
        }
        else {
            other.mMaskOff->topologyUnion(*mMaskOff);
            mMaskOff.reset(other.mMaskOff.release());
        }
    }

    std::unique_ptr<MaskTreeT> mask() { return std::move(mMask); }
    std::unique_ptr<MaskTreeT> maskoff() { return std::move(mMaskOff); }

protected:
    SurfaceMaskOp(const math::Transform& points,
                  const math::Transform& surface,
                   // Clip the surface to this bounds. Only used for the smooth
                   // raster workflow to limit to search radii topology init
                   const CoordBBox* const maxClipBounds = nullptr,
                  InterrupterT* interrupter = nullptr)
        : mMask(new MaskTreeT)
        , mMaskOff(new MaskTreeT)
        , mPointsTransform(points)
        , mSurfaceTransform(surface)
        , mMaxClipBounds(maxClipBounds ? this->toSurfaceBounds(*maxClipBounds) : CoordBBox::inf())
        , mInterrupter(interrupter) {}

    SurfaceMaskOp(const SurfaceMaskOp& other)
        : mMask(new MaskTreeT)
        , mMaskOff(new MaskTreeT)
        , mPointsTransform(other.mPointsTransform)
        , mSurfaceTransform(other.mSurfaceTransform)
        , mMaxClipBounds(other.mMaxClipBounds)
        , mInterrupter(other.mInterrupter) {}

    // @brief  Sparse fill a tree with activated bounding boxes expanded from
    //   each active voxel.
    // @note  This method used to fill from each individual voxel. Whilst more
    //   accurate, this was slower in comparison to using the active node bounds.
    //   As the rasterization is so fast (discarding of voxels out of range)
    //   this overzealous activation results in far superior performance overall.
    template <typename LeafT>
    inline bool activate(const LeafT& leaf, const int32_t dist)
    {
        CoordBBox bounds = this->toSurfaceBounds(this->getActiveBoundingBox(leaf));
        if (bounds.empty()) return false;
        // Expand by the desired surface index space distance
        bounds.expand(dist);
        this->activate(bounds);
        return true;
    }

    template <typename LeafT>
    inline bool activate(const LeafT& leaf, const Vec3i dist)
    {
        CoordBBox bounds = this->toSurfaceBounds(this->getActiveBoundingBox(leaf));
        if (bounds.empty()) return false;
        // Expand by the desired surface index space distance
        bounds.min() -= Coord(dist);
        bounds.max() += Coord(dist);
        this->activate(bounds);
        return true;
    }

    template <typename LeafT>
    inline void deactivate(const LeafT& leaf, const int32_t dist)
    {
        assert((dist % MaskTreeT::LeafNodeType::DIM) == 0);
        // We only deactivate in incrementd of leaf nodes, so as long as
        // dist >= 0 we don't need a tight bounding box
        CoordBBox bounds = this->toSurfaceBounds(leaf.getNodeBoundingBox());
        // Expand by the desired surface index space distance
        bounds.expand(dist);
        this->deactivate(bounds);
    }

    inline void activate(CoordBBox& bounds)
    {
        bounds.intersect(mMaxClipBounds);
        mMask->sparseFill(bounds, true, true);
    }

    inline void deactivate(const CoordBBox& bounds)
    {
        mMaskOff->sparseFill(bounds, true, true);
    }

    inline bool interrupted()
    {
        if (util::wasInterrupted(mInterrupter)) {
            thread::cancelGroupExecution();
            return true;
        }
        return false;
    }

    //

    template <typename LeafT>
    inline CoordBBox getActiveBoundingBox(const LeafT& leaf) const
    {
        CoordBBox bounds;
        const auto& mask = leaf.getValueMask();
        if (mask.isOn()) {
            // includes translation to leaf origin
            bounds = leaf.getNodeBoundingBox();
        }
        else {
            for (auto iter = mask.beginOn(); iter; ++iter) {
                bounds.expand(leaf.offsetToLocalCoord(iter.pos()));
            }
            if (bounds.empty()) return bounds;
            bounds.translate(leaf.origin());
        }
        return bounds;
    }

    /// @brief  Given a leaf node (and assuming the coordinate bounds of the
    ///   leaf come from the PointDataGrid in use), find the bounds of its
    ///   index space activity and return these bounds at the index space of
    ///   the target surface grid
    inline CoordBBox toSurfaceBounds(const CoordBBox& bounds) const
    {
        if (bounds.empty()) return bounds;
        // Offset the point leaf bounds to the actual position of this node's
        // faces in index space (of the points), then convert this to the
        // corresponding index space of the cloest node bounds in the target
        // surface grid
        const BBoxd wsbounds(
            bounds.min().asVec3d() - 0.5,
            bounds.max().asVec3d() + 0.5);
        return mSurfaceTransform.worldToIndexCellCentered(
            mPointsTransform.indexToWorld(wsbounds));
    }

protected:
    std::unique_ptr<MaskTreeT> mMask;
    std::unique_ptr<MaskTreeT> mMaskOff;
    const math::Transform& mPointsTransform;
    const math::Transform& mSurfaceTransform;
    const CoordBBox mMaxClipBounds;
    InterrupterT* mInterrupter;
};

/// @brief Initializes a fixed activity mask
template <typename MaskTreeT,
    typename InterrupterT = util::NullInterrupter>
struct FixedSurfaceMaskOp
    : public SurfaceMaskOp<MaskTreeT, InterrupterT>
{
    using BaseT = SurfaceMaskOp<MaskTreeT, InterrupterT>;
    using LeafManagerT =
        tree::LeafManager<const points::PointDataTree>;

    FixedSurfaceMaskOp(const math::Transform& points,
                   const math::Transform& surface,
                   const double minBandRadius, // sdf index space
                   const double maxBandRadius, // sdf index space
                   const CoordBBox* const maxClipBounds,
                   InterrupterT* interrupter = nullptr)
        : BaseT(points, surface, maxClipBounds, interrupter)
        , mMin(), mMax()
    {
        // calculate the min interior cube area of activity. this is the side
        // of the largest possible cube that fits into the radius "min":
        //   d = 2r -> 2r = 3x^2 -> x = 2r / sqrt(3)
        // Half side of the cube which fits into the sphere with radius minBandRadius
        const Real halfside = ((2.0 * minBandRadius) / std::sqrt(3.0)) / 2.0;
        assert(halfside >= 0.0); // minBandRadius shouldn't be negative
        // Round down to avoid deactivating partially occluded voxels
        const int32_t min = static_cast<int32_t>(std::max(0.0, halfside));
        // mMin is the distance from the nodes bounding box that we can
        // deactivate. Because we don't know the point positions here, we
        // can only deactivate based on the worst scenario (that is, we can
        // only deactivate entire leaf nodes, and we can only do so if we are
        // sure they are going to be encompassed by any single sphere). So take
        // the min distance and see how many leaf nodes the half distance
        // encompasses entirely.
        const int32_t nodes = min / MaskTreeT::LeafNodeType::DIM;
        assert(nodes >= 0);
        // Back to voxel dim (minus 1 as we expand out from a leaf node)
        mMin = (nodes-1) * MaskTreeT::LeafNodeType::DIM;
        mMax = static_cast<int32_t>(math::Round(maxBandRadius)); // furthest voxel
    }

    FixedSurfaceMaskOp(const FixedSurfaceMaskOp& other, tbb::split)
        : BaseT(other), mMin(other.mMin), mMax(other.mMax) {}

    void operator()(const typename LeafManagerT::LeafRange& range)
    {
        if (this->interrupted()) return;
        for (auto leaf = range.begin(); leaf; ++leaf) {
            this->activate(*leaf, mMax);
        }
        if (mMin < 0) return;
        for (auto leaf = range.begin(); leaf; ++leaf) {
            this->deactivate(*leaf, mMin);
        }
    }

private:
    int32_t mMin, mMax;
};

/// @brief Initializes a variable activity mask
template <typename RadiusTreeT,
    typename MaskTreeT,
    typename InterrupterT = util::NullInterrupter>
struct VariableSurfaceMaskOp
    : public SurfaceMaskOp<MaskTreeT, InterrupterT>
{
    using BaseT = SurfaceMaskOp<MaskTreeT, InterrupterT>;
    using LeafManagerT =
        tree::LeafManager<const points::PointDataTree>;

    VariableSurfaceMaskOp(const math::Transform& pointsTransform,
                         const math::Transform& surfaceTransform,
                         const RadiusTreeT* const min,
                         const RadiusTreeT& max,
                         const Real minScale,
                         const Real maxScale,
                         const Real halfband,
                         const CoordBBox* const maxClipBounds,
                         InterrupterT* interrupter = nullptr)
        : BaseT(pointsTransform, surfaceTransform, maxClipBounds, interrupter)
        , mMin(min), mMax(max), mMinScale(minScale), mMaxScale(maxScale)
        , mHalfband(halfband) {}

    VariableSurfaceMaskOp(const VariableSurfaceMaskOp&) = default;
    VariableSurfaceMaskOp(const VariableSurfaceMaskOp& other, tbb::split)
        : VariableSurfaceMaskOp(other) {}

    void operator()(const typename LeafManagerT::LeafRange& range)
    {
        if (this->interrupted()) return;
        const tree::ValueAccessor<const RadiusTreeT> maxacc(mMax);
        for (auto leafIter = range.begin(); leafIter; ++leafIter) {
            const int32_t max = this->maxDist(maxacc.getValue(leafIter->origin()));
            this->activate(*leafIter, max);
        }

        if (!mMin) return;
        const tree::ValueAccessor<const RadiusTreeT> minacc(*mMin);
        for (auto leafIter = range.begin(); leafIter; ++leafIter) {
            const int32_t min = this->minDist(minacc.getValue(leafIter->origin()));
            if (min < 0) continue;
            this->deactivate(*leafIter, min);
        }
    }

private:
    inline int32_t maxDist(const typename RadiusTreeT::ValueType& maxRadiusWs) const
    {
        // max radius in index space
        const Real maxBandRadius = (Real(maxRadiusWs) * mMaxScale) + mHalfband;
        return static_cast<int32_t>(math::Round(maxBandRadius)); // furthest voxel
    }

    inline int32_t minDist(const typename RadiusTreeT::ValueType& minRadiusWs) const
    {
        // min radius in index space
        Real minBandRadius = math::Max(0.0, (Real(minRadiusWs) * mMinScale) - mHalfband);
        // calculate the min interior cube area of activity. this is the side
        // of the largest possible cube that fits into the radius "min":
        //   d = 2r -> 2r = 3x^2 -> x = 2r / sqrt(3)
        // Half side of the cube which fits into the sphere with radius minBandRadius
        const Real halfside = ((2.0 * minBandRadius) / std::sqrt(3.0)) / 2.0;
        assert(halfside >= 0.0); // minBandRadius shouldn't be negative
        // Round down to avoid deactivating partially occluded voxels
        const int32_t min = static_cast<int32_t>(std::max(0.0, halfside));
        // mMin is the distance from the nodes bounding box that we can
        // deactivate. Because we don't know the point positions here, we
        // can only deactivate based on the worst scenario (that is, we can
        // only deactivate entire leaf nodes if we are sure they are going
        // to be encompassed by any single sphere). So take the min distance
        // and see how many leaf nodes the half distance encompasses entirely.
        const int32_t nodes = min / MaskTreeT::LeafNodeType::DIM;
        assert(nodes >= 0);
        // Back to voxel dim (minus 1 as we expand out from a leaf node)
        return (nodes-1) * MaskTreeT::LeafNodeType::DIM;
    }

private:
    const RadiusTreeT* const mMin;
    const RadiusTreeT& mMax;
    const Real mMinScale, mMaxScale, mHalfband;
};

template <typename SdfT, typename MaskTreeT>
inline typename SdfT::Ptr
initSdfFromMasks(math::Transform::Ptr& transform,
        const typename SdfT::ValueType bg,
        std::unique_ptr<MaskTreeT> on,
        std::unique_ptr<MaskTreeT> off)
{
    typename SdfT::Ptr surface = SdfT::create(bg);
    surface->setTransform(transform);
    surface->setGridClass(GRID_LEVEL_SET);

    if (!off->empty()) {
        on->topologyDifference(*off);
        // union will copy empty nodes so prune them
        tools::pruneInactive(*on);
        surface->tree().topologyUnion(*on);
        // set off values to -background
        tree::ValueAccessor<const MaskTreeT> acc(*off);
        auto setOffOp = [acc](auto& iter) {
            if (acc.isValueOn(iter.getCoord())) {
                iter.modifyValue([](auto& v) { v = -v; });
            }
        };
        tools::foreach(surface->beginValueOff(), setOffOp,
            /*thread=*/true, /*shared=*/false);
    }
    else {
        surface->tree().topologyUnion(*on);
    }

    on.reset();
    off.reset();
    surface->tree().voxelizeActiveTiles();
    return surface;
}

template <typename SdfT, typename InterrupterT, typename PointDataGridT>
inline typename SdfT::Ptr
initFixedSdf(const PointDataGridT& points,
        math::Transform::Ptr transform,
        const typename SdfT::ValueType bg,
        const double minBandRadius,
        const double maxBandRadius,
        InterrupterT* interrupter)
{
    using LeafManagerT = tree::LeafManager<const typename PointDataGridT::TreeType>;
    using MaskTreeT = typename SdfT::TreeType::template ValueConverter<ValueMask>::Type;

    if (interrupter) interrupter->start("Generating uniform surface topology");

    FixedSurfaceMaskOp<MaskTreeT, InterrupterT> op(points.transform(),
       *transform, minBandRadius, maxBandRadius, /*clipbounds=*/nullptr, interrupter);

    LeafManagerT manager(points.tree());
    tbb::parallel_reduce(manager.leafRange(), op);

    typename SdfT::Ptr surface =
        initSdfFromMasks<SdfT, MaskTreeT>(transform, bg, op.mask(), op.maskoff());

    if (interrupter) interrupter->end();
    return surface;
}

template <typename SdfT,
    typename InterrupterT,
    typename PointDataGridT,
    typename RadiusTreeT>
inline typename SdfT::Ptr
initVariableSdf(const PointDataGridT& points,
        math::Transform::Ptr transform,
        const typename SdfT::ValueType bg,
        const RadiusTreeT& min,
        const RadiusTreeT& max,
        const Real scale,
        const Real halfband,
        InterrupterT* interrupter)
{
    using LeafManagerT = tree::LeafManager<const typename PointDataGridT::TreeType>;
    using MaskTreeT = typename SdfT::TreeType::template ValueConverter<ValueMask>::Type;

    if (interrupter) interrupter->start("Generating variable surface topology");

    VariableSurfaceMaskOp<RadiusTreeT, MaskTreeT, InterrupterT>
        op(points.transform(), *transform, &min, max, scale, scale, halfband,
            /*clipbounds=*/nullptr, interrupter);

    LeafManagerT manager(points.tree());
    tbb::parallel_reduce(manager.leafRange(), op);

    typename SdfT::Ptr surface =
        initSdfFromMasks<SdfT, MaskTreeT>(transform, bg, op.mask(), op.maskoff());

    if (interrupter) interrupter->end();
    return surface;
}

template <typename SdfT, typename InterrupterT, typename PointDataGridT>
inline typename SdfT::Ptr
initFixedSmoothSdf(const PointDataGridT& points,
        math::Transform::Ptr transform,
        const typename SdfT::ValueType bg,
        const Real maxBandRadius,
        const CoordBBox& bounds,
        InterrupterT* interrupter)
{
    using LeafManagerT = tree::LeafManager<const typename PointDataGridT::TreeType>;
    using MaskTreeT = typename SdfT::TreeType::template ValueConverter<ValueMask>::Type;

    if (interrupter) interrupter->start("Generating uniform surface topology");

    // @note Currently don't use min radii to deactivate, can't compute
    //   this with the ZB kernel
    FixedSurfaceMaskOp<MaskTreeT, InterrupterT> op(points.transform(),
       *transform, /*minBandRadius=*/0.0, maxBandRadius, &bounds, interrupter);

    LeafManagerT manager(points.tree());
    tbb::parallel_reduce(manager.leafRange(), op);

    typename SdfT::Ptr surface =
        initSdfFromMasks<SdfT, MaskTreeT>(transform, bg, op.mask(), op.maskoff());

    if (interrupter) interrupter->end();
    return surface;
}

template <typename SdfT,
    typename InterrupterT,
    typename PointDataGridT,
    typename RadiusTreeT>
inline typename SdfT::Ptr
initVariableSmoothSdf(const PointDataGridT& points,
        math::Transform::Ptr transform,
        const typename SdfT::ValueType bg,
        const RadiusTreeT& maxTree,
        const Real scale,
        const Real halfband,
        const CoordBBox& bounds,
        InterrupterT* interrupter)
{
    using LeafManagerT = tree::LeafManager<const typename PointDataGridT::TreeType>;
    using MaskTreeT = typename SdfT::TreeType::template ValueConverter<ValueMask>::Type;

    if (interrupter) interrupter->start("Generating variable surface topology");

    // @note Currently don't use min radii/tree to deactivate, can't
    //   compute this with the ZB kernel
    VariableSurfaceMaskOp<RadiusTreeT, MaskTreeT, InterrupterT>
        op(points.transform(), *transform, nullptr, maxTree, /*minscale=*/1.0,
           scale, halfband, &bounds, interrupter);

    LeafManagerT manager(points.tree());
    tbb::parallel_reduce(manager.leafRange(), op);

    typename SdfT::Ptr surface =
        initSdfFromMasks<SdfT, MaskTreeT>(transform, bg, op.mask(), op.maskoff());

    if (interrupter) interrupter->end();
    return surface;
}

template <typename PointDataTreeT,
    typename AttributeTypes>
inline GridPtrVec
transferAttributes(const tree::LeafManager<const PointDataTreeT>& manager,
                   const std::vector<std::string>& attributes,
                   const Int64Tree& cpg,
                   const math::Transform::Ptr transform)
{
    assert(manager.leafCount() != 0);
    // masking uses upper 32 bits for leaf node id
    // @note we can use a point list impl to support larger counts
    // if necessary but this is far faster
    assert(manager.leafCount() <
        size_t(std::numeric_limits<Index>::max()));

    // linearise cpg to avoid having to probe data
    const tree::LeafManager<const Int64Tree> cpmanager(cpg);

    auto transfer = [&](auto& tree, const size_t attrIdx) {
        using TreeType = typename std::decay<decltype(tree)>::type;
        using HandleT = AttributeHandle<typename TreeType::ValueType>;

        // init topology
        tree.topologyUnion(cpg);
        tree::LeafManager<TreeType> lm(tree);

        // init values
        lm.foreach([&manager, &cpmanager, attrIdx]
            (auto& leaf, const size_t idx)  {
            auto voxel = leaf.beginValueOn();
            if (!voxel) return;

            auto* data = leaf.buffer().data();
            const Int64* ids = cpmanager.leaf(idx).buffer().data();
            Index prev = Index(ids[voxel.pos()] >> 32);
            typename HandleT::UniquePtr handle(
                new HandleT(manager.leaf(prev).constAttributeArray(attrIdx)));

            for (; voxel; ++voxel) {
                const Int64 hash = ids[voxel.pos()];
                const Index lfid = Index(hash >> 32); // upper 32 bits to leaf id
                const Index ptid = static_cast<Index>(hash); // lower
                if (lfid != prev) {
                    handle.reset(new HandleT(manager.leaf(lfid).constAttributeArray(attrIdx)));
                    prev = lfid;
                }
                data[voxel.pos()] = handle->get(ptid);
            }
        });
    };

    GridPtrVec grids;
    grids.reserve(attributes.size());
    const auto& attrSet = manager.leaf(0).attributeSet();
    tbb::task_group tasks;

    for (const auto& name : attributes) {
        const size_t attrIdx = attrSet.find(name);
        if (attrIdx == points::AttributeSet::INVALID_POS) continue;
        if (attrSet.get(attrIdx)->stride() != 1) {
            OPENVDB_THROW(RuntimeError, "Transfer of attribute " + name +
               " not supported since it is strided");
        }

        const std::string& type = attrSet.descriptor().valueType(attrIdx);
        GridBase::Ptr grid = nullptr;
        AttributeTypes::foreach([&](const auto& v) {
            using ValueType = typename std::remove_const<typename std::decay<decltype(v)>::type>::type;
            using TreeT = typename PointDataTreeT::template ValueConverter<ValueType>::Type;
            if (!grid && typeNameAsString<ValueType>() == type) {
                auto typed = Grid<TreeT>::create();
                grid = typed;
                typed->setName(name);
                typed->setTransform(transform);
                tasks.run([typed, attrIdx, transfer] { transfer(typed->tree(), attrIdx); });
                grids.emplace_back(grid);
            }
        });

        if (!grid) {
            OPENVDB_THROW(RuntimeError, "No support for attribute type " + type +
                " built during closest point surface transfer");
        }
    }

    tasks.wait();
    return grids;
}

template <typename SdfT,
    template <typename, typename, typename, bool> class TransferInterfaceT,
    typename AttributeTypes,
    typename InterrupterT,
    typename PointDataGridT,
    typename FilterT,
    typename ...Args>
inline GridPtrVec
doRasterizeSurface(const PointDataGridT& points,
    const std::vector<std::string>& attributes,
    const FilterT& filter,
    SdfT& surface,
    InterrupterT* interrupter,
    Args&&... args)
{
    using RadRefT = typename std::tuple_element<1, std::tuple<Args...>>::type;
    using RadT = typename std::remove_reference<RadRefT>::type;

    GridPtrVec grids;
    const auto leaf = points.constTree().cbeginLeaf();
    if (!leaf) return grids;

    const size_t pidx = leaf->attributeSet().find("P");
    if (pidx == AttributeSet::INVALID_POS) {
        OPENVDB_THROW(RuntimeError, "Failed to find position attribute");
    }

    // @note  Can't split this out into a generic lambda yet as there
    // are compiler issues with capturing variadic arguments
    const NamePair& ptype = leaf->attributeSet().descriptor().type(pidx);

    if (attributes.empty()) {
        if (ptype.second == NullCodec::name()) {
            using TransferT = TransferInterfaceT<SdfT, NullCodec, RadT, false>;
            TransferT transfer(pidx, args...);
            rasterize<PointDataGridT, TransferT, FilterT, InterrupterT>
                (points, transfer, filter, interrupter);
        }
        else {
            using TransferT = TransferInterfaceT<SdfT, UnknownCodec, RadT, false>;
            TransferT transfer(pidx, args...);
            rasterize<PointDataGridT, TransferT, FilterT, InterrupterT>
                (points, transfer, filter, interrupter);
        }
    }
    else {
        Int64Tree cpg;
        cpg.topologyUnion(surface.tree());
        tree::LeafManager<const PointDataTree> manager(points.tree());
        // map point leaf nodes to their linear id
        // @todo sorted vector of leaf ptr-> index pair then lookup with binary search?
        std::unordered_map<const PointDataTree::LeafNodeType*, Index> ids;
        manager.foreach([&](auto& leafnode, size_t idx) { ids[&leafnode] = Index(idx); }, false);

        if (ptype.second == NullCodec::name()) {
            using TransferT = TransferInterfaceT<SdfT, NullCodec, RadT, true>;
            TransferT transfer(pidx, args..., &cpg, &ids);
            rasterize<PointDataGridT, TransferT, FilterT, InterrupterT>
                (points, transfer, filter, interrupter);
        }
        else {
            using TransferT = TransferInterfaceT<SdfT, UnknownCodec, RadT, true>;
            TransferT transfer(pidx, args..., &cpg, &ids);
            rasterize<PointDataGridT, TransferT, FilterT, InterrupterT>
                (points, transfer, filter, interrupter);
        }

        ids.clear();
        tools::pruneInactive(cpg);
        // Build attribute transfer grids
        grids = transferAttributes
            <typename PointDataGrid::TreeType, AttributeTypes>
                (manager, attributes, cpg, surface.transformPtr());
    }

    return grids;
}

template <typename PointDataGridT,
    typename SdfT,
    typename SettingsT>
GridPtrVec
rasterizeSpheres(const PointDataGridT& points,
                 const SettingsT& settings,
                 const typename SettingsT::FilterType& filter)
{
    static_assert(IsSpecializationOf<PointDataGridT, Grid>::value);
    static_assert(IsSpecializationOf<SettingsT, SphereSettings>::value);

    using AttributeTypes = typename SettingsT::AttributeTypes;
    using InterrupterType = typename SettingsT::InterrupterType;

    const std::vector<std::string>& attributes = settings.attributes;
    const Real halfband = settings.halfband;
    auto* interrupter = settings.interrupter;

    math::Transform::Ptr transform = settings.transform;
    if (!transform) transform = points.transform().copy();
    const Real vs = transform->voxelSize()[0];
    const typename SdfT::ValueType background =
        static_cast<typename SdfT::ValueType>(vs * halfband);

    typename SdfT::Ptr surface;
    GridPtrVec grids;

    if (settings.radius.empty())
    {
        // search distance at the SDF transform, including its half band
        const Real radiusIndexSpace = settings.radiusScale / vs;
        const FixedBandRadius<Real> rad(radiusIndexSpace, float(halfband));
        const Real minBandRadius = rad.min();
        const Real maxBandRadius = rad.max();
        const size_t width = static_cast<size_t>(math::RoundUp(maxBandRadius));

        surface = initFixedSdf<SdfT, InterrupterType>
            (points, transform, background, minBandRadius, maxBandRadius, interrupter);

        if (interrupter) interrupter->start("Rasterizing particles to level set using constant Spheres");

        grids = doRasterizeSurface<SdfT, SphericalTransfer, AttributeTypes, InterrupterType>
            (points, attributes, filter, *surface, interrupter,
                width, rad, points.transform(), *surface); // args
    }
    else {
        using RadiusT = typename SettingsT::RadiusAttributeType;
        using PointDataTreeT = typename PointDataGridT::TreeType;
        using RadTreeT = typename PointDataTreeT::template ValueConverter<RadiusT>::Type;

        RadiusT min(0), max(0);
        typename RadTreeT::Ptr mintree(new RadTreeT), maxtree(new RadTreeT);
        points::evalMinMax<RadiusT, UnknownCodec>
            (points.tree(), settings.radius, min, max, filter, mintree.get(), maxtree.get());

        // search distance at the SDF transform
        const RadiusT indexSpaceScale = RadiusT(settings.radiusScale / vs);
        surface = initVariableSdf<SdfT, InterrupterType>
            (points, transform, background, *mintree, *maxtree,
                indexSpaceScale, halfband, interrupter);
        mintree.reset();
        maxtree.reset();

        const auto leaf = points.constTree().cbeginLeaf();
        if (!leaf) return GridPtrVec(1, surface);

        // max possible index space radius
        const size_t width = static_cast<size_t>
            (math::RoundUp((Real(max) * indexSpaceScale) + Real(halfband)));

        const size_t ridx = leaf->attributeSet().find(settings.radius);
        if (ridx == AttributeSet::INVALID_POS) {
            OPENVDB_THROW(RuntimeError, "Failed to find radius attribute \"" + settings.radius + "\"");
        }
        VaryingBandRadius<RadiusT> rad(ridx, RadiusT(halfband), indexSpaceScale);

        if (interrupter) interrupter->start("Rasterizing particles to level set using variable Spheres");

        grids = doRasterizeSurface<SdfT, SphericalTransfer, AttributeTypes, InterrupterType>
            (points, attributes, filter, *surface, interrupter,
                width, rad, points.transform(), *surface); // args
    }

    if (interrupter) interrupter->end();

    tools::pruneLevelSet(surface->tree());
    grids.insert(grids.begin(), surface);
    return grids;
}


template <typename PointDataGridT,
    typename SdfT,
    typename SettingsT>
GridPtrVec
rasterizeSmoothSpheres(const PointDataGridT& points,
                       const SettingsT& settings,
                       const typename SettingsT::FilterType& filter)
{
    static_assert(IsSpecializationOf<PointDataGridT, Grid>::value);
    static_assert(IsSpecializationOf<SettingsT, SmoothSphereSettings>::value);

    using AttributeTypes = typename SettingsT::AttributeTypes;
    using InterrupterType = typename SettingsT::InterrupterType;

    const std::vector<std::string>& attributes = settings.attributes;
    const Real halfband = settings.halfband;
    auto* interrupter = settings.interrupter;

    math::Transform::Ptr transform = settings.transform;
    if (!transform) transform = points.transform().copy();
    const Real vs = transform->voxelSize()[0];
    const typename SdfT::ValueType background =
        static_cast<typename SdfT::ValueType>(vs * halfband);

    const Real indexSpaceSearch = settings.searchRadius / vs;
    // max possible index space search radius
    const size_t width = static_cast<size_t>(math::RoundUp(indexSpaceSearch));

    // The topology we need to activate is at a distance based on the maximum
    // radii of each point and the uniform search radius. Even though we're
    // guaranteed to be generating new positions within the distribution of
    // point neighbours, these positions may end up outside of active topology
    // were we _only_ to use the radius of the particles for topology
    // activation.
    const Real maxActivationRadius = std::max(settings.searchRadius, settings.radiusScale) / vs;
    const auto leaf = points.constTree().cbeginLeaf();

    // Compute estimated max bounds for clipping. This is used if the search
    // radius is larger than the max particle radius (as we don't need to
    // activate topology further outside the bounds of the point data grid).
    // This bounds is expanded by the halfband + max radii
    CoordBBox bounds;
    points.tree().evalLeafBoundingBox(bounds);

    typename SdfT::Ptr surface;
    GridPtrVec grids;

    if (settings.radius.empty())
    {
        // This is the max possible distance we need to activate, but we'll
        // clip this at the edges of the point bounds (as the ZB kernel will
        // only create positions in between points).
        const FixedBandRadius<Real> bands(maxActivationRadius, float(halfband));
        const Real max = bands.max();

        // Compute max radius in index space and expand bounds
        bounds.expand(static_cast<Coord::ValueType>(halfband + math::Round(settings.radiusScale / vs)));
        // init surface
        surface = initFixedSmoothSdf<SdfT, InterrupterType>
            (points, transform, background, max, bounds, interrupter);

        if (!leaf) return GridPtrVec(1, surface);

        const FixedRadius<Real> rad(settings.radiusScale / vs);
        if (interrupter) interrupter->start("Rasterizing particles to level set using constant Zhu-Bridson");

        grids = doRasterizeSurface<SdfT, AveragePositionTransfer, AttributeTypes, InterrupterType>
            (points, attributes, filter, *surface, interrupter,
                width, rad, indexSpaceSearch, points.transform(), *surface); // args
    }
    else
    {
        using RadiusT = typename SettingsT::RadiusAttributeType;
        using PointDataTreeT = typename PointDataGridT::TreeType;
        using RadTreeT = typename PointDataTreeT::template ValueConverter<RadiusT>::Type;

        // We currently don't use the min values for the ZB kernel topology
        // activation.
        // @todo  We should be able to deactivate on some metric
        RadiusT min(0), max(0);
        typename RadTreeT::Ptr maxtree(new RadTreeT);
        points::evalMinMax<RadiusT, UnknownCodec>
            (points.tree(), settings.radius, min, max, filter, nullptr, maxtree.get());

        if ((settings.searchRadius > settings.radiusScale) && (min < settings.searchRadius)) {
            // set radius tree values to search distances if they are less,
            // just for the surface topology initialization. This is the max
            // possible distance we need to activate, but we'll clip this at
            // the edges of the point bounds (as the ZB kernel will only
            // create positions in-between points).
            tools::foreach(maxtree->beginValueOn(),
                [&](auto& iter) {
                    iter.modifyValue([&](auto& r) {
                        if (r < settings.searchRadius) {
                            // initVariableSmoothSdf scales radii by (settings.radiusScale/vs).
                            // We don't want to scale the search radii by the radius scale, so
                            // cancel it out here.
                            r = static_cast<RadiusT>(settings.searchRadius / settings.radiusScale);
                        }
                    });
                }, /*thread=*/true, /*shared=*/true);
        }

        const RadiusT indexSpaceScale = RadiusT(settings.radiusScale / vs);
        // Compute max radius in index space and expand bounds
        bounds.expand(static_cast<Coord::ValueType>(halfband + math::Round(max * indexSpaceScale)));

        // init surface
        surface = initVariableSmoothSdf<SdfT, InterrupterType>
            (points, transform, background, *maxtree,
                indexSpaceScale, halfband, bounds, interrupter);
        maxtree.reset();

        if (!leaf) return GridPtrVec(1, surface);

        const size_t ridx = leaf->attributeSet().find(settings.radius);
        if (ridx == AttributeSet::INVALID_POS) {
            OPENVDB_THROW(RuntimeError, "Failed to find radius attribute");
        }

        VaryingRadius<RadiusT> rad(ridx, indexSpaceScale);
        if (interrupter) interrupter->start("Rasterizing particles to level set using variable Zhu-Bridson");

        grids = doRasterizeSurface<SdfT, AveragePositionTransfer, AttributeTypes, InterrupterType>
            (points, attributes, filter, *surface, interrupter,
                width, rad, indexSpaceSearch, points.transform(), *surface); // args
    }

    if (interrupter) interrupter->end();

    tools::pruneInactive(surface->tree());
    grids.insert(grids.begin(), surface);
    return grids;
}

/// @brief  prototype - definition lives in impl/PointRasterizeEllipsoidsSDF.h
template <typename PointDataGridT, typename SdfT, typename SettingsT>
GridPtrVec rasterizeEllipsoids(const PointDataGridT&, const SettingsT&, const typename SettingsT::FilterType&);

} // namespace rasterize_sdf_internal

/// @endcond

///////////////////////////////////////////////////
///////////////////////////////////////////////////

template <typename PointDataGridT,
    typename SdfT,
    typename SettingsT>
GridPtrVec
rasterizeSdf(const PointDataGridT& points, const SettingsT& settings)
{
    const typename SettingsT::FilterType* filter = settings.filter;

    if constexpr (!std::is_same<typename SettingsT::FilterType, NullFilter>::value) {
        // To avoid rasterizeSdf invoking (at compile time) its sub methods for
        // both NullFilter and a custom filter, disallow the filter value on the
        // settings structs to be a nullptr. We allow it for NullFilters where
        // we can create a trivial static instance below and use that instead.
        if (!filter) {
            OPENVDB_THROW(RuntimeError,
                "A nullptr for a custom point-filter cannot be passed to rasterizeSdf().");
        }
    }
    else {
        if (!filter) {
            // We create a dummy static instance for NullFilters if none has
            // been provided
            static const NullFilter sNullFilter;
            filter = &sNullFilter;
        }
    }
    assert(filter);

    if constexpr(IsSpecializationOf<SettingsT, SphereSettings>::value) {
        return rasterize_sdf_internal::rasterizeSpheres<PointDataGridT, SdfT, SettingsT>(points, settings, *filter);
    }
    else if constexpr(IsSpecializationOf<SettingsT, SmoothSphereSettings>::value) {
        return rasterize_sdf_internal::rasterizeSmoothSpheres<PointDataGridT, SdfT, SettingsT>(points, settings, *filter);
    }
    else if constexpr(IsSpecializationOf<SettingsT, EllipsoidSettings>::value) {
        return rasterize_sdf_internal::rasterizeEllipsoids<PointDataGridT, SdfT, SettingsT>(points, settings, *filter);
    }
    else {
        static_assert(!sizeof(SettingsT),
            "No valid implementation for provided rasterization settings exists.");
        return GridPtrVec(); // silences irrelevant compiler warnings
    }
}

///////////////////////////////////////////////////
///////////////////////////////////////////////////

/// @deprecated  The following API calls are deprecated in favour of the more
///   general rasterizeSdf<>() method which determines its behaviour based on
///   the passed settings struct. These methods were introduced in VDB 9.1.
///   so are not currently marked as deprecated but should be marked as such
///   from the first minor release after OpenVDB 11.0.0.

template <typename PointDataGridT,
    typename SdfT = typename PointDataGridT::template ValueConverter<float>::Type,
    typename FilterT = NullFilter,
    typename InterrupterT = util::NullInterrupter>
typename SdfT::Ptr
rasterizeSpheres(const PointDataGridT& points,
             const Real radius,
             const Real halfband = LEVEL_SET_HALF_WIDTH,
             math::Transform::Ptr transform = nullptr,
             const FilterT& filter = NullFilter(),
             InterrupterT* interrupter = nullptr)
{
    auto grids =
        rasterizeSpheres<PointDataGridT, TypeList<>, SdfT, FilterT, InterrupterT>
            (points, radius, {}, halfband, transform, filter, interrupter);
    return StaticPtrCast<SdfT>(grids.front());
}

template <typename PointDataGridT,
    typename RadiusT = float,
    typename SdfT = typename PointDataGridT::template ValueConverter<float>::Type,
    typename FilterT = NullFilter,
    typename InterrupterT = util::NullInterrupter>
typename SdfT::Ptr
rasterizeSpheres(const PointDataGridT& points,
             const std::string& radius,
             const Real scale = 1.0,
             const Real halfband = LEVEL_SET_HALF_WIDTH,
             math::Transform::Ptr transform = nullptr,
             const FilterT& filter = NullFilter(),
             InterrupterT* interrupter = nullptr)
{
    auto grids =
        rasterizeSpheres<PointDataGridT, TypeList<>, RadiusT, SdfT, FilterT, InterrupterT>
            (points, radius, {}, scale, halfband, transform, filter, interrupter);
    return StaticPtrCast<SdfT>(grids.front());
}

template <typename PointDataGridT,
    typename AttributeTypes,
    typename SdfT = typename PointDataGridT::template ValueConverter<float>::Type,
    typename FilterT = NullFilter,
    typename InterrupterT = util::NullInterrupter>
GridPtrVec
rasterizeSpheres(const PointDataGridT& points,
             const Real radius,
             const std::vector<std::string>& attributes,
             const Real halfband = LEVEL_SET_HALF_WIDTH,
             math::Transform::Ptr transform = nullptr,
             const FilterT& filter = NullFilter(),
             InterrupterT* interrupter = nullptr)
{
    SphereSettings<AttributeTypes, float, FilterT, InterrupterT> s;
    s.radius = "";
    s.radiusScale = radius;
    s.halfband = halfband;
    s.attributes = attributes;
    s.transform = transform;
    s.filter = &filter;
    s.interrupter = interrupter;
    return rasterizeSdf<PointDataGridT, SdfT>(points, s);
}

template <typename PointDataGridT,
    typename AttributeTypes,
    typename RadiusT = float,
    typename SdfT = typename PointDataGridT::template ValueConverter<float>::Type,
    typename FilterT = NullFilter,
    typename InterrupterT = util::NullInterrupter>
GridPtrVec
rasterizeSpheres(const PointDataGridT& points,
             const std::string& radius,
             const std::vector<std::string>& attributes,
             const Real scale = 1.0,
             const Real halfband = LEVEL_SET_HALF_WIDTH,
             math::Transform::Ptr transform = nullptr,
             const FilterT& filter = NullFilter(),
             InterrupterT* interrupter = nullptr)
{
    // mimics old behaviour - rasterize_sdf_internal::rasterizeSmoothSpheres
    // will fall back to uniform rasterization if the attribute doesn't exist.
    if (auto leaf = points.constTree().cbeginLeaf()) {
        const size_t ridx = leaf->attributeSet().find(radius);
        if (ridx == AttributeSet::INVALID_POS) {
            OPENVDB_THROW(RuntimeError, "Failed to find radius attribute \"" + radius + "\"");
        }
    }
    SphereSettings<AttributeTypes, RadiusT, FilterT, InterrupterT> s;
    s.radius = radius;
    s.radiusScale = scale;
    s.halfband = halfband;
    s.attributes = attributes;
    s.transform = transform;
    s.filter = &filter;
    s.interrupter = interrupter;
    return rasterizeSdf<PointDataGridT, SdfT>(points, s);
}

///////////////////////////////////////////////////

template <typename PointDataGridT,
    typename SdfT = typename PointDataGridT::template ValueConverter<float>::Type,
    typename FilterT = NullFilter,
    typename InterrupterT = util::NullInterrupter>
typename SdfT::Ptr
rasterizeSmoothSpheres(const PointDataGridT& points,
             const Real radius,
             const Real searchRadius,
             const Real halfband = LEVEL_SET_HALF_WIDTH,
             math::Transform::Ptr transform = nullptr,
             const FilterT& filter = NullFilter(),
             InterrupterT* interrupter = nullptr)
{
    auto grids =
        rasterizeSmoothSpheres<PointDataGridT, TypeList<>, SdfT, FilterT, InterrupterT>
            (points, radius, searchRadius, {}, halfband, transform, filter, interrupter);
    return StaticPtrCast<SdfT>(grids.front());
}

template <typename PointDataGridT,
    typename RadiusT = float,
    typename SdfT = typename PointDataGridT::template ValueConverter<float>::Type,
    typename FilterT = NullFilter,
    typename InterrupterT = util::NullInterrupter>
typename SdfT::Ptr
rasterizeSmoothSpheres(const PointDataGridT& points,
                 const std::string& radius,
                 const Real radiusScale,
                 const Real searchRadius,
                 const Real halfband = LEVEL_SET_HALF_WIDTH,
                 math::Transform::Ptr transform = nullptr,
                 const FilterT& filter = NullFilter(),
                 InterrupterT* interrupter = nullptr)
{
    auto grids =
        rasterizeSmoothSpheres<PointDataGridT, TypeList<>, RadiusT, SdfT, FilterT, InterrupterT>
            (points, radius, radiusScale, searchRadius, {}, halfband, transform, filter, interrupter);
    return StaticPtrCast<SdfT>(grids.front());
}

template <typename PointDataGridT,
    typename AttributeTypes,
    typename SdfT = typename PointDataGridT::template ValueConverter<float>::Type,
    typename FilterT = NullFilter,
    typename InterrupterT = util::NullInterrupter>
GridPtrVec
rasterizeSmoothSpheres(const PointDataGridT& points,
                 const Real radius,
                 const Real searchRadius,
                 const std::vector<std::string>& attributes,
                 const Real halfband = LEVEL_SET_HALF_WIDTH,
                 math::Transform::Ptr transform = nullptr,
                 const FilterT& filter = NullFilter(),
                 InterrupterT* interrupter = nullptr)
{
    SmoothSphereSettings<AttributeTypes, float, FilterT, InterrupterT> s;
    s.radius = "";
    s.radiusScale = radius;
    s.halfband = halfband;
    s.attributes = attributes;
    s.transform = transform;
    s.filter = &filter;
    s.interrupter = interrupter;
    s.searchRadius = searchRadius;
    return rasterizeSdf<PointDataGridT, SdfT>(points, s);
}

template <typename PointDataGridT,
    typename AttributeTypes,
    typename RadiusT = float,
    typename SdfT = typename PointDataGridT::template ValueConverter<float>::Type,
    typename FilterT = NullFilter,
    typename InterrupterT = util::NullInterrupter>
GridPtrVec
rasterizeSmoothSpheres(const PointDataGridT& points,
                 const std::string& radius,
                 const Real radiusScale,
                 const Real searchRadius,
                 const std::vector<std::string>& attributes,
                 const Real halfband = LEVEL_SET_HALF_WIDTH,
                 math::Transform::Ptr transform = nullptr,
                 const FilterT& filter = NullFilter(),
                 InterrupterT* interrupter = nullptr)
{
    // mimics old behaviour - rasterize_sdf_internal::rasterizeSmoothSpheres
    // will fall back to uniform rasterization if the attribute doesn't exist.
    if (auto leaf = points.constTree().cbeginLeaf()) {
        const size_t ridx = leaf->attributeSet().find(radius);
        if (ridx == AttributeSet::INVALID_POS) {
            OPENVDB_THROW(RuntimeError, "Failed to find radius attribute \"" + radius + "\"");
        }
    }
    SmoothSphereSettings<AttributeTypes, RadiusT, FilterT, InterrupterT> s;
    s.radius = radius;
    s.radiusScale = radiusScale;
    s.halfband = halfband;
    s.attributes = attributes;
    s.transform = transform;
    s.filter = &filter;
    s.interrupter = interrupter;
    s.searchRadius = searchRadius;
    return rasterizeSdf<PointDataGridT, SdfT>(points, s);
}


} // namespace points
} // namespace OPENVDB_VERSION_NAME
} // namespace openvdb

#endif //OPENVDB_POINTS_RASTERIZE_SDF_IMPL_HAS_BEEN_INCLUDED
