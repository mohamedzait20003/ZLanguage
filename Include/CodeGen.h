#pragma once

#include "AST.h"

#include <memory>
#include <string>
#include <unordered_map>
#include <llvm/IR/Module.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/LLVMContext.h>

namespace ZCompiler {

    class CodeGen {
        public:
            explicit CodeGen(const std::string& moduleName);

            void generate(const Program& program);
            std::unique_ptr<llvm::Module> takeModule();

        private:
            llvm::LLVMContext ctx_;
            std::unique_ptr<llvm::Module> module_;

            llvm::IRBuilder<> builder_;
            std::unordered_map<std::string, llvm::AllocaInst*> symbols_;
            
            llvm::Function* printf_ = nullptr;
            llvm::Value* genExpr(const Expr& expr);

            void declarePrintf();
            void genStmt(const Stmt& stmt);
            void genFnDecl(const FnDecl& fn);
    };
}
