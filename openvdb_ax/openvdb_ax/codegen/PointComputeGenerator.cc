// Copyright Contributors to the OpenVDB Project
// SPDX-License-Identifier: MPL-2.0

/// @file codegen/PointComputeGenerator.cc

#include "PointComputeGenerator.h"

#include "FunctionRegistry.h"
#include "FunctionTypes.h"
#include "Types.h"
#include "Utils.h"

#include "openvdb_ax/Exceptions.h"
#include "openvdb_ax/ast/Scanners.h"

#include <llvm/ADT/SmallVector.h>
#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/CallingConv.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/GlobalVariable.h>
#include <llvm/IR/InlineAsm.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/Intrinsics.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/Pass.h>
#include <llvm/Support/MathExtras.h>

namespace openvdb {
OPENVDB_USE_VERSION_NAMESPACE
namespace OPENVDB_VERSION_NAME {

namespace ax {
namespace codegen {

const std::array<std::string, PointKernelValue::N_ARGS>&
PointKernelValue::argumentKeys()
{
    static const std::array<std::string, PointKernelValue::N_ARGS> arguments = {{
        "custom_data",
        "origin",
        "value_buffer",
        "active",
        "point_index",
        "transforms",
        "attribute_arrays",
        "flags",
        "attribute_set",
        "group_handles",
        "leaf_data"
    }};

    return arguments;
}

const char* PointKernelValue::getDefaultName() { return "ax.compute.point.k1"; }

//

const std::array<std::string, PointKernelRange::N_ARGS>&
PointKernelRange::argumentKeys()
{
    static const std::array<std::string, PointKernelRange::N_ARGS> arguments = {{
        "custom_data",
        "origin",
        "value_buffer",
        "active_buffer",
        "buffer_size",
        "mode",
        "transforms",
        "attribute_arrays",
        "flags",
        "attribute_set",
        "group_handles",
        "leaf_data"
    }};

    return arguments;
}

const char* PointKernelRange::getDefaultName() { return "ax.compute.point.k2"; }


///////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////

namespace codegen_internal {

inline void PointComputeGenerator::computek2(llvm::Function* compute, const AttributeRegistry&)
{
    auto generate =
        [&](const std::vector<llvm::Value*>& args,
            llvm::IRBuilder<>& B) -> llvm::Value*
    {
        assert(args.size() == 9);
        llvm::Value* vbuff = args[2]; //extractArgument(rangeFunction, "value_buffer");
        llvm::Value* abuff = args[3]; //extractArgument(rangeFunction, "active_buffer");
        llvm::Value* buffSize = args[4]; //extractArgument(rangeFunction, "buffer_size");
        llvm::Value* mode = args[5]; //extractArgument(rangeFunction, "mode");
        assert(buffSize);
        assert(vbuff);
        assert(abuff);
        assert(mode);

        llvm::Function* base = B.GetInsertBlock()->getParent();
        llvm::LLVMContext& C = B.getContext();

        llvm::BasicBlock* conditionBlock = llvm::BasicBlock::Create(C, "k2.condition", base);
        llvm::BasicBlock* bodyBlock = llvm::BasicBlock::Create(C, "k2.body", base);
        llvm::BasicBlock* iterBlock = llvm::BasicBlock::Create(C, "k2.buffiter", base);

        // init var - loops from 0 -> buffSize
        llvm::Value* incr = insertStaticAlloca(B, LLVMType<int64_t>::get(C));
        B.CreateStore(B.getInt64(0), incr);
        B.CreateBr(conditionBlock);

        // increment
        B.SetInsertPoint(iterBlock);
        {
            llvm::Value* new_incr = B.CreateAdd(B.CreateLoad(incr), B.getInt64(1));
            B.CreateStore(new_incr, incr);
            B.CreateBr(conditionBlock);
        }

        // generate loop body
        B.SetInsertPoint(bodyBlock);
        {
            llvm::Value* lincr = B.CreateLoad(incr);

            // Extract mask bit from array of words
            // NodeMask::isOn() = (0 != (mWords[n >> 6] & (Word(1) << (n & 63))));
            llvm::Value* mask = binaryOperator(B.getInt64(1),
                binaryOperator(lincr, B.getInt64(63), ast::tokens::BITAND, B),
                    ast::tokens::SHIFTLEFT, B);
            llvm::Value* word_idx = binaryOperator(lincr, B.getInt64(6), ast::tokens::SHIFTRIGHT, B);
            llvm::Value* word = B.CreateGEP(abuff, word_idx);
            word = B.CreateLoad(word);
            word = binaryOperator(word, mask, ast::tokens::BITAND, B);
            llvm::Value* ison = B.CreateICmpNE(word, B.getInt64(0));

            // Check if we should run the kernel depending on the mode.
            //   mode == 0, inactive values
            //   mode == 1, active values
            //   mode == 2, all values
            llvm::Value* matches_mode = B.CreateICmpEQ(B.CreateZExt(ison, mode->getType()), mode);
            llvm::Value* mode_is_all = B.CreateICmpEQ(mode, B.getInt64(2));
            llvm::Value* process = binaryOperator(matches_mode, mode_is_all, ast::tokens::OR, B);
            llvm::BasicBlock* then = llvm::BasicBlock::Create(C, "k2.do_points", base);

            B.CreateCondBr(process, then, iterBlock);
            B.SetInsertPoint(then);
            {
                // branches for getting the end point index
                llvm::BasicBlock* pthen = llvm::BasicBlock::Create(C, "k2.get_0_end", base);
                llvm::BasicBlock* pelse = llvm::BasicBlock::Create(C, "k2.get_p_end", base);

                // loop branches
                llvm::BasicBlock* pcondition = llvm::BasicBlock::Create(C, "k2.pcond", base);
                llvm::BasicBlock* pbody = llvm::BasicBlock::Create(C, "k2.pbody", base);
                llvm::BasicBlock* piter = llvm::BasicBlock::Create(C, "k2.piter", base);

                // loops from pindex->pindexend (point grids have 32bit buffers)
                llvm::Value* pindex = insertStaticAlloca(B, B.getInt32Ty());
                llvm::Value* pindexend = B.CreateGEP(vbuff, lincr);
                pindexend = B.CreateLoad(pindexend);

                llvm::Value* firstvoxel = binaryOperator(lincr, B.getInt64(0), ast::tokens::EQUALSEQUALS, B);
                B.CreateCondBr(firstvoxel, pthen, pelse);
                B.SetInsertPoint(pthen);
                {
                    B.CreateStore(B.getInt32(0), pindex);
                    B.CreateBr(pcondition);
                }

                B.SetInsertPoint(pelse);
                {
                    llvm::Value* prevv = binaryOperator(lincr, B.getInt64(1), ast::tokens::MINUS, B);
                    llvm::Value* pindexcount = B.CreateGEP(vbuff, prevv);
                    B.CreateStore(B.CreateLoad(pindexcount), pindex);
                    B.CreateBr(pcondition);
                }

                B.SetInsertPoint(pcondition);
                {
                    llvm::Value* end = B.CreateICmpULT(B.CreateLoad(pindex), pindexend);
                    B.CreateCondBr(end, pbody, iterBlock);
                }

                B.SetInsertPoint(piter);
                {
                    llvm::Value* pnext = B.CreateAdd(B.CreateLoad(pindex), B.getInt32(1));
                    B.CreateStore(pnext, pindex);
                    B.CreateBr(pcondition);
                }

                B.SetInsertPoint(pbody);
                {
                    // invoke the point kernel for this value
                    const std::array<llvm::Value*, 11> input {
                        args[0],  // ax::CustomData
                        args[1],  // index space coordinate
                        vbuff,    // value buffer
                        ison,     // active/inactive
                        B.CreateLoad(pindex),  // offset in the point array
                        args[6],  // transforms
                        args[7],  // attr arrays
                        args[8],  // flags
                        args[9],  // attr set
                        args[10], // groups
                        args[11]  // leafdata
                    };
                    B.CreateCall(compute, input);
                    B.CreateBr(piter);
                }
            }
        }

        B.SetInsertPoint(conditionBlock);
        llvm::Value* endCondition = B.CreateICmpULT(B.CreateLoad(incr), buffSize);

        llvm::BasicBlock* postBlock = llvm::BasicBlock::Create(C, "k2.end", base);
        B.CreateCondBr(endCondition, bodyBlock, postBlock);
        B.SetInsertPoint(postBlock);
        return B.CreateRetVoid();
    };

    // Use the function builder to generate the correct prototype and body for K2
    auto k2 = FunctionBuilder(PointKernelRange::getDefaultName())
        .addSignature<PointKernelRange::Signature>(generate, PointKernelRange::getDefaultName())
        .setConstantFold(false)
        .setEmbedIR(false)
        .addParameterAttribute(0, llvm::Attribute::ReadOnly)
        .addParameterAttribute(0, llvm::Attribute::NoCapture)
        .addParameterAttribute(0, llvm::Attribute::NoAlias)
        .addParameterAttribute(1, llvm::Attribute::ReadOnly)
        .addParameterAttribute(1, llvm::Attribute::NoCapture)
        .addParameterAttribute(1, llvm::Attribute::NoAlias)
        .addParameterAttribute(2, llvm::Attribute::NoCapture)
        .addParameterAttribute(2, llvm::Attribute::NoAlias)
        .addParameterAttribute(3, llvm::Attribute::NoCapture)
        .addParameterAttribute(3, llvm::Attribute::NoAlias)
        .addParameterAttribute(6, llvm::Attribute::NoCapture)
        .addParameterAttribute(6, llvm::Attribute::NoAlias)
        .addParameterAttribute(7, llvm::Attribute::NoCapture)
        .addParameterAttribute(7, llvm::Attribute::NoAlias)
        .addFunctionAttribute(llvm::Attribute::NoRecurse)
        .get();

    k2->list()[0]->create(mContext, &mModule);
}


PointComputeGenerator::PointComputeGenerator(llvm::Module& module,
                                             const FunctionOptions& options,
                                             FunctionRegistry& functionRegistry,
                                             Logger& logger)
    : ComputeGenerator(module, options, functionRegistry, logger) {}


AttributeRegistry::Ptr PointComputeGenerator::generate(const ast::Tree& tree)
{
    llvm::FunctionType* type =
        llvmFunctionTypeFromSignature<PointKernelValue::Signature>(mContext);

    mFunction = llvm::Function::Create(type,
        llvm::Function::ExternalLinkage,
        PointKernelValue::getDefaultName(),
        &mModule);

    // Set up arguments for initial entry

    llvm::Function::arg_iterator argIter = mFunction->arg_begin();
    const auto arguments = PointKernelValue::argumentKeys();
    auto keyIter = arguments.cbegin();

    for (; argIter != mFunction->arg_end(); ++argIter, ++keyIter) {
        argIter->setName(*keyIter);
    }

    llvm::BasicBlock* entry = llvm::BasicBlock::Create(mContext, "k1.entry", mFunction);
    mBuilder.SetInsertPoint(entry);

    // build the attribute registry

    AttributeRegistry::Ptr registry = AttributeRegistry::create(tree);

    // Visit all attributes and allocate them in local IR memory - assumes attributes
    // have been verified by the ax compiler
    // @note  Call all attribute allocs at the start of this block so that llvm folds
    // them into the function prologue (as a static allocation)

    SymbolTable* localTable = this->mSymbolTables.getOrInsert(1);

    // run allocations and update the symbol table

    for (const AttributeRegistry::AccessData& data : registry->data()) {
        llvm::Type* type = llvmTypeFromToken(data.type(), mContext);
        {
            llvm::Value* vptr = mBuilder.CreateAlloca(type->getPointerTo(0));
            localTable->insert(data.tokenname() + "_vptr", vptr);
            assert(llvm::cast<llvm::AllocaInst>(vptr)->isStaticAlloca());
        }

        // @warning This method will insert the alloc before the above alloc.
        //  This is fine, but is worth noting
        llvm::Value* value = insertStaticAlloca(mBuilder, type);
        assert(llvm::cast<llvm::AllocaInst>(value)->isStaticAlloca());

        // @note  this technically doesn't need to live in the local table
        //  (only the pointer to this value (_vptr) needs to) but it's
        //  re-accessed by the subsequent loop. could remove this.
        localTable->insert(data.tokenname(), value);
    }

    // insert getters for read variables

    llvm::Value* pindex = extractArgument(mFunction, "point_index");
    llvm::Value* flags = extractArgument(mFunction, "flags");
    llvm::Value* arrays = extractArgument(mFunction, "attribute_arrays");

    for (const AttributeRegistry::AccessData& data : registry->data()) {
        if (!data.reads()) continue;
        const std::string token = data.tokenname();
        this->getAttributeValue(token, pindex, localTable->get(token));
    }

    // full code generation
    // errors can stop traversal, but dont always, so check the log

    if (!this->traverse(&tree) || mLog.hasError()) return nullptr;

    // insert set code and deallocations

    std::vector<const AttributeRegistry::AccessData*> write;
    for (const AttributeRegistry::AccessData& data : registry->data()) {
        if (data.writes()) write.emplace_back(&data);
    }

    // cache the basic blocks with return instructions

    std::vector<llvm::BasicBlock*> blocks;
    for (auto block = mFunction->begin(); block != mFunction->end(); ++block) {
        // Only inset set calls if theres a valid return instruction in this block
        llvm::Instruction* inst = block->getTerminator();
        if (!inst || !llvm::isa<llvm::ReturnInst>(inst)) continue;
        blocks.emplace_back(&*block);
    }

    for (auto& block : blocks) {

        llvm::Instruction* inst = block->getTerminator();
        mBuilder.SetInsertPoint(inst);

        // Insert set attribute instructions before termination

        for (const AttributeRegistry::AccessData* data : write) {

            const std::string token = data->tokenname();
            llvm::Value* value = localTable->get(token);

            // Expected to be used more than one (i.e. should never be zero)
            assert(value->hasNUsesOrMore(1));

            // Check to see if this value is still being used - it may have
            // been cleaned up due to returns. If there's only one use, it's
            // the original get of this attribute.
            if (value->hasOneUse()) {
                // @todo  The original get can also be optimized out in this case
                // this->globals().remove(variable.first);
                // mModule.getGlobalVariable(variable.first)->eraseFromParent();
                continue;
            }

            llvm::Value* index = this->globals().get(token);
            index = mBuilder.CreateLoad(index);
            llvm::Value* flag = mBuilder.CreateLoad(mBuilder.CreateGEP(flags, index));
            llvm::Value* isbuffer = mBuilder.CreateAnd(flag, LLVMType<uint8_t>::get(mContext, uint8_t(0x1)));
            llvm::Value* isnotbuffer = mBuilder.CreateICmpEQ(isbuffer, LLVMType<uint8_t>::get(mContext, uint8_t(0)));

            llvm::BasicBlock* then = llvm::BasicBlock::Create(mContext, "k1.set", mFunction);
            llvm::BasicBlock* post = llvm::BasicBlock::Create(mContext, "k1.set_post", mFunction);
            mBuilder.CreateCondBr(isnotbuffer, then, post);

            mBuilder.SetInsertPoint(then);
            {
                llvm::Type* type = value->getType()->getPointerElementType();
                llvm::Type* strType = LLVMType<codegen::String>::get(mContext);
                const bool usingString = type == strType;

                llvm::Value* array = mBuilder.CreateGEP(arrays, index);
                array = mBuilder.CreateLoad(array); // void** = void*

                // load the result (if its a scalar)
                if (type->isIntegerTy() || type->isFloatingPointTy()) {
                    value = mBuilder.CreateLoad(value);
                }

                // construct function arguments
                std::vector<llvm::Value*> args {
                    array, // handle
                    pindex, // point index
                    value // set value
                };

                if (usingString) {
                    args.emplace_back(extractArgument(mFunction, "leaf_data"));
                }

                const FunctionGroup* const F = this->getFunction("setattribute", true);
                F->execute(args, mBuilder);
                mBuilder.CreateBr(post);
            }

            mBuilder.SetInsertPoint(post);
        }

        // move the return instruction into the new, final, post block
        inst->removeFromParent(); // unlink
        mBuilder.Insert(inst); // insert
    }

    // insert free calls for any strings

    this->createFreeSymbolStrings(mBuilder);

    this->computek2(mFunction, *registry);

    return registry;
}

bool PointComputeGenerator::visit(const ast::Attribute* node)
{
    SymbolTable* localTable = this->mSymbolTables.getOrInsert(1);
    const std::string globalName = node->tokenname();
    llvm::Value* value;
    value = localTable->get(globalName + "_vptr");
    value = mBuilder.CreateLoad(value);
    assert(value);
    mValues.push(value);
    return true;
}


///////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////


void PointComputeGenerator::getAttributeValue(const std::string& token, llvm::Value* pindex, llvm::Value* location)
{
    llvm::Type* type = location->getType(); // ValueType*
    llvm::Value* locationptr = this->mSymbolTables.get(1)->get(token + "_vptr"); // ValueType**

    // insert the attribute into the map of global variables and get a unique global representing
    // the location which will hold the attribute handle offset.
    llvm::Value* index = llvm::cast<llvm::GlobalVariable>
        (mModule.getOrInsertGlobal(token, LLVMType<int64_t>::get(mContext)));
    this->globals().insert(token, index);

    // index into the void* array of handles and load the value.
    index = mBuilder.CreateLoad(index);

    llvm::Value* arrays = extractArgument(mFunction, "attribute_arrays");
    assert(arrays);
    llvm::Value* array = mBuilder.CreateGEP(arrays, index);
    array = mBuilder.CreateLoad(array); // void** = void*

    // Check to see if we can directly extract the value or if we need to invoke the C binding
    llvm::Value* flags = extractArgument(mFunction, "flags");
    llvm::Value* flag = mBuilder.CreateLoad(mBuilder.CreateGEP(flags, index));
    llvm::Value* isbuffer = mBuilder.CreateAnd(flag, LLVMType<uint8_t>::get(mContext, uint8_t(0x1)));
    isbuffer = boolComparison(isbuffer, mBuilder);

    llvm::BasicBlock* then = llvm::BasicBlock::Create(mContext, "k1.get_buffer", mFunction);
    llvm::BasicBlock* els  = llvm::BasicBlock::Create(mContext, "k1.get_attr", mFunction);
    llvm::BasicBlock* post = llvm::BasicBlock::Create(mContext, "k1.post_get", mFunction);
    mBuilder.CreateCondBr(isbuffer, then, els);

    mBuilder.SetInsertPoint(then);
    {
        // buffer passed directly
        llvm::Value* buffer = mBuilder.CreatePointerCast(array, type); // void* = ValueType*

        llvm::BasicBlock* then2 = llvm::BasicBlock::Create(mContext, "k1.get_buffer.uniform", mFunction);
        llvm::BasicBlock* els2  = llvm::BasicBlock::Create(mContext, "k1.get_buffer.nuniform", mFunction);
        llvm::Value* isuniform = mBuilder.CreateAnd(flag, LLVMType<uint8_t>::get(mContext, uint8_t(0x2)));
        isuniform = boolComparison(isuniform, mBuilder);

        mBuilder.CreateCondBr(isuniform, then2, els2);

        mBuilder.SetInsertPoint(then2);
        {
            llvm::Value* value = mBuilder.CreateGEP(buffer, mBuilder.getInt64(0));
            mBuilder.CreateStore(value, locationptr);
            mBuilder.CreateBr(post);
        }

        mBuilder.SetInsertPoint(els2);
        {
            llvm::Value* value = mBuilder.CreateGEP(buffer, pindex);
            mBuilder.CreateStore(value, locationptr);
            mBuilder.CreateBr(post);
        }
    }

    mBuilder.SetInsertPoint(els);
    {
        // invoke C binding
        const bool usingString =
            type == LLVMType<codegen::String*>::get(mContext);

        std::vector<llvm::Value*> args {
            array,
            pindex,
            location
        };

        if (usingString) {
            args.emplace_back(extractArgument(mFunction, "leaf_data"));
        }

        const FunctionGroup* const F = this->getFunction("getattribute", true);
        F->execute(args, mBuilder);

        mBuilder.CreateStore(location, locationptr);
        mBuilder.CreateBr(post);
    }

    mBuilder.SetInsertPoint(post);
}

} // namespace codegen_internal

} // namespace codegen
} // namespace ax
} // namespace OPENVDB_VERSION_NAME
} // namespace openvdb

