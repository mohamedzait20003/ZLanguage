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

    static llvm::AllocaInst* createEntryBlockAlloca(llvm::Function* fn, const std::string& name, llvm::Type* ty) {
        llvm::IRBuilder<> tmp(&fn->getEntryBlock(), fn->getEntryBlock().begin());
        return tmp.CreateAlloca(ty, nullptr, name);
    }

    void CodeGen::declarePrintf() {
        llvm::Type* ptrTy = llvm::PointerType::get(ctx_, 0);
        llvm::FunctionType* ft = llvm::FunctionType::get(
            llvm::Type::getInt32Ty(ctx_), {ptrTy}, true
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
        symbols_.clear();
        std::vector<llvm::Type*> paramTypes(fn.params.size(), llvm::Type::getInt64Ty(ctx_));

        llvm::Type* retTy = (fn.name == "main") ? llvm::Type::getInt32Ty(ctx_) : llvm::Type::getInt64Ty(ctx_);

        llvm::FunctionType* ft = llvm::FunctionType::get(retTy, paramTypes, false);

        llvm::Function* func = llvm::Function::Create(
            ft, llvm::Function::ExternalLinkage, fn.name, module_.get()
        );

        std::size_t i = 0;
        for (auto& arg : func->args())
            arg.setName(fn.params[i++].name);

        llvm::BasicBlock* entry = llvm::BasicBlock::Create(ctx_, "entry", func);
        builder_.SetInsertPoint(entry);

        i = 0;
        for (auto& arg : func->args()) {
            llvm::AllocaInst* alloca = createEntryBlockAlloca(func, fn.params[i].name, llvm::Type::getInt64Ty(ctx_));
            builder_.CreateStore(&arg, alloca);
            symbols_[fn.params[i].name] = alloca;
            i++;
        }

        for (const auto& stmt : fn.body->stmts)
            genStmt(*stmt);

        if (!builder_.GetInsertBlock()->getTerminator())
            builder_.CreateUnreachable();
    }

    void CodeGen::genStmt(const Stmt& stmt) {
        if (auto* ret = dynamic_cast<const ReturnStmt*>(&stmt)) {
            llvm::Value* val = genExpr(*ret->value);
            llvm::Type* retTy = builder_.GetInsertBlock()->getParent()->getReturnType();

            if (retTy->isIntegerTy(32))
                val = builder_.CreateTrunc(val, retTy, "ret32");

            builder_.CreateRet(val);
            return;
        }
    
        if (auto* es = dynamic_cast<const ExprStmt*>(&stmt)) {
            genExpr(*es->expr);
            return;
        }

        if (auto* let = dynamic_cast<const LetStmt*>(&stmt)) {
            llvm::Function* fn = builder_.GetInsertBlock()->getParent();
            llvm::AllocaInst* alloca = createEntryBlockAlloca(fn, let->name, llvm::Type::getInt64Ty(ctx_));
            symbols_[let->name] = alloca;

            llvm::Value* initVal = genExpr(*let->init);
            builder_.CreateStore(initVal, alloca);
            return;
        }

        if (auto* asgn = dynamic_cast<const AssignStmt*>(&stmt)) {
            auto it = symbols_.find(asgn->name);
            if (it == symbols_.end())
                throw std::runtime_error("assignment to undeclared variable '" + asgn->name + "'");

            llvm::Value* val = genExpr(*asgn->value);
            builder_.CreateStore(val, it->second);
            return;
        }

        if (auto* ifs = dynamic_cast<const IfStmt*>(&stmt)) {
            llvm::Function* fn = builder_.GetInsertBlock()->getParent();
            llvm::Value* condVal = genExpr(*ifs->cond);

            llvm::Value* condBool = builder_.CreateICmpNE(
                condVal, llvm::ConstantInt::get(llvm::Type::getInt64Ty(ctx_), 0), "ifcond"
            );

            llvm::BasicBlock* thenBB = llvm::BasicBlock::Create(ctx_, "then", fn);
            llvm::BasicBlock* elseBB = llvm::BasicBlock::Create(ctx_, "else");
            llvm::BasicBlock* mergeBB = llvm::BasicBlock::Create(ctx_, "merge");

            builder_.CreateCondBr(condBool, thenBB, elseBB);

            builder_.SetInsertPoint(thenBB);
            for (const auto& stmt : ifs->thenBranch->stmts)
                genStmt(*stmt);
            
            if (!builder_.GetInsertBlock()->getTerminator())
                builder_.CreateBr(mergeBB);

            elseBB->insertInto(fn);
            builder_.SetInsertPoint(elseBB);
            if (ifs->elseBranch) {
                for (const auto& stmt : ifs->elseBranch->stmts)
                    genStmt(*stmt);
            }

            if (!builder_.GetInsertBlock()->getTerminator())
                builder_.CreateBr(mergeBB);

            mergeBB->insertInto(fn);
            builder_.SetInsertPoint(mergeBB);
            return;
        }

        if (auto* whl = dynamic_cast<const WhileStmt*>(&stmt)) {
            llvm::Function* fn = builder_.GetInsertBlock()->getParent();

            llvm::BasicBlock* headerBB = llvm::BasicBlock::Create(ctx_, "while.cond", fn);
            llvm::BasicBlock* bodyBB   = llvm::BasicBlock::Create(ctx_, "while.body", fn);
            llvm::BasicBlock* exitBB   = llvm::BasicBlock::Create(ctx_, "while.exit", fn);

            builder_.CreateBr(headerBB);

            builder_.SetInsertPoint(headerBB);
            llvm::Value* cond = genExpr(*whl->cond);
            llvm::Value* condBool = builder_.CreateICmpNE(
                cond, llvm::ConstantInt::get(llvm::Type::getInt64Ty(ctx_), 0), "whilecond"
            );

            builder_.CreateCondBr(condBool, bodyBB, exitBB);

            builder_.SetInsertPoint(bodyBB);

            for (const auto& s : whl->body->stmts)
                genStmt(*s);

            if (!builder_.GetInsertBlock()->getTerminator())
                builder_.CreateBr(headerBB);

            builder_.SetInsertPoint(exitBB);
            return;
        }

        if (auto* ds = dynamic_cast<const DoStmt*>(&stmt)) {
            llvm::Function* fn = builder_.GetInsertBlock()->getParent();

            llvm::BasicBlock* bodyBB = llvm::BasicBlock::Create(ctx_, "do.body", fn);
            llvm::BasicBlock* condBB = llvm::BasicBlock::Create(ctx_, "do.cond", fn);
            llvm::BasicBlock* exitBB = llvm::BasicBlock::Create(ctx_, "do.exit", fn);

            builder_.CreateBr(bodyBB);   // unconditional — body runs before first check

            builder_.SetInsertPoint(bodyBB);
            for (const auto& s : ds->body->stmts)
                genStmt(*s);
            if (!builder_.GetInsertBlock()->getTerminator())
                builder_.CreateBr(condBB);

            builder_.SetInsertPoint(condBB);
            llvm::Value* cond = genExpr(*ds->cond);
            llvm::Value* condBool = builder_.CreateICmpNE(
                cond, llvm::ConstantInt::get(llvm::Type::getInt64Ty(ctx_), 0), "doalong_cond"
            );
            builder_.CreateCondBr(condBool, bodyBB, exitBB);

            builder_.SetInsertPoint(exitBB);
            return;
        }

        if (auto* fs = dynamic_cast<const ForStmt*>(&stmt)) {
            llvm::Function* fn = builder_.GetInsertBlock()->getParent();

            genStmt(*fs->init);

            llvm::BasicBlock* headerBB = llvm::BasicBlock::Create(ctx_, "for.cond", fn);
            llvm::BasicBlock* bodyBB   = llvm::BasicBlock::Create(ctx_, "for.body", fn);
            llvm::BasicBlock* stepBB   = llvm::BasicBlock::Create(ctx_, "for.step", fn);
            llvm::BasicBlock* exitBB   = llvm::BasicBlock::Create(ctx_, "for.exit", fn);

            builder_.CreateBr(headerBB);

            builder_.SetInsertPoint(headerBB);
            llvm::Value* cond = genExpr(*fs->cond);
            llvm::Value* condBool = builder_.CreateICmpNE(
                cond, llvm::ConstantInt::get(llvm::Type::getInt64Ty(ctx_), 0), "forcond"
            );
            builder_.CreateCondBr(condBool, bodyBB, exitBB);

            builder_.SetInsertPoint(bodyBB);
            for (const auto& s : fs->body->stmts)
                genStmt(*s);
            if (!builder_.GetInsertBlock()->getTerminator())
                builder_.CreateBr(stepBB);

            builder_.SetInsertPoint(stepBB);
            genStmt(*fs->step);
            builder_.CreateBr(headerBB);

            builder_.SetInsertPoint(exitBB);
            return;
        }

        if (auto* sw = dynamic_cast<const SwitchStmt*>(&stmt)) {
            llvm::Function* fn = builder_.GetInsertBlock()->getParent();
            llvm::Value* scrutinee = genExpr(*sw->scrutinee);

            llvm::BasicBlock* mergeBB = llvm::BasicBlock::Create(ctx_, "switch.end", fn);
            llvm::BasicBlock* defaultBB = sw->defaultArm
                ? llvm::BasicBlock::Create(ctx_, "switch.default", fn)
                : mergeBB;

            llvm::SwitchInst* sw_inst = builder_.CreateSwitch(scrutinee, defaultBB,
                                                               sw->cases.size());

            for (const auto& arm : sw->cases) {
                llvm::BasicBlock* caseBB = llvm::BasicBlock::Create(ctx_, "switch.case", fn);
                llvm::Value* caseVal = genExpr(*arm.value);
                auto* constVal = llvm::dyn_cast<llvm::ConstantInt>(caseVal);
                if (!constVal)
                    throw std::runtime_error("codegen: switch case value must be a constant integer");
                sw_inst->addCase(constVal, caseBB);

                builder_.SetInsertPoint(caseBB);
                for (const auto& s : arm.body->stmts)
                    genStmt(*s);
                if (!builder_.GetInsertBlock()->getTerminator())
                    builder_.CreateBr(mergeBB);
            }

            if (sw->defaultArm) {
                builder_.SetInsertPoint(defaultBB);
                for (const auto& s : sw->defaultArm->stmts)
                    genStmt(*s);
                if (!builder_.GetInsertBlock()->getTerminator())
                    builder_.CreateBr(mergeBB);
            }

            builder_.SetInsertPoint(mergeBB);
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
                llvm::Value* fmt = builder_.CreateGlobalString("%lld\n", "fmt_int");
                return builder_.CreateCall(printf_, {fmt, arg});
            }

            llvm::Function* callee = module_->getFunction(call->callee);
            if (!callee)
                throw std::runtime_error("call to undeclared function '" + call->callee + "'");

            if (callee->arg_size() != call->args.size())
                throw std::runtime_error(
                    "function '" + call->callee + "' expects " +
                    std::to_string(callee->arg_size()) + " arguments, but got " +
                    std::to_string(call->args.size())
                );

            std::vector<llvm::Value*> args;
            for (const auto& arg : call->args)
                args.push_back(genExpr(*arg));
            
            return builder_.CreateCall(callee, args);
        }

        if (auto* id = dynamic_cast<const IdentExpr*>(&expr)) {
            auto it = symbols_.find(id->name);
            if (it == symbols_.end())
                throw std::runtime_error("use of undeclared variable '" + id->name + "'");

            return builder_.CreateLoad(llvm::Type::getInt64Ty(ctx_), it->second, id->name);
        }

        if (auto* bin = dynamic_cast<const BinaryExpr*>(&expr)) {
            llvm::Value* lhs = genExpr(*bin->lhs);
            llvm::Value* rhs = genExpr(*bin->rhs);

            if (bin->op == "+")
                return builder_.CreateAdd(lhs, rhs, "addtmp");

            if (bin->op == "*")
                return builder_.CreateMul(lhs, rhs, "multmp");
            
            if (bin->op == "-")
                return builder_.CreateSub(lhs, rhs, "subtmp");

            if (bin->op == "/")
                return builder_.CreateSDiv(lhs, rhs, "divtmp");
            
            if (bin->op == "%")
                return builder_.CreateSRem(lhs, rhs, "remtmp");
            
            if (bin->op == "==")
                return builder_.CreateIntCast(
                    builder_.CreateICmpEQ(lhs, rhs, "cmtmp"),
                    llvm::Type::getInt64Ty(ctx_),
                    false,
                    "booltmp"
                );

            if (bin->op == "!=")
                return builder_.CreateIntCast(
                    builder_.CreateICmpNE(lhs, rhs, "cmtmp"),
                    llvm::Type::getInt64Ty(ctx_),
                    false,
                    "booltmp"
                );

            if (bin->op == "<")
                return builder_.CreateIntCast(
                    builder_.CreateICmpSLT(lhs, rhs, "cmtmp"),
                    llvm::Type::getInt64Ty(ctx_),
                    false,
                    "booltmp"
                );

            if (bin->op == "<=")
                return builder_.CreateIntCast(
                    builder_.CreateICmpSLE(lhs, rhs, "cmtmp"),
                    llvm::Type::getInt64Ty(ctx_),
                    false,
                    "booltmp"
                );

            if (bin->op == ">")
                return builder_.CreateIntCast(
                    builder_.CreateICmpSGT(lhs, rhs, "cmtmp"),
                    llvm::Type::getInt64Ty(ctx_),
                    false,
                    "booltmp"
                );

            if (bin->op == ">=")
                return builder_.CreateIntCast(
                    builder_.CreateICmpSGE(lhs, rhs, "cmtmp"),
                    llvm::Type::getInt64Ty(ctx_),
                    false,
                    "booltmp"
                );

            if (bin->op == "&&") {
                llvm::Value* l = builder_.CreateICmpNE(lhs, llvm::ConstantInt::get(llvm::Type::getInt64Ty(ctx_), 0), "ltmp");
                llvm::Value* r = builder_.CreateICmpNE(rhs, llvm::ConstantInt::get(llvm::Type::getInt64Ty(ctx_), 0), "rtmp");

                return builder_.CreateIntCast(
                    builder_.CreateAnd(l, r, "andtmp"),
                    llvm::Type::getInt64Ty(ctx_),
                    false,
                    "booltmp"
                );
            }

            if (bin->op == "||") {
                llvm::Value* l = builder_.CreateICmpNE(lhs, llvm::ConstantInt::get(llvm::Type::getInt64Ty(ctx_), 0), "ltmp");
                llvm::Value* r = builder_.CreateICmpNE(rhs, llvm::ConstantInt::get(llvm::Type::getInt64Ty(ctx_), 0), "rtmp");

                return builder_.CreateIntCast(
                    builder_.CreateOr(l, r, "ortmp"),
                    llvm::Type::getInt64Ty(ctx_),
                    false,
                    "booltmp"
                );
            }


            throw std::runtime_error("codegen: unknown binary operator '" + bin->op + "'");
        }

        if (auto* un = dynamic_cast<const UnaryExpr*>(&expr)) {
            llvm::Value* operand = genExpr(*un->operand);

            if (un->op == "-")
                return builder_.CreateNeg(operand, "negtmp");

            if (un->op == "!") {
                llvm::Value* cmp = builder_.CreateICmpEQ(operand, llvm::ConstantInt::get(llvm::Type::getInt64Ty(ctx_), 0), "nottmp");
                return builder_.CreateIntCast(cmp, llvm::Type::getInt64Ty(ctx_), false, "booltmp");
            }

            throw std::runtime_error("codegen: unknown unary operator '" + un->op + "'");
        }

        throw std::runtime_error("codegen: unknown expression type");
    }
}
