// Copyright Contributors to the OpenVDB Project
// SPDX-License-Identifier: MPL-2.0

/// @file cmd/openvdb_ax.cc
///
/// @authors Nick Avramoussis, Richard Jones
///
/// @brief  The command line vdb_ax binary which provides tools to
///   run and analyze AX code.
///

#include <openvdb_ax/ast/AST.h>
#include <openvdb_ax/ast/Scanners.h>
#include <openvdb_ax/ast/PrintTree.h>
#include <openvdb_ax/codegen/Functions.h>
#include <openvdb_ax/compiler/Compiler.h>
#include <openvdb_ax/compiler/AttributeRegistry.h>
#include <openvdb_ax/compiler/CompilerOptions.h>
#include <openvdb_ax/compiler/PointExecutable.h>
#include <openvdb_ax/compiler/VolumeExecutable.h>
#include <openvdb_ax/compiler/Logger.h>

#include <openvdb/openvdb.h>
#include <openvdb/version.h>
#include <openvdb/util/logging.h>
#include <openvdb/util/CpuTimer.h>
#include <openvdb/points/PointDelete.h>

#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

const char* gProgName = "";

void usage [[noreturn]] (int exitStatus = EXIT_FAILURE)
{
    std::cerr << "Usage: " << gProgName
        << R"( [input.vdb [output.vdb] | MODE] [-s "string" | -f file.txt] [OPTIONS]
Which: executes a string or file containing a code snippet on an input.vdb file

MODE:
  Either [analyze] or [functions].

OPTIONS:
 [execute] run AX on an input vdb
   -s snippet        execute code snippet on the input.vdb file
   -f file.txt       execute text file containing AX on the input.vdb file
   -v                verbose (print timing and diagnostics)
    --no-delay-load  disable delay loading (e.g. for profiling)
)";

    std::cerr << "  Volumes:\n";

    openvdb::ax::VolumeExecutable::CLI::usage(std::cerr);

    std::cerr << "  Points:\n";

    openvdb::ax::PointExecutable::CLI::usage(std::cerr);

    std::cerr << "  Compiler:";

    std::cerr << R"(
    --opt [NONE|O0|O1|O2|Os|Oz|O3] : Default [O3]
        set an optimization level.
    --werror         set warnings as errors
    --max-errors [n] : Default [0]
        sets the maximum number of error messages. a value of 0 (default) allows
        all error messages

 [analyze] parse the provided code and enter analysis mode
    --ast-print      descriptive print the abstract syntax tree generated
    --re-print       re-interpret print of the provided code after ast traversal
    --reg-print      print the attribute registry (name, types, access, dependencies)
    --try-compile [points|volumes]
        attempt to compile the provided code for points or volumes, or both if
        no additional option is provided, reporting any failures or success.

 [functions] enter function mode to query available function information
    --list [name]
        list all available functions, their documentation and their signatures.
        optionally filter the output with functions whose name includes [name]
    --list-names     list all available functions names only

Warning:
  Providing the same file-path to both input.vdb and output.vdb arguments will
  overwrite the file. If no output file is provided, the input.vdb will be
  processed but will remain unchanged on disk (this is useful for testing the
  success status of code).
)";
    exit(exitStatus);
}

struct ProgOptions
{
    enum Mode { Execute, Analyze, Functions };
    enum Compilation { All, Points, Volumes };

    Mode mMode = Execute;

    // Compilation options
    size_t mMaxErrors = 0;
    bool mWarningsAsErrors = false;

    // Execute options
    std::unique_ptr<std::string> mInputCode = nullptr;
    std::string mInputVDBFile = "";
    std::string mOutputVDBFile = "";
    bool mVerbose = false;
    bool mDelayLoad = true;
    openvdb::ax::CompilerOptions::OptLevel mOptLevel =
        openvdb::ax::CompilerOptions::OptLevel::O3;

    // Analyze options
    bool mPrintAST = false;
    bool mReprint = false;
    bool mAttribRegPrint = false;
    bool mInitCompile = false;
    Compilation mCompileFor = All;

    // Function Options
    bool mFunctionList = false;
    bool mFunctionNamesOnly = false;
    std::string mFunctionSearch = "";
};

inline std::string modeString(const ProgOptions::Mode mode)
{
    switch (mode) {
        case ProgOptions::Execute   : return "execute";
        case ProgOptions::Analyze   : return "analyze";
        case ProgOptions::Functions : return "functions";
        default : return "";
    }
}

ProgOptions::Compilation
tryCompileStringToCompilation(const std::string& str)
{
    if (str == "points")   return ProgOptions::Points;
    if (str == "volumes")  return ProgOptions::Volumes;
    OPENVDB_LOG_FATAL("invalid option given for --try-compile level");
    usage();
}

openvdb::ax::CompilerOptions::OptLevel
optStringToLevel(const std::string& str)
{
    if (str == "NONE") return openvdb::ax::CompilerOptions::OptLevel::NONE;
    if (str == "O0")   return openvdb::ax::CompilerOptions::OptLevel::O0;
    if (str == "O1")   return openvdb::ax::CompilerOptions::OptLevel::O1;
    if (str == "O2")   return openvdb::ax::CompilerOptions::OptLevel::O2;
    if (str == "Os")   return openvdb::ax::CompilerOptions::OptLevel::Os;
    if (str == "Oz")   return openvdb::ax::CompilerOptions::OptLevel::Oz;
    if (str == "O3")   return openvdb::ax::CompilerOptions::OptLevel::O3;
    OPENVDB_LOG_FATAL("invalid option given for --opt level");
    usage();
}

inline std::string
optLevelToString(const openvdb::ax::CompilerOptions::OptLevel level)
{
    switch (level) {
        case  openvdb::ax::CompilerOptions::OptLevel::NONE : return "NONE";
        case  openvdb::ax::CompilerOptions::OptLevel::O1 : return "O1";
        case  openvdb::ax::CompilerOptions::OptLevel::O2 : return "O2";
        case  openvdb::ax::CompilerOptions::OptLevel::Os : return "Os";
        case  openvdb::ax::CompilerOptions::OptLevel::Oz : return "Oz";
        case  openvdb::ax::CompilerOptions::OptLevel::O3 : return "O3";
        default : return "";
    }
}

void loadSnippetFile(const std::string& fileName, std::string& textString)
{
    std::ifstream in(fileName.c_str(), std::ios::in | std::ios::binary);

    if (!in) {
        OPENVDB_LOG_FATAL("File Load Error: " << fileName);
        usage();
    }

    textString =
        std::string(std::istreambuf_iterator<char>(in),
                    std::istreambuf_iterator<char>());
}

struct ScopedInitialize
{
    ScopedInitialize(int argc, char *argv[]) {
        openvdb::logging::initialize(argc, argv);
        openvdb::initialize();
    }

    ~ScopedInitialize() {
        if (openvdb::ax::isInitialized()) {
            openvdb::ax::uninitialize();
        }
        openvdb::uninitialize();
    }

    inline void initializeCompiler() const { openvdb::ax::initialize(); }
    inline bool isInitialized() const { return openvdb::ax::isInitialized(); }
};

struct BinLog : public openvdb::ax::Logger
{
    BinLog(const size_t max, const bool warnerr, const bool verbose)
        : Logger([this](const std::string& msg) { this->mErrs.emplace_back(msg); },
                 [this](const std::string& msg) { this->mErrs.emplace_back(msg); })
        , mErrs()
    {
        this->setMaxErrors(max);
        this->setWarningsAsErrors(warnerr);
        this->setPrintLines(true);
        this->setIndent(verbose ? 2 : 0);
        this->setNumberedOutput(true);
    }

    inline void report(const bool clear = true)
    {
        for (auto& e : mErrs) {
            std::cerr << e << std::endl;
        }
        if (clear) {
            mErrs.clear();
            this->reset();
        }
    }

private:
    std::vector<std::string> mErrs;
};

void printFunctions(const bool namesOnly,
                const std::string& search,
                std::ostream& os)
{
    static const size_t maxHelpTextWidth = 100;

    openvdb::ax::FunctionOptions opts;
    opts.mLazyFunctions = false;
    const openvdb::ax::codegen::FunctionRegistry::UniquePtr registry =
        openvdb::ax::codegen::createDefaultRegistry(&opts);

    // convert to ordered map for alphabetical sorting
    // only include non internal functions and apply any search
    // criteria

    std::map<std::string, const openvdb::ax::codegen::FunctionGroup*> functionMap;
    for (const auto& iter : registry->map()) {
        if (iter.second.isInternal()) continue;
        if (!search.empty() && iter.first.find(search) == std::string::npos) {
            continue;
        }
        functionMap[iter.first] = iter.second.function();
    }

    if (functionMap.empty()) return;

    if (namesOnly) {

        const size_t size = functionMap.size();
        size_t pos = 0, count = 0;

        auto iter = functionMap.cbegin();
        for (; iter != functionMap.cend(); ++iter) {
            if (count == size - 1) break;
            const std::string& name = iter->first;
            if (count != 0 && pos > maxHelpTextWidth) {
                os << '\n';
                pos = 0;
            }
            pos += name.size() + 2; // 2=", "
            os << name << ',' << ' ';
            ++count;
        }

        os << iter->first << '\n';
    }
    else {

        llvm::LLVMContext C;

        for (const auto& iter : functionMap) {
            const openvdb::ax::codegen::FunctionGroup* const function = iter.second;
            const char* cdocs = function->doc();
            if (!cdocs || cdocs[0] == '\0') {
                cdocs = "<No documentation exists for this function>";
            }

            // do some basic formatting on the help text

            std::string docs(cdocs);
            size_t pos = maxHelpTextWidth;
            while (pos < docs.size()) {
                while (docs[pos] != ' ' && pos != 0) --pos;
                if (pos == 0) break;
                docs.insert(pos, "\n|  ");
                pos += maxHelpTextWidth;
            }

            os << iter.first << '\n' << '|' << '\n';
            os << "| - " << docs << '\n' << '|' << '\n';

            const auto& list = function->list();
            for (const openvdb::ax::codegen::Function::Ptr& decl : list) {
                os << "|  - ";
                decl->print(C, os);
                os << '\n';
            }
            os << '\n';
        }
    }
}

int
main(int argc, char *argv[])
{
    OPENVDB_START_THREADSAFE_STATIC_WRITE
    gProgName = argv[0];
    const char* ptr = ::strrchr(gProgName, '/');
    if (ptr != nullptr) gProgName = ptr + 1;
    OPENVDB_FINISH_THREADSAFE_STATIC_WRITE

    if (argc == 1) usage();

    ProgOptions opts;

    openvdb::util::CpuTimer timer;
    auto getTime = [&timer]() -> std::string {
        const double msec = timer.milliseconds();
        std::ostringstream os;
        openvdb::util::printTime(os, msec, "", "", 1, 1, 0);
        return os.str();
    };

    auto& os = std::cout;
#define axlog(message) \
    { if (opts.mVerbose) os << message; }
#define axtimer() timer.restart()
#define axtime() getTime()

    std::vector<int> args(argc, 0);
    auto argparse = [&](int idx, const std::string& name, int numArgs = 1) {
        if (argv[idx] == name) {
            if (idx + numArgs >= argc) {
                std::ostringstream os;
                os << "option " << name << " requires "
                    << numArgs << " argument" << (numArgs == 1 ? "" : "s");
                throw std::runtime_error(os.str());
            }
            for (int i = idx; i <= idx+numArgs; ++i) args[i]=1;
            return true;
        }
        return false;
    };

    auto volumecli = openvdb::ax::VolumeExecutable::CLI::create(argc, argv, args.data());
    auto pointcli = openvdb::ax::PointExecutable::CLI::create(argc, argv, args.data());

    bool multiSnippet = false;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg[0] == '-') {
            if (argparse(i, "-s")) {
                ++i;
                multiSnippet |= static_cast<bool>(opts.mInputCode);
                opts.mInputCode.reset(new std::string(argv[i]));
            } else if (argparse(i, "-f")) {
                ++i;
                multiSnippet |= static_cast<bool>(opts.mInputCode);
                opts.mInputCode.reset(new std::string());
                loadSnippetFile(argv[i], *opts.mInputCode);
            } else if (argparse(i, "-v", 0)) {
                opts.mVerbose = true;
            } else if (argparse(i, "--no-delay-load", 0)) {
                opts.mDelayLoad = false;
            } else if (argparse(i, "--max-errors")) {
                opts.mMaxErrors = atoi(argv[++i]);
            } else if (argparse(i, "--werror", 0)) {
                opts.mWarningsAsErrors = true;
            } else if (argparse(i, "--list", 0)) {
                opts.mFunctionList = true;
                opts.mInitCompile = true; // need to intialize llvm
                opts.mFunctionNamesOnly = false;
                if (i + 1 >= argc) continue;
                if (argv[i+1][0] == '-') continue;
                ++i;
                opts.mFunctionSearch = std::string(argv[i]);
            } else if (argparse(i, "--list-names", 0)) {
                opts.mFunctionList = true;
                opts.mFunctionNamesOnly = true;
            } else if (argparse(i, "--ast-print", 0)) {
                opts.mPrintAST = true;
            } else if (argparse(i, "--re-print", 0)) {
                opts.mReprint = true;
            } else if (argparse(i, "--reg-print", 0)) {
                opts.mAttribRegPrint = true;
            } else if (argparse(i, "--try-compile", 0)) {
                opts.mInitCompile = true;
                if (i + 1 >= argc) continue;
                if (argv[i+1][0] == '-') continue;
                ++i;
                opts.mCompileFor = tryCompileStringToCompilation(argv[i]);
            } else if (argparse(i, "--opt")) {
                ++i;
                opts.mOptLevel = optStringToLevel(argv[i]);
            } else if (arg == "-h" || arg == "-help" || arg == "--help") {
                usage(EXIT_SUCCESS);
            }
        } else if (!arg.empty()) {
            // if mode has already been set, no more positional arguments are expected
            // (except for execute which takes in and out)
            if (opts.mMode != ProgOptions::Mode::Execute) {
                OPENVDB_LOG_FATAL("unrecognized positional argument: \"" << arg << "\"");
                usage();
            }

            if (arg == "analyze")        opts.mMode = ProgOptions::Analyze;
            else if (arg == "functions") opts.mMode = ProgOptions::Functions;

            if (opts.mMode == ProgOptions::Mode::Execute) {
                opts.mInitCompile = true;
                // execute positional argument setup
                if (opts.mInputVDBFile.empty()) {
                    opts.mInputVDBFile = arg;
                }
                else if (opts.mOutputVDBFile.empty()) {
                    opts.mOutputVDBFile = arg;
                }
                else {
                    OPENVDB_LOG_FATAL("unrecognized positional argument: \"" << arg << "\"");
                    usage();
                }
            }
            else if (!opts.mInputVDBFile.empty() ||
                !opts.mOutputVDBFile.empty())
            {
                OPENVDB_LOG_FATAL("unrecognized positional argument: \"" << arg << "\"");
                usage();
            }

            args[i]=1;
        }
    }

    for (int i = 1; i < argc; ++i) {
        if (args[i]) continue;
        OPENVDB_LOG_FATAL("\"" << argv[i] << "\" is not a valid option");
        usage();
    }

    if (opts.mVerbose) {
        axlog("OpenVDB AX " << openvdb::getLibraryVersionString() << '\n');
        axlog("----------------\n");
        axlog("Inputs\n");
        axlog("  mode    : " << modeString(opts.mMode));
        if (opts.mMode == ProgOptions::Analyze) {
            axlog(" (");
            if (opts.mPrintAST) axlog("|ast out");
            if (opts.mReprint)  axlog("|reprint out");
            if (opts.mAttribRegPrint) axlog("|registry out");
            if (opts.mInitCompile)  axlog("|compilation");
            axlog("|)");
        }
        axlog('\n');

        if (opts.mMode == ProgOptions::Execute) {
            axlog("  vdb in  : \"" << opts.mInputVDBFile  << "\"\n");
            axlog("  vdb out : \"" << opts.mOutputVDBFile << "\"\n");
        }
        if (opts.mMode == ProgOptions::Execute ||
            opts.mMode == ProgOptions::Analyze) {
            axlog("  ax code : ");
            if (opts.mInputCode && !opts.mInputCode->empty()) {
                const bool containsnl =
                    opts.mInputCode->find('\n') != std::string::npos;
                if (containsnl) axlog("\n    ");

                // indent output
                const char* c = opts.mInputCode->c_str();
                while (*c != '\0') {
                    axlog(*c);
                    if (*c == '\n') axlog("    ");
                    ++c;
                }
            }
            else {
                axlog("\"\"");
            }
            axlog('\n');
            axlog('\n');
        }
        axlog(std::flush);
    }

    if (opts.mMode != ProgOptions::Functions) {
        if (!opts.mInputCode) {
            OPENVDB_LOG_FATAL("expected at least one AX file or a code snippet");
            usage();
        }
        if (multiSnippet) {
            OPENVDB_LOG_WARN("multiple code snippets provided, only using last input.");
        }
    }

    if (opts.mMode == ProgOptions::Execute) {
        if (opts.mInputVDBFile.empty()) {
            OPENVDB_LOG_FATAL("expected at least one VDB file or analysis mode");
            usage();
        }
        if (opts.mOutputVDBFile.empty()) {
            OPENVDB_LOG_WARN("no output VDB File specified - nothing will be written to disk");
        }
    }

    axtimer();
    axlog("[INFO] Initializing OpenVDB" << std::flush);
    ScopedInitialize initializer(argc, argv);
    axlog(": " << axtime() << '\n');

    // read vdb file data for

    openvdb::GridPtrVecPtr grids;
    openvdb::MetaMap::Ptr meta;

    if (opts.mMode == ProgOptions::Execute) {
        openvdb::io::File file(opts.mInputVDBFile);
        try {
            axtimer();
            axlog("[INFO] Reading VDB data"
                << (opts.mDelayLoad && openvdb::io::Archive::isDelayedLoadingEnabled() ?
                    " (delay-load)" : "") << std::flush);
            /// @note  io::File will query the OPENVDB_DISABLE_DELAYED_LOAD
            ///   environment variable even if mDelayLoad is true.
            file.open(opts.mDelayLoad);
            grids = file.getGrids();
            meta = file.getMetadata();
            file.close();
            axlog(": " << axtime() << '\n');
        } catch (openvdb::Exception& e) {
            OPENVDB_LOG_ERROR(e.what() << " (" << opts.mInputVDBFile << ")");
            return EXIT_FAILURE;
        }
    }

    if (opts.mInitCompile) {
        axtimer();
        axlog("[INFO] Initializing AX/LLVM" << std::flush);
        initializer.initializeCompiler();
        axlog(": " << axtime() << '\n');
    }

    if (opts.mMode == ProgOptions::Functions) {
        if (opts.mFunctionList) {
            axlog("Querying available functions\n" << std::flush);
            assert(opts.mFunctionNamesOnly || initializer.isInitialized());
            printFunctions(opts.mFunctionNamesOnly,
                opts.mFunctionSearch,
                std::cout);
        }
        return EXIT_SUCCESS;
    }

    // set up logger

    BinLog logger(opts.mMaxErrors, opts.mWarningsAsErrors, opts.mVerbose);

    // parse

    axtimer();
    axlog("[INFO] Parsing input code" << std::flush);

    const openvdb::ax::ast::Tree::ConstPtr syntaxTree =
        openvdb::ax::ast::parse(opts.mInputCode->c_str(), logger);
    axlog(": " << axtime() << '\n');
    logger.report();

    if (!syntaxTree) {
        return EXIT_FAILURE;
    }

    if (opts.mMode == ProgOptions::Analyze) {
        axlog("[INFO] Running analysis options\n" << std::flush);
        if (opts.mPrintAST) {
            axlog("[INFO] | Printing AST\n" << std::flush);
            openvdb::ax::ast::print(*syntaxTree, true, std::cout);
        }
        if (opts.mReprint) {
            axlog("[INFO] | Reprinting code\n" << std::flush);
            openvdb::ax::ast::reprint(*syntaxTree, std::cout);
        }
        if (opts.mAttribRegPrint) {
            axlog("[INFO] | Printing Attribute Registry\n" << std::flush);
            const openvdb::ax::AttributeRegistry::ConstPtr reg =
                openvdb::ax::AttributeRegistry::create(*syntaxTree);
            reg->print(std::cout);
            std::cout << std::flush;
        }

        if (!opts.mInitCompile) {
            return EXIT_SUCCESS;
        }
    }

    assert(opts.mInitCompile);

    axtimer();
    axlog("[INFO] Creating Compiler\n");
    axlog("[INFO] | Optimization Level [" << optLevelToString(opts.mOptLevel) << "]\n" << std::flush);
    openvdb::ax::CompilerOptions compOpts;
    compOpts.mOptLevel = opts.mOptLevel;

    openvdb::ax::Compiler::Ptr compiler =
        openvdb::ax::Compiler::create(compOpts);
    openvdb::ax::CustomData::Ptr customData =
        openvdb::ax::CustomData::create();
    axlog("[INFO] | " << axtime() << '\n' << std::flush);

    // Check what we need to compile for if performing execution

    if (opts.mMode == ProgOptions::Execute) {
        assert(meta);
        assert(grids);
        bool points = false;
        bool volumes = false;
        for (auto grid : *grids) {
            points |= grid->isType<openvdb::points::PointDataGrid>();
            volumes |= !points;
            if (points && volumes) break;
        }
        if (points && volumes) opts.mCompileFor = ProgOptions::Compilation::All;
        else if (points)       opts.mCompileFor = ProgOptions::Compilation::Points;
        else if (volumes)      opts.mCompileFor = ProgOptions::Compilation::Volumes;
    }

    if (opts.mMode == ProgOptions::Analyze) {

        bool psuccess = true;

        if (opts.mCompileFor == ProgOptions::Compilation::All ||
            opts.mCompileFor == ProgOptions::Compilation::Points) {
            axtimer();
            axlog("[INFO] Compiling for VDB Points" << std::flush);
            try {
                psuccess = static_cast<bool>(compiler->compile<openvdb::ax::PointExecutable>(*syntaxTree, logger, customData));
             }
            catch (std::exception& e) {
                OPENVDB_LOG_FATAL("\nUnhandled logging exception was thrown during AX compilation:\n" << e.what());
                return EXIT_FAILURE;
            }
            axlog(": " << axtime() << '\n' << std::flush);

            if (psuccess) {
                axlog("[INFO] | Compilation successful");
                if (logger.hasWarning()) axlog(" with warning(s)");
                axlog('\n');
            }
            else {
                assert(logger.hasError());
                axlog("[INFO] | Compilation error(s)!\n");
            }
            logger.report();
        }

        bool vsuccess = true;

        if (opts.mCompileFor == ProgOptions::Compilation::All ||
            opts.mCompileFor == ProgOptions::Compilation::Volumes) {
            axtimer();
            axlog("[INFO] Compiling for VDB Volumes" << std::flush);
            try {
                vsuccess = static_cast<bool>(compiler->compile<openvdb::ax::VolumeExecutable>(*syntaxTree, logger, customData));
            }
            catch (std::exception& e) {
                OPENVDB_LOG_FATAL("\nUnhandled logging exception was thrown during AX compilation:\n" << e.what());
                return EXIT_FAILURE;
            }
            axlog(": " << axtime() << '\n' << std::flush);

            if (vsuccess) {
                axlog("[INFO] | Compilation successful");
                if (logger.hasWarning()) axlog(" with warning(s)");
                axlog('\n');
            }
            else {
                assert(logger.hasError());
                axlog("[INFO] | Compilation error(s)!\n");
            }
            logger.report();
        }

        return ((vsuccess && psuccess) ? EXIT_SUCCESS : EXIT_FAILURE);
    }

    // Execute points

    if (opts.mCompileFor == ProgOptions::Compilation::All ||
        opts.mCompileFor == ProgOptions::Compilation::Points) {

        axlog("[INFO] VDB PointDataGrids Found\n" << std::flush);

        openvdb::ax::PointExecutable::Ptr pointExe;

        axtimer();
        axlog("[INFO] Compiling for VDB Points" << std::flush);
        try {
            pointExe = compiler->compile<openvdb::ax::PointExecutable>(*syntaxTree, logger, customData);
        } catch (std::exception& e) {
            OPENVDB_LOG_FATAL("\nUnhandled logging exception was thrown during AX compilation:\n" << e.what());
            return EXIT_FAILURE;
        }
        axlog(": " << axtime() << '\n' << std::flush);

        if (pointExe) {
            axlog("[INFO] | Compilation successful");
            if (logger.hasWarning()) axlog(" with warning(s)");
            axlog('\n');
            logger.report();
        }
        else {
            assert(logger.hasError());
            axlog("[INFO] | Compilation error(s)!\n");
            logger.report();
            return EXIT_FAILURE;
        }

        pointExe->setSettingsFromCLI(std::move(pointcli));

        size_t total = 0, count = 1;
        if (opts.mVerbose) {
            for (auto grid : *grids) {
                if (!grid->isType<openvdb::points::PointDataGrid>()) continue;
                ++total;
            }
        }

        for (auto grid : *grids) {
            if (!grid->isType<openvdb::points::PointDataGrid>()) continue;
            openvdb::points::PointDataGrid::Ptr points =
                openvdb::gridPtrCast<openvdb::points::PointDataGrid>(grid);
            axlog("[INFO] Executing on \"" << points->getName() << "\" "
                  << count << " of " << total << '\n' << std::flush);
            ++count;

            axtimer();
            try {
                pointExe->execute(*points, &logger);
            }
            catch (std::exception& e) {
                OPENVDB_LOG_FATAL("\nUnhandled logging exception was thrown during AX execution:\n" << e.what());
                return EXIT_FAILURE;
            }

            axlog("[INFO] | Execution success.\n");
            axlog("[INFO] | " << axtime() << '\n' << std::flush);
            logger.report();
        }
    }

    // Execute volumes

    if (opts.mCompileFor == ProgOptions::Compilation::All ||
        opts.mCompileFor == ProgOptions::Compilation::Volumes) {

        axlog("[INFO] VDB Volumes Found\n" << std::flush);

        if (opts.mVerbose) {
            std::vector<const std::string*> names;
            for (auto grid : *grids) {
                if (grid->isType<openvdb::points::PointDataGrid>()) continue;
                axlog("  Name: " << grid->getName());
                axlog(", type: " << grid->valueType());
                axlog(", class: " << grid->gridClassToString(grid->getGridClass()) << '\n');
            }
            axlog(std::flush);
        }

        openvdb::ax::VolumeExecutable::Ptr volumeExe;
        axlog("[INFO] Compiling for VDB Volumes" << std::flush);
        try {
            volumeExe = compiler->compile<openvdb::ax::VolumeExecutable>(*syntaxTree, logger, customData);
        } catch (std::exception& e) {
            OPENVDB_LOG_FATAL("\nUnhandled logging exception was thrown during AX compilation:\n" << e.what());
            return EXIT_FAILURE;
        }
        axlog(": " << axtime() << '\n' << std::flush);

        if (volumeExe) {
            axlog("[INFO] | Compilation successful");
            if (logger.hasWarning()) axlog(" with warning(s)");
            axlog('\n');
            logger.report();
        }
        else {
            assert(logger.hasError());
            axlog("[INFO] | Compilation error(s)!\n");
            logger.report();
            return EXIT_FAILURE;
        }

        volumeExe->setSettingsFromCLI(std::move(volumecli));
        volumeExe->setCreateMissing(false);

        axlog("[INFO] Executing on VDB Volumes" << std::flush);
        axtimer();
        try { volumeExe->execute(*grids, &logger); }
        catch (std::exception& e) {
            OPENVDB_LOG_FATAL("\nUnhandled logging exception was thrown during AX execution:\n" << e.what());
            return EXIT_FAILURE;
        }
        axlog(' '
            << (opts.mDelayLoad && openvdb::io::Archive::isDelayedLoadingEnabled() ?
                "(includes disk-read): " : ": ")
            << axtime() << '\n');

        if (logger.hasError()) {
            axlog("[INFO] | Execution error(s)!\n");
        }
        else {
            axlog("[INFO] | Execution successful");
            if (logger.hasWarning()) axlog(" with warning(s)");
            axlog('\n');
        }

        logger.report();
    }

    if (!opts.mOutputVDBFile.empty()) {
        axtimer();
        axlog("[INFO] Writing results" << std::flush);
        openvdb::io::File out(opts.mOutputVDBFile);
        try {
            out.write(*grids, *meta);
        } catch (openvdb::Exception& e) {
            OPENVDB_LOG_ERROR(e.what() << " (" << out.filename() << ")");
            return EXIT_FAILURE;
        }
        axlog(": " << axtime() << '\n' << std::flush);
    }

    return EXIT_SUCCESS;
}

