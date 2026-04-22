#include "Lexer.h"
#include "Parser.h"
#include "CodeGen.h"

#include <string>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <iostream>
#include <stdexcept>

#include <llvm/IR/Verifier.h>
#include <llvm/Support/CodeGen.h>
#include <llvm/TargetParser/Host.h>
#include <llvm/MC/TargetRegistry.h>
#include <llvm/Support/FileSystem.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/TargetParser/Triple.h>
#include <llvm/IR/LegacyPassManager.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/Target/TargetMachine.h>
#include <llvm/Target/TargetOptions.h>


static std::string indent(int n) { return std::string(n * 2, ' '); }

static void dumpExpr(const ZCompiler::Expr& expr, int d) {
    if (auto* lit = dynamic_cast<const ZCompiler::IntLitExpr*>(&expr)) {
        std::cout << indent(d) << "IntLitExpr(" << lit->value << ")\n";
        return;
    }

    if (auto* call = dynamic_cast<const ZCompiler::CallExpr*>(&expr)) {
        std::cout << indent(d) << "CallExpr(callee=" << call->callee << ")\n";
        for (const auto& arg : call->args)
            dumpExpr(*arg, d + 1);

        return;
    }

    if (auto* id = dynamic_cast<const ZCompiler::IdentExpr*>(&expr)) {
        std::cout << indent(d) << "IdentExpr(" << id->name << ")\n";
        return;
    }

    if (auto* bin = dynamic_cast<const ZCompiler::BinaryExpr*>(&expr)) {
        std::cout << indent(d) << "BinaryExpr(" << bin->op << ")\n";
        dumpExpr(*bin->lhs, d + 1);
        dumpExpr(*bin->rhs, d + 1);
        return;
    }

    std::cout << indent(d) << "UnknownExpr\n";
}

static void dumpStmt(const ZCompiler::Stmt& stmt, int d) {
    if (auto* ret = dynamic_cast<const ZCompiler::ReturnStmt*>(&stmt)) {
        std::cout << indent(d) << "ReturnStmt\n";
        dumpExpr(*ret->value, d + 1);
        return;
    }

    if (auto* es = dynamic_cast<const ZCompiler::ExprStmt*>(&stmt)) {
        std::cout << indent(d) << "ExprStmt\n";
        dumpExpr(*es->expr, d + 1);
        return;
    }

    if (auto* let = dynamic_cast<const ZCompiler::LetStmt*>(&stmt)) {
        std::cout << indent(d) << "LetStmt(name=" << let->name << ")\n";
        dumpExpr(*let->init, d + 1);
        return;
    }

    if (auto* asgn = dynamic_cast<const ZCompiler::AssignStmt*>(&stmt)) {
        std::cout << indent(d) << "AssignStmt(" << asgn->name << ")\n";
        dumpExpr(*asgn->value, d + 1);
        return;
    }

    std::cout << indent(d) << "UnknownStmt\n";
}

static void dumpProgram(const ZCompiler::Program& prog) {
    std::cout << "Program\n";
    for (const auto& decl : prog.decls) {
        if (auto* fn = dynamic_cast<const ZCompiler::FnDecl*>(decl.get())) {
            std::cout << "  FnDecl(name=" << fn->name << ")\n";
            for (const auto& stmt : fn->body->stmts)
                dumpStmt(*stmt, 2);
        }
    }
}

int main(int argc, char* argv[]) {
    bool dumpTokens = false;
    bool dumpAst    = false;
    bool emitLlvm   = false;

    std::string inputFile;
    std::string outputFile = "a.exe";

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];

        if (arg == "--dump-tokens") 
            dumpTokens = true;
        else if (arg == "--dump-ast")    
            dumpAst    = true;
        else if (arg == "--emit-llvm")   
            emitLlvm   = true;
        else if (arg == "-o" && i + 1 < argc) 
            outputFile = argv[++i];
        else 
            inputFile = arg;
    }

    if (inputFile.empty()) {
        std::cerr << "usage: zc [--dump-tokens] [--dump-ast] [--emit-llvm] [-o out] <file.z>\n";
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

        if (dumpAst) {
            dumpProgram(program);
            return 0;
        }

        ZCompiler::CodeGen cg(inputFile);
        cg.generate(program);
        auto module = cg.takeModule();

        std::string verifyErr;
        llvm::raw_string_ostream verifyStream(verifyErr);
        if (llvm::verifyModule(*module, &verifyStream)) {
            std::cerr << "IR verification failed:\n" << verifyErr << "\n";
            return 1;
        }

        if (emitLlvm) {
            module->print(llvm::outs(), nullptr);
            return 0;
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
        llvm::TargetMachine* tm = target->createTargetMachine(
            targetTriple, "generic", "", opt, std::nullopt
        );
        module->setDataLayout(tm->createDataLayout());

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

        std::string linkCmd = "clang " + objFile + " -o " + outputFile;
        int ret = std::system(linkCmd.c_str());
        if (ret != 0) {
            std::cerr << "error: linker failed (exit " << ret << ")\n";
            return 1;
        }

        std::cout << "compiled: " << outputFile << "\n";
        delete tm;

    } catch (const std::exception& e) {
        std::cerr << "error: " << e.what() << "\n";
        return 1;
    }

    return 0;
}
