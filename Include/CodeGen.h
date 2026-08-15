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

            struct VarBinding {
                llvm::AllocaInst* slot = nullptr;
                TypeRef type = TypeRef::Int;
            };

            struct FnSignature {
                TypeRef returnType = TypeRef::Int;
                std::vector<TypeRef> paramTypes;
            };

            std::vector<std::unordered_map<std::string, VarBinding>> scopes_;
            std::unordered_map<std::string, FnSignature> signatures_;
            TypeRef currentReturnType_ = TypeRef::Int;
            
            std::vector<llvm::BasicBlock*> breakTargets_;
            std::vector<llvm::BasicBlock*> continueTargets_;

            llvm::Function* printf_ = nullptr;

            llvm::Function* stringConcat_ = nullptr;
            llvm::Function* stringCmp_ = nullptr;
            llvm::Function* stringCstr_ = nullptr;
            llvm::Function* dynamicBox_ = nullptr;
            llvm::Function* dynamicUnbox_ = nullptr;
            llvm::Function* dynamicTryUnbox_ = nullptr;
            llvm::Function* dynamicPrint_ = nullptr;


            std::unordered_map<std::string, llvm::Constant*> stringLiterals_;

            llvm::Type* llvmType(TypeRef type);
            llvm::StructType* zstringType();
            llvm::Function* runtimeFn(llvm::Function*& cache, const char* name, llvm::Type* ret, llvm::ArrayRef<llvm::Type*> params);

            llvm::Constant* internString(const std::string& value);
            llvm::Value* boxToDynamic(llvm::Value* v, TypeRef from);
            llvm::Value* unboxFromDynamic(llvm::Value* dyn, TypeRef to, bool safe);
            int dynamicTagFor(TypeRef type);

            void pushScope();
            void popScope();
            void declareVar(const std::string& name, llvm::AllocaInst* slot, TypeRef type);
            const VarBinding* lookupVar(const std::string& name) const;

            llvm::Value* genExpr(const Expr& expr);
            llvm::Value* toBoolValue(llvm::Value* val);
            llvm::Value* genPrint(const CallExpr& call);
            llvm::Value* genShortCircuit(const BinaryExpr& bin);
            llvm::Value* genStringBinary(const BinaryExpr& bin, llvm::Value* lhs, llvm::Value* rhs);
            llvm::Value* genTernary(const TernaryExpr& tern);
            llvm::Value* coerce(llvm::Value* val, llvm::Type* from, llvm::Type* to);
            llvm::Value* convert(llvm::Value* val, TypeRef from, TypeRef to);

            void declarePrintf();
            void startDeadBlock();
            void genStmt(const Stmt& stmt);
            void declareFnDecl(const FnDecl& fn);
            void defineFnDecl(const FnDecl& fn);
    };
}
