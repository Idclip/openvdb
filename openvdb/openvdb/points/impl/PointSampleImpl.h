// Copyright Contributors to the OpenVDB Project
// SPDX-License-Identifier: Apache-2.0

/// @author Nick Avramoussis, Francisco Gochez, Dan Bailey
///
/// @file PointSampleImpl.h
///

#ifndef OPENVDB_POINTS_POINT_SAMPLE_IMPL_HAS_BEEN_INCLUDED
#define OPENVDB_POINTS_POINT_SAMPLE_IMPL_HAS_BEEN_INCLUDED

namespace openvdb {
OPENVDB_USE_VERSION_NAMESPACE
namespace OPENVDB_VERSION_NAME {
namespace points {

/// @cond OPENVDB_DOCS_INTERNAL

namespace point_sample_internal {


template<typename FromType, typename ToType>
struct CompatibleTypes { enum { value = std::is_constructible<ToType, FromType>::value }; };

// Specializations for types that can be converted from source grid to target attribute
template<typename T> struct CompatibleTypes<
    T, T> {                             enum { value = true }; };
template<typename T> struct CompatibleTypes<
    T, math::Vec2<T>> {                 enum { value = true }; };
template<typename T> struct CompatibleTypes<
    T, math::Vec3<T>> {                 enum { value = true }; };
template<typename T> struct CompatibleTypes<
    T, math::Vec4<T>> {                 enum { value = true }; };
template<typename T> struct CompatibleTypes<
    math::Vec2<T>, math::Vec2<T>> {     enum { value = true }; };
template<typename T> struct CompatibleTypes<
    math::Vec3<T>, math::Vec3<T>> {     enum { value = true }; };
template<typename T> struct CompatibleTypes<
    math::Vec4<T>, math::Vec4<T>> {     enum { value = true }; };
template<typename T0, typename T1> struct CompatibleTypes<
    math::Vec2<T0>, math::Vec2<T1>> {   enum { value = CompatibleTypes<T0, T1>::value }; };
template<typename T0, typename T1> struct CompatibleTypes<
    math::Vec3<T0>, math::Vec3<T1>> {   enum { value = CompatibleTypes<T0, T1>::value }; };
template<typename T0, typename T1> struct CompatibleTypes<
    math::Vec4<T0>, math::Vec4<T1>> {   enum { value = CompatibleTypes<T0, T1>::value }; };
template<typename T> struct CompatibleTypes<
    ValueMask, T> {                     enum { value = CompatibleTypes<bool, T>::value }; };


// Ability to access the Order and Staggered template parameter from tools::Sampler<Order, Staggered>
template <typename T> struct SamplerTraits {
    static const size_t Order = 0;
    static const bool Staggered = false;
};
template <size_t T0, bool T1> struct SamplerTraits<tools::Sampler<T0, T1>> {
    static const size_t Order = T0;
    static const bool Staggered = T1;
};


// default sampling is incompatible, so throw an error
template <typename ValueT, typename SamplerT, typename AccessorT, bool Round, bool Compatible = false>
struct SampleWithRoundingOp
{
    static inline void sample(ValueT&, const AccessorT&, const Vec3d&)
    {
        std::ostringstream ostr;
        ostr << "Cannot sample a " << typeNameAsString<typename AccessorT::ValueType>()
            << " grid on to a " << typeNameAsString<ValueT>() << " attribute";
        OPENVDB_THROW(TypeError, ostr.str());
    }
};
// partial specialization to handle sampling and rounding of compatible conversion
template <typename ValueT, typename SamplerT, typename AccessorT>
struct SampleWithRoundingOp<ValueT, SamplerT, AccessorT, /*Round=*/true, /*Compatible=*/true>
{
    static inline void sample(ValueT& value, const AccessorT& accessor, const Vec3d& position)
    {
        value = ValueT(math::Round(SamplerT::sample(accessor, position)));
    }
};
// partial specialization to handle sampling and simple casting of compatible conversion
template <typename ValueT, typename SamplerT, typename AccessorT>
struct SampleWithRoundingOp<ValueT, SamplerT, AccessorT, /*Round=*/false, /*Compatible=*/true>
{
    static inline void sample(ValueT& value, const AccessorT& accessor, const Vec3d& position)
    {
        value = ValueT(SamplerT::sample(accessor, position));
    }
};


template <typename PointDataGridT, typename SamplerT, typename FilterT, typename InterrupterT>
class PointDataSampler
{
public:
    PointDataSampler(size_t order,
                     PointDataGridT& points,
                     const SamplerT& sampler,
                     const FilterT& filter,
                     InterrupterT* const interrupter,
                     const bool threaded)
        : mOrder(order)
        , mPoints(points)
        , mSampler(sampler)
        , mFilter(filter)
        , mInterrupter(interrupter)
        , mThreaded(threaded) { }

private:
    // No-op transformation
    struct AlignedTransform
    {
        inline Vec3d transform(const Vec3d& position) const { return position; }
    }; // struct AlignedTransform

    // Re-sample world-space position from source to target transforms
    struct NonAlignedTransform
    {
        NonAlignedTransform(const math::Transform& source, const math::Transform& target)
            : mSource(source)
            , mTarget(target) { }

        inline Vec3d transform(const Vec3d& position) const
        {
            return mSource.worldToIndex(mTarget.indexToWorld(position));
        }

    private:
        const math::Transform& mSource;
        const math::Transform& mTarget;
    }; // struct NonAlignedTransform

    // A simple convenience wrapper that contains the source grid accessor and the sampler
    template <typename ValueT, typename SourceGridT, typename GridSamplerT>
    struct SamplerWrapper
    {
        using ValueType = ValueT;
        using SourceValueType = typename SourceGridT::ValueType;
        using SourceAccessorT = typename SourceGridT::ConstAccessor;

        // can only sample from a bool or mask grid using a PointSampler
        static const bool SourceIsBool = std::is_same<SourceValueType, bool>::value ||
            std::is_same<SourceValueType, ValueMask>::value;
        static const bool OrderIsZero = SamplerTraits<GridSamplerT>::Order == 0;
        static const bool IsValid = !SourceIsBool || OrderIsZero;

        SamplerWrapper(const SourceGridT& sourceGrid, const SamplerT& sampler)
            : mAccessor(sourceGrid.getConstAccessor())
            , mSampler(sampler) { }

        // note that creating a new accessor from the underlying tree is faster than
        // copying an existing accessor
        SamplerWrapper(const SamplerWrapper& other)
            : mAccessor(other.mAccessor.tree())
            , mSampler(other.mSampler) { }

        template <bool IsValidT = IsValid>
        inline typename std::enable_if<IsValidT, ValueT>::type
        sample(const Vec3d& position) const {
            return mSampler.template sample<ValueT, GridSamplerT, SourceAccessorT>(
                mAccessor, position);
        }

        template <bool IsValidT = IsValid>
        inline typename std::enable_if<!IsValidT, ValueT>::type
        sample(const Vec3d& /*position*/) const {
            OPENVDB_THROW(RuntimeError, "Cannot sample bool grid with BoxSampler or QuadraticSampler.");
        }

    private:
        SourceAccessorT mAccessor;
        const SamplerT& mSampler;
    }; // struct SamplerWrapper

    template <typename SamplerWrapperT, typename TransformerT>
    inline void doSample(const SamplerWrapperT& sampleWrapper, const Index targetIndex,
        const TransformerT& transformer)
    {
        using PointDataTreeT = typename PointDataGridT::TreeType;
        using LeafT = typename PointDataTreeT::LeafNodeType;
        using LeafManagerT = typename tree::LeafManager<PointDataTreeT>;

        const auto& filter(mFilter);
        const auto& interrupter(mInterrupter);

        auto sampleLambda = [targetIndex, &sampleWrapper, &transformer, &filter, &interrupter](
            LeafT& leaf, size_t /*idx*/)
        {
            using TargetHandleT = AttributeWriteHandle<typename SamplerWrapperT::ValueType>;

            if (util::wasInterrupted(interrupter)) {
                thread::cancelGroupExecution();
                return;
            }

            SamplerWrapperT newSampleWrapper(sampleWrapper);
            auto positionHandle = AttributeHandle<Vec3f>::create(leaf.constAttributeArray("P"));
            auto targetHandle = TargetHandleT::create(leaf.attributeArray(targetIndex));
            for (auto iter = leaf.beginIndexOn(filter); iter; ++iter) {
                const Vec3d position = transformer.transform(
                    positionHandle->get(*iter) + iter.getCoord().asVec3d());
                targetHandle->set(*iter, newSampleWrapper.sample(position));
            }
        };

        LeafManagerT leafManager(mPoints.tree());

        if (mInterrupter) mInterrupter->start();

        leafManager.foreach(sampleLambda, mThreaded);

        if (mInterrupter) mInterrupter->end();
    }

    template <typename SourceGridT, typename SamplerWrapperT>
    inline void resolveTransform(const SourceGridT& sourceGrid, const SamplerWrapperT& sampleWrapper,
        const Index targetIndex)
    {
        const auto& sourceTransform = sourceGrid.constTransform();
        const auto& pointsTransform = mPoints.constTransform();

        if (sourceTransform == pointsTransform) {
            AlignedTransform transformer;
            doSample(sampleWrapper, targetIndex, transformer);
        } else {
            NonAlignedTransform transformer(sourceTransform, pointsTransform);
            doSample(sampleWrapper, targetIndex, transformer);
        }
    }

    template <typename SourceGridT, typename TargetValueT, size_t Order>
    inline void resolveStaggered(const SourceGridT& sourceGrid, const Index targetIndex)
    {
        using SamplerWrapperT = SamplerWrapper<TargetValueT, SourceGridT, tools::Sampler<Order, false>>;
        using StaggeredSamplerWrapperT = SamplerWrapper<TargetValueT, SourceGridT, tools::Sampler<Order, true>>;

        using SourceValueType = typename SourceGridT::ValueType;
        if (VecTraits<SourceValueType>::Size == 3 && sourceGrid.getGridClass() == GRID_STAGGERED) {
            StaggeredSamplerWrapperT sampleWrapper(sourceGrid, mSampler);
            resolveTransform(sourceGrid, sampleWrapper, targetIndex);
        } else {
            SamplerWrapperT sampleWrapper(sourceGrid, mSampler);
            resolveTransform(sourceGrid, sampleWrapper, targetIndex);
        }
    }

public:
    template <typename SourceGridT, typename TargetValueT = typename SourceGridT::ValueType>
    inline void sample(const SourceGridT& sourceGrid, Index targetIndex)
    {
        using SourceValueType = typename SourceGridT::ValueType;
        static const bool SourceIsMask = std::is_same<SourceValueType, bool>::value ||
            std::is_same<SourceValueType, ValueMask>::value;

        if (SourceIsMask || mOrder == 0) {
            resolveStaggered<SourceGridT, TargetValueT, 0>(sourceGrid, targetIndex);
        } else if (mOrder == 1) {
            resolveStaggered<SourceGridT, TargetValueT, 1>(sourceGrid, targetIndex);
        } else if (mOrder == 2) {
            resolveStaggered<SourceGridT, TargetValueT, 2>(sourceGrid, targetIndex);
        }
    }

private:
    size_t mOrder;
    PointDataGridT& mPoints;
    const SamplerT& mSampler;
    const FilterT& mFilter;
    InterrupterT* const mInterrupter;
    const bool mThreaded;
}; // class PointDataSampler


template <typename PointDataGridT, typename ValueT>
struct AppendAttributeOp
{
    static void append(PointDataGridT& points, const Name& attribute)
    {
        appendAttribute<ValueT>(points.tree(), attribute);
    }
};
// partial specialization to disable attempts to append attribute type of DummySampleType
template <typename PointDataGridT>
struct AppendAttributeOp<PointDataGridT, DummySampleType>
{
    static void append(PointDataGridT&, const Name&) { }
};

} // namespace point_sample_internal

/// @endcond

////////////////////////////////////////


template<typename ValueT, typename SamplerT, typename AccessorT>
ValueT SampleWithRounding::sample(const AccessorT& accessor, const Vec3d& position) const
{
    using namespace point_sample_internal;
    using SourceValueT = typename AccessorT::ValueType;
    static const bool staggered = SamplerTraits<SamplerT>::Staggered;
    static const bool compatible = CompatibleTypes</*from=*/SourceValueT, /*to=*/ValueT>::value &&
                                   (!staggered || (staggered && VecTraits<SourceValueT>::Size == 3));
    static const bool round =   std::is_floating_point<SourceValueT>::value &&
                                std::is_integral<ValueT>::value;
    ValueT value;
    SampleWithRoundingOp<ValueT, SamplerT, AccessorT, round, compatible>::sample(
        value, accessor, position);
    return value;
}


////////////////////////////////////////


template<typename PointDataGridT, typename SourceGridT, typename TargetValueT,
    typename SamplerT, typename FilterT, typename InterrupterT>
inline void sampleGrid( size_t order,
                        PointDataGridT& points,
                        const SourceGridT& sourceGrid,
                        const Name& targetAttribute,
                        const FilterT& filter,
                        const SamplerT& sampler,
                        InterrupterT* const interrupter,
                        const bool threaded)
{
    using point_sample_internal::AppendAttributeOp;
    using point_sample_internal::PointDataSampler;

    // use the name of the grid if no target attribute name supplied
    Name attribute(targetAttribute);
    if (targetAttribute.empty()) {
        attribute = sourceGrid.getName();
    }

    // we do not allow sampling onto the "P" attribute
    if (attribute == "P") {
        OPENVDB_THROW(RuntimeError, "Cannot sample onto the \"P\" attribute");
    }

    auto leaf = points.tree().cbeginLeaf();
    if (!leaf)  return;

    PointDataSampler<PointDataGridT, SamplerT, FilterT, InterrupterT> pointDataSampler(
        order, points, sampler, filter, interrupter, threaded);

    const auto& descriptor = leaf->attributeSet().descriptor();
    size_t targetIndex = descriptor.find(attribute);
    const bool attributeExists = targetIndex != AttributeSet::INVALID_POS;

    if (std::is_same<TargetValueT, DummySampleType>::value) {
        if (!attributeExists) {
            // append attribute of source grid value type
            appendAttribute<typename SourceGridT::ValueType>(points.tree(), attribute);
            targetIndex = leaf->attributeSet().descriptor().find(attribute);
            OPENVDB_ASSERT(targetIndex != AttributeSet::INVALID_POS);

            // sample using same type as source grid
            pointDataSampler.template sample<SourceGridT>(sourceGrid, Index(targetIndex));
        } else {
            auto targetIdx = static_cast<Index>(targetIndex);
            // attempt to explicitly sample using type of existing attribute
            const Name& targetType = descriptor.valueType(targetIndex);
            if (targetType == typeNameAsString<Vec3f>()) {
                pointDataSampler.template sample<SourceGridT, Vec3f>(sourceGrid, targetIdx);
            } else if (targetType == typeNameAsString<Vec3d>()) {
                pointDataSampler.template sample<SourceGridT, Vec3d>(sourceGrid, targetIdx);
            } else if (targetType == typeNameAsString<Vec3i>()) {
                pointDataSampler.template sample<SourceGridT, Vec3i>(sourceGrid, targetIdx);
            } else if (targetType == typeNameAsString<int8_t>()) {
                pointDataSampler.template sample<SourceGridT, int8_t>(sourceGrid, targetIdx);
            } else if (targetType == typeNameAsString<int16_t>()) {
                pointDataSampler.template sample<SourceGridT, int16_t>(sourceGrid, targetIdx);
            } else if (targetType == typeNameAsString<int32_t>()) {
                pointDataSampler.template sample<SourceGridT, int32_t>(sourceGrid, targetIdx);
            } else if (targetType == typeNameAsString<int64_t>()) {
                pointDataSampler.template sample<SourceGridT, int64_t>(sourceGrid, targetIdx);
            } else if (targetType == typeNameAsString<float>()) {
                pointDataSampler.template sample<SourceGridT, float>(sourceGrid, targetIdx);
            } else if (targetType == typeNameAsString<double>()) {
                pointDataSampler.template sample<SourceGridT, double>(sourceGrid, targetIdx);
            } else if (targetType == typeNameAsString<bool>()) {
                pointDataSampler.template sample<SourceGridT, bool>(sourceGrid, targetIdx);
            } else {
                std::ostringstream ostr;
                ostr << "Cannot sample attribute of type - " << targetType;
                OPENVDB_THROW(TypeError, ostr.str());
            }
        }
    } else {
        if (!attributeExists) {
            // append attribute of target value type
            // (point_sample_internal wrapper disables the ability to use DummySampleType)
            AppendAttributeOp<PointDataGridT, TargetValueT>::append(points, attribute);
            targetIndex = leaf->attributeSet().descriptor().find(attribute);
            OPENVDB_ASSERT(targetIndex != AttributeSet::INVALID_POS);
        }
        else {
            const Name targetType = typeNameAsString<TargetValueT>();
            const Name attributeType = descriptor.valueType(targetIndex);
            if (targetType != attributeType) {
                std::ostringstream ostr;
                ostr << "Requested attribute type " << targetType << " for sampling "
                    << " does not match existing attribute type " << attributeType;
                OPENVDB_THROW(TypeError, ostr.str());
            }
        }

        // sample using target value type
        pointDataSampler.template sample<SourceGridT, TargetValueT>(
            sourceGrid, static_cast<Index>(targetIndex));
    }
}

template<typename PointDataGridT, typename SourceGridT, typename FilterT, typename InterrupterT>
inline void pointSample(PointDataGridT& points,
                        const SourceGridT& sourceGrid,
                        const Name& targetAttribute,
                        const FilterT& filter,
                        InterrupterT* const interrupter)
{
    SampleWithRounding sampler;
    sampleGrid(/*order=*/0, points, sourceGrid, targetAttribute, filter, sampler, interrupter);
}

template<typename PointDataGridT, typename SourceGridT, typename FilterT, typename InterrupterT>
inline void boxSample(  PointDataGridT& points,
                        const SourceGridT& sourceGrid,
                        const Name& targetAttribute,
                        const FilterT& filter,
                        InterrupterT* const interrupter)
{
    SampleWithRounding sampler;
    sampleGrid(/*order=*/1, points, sourceGrid, targetAttribute, filter, sampler, interrupter);
}

template<typename PointDataGridT, typename SourceGridT, typename FilterT, typename InterrupterT>
inline void quadraticSample(PointDataGridT& points,
                            const SourceGridT& sourceGrid,
                            const Name& targetAttribute,
                            const FilterT& filter,
                            InterrupterT* const interrupter)
{
    SampleWithRounding sampler;
    sampleGrid(/*order=*/2, points, sourceGrid, targetAttribute, filter, sampler, interrupter);
}


////////////////////////////////////////


} // namespace points
} // namespace OPENVDB_VERSION_NAME
} // namespace openvdb

#endif // OPENVDB_POINTS_POINT_SAMPLE_IMPL_HAS_BEEN_INCLUDED
