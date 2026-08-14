#include "CodeGen.h"

#include <stdexcept>
#include <llvm/IR/Type.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/DerivedTypes.h>


namespace ZCompiler {
    CodeGen::CodeGen(const std::string& moduleName) : module_(std::make_unique<llvm::Module>(moduleName, ctx_)), builder_(ctx_) {}

    // Public method implementations
    void CodeGen::generate(const Program& program) {
        declarePrintf();

        for (const auto& decl : program.decls) {
            if (auto* fn = dynamic_cast<const FnDecl*>(decl.get()))
                genFnDecl(*fn);
        }   
    }

    // Private Method implementations
    std::unique_ptr<llvm::Module> CodeGen::takeModule() {
        return std::move(module_);
    }

    static llvm::AllocaInst* createEntryBlockAlloca(llvm::Function* fn, const std::string& name, llvm::Type* ty) {
        llvm::IRBuilder<> tmp(&fn->getEntryBlock(), fn->getEntryBlock().begin());
        return tmp.CreateAlloca(ty, nullptr, name);
    }

    // Scope helpers — these mirror Sema's push/pop discipline exactly. If the two
    void CodeGen::pushScope() {
        scopes_.emplace_back();
    }

    void CodeGen::popScope() {
        scopes_.pop_back();
    }

    void CodeGen::declareVar(const std::string& name, llvm::AllocaInst* slot) {
        if (scopes_.empty())
            scopes_.emplace_back();

        scopes_.back()[name] = slot;
    }

    llvm::AllocaInst* CodeGen::lookupVar(const std::string& name) const {
        for (auto it = scopes_.rbegin(); it != scopes_.rend(); ++it) {
            auto found = it->find(name);

            if (found != it->end())
                return found->second;
        }

        return nullptr;
    }

    llvm::Type* CodeGen::llvmType(TypeRef t) {
        switch (t) {
            case TypeRef::Bool:
                return llvm::Type::getInt1Ty(ctx_);
            case TypeRef::Int32:
            case TypeRef::Char:
                return llvm::Type::getInt32Ty(ctx_);
            case TypeRef::Int:
            case TypeRef::Int64:
                return llvm::Type::getInt64Ty(ctx_);
            case TypeRef::Int128:
                return llvm::Type::getInt128Ty(ctx_);
            case TypeRef::Float16:
                return llvm::Type::getHalfTy(ctx_);
            case TypeRef::Float:
            case TypeRef::Float32:
            return llvm::Type::getFloatTy(ctx_);
            case TypeRef::Double:
            case TypeRef::Float64:
                return llvm::Type::getDoubleTy(ctx_);
            case TypeRef::String:
                return llvm::PointerType::get(ctx_, 0);
            default:
                throw std::runtime_error("codegen: unknown type");
        }
    }

    llvm::Value* CodeGen::genExpr(const Expr& expr) {
        if (auto* lit = dynamic_cast<const IntLitExpr*>(&expr)) {
            return llvm::ConstantInt::get(llvm::Type::getInt64Ty(ctx_), lit->value, true);
        }

        if (auto* lit = dynamic_cast<const FloatLitExpr*>(&expr)) {
            return llvm::ConstantFP::get(llvm::Type::getFloatTy(ctx_), lit->value);
        }

        if (auto* lit = dynamic_cast<const DoubleLitExpr*>(&expr)) {
            return llvm::ConstantFP::get(llvm::Type::getDoubleTy(ctx_), lit->value);
        }

        if (auto* lit = dynamic_cast<const BoolLitExpr*>(&expr)) {
            return llvm::ConstantInt::get(llvm::Type::getInt1Ty(ctx_), lit->value);
        }

        if (auto* lit = dynamic_cast<const CharLitExpr*>(&expr)) {
            return llvm::ConstantInt::get(llvm::Type::getInt32Ty(ctx_), lit->value);
        }

        if (auto* lit = dynamic_cast<const StringLitExpr*>(&expr)) {
            return builder_.CreateGlobalString(lit->value, "str");
        }

        if (auto* cast = dynamic_cast<const CastExpr*>(&expr)) {
            llvm::Value* src = genExpr(*cast->operand);

            llvm::Type* from = src->getType();
            llvm::Type* to = llvmType(cast->targetType);

            return coerce(src, from, to);
        }

        if (auto* call = dynamic_cast<const CallExpr*>(&expr)) {
            if (call->callee == "print")
                return genPrint(*call);

            llvm::Function* callee = module_->getFunction(call->callee);
            if (!callee)
                throw std::runtime_error("call to undeclared function '" + call->callee + "'");

            if (callee->arg_size() != call->args.size())
                throw std::runtime_error("function '" + call->callee + "' expects " + std::to_string(callee->arg_size()) + " arguments, but got " + std::to_string(call->args.size()) + ")");

            std::vector<llvm::Value*> args;
            for (const auto& arg : call->args)
                args.push_back(genExpr(*arg));
    
            return builder_.CreateCall(callee, args);
        }

        if (auto* id = dynamic_cast<const IdentExpr*>(&expr)) {
            llvm::AllocaInst* slot = lookupVar(id->name);
            if (!slot)
                throw std::runtime_error("use of undeclared variable '" + id->name + "'");

            return builder_.CreateLoad(slot->getAllocatedType(), slot, id->name);
        }

        if (auto* bin = dynamic_cast<const BinaryExpr*>(&expr)) {
            // Must be intercepted before either operand is generated: the whole
            // point is that the right-hand side is not always evaluated.
            if (bin->op == "&&" || bin->op == "||")
                return genShortCircuit(*bin);

            llvm::Value* lhs = genExpr(*bin->lhs);
            llvm::Value* rhs = genExpr(*bin->rhs);

            llvm::Type* LhsType = lhs->getType();
            llvm::Type* RhsType = rhs->getType();

            if (LhsType != RhsType) {
                llvm::Type* common = nullptr;
                
                if (LhsType->isIntegerTy() && RhsType->isIntegerTy())
                    common = LhsType->getIntegerBitWidth() >= RhsType->getIntegerBitWidth() ? LhsType : RhsType;
                else if (LhsType->isFloatingPointTy() && RhsType->isFloatingPointTy())
                    common = LhsType->getPrimitiveSizeInBits() >= RhsType->getPrimitiveSizeInBits() ? LhsType : RhsType;
                else
                    common = llvm::Type::getDoubleTy(ctx_);
                
                lhs = coerce(lhs, LhsType, common);
                rhs = coerce(rhs, RhsType, common);
            }

            bool isFloatOp = lhs->getType()->isFloatingPointTy();

            if (bin->op == "+")
                return isFloatOp ? builder_.CreateFAdd(lhs, rhs, "addtmp") : builder_.CreateAdd(lhs, rhs, "addtmp");
            if (bin->op == "*")
                return isFloatOp ? builder_.CreateFMul(lhs, rhs, "multmp") : builder_.CreateMul(lhs, rhs, "multmp");
            if (bin->op == "-")
                return isFloatOp ? builder_.CreateFSub(lhs, rhs, "subtmp") : builder_.CreateSub(lhs, rhs, "subtmp");
            if (bin->op == "/")
                return isFloatOp ? builder_.CreateFDiv(lhs, rhs, "divtmp") : builder_.CreateSDiv(lhs, rhs, "divtmp");
            if (bin->op == "%")
                return builder_.CreateSRem(lhs, rhs, "remtmp");

            if (bin->op == "==") 
                return isFloatOp ? builder_.CreateFCmpOEQ(lhs,rhs) : builder_.CreateICmpEQ(lhs,rhs);
            if (bin->op == "!=") 
                return isFloatOp ? builder_.CreateFCmpONE(lhs,rhs) : builder_.CreateICmpNE(lhs,rhs);
            if (bin->op == "<")  
                return isFloatOp ? builder_.CreateFCmpOLT(lhs,rhs) : builder_.CreateICmpSLT(lhs,rhs);
            if (bin->op == "<=") 
                return isFloatOp ? builder_.CreateFCmpOLE(lhs,rhs) : builder_.CreateICmpSLE(lhs,rhs);
            if (bin->op == ">")  
                return isFloatOp ? builder_.CreateFCmpOGT(lhs,rhs) : builder_.CreateICmpSGT(lhs,rhs);
            if (bin->op == ">=") 
                return isFloatOp ? builder_.CreateFCmpOGE(lhs,rhs) : builder_.CreateICmpSGE(lhs,rhs);

            // `&&` and `||` never reach here — genExpr routes them to
            // genShortCircuit before the operands are evaluated.
            throw std::runtime_error("codegen: unknown binary operator '" + bin->op + "'");
        }

        if (auto* un = dynamic_cast<const UnaryExpr*>(&expr)) {
            llvm::Value* operand = genExpr(*un->operand);

            if (un->op == "-") {
                if (operand->getType()->isFloatingPointTy())
                    return builder_.CreateFNeg(operand, "negtmp");
                return builder_.CreateNeg(operand, "negtmp");
            }

            if (un->op == "!") {
                return builder_.CreateNot(toBoolValue(operand), "nottmp");
            }

            throw std::runtime_error("codegen: unknown unary operator '" + un->op + "'");
        }

        throw std::runtime_error("codegen: unknown expression type");
    }

    llvm::Value* CodeGen::genPrint(const CallExpr& call) {
        if (call.args.empty())
            throw std::runtime_error("print() requires one argument");

        const Expr& argExpr = *call.args[0];
        llvm::Value* arg = genExpr(argExpr);

        const char* fmt = nullptr;

        switch (argExpr.resolvedType) {
            case TypeRef::Bool:
                fmt = "%d\n";
                arg = builder_.CreateZExt(arg, llvm::Type::getInt32Ty(ctx_));
                break;

            case TypeRef::Char:
                fmt = "%c\n";
                break;

            case TypeRef::Int32:
                fmt = "%d\n";
                break;

            case TypeRef::Int:
            case TypeRef::Int64:
                fmt = "%lld\n";
                break;

            case TypeRef::Int128:
                fmt = "%lld\n";
                arg = builder_.CreateTrunc(arg, llvm::Type::getInt64Ty(ctx_));
                break;

            case TypeRef::Float16:
            case TypeRef::Float:
            case TypeRef::Float32:
                fmt = "%f\n";
                arg = builder_.CreateFPExt(arg, llvm::Type::getDoubleTy(ctx_));
                break;

            case TypeRef::Double:
            case TypeRef::Float64:
                fmt = "%lf\n";
                break;

            case TypeRef::String:
                fmt = "%s\n";
                break;

            default:
                throw std::runtime_error("codegen: print() does not support this type");
        }

        llvm::Value* fmtStr = builder_.CreateGlobalString(fmt, "fmt");
        return builder_.CreateCall(printf_, {fmtStr, arg});
    }

    llvm::Value* CodeGen::genShortCircuit(const BinaryExpr& bin) {
        const bool isAnd = (bin.op == "&&");

        llvm::Function* fn = builder_.GetInsertBlock()->getParent();
        llvm::Value* lhsBool = toBoolValue(genExpr(*bin.lhs));
        llvm::BasicBlock* lhsEnd = builder_.GetInsertBlock();

        llvm::BasicBlock* rhsBB   = llvm::BasicBlock::Create(ctx_, isAnd ? "and.rhs" : "or.rhs", fn);
        llvm::BasicBlock* mergeBB = llvm::BasicBlock::Create(ctx_, isAnd ? "and.end" : "or.end", fn);

        if (isAnd)
            builder_.CreateCondBr(lhsBool, rhsBB, mergeBB);
        else
            builder_.CreateCondBr(lhsBool, mergeBB, rhsBB);

        builder_.SetInsertPoint(rhsBB);
        llvm::Value* rhsBool = toBoolValue(genExpr(*bin.rhs));
        llvm::BasicBlock* rhsEnd = builder_.GetInsertBlock();

        builder_.CreateBr(mergeBB);
        builder_.SetInsertPoint(mergeBB);

        llvm::Type* boolTy = llvm::Type::getInt1Ty(ctx_);
        llvm::PHINode* phi = builder_.CreatePHI(boolTy, 2, isAnd ? "andtmp" : "ortmp");

        phi->addIncoming(llvm::ConstantInt::get(boolTy, isAnd ? 0 : 1), lhsEnd);
        phi->addIncoming(rhsBool, rhsEnd);
        return phi;
    }

    llvm::Value* CodeGen::toBoolValue(llvm::Value* v) {
        llvm::Type* ty = v->getType();

        if (ty->isIntegerTy(1))   
            return v;

        if (ty->isIntegerTy())
            return builder_.CreateICmpNE(v, llvm::ConstantInt::get(ty, 0), "tobool");
        
        if (ty->isFloatingPointTy())
            return builder_.CreateFCmpONE(v, llvm::ConstantFP::get(ty, 0.0), "tobool");
        
        return builder_.CreateICmpNE(v, llvm::ConstantInt::get(ty, 0), "tobool");
    }

    llvm::Value* CodeGen::coerce(llvm::Value* v, llvm::Type* from, llvm::Type* to) {
        if (from == to) 
            return v;

        bool fi = from->isIntegerTy(), ti = to->isIntegerTy();
        bool ff = from->isFloatingPointTy(), tf = to->isFloatingPointTy();

        if (fi && ti) {
            return from->getIntegerBitWidth() < to->getIntegerBitWidth() ? builder_.CreateSExt(v, to) : builder_.CreateTrunc(v, to);
        }

        if (ff && tf) {
            return from->getPrimitiveSizeInBits() < to->getPrimitiveSizeInBits() ? builder_.CreateFPExt(v, to) : builder_.CreateFPTrunc(v, to);
        }

        if (fi && tf) 
            return builder_.CreateSIToFP(v, to);
        if (ff && ti) 
            return builder_.CreateFPToSI(v, to);

        if (from->isIntegerTy(1) && ti) 
            return builder_.CreateZExt(v, to);

        return v;
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

    void CodeGen::startDeadBlock() {
        llvm::Function *fn = builder_.GetInsertBlock()->getParent();
        builder_.SetInsertPoint(llvm::BasicBlock::Create(ctx_, "dead", fn));
    }

    void CodeGen::genStmt(const Stmt& stmt) {
        if (auto* ret = dynamic_cast<const ReturnStmt*>(&stmt)) {
            llvm::Value* val = genExpr(*ret->value);
            llvm::Type* retTy = builder_.GetInsertBlock()->getParent()->getReturnType();

            if (val->getType() != retTy)
                val = coerce(val, val->getType(), retTy);

            builder_.CreateRet(val);
            return;
        }
    
        if (auto* es = dynamic_cast<const ExprStmt*>(&stmt)) {
            genExpr(*es->expr);
            return;
        }

        if (auto* let = dynamic_cast<const LetStmt*>(&stmt)) {
            llvm::Function* fn = builder_.GetInsertBlock()->getParent();

            llvm::Type* allocaTy = llvmType(let->type);
            llvm::AllocaInst* alloca = createEntryBlockAlloca(fn, let->name, allocaTy);
            declareVar(let->name, alloca);

            llvm::Value* initVal = genExpr(*let->init);
            if (initVal->getType() != allocaTy)
                initVal = coerce(initVal, initVal->getType(), allocaTy);

            builder_.CreateStore(initVal, alloca);
            return;
        }

        if (auto* asgn = dynamic_cast<const AssignStmt*>(&stmt)) {
            llvm::AllocaInst* slot = lookupVar(asgn->name);
            if (!slot)
                throw std::runtime_error("assignment to undeclared variable '" + asgn->name + "'");

            llvm::Type* varTy = slot->getAllocatedType();
            llvm::Value* val = genExpr(*asgn->value);

            if (val->getType() != varTy)
                val = coerce(val, val->getType(), varTy);

            builder_.CreateStore(val, slot);
            return;
        }

        if (auto* ifs = dynamic_cast<const IfStmt*>(&stmt)) {
            llvm::Function* fn = builder_.GetInsertBlock()->getParent();

            llvm::Value* condBool = toBoolValue(genExpr(*ifs->cond));

            llvm::BasicBlock* thenBB = llvm::BasicBlock::Create(ctx_, "then", fn);
            llvm::BasicBlock* elseBB = llvm::BasicBlock::Create(ctx_, "else");
            llvm::BasicBlock* mergeBB = llvm::BasicBlock::Create(ctx_, "merge");

            builder_.CreateCondBr(condBool, thenBB, elseBB);

            builder_.SetInsertPoint(thenBB);
            pushScope();
            for (const auto& stmt : ifs->thenBranch->stmts)
                genStmt(*stmt);
            popScope();

            if (!builder_.GetInsertBlock()->getTerminator())
                builder_.CreateBr(mergeBB);

            elseBB->insertInto(fn);
            builder_.SetInsertPoint(elseBB);
            if (ifs->elseBranch) {
                pushScope();
                for (const auto& stmt : ifs->elseBranch->stmts)
                    genStmt(*stmt);
                popScope();
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
            llvm::BasicBlock* bodyBB = llvm::BasicBlock::Create(ctx_, "while.body", fn);
            llvm::BasicBlock* exitBB = llvm::BasicBlock::Create(ctx_, "while.exit", fn);

            builder_.CreateBr(headerBB);
            builder_.SetInsertPoint(headerBB);

            llvm::Value* cond = genExpr(*whl->cond);
            llvm::Value* condBool = toBoolValue(cond);

            builder_.CreateCondBr(condBool, bodyBB, exitBB);

            builder_.SetInsertPoint(bodyBB);

            pushScope();
            breakTargets_.push_back(exitBB);
            continueTargets_.push_back(headerBB);

            for (const auto& s : whl->body->stmts)
                genStmt(*s);

            continueTargets_.pop_back();
            breakTargets_.pop_back();
            popScope();

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

            builder_.CreateBr(bodyBB);
            builder_.SetInsertPoint(bodyBB);

            pushScope();
            breakTargets_.push_back(exitBB);
            continueTargets_.push_back(condBB);

            for (const auto& s : ds->body->stmts)
                genStmt(*s);
            
            continueTargets_.pop_back();
            breakTargets_.pop_back();
            popScope();

            if (!builder_.GetInsertBlock()->getTerminator())
                builder_.CreateBr(condBB);

            builder_.SetInsertPoint(condBB);
            llvm::Value* cond = genExpr(*ds->cond);
            llvm::Value* condBool = toBoolValue(cond);

            builder_.CreateCondBr(condBool, bodyBB, exitBB);
            builder_.SetInsertPoint(exitBB);
            return;
        }

        if (auto* fs = dynamic_cast<const ForStmt*>(&stmt)) {
            llvm::Function* fn = builder_.GetInsertBlock()->getParent();

            pushScope();
            genStmt(*fs->init);

            llvm::BasicBlock* headerBB = llvm::BasicBlock::Create(ctx_, "for.cond", fn);
            llvm::BasicBlock* bodyBB   = llvm::BasicBlock::Create(ctx_, "for.body", fn);
            llvm::BasicBlock* stepBB   = llvm::BasicBlock::Create(ctx_, "for.step", fn);
            llvm::BasicBlock* exitBB   = llvm::BasicBlock::Create(ctx_, "for.exit", fn);

            builder_.CreateBr(headerBB);

            builder_.SetInsertPoint(headerBB);
            llvm::Value* cond = genExpr(*fs->cond);
            llvm::Value* condBool = toBoolValue(cond);

            builder_.CreateCondBr(condBool, bodyBB, exitBB);
            builder_.SetInsertPoint(bodyBB);
            breakTargets_.push_back(exitBB);
            continueTargets_.push_back(stepBB);

            for (const auto& s : fs->body->stmts)
                genStmt(*s);
            
            if (!builder_.GetInsertBlock()->getTerminator())
                builder_.CreateBr(stepBB);

            builder_.SetInsertPoint(stepBB);
            genStmt(*fs->step);
            builder_.CreateBr(headerBB);

            builder_.SetInsertPoint(exitBB);
            continueTargets_.pop_back();
            breakTargets_.pop_back();

            popScope();
            return;
        }

        if (dynamic_cast<const BreakStmt*>(&stmt)) {
            if (breakTargets_.empty())
                throw std::runtime_error("break statement not within a loop or switch");

            builder_.CreateBr(breakTargets_.back());
            startDeadBlock();
            return;
        }

        if (dynamic_cast<const ContinueStmt*>(&stmt)) {
            if (continueTargets_.empty())
                throw std::runtime_error("continue statement not within a loop");

            builder_.CreateBr(continueTargets_.back());
            startDeadBlock();
            return;
        }

        if (auto* sw = dynamic_cast<const SwitchStmt*>(&stmt)) {
            llvm::Function* fn = builder_.GetInsertBlock()->getParent();
            llvm::Value* scrutinee = genExpr(*sw->scrutinee);

            llvm::BasicBlock* mergeBB = llvm::BasicBlock::Create(ctx_, "switch.end", fn);
            llvm::BasicBlock* defaultBB = sw->defaultArm ? llvm::BasicBlock::Create(ctx_, "switch.default", fn) : mergeBB;

            auto* scrutineeTy = llvm::dyn_cast<llvm::IntegerType>(scrutinee->getType());
            if (!scrutineeTy)
                throw std::runtime_error("codegen: switch scrutinee must be an integer");

            llvm::SwitchInst* sw_inst = builder_.CreateSwitch(scrutinee, defaultBB, sw->cases.size());
            breakTargets_.push_back(mergeBB);

            for (const auto& arm : sw->cases) {
                llvm::BasicBlock* caseBB = llvm::BasicBlock::Create(ctx_, "switch.case", fn);

                sw_inst->addCase(llvm::ConstantInt::get(scrutineeTy, arm.constValue, true), caseBB);
                builder_.SetInsertPoint(caseBB);

                pushScope();
                for (const auto& s : arm.body->stmts)
                    genStmt(*s);
                popScope();

                if (!builder_.GetInsertBlock()->getTerminator())
                    builder_.CreateBr(mergeBB);
            }

            if (sw->defaultArm) {
                builder_.SetInsertPoint(defaultBB);

                pushScope();
                for (const auto& s : sw->defaultArm->stmts)
                    genStmt(*s);
                popScope();

                if (!builder_.GetInsertBlock()->getTerminator())
                    builder_.CreateBr(mergeBB);
            }

            breakTargets_.pop_back();
            builder_.SetInsertPoint(mergeBB);
            return;
        }

        throw std::runtime_error("codegen: unknown statement type");
    }

    void CodeGen::genFnDecl(const FnDecl& fn) {
        scopes_.clear();
        pushScope();

        std::vector<llvm::Type*> paramTypes;
        for (const auto& p : fn.params)
            paramTypes.push_back(llvmType(p.type));

        llvm::Type* retTy = (fn.name == "main") ? llvm::Type::getInt32Ty(ctx_) : llvmType(fn.returnType);

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
            llvm::AllocaInst* alloca = createEntryBlockAlloca(func, fn.params[i].name, llvmType(fn.params[i].type));

            builder_.CreateStore(&arg, alloca);
            declareVar(fn.params[i].name, alloca);
            i++;
        }

        for (const auto& stmt : fn.body->stmts)
            genStmt(*stmt);

        // Sema guarantees every path returns, so this only terminates the unreachable
        // block left behind by a trailing `break` or `continue`.
        if (!builder_.GetInsertBlock()->getTerminator())
            builder_.CreateUnreachable();

        popScope();
    }
}
