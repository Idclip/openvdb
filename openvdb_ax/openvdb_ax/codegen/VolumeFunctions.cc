// Copyright Contributors to the OpenVDB Project
// SPDX-License-Identifier: MPL-2.0

/// @file codegen/VolumeFunctions.cc
///
/// @authors Nick Avramoussis, Richard Jones
///
/// @brief  Contains the function objects that define the functions used in
///   volume compute function generation, to be inserted into the FunctionRegistry.
///   These define the functions available when operating on volumes.
///   Also includes the definitions for the volume value retrieval and setting.
///

#include "Functions.h"
#include "FunctionTypes.h"
#include "Types.h"
#include "Utils.h"

#include "openvdb_ax/ast/AST.h"
#include "openvdb_ax/compiler/CompilerOptions.h"
#include "openvdb_ax/Exceptions.h"

#include <openvdb/version.h>
#include <openvdb/tools/Interpolation.h>

#include <unordered_map>
#include <cstdlib>
#include <cstring>

namespace openvdb {
OPENVDB_USE_VERSION_NAMESPACE
namespace OPENVDB_VERSION_NAME {

namespace ax {
namespace codegen {


namespace volume {

#define OPENVDB_AX_CHECK_MODULE_CONTEXT(B) \
    { \
        const llvm::Function* F = B.GetInsertBlock()->getParent(); \
        const llvm::Module* M = F ? F->getParent() : nullptr; \
        if (!M || M->getName() != "ax.volume.module") { \
            OPENVDB_THROW(AXCompilerError, "Function \"" << (F ? F->getName().str() : "unknown") << \
                "\" cannot be called for the current target:\"" << (M ? M->getName().str() : "unknown") << \
                "\". This function only runs on OpenVDB Grids (not OpenVDB Point Grids)."); \
        } \
    }

}

void appendAccessorArgument(std::vector<llvm::Value*>& args,
    llvm::IRBuilder<>& B,
    const ast::Attribute& attr)
{
    llvm::Function* compute = B.GetInsertBlock()->getParent();
    llvm::Module* M = compute->getParent();

    const std::string& globalName = attr.tokenname();
    llvm::Value* index = M->getNamedValue(globalName);
    assert(index);

    index = B.CreateLoad(index);
    llvm::Value* aptr = extractArgument(compute, "accessors");
    assert(aptr);
    aptr = B.CreateGEP(aptr, index);
    aptr = B.CreateLoad(aptr);
    args.emplace_back(aptr);
}

llvm::Value* getClassArgument(llvm::IRBuilder<>& B, const ast::Attribute& attr)
{
    llvm::Function* compute = B.GetInsertBlock()->getParent();
    llvm::Module* M = compute->getParent();

    const std::string& globalName = attr.tokenname();
    llvm::Value* index = M->getNamedValue(globalName);
    assert(index);

    index = B.CreateLoad(index);
    llvm::Value* cptr = extractArgument(compute, "class");
    assert(cptr);
    return B.CreateGEP(cptr, index);
}

void appendGridArgument(std::vector<llvm::Value*>& args,
    llvm::IRBuilder<>& B,
    const ast::Attribute& attr)
{
    llvm::Function* compute = B.GetInsertBlock()->getParent();
    llvm::Module* M = compute->getParent();

    const std::string& globalName = attr.tokenname();
    llvm::Value* index = M->getNamedValue(globalName);
    assert(index);

    index = B.CreateLoad(index);
    llvm::Value* tptr = extractArgument(compute, "grids");
    assert(tptr);
    tptr = B.CreateGEP(tptr, index);
    tptr = B.CreateLoad(tptr);
    args.emplace_back(tptr);
}

void appendAttributeISEL(std::vector<llvm::Value*>& args,
    llvm::IRBuilder<>& B,
    const ast::Attribute& attr)
{
    llvm::Type* type = llvmTypeFromToken(attr.type(), B.getContext());
    type = type->getPointerTo(0);
    llvm::Value* isel = llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(type));
    args.emplace_back(isel);
}

inline FunctionGroup::UniquePtr axcoordtooffset(const FunctionOptions& op)
{
    using LeafNodeT = openvdb::BoolGrid::TreeType::LeafNodeType;

    /// @warning This function assumes that the node in question is a LeafNode!
    ///   This means that the result of this method is ONLY correct if the
    ///   origin points to an existing leaf node, OR if the offset is zero.
    ///   Currently the VolumeExectuable processes non-leaf nodes (active tiles)
    ///   individually, so the offset for these nodes is always zero. Should
    ///   we need to processes a non-leaf node with a non-zero offset, this
    ///   function should be extended to take a "level" param from the parent
    ///   which identifies the node level and can thus be used to call the
    ///   appropriate offset logic.

    static auto generate = [](const std::vector<llvm::Value*>& args,
         llvm::IRBuilder<>& B) -> llvm::Value*
    {
        assert(args.size() == 1);
        OPENVDB_AX_CHECK_MODULE_CONTEXT(B);
        llvm::Value* x = B.CreateConstGEP2_64(args[0], 0, 0);
        llvm::Value* y = B.CreateConstGEP2_64(args[0], 0, 1);
        llvm::Value* z = B.CreateConstGEP2_64(args[0], 0, 2);
        llvm::Value* dimmin1 = LLVMType<int32_t>::get(B.getContext(), int32_t(LeafNodeT::DIM-1u));
        llvm::Value* l2d2 = LLVMType<int32_t>::get(B.getContext(), int32_t(2*LeafNodeT::LOG2DIM));
        llvm::Value* l2d = LLVMType<int32_t>::get(B.getContext(), int32_t(LeafNodeT::LOG2DIM));

        // ((xyz[0] & (DIM-1u)) << 2*Log2Dim)
        x = B.CreateLoad(x);
        x = binaryOperator(x, dimmin1, ast::tokens::BITAND, B);
        x = binaryOperator(x, l2d2, ast::tokens::SHIFTLEFT, B);

        // ((xyz[1] & (DIM-1u)) << Log2Dim)
        y = B.CreateLoad(y);
        y = binaryOperator(y, dimmin1, ast::tokens::BITAND, B);
        y = binaryOperator(y, l2d, ast::tokens::SHIFTLEFT, B);

        // (xyz[2] & (DIM-1u))
        z = B.CreateLoad(z);
        z = binaryOperator(z, dimmin1, ast::tokens::BITAND, B);

        return
            binaryOperator(z,
                binaryOperator(x, y, ast::tokens::PLUS, B),
                    ast::tokens::PLUS, B);
    };

    static auto coordtooffset =
        [](const openvdb::math::Vec3<int32_t>* iscoord)
    {
        const openvdb::Coord* ijk = reinterpret_cast<const openvdb::Coord*>(iscoord);
        return int32_t(LeafNodeT::coordToOffset(*ijk));
    };

    return FunctionBuilder("coordtooffset")
        .addSignature<int32_t(const openvdb::math::Vec3<int32_t>*)>(generate,
                (int32_t(*)(const openvdb::math::Vec3<int32_t>*))(coordtooffset))
        .setArgumentNames({"coord"})
        .addFunctionAttribute(llvm::Attribute::ReadOnly)
        .addFunctionAttribute(llvm::Attribute::NoRecurse)
        .addFunctionAttribute(llvm::Attribute::NoUnwind)
        .addFunctionAttribute(llvm::Attribute::AlwaysInline)
        .setConstantFold(op.mConstantFoldCBindings)
        .setPreferredImpl(op.mPrioritiseIR ? FunctionBuilder::IR : FunctionBuilder::C)
        .setDocumentation("Return the linear table offset of the given global or local coordinates.")
        .get();
}

inline FunctionGroup::UniquePtr axoffsettocoord(const FunctionOptions& op)
{
    using LeafNodeT = openvdb::BoolGrid::TreeType::LeafNodeType;

    /// @warning This function assumes that the node in question is a LeafNode!
    ///   This means that the result of this method is ONLY correct if the
    ///   origin points to an existing leaf node, OR if the offset is zero.
    ///   Currently the VolumeExectuable processes non-leaf nodes (active tiles)
    ///   individually, so the offset for these nodes is always zero. Should
    ///   we need to processes a non-leaf node with a non-zero offset, this
    ///   function should be extended to take a "level" param from the parent
    ///   which identifies the node level and can thus be used to call the
    ///   appropriate offset logic.

    static auto generate = [](const std::vector<llvm::Value*>& args,
         llvm::IRBuilder<>& B) -> llvm::Value*
    {
        assert(args.size() == 2);
        OPENVDB_AX_CHECK_MODULE_CONTEXT(B);

        llvm::Value* ijk = args[0];
        llvm::Value* offset = args[1];

        llvm::Value* l2d2 = LLVMType<int32_t>::get(B.getContext(), int32_t(2*LeafNodeT::LOG2DIM));
        llvm::Value* l2d = LLVMType<int32_t>::get(B.getContext(), int32_t(LeafNodeT::LOG2DIM));

        // (offset >> 2*Log2Dim)
        llvm::Value* x = binaryOperator(offset, l2d2, ast::tokens::SHIFTRIGHT, B);
        B.CreateStore(x, B.CreateConstGEP2_64(ijk, 0, 0));

        // (offset &= ((1<<2*Log2Dim)-1))
        static constexpr int32_t ymask = ((1<<2*LeafNodeT::LOG2DIM)-1);
        offset = binaryOperator(offset, B.getInt32(ymask), ast::tokens::BITAND, B);

        // (n >> Log2Dim)
        llvm::Value* y = binaryOperator(offset, l2d, ast::tokens::SHIFTRIGHT, B);
        B.CreateStore(y, B.CreateConstGEP2_64(ijk, 0, 1));

        // (n & ((1<<Log2Dim)-1))
        static constexpr int32_t zmask = ((1<<LeafNodeT::LOG2DIM)-1);
        llvm::Value* z = binaryOperator(offset, B.getInt32(zmask), ast::tokens::BITAND, B);
        B.CreateStore(z, B.CreateConstGEP2_64(ijk, 0, 2));
        return nullptr;
    };

    static auto offsetToCoord =
        [](openvdb::math::Vec3<int32_t>* out, const int32_t offset)
    {
        *out = LeafNodeT::offsetToLocalCoord(offset).asVec3i();
    };

    using OffsetToCoordT = void(openvdb::math::Vec3<int32_t>*, const int32_t);

    return FunctionBuilder("offsettocoord")
        .addSignature<OffsetToCoordT, true>(generate, (OffsetToCoordT*)(offsetToCoord))
        .setArgumentNames({"offset"})
        .addParameterAttribute(0, llvm::Attribute::NoAlias)
        .addParameterAttribute(0, llvm::Attribute::WriteOnly)
        .addParameterAttribute(0, llvm::Attribute::NoCapture)
        .addFunctionAttribute(llvm::Attribute::NoUnwind)
        .addFunctionAttribute(llvm::Attribute::NoRecurse)
        .addFunctionAttribute(llvm::Attribute::AlwaysInline)
        .setConstantFold(op.mConstantFoldCBindings)
        .setPreferredImpl(op.mPrioritiseIR ? FunctionBuilder::IR : FunctionBuilder::C)
        .setDocumentation("")
        .get();
}

inline FunctionGroup::UniquePtr axoffsettoglobalcoord(const FunctionOptions& op)
{
    using LeafNodeT = openvdb::BoolGrid::TreeType::LeafNodeType;

    /// @warning This function assumes that the node in question is a LeafNode!
    ///   This means that the result of this method is ONLY correct if the
    ///   origin points to an existing leaf node, OR if the offset is zero.
    ///   Currently the VolumeExectuable processes non-leaf nodes (active tiles)
    ///   individually, so the offset for these nodes is always zero. Should
    ///   we need to processes a non-leaf node with a non-zero offset, this
    ///   function should be extended to take a "level" param from the parent
    ///   which identifies the node level and can thus be used to call the
    ///   appropriate offset logic.

    auto generate = [op](const std::vector<llvm::Value*>& args,
         llvm::IRBuilder<>& B) -> llvm::Value*
    {
        assert(args.size() == 3);
        OPENVDB_AX_CHECK_MODULE_CONTEXT(B);

        llvm::Value* result = args[0];
        llvm::Value* offset = args[1];
        llvm::Value* origin = args[2];

        llvm::Value* local = axoffsettocoord(op)->execute({offset}, B);

        for (size_t i = 0; i < 3; ++i){
            llvm::Value* lx = B.CreateConstGEP2_64(local, 0, i);
            llvm::Value* ox = B.CreateConstGEP2_64(origin, 0, i);
            ox = binaryOperator(B.CreateLoad(ox), B.CreateLoad(lx), ast::tokens::PLUS, B);
            B.CreateStore(ox, B.CreateConstGEP2_64(result, 0, i));
        }

        return nullptr;
    };

    static auto offsetToGlobalCoord =
        [](openvdb::math::Vec3<int32_t>* out, const int32_t offset, const openvdb::math::Vec3<int32_t>* in)
    {
        auto coord = LeafNodeT::offsetToLocalCoord(offset);
        out->x() = coord.x() + in->x();
        out->y() = coord.y() + in->y();
        out->z() = coord.z() + in->z();
    };

    using OffsetToGlobalCoordT = void(openvdb::math::Vec3<int32_t>*,const int32_t,const openvdb::math::Vec3<int32_t>*);

    return FunctionBuilder("offsettoglobalcoord")
        .addSignature<OffsetToGlobalCoordT, true>(generate, (OffsetToGlobalCoordT*)(offsetToGlobalCoord))
        .setArgumentNames({"offset", "coord"})
        .addParameterAttribute(0, llvm::Attribute::NoAlias)
        .addParameterAttribute(0, llvm::Attribute::WriteOnly)
        .addParameterAttribute(2, llvm::Attribute::NoAlias)
        .addParameterAttribute(2, llvm::Attribute::ReadOnly)
        .addFunctionAttribute(llvm::Attribute::NoUnwind)
        .addFunctionAttribute(llvm::Attribute::AlwaysInline)
        .setConstantFold(op.mConstantFoldCBindings)
        .setPreferredImpl(op.mPrioritiseIR ? FunctionBuilder::IR : FunctionBuilder::C)
        .setDocumentation("")
        .get();
}

inline FunctionGroup::UniquePtr axindextoworld(const FunctionOptions& op)
{
    static auto indexToWorld =
        [](openvdb::math::Vec3<double>* out,
           const openvdb::math::Vec3<int32_t>* coord,
           const void* transform)
    {
        const openvdb::math::Transform* const transformPtr =
                static_cast<const openvdb::math::Transform*>(transform);
        const openvdb::Coord* ijk = reinterpret_cast<const openvdb::Coord*>(coord);
        *out = transformPtr->indexToWorld(*ijk);
    };

    using IndexToWorldT = void(openvdb::math::Vec3<double>*, const openvdb::math::Vec3<int32_t>*, const void*);

    return FunctionBuilder("indextoworld")
        .addSignature<IndexToWorldT, true>((IndexToWorldT*)(indexToWorld))
        .setArgumentNames({"coord", "transform"})
        .addParameterAttribute(0, llvm::Attribute::NoAlias)
        .addParameterAttribute(0, llvm::Attribute::WriteOnly)
        .addParameterAttribute(1, llvm::Attribute::NoAlias)
        .addParameterAttribute(1, llvm::Attribute::ReadOnly)
        .addFunctionAttribute(llvm::Attribute::NoUnwind)
        .addFunctionAttribute(llvm::Attribute::AlwaysInline)
        .setConstantFold(false)
        .setPreferredImpl(op.mPrioritiseIR ? FunctionBuilder::IR : FunctionBuilder::C)
        .setDocumentation("Converted the given index space coordiante to a world space value based on the currently executing volume.")
        .get();
}

inline FunctionGroup::UniquePtr axgetcoord(const FunctionOptions& op)
{
    auto generate = [op](const std::vector<llvm::Value*>&,
         llvm::IRBuilder<>& B) -> llvm::Value*
    {
        // Pull out parent function arguments
        llvm::Function* compute = B.GetInsertBlock()->getParent();
        OPENVDB_AX_CHECK_MODULE_CONTEXT(B);
        llvm::Value* origin = extractArgument(compute, "origin");
        llvm::Value* offset = extractArgument(compute, "offset");
        return axoffsettoglobalcoord(op)->execute({offset, origin}, B);
    };

    return FunctionBuilder("getcoord")
        .addSignature<openvdb::math::Vec3<int32_t>*()>(generate)
        .setEmbedIR(true)
        .setConstantFold(false)
        .addDependency("offsettoglobalcoord")
        .setPreferredImpl(op.mPrioritiseIR ? FunctionBuilder::IR : FunctionBuilder::C)
        .setDocumentation("Returns the current voxel's ijk index space coordiante.")
        .get();
}

template <size_t Index>
inline FunctionGroup::UniquePtr axgetcoord(const FunctionOptions& op)
{
    static_assert(Index <= 2, "Invalid index for axgetcoord");

    auto generate = [op](const std::vector<llvm::Value*>&,
         llvm::IRBuilder<>& B) -> llvm::Value*
    {
        llvm::Value* coord = axgetcoord(op)->execute({}, B);
        return B.CreateLoad(B.CreateConstGEP2_64(coord, 0, Index));
    };

    return FunctionBuilder((Index == 0 ? "getcoordx" : Index == 1 ? "getcoordy" : "getcoordz"))
        .addSignature<int32_t()>(generate)
        .setEmbedIR(true)
        .setConstantFold(false)
        .setPreferredImpl(op.mPrioritiseIR ? FunctionBuilder::IR : FunctionBuilder::C)
        .addDependency("getcoord")
        .setDocumentation((
             Index == 0 ? "Returns the current voxel's X index value in index space as an integer." :
             Index == 1 ? "Returns the current voxel's Y index value in index space as an integer." :
                          "Returns the current voxel's Z index value in index space as an integer."))
        .get();
}

inline FunctionGroup::UniquePtr axgetvoxelpws(const FunctionOptions& op)
{
    auto generate = [op](const std::vector<llvm::Value*>&,
         llvm::IRBuilder<>& B) -> llvm::Value*
    {
        OPENVDB_AX_CHECK_MODULE_CONTEXT(B);
        llvm::Function* compute = B.GetInsertBlock()->getParent();
        llvm::Value* transform = extractArgument(compute, "transforms");
        llvm::Value* wi = extractArgument(compute, "write_index");
        transform = B.CreateGEP(transform, wi);
        transform = B.CreateLoad(transform);
        llvm::Value* coord = axgetcoord(op)->execute({}, B);
        return axindextoworld(op)->execute({coord, transform}, B);
    };

    return FunctionBuilder("getvoxelpws")
        .addSignature<openvdb::math::Vec3<double>*()>(generate)
        .setEmbedIR(true)
        .setConstantFold(false)
        .addDependency("getcoord")
        .addDependency("indextoworld")
        .setPreferredImpl(op.mPrioritiseIR ? FunctionBuilder::IR : FunctionBuilder::C)
        .setDocumentation("Returns the current voxel's position in world space as a vector float.")
        .get();
}

inline FunctionGroup::UniquePtr axisactive(const FunctionOptions& op)
{
    static auto generate = [](const std::vector<llvm::Value*>&,
         llvm::IRBuilder<>& B) -> llvm::Value*
    {
        OPENVDB_AX_CHECK_MODULE_CONTEXT(B);
        // Pull out parent function arguments
        llvm::Function* compute = B.GetInsertBlock()->getParent();
        return extractArgument(compute, "active");
    };

    return FunctionBuilder("isactive")
        .addSignature<bool()>(generate)
        .setEmbedIR(true)
        .setConstantFold(false)
        .setPreferredImpl(op.mPrioritiseIR ? FunctionBuilder::IR : FunctionBuilder::C)
        .setDocumentation("Returns whether the current voxel or tile is active.")
        .get();
}

inline FunctionGroup::UniquePtr axsetvoxel(const FunctionOptions& op)
{
    static auto setvoxelptr =
        [](void* accessor,
           const openvdb::math::Vec3<int32_t>* coord,
           const int32_t level,
           const bool ison,
           const auto value)
    {
        using ValueType = typename std::remove_const
            <typename std::remove_pointer
                <decltype(value)>::type>::type;
        using GridType = typename openvdb::BoolGrid::ValueConverter<ValueType>::Type;
        using RootNodeType = typename GridType::TreeType::RootNodeType;
        using AccessorType = typename GridType::Accessor;

        assert(accessor);
        assert(coord);

        // set value only to avoid changing topology
        const openvdb::Coord* ijk = reinterpret_cast<const openvdb::Coord*>(coord);
        AccessorType* const accessorPtr = static_cast<AccessorType*>(accessor);

        if (level != -1) {
            assert(level >= 0);
            accessorPtr->addTile(Index(level), *ijk, *value, ison);
        }
        else {
            // Check the depth to avoid creating voxel topology for higher levels
            // @note  This option is not configurable outside of the executable
            const int depth = accessorPtr->getValueDepth(*ijk);
            if (depth == static_cast<int>(RootNodeType::LEVEL)) {
                // voxel/leaf level
                assert(accessorPtr->probeConstLeaf(*ijk));
                if (ison) accessorPtr->setValueOn(*ijk, *value);
                else      accessorPtr->setValueOff(*ijk, *value);
            }
            else {
                // If the current depth is not the maximum (i.e voxel/leaf level) then
                // we're iterating over tiles of an internal node (NodeT0 is the leaf level).
                // We can't call setValueOnly or other variants as this will forcer voxel
                // topology to be created. Whilst the VolumeExecutables runs in such a
                // way that this is safe, it's not desirable; we just want to change the
                // tile value. There is no easy way to do this; we have to set a new tile
                // with the same active state.
                // @warning This code assume that getValueDepth() is always called to force
                // a node cache.
                using NodeT1 = typename AccessorType::NodeT1;
                using NodeT2 = typename AccessorType::NodeT2;
                if (NodeT1* node = accessorPtr->template getNode<NodeT1>()) {
                    const openvdb::Index index = node->coordToOffset(*ijk);
                    assert(node->isChildMaskOff(index));
                    node->addTile(index, *value, ison);
                }
                else if (NodeT2* node = accessorPtr->template getNode<NodeT2>()) {
                    const openvdb::Index index = node->coordToOffset(*ijk);
                    assert(node->isChildMaskOff(index));
                    node->addTile(index, *value, ison);
                }
                else {
                    const int level = RootNodeType::LEVEL - depth;
                    accessorPtr->addTile(level, *ijk, *value, ison);
                }
            }
        }
    };

    static auto setvoxelstr =
        [](void* accessor,
           const openvdb::math::Vec3<int32_t>* coord,
           const int32_t level,
           const bool ison,
           codegen::String* value)
    {
        const std::string copy(value->str());
        setvoxelptr(accessor, coord, level, ison, &copy);
    };

    static auto setvoxel =
        [](void* accessor,
           const openvdb::math::Vec3<int32_t>* coord,
           const int32_t level,
           const bool ison,
           const auto value) {
        setvoxelptr(accessor, coord, level, ison, &value);
    };

    using SetVoxelD = void(void*, const openvdb::math::Vec3<int32_t>*, const int32_t, const bool, const double);
    using SetVoxelF = void(void*, const openvdb::math::Vec3<int32_t>*, const int32_t, const bool, const float);
    using SetVoxelI64 = void(void*, const openvdb::math::Vec3<int32_t>*, const int32_t, const bool, const int64_t);
    using SetVoxelI32 = void(void*, const openvdb::math::Vec3<int32_t>*, const int32_t, const bool, const int32_t);
    using SetVoxelI16 = void(void*, const openvdb::math::Vec3<int32_t>*, const int32_t, const bool, const int16_t);
    using SetVoxelB = void(void*, const openvdb::math::Vec3<int32_t>*, const int32_t, const bool, const bool);
    using SetVoxelV2D = void(void*, const openvdb::math::Vec3<int32_t>*, const int32_t, const bool, const openvdb::math::Vec2<double>*);
    using SetVoxelV2F = void(void*, const openvdb::math::Vec3<int32_t>*, const int32_t, const bool, const openvdb::math::Vec2<float>*);
    using SetVoxelV2I = void(void*, const openvdb::math::Vec3<int32_t>*, const int32_t, const bool, const openvdb::math::Vec2<int32_t>*);
    using SetVoxelV3D = void(void*, const openvdb::math::Vec3<int32_t>*, const int32_t, const bool, const openvdb::math::Vec3<double>*);
    using SetVoxelV3F = void(void*, const openvdb::math::Vec3<int32_t>*, const int32_t, const bool, const openvdb::math::Vec3<float>*);
    using SetVoxelV3I = void(void*, const openvdb::math::Vec3<int32_t>*, const int32_t, const bool, const openvdb::math::Vec3<int32_t>*);
    using SetVoxelV4D = void(void*, const openvdb::math::Vec3<int32_t>*, const int32_t, const bool, const openvdb::math::Vec4<double>*);
    using SetVoxelV4F = void(void*, const openvdb::math::Vec3<int32_t>*, const int32_t, const bool, const openvdb::math::Vec4<float>*);
    using SetVoxelV4I = void(void*, const openvdb::math::Vec3<int32_t>*, const int32_t, const bool, const openvdb::math::Vec4<int32_t>*);
    using SetVoxelM3D = void(void*, const openvdb::math::Vec3<int32_t>*, const int32_t, const bool, const openvdb::math::Mat3<double>*);
    using SetVoxelM3F = void(void*, const openvdb::math::Vec3<int32_t>*, const int32_t, const bool, const openvdb::math::Mat3<float>*);
    using SetVoxelM4D = void(void*, const openvdb::math::Vec3<int32_t>*, const int32_t, const bool, const openvdb::math::Mat4<double>*);
    using SetVoxelM4F = void(void*, const openvdb::math::Vec3<int32_t>*, const int32_t, const bool, const openvdb::math::Mat4<float>*);
    using SetVoxelStr = void(void*, const openvdb::math::Vec3<int32_t>*, const int32_t, const bool, codegen::String*);

    return FunctionBuilder("setvoxel")
        .addSignature<SetVoxelD>((SetVoxelD*)(setvoxel))
        .addSignature<SetVoxelF>((SetVoxelF*)(setvoxel))
        .addSignature<SetVoxelI64>((SetVoxelI64*)(setvoxel))
        .addSignature<SetVoxelI32>((SetVoxelI32*)(setvoxel))
        .addSignature<SetVoxelI16>((SetVoxelI16*)(setvoxel))
        .addSignature<SetVoxelB>((SetVoxelB*)(setvoxel))
            .addParameterAttribute(0, llvm::Attribute::NoAlias)
            .addParameterAttribute(0, llvm::Attribute::ReadOnly)
            .addParameterAttribute(0, llvm::Attribute::NoCapture)
            .addParameterAttribute(1, llvm::Attribute::ReadOnly)
            .addParameterAttribute(1, llvm::Attribute::NoCapture)
            .addFunctionAttribute(llvm::Attribute::NoUnwind)
            .addFunctionAttribute(llvm::Attribute::NoRecurse)
            .setConstantFold(false)
        .addSignature<SetVoxelV2D>((SetVoxelV2D*)(setvoxelptr))
        .addSignature<SetVoxelV2F>((SetVoxelV2F*)(setvoxelptr))
        .addSignature<SetVoxelV2I>((SetVoxelV2I*)(setvoxelptr))
        .addSignature<SetVoxelV3D>((SetVoxelV3D*)(setvoxelptr))
        .addSignature<SetVoxelV3F>((SetVoxelV3F*)(setvoxelptr))
        .addSignature<SetVoxelV3I>((SetVoxelV3I*)(setvoxelptr))
        .addSignature<SetVoxelV4D>((SetVoxelV4D*)(setvoxelptr))
        .addSignature<SetVoxelV4F>((SetVoxelV4F*)(setvoxelptr))
        .addSignature<SetVoxelV4I>((SetVoxelV4I*)(setvoxelptr))
        .addSignature<SetVoxelM3D>((SetVoxelM3D*)(setvoxelptr))
        .addSignature<SetVoxelM3F>((SetVoxelM3F*)(setvoxelptr))
        .addSignature<SetVoxelM4D>((SetVoxelM4D*)(setvoxelptr))
        .addSignature<SetVoxelM4F>((SetVoxelM4F*)(setvoxelptr))
        .addSignature<SetVoxelStr>((SetVoxelStr*)(setvoxelstr))
            .addParameterAttribute(0, llvm::Attribute::NoAlias)
            .addParameterAttribute(0, llvm::Attribute::ReadOnly)
            .addParameterAttribute(0, llvm::Attribute::NoCapture)
            .addParameterAttribute(1, llvm::Attribute::ReadOnly)
            .addParameterAttribute(1, llvm::Attribute::NoCapture)
            .addParameterAttribute(4, llvm::Attribute::NoAlias)
            .addParameterAttribute(4, llvm::Attribute::ReadOnly)
            .addParameterAttribute(4, llvm::Attribute::NoCapture)
            .addFunctionAttribute(llvm::Attribute::NoUnwind)
            .addFunctionAttribute(llvm::Attribute::NoRecurse)
            .setConstantFold(false)
        .setPreferredImpl(op.mPrioritiseIR ? FunctionBuilder::IR : FunctionBuilder::C)
        .setDocumentation("Internal function for setting the value of a voxel.")
        .get();
}

inline FunctionGroup::UniquePtr axgetvoxel(const FunctionOptions& op)
{
    static auto getvoxel =
        [](void* accessor,
           const openvdb::math::Vec3<int32_t>* coord,
           auto value)
    {
        using ValueType = typename std::remove_pointer<decltype(value)>::type;
        using GridType = typename openvdb::BoolGrid::ValueConverter<ValueType>::Type;
        using AccessorType = typename GridType::Accessor;

        assert(accessor);
        assert(coord);
        assert(value);

        const openvdb::Coord* ijk = reinterpret_cast<const openvdb::Coord*>(coord);
        (*value) = static_cast<const AccessorType*>(accessor)->getValue(*ijk);
    };

    static auto getvoxelstr =
        [](void* accessor,
           const openvdb::math::Vec3<int32_t>* coord,
           codegen::String* value)
    {
        using GridType = openvdb::BoolGrid::ValueConverter<std::string>::Type;
        using AccessorType = GridType::Accessor;

        assert(accessor);
        assert(coord);
        assert(value);

        const openvdb::Coord* ijk = reinterpret_cast<const openvdb::Coord*>(coord);
        const std::string& str = static_cast<const AccessorType*>(accessor)->getValue(*ijk);
        // Copy the string to AX's required representation
        *value = str;
    };

    static auto getvoxel_s2t =
        [](void* accessor,
           void* sourceTransform,
           void* targetTransform,
           const openvdb::math::Vec3<int32_t>* origin,
           const int32_t offset,
           auto value)
    {
        using ValueType = typename std::remove_pointer<decltype(value)>::type;
        using GridType = typename openvdb::BoolGrid::ValueConverter<ValueType>::Type;
        using LeafNodeT = typename GridType::TreeType::LeafNodeType;
        using AccessorType = typename GridType::Accessor;

        assert(accessor);
        assert(origin);
        assert(sourceTransform);
        assert(targetTransform);

        const AccessorType* const accessorPtr = static_cast<const AccessorType*>(accessor);
        const openvdb::math::Transform* const sourceTransformPtr =
                static_cast<const openvdb::math::Transform*>(sourceTransform);
        const openvdb::math::Transform* const targetTransformPtr =
                static_cast<const openvdb::math::Transform*>(targetTransform);

        const openvdb::Coord* ijk = reinterpret_cast<const openvdb::Coord*>(origin);
        auto coord = *ijk + LeafNodeT::offsetToLocalCoord(offset);
        coord = targetTransformPtr->worldToIndexCellCentered(sourceTransformPtr->indexToWorld(coord));
        (*value) = accessorPtr->getValue(coord);
    };

    static auto getvoxelstr_s2t =
        [](void* accessor,
           void* sourceTransform,
           void* targetTransform,
           const openvdb::math::Vec3<int32_t>* origin,
           const int32_t offset,
           codegen::String* value)
    {
        using GridType = typename openvdb::BoolGrid::ValueConverter<std::string>::Type;
        using LeafNodeT = typename GridType::TreeType::LeafNodeType;
        using AccessorType = typename GridType::Accessor;

        assert(accessor);
        assert(origin);
        assert(sourceTransform);
        assert(targetTransform);

        const AccessorType* const accessorPtr = static_cast<const AccessorType*>(accessor);
        const openvdb::math::Transform* const sourceTransformPtr =
                static_cast<const openvdb::math::Transform*>(sourceTransform);
        const openvdb::math::Transform* const targetTransformPtr =
                static_cast<const openvdb::math::Transform*>(targetTransform);

        const openvdb::Coord* ijk = reinterpret_cast<const openvdb::Coord*>(origin);
        auto coord = *ijk + LeafNodeT::offsetToLocalCoord(offset);
        coord = targetTransformPtr->worldToIndexCellCentered(sourceTransformPtr->indexToWorld(coord));
        const std::string& str = accessorPtr->getValue(coord);
        // Copy the string to AX's required representation
        *value = str;
    };

    using GetVoxelS2T_D = void(void*, void*, void*, const openvdb::math::Vec3<int32_t>*, int32_t, double*);
    using GetVoxelS2T_F = void(void*, void*, void*, const openvdb::math::Vec3<int32_t>*, int32_t, float*);
    using GetVoxelS2T_I64 = void(void*, void*, void*, const openvdb::math::Vec3<int32_t>*, int32_t, int64_t*);
    using GetVoxelS2T_I32 = void(void*, void*, void*, const openvdb::math::Vec3<int32_t>*, int32_t, int32_t*);
    using GetVoxelS2T_I16 = void(void*, void*, void*, const openvdb::math::Vec3<int32_t>*, int32_t, int16_t*);
    using GetVoxelS2T_B = void(void*, void*, void*, const openvdb::math::Vec3<int32_t>*, int32_t, bool*);
    using GetVoxelS2T_V2D = void(void*, void*, void*, const openvdb::math::Vec3<int32_t>*, int32_t, openvdb::math::Vec2<double>*);
    using GetVoxelS2T_V2F = void(void*, void*, void*, const openvdb::math::Vec3<int32_t>*, int32_t, openvdb::math::Vec2<float>*);
    using GetVoxelS2T_V2I = void(void*, void*, void*, const openvdb::math::Vec3<int32_t>*, int32_t, openvdb::math::Vec2<int32_t>*);
    using GetVoxelS2T_V3D = void(void*, void*, void*, const openvdb::math::Vec3<int32_t>*, int32_t, openvdb::math::Vec3<double>*);
    using GetVoxelS2T_V3F = void(void*, void*, void*, const openvdb::math::Vec3<int32_t>*, int32_t, openvdb::math::Vec3<float>*);
    using GetVoxelS2T_V3I = void(void*, void*, void*, const openvdb::math::Vec3<int32_t>*, int32_t, openvdb::math::Vec3<int32_t>*);
    using GetVoxelS2T_V4D = void(void*, void*, void*, const openvdb::math::Vec3<int32_t>*, int32_t, openvdb::math::Vec4<double>*);
    using GetVoxelS2T_V4F = void(void*, void*, void*, const openvdb::math::Vec3<int32_t>*, int32_t, openvdb::math::Vec4<float>*);
    using GetVoxelS2T_V4I = void(void*, void*, void*, const openvdb::math::Vec3<int32_t>*, int32_t, openvdb::math::Vec4<int32_t>*);
    using GetVoxelS2T_M3D = void(void*, void*, void*, const openvdb::math::Vec3<int32_t>*, int32_t, openvdb::math::Mat3<double>*);
    using GetVoxelS2T_M3F = void(void*, void*, void*, const openvdb::math::Vec3<int32_t>*, int32_t, openvdb::math::Mat3<float>*);
    using GetVoxelS2T_M4D = void(void*, void*, void*, const openvdb::math::Vec3<int32_t>*, int32_t, openvdb::math::Mat4<double>*);
    using GetVoxelS2T_M4F = void(void*, void*, void*, const openvdb::math::Vec3<int32_t>*, int32_t, openvdb::math::Mat4<float>*);
    using GetVoxelS2T_Str = void(void*, void*, void*, const openvdb::math::Vec3<int32_t>*, int32_t, codegen::String*);

    using GetVoxelD = void(void*, const openvdb::math::Vec3<int32_t>*, double*);
    using GetVoxelF = void(void*, const openvdb::math::Vec3<int32_t>*, float*);
    using GetVoxelI64 = void(void*, const openvdb::math::Vec3<int32_t>*, int64_t*);
    using GetVoxelI32 = void(void*, const openvdb::math::Vec3<int32_t>*, int32_t*);
    using GetVoxelI16 = void(void*, const openvdb::math::Vec3<int32_t>*, int16_t*);
    using GetVoxelB = void(void*, const openvdb::math::Vec3<int32_t>*, bool*);
    using GetVoxelV2D = void(void*, const openvdb::math::Vec3<int32_t>*, openvdb::math::Vec2<double>*);
    using GetVoxelV2F = void(void*, const openvdb::math::Vec3<int32_t>*, openvdb::math::Vec2<float>*);
    using GetVoxelV2I = void(void*, const openvdb::math::Vec3<int32_t>*, openvdb::math::Vec2<int32_t>*);
    using GetVoxelV3D = void(void*, const openvdb::math::Vec3<int32_t>*, openvdb::math::Vec3<double>*);
    using GetVoxelV3F = void(void*, const openvdb::math::Vec3<int32_t>*, openvdb::math::Vec3<float>*);
    using GetVoxelV3I = void(void*, const openvdb::math::Vec3<int32_t>*, openvdb::math::Vec3<int32_t>*);
    using GetVoxelV4D = void(void*, const openvdb::math::Vec3<int32_t>*, openvdb::math::Vec4<double>*);
    using GetVoxelV4F = void(void*, const openvdb::math::Vec3<int32_t>*, openvdb::math::Vec4<float>*);
    using GetVoxelV4I = void(void*, const openvdb::math::Vec3<int32_t>*, openvdb::math::Vec4<int32_t>*);
    using GetVoxelM3D = void(void*, const openvdb::math::Vec3<int32_t>*, openvdb::math::Mat3<double>*);
    using GetVoxelM3F = void(void*, const openvdb::math::Vec3<int32_t>*, openvdb::math::Mat3<float>*);
    using GetVoxelM4D = void(void*, const openvdb::math::Vec3<int32_t>*, openvdb::math::Mat4<double>*);
    using GetVoxelM4F = void(void*, const openvdb::math::Vec3<int32_t>*, openvdb::math::Mat4<float>*);
    using GetVoxelStr = void(void*, const openvdb::math::Vec3<int32_t>*, codegen::String*);

    return FunctionBuilder("getvoxel")
        .addSignature<GetVoxelD>((GetVoxelD*)(getvoxel))
        .addSignature<GetVoxelF>((GetVoxelF*)(getvoxel))
        .addSignature<GetVoxelI64>((GetVoxelI64*)(getvoxel))
        .addSignature<GetVoxelI32>((GetVoxelI32*)(getvoxel))
        .addSignature<GetVoxelI16>((GetVoxelI16*)(getvoxel))
        .addSignature<GetVoxelB>((GetVoxelB*)(getvoxel))
        .addSignature<GetVoxelV2D>((GetVoxelV2D*)(getvoxel))
        .addSignature<GetVoxelV2F>((GetVoxelV2F*)(getvoxel))
        .addSignature<GetVoxelV2I>((GetVoxelV2I*)(getvoxel))
        .addSignature<GetVoxelV3D>((GetVoxelV3D*)(getvoxel))
        .addSignature<GetVoxelV3F>((GetVoxelV3F*)(getvoxel))
        .addSignature<GetVoxelV3I>((GetVoxelV3I*)(getvoxel))
        .addSignature<GetVoxelV4D>((GetVoxelV4D*)(getvoxel))
        .addSignature<GetVoxelV4F>((GetVoxelV4F*)(getvoxel))
        .addSignature<GetVoxelV4I>((GetVoxelV4I*)(getvoxel))
        .addSignature<GetVoxelM3F>((GetVoxelM3F*)(getvoxel))
        .addSignature<GetVoxelM3D>((GetVoxelM3D*)(getvoxel))
        .addSignature<GetVoxelM4F>((GetVoxelM4F*)(getvoxel))
        .addSignature<GetVoxelM4D>((GetVoxelM4D*)(getvoxel))
        .addSignature<GetVoxelStr>((GetVoxelStr*)(getvoxelstr))
            .addParameterAttribute(0, llvm::Attribute::NoAlias)
            .addParameterAttribute(1, llvm::Attribute::NoAlias)
            .addParameterAttribute(1, llvm::Attribute::ReadOnly)
            .addParameterAttribute(2, llvm::Attribute::WriteOnly)
            .addParameterAttribute(2, llvm::Attribute::NoAlias)
            .addFunctionAttribute(llvm::Attribute::NoUnwind)
            .addFunctionAttribute(llvm::Attribute::NoRecurse)
            .setConstantFold(false)
        .addSignature<GetVoxelS2T_D>((GetVoxelS2T_D*)(getvoxel_s2t))
        .addSignature<GetVoxelS2T_F>((GetVoxelS2T_F*)(getvoxel_s2t))
        .addSignature<GetVoxelS2T_I64>((GetVoxelS2T_I64*)(getvoxel_s2t))
        .addSignature<GetVoxelS2T_I32>((GetVoxelS2T_I32*)(getvoxel_s2t))
        .addSignature<GetVoxelS2T_I16>((GetVoxelS2T_I16*)(getvoxel_s2t))
        .addSignature<GetVoxelS2T_B>((GetVoxelS2T_B*)(getvoxel_s2t))
        .addSignature<GetVoxelS2T_V2D>((GetVoxelS2T_V2D*)(getvoxel_s2t))
        .addSignature<GetVoxelS2T_V2F>((GetVoxelS2T_V2F*)(getvoxel_s2t))
        .addSignature<GetVoxelS2T_V2I>((GetVoxelS2T_V2I*)(getvoxel_s2t))
        .addSignature<GetVoxelS2T_V3D>((GetVoxelS2T_V3D*)(getvoxel_s2t))
        .addSignature<GetVoxelS2T_V3F>((GetVoxelS2T_V3F*)(getvoxel_s2t))
        .addSignature<GetVoxelS2T_V3I>((GetVoxelS2T_V3I*)(getvoxel_s2t))
        .addSignature<GetVoxelS2T_V4D>((GetVoxelS2T_V4D*)(getvoxel_s2t))
        .addSignature<GetVoxelS2T_V4F>((GetVoxelS2T_V4F*)(getvoxel_s2t))
        .addSignature<GetVoxelS2T_V4I>((GetVoxelS2T_V4I*)(getvoxel_s2t))
        .addSignature<GetVoxelS2T_M3F>((GetVoxelS2T_M3F*)(getvoxel_s2t))
        .addSignature<GetVoxelS2T_M3D>((GetVoxelS2T_M3D*)(getvoxel_s2t))
        .addSignature<GetVoxelS2T_M4F>((GetVoxelS2T_M4F*)(getvoxel_s2t))
        .addSignature<GetVoxelS2T_M4D>((GetVoxelS2T_M4D*)(getvoxel_s2t))
        .addSignature<GetVoxelS2T_Str>((GetVoxelS2T_Str*)(getvoxelstr_s2t))
            .addParameterAttribute(0, llvm::Attribute::NoAlias)
            .addParameterAttribute(1, llvm::Attribute::NoAlias)
            .addParameterAttribute(1, llvm::Attribute::ReadOnly)
            .addParameterAttribute(2, llvm::Attribute::ReadOnly)
            .addParameterAttribute(3, llvm::Attribute::WriteOnly)
            .addParameterAttribute(3, llvm::Attribute::NoAlias)
            .addFunctionAttribute(llvm::Attribute::NoUnwind)
            .addFunctionAttribute(llvm::Attribute::NoRecurse)
            .setConstantFold(false)
        .setPreferredImpl(op.mPrioritiseIR ? FunctionBuilder::IR : FunctionBuilder::C)
        .setDocumentation("Internal function for setting the value of a voxel.")
        .get();
}

inline FunctionGroup::UniquePtr axprobevalue(const FunctionOptions& op)
{
    static auto probe =
        [](void* accessor,
           const openvdb::math::Vec3<int32_t>* coord,
           bool* ison,
           auto value)
    {
        using ValueType = typename std::remove_pointer<decltype(value)>::type;
        using GridType = typename openvdb::BoolGrid::ValueConverter<ValueType>::Type;
        using AccessorType = typename GridType::Accessor;
        assert(accessor);
        assert(coord);
        assert(value);
        assert(ison);

        const openvdb::Coord* ijk = reinterpret_cast<const openvdb::Coord*>(coord);
        *ison = static_cast<const AccessorType*>(accessor)->probeValue(*ijk, *value);
    };

    static auto probestr =
        [](void* accessor,
           const openvdb::math::Vec3<int32_t>* coord,
           bool* ison,
           codegen::String* value)
    {
        using GridType = openvdb::BoolGrid::ValueConverter<std::string>::Type;
        using AccessorType = GridType::Accessor;

        assert(accessor);
        assert(coord);
        assert(value);
        assert(ison);

        const openvdb::Coord* ijk = reinterpret_cast<const openvdb::Coord*>(coord);

        std::string str;
        *ison = static_cast<const AccessorType*>(accessor)->probeValue(*ijk, str);
        // Copy the string to AX's required representation
        *value = str;
    };

    using ProbeValueD = void(void*, const openvdb::math::Vec3<int32_t>*, bool*, double*);
    using ProbeValueF = void(void*, const openvdb::math::Vec3<int32_t>*, bool*, float*);
    using ProbeValueI64 = void(void*, const openvdb::math::Vec3<int32_t>*, bool*, int64_t*);
    using ProbeValueI32 = void(void*, const openvdb::math::Vec3<int32_t>*, bool*, int32_t*);
    using ProbeValueI16 = void(void*, const openvdb::math::Vec3<int32_t>*, bool*, int16_t*);
    using ProbeValueB = void(void*, const openvdb::math::Vec3<int32_t>*, bool*, bool*);
    using ProbeValueV2D = void(void*, const openvdb::math::Vec3<int32_t>*, bool*, openvdb::math::Vec2<double>*);
    using ProbeValueV2F = void(void*, const openvdb::math::Vec3<int32_t>*, bool*, openvdb::math::Vec2<float>*);
    using ProbeValueV2I = void(void*, const openvdb::math::Vec3<int32_t>*, bool*, openvdb::math::Vec2<int32_t>*);
    using ProbeValueV3D = void(void*, const openvdb::math::Vec3<int32_t>*, bool*, openvdb::math::Vec3<double>*);
    using ProbeValueV3F = void(void*, const openvdb::math::Vec3<int32_t>*, bool*, openvdb::math::Vec3<float>*);
    using ProbeValueV3I = void(void*, const openvdb::math::Vec3<int32_t>*, bool*, openvdb::math::Vec3<int32_t>*);
    using ProbeValueV4D = void(void*, const openvdb::math::Vec3<int32_t>*, bool*, openvdb::math::Vec4<double>*);
    using ProbeValueV4F = void(void*, const openvdb::math::Vec3<int32_t>*, bool*, openvdb::math::Vec4<float>*);
    using ProbeValueV4I = void(void*, const openvdb::math::Vec3<int32_t>*, bool*, openvdb::math::Vec4<int32_t>*);
    using ProbeValueM3D = void(void*, const openvdb::math::Vec3<int32_t>*, bool*, openvdb::math::Mat3<double>*);
    using ProbeValueM3F = void(void*, const openvdb::math::Vec3<int32_t>*, bool*, openvdb::math::Mat3<float>*);
    using ProbeValueM4D = void(void*, const openvdb::math::Vec3<int32_t>*, bool*, openvdb::math::Mat4<double>*);
    using ProbeValueM4F = void(void*, const openvdb::math::Vec3<int32_t>*, bool*, openvdb::math::Mat4<float>*);
    using ProbeValueStr = void(void*, const openvdb::math::Vec3<int32_t>*, bool*, codegen::String*);

    return FunctionBuilder("probevalue")
        .addSignature<ProbeValueD>((ProbeValueD*)(probe))
        .addSignature<ProbeValueF>((ProbeValueF*)(probe))
        .addSignature<ProbeValueI64>((ProbeValueI64*)(probe))
        .addSignature<ProbeValueI32>((ProbeValueI32*)(probe))
        .addSignature<ProbeValueI16>((ProbeValueI16*)(probe))
        .addSignature<ProbeValueB>((ProbeValueB*)(probe))
        .addSignature<ProbeValueV2D>((ProbeValueV2D*)(probe))
        .addSignature<ProbeValueV2F>((ProbeValueV2F*)(probe))
        .addSignature<ProbeValueV2I>((ProbeValueV2I*)(probe))
        .addSignature<ProbeValueV3D>((ProbeValueV3D*)(probe))
        .addSignature<ProbeValueV3F>((ProbeValueV3F*)(probe))
        .addSignature<ProbeValueV3I>((ProbeValueV3I*)(probe))
        .addSignature<ProbeValueV4D>((ProbeValueV4D*)(probe))
        .addSignature<ProbeValueV4F>((ProbeValueV4F*)(probe))
        .addSignature<ProbeValueV4I>((ProbeValueV4I*)(probe))
        .addSignature<ProbeValueM3F>((ProbeValueM3F*)(probe))
        .addSignature<ProbeValueM3D>((ProbeValueM3D*)(probe))
        .addSignature<ProbeValueM4F>((ProbeValueM4F*)(probe))
        .addSignature<ProbeValueM4D>((ProbeValueM4D*)(probe))
        .addSignature<ProbeValueStr>((ProbeValueStr*)(probestr))
            .addParameterAttribute(0, llvm::Attribute::NoAlias)
            .addParameterAttribute(1, llvm::Attribute::NoAlias)
            .addParameterAttribute(1, llvm::Attribute::ReadOnly)
            .addParameterAttribute(2, llvm::Attribute::WriteOnly)
            .addParameterAttribute(2, llvm::Attribute::NoAlias)
            .addParameterAttribute(3, llvm::Attribute::WriteOnly)
            .addParameterAttribute(3, llvm::Attribute::NoAlias)
        .setDocumentation("Internal function for getting the value of a voxel and its active state.")
        .get();
}

inline FunctionGroup::UniquePtr ax__voxel(const FunctionOptions& op)
{
    static auto voxel =
        [](auto* value,
           const openvdb::math::Vec3<int32_t>* coord,
           void* accessor,
           auto) //only used for prototype selection
    {
        assert(value);
        assert(coord);
        assert(accessor);
        const AccessorType* const aptr =
            static_cast<const AccessorType* const>(accessor);
        const openvdb::Coord* ijk =
            reinterpret_cast<const openvdb::Coord*>(coord);
        *value = aptr->getValue(*ijk);
    };

    using VoxelD = void(double*, const openvdb::math::Vec3<int32_t>*, void*, const double*);
    using VoxelF = void(float*, const openvdb::math::Vec3<int32_t>*, void*, const float*);
    using VoxelI64 = void(int64_t*, const openvdb::math::Vec3<int32_t>*, void*, const int64_t*);
    using VoxelI32 = void(int32_t*, const openvdb::math::Vec3<int32_t>*, void*, const int32_t*);
    using VoxelI16 = void(int16_t*, const openvdb::math::Vec3<int32_t>*, void*, const int16_t*);
    using VoxelB = void(bool*, const openvdb::math::Vec3<int32_t>*, void*, const bool);
    using VoxelV2D = void(openvdb::math::Vec2<double>*, const openvdb::math::Vec3<int32_t>*, void*, const openvdb::math::Vec2<double>*);
    using VoxelV2F = void(openvdb::math::Vec2<float>*, const openvdb::math::Vec3<int32_t>*, void*, const openvdb::math::Vec2<float>*);
    using VoxelV2I = void(openvdb::math::Vec2<int32_t>*, const openvdb::math::Vec3<int32_t>*, void*, const openvdb::math::Vec2<int32_t>*);
    using VoxelV3D = void(openvdb::math::Vec3<double>*, const openvdb::math::Vec3<int32_t>*, void*, const openvdb::math::Vec3<double>*);
    using VoxelV3F = void(openvdb::math::Vec3<float>*, const openvdb::math::Vec3<int32_t>*, void*, const openvdb::math::Vec3<float>*);
    using VoxelV3I = void(openvdb::math::Vec3<int32_t>*, const openvdb::math::Vec3<int32_t>*, void*, const openvdb::math::Vec3<int32_t>*);
    using VoxelV4D = void(openvdb::math::Vec4<double>*, const openvdb::math::Vec3<int32_t>*, void*, const openvdb::math::Vec4<double>*);
    using VoxelV4F = void(openvdb::math::Vec4<float>*, const openvdb::math::Vec3<int32_t>*, void*, const openvdb::math::Vec4<float>*);
    using VoxelV4I = void(openvdb::math::Vec4<int32_t>*, const openvdb::math::Vec3<int32_t>*, void*, const openvdb::math::Vec4<int32_t>*);
    using VoxelM3D = void(openvdb::math::Mat3<double>*, const openvdb::math::Vec3<int32_t>*, void*, const openvdb::math::Mat3<double>*);
    using VoxelM3F = void(openvdb::math::Mat3<float>*, const openvdb::math::Vec3<int32_t>*, void*, const openvdb::math::Mat3<float>*);
    using VoxelM4D = void(openvdb::math::Mat4<double>*, const openvdb::math::Vec3<int32_t>*, void*, const openvdb::math::Mat4<double>*);
    using VoxelM4F = void(openvdb::math::Mat4<float>*, const openvdb::math::Vec3<int32_t>*, void*, const openvdb::math::Mat4<float>*);

    return FunctionBuilder("__voxel")
        .addSignature<VoxelD, true>((VoxelD*)(voxel))
        .addSignature<VoxelF, true>((VoxelF*)(voxel))
        .addSignature<VoxelI64, true>((VoxelI64*)(voxel))
        .addSignature<VoxelI32, true>((VoxelI32*)(voxel))
        .addSignature<VoxelI16, true>((VoxelI16*)(voxel))
        .addSignature<VoxelB, true>((VoxelB*)(voxel))
        .addSignature<VoxelV2D, true>((VoxelV2D*)(voxel))
        .addSignature<VoxelV2F, true>((VoxelV2F*)(voxel))
        .addSignature<VoxelV2I, true>((VoxelV2I*)(voxel))
        .addSignature<VoxelV3D, true>((VoxelV3D*)(voxel))
        .addSignature<VoxelV3F, true>((VoxelV3F*)(voxel))
        .addSignature<VoxelV3I, true>((VoxelV3I*)(voxel))
        .addSignature<VoxelV4D, true>((VoxelV4D*)(voxel))
        .addSignature<VoxelV4F, true>((VoxelV4F*)(voxel))
        .addSignature<VoxelV4I, true>((VoxelV4I*)(voxel))
        .addSignature<VoxelM3F, true>((VoxelM3F*)(voxel))
        .addSignature<VoxelM3D, true>((VoxelM3D*)(voxel))
        .addSignature<VoxelM4F, true>((VoxelM4F*)(voxel))
        .addSignature<VoxelM4D, true>((VoxelM4D*)(voxel))
            .addParameterAttribute(0, llvm::Attribute::NoAlias)
            .addParameterAttribute(0, llvm::Attribute::WriteOnly)
            .addParameterAttribute(1, llvm::Attribute::ReadOnly)
            .addParameterAttribute(3, llvm::Attribute::WriteOnly)
            .addParameterAttribute(3, llvm::Attribute::NoAlias)
            .addFunctionAttribute(llvm::Attribute::NoUnwind)
            .addFunctionAttribute(llvm::Attribute::NoRecurse)
            .setConstantFold(false)
        .setPreferredImpl(op.mPrioritiseIR ? FunctionBuilder::IR : FunctionBuilder::C)
        .setDocumentation("Returns the value of a voxel.")
        .get();
}

inline FunctionGroup::UniquePtr axvoxel(const FunctionOptions& op)
{
    auto generate = [op](const std::vector<llvm::Value*>& args,
         llvm::IRBuilder<>& B,
         const ast::FunctionCall* f) -> llvm::Value*
    {
        llvm::Function* compute = B.GetInsertBlock()->getParent();
        verifyContext(compute, "voxel");

        assert(f->parent() && f->parent()->isType<ast::AttributeFunctionCall>());
        auto* afc = static_cast<const ast::AttributeFunctionCall*>(f->parent());

        std::vector<llvm::Value*> input(args);
        appendAccessorArgument(input, B, afc->attr());
        appendAttributeISEL(input, B, afc->attr());
        return ax__voxel(op)->execute(input, B);
    };

    return FunctionBuilder("voxel")
        .addSignature<void(const openvdb::math::Vec3<int32_t>*)>(generate)
            .addParameterAttribute(0, llvm::Attribute::ReadOnly)
            .addFunctionAttribute(llvm::Attribute::NoUnwind)
            .addFunctionAttribute(llvm::Attribute::NoRecurse)
            .setEmbedIR(true)
        .addDependency("__voxel")
        .setPreferredImpl(op.mPrioritiseIR ? FunctionBuilder::IR : FunctionBuilder::C)
        .setDocumentation("Returns the value of a voxel.")
        .get();
}

inline FunctionGroup::UniquePtr ax__isvoxel(const FunctionOptions& op)
{
    static auto isactive =
        [](const openvdb::math::Vec3<int32_t>* coord,
           void* accessor,
           auto* value) //only used for prototype selection
            -> bool {
        using ValueType = typename std::remove_pointer<decltype(value)>::type;
        using GridType = typename openvdb::BoolGrid::ValueConverter<ValueType>::Type;
        using AccessorType = typename GridType::Accessor;
        assert(coord);
        assert(accessor);
        const AccessorType* const aptr =
            static_cast<const AccessorType* const>(accessor);
        const openvdb::Coord* ijk =
            reinterpret_cast<const openvdb::Coord*>(coord);
        return aptr->isVoxel(*ijk);
    };

    using IsActiveD = bool(const openvdb::math::Vec3<int32_t>*, void*, const double*);
    using IsActiveF = bool(const openvdb::math::Vec3<int32_t>*, void*, const float*);
    using IsActiveI64 = bool(const openvdb::math::Vec3<int32_t>*, void*, const int64_t*);
    using IsActiveI32 = bool(const openvdb::math::Vec3<int32_t>*, void*, const int32_t*);
    using IsActiveI16 = bool(const openvdb::math::Vec3<int32_t>*, void*, const int16_t*);
    using IsActiveB = bool(const openvdb::math::Vec3<int32_t>*, void*, const bool*);
    using IsActiveV2D = bool(const openvdb::math::Vec3<int32_t>*, void*, const openvdb::math::Vec2<double>*);
    using IsActiveV2F = bool(const openvdb::math::Vec3<int32_t>*, void*, const openvdb::math::Vec2<float>*);
    using IsActiveV2I = bool(const openvdb::math::Vec3<int32_t>*, void*, const openvdb::math::Vec2<int32_t>*);
    using IsActiveV3D = bool(const openvdb::math::Vec3<int32_t>*, void*, const openvdb::math::Vec3<double>*);
    using IsActiveV3F = bool(const openvdb::math::Vec3<int32_t>*, void*, const openvdb::math::Vec3<float>*);
    using IsActiveV3I = bool(const openvdb::math::Vec3<int32_t>*, void*, const openvdb::math::Vec3<int32_t>*);
    using IsActiveV4D = bool(const openvdb::math::Vec3<int32_t>*, void*, const openvdb::math::Vec4<double>*);
    using IsActiveV4F = bool(const openvdb::math::Vec3<int32_t>*, void*, const openvdb::math::Vec4<float>*);
    using IsActiveV4I = bool(const openvdb::math::Vec3<int32_t>*, void*, const openvdb::math::Vec4<int32_t>*);
    using IsActiveM3D = bool(const openvdb::math::Vec3<int32_t>*, void*, const openvdb::math::Mat3<double>*);
    using IsActiveM3F = bool(const openvdb::math::Vec3<int32_t>*, void*, const openvdb::math::Mat3<float>*);
    using IsActiveM4D = bool(const openvdb::math::Vec3<int32_t>*, void*, const openvdb::math::Mat4<double>*);
    using IsActiveM4F = bool(const openvdb::math::Vec3<int32_t>*, void*, const openvdb::math::Mat4<float>*);

    return FunctionBuilder("__isvoxel")
        .addSignature<IsActiveD>((IsActiveD*)(isactive))
        .addSignature<IsActiveF>((IsActiveF*)(isactive))
        .addSignature<IsActiveI64>((IsActiveI64*)(isactive))
        .addSignature<IsActiveI32>((IsActiveI32*)(isactive))
        .addSignature<IsActiveI16>((IsActiveI16*)(isactive))
        .addSignature<IsActiveB>((IsActiveB*)(isactive))
        .addSignature<IsActiveV2D>((IsActiveV2D*)(isactive))
        .addSignature<IsActiveV2F>((IsActiveV2F*)(isactive))
        .addSignature<IsActiveV2I>((IsActiveV2I*)(isactive))
        .addSignature<IsActiveV3D>((IsActiveV3D*)(isactive))
        .addSignature<IsActiveV3F>((IsActiveV3F*)(isactive))
        .addSignature<IsActiveV3I>((IsActiveV3I*)(isactive))
        .addSignature<IsActiveV4D>((IsActiveV4D*)(isactive))
        .addSignature<IsActiveV4F>((IsActiveV4F*)(isactive))
        .addSignature<IsActiveV4I>((IsActiveV4I*)(isactive))
        .addSignature<IsActiveM3D>((IsActiveM3D*)(isactive))
        .addSignature<IsActiveM3F>((IsActiveM3F*)(isactive))
        .addSignature<IsActiveM4D>((IsActiveM4D*)(isactive))
        .addSignature<IsActiveM4F>((IsActiveM4F*)(isactive))
            .addParameterAttribute(0, llvm::Attribute::ReadOnly)
            .addFunctionAttribute(llvm::Attribute::NoUnwind)
            .addFunctionAttribute(llvm::Attribute::NoRecurse)
            .setConstantFold(false)
        .setPreferredImpl(op.mPrioritiseIR ? FunctionBuilder::IR : FunctionBuilder::C)
        .setDocumentation("Internal function for querying if a coordinate is a voxel.")
        .get();
}

inline FunctionGroup::UniquePtr axisvoxel(const FunctionOptions& op)
{
    auto generate = [op](const std::vector<llvm::Value*>& args,
         llvm::IRBuilder<>& B,
         const ast::FunctionCall* f) -> llvm::Value*
    {
        llvm::Function* compute = B.GetInsertBlock()->getParent();
        verifyContext(compute, "isvoxel");

        assert(f->parent() && f->parent()->isType<ast::AttributeFunctionCall>());
        auto* afc = static_cast<const ast::AttributeFunctionCall*>(f->parent());

        std::vector<llvm::Value*> input(args);
        appendAccessorArgument(input, B, afc->attr());
        appendAttributeISEL(input, B, afc->attr());
        return ax__isvoxel(op)->execute(input, B);
    };

    return FunctionBuilder("isvoxel")
        .addSignature<bool(const openvdb::math::Vec3<int32_t>*)>(generate)
            .addParameterAttribute(0, llvm::Attribute::ReadOnly)
            .addFunctionAttribute(llvm::Attribute::NoUnwind)
            .addFunctionAttribute(llvm::Attribute::NoRecurse)
            .setEmbedIR(true)
        .addDependency("__isvoxel")
        .setPreferredImpl(op.mPrioritiseIR ? FunctionBuilder::IR : FunctionBuilder::C)
        .setDocumentation("Returns if the value at the specified index coordinate is "
            "at the voxel level of the VDB tree.")
        .get();
}

template <size_t Order>
inline FunctionGroup::UniquePtr ax__sample(const FunctionOptions& op)
{
    static_assert(Order <= 2, "Invalid Order for axsample");

    static auto sample =
        [](auto* value,
           const openvdb::math::Vec3<double>* pos,
           void* accessor,
           auto) //only used for prototype selection
    {
        using ValueType = typename std::remove_pointer<decltype(value)>::type;
        using GridType = typename openvdb::BoolGrid::ValueConverter<ValueType>::Type;
        using AccessorType = typename GridType::Accessor;
        assert(value);
        assert(pos);
        assert(accessor);
        const AccessorType* const aptr =
            static_cast<const AccessorType* const>(accessor);
        openvdb::tools::Sampler<Order, false>::sample(*aptr, *pos, *value);
    };

    static auto sample_v3 =
        [](auto* value,
           const openvdb::math::Vec3<double>* pos,
           const bool staggered,
           void* accessor,
           auto) //only used for prototype selection
    {
        using ValueType = typename std::remove_pointer<decltype(value)>::type;
        using GridType = typename openvdb::BoolGrid::ValueConverter<ValueType>::Type;
        using AccessorType = typename GridType::Accessor;
        assert(value);
        assert(pos);
        assert(accessor);
        const AccessorType* const aptr =
            static_cast<const AccessorType* const>(accessor);
        if (staggered) openvdb::tools::Sampler<Order, true>::sample(*aptr, *pos, *value);
        else           openvdb::tools::Sampler<Order, false>::sample(*aptr, *pos, *value);
    };

    using SampleD = void(double*, const openvdb::math::Vec3<double>*, void*, const double*);
    using SampleF = void(float*, const openvdb::math::Vec3<double>*, void*, const float*);
    using SampleI64 = void(int64_t*, const openvdb::math::Vec3<double>*, void*, const int64_t*);
    using SampleI32 = void(int32_t*, const openvdb::math::Vec3<double>*, void*, const int32_t*);
    using SampleI16 = void(int16_t*, const openvdb::math::Vec3<double>*, void*, const int16_t*);
    using SampleB = void(bool*, const openvdb::math::Vec3<double>*, void*, const bool);
    using SampleV2D = void(openvdb::math::Vec2<double>*, const openvdb::math::Vec3<double>*, void*, const openvdb::math::Vec2<double>*);
    using SampleV2F = void(openvdb::math::Vec2<float>*, const openvdb::math::Vec3<double>*, void*, const openvdb::math::Vec2<float>*);
    using SampleV2I = void(openvdb::math::Vec2<int32_t>*, const openvdb::math::Vec3<double>*, void*, const openvdb::math::Vec2<int32_t>*);
    using SampleV3D = void(openvdb::math::Vec3<double>*, const openvdb::math::Vec3<double>*, void*, const openvdb::math::Vec3<double>*);
    using SampleV3F = void(openvdb::math::Vec3<float>*, const openvdb::math::Vec3<double>*, void*, const openvdb::math::Vec3<float>*);
    using SampleV3I = void(openvdb::math::Vec3<int32_t>*, const openvdb::math::Vec3<double>*, void*, const openvdb::math::Vec3<int32_t>*);
    using SampleV4D = void(openvdb::math::Vec4<double>*, const openvdb::math::Vec3<double>*, void*, const openvdb::math::Vec4<double>*);
    using SampleV4F = void(openvdb::math::Vec4<float>*, const openvdb::math::Vec3<double>*, void*, const openvdb::math::Vec4<float>*);
    using SampleV4I = void(openvdb::math::Vec4<int32_t>*, const openvdb::math::Vec3<double>*, void*, const openvdb::math::Vec4<int32_t>*);

    using SampleV3D_S = void(openvdb::math::Vec3<double>*, const openvdb::math::Vec3<double>*, const bool, void*, const openvdb::math::Vec3<double>*);
    using SampleV3F_S = void(openvdb::math::Vec3<float>*, const openvdb::math::Vec3<double>*, const bool, void*, const openvdb::math::Vec3<float>*);
    using SampleV3I_S = void(openvdb::math::Vec3<int32_t>*, const openvdb::math::Vec3<double>*, const bool, void*, const openvdb::math::Vec3<int32_t>*);

    return FunctionBuilder(
        Order == 0 ? "__pointsample" :
        Order == 1 ? "__boxsample" :
        Order == 2 ? "__quadraticsample" : "")
        .addSignature<SampleD, true>((SampleD*)(sample))
        .addSignature<SampleF, true>((SampleF*)(sample))
        .addSignature<SampleI64, true>((SampleI64*)(sample))
        .addSignature<SampleI32, true>((SampleI32*)(sample))
        .addSignature<SampleI16, true>((SampleI16*)(sample))
        .addSignature<SampleB, true>((SampleB*)(sample))
        .addSignature<SampleV2D, true>((SampleV2D*)(sample))
        .addSignature<SampleV2F, true>((SampleV2F*)(sample))
        .addSignature<SampleV2I, true>((SampleV2I*)(sample))
        .addSignature<SampleV3D, true>((SampleV3D*)(sample))
        .addSignature<SampleV3F, true>((SampleV3F*)(sample))
        .addSignature<SampleV3I, true>((SampleV3I*)(sample))
        .addSignature<SampleV4D, true>((SampleV4D*)(sample))
        .addSignature<SampleV4F, true>((SampleV4F*)(sample))
        .addSignature<SampleV4I, true>((SampleV4I*)(sample))
        .addSignature<SampleV3D_S, true>((SampleV3D_S*)(sample_v3))
        .addSignature<SampleV3F_S, true>((SampleV3F_S*)(sample_v3))
        .addSignature<SampleV3I_S, true>((SampleV3I_S*)(sample_v3))
            .addParameterAttribute(0, llvm::Attribute::NoAlias)
            .addParameterAttribute(0, llvm::Attribute::WriteOnly)
            .addParameterAttribute(1, llvm::Attribute::ReadOnly)
            .addFunctionAttribute(llvm::Attribute::NoUnwind)
            .addFunctionAttribute(llvm::Attribute::NoRecurse)
            .setConstantFold(false)
        .setPreferredImpl(op.mPrioritiseIR ? FunctionBuilder::IR : FunctionBuilder::C)
        .setDocumentation("Internal sampling.")
        .get();
}

template <size_t Order>
inline FunctionGroup::UniquePtr axsample(const FunctionOptions& op)
{
    auto generate = [op](const std::vector<llvm::Value*>& args,
         llvm::IRBuilder<>& B,
         const ast::FunctionCall* f) -> llvm::Value*
    {
        llvm::Function* compute = B.GetInsertBlock()->getParent();
        verifyContext(compute, "sample");

        assert(f->parent() && f->parent()->isType<ast::AttributeFunctionCall>());
        auto* afc = static_cast<const ast::AttributeFunctionCall*>(f->parent());

        const bool isVec3 = afc->attr().type() == ast::tokens::CoreType::VEC3F
            || afc->attr().type() == ast::tokens::CoreType::VEC3D
            || afc->attr().type() == ast::tokens::CoreType::VEC3I;

        std::vector<llvm::Value*> input(args);

        if (isVec3 && input.size() == 1) {
            llvm::Value* gclass = B.CreateLoad(getClassArgument(B, afc->attr()));
            llvm::Value* V = LLVMType<int8_t>::get(B.getContext(), static_cast<int8_t>(openvdb::GRID_STAGGERED));
            llvm::Value* staggered = B.CreateICmpEQ(gclass, V);
            input.emplace_back(staggered);
        }
        else if (!isVec3 && input.size() == 2) {
            // @todo warn/error?
            input.pop_back();
        }

        appendAccessorArgument(input, B, afc->attr());
        appendAttributeISEL(input, B, afc->attr());
        return ax__sample<Order>(op)->execute(input, B);
    };

    return FunctionBuilder(
        Order == 0 ? "pointsample" :
        Order == 1 ? "boxsample" :
        Order == 2 ? "quadraticsample" : "")
        .template addSignature<void(const openvdb::math::Vec3<double>*)>(generate)
        .template addSignature<void(const openvdb::math::Vec3<double>*, bool)>(generate)
            .addParameterAttribute(0, llvm::Attribute::ReadOnly)
            .addFunctionAttribute(llvm::Attribute::NoUnwind)
            .addFunctionAttribute(llvm::Attribute::NoRecurse)
            .setEmbedIR(true)
        .addDependency("__pointsample")
        .addDependency("__boxsample")
        .addDependency("__quadraticsample")
        .setArgumentNames({"position", "staggered"})
        .setPreferredImpl(op.mPrioritiseIR ? FunctionBuilder::IR : FunctionBuilder::C)
        .setDocumentation(Order == 0 ? "Point sample the given volume at an index space "
            "position. Point sampling is the same as single value voxel retrieval, where "
            "the position is rounded to the nearest voxel coordinate." :
            Order == 1 ? "Box sample the given volume at an index space position. "
            "Box-sampling performs trilinear interpolation on the nearest 8 values." :
            Order == 2 ? "Quadratic sample the given volume at an index space "
            "position.  Quadratic-sampling performs triquadratic interpolation across "
            "the nearest 27 values." : "")
        .get();
}

/*
inline FunctionGroup::UniquePtr ax__mean(const FunctionOptions& op)
{
    static auto mean =
        [](auto* value,
           const openvdb::math::Vec3<int32_t>* coord,
           const int32_t width,
           void* accessor,
           auto) //only used for prototype selection
    {
        using ValueType = typename std::remove_pointer<decltype(value)>::type;
        // use long double for int64_t to ensure precision
        using WeightT = typename std::conditional<std::is_same<ValueType, int64_t>::value, long double, double>::type;
        using GridType = typename openvdb::BoolGrid::ValueConverter<ValueType>::Type;
        using AccessorType = typename GridType::Accessor;
        assert(value);
        assert(coord);
        assert(accessor);

        const openvdb::Coord* ijk =
            reinterpret_cast<const openvdb::Coord*>(coord);
        const AccessorType* const aptr =
            static_cast<const AccessorType* const>(accessor);

        ValueType sum = openvdb::zeroVal<ValueType>();
        openvdb::Coord xyz(*ijk);

        // @todo GCC prints some weird warnings for int16_t conversion which
        // might be a GCC bug. Need to investigate
        OPENVDB_NO_TYPE_CONVERSION_WARNING_BEGIN
        openvdb::Int32& i = xyz[0], j = i + width;
        for (i -= width; i <= j; ++i) sum += aptr->getValue(xyz);
        xyz[0] = (*ijk)[0];

        i = xyz[1], j = i + width;
        for (i -= width; i <= j; ++i) sum += aptr->getValue(xyz);
        xyz[1] = (*ijk)[1];

        i = xyz[2], j = i + width;
        for (i -= width; i <= j; ++i) sum += aptr->getValue(xyz);
        OPENVDB_NO_TYPE_CONVERSION_WARNING_END

        const WeightT weight = 1.0 / static_cast<WeightT>(3*(2*width+1));
        *value = ValueType(sum * weight);
    };

    using MeanD = void(double*, const openvdb::math::Vec3<int32_t>*, const int32_t, void*, const double*);
    using MeanF = void(float*, const openvdb::math::Vec3<int32_t>*, const int32_t, void*, const float*);
    using MeanI64 = void(int64_t*, const openvdb::math::Vec3<int32_t>*, const int32_t, void*, const int64_t*);
    using MeanI32 = void(int32_t*, const openvdb::math::Vec3<int32_t>*, const int32_t, void*, const int32_t*);
    using MeanI16 = void(int16_t*, const openvdb::math::Vec3<int32_t>*, const int32_t, void*, const int16_t*);
    using MeanB = void(bool*, const openvdb::math::Vec3<int32_t>*, const int32_t, void*, const bool);
    using MeanV2D = void(openvdb::math::Vec2<double>*, const openvdb::math::Vec3<int32_t>*, const int32_t, void*, const openvdb::math::Vec2<double>*);
    using MeanV2F = void(openvdb::math::Vec2<float>*, const openvdb::math::Vec3<int32_t>*, const int32_t, void*, const openvdb::math::Vec2<float>*);
    using MeanV2I = void(openvdb::math::Vec2<int32_t>*, const openvdb::math::Vec3<int32_t>*, const int32_t, void*, const openvdb::math::Vec2<int32_t>*);
    using MeanV3D = void(openvdb::math::Vec3<double>*, const openvdb::math::Vec3<int32_t>*, const int32_t, void*, const openvdb::math::Vec3<double>*);
    using MeanV3F = void(openvdb::math::Vec3<float>*, const openvdb::math::Vec3<int32_t>*, const int32_t, void*, const openvdb::math::Vec3<float>*);
    using MeanV3I = void(openvdb::math::Vec3<int32_t>*, const openvdb::math::Vec3<int32_t>*, const int32_t, void*, const openvdb::math::Vec3<int32_t>*);
    using MeanV4D = void(openvdb::math::Vec4<double>*, const openvdb::math::Vec3<int32_t>*, const int32_t, void*, const openvdb::math::Vec4<double>*);
    using MeanV4F = void(openvdb::math::Vec4<float>*, const openvdb::math::Vec3<int32_t>*, const int32_t, void*, const openvdb::math::Vec4<float>*);
    using MeanV4I = void(openvdb::math::Vec4<int32_t>*, const openvdb::math::Vec3<int32_t>*, const int32_t, void*, const openvdb::math::Vec4<int32_t>*);

    return FunctionBuilder("__mean")
        .addSignature<MeanD, true>((MeanD*)(mean))
        .addSignature<MeanF, true>((MeanF*)(mean))
        .addSignature<MeanI64, true>((MeanI64*)(mean))
        .addSignature<MeanI32, true>((MeanI32*)(mean))
        .addSignature<MeanI16, true>((MeanI16*)(mean))
        .addSignature<MeanB, true>((MeanB*)(mean))
        .addSignature<MeanV2D, true>((MeanV2D*)(mean))
        .addSignature<MeanV2F, true>((MeanV2F*)(mean))
        .addSignature<MeanV2I, true>((MeanV2I*)(mean))
        .addSignature<MeanV3D, true>((MeanV3D*)(mean))
        .addSignature<MeanV3F, true>((MeanV3F*)(mean))
        .addSignature<MeanV3I, true>((MeanV3I*)(mean))
        .addSignature<MeanV4D, true>((MeanV4D*)(mean))
        .addSignature<MeanV4F, true>((MeanV4F*)(mean))
        .addSignature<MeanV4I, true>((MeanV4I*)(mean))
            .addParameterAttribute(0, llvm::Attribute::NoAlias)
            .addParameterAttribute(0, llvm::Attribute::WriteOnly)
            .addParameterAttribute(1, llvm::Attribute::ReadOnly)
            .addFunctionAttribute(llvm::Attribute::NoUnwind)
            .addFunctionAttribute(llvm::Attribute::NoRecurse)
            .setConstantFold(false)
        .setPreferredImpl(op.mPrioritiseIR ? FunctionBuilder::IR : FunctionBuilder::C)
        .setDocumentation("Internal function for computing a voxel mean value.")
        .get();
}

inline FunctionGroup::UniquePtr axmean(const FunctionOptions& op)
{
    auto generate = [op](const std::vector<llvm::Value*>& args,
         llvm::IRBuilder<>& B,
         void* metadata) -> llvm::Value*
    {
        const ast::Attribute& attr = *(static_cast<ast::Attribute*>(metadata));
        llvm::Function* compute = B.GetInsertBlock()->getParent();
        verifyContext(compute, "mean");

        std::vector<llvm::Value*> input(args);
        appendAccessorArgument(input, B, attr);
        return ax__mean(op)->execute(input, B);
    };

    return FunctionBuilder("mean")
        .addSignature<void(const openvdb::math::Vec3<int32_t>*, int32_t)>(generate)
            .addParameterAttribute(0, llvm::Attribute::ReadOnly)
            .addFunctionAttribute(llvm::Attribute::NoUnwind)
            .addFunctionAttribute(llvm::Attribute::NoRecurse)
            .setEmbedIR(true)
        .addDependency("__mean")
        .setArgumentNames({"ijk", "width"})
        .setPreferredImpl(op.mPrioritiseIR ? FunctionBuilder::IR : FunctionBuilder::C)
        .setDocumentation("Returns a single iteration of a fast separable mean-value "
            "(i.e. box) filter for a particular index coordinate. The width argument "
            "defines the neighbour radius in voxels.")
        .get();
}
*/

inline FunctionGroup::UniquePtr ax__transform(const FunctionOptions& op)
{
    using GetTransformMatT = void(openvdb::math::Mat4<double>*, void*);

    static auto getTransform = [](openvdb::math::Mat4<double>* mat, void* gridbase)
    {
        assert(gridbase);
        const openvdb::GridBase* const gptr =
            static_cast<const openvdb::GridBase* const>(gridbase);
        // @warning, virtual function, it's slow
        // @todo improve
        *mat = gptr->transform().baseMap()->getAffineMap()->getMat4();
    };

    return FunctionBuilder("__transform")
        .addSignature<GetTransformMatT>((GetTransformMatT*)(getTransform))
            .addParameterAttribute(0, llvm::Attribute::NoAlias)
            .addParameterAttribute(0, llvm::Attribute::WriteOnly)
            .addParameterAttribute(1, llvm::Attribute::ReadOnly)
            .addParameterAttribute(1, llvm::Attribute::NoAlias)
            .addFunctionAttribute(llvm::Attribute::NoUnwind)
            .addFunctionAttribute(llvm::Attribute::NoRecurse)
            .setConstantFold(false)
        .setPreferredImpl(op.mPrioritiseIR ? FunctionBuilder::IR : FunctionBuilder::C)
        .setDocumentation("Internal function for getting the 4x4 transformation matrix.")
        .get();
}

inline FunctionGroup::UniquePtr axtransform(const FunctionOptions& op)
{
    auto generate = [op](const std::vector<llvm::Value*>& args,
         llvm::IRBuilder<>& B,
         const ast::FunctionCall* f) -> llvm::Value*
    {
        llvm::Function* compute = B.GetInsertBlock()->getParent();
        verifyContext(compute, "transform");

        assert(f->parent() && f->parent()->isType<ast::AttributeFunctionCall>());
        auto* afc = static_cast<const ast::AttributeFunctionCall*>(f->parent());

        std::vector<llvm::Value*> input(args);
        appendGridArgument(input, B, afc->attr());
        ax__transform(op)->execute(input, B);
        return nullptr;
    };

    return FunctionBuilder("transform")
        .addSignature<void(openvdb::math::Mat4<double>*), true>(generate)
            .addParameterAttribute(0, llvm::Attribute::NoAlias)
            .addParameterAttribute(0, llvm::Attribute::WriteOnly)
            .addFunctionAttribute(llvm::Attribute::NoUnwind)
            .addFunctionAttribute(llvm::Attribute::NoRecurse)
            .setEmbedIR(true)
        .addDependency("__transform")
        .setPreferredImpl(op.mPrioritiseIR ? FunctionBuilder::IR : FunctionBuilder::C)
        .setDocumentation("Returns the 4x4 transformation matrix of this VDB.")
        .get();
}

inline FunctionGroup::UniquePtr ax__voxelsize(const FunctionOptions& op)
{
    static auto voxelsize = [](openvdb::math::Vec3<double>* mat, void* gridbase)
    {
        assert(gridbase);
        const openvdb::GridBase* const gptr =
            static_cast<const openvdb::GridBase* const>(gridbase);
        // @warning, virtual function, it's slow
        // @todo improve
        *mat = gptr->voxelSize();
    };

    return FunctionBuilder("__voxelsize")
        .addSignature<void(openvdb::math::Vec3<double>*, void*)>(voxelsize)
            .addParameterAttribute(0, llvm::Attribute::NoAlias)
            .addParameterAttribute(0, llvm::Attribute::WriteOnly)
            .addParameterAttribute(1, llvm::Attribute::NoAlias)
            .addParameterAttribute(1, llvm::Attribute::ReadOnly)
            .addFunctionAttribute(llvm::Attribute::NoUnwind)
            .addFunctionAttribute(llvm::Attribute::NoRecurse)
            .setConstantFold(false)
        .setPreferredImpl(op.mPrioritiseIR ? FunctionBuilder::IR : FunctionBuilder::C)
        .setDocumentation("Internal function for getting the voxel size from a transform.")
        .get();
}

inline FunctionGroup::UniquePtr axvoxelsize(const FunctionOptions& op)
{
    auto generate = [op](const std::vector<llvm::Value*>& args,
         llvm::IRBuilder<>& B,
         const ast::FunctionCall* f) -> llvm::Value*
    {
        llvm::Function* compute = B.GetInsertBlock()->getParent();
        verifyContext(compute, "voxelsize");

        assert(f->parent() && f->parent()->isType<ast::AttributeFunctionCall>());
        auto* afc = static_cast<const ast::AttributeFunctionCall*>(f->parent());

        std::vector<llvm::Value*> input(args);
        appendGridArgument(input, B, afc->attr());
        return ax__voxelsize(op)->execute(input, B);
    };

    return FunctionBuilder("voxelsize")
        .addSignature<void(openvdb::math::Vec3<double>*), true>(generate)
            .addFunctionAttribute(llvm::Attribute::NoUnwind)
            .addFunctionAttribute(llvm::Attribute::NoRecurse)
            .setEmbedIR(true)
        .addDependency("__voxelsize")
        .setPreferredImpl(op.mPrioritiseIR ? FunctionBuilder::IR : FunctionBuilder::C)
        .setDocumentation("Returns the voxel size of this VDB. This assumes the VDB transformation is linear.")
        .get();
}

inline FunctionGroup::UniquePtr ax__voxelvolume(const FunctionOptions& op)
{
    static auto voxelvolume = [](void* gridbase) -> double
    {
        assert(gridbase);
        const openvdb::GridBase* const gptr =
            static_cast<const openvdb::GridBase* const>(gridbase);
        // @warning, virtual function, it's slow
        // @todo improve
        return gptr->transform().voxelVolume();
    };

    return FunctionBuilder("__voxelvolume")
        .addSignature<double(void*)>(voxelvolume)
            .addParameterAttribute(0, llvm::Attribute::NoAlias)
            .addParameterAttribute(0, llvm::Attribute::ReadOnly)
            .addFunctionAttribute(llvm::Attribute::NoUnwind)
            .addFunctionAttribute(llvm::Attribute::NoRecurse)
            .setConstantFold(false)
        .setPreferredImpl(op.mPrioritiseIR ? FunctionBuilder::IR : FunctionBuilder::C)
        .setDocumentation("Internal function for getting the voxel volume from a transform.")
        .get();
}

inline FunctionGroup::UniquePtr axvoxelvolume(const FunctionOptions& op)
{
    auto generate = [op](const std::vector<llvm::Value*>& args,
         llvm::IRBuilder<>& B,
         const ast::FunctionCall* f) -> llvm::Value*
    {
        llvm::Function* compute = B.GetInsertBlock()->getParent();
        verifyContext(compute, "voxelvolume");

        assert(f->parent() && f->parent()->isType<ast::AttributeFunctionCall>());
        auto* afc = static_cast<const ast::AttributeFunctionCall*>(f->parent());

        std::vector<llvm::Value*> input(args);
        appendGridArgument(input, B, afc->attr());
        return ax__voxelvolume(op)->execute(input, B);
    };

    return FunctionBuilder("voxelvolume")
        .addSignature<double()>(generate)
            .addFunctionAttribute(llvm::Attribute::NoUnwind)
            .addFunctionAttribute(llvm::Attribute::NoRecurse)
            .setEmbedIR(true)
        .addDependency("__voxelvolume")
        .setPreferredImpl(op.mPrioritiseIR ? FunctionBuilder::IR : FunctionBuilder::C)
        .setDocumentation("Return the volume of a single voxel. This assumes the VDB transformation is linear.")
        .get();
}

} // namespace volume


////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////

void insertVDBVolumeFunctions(FunctionRegistry& reg,
    const FunctionOptions* options)
{
    const bool create = options && !options->mLazyFunctions;
    auto add = [&](const std::string& name,
        const FunctionRegistry::ConstructorT creator,
        const bool internal = false)
    {
        if (create) reg.insertAndCreate(name, creator, *options, internal);
        else        reg.insert(name, creator, internal);
    };

    add("getcoord", volume::axgetcoord);
    add("getcoordx", volume::axgetcoord<0>);
    add("getcoordy", volume::axgetcoord<1>);
    add("getcoordz", volume::axgetcoord<2>);
    add("getvoxelpws", volume::axgetvoxelpws);
    add("getvoxel", volume::axgetvoxel, true);
    add("setvoxel", volume::axsetvoxel, true);
}

void insertVDBVolumeAttrFunctions(FunctionRegistry& reg,
    const FunctionOptions* options)
{
    const bool create = options && !options->mLazyFunctions;
    auto add = [&](const std::string& name,
        const FunctionRegistry::ConstructorT creator,
        const bool internal = false)
    {
        if (create) reg.insertAndCreate(name, creator, *options, internal);
        else        reg.insert(name, creator, internal);
    };

    // transform accessors

    // @todo fix function registries so this can be added
    //add("transform", volume::axtransform);
    //add("__transform", volume::ax__transform, true);
    add("voxelsize", volume::axvoxelsize);
    add("__voxelsize", volume::ax__voxelsize, true);
    add("voxelvolume", volume::axvoxelvolume);
    add("__voxelvolume", volume::ax__voxelvolume, true);

    // value accessors

    add("voxel", volume::axvoxel);
    add("__voxel", volume::ax__voxel, true);

    add("isvoxel", volume::axisvoxel);
    add("__isvoxel", volume::ax__isvoxel, true);

    // @todo add simplier method for function aliases
    //add("sample", volume::axsample<1>);

    add("pointsample", volume::axsample<0>);
    add("__pointsample", volume::ax__sample<0>, true);
    add("boxsample", volume::axsample<1>);
    add("__boxsample", volume::ax__sample<1>, true);
    add("quadraticsample", volume::axsample<2>);
    add("__quadraticsample", volume::ax__sample<2>, true);

    add("coordtooffset", axcoordtooffset, true);
    add("offsettocoord", axoffsettocoord, true);
    add("offsettoglobalcoord", axoffsettoglobalcoord, true);
    add("indextoworld", axindextoworld, true);

    add("getcoord", axgetcoord);
    add("getcoordx", axgetcoord<0>);
    add("getcoordy", axgetcoord<1>);
    add("getcoordz", axgetcoord<2>);
    add("getvoxelpws", axgetvoxelpws);
    add("isactive", axisactive, true); // needs tests

    add("getvoxel", axgetvoxel, true);
    add("setvoxel", axsetvoxel, true);
    add("probevalue", axprobevalue, true);

    // add("mean", volume::axmean);
    // add("__mean", volume::ax__mean, true);
}

} // namespace codegen
} // namespace ax
} // namespace OPENVDB_VERSION_NAME
} // namespace openvdb

