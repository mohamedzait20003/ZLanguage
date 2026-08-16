#include "CodeGen.h"
#include "Mangler.h"

#include <functional>

#include <stdexcept>
#include <llvm/IR/Type.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/DerivedTypes.h>


namespace ZCompiler {
    CodeGen::CodeGen(const std::string& moduleName) : module_(std::make_unique<llvm::Module>(moduleName, ctx_)), builder_(ctx_) {}

    // Visits every function in a namespace tree, at any nesting depth.
    static void walkNamespace(const NamespaceDecl& ns, const std::function<void(const FnDecl&)>& visit) {
        for (const auto& member : ns.decls) {
            if (auto* nested = dynamic_cast<const NamespaceDecl*>(member.get())) {
                walkNamespace(*nested, visit);
                continue;
            }

            if (auto* fn = dynamic_cast<const FnDecl*>(member.get()))
                visit(*fn);
        }
    }

    // Public method implementations
    void CodeGen::generate(const Program& program) {
        declarePrintf();

        auto record = [&](const FnDecl& fn) {
            FnSignature sig;
            sig.returnType = fn.returnType;

            for (const auto& p : fn.params)
                sig.paramTypes.push_back(p.type);

            signatures_[fn.isExtern ? fn.name : mangleFunction(fn.owner, fn.name)] = std::move(sig);
        };

        // Constants are folded at each use, so they only need to be findable —
        // nothing is emitted for them.
        std::function<void(const std::vector<DeclPtr>&)> collectConsts =
            [&](const std::vector<DeclPtr>& decls) {
                for (const auto& decl : decls) {
                    if (auto* ns = dynamic_cast<const NamespaceDecl*>(decl.get())) {
                        collectConsts(ns->decls);
                        continue;
                    }

                    if (auto* konst = dynamic_cast<const ConstDecl*>(decl.get()))
                        constants_[mangleFunction(konst->owner, konst->name)] = konst;
                }
            };
        collectConsts(program.decls);

        auto forEachFn = [&](const std::function<void(const FnDecl&)>& visit) {
            for (const auto& decl : program.decls) {
                if (auto* fn = dynamic_cast<const FnDecl*>(decl.get())) {
                    visit(*fn);
                    continue;
                }

                if (auto* ns = dynamic_cast<const NamespaceDecl*>(decl.get()))
                    walkNamespace(*ns, visit);
            }
        };

        // Three passes, because an LLVM function must exist before anything can
        // call it. Emitting each body as its declaration is created would break
        // any forward reference — a function calling one defined later in the
        // file, mutual recursion, or a user program calling into the stdlib,
        // which is appended after the user's own declarations.
        forEachFn(record);
        forEachFn([&](const FnDecl& fn) { declareFnDecl(fn); });
        forEachFn([&](const FnDecl& fn) { defineFnDecl(fn); });
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

    void CodeGen::declareVar(const std::string& name, llvm::AllocaInst* slot, TypeRef type) {
        if (scopes_.empty())
            scopes_.emplace_back();

        scopes_.back()[name] = VarBinding{slot, type};
    }

    const CodeGen::VarBinding* CodeGen::lookupVar(const std::string& name) const {
        for (auto it = scopes_.rbegin(); it != scopes_.rend(); ++it) {
            auto found = it->find(name);

            if (found != it->end())
                return &found->second;
        }

        return nullptr;
    }

    llvm::StructType* CodeGen::zstringType() {
        if (auto* existing = llvm::StructType::getTypeByName(ctx_, "ZString"))
            return existing;

        llvm::Type* ptrTy = llvm::PointerType::get(ctx_, 0);
        llvm::Type* i32Ty = llvm::Type::getInt32Ty(ctx_);
        llvm::Type* i64Ty = llvm::Type::getInt64Ty(ctx_);

        auto* header = llvm::StructType::create(ctx_, {ptrTy, i32Ty, i32Ty}, "ZGCHeader");
        return llvm::StructType::create(ctx_, {header, i64Ty}, "ZString");
    }

    llvm::Function* CodeGen::runtimeFn(llvm::Function*& cache, const char* name, llvm::Type* ret, llvm::ArrayRef<llvm::Type*> params) {
        if (cache)
            return cache;

        // The module may already declare this symbol: an `extern fn` in Z can
        // name the same runtime entry point an intrinsic like `print` uses.
        // Creating a second one makes LLVM rename it to `name.1`, which then has
        // no definition to link against.
        if (llvm::Function* existing = module_->getFunction(name)) {
            cache = existing;
            return cache;
        }

        llvm::FunctionType* ft = llvm::FunctionType::get(ret, params, false);
        cache = llvm::Function::Create(ft, llvm::Function::ExternalLinkage, name, module_.get());
        return cache;
    }

    llvm::Type* CodeGen::llvmType(TypeRef t) {
        switch (t) {
            case TypeRef::Bool:
                return llvm::Type::getInt1Ty(ctx_);
            case TypeRef::Int32:
            case TypeRef::Character:
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
            case TypeRef::Dynamic:
            case TypeRef::Null:
                return llvm::PointerType::get(ctx_, 0);
            default:
                throw std::runtime_error("codegen: unknown type");
        }
    }

    int CodeGen::dynamicTagFor(TypeRef type) {
        switch (type) {
            case TypeRef::Int:
            case TypeRef::Int32:
            case TypeRef::Int64:
            case TypeRef::Int128:
                return 1;   // DYN_INT
            case TypeRef::Float:
            case TypeRef::Float32:
            case TypeRef::Float16:
                return 2;   // DYN_FLOAT
            case TypeRef::Double:
            case TypeRef::Float64:
                return 3;   // DYN_DOUBLE
            case TypeRef::Bool:
                return 4;   // DYN_BOOL
            case TypeRef::Character:
                return 5;   // DYN_CHAR
            case TypeRef::String:
                return 6;   // DYN_STRING
            default:
                throw std::runtime_error("codegen: type cannot be stored in a dynamic");
        }
    }

    // Emits a string literal as a private global ZString and returns a pointer
    // to it. Literals are interned by value, so repeated text costs one global.
    llvm::Constant* CodeGen::internString(const std::string& value) {
        auto found = stringLiterals_.find(value);
        if (found != stringLiterals_.end())
            return found->second;

        llvm::Type* ptrTy = llvm::PointerType::get(ctx_, 0);
        llvm::Type* i32Ty = llvm::Type::getInt32Ty(ctx_);
        llvm::Type* i64Ty = llvm::Type::getInt64Ty(ctx_);

        auto* headerTy = llvm::StructType::getTypeByName(ctx_, "ZGCHeader");
        if (!headerTy) {
            zstringType();
            headerTy = llvm::StructType::getTypeByName(ctx_, "ZGCHeader");
        }

        llvm::ArrayType* bytesTy = llvm::ArrayType::get(llvm::Type::getInt8Ty(ctx_), value.size() + 1);
        llvm::StructType* litTy = llvm::StructType::get(ctx_, {headerTy, i64Ty, bytesTy});

        llvm::Constant* header = llvm::ConstantStruct::get(
            headerTy,
            {llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(ptrTy)),
             llvm::ConstantInt::get(i32Ty, 0x7FFFFFFF),
             llvm::ConstantInt::get(i32Ty, 0)});

        llvm::Constant* init = llvm::ConstantStruct::get(
            litTy,
            {header,
             llvm::ConstantInt::get(i64Ty, static_cast<uint64_t>(value.size())),
             llvm::ConstantDataArray::getString(ctx_, value, true)});

        auto* global = new llvm::GlobalVariable(*module_, litTy, true, llvm::GlobalValue::PrivateLinkage, init, "zstr");
        global->setAlignment(llvm::MaybeAlign(8));

        stringLiterals_[value] = global;
        return global;
    }

    llvm::Value* CodeGen::boxToDynamic(llvm::Value* v, TypeRef from) {
        llvm::Type* i32Ty = llvm::Type::getInt32Ty(ctx_);
        llvm::Type* i64Ty = llvm::Type::getInt64Ty(ctx_);
        llvm::Type* ptrTy = llvm::PointerType::get(ctx_, 0);
        llvm::Value* payload = nullptr;

        switch (from) {
            case TypeRef::Float:
            case TypeRef::Float32:
                payload = builder_.CreateZExt(builder_.CreateBitCast(v, i32Ty), i64Ty);
                break;

            case TypeRef::Float16:
                payload = builder_.CreateZExt(
                    builder_.CreateBitCast(builder_.CreateFPExt(v, llvm::Type::getFloatTy(ctx_)), i32Ty),
                    i64Ty);
                break;

            case TypeRef::Double:
            case TypeRef::Float64:
                payload = builder_.CreateBitCast(v, i64Ty);
                break;

            case TypeRef::String:
                payload = builder_.CreatePtrToInt(v, i64Ty);
                break;

            case TypeRef::Bool:
                payload = builder_.CreateZExt(v, i64Ty);
                break;

            default:
                payload = builder_.CreateSExt(v, i64Ty);
                break;
        }

        llvm::Function* box = runtimeFn(dynamicBox_, "z_dynamic_box", ptrTy, {i32Ty, i64Ty});
        return builder_.CreateCall(
            box, {llvm::ConstantInt::get(i32Ty, dynamicTagFor(from)), payload});
    }

    llvm::Value* CodeGen::unboxFromDynamic(llvm::Value* dyn, TypeRef to, bool safe) {
        llvm::Type* i32Ty = llvm::Type::getInt32Ty(ctx_);
        llvm::Type* i64Ty = llvm::Type::getInt64Ty(ctx_);
        llvm::Type* ptrTy = llvm::PointerType::get(ctx_, 0);

        llvm::Function* fn = safe ? runtimeFn(dynamicTryUnbox_, "z_dynamic_try_unbox", i64Ty, {i32Ty, ptrTy}) : runtimeFn(dynamicUnbox_, "z_dynamic_unbox", i64Ty, {i32Ty, ptrTy});

        llvm::Value* raw = builder_.CreateCall(fn, {llvm::ConstantInt::get(i32Ty, dynamicTagFor(to)), dyn});

        switch (to) {
            case TypeRef::Float:
            case TypeRef::Float32:
                return builder_.CreateBitCast(builder_.CreateTrunc(raw, i32Ty), llvm::Type::getFloatTy(ctx_));

            case TypeRef::Float16:
                return builder_.CreateFPTrunc(
                    builder_.CreateBitCast(builder_.CreateTrunc(raw, i32Ty), llvm::Type::getFloatTy(ctx_)),
                    llvm::Type::getHalfTy(ctx_));

            case TypeRef::Double:
            case TypeRef::Float64:
                return builder_.CreateBitCast(raw, llvm::Type::getDoubleTy(ctx_));

            case TypeRef::String:
                return builder_.CreateIntToPtr(raw, ptrTy);

            case TypeRef::Bool:
                return builder_.CreateICmpNE(raw, llvm::ConstantInt::get(i64Ty, 0));

            default:
                return coerce(raw, i64Ty, llvmType(to));
        }
    }

    llvm::Value* CodeGen::convert(llvm::Value* val, TypeRef from, TypeRef to) {
        if (from == to)
            return val;

        if (to == TypeRef::Dynamic && from != TypeRef::Dynamic) {
            if (from == TypeRef::Null)
                return llvm::ConstantPointerNull::get(llvm::PointerType::get(ctx_, 0));

            return boxToDynamic(val, from);
        }

        if (from == TypeRef::Dynamic && to != TypeRef::Dynamic)
            return unboxFromDynamic(val, to, false);

        if (from == TypeRef::Null)
            return llvm::ConstantPointerNull::get(llvm::PointerType::get(ctx_, 0));

        return coerce(val, val->getType(), llvmType(to));
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
            return internString(lit->value);
        }

        if (dynamic_cast<const NullLitExpr*>(&expr)) {
            return llvm::ConstantPointerNull::get(llvm::PointerType::get(ctx_, 0));
        }

        if (auto* tern = dynamic_cast<const TernaryExpr*>(&expr)) {
            return genTernary(*tern);
        }

        if (auto* cast = dynamic_cast<const CastExpr*>(&expr)) {
            llvm::Value* src = genExpr(*cast->operand);
            return convert(src, cast->operand->resolvedType, cast->targetType);
        }

        if (auto* cast = dynamic_cast<const DynCastExpr*>(&expr)) {
            llvm::Value* src = genExpr(*cast->operand);
            return unboxFromDynamic(src, cast->targetType, true);
        }

        if (auto* call = dynamic_cast<const CallExpr*>(&expr)) {
            if (call->callee == "print")
                return genPrint(*call);

            // An extern function is registered under its literal C name, so try
            // the mangled symbol first and fall back to the bare one.
            std::string symbol = mangleFunction(call->qualifier, call->callee);

            llvm::Function* callee = module_->getFunction(symbol);
            if (!callee && module_->getFunction(call->callee)) {
                symbol = call->callee;
                callee = module_->getFunction(symbol);
            }

            if (!callee)
                throw std::runtime_error("call to undeclared function '" + symbol + "'");

            if (callee->arg_size() != call->args.size())
                throw std::runtime_error("function '" + call->callee + "' expects " + std::to_string(callee->arg_size()) + " arguments, but got " + std::to_string(call->args.size()) + ")");

            auto sig = signatures_.find(symbol);

            std::vector<llvm::Value*> args;
            for (std::size_t i = 0; i < call->args.size(); ++i) {
                llvm::Value* v = genExpr(*call->args[i]);

                if (sig != signatures_.end() && i < sig->second.paramTypes.size())
                    v = convert(v, call->args[i]->resolvedType, sig->second.paramTypes[i]);

                args.push_back(v);
            }

            return builder_.CreateCall(callee, args);
        }

        if (auto* id = dynamic_cast<const IdentExpr*>(&expr)) {
            if (!id->qualifier.empty() || !lookupVar(id->name)) {
                auto konst = constants_.find(mangleFunction(id->qualifier, id->name));

                if (konst != constants_.end())
                    return convert(genExpr(*konst->second->value), konst->second->value->resolvedType, id->resolvedType);
            }

            const VarBinding* binding = lookupVar(id->name);
            if (!binding)
                throw std::runtime_error("use of undeclared variable '" + id->name + "'");

            return builder_.CreateLoad(binding->slot->getAllocatedType(), binding->slot, id->name);
        }

        if (auto* bin = dynamic_cast<const BinaryExpr*>(&expr)) {
            if (bin->op == "&&" || bin->op == "||")
                return genShortCircuit(*bin);

            llvm::Value* lhs = genExpr(*bin->lhs);
            llvm::Value* rhs = genExpr(*bin->rhs);

            const TypeRef lhsZ = bin->lhs->resolvedType;
            const TypeRef rhsZ = bin->rhs->resolvedType;

            if ((bin->op == "==" || bin->op == "!=") &&
                (lhsZ == TypeRef::Null || rhsZ == TypeRef::Null)) {
                llvm::Value* ptr = lhsZ == TypeRef::Null ? rhs : lhs;
                llvm::Value* null = llvm::ConstantPointerNull::get(llvm::PointerType::get(ctx_, 0));

                return bin->op == "==" ? builder_.CreateICmpEQ(ptr, null, "isnull") : builder_.CreateICmpNE(ptr, null, "notnull");
            }

            if (lhsZ == TypeRef::String && rhsZ == TypeRef::String)
                return genStringBinary(*bin, lhs, rhs);

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

            case TypeRef::Character:
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

            case TypeRef::String: {
                llvm::Type* ptrTy = llvm::PointerType::get(ctx_, 0);
                llvm::Function* cstr = runtimeFn(stringCstr_, "z_string_cstr", ptrTy, {ptrTy});
                arg = builder_.CreateCall(cstr, {arg}, "cstr");
                fmt = "%s\n";
                break;
            }

            case TypeRef::Dynamic: {
                llvm::Type* ptrTy = llvm::PointerType::get(ctx_, 0);
                llvm::Function* dynPrint = runtimeFn(dynamicPrint_, "z_dynamic_print", llvm::Type::getVoidTy(ctx_), {ptrTy});
                
                builder_.CreateCall(dynPrint, {arg});
                return llvm::ConstantInt::get(llvm::Type::getInt32Ty(ctx_), 0);
            }

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

    llvm::Value* CodeGen::genStringBinary(const BinaryExpr& bin, llvm::Value* lhs, llvm::Value* rhs) {
        llvm::Type* ptrTy = llvm::PointerType::get(ctx_, 0);
        llvm::Type* i32Ty = llvm::Type::getInt32Ty(ctx_);

        if (bin.op == "+") {
            llvm::Function* concat = runtimeFn(stringConcat_, "z_string_concat", ptrTy, {ptrTy, ptrTy});
            return builder_.CreateCall(concat, {lhs, rhs}, "concat");
        }

        llvm::Function* cmp = runtimeFn(stringCmp_, "z_string_cmp", i32Ty, {ptrTy, ptrTy});
        llvm::Value* result = builder_.CreateCall(cmp, {lhs, rhs}, "strcmp");
        llvm::Value* zero = llvm::ConstantInt::get(i32Ty, 0);

        if (bin.op == "==")
            return builder_.CreateICmpEQ(result, zero);
        
        if (bin.op == "!=")
            return builder_.CreateICmpNE(result, zero);
        
        if (bin.op == "<")
            return builder_.CreateICmpSLT(result, zero);
        
        if (bin.op == "<=")
            return builder_.CreateICmpSLE(result, zero);
        
        if (bin.op == ">")
            return builder_.CreateICmpSGT(result, zero);
        
        if (bin.op == ">=")
            return builder_.CreateICmpSGE(result, zero);

        throw std::runtime_error("codegen: operator '" + bin.op + "' is not defined for strings");
    }

    llvm::Value* CodeGen::genTernary(const TernaryExpr& tern) {
        llvm::Value* cond = toBoolValue(genExpr(*tern.cond));

        llvm::Value* thenVal = convert(genExpr(*tern.thenExpr), tern.thenExpr->resolvedType, tern.resolvedType);
        llvm::Value* elseVal = convert(genExpr(*tern.elseExpr), tern.elseExpr->resolvedType, tern.resolvedType);

        return builder_.CreateSelect(cond, thenVal, elseVal, "terntmp");
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
            val = convert(val, ret->value->resolvedType, currentReturnType_);

            // `main` is declared i32 regardless of Z's `int`, so a final width
            // adjustment can still be needed after the Z-level conversion.
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
            declareVar(let->name, alloca, let->type);

            llvm::Value* initVal = convert(genExpr(*let->init), let->init->resolvedType, let->type);
            if (initVal->getType() != allocaTy)
                initVal = coerce(initVal, initVal->getType(), allocaTy);

            builder_.CreateStore(initVal, alloca);
            return;
        }

        if (auto* asgn = dynamic_cast<const AssignStmt*>(&stmt)) {
            const VarBinding* binding = lookupVar(asgn->name);
            if (!binding)
                throw std::runtime_error("assignment to undeclared variable '" + asgn->name + "'");

            llvm::Type* varTy = binding->slot->getAllocatedType();
            llvm::Value* val = convert(genExpr(*asgn->value), asgn->value->resolvedType, binding->type);

            if (val->getType() != varTy)
                val = coerce(val, val->getType(), varTy);

            builder_.CreateStore(val, binding->slot);
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

    // Creates the llvm::Function with no body. Every function in the program is
    // declared before any body is emitted.
    void CodeGen::declareFnDecl(const FnDecl& fn) {
        const std::string symbol = fn.isExtern ? fn.name : mangleFunction(fn.owner, fn.name);

        if (module_->getFunction(symbol))
            return;

        std::vector<llvm::Type*> paramTypes;
        for (const auto& p : fn.params)
            paramTypes.push_back(llvmType(p.type));

        llvm::Type* retTy = (fn.name == "main") ? llvm::Type::getInt32Ty(ctx_) : llvmType(fn.returnType);
        llvm::FunctionType* ft = llvm::FunctionType::get(retTy, paramTypes, false);

        // An extern function keeps its literal C name — the runtime owns it.
        // Everything else except `main` is internal, so the optimiser can delete
        // whatever the program does not reach; without that the whole stdlib is
        // emitted into every binary and globaldce cannot touch it.
        const bool isEntryPoint = fn.name == "main" && fn.owner.empty();
        const auto linkage = (fn.isExtern || isEntryPoint)
            ? llvm::Function::ExternalLinkage
            : llvm::Function::InternalLinkage;

        llvm::Function::Create(ft, linkage, symbol, module_.get());
    }

    void CodeGen::defineFnDecl(const FnDecl& fn) {
        // No body to emit — the C runtime provides the definition.
        if (fn.isExtern)
            return;

        scopes_.clear();
        pushScope();
        currentReturnType_ = fn.returnType;

        llvm::Function* func = module_->getFunction(mangleFunction(fn.owner, fn.name));

        std::size_t i = 0;
        for (auto& arg : func->args())
            arg.setName(fn.params[i++].name);
          
        llvm::BasicBlock* entry = llvm::BasicBlock::Create(ctx_, "entry", func);
        builder_.SetInsertPoint(entry);

        i = 0;
        for (auto& arg : func->args()) {
            llvm::AllocaInst* alloca = createEntryBlockAlloca(func, fn.params[i].name, llvmType(fn.params[i].type));

            builder_.CreateStore(&arg, alloca);
            declareVar(fn.params[i].name, alloca, fn.params[i].type);
            i++;
        }

        for (const auto& stmt : fn.body->stmts)
            genStmt(*stmt);

        if (!builder_.GetInsertBlock()->getTerminator())
            builder_.CreateUnreachable();

        popScope();
    }
}
