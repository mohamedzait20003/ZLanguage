#include "Lexer.h"
#include "Parser.h"
#include "Sema.h"
#include "CodeGen.h"

#include <string>
#include <vector>
#include <algorithm>
#include <filesystem>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <iostream>
#include <stdexcept>

#include <llvm/IR/Verifier.h>
#include <llvm/Support/CodeGen.h>
#include <llvm/TargetParser/Host.h>
#include <llvm/MC/TargetRegistry.h>
#include <llvm/Passes/PassBuilder.h>
#include <llvm/Support/FileSystem.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/TargetParser/Triple.h>
#include <llvm/IR/LegacyPassManager.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/Target/TargetMachine.h>
#include <llvm/Target/TargetOptions.h>


static std::string indent(int n) { return std::string(n * 2, ' '); }

static const char* typeName(ZCompiler::TypeRef t) {
    using T = ZCompiler::TypeRef;
    switch (t) {
        case T::Int:       return "int";
        case T::Int32:     return "int32";
        case T::Int64:     return "int64";
        case T::Int128:    return "int128";
        case T::Float:     return "float";
        case T::Float16:   return "float16";
        case T::Float32:   return "float32";
        case T::Float64:   return "float64";
        case T::Double:    return "double";
        case T::Bool:      return "bool";
        case T::Character: return "character";
        case T::String:    return "string";
        case T::Dynamic:   return "dynamic";
        case T::Null:      return "null";
    }
    return "?";
}

static void dumpStmt(const ZCompiler::Stmt& stmt, int d);

static void dumpBlock(const ZCompiler::BlockStmt& block, int d, const char* label) {
    std::cout << indent(d) << label << "\n";
    for (const auto& s : block.stmts)
        dumpStmt(*s, d + 1);
}

static void dumpExpr(const ZCompiler::Expr& expr, int d) {
    using namespace ZCompiler;

    if (auto* lit = dynamic_cast<const IntLitExpr*>(&expr)) {
        std::cout << indent(d) << "IntLit(" << lit->value << ")\n";
        return;
    }
    if (auto* lit = dynamic_cast<const FloatLitExpr*>(&expr)) {
        std::cout << indent(d) << "FloatLit(" << lit->value << ")\n";
        return;
    }
    if (auto* lit = dynamic_cast<const DoubleLitExpr*>(&expr)) {
        std::cout << indent(d) << "DoubleLit(" << lit->value << ")\n";
        return;
    }
    if (auto* lit = dynamic_cast<const BoolLitExpr*>(&expr)) {
        std::cout << indent(d) << "BoolLit(" << (lit->value ? "true" : "false") << ")\n";
        return;
    }
    if (auto* lit = dynamic_cast<const CharLitExpr*>(&expr)) {
        std::cout << indent(d) << "CharLit(" << lit->value << ")\n";
        return;
    }
    if (auto* lit = dynamic_cast<const StringLitExpr*>(&expr)) {
        std::cout << indent(d) << "StringLit(\"" << lit->value << "\")\n";
        return;
    }
    if (dynamic_cast<const NullLitExpr*>(&expr)) {
        std::cout << indent(d) << "NullLit\n";
        return;
    }
    if (auto* id = dynamic_cast<const IdentExpr*>(&expr)) {
        std::cout << indent(d) << "Ident(" << id->name << ")\n";
        return;
    }
    if (auto* call = dynamic_cast<const CallExpr*>(&expr)) {
        std::cout << indent(d) << "Call("
                  << (call->qualifier.empty() ? "" : call->qualifier + ".")
                  << call->callee << ")\n";
        for (const auto& arg : call->args)
            dumpExpr(*arg, d + 1);
        return;
    }
    if (auto* bin = dynamic_cast<const BinaryExpr*>(&expr)) {
        std::cout << indent(d) << "Binary(" << bin->op << ")\n";
        dumpExpr(*bin->lhs, d + 1);
        dumpExpr(*bin->rhs, d + 1);
        return;
    }
    if (auto* un = dynamic_cast<const UnaryExpr*>(&expr)) {
        std::cout << indent(d) << "Unary(" << un->op << ")\n";
        dumpExpr(*un->operand, d + 1);
        return;
    }
    if (auto* tern = dynamic_cast<const TernaryExpr*>(&expr)) {
        std::cout << indent(d) << "Ternary\n";
        dumpExpr(*tern->cond, d + 1);
        dumpExpr(*tern->thenExpr, d + 1);
        dumpExpr(*tern->elseExpr, d + 1);
        return;
    }
    if (auto* cast = dynamic_cast<const CastExpr*>(&expr)) {
        std::cout << indent(d) << "StaticCast(" << typeName(cast->targetType) << ")\n";
        dumpExpr(*cast->operand, d + 1);
        return;
    }
    if (auto* cast = dynamic_cast<const DynCastExpr*>(&expr)) {
        std::cout << indent(d) << "DynamicCast(" << typeName(cast->targetType) << ")\n";
        dumpExpr(*cast->operand, d + 1);
        return;
    }

    std::cout << indent(d) << "UnknownExpr\n";
}

static void dumpStmt(const ZCompiler::Stmt& stmt, int d) {
    using namespace ZCompiler;

    if (auto* ret = dynamic_cast<const ReturnStmt*>(&stmt)) {
        std::cout << indent(d) << "Return\n";
        dumpExpr(*ret->value, d + 1);
        return;
    }
    if (auto* es = dynamic_cast<const ExprStmt*>(&stmt)) {
        std::cout << indent(d) << "ExprStmt\n";
        dumpExpr(*es->expr, d + 1);
        return;
    }
    if (auto* let = dynamic_cast<const LetStmt*>(&stmt)) {
        std::cout << indent(d) << "Let(" << let->name << ": " << typeName(let->type) << ")\n";
        dumpExpr(*let->init, d + 1);
        return;
    }
    if (auto* asgn = dynamic_cast<const AssignStmt*>(&stmt)) {
        std::cout << indent(d) << "Assign(" << asgn->name << ")\n";
        dumpExpr(*asgn->value, d + 1);
        return;
    }
    if (auto* ifs = dynamic_cast<const IfStmt*>(&stmt)) {
        std::cout << indent(d) << "If\n";
        dumpExpr(*ifs->cond, d + 1);
        dumpBlock(*ifs->thenBranch, d + 1, "Then");
        if (ifs->elseBranch)
            dumpBlock(*ifs->elseBranch, d + 1, "Else");
        return;
    }
    if (auto* whl = dynamic_cast<const WhileStmt*>(&stmt)) {
        std::cout << indent(d) << "While\n";
        dumpExpr(*whl->cond, d + 1);
        dumpBlock(*whl->body, d + 1, "Body");
        return;
    }
    if (auto* ds = dynamic_cast<const DoStmt*>(&stmt)) {
        std::cout << indent(d) << "DoAlong\n";
        dumpBlock(*ds->body, d + 1, "Body");
        dumpExpr(*ds->cond, d + 1);
        return;
    }
    if (auto* fs = dynamic_cast<const ForStmt*>(&stmt)) {
        std::cout << indent(d) << "For\n";
        dumpStmt(*fs->init, d + 1);
        dumpExpr(*fs->cond, d + 1);
        dumpStmt(*fs->step, d + 1);
        dumpBlock(*fs->body, d + 1, "Body");
        return;
    }
    if (auto* sw = dynamic_cast<const SwitchStmt*>(&stmt)) {
        std::cout << indent(d) << "Switch\n";
        dumpExpr(*sw->scrutinee, d + 1);
        for (const auto& arm : sw->cases) {
            std::cout << indent(d + 1) << "Case\n";
            dumpExpr(*arm.value, d + 2);
            dumpBlock(*arm.body, d + 2, "Body");
        }
        if (sw->defaultArm)
            dumpBlock(*sw->defaultArm, d + 1, "Default");
        return;
    }
    if (dynamic_cast<const BreakStmt*>(&stmt)) {
        std::cout << indent(d) << "Break\n";
        return;
    }
    if (dynamic_cast<const ContinueStmt*>(&stmt)) {
        std::cout << indent(d) << "Continue\n";
        return;
    }
    if (auto* block = dynamic_cast<const BlockStmt*>(&stmt)) {
        dumpBlock(*block, d, "Block");
        return;
    }

    std::cout << indent(d) << "UnknownStmt\n";
}

static void dumpFn(const ZCompiler::FnDecl& fn, int d) {
    std::cout << indent(d) << "Fn " << fn.name << "(";
    for (std::size_t i = 0; i < fn.params.size(); ++i) {
        if (i) std::cout << ", ";
        std::cout << fn.params[i].name << ": " << typeName(fn.params[i].type);
    }
    std::cout << ") -> " << typeName(fn.returnType) << "\n";

    for (const auto& stmt : fn.body->stmts)
        dumpStmt(*stmt, d + 1);
}

// Namespaces nest, so the dump does too. The printed name is the fully
// qualified dotted one, which is also the key Sema uses.
static void dumpNamespace(const ZCompiler::NamespaceDecl& ns, int d) {
    using namespace ZCompiler;

    std::cout << indent(d) << "Namespace " << ns.name << "\n";

    for (const auto& member : ns.decls) {
        if (auto* nested = dynamic_cast<const NamespaceDecl*>(member.get())) {
            dumpNamespace(*nested, d + 1);
            continue;
        }

        if (auto* fn = dynamic_cast<const FnDecl*>(member.get()))
            dumpFn(*fn, d + 1);
    }
}

static void dumpProgram(const ZCompiler::Program& prog) {
    using namespace ZCompiler;

    std::cout << "Program\n";

    for (const auto& use : prog.usings)
        std::cout << "  Using(" << use.name << ")\n";

    for (const auto& decl : prog.decls) {
        if (auto* fn = dynamic_cast<const FnDecl*>(decl.get())) {
            dumpFn(*fn, 1);
            continue;
        }

        if (auto* ns = dynamic_cast<const NamespaceDecl*>(decl.get()))
            dumpNamespace(*ns, 1);
    }
}

static std::string readFile(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in)
        throw std::runtime_error("cannot open '" + path + "'");

    std::ostringstream buf;
    buf << in.rdbuf();
    return buf.str();
}

// The standard library is ordinary Z source, not compiler-synthesised
// declarations. Every stdlib file is parsed through the same pipeline as user
// code and its namespaces are merged into the program, so `using string`
// resolves through the M4/M5 namespace mechanism with no special registry.
//
// Every file is parsed regardless of what the program imports: qualified access
// (`string.length(s)`) is valid without a `using`, so the namespaces have to
// exist before Sema runs. Unused functions cost nothing in the output — they are
// emitted with internal linkage and deleted by globaldce.
static void loadStdlib(ZCompiler::Program& program, const std::string& stdlibDir) {
    namespace fs = std::filesystem;

    std::error_code ec;
    if (!fs::is_directory(stdlibDir, ec))
        return;

    std::vector<std::string> files;
    for (const auto& entry : fs::directory_iterator(stdlibDir, ec))
        if (entry.is_regular_file() && entry.path().extension() == ".z")
            files.push_back(entry.path().string());

    // Deterministic order, so a duplicate-symbol diagnostic never depends on
    // how the filesystem happened to enumerate the directory.
    std::sort(files.begin(), files.end());

    for (const auto& file : files) {
        // A diagnostic from a stdlib file is useless without its name — the user
        // did not write this code and has no other way to locate the line.
        auto located = [&file](const std::exception& e) {
            return std::runtime_error("in stdlib file '" + file + "': " + e.what());
        };

        ZCompiler::Program lib;
        try {
            ZCompiler::Lexer lexer(readFile(file));
            ZCompiler::Parser parser(lexer.tokenize());
            lib = parser.parse();
        } catch (const std::exception& e) {
            throw located(e);
        }

        if (!lib.usings.empty())
            throw std::runtime_error("stdlib file '" + file + "' may not contain 'using' declarations");

        for (auto& decl : lib.decls) {
            // Only namespaces are merged. A bare function in a stdlib file would
            // land in the user's file scope and silently shadow their own names.
            if (!dynamic_cast<ZCompiler::NamespaceDecl*>(decl.get()))
                throw std::runtime_error(
                    "stdlib file '" + file + "' may only declare namespaces at top level");

            program.decls.push_back(std::move(decl));
        }
    }
}

// stdlib/ sits next to the repository, not next to the binary, so it is located
// relative to the executable and then by walking up. Z_STDLIB_DIR from CMake is
// the authoritative answer; the search is a fallback for a relocated binary.
static std::string findStdlib(const char* argv0) {
    namespace fs = std::filesystem;
    std::error_code ec;

#ifdef Z_STDLIB_DIR
    if (fs::is_directory(Z_STDLIB_DIR, ec))
        return Z_STDLIB_DIR;
#endif

    fs::path dir = fs::absolute(fs::path(argv0), ec).parent_path();

    for (int depth = 0; depth < 4 && !dir.empty(); ++depth) {
        const fs::path candidate = dir / "stdlib";

        if (fs::is_directory(candidate, ec))
            return candidate.string();

        dir = dir.parent_path();
    }

    return {};
}

int main(int argc, char* argv[]) {
    bool dumpTokens = false;
    bool dumpAst = false;
    bool emitLlvm = false;
    unsigned optLevel = 2;

    std::string inputFile;
    std::string outputFile = "a.exe";

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];

        if (arg == "--dump-tokens")
            dumpTokens = true;
        else if (arg == "--dump-ast")
            dumpAst = true;
        else if (arg == "--emit-llvm")
            emitLlvm = true;
        else if (arg == "-O0")
            optLevel = 0;
        else if (arg == "-O1")
            optLevel = 1;
        else if (arg == "-O2")
            optLevel = 2;
        else if (arg == "-O3")
            optLevel = 3;
        else if (arg == "-o" && i + 1 < argc)
            outputFile = argv[++i];
        else
            inputFile = arg;
    }

    if (inputFile.empty()) {
        std::cerr << "usage: zc [--dump-tokens] [--dump-ast] [--emit-llvm] [-O0..-O3] [-o out] <file.z>\n";
        return 1;
    }

    std::ifstream f(inputFile);
    if (!f) {
        std::cerr << "error: cannot open '" << inputFile << "'\n";
        return 1;
    }
    std::ostringstream buf;
    buf << f.rdbuf();
    std::string source = buf.str();

    try {
        ZCompiler::Lexer lexer(source);
        std::vector<ZCompiler::Token> tokens = lexer.tokenize();

        if (dumpTokens) {
            for (const auto& t : tokens)
                std::cout << t.line << ":" << t.column
                          << "  " << ZCompiler::to_string(t.type)
                          << "  '" << t.lexeme << "'\n";
            return 0;
        }

        ZCompiler::Parser parser(tokens);
        ZCompiler::Program program = parser.parse();

        // --dump-ast shows the user's program alone; merging the stdlib in first
        // would bury it under hundreds of library declarations.
        if (dumpAst) {
            dumpProgram(program);
            return 0;
        }

        loadStdlib(program, findStdlib(argv[0]));

        ZCompiler::Sema sema;
        sema.check(program);

        ZCompiler::CodeGen cg(inputFile);
        cg.generate(program);
        auto module = cg.takeModule();

        std::string verifyErr;
        llvm::raw_string_ostream verifyStream(verifyErr);
        if (llvm::verifyModule(*module, &verifyStream)) {
            std::cerr << "IR verification failed:\n" << verifyErr << "\n";
            return 1;
        }

        llvm::InitializeNativeTarget();
        llvm::InitializeNativeTargetAsmPrinter();
        llvm::InitializeNativeTargetAsmParser();

        llvm::Triple targetTriple(llvm::sys::getDefaultTargetTriple());
        module->setTargetTriple(targetTriple);

        std::string lookupErr;
        const llvm::Target* target =
            llvm::TargetRegistry::lookupTarget(targetTriple, lookupErr);
        if (!target) {
            std::cerr << "error: " << lookupErr << "\n";
            return 1;
        }

        llvm::TargetOptions opt;
        std::unique_ptr<llvm::TargetMachine> tm(target->createTargetMachine(
            targetTriple, "generic", "", opt, std::nullopt
        ));
        module->setDataLayout(tm->createDataLayout());

        // The optimisation pipeline runs before --emit-llvm so the dumped IR shows
        // what the backend will actually see. `-O0 --emit-llvm` gives the raw
        // CodeGen output; `-O2 --emit-llvm` shows it after the passes.
        if (optLevel > 0) {
            llvm::OptimizationLevel level = llvm::OptimizationLevel::O2;

            if (optLevel == 1)
                level = llvm::OptimizationLevel::O1;
            else if (optLevel == 3)
                level = llvm::OptimizationLevel::O3;

            llvm::PassBuilder pb(tm.get());
            llvm::LoopAnalysisManager     lam;
            llvm::FunctionAnalysisManager fam;
            llvm::CGSCCAnalysisManager    cgam;
            llvm::ModuleAnalysisManager   mam;

            pb.registerModuleAnalyses(mam);
            pb.registerCGSCCAnalyses(cgam);
            pb.registerFunctionAnalyses(fam);
            pb.registerLoopAnalyses(lam);
            pb.crossRegisterProxies(lam, fam, cgam, mam);
            pb.buildPerModuleDefaultPipeline(level).run(*module, mam);

            // Optimisation must not be able to produce malformed IR. If it does,
            // the input IR relied on undefined behaviour.
            std::string postErr;
            llvm::raw_string_ostream postStream(postErr);
            if (llvm::verifyModule(*module, &postStream)) {
                std::cerr << "IR verification failed after -O" << optLevel
                          << " pipeline:\n" << postErr << "\n";
                return 1;
            }
        }

        if (emitLlvm) {
            module->print(llvm::outs(), nullptr);
            return 0;
        }

        std::string objFile = outputFile + ".o";
        std::error_code ec;
        llvm::raw_fd_ostream dest(objFile, ec, llvm::sys::fs::OF_None);
        if (ec) {
            std::cerr << "error: cannot open output file: " << ec.message() << "\n";
            return 1;
        }

        llvm::legacy::PassManager pm;
        if (tm->addPassesToEmitFile(pm, dest, nullptr,
                                    llvm::CodeGenFileType::ObjectFile)) {
            std::cerr << "error: target cannot emit object file\n";
            return 1;
        }
        pm.run(*module);
        dest.flush();

#ifdef Z_RUNTIME_LIB
        const std::string runtimeLib = " \"" Z_RUNTIME_LIB "\"";
#else
        const std::string runtimeLib = "";
#endif

        std::string linkCmd = "clang \"" + objFile + "\"" + runtimeLib + " -o \"" + outputFile + "\"";
        int ret = std::system(linkCmd.c_str());
        if (ret != 0) {
            std::cerr << "error: linker failed (exit " << ret << ")\n";
            return 1;
        }

        std::cout << "compiled: " << outputFile << "\n";

    } catch (const std::exception& e) {
        std::cerr << "error: " << e.what() << "\n";
        return 1;
    }

    return 0;
}
