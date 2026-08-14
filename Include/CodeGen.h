#pragma once

#include "AST.h"

#include <memory>
#include <string>
#include <vector>
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

            // Mirrors Sema's scope stack. A flat map would let a shadowed `let`
            // overwrite the outer binding and never restore it.
            std::vector<std::unordered_map<std::string, llvm::AllocaInst*>> scopes_;
            
            std::vector<llvm::BasicBlock*> breakTargets_;
            std::vector<llvm::BasicBlock*> continueTargets_;

            llvm::Function* printf_ = nullptr;
            llvm::Type* llvmType(TypeRef type);

            void pushScope();
            void popScope();
            void declareVar(const std::string& name, llvm::AllocaInst* slot);
            llvm::AllocaInst* lookupVar(const std::string& name) const;

            llvm::Value* genExpr(const Expr& expr);
            llvm::Value* toBoolValue(llvm::Value* val);
            llvm::Value* genPrint(const CallExpr& call);
            llvm::Value* genShortCircuit(const BinaryExpr& bin);
            llvm::Value* coerce(llvm::Value* val, llvm::Type* from, llvm::Type* to);

            void declarePrintf();
            void startDeadBlock();
            void genStmt(const Stmt& stmt);
            void genFnDecl(const FnDecl& fn);
    };
}
