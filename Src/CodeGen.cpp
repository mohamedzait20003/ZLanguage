#include "CodeGen.h"

#include <stdexcept>
#include <llvm/IR/Type.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/DerivedTypes.h>


namespace ZCompiler {
    CodeGen::CodeGen(const std::string& moduleName) : module_(std::make_unique<llvm::Module>(moduleName, ctx_)), builder_(ctx_) {}

    std::unique_ptr<llvm::Module> CodeGen::takeModule() {
        return std::move(module_);
    }

    void CodeGen::declarePrintf() {
        llvm::Type* i8ptr = llvm::PointerType::get(llvm::Type::getInt8Ty(ctx_), 0);
        llvm::FunctionType* ft = llvm::FunctionType::get(
            llvm::Type::getInt32Ty(ctx_), {i8ptr}, true
        );

        printf_ = llvm::Function::Create(
            ft, llvm::Function::ExternalLinkage, "printf", module_.get()
        );
    }

    void CodeGen::generate(const Program& program) {
        declarePrintf();
        for (const auto& decl : program.decls) {
            if (auto* fn = dynamic_cast<const FnDecl*>(decl.get()))
                genFnDecl(*fn);
        }
    }

    void CodeGen::genFnDecl(const FnDecl& fn) {
        llvm::FunctionType* ft = llvm::FunctionType::get(
            llvm::Type::getInt32Ty(ctx_), {}, false
        );

        llvm::Function* func = llvm::Function::Create(
            ft, llvm::Function::ExternalLinkage, fn.name, module_.get()
        );

        llvm::BasicBlock* entry = llvm::BasicBlock::Create(ctx_, "entry", func);
        builder_.SetInsertPoint(entry);

        for (const auto& stmt : fn.body->stmts)
            genStmt(*stmt);
    }

    void CodeGen::genStmt(const Stmt& stmt) {
        if (auto* ret = dynamic_cast<const ReturnStmt*>(&stmt)) {
            llvm::Value* val = genExpr(*ret->value);

            llvm::Value* ret32 = builder_.CreateTrunc(
                val, llvm::Type::getInt32Ty(ctx_), "ret32"
            );

            builder_.CreateRet(ret32);
            return;
        }
    
        if (auto* es = dynamic_cast<const ExprStmt*>(&stmt)) {
            genExpr(*es->expr);
            return;
        }

        throw std::runtime_error("codegen: unknown statement type");
    }

    llvm::Value* CodeGen::genExpr(const Expr& expr) {
        if (auto* lit = dynamic_cast<const IntLitExpr*>(&expr)) {
            return llvm::ConstantInt::get(
                llvm::Type::getInt64Ty(ctx_), lit->value, true
            );
        }

        if (auto* call = dynamic_cast<const CallExpr*>(&expr)) {
            if (call->callee == "print") {
                if (call->args.empty())
                    throw std::runtime_error("print() requires one argument");
            
                llvm::Value* arg = genExpr(*call->args[0]);
                llvm::Value* fmt = builder_.CreateGlobalStringPtr("%lld\n", "fmt_int");
                return builder_.CreateCall(printf_, {fmt, arg});
            }

            throw std::runtime_error("codegen: unknown function '" + call->callee + "'");
        }

        throw std::runtime_error("codegen: unknown expression type");
    }
}
