// Copyright Contributors to the OpenVDB Project
// SPDX-License-Identifier: MPL-2.0

#include "ax.h"
#include "ast/AST.h"
#include "compiler/Logger.h"
#include "compiler/Compiler.h"
#include "compiler/PointExecutable.h"
#include "compiler/VolumeExecutable.h"

#include <llvm/InitializePasses.h>
#include <llvm/PassRegistry.h>
#include <llvm/Config/llvm-config.h> // version numbers
#include <llvm/Support/TargetSelect.h> // InitializeNativeTarget
#include <llvm/Support/ManagedStatic.h> // llvm_shutdown
#include <llvm/ExecutionEngine/MCJIT.h> // LLVMLinkInMCJIT


#include <llvm/ADT/Optional.h>
#include <llvm/ADT/Triple.h>
#include <llvm/Analysis/TargetLibraryInfo.h>
#include <llvm/Analysis/TargetTransformInfo.h>
#include <llvm/Config/llvm-config.h>
#include <llvm/ExecutionEngine/ExecutionEngine.h>
#include <llvm/IR/LegacyPassManager.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Mangler.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/PassManager.h>
#include <llvm/IR/Verifier.h>
#include <llvm/IRReader/IRReader.h>
#include <llvm/MC/SubtargetFeature.h>
#include <llvm/Passes/PassBuilder.h>
#include <llvm/Support/Host.h>
#include <llvm/Support/MemoryBuffer.h>
#include <llvm/Support/raw_os_ostream.h>
#include <llvm/Support/SourceMgr.h> // SMDiagnostic
#include <llvm/Support/TargetRegistry.h>
#include <llvm/Target/TargetMachine.h>
#include <llvm/Target/TargetOptions.h>
#include <llvm/Support/DynamicLibrary.h>

// @note  As of adding support for LLVM 5.0 we not longer explicitly
// perform standrd compiler passes (-std-compile-opts) based on the changes
// to the opt binary in the llvm codebase (tools/opt.cpp). We also no
// longer explicitly perform:
//  - llvm::createStripSymbolsPass()
// And have never performed any specific target machine analysis passes
//
// @todo  Properly identify the IPO passes that we would benefit from using
// as well as what user controls would otherwise be appropriate

#include <llvm/Transforms/IPO.h> // Inter-procedural optimization passes
#include <llvm/Transforms/IPO/AlwaysInliner.h>
#include <llvm/Transforms/IPO/PassManagerBuilder.h>


#include <mutex>

namespace openvdb {
OPENVDB_USE_VERSION_NAMESPACE
namespace OPENVDB_VERSION_NAME {
namespace ax {

/// @note Implementation for initialize, isInitialized and unitialized
///       reamins in compiler/Compiler.cc

void run(const char* ax, openvdb::GridBase& grid)
{
    // Construct a logger that will output errors to cerr and suppress warnings
    openvdb::ax::Logger logger;
    // Construct a generic compiler
    openvdb::ax::Compiler compiler;
    // Parse the provided code and produce an abstract syntax tree
    // @note  Throws with parser errors if invalid. Parsable code does not
    //        necessarily equate to compilable code
    const openvdb::ax::ast::Tree::ConstPtr
        ast = openvdb::ax::ast::parse(ax, logger);

    if (grid.isType<points::PointDataGrid>()) {
        // Compile for Point support and produce an executable
        // @note  Throws compiler errors on invalid code. On success, returns
        //        the executable which can be used multiple times on any inputs
        const openvdb::ax::PointExecutable::Ptr exe =
            compiler.compile<openvdb::ax::PointExecutable>(*ast, logger);
        // Execute on the provided points
        // @note  Throws on invalid point inputs such as mismatching types
        exe->execute(static_cast<points::PointDataGrid&>(grid));
    }
    else {
        // Compile for numerical grid support and produce an executable
        // @note  Throws compiler errors on invalid code. On success, returns
        //        the executable which can be used multiple times on any inputs
        const openvdb::ax::VolumeExecutable::Ptr exe =
            compiler.compile<openvdb::ax::VolumeExecutable>(*ast, logger);
        // Execute on the provided numerical grid
        // @note  Throws on invalid grid inputs such as mismatching types
        exe->execute(grid);
    }
}

void run(const char* ax, openvdb::GridPtrVec& grids)
{
    if (grids.empty()) return;
    // Check the type of all grids. If they are all points, run for point data.
    // Otherwise, run for numerical volumes. Throw if the container has both.
    const bool points = grids.front()->isType<points::PointDataGrid>();
    for (auto& grid : grids) {
        if (points ^ grid->isType<points::PointDataGrid>()) {
            OPENVDB_THROW(AXCompilerError,
                "Unable to process both OpenVDB Points and OpenVDB Volumes in "
                "a single invocation of ax::run()");
        }
    }
    // Construct a logger that will output errors to cerr and suppress warnings
    openvdb::ax::Logger logger;
    // Construct a generic compiler
    openvdb::ax::Compiler compiler;
    // Parse the provided code and produce an abstract syntax tree
    // @note  Throws with parser errors if invalid. Parsable code does not
    //        necessarily equate to compilable code
    const openvdb::ax::ast::Tree::ConstPtr
        ast = openvdb::ax::ast::parse(ax, logger);
    if (points) {
        // Compile for Point support and produce an executable
        // @note  Throws compiler errors on invalid code. On success, returns
        //        the executable which can be used multiple times on any inputs
        const openvdb::ax::PointExecutable::Ptr exe =
            compiler.compile<openvdb::ax::PointExecutable>(*ast, logger);
        // Execute on the provided points individually
        // @note  Throws on invalid point inputs such as mismatching types
        for (auto& grid : grids) {
            exe->execute(static_cast<points::PointDataGrid&>(*grid));
        }
    }
    else {
        // Compile for Volume support and produce an executable
        // @note  Throws compiler errors on invalid code. On success, returns
        //        the executable which can be used multiple times on any inputs
        const openvdb::ax::VolumeExecutable::Ptr exe =
            compiler.compile<openvdb::ax::VolumeExecutable>(*ast, logger);
        // Execute on the provided volumes
        // @note  Throws on invalid grid inputs such as mismatching types
        exe->execute(grids);
    }
}

namespace {
// Declare this at file scope to ensure thread-safe initialization.
std::mutex sInitMutex;
bool sIsInitialized = false;
bool sShutdown = false;
}

bool isInitialized()
{
    std::lock_guard<std::mutex> lock(sInitMutex);
    return sIsInitialized;
}

struct PRL : public llvm::PassRegistrationListener
{
  PRL() = default;
    ~PRL() override = default;

  /// Callback functions - These functions are invoked whenever a pass is loaded
  /// or removed from the current executable.
    void passRegistered(const llvm::PassInfo *PI) override { std::cerr << PI->getPassName().str() << std::endl; }

  /// passEnumerate - Callback function invoked when someone calls
  /// enumeratePasses on this PassRegistrationListener object.
    void passEnumerate(const llvm::PassInfo *PI) override { }
};

void initialize()
{
    std::lock_guard<std::mutex> lock(sInitMutex);
    if (sIsInitialized) return;

    if (sShutdown) {
        OPENVDB_THROW(AXCompilerError,
            "Unable to re-initialize LLVM target after uninitialize has been called.");
    }

    // Init JIT
    if (llvm::InitializeNativeTarget() ||
        llvm::InitializeNativeTargetAsmPrinter() ||
        llvm::InitializeNativeTargetAsmParser())
    {
        OPENVDB_THROW(AXCompilerError,
            "Failed to initialize LLVM target for JIT");
    }

    // required on some systems
    LLVMLinkInMCJIT();

    // Initialize passes
    llvm::PassRegistry& registry = *llvm::PassRegistry::getPassRegistry();
    llvm::initializeCore(registry);
    llvm::initializeScalarOpts(registry);
    llvm::initializeObjCARCOpts(registry);
    llvm::initializeVectorization(registry);
    llvm::initializeIPO(registry);
    llvm::initializeAnalysis(registry);
    llvm::initializeTransformUtils(registry);
    llvm::initializeInstCombine(registry);
#if LLVM_VERSION_MAJOR > 6
    llvm::initializeAggressiveInstCombine(registry);
#endif
    llvm::initializeInstrumentation(registry);
    llvm::initializeTarget(registry);
    // For codegen passes, only passes that do IR to IR transformation are
    // supported.


    llvm::legacy::PassManager passes;
    llvm::legacy::FunctionPassManager fp(nullptr);
    llvm::PassManagerBuilder builder;
    builder.OptLevel = 3;
    builder.SizeLevel = 0;
    builder.populateFunctionPassManager(fp);
    builder.populateModulePassManager(passes);


    // This handles MARCH (i.e. we don't need to set it on the EngineBuilder)
    llvm::LLVMContext C;
    auto M = std::make_unique<llvm::Module>("", C);
    M->setTargetTriple(llvm::sys::getDefaultTargetTriple());
    llvm::Module* module = M.get();

    // stringref->bool map of features->enabled
    llvm::StringMap<bool> HostFeatures;
    if (!llvm::sys::getHostCPUFeatures(HostFeatures)) {
    }

    std::vector<llvm::StringRef> features;
    for (auto& feature : HostFeatures) {
        if (feature.second) features.emplace_back(feature.first());
    }

    std::string error;
    std::unique_ptr<llvm::ExecutionEngine>
        EE(llvm::EngineBuilder(std::move(M))
            .setErrorStr(&error)
            .setEngineKind(llvm::EngineKind::JIT)
            .setOptLevel(llvm::CodeGenOpt::Level::Default)
            .setMCPU(llvm::sys::getHostCPUName())
            .setMAttrs(features)
            .create());


    // Data layout is also handled in the MCJIT from the generated target machine
    // but we set it on the module in case opt passes request it
    if (auto* TM = EE->getTargetMachine()) {
        module->setDataLayout(TM->createDataLayout());
        TM->adjustPassManager(builder);
    }


    // Try to register the program as a source of symbols to resolve against.
    //
    // FIXME: Don't do this here.
    llvm::sys::DynamicLibrary::LoadLibraryPermanently(nullptr, nullptr);

    llvm::legacy::PassManager PM;

    // The RuntimeDyld will take ownership of this shortly
    llvm::SmallVector<char, 4096> ObjBufferSV;
    llvm::raw_svector_ostream ObjStream(ObjBufferSV);

    // Turn the machine code intermediate representation into bytes in memory
    // that may be executed.
    if (auto* TM = EE->getTargetMachine()) {
        llvm::MCContext * mc;
        if (TM->addPassesToEmitMC(PM, mc, ObjStream, false)) {
            llvm::report_fatal_error("Target does not support MC emission!");
        }
    }
    registry.addRegistrationListener(new PRL());

    llvm::initializeExpandMemCmpPassPass(registry);
    llvm::initializeCodeGenPreparePass(registry);
    llvm::initializeAtomicExpandPass(registry);
    llvm::initializeRewriteSymbolsLegacyPassPass(registry);
    llvm::initializeWinEHPreparePass(registry);
    llvm::initializeSafeStackLegacyPassPass(registry);
    llvm::initializeSjLjEHPreparePass(registry);
    llvm::initializePreISelIntrinsicLoweringLegacyPassPass(registry);
    llvm::initializeGlobalMergePass(registry);
#if LLVM_VERSION_MAJOR > 6
    llvm::initializeIndirectBrExpandPassPass(registry);
#endif
#if LLVM_VERSION_MAJOR > 7
    llvm::initializeInterleavedLoadCombinePass(registry);
#endif
    llvm::initializeInterleavedAccessPass(registry);
    llvm::initializeEntryExitInstrumenterPass(registry);
    llvm::initializePostInlineEntryExitInstrumenterPass(registry);
    llvm::initializeUnreachableBlockElimLegacyPassPass(registry);
    llvm::initializeExpandReductionsPass(registry);
#if LLVM_VERSION_MAJOR > 6
    llvm::initializeWasmEHPreparePass(registry);
#endif
    llvm::initializeWriteBitcodePassPass(registry);



    sIsInitialized = true;
}

void uninitialize()
{
    std::lock_guard<std::mutex> lock(sInitMutex);
    if (!sIsInitialized) return;

    // @todo consider replacing with storage to Support/InitLLVM
    llvm::llvm_shutdown();

    sIsInitialized = false;
    sShutdown = true;
}

} // namespace ax
} // namespace OPENVDB_VERSION_NAME
} // namespace openvdb

