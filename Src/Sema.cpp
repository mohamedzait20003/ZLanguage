#include "Sema.h"

#include <map>
#include <stdexcept>
#include <algorithm>

namespace ZCompiler {
    static FuncSig signatureOf(const FnDecl& fn) {
        FuncSig sig;
        sig.returnType = fn.returnType;
        sig.owner = fn.owner;
        sig.isExtern = fn.isExtern;

        for (const auto& p : fn.params)
            sig.paramTypes.push_back(p.type);

        return sig;
    }

    // True when `candidate` is `prefix` itself or nested inside it. Compares on
    // dot boundaries so `mathematics` is not treated as a child of `math`.
    static bool isWithin(const std::string& candidate, const std::string& prefix) {
        if (candidate == prefix)
            return true;

        return candidate.size() > prefix.size()
            && candidate.compare(0, prefix.size(), prefix) == 0
            && candidate[prefix.size()] == '.';
    }

    void Sema::collectNamespace(const NamespaceDecl& ns) {
        // Registering the namespace even when it declares nothing directly means
        // `using a` works for a namespace that only contains sub-namespaces.
        auto& members = namespaces_[ns.name];

        for (const auto& member : ns.decls) {
            if (auto* nested = dynamic_cast<NamespaceDecl*>(member.get())) {
                collectNamespace(*nested);
                continue;
            }

            auto* fn = dynamic_cast<FnDecl*>(member.get());
            if (!fn)
                continue;

            // Without overloading (M16) a repeated name inside one namespace
            // can only be a mistake.
            if (members.count(fn->name))
                throw std::runtime_error(
                    "redeclaration of '" + fn->name + "' in namespace '" + ns.name
                    + "' at line " + std::to_string(fn->line)
                );

            members[fn->name] = signatureOf(*fn);
        }
    }

    // Pass 0: every namespace and its members, keyed by fully qualified dotted
    // name and merged across blocks. Runs before signatures so a namespace
    // declared below its first use still resolves.
    void Sema::collectNamespaces(const Program& program) {
        for (const auto& decl : program.decls) {
            if (auto* ns = dynamic_cast<NamespaceDecl*>(decl.get()))
                collectNamespace(*ns);
        }
    }

    void Sema::applyUsings(const Program& program) {
        for (const auto& use : program.usings) {
            if (!namespaces_.count(use.name))
                throw std::runtime_error(
                    "unknown namespace '" + use.name + "' at line " + std::to_string(use.line)
                    + ", column " + std::to_string(use.column)
                );

            if (std::find(imports_.begin(), imports_.end(), use.name) != imports_.end())
                throw std::runtime_error(
                    "duplicate 'using " + use.name + "' at line " + std::to_string(use.line)
                    + ", column " + std::to_string(use.column)
                );

            imports_.push_back(use.name);
        }
    }

    void Sema::check(Program& program) {
        collectNamespaces(program);
        applyUsings(program);

        // Pass 1: file-scope signatures.
        for (const auto& decl : program.decls) {
            if (auto* fn = dynamic_cast<FnDecl*>(decl.get()))
                functions_[fn->name] = signatureOf(*fn);
        }

        // Pass 2: bodies, file scope first, then every namespace member.
        for (const auto& decl : program.decls) {
            if (auto* fn = dynamic_cast<FnDecl*>(decl.get())) {
                checkFnDecl(*fn);
                continue;
            }

            if (auto* ns = dynamic_cast<NamespaceDecl*>(decl.get()))
                checkNamespaceBodies(*ns);
        }
    }

    void Sema::checkNamespaceBodies(const NamespaceDecl& ns) {
        for (const auto& member : ns.decls) {
            if (auto* nested = dynamic_cast<NamespaceDecl*>(member.get())) {
                checkNamespaceBodies(*nested);
                continue;
            }

            if (auto* fn = dynamic_cast<FnDecl*>(member.get()))
                checkFnDecl(*fn);
        }
    }

    const FuncSig* Sema::resolveCall(CallExpr& call) {
        const std::string where = " at line " + std::to_string(call.line);

        if (!call.qualifier.empty()) {
            auto ns = namespaces_.find(call.qualifier);
            if (ns == namespaces_.end())
                throw std::runtime_error("unknown namespace '" + call.qualifier + "'" + where);

            auto fn = ns->second.find(call.callee);
            if (fn == ns->second.end())
                throw std::runtime_error(
                    "namespace '" + call.qualifier + "' has no function '" + call.callee + "'" + where
                );

            return &fn->second;
        }

        // Innermost scope wins: a namespace member sees its siblings before it
        // sees file scope or anything imported.
        if (!currentNamespace_.empty()) {
            auto ns = namespaces_.find(currentNamespace_);

            if (ns != namespaces_.end()) {
                auto fn = ns->second.find(call.callee);

                if (fn != ns->second.end()) {
                    call.qualifier = currentNamespace_;
                    return &fn->second;
                }
            }
        }

        auto local = functions_.find(call.callee);
        if (local != functions_.end())
            return &local->second;

        // `using X` brings in X and everything nested beneath it, so the search
        // covers each import and all its descendants.
        const FuncSig* found = nullptr;
        std::string foundIn;

        for (const auto& imported : imports_) {
            for (const auto& entry : namespaces_) {
                if (!isWithin(entry.first, imported))
                    continue;

                auto fn = entry.second.find(call.callee);
                if (fn == entry.second.end())
                    continue;

                // The same namespace can be reached through two imports (`using
                // a` and `using a.b`); that is not ambiguity, it is one symbol.
                if (found && foundIn == entry.first)
                    continue;

                if (found)
                    throw std::runtime_error(
                        "'" + call.callee + "' is ambiguous between namespaces '" + foundIn
                        + "' and '" + entry.first + "'" + where + " — qualify the call"
                    );

                found = &fn->second;
                foundIn = entry.first;
            }
        }

        if (!found)
            throw std::runtime_error("call to undeclared function '" + call.callee + "'" + where);

        call.qualifier = foundIn;
        return found;
    }


    // Scope Helpers
    void Sema::pushScope() {
        scopes_.emplace_back();
    }

    void Sema::popScope() {
        scopes_.pop_back();
    }

    void Sema::declare(const std::string& name, TypeRef type, int line, int column) {
        if(scopes_.empty())
            scopes_.emplace_back();

        auto& top = scopes_.back();

        if (top.count(name)) {
            throw std::runtime_error(
                "semantic error: redeclaration of '" + name + "' at line " +
                std::to_string(line) + ", column " + std::to_string(column)
            );
        }

        top[name] = type;
    }

    TypeRef Sema::lookup(const std::string& name, int line, int column) const {
        for (auto it = scopes_.rbegin(); it != scopes_.rend(); ++it) {
            auto found = it->find(name);

            if (found != it->end()) {
                return found->second;
            }
        }

        throw std::runtime_error(
            "semantic error: undeclared identifier '" + name + "' at line " +
            std::to_string(line) + ", column " + std::to_string(column)
        );
    }

    // Type Helpers
    TypeRef Sema::normalize(TypeRef type) {
        switch(type) {
            case TypeRef::Float32:
                return TypeRef::Float;
            case TypeRef::Float64:
                return TypeRef::Double;
            case TypeRef::Int64:
                return TypeRef::Int;
            default:
                return type;
        }
    }

    bool Sema::isNumeric(TypeRef type){
        switch (normalize(type)){
            case TypeRef::Bool:
            case TypeRef::Int:
            case TypeRef::Int32:
            case TypeRef::Int128:
            case TypeRef::Float:
            case TypeRef::Float16:
            case TypeRef::Double:
            case TypeRef::Character:
                return true;

            default:
                return false;
        }
    }

    bool Sema::isInteger(TypeRef type){
        switch (normalize(type)){
            case TypeRef::Bool:
            case TypeRef::Int:
            case TypeRef::Int32:
            case TypeRef::Int128:
            case TypeRef::Character:
                return true;

            default:
                return false;
        }
    }

    bool Sema::isFloat(TypeRef type){
        switch (normalize(type)){
            case TypeRef::Float:
            case TypeRef::Float16:
            case TypeRef::Double:
                return true;

            default:
                return false;
        }
    }

    bool Sema::canWiden(TypeRef from, TypeRef to){
        TypeRef fromType = normalize(from);
        TypeRef toType = normalize(to);

        if (fromType == toType)
            return true;

        // null is assignable to any reference type, and to nothing else.
        if (fromType == TypeRef::Null)
            return isReferenceType(toType);

        if (toType == TypeRef::Null)
            return false;

        // Boxing into dynamic is implicit; leaving dynamic never is.
        if (toType == TypeRef::Dynamic)
            return true;

        if (fromType == TypeRef::Dynamic)
            return false;

        // string converts to nothing and nothing converts to string.
        if (fromType == TypeRef::String || toType == TypeRef::String)
            return false;

        static const TypeRef intChain[] = {
            TypeRef::Bool,
            TypeRef::Int32,
            TypeRef::Int,
            TypeRef::Int128
        };

        int Fi = -1, Ti = -1;
        for (size_t i = 0; i < std::size(intChain); ++i) {
            if (fromType == intChain[i]) 
                Fi = i;

            if (toType == intChain[i]) 
                Ti = i;
        }

        if (Fi >= 0 && Ti >= 0 && Fi <= Ti) 
            return true;

        static const TypeRef floatChain[] = {
            TypeRef::Float16,
            TypeRef::Float,
            TypeRef::Double
        };

        Fi = Ti = -1;
        for (size_t i = 0; i < std::size(floatChain); ++i) {
            if (fromType == floatChain[i]) 
                Fi = i;

            if (toType == floatChain[i]) 
                Ti = i;
        }

        if (Fi >= 0 && Ti >= 0 && Fi <= Ti) 
            return true;

        if (isInteger(fromType) && isFloat(toType))
            return true;

        return false;
    }

    TypeRef Sema::promoteArith(TypeRef a, TypeRef b) {
        TypeRef normA = normalize(a);
        TypeRef normB = normalize(b);

        if (normA == normB)
            return normA;

        if (normA == TypeRef::Double || normB == TypeRef::Double)
            return TypeRef::Double;

        if (normA == TypeRef::Float || normB == TypeRef::Float)
            return TypeRef::Float;

        if (normA == TypeRef::Float16 || normB == TypeRef::Float16)
            return TypeRef::Float16;

        static const TypeRef intOrder[] = {
            TypeRef::Bool,
            TypeRef::Int32,
            TypeRef::Int,
            TypeRef::Int128
        };


        int Ai = -1, Bi = -1;
        for (size_t i = 0; i < std::size(intOrder); ++i) {
            if (normA == intOrder[i]) 
                Ai = i;

            if (normB == intOrder[i]) 
                Bi = i;
        }

        if (Ai >= 0 && Bi >= 0) 
            return intOrder[std::max(Ai, Bi)];

        return TypeRef::Int;
    }

    std::string Sema::typeName(TypeRef type) {
        switch (type) {
            case TypeRef::Int: 
                return "int";
            case TypeRef::Int32: 
                return "int32";
            case TypeRef::Int64: 
                return "int64";
            case TypeRef::Int128: 
                return "int128";
            case TypeRef::Float: 
                return "float";
            case TypeRef::Float16: 
                return "float16";
            case TypeRef::Float32: 
                return "float32";
            case TypeRef::Float64: 
                return "float64";
            case TypeRef::Double: 
                return "double";
            case TypeRef::Bool: 
                return "bool";
            case TypeRef::Character:
                return "character";
            case TypeRef::String:
                return "string";
            case TypeRef::Dynamic:
                return "dynamic";
            case TypeRef::Null:
                return "null";
            default:
                return "unknown";
        }
    }

    int Sema::intBitWidth(TypeRef type) {
        switch (normalize(type)) {
            case TypeRef::Bool:
                return 1;
            case TypeRef::Int32:
            case TypeRef::Character:
                return 32;
            case TypeRef::Int128:
                return 128;
            default:
                return 64;
        }
    }

    bool Sema::constIntValue(const Expr& expr, long long& out) {
        if (auto* i = dynamic_cast<const IntLitExpr*>(&expr)) {
            out = i->value;
            return true;
        }

        if (auto* c = dynamic_cast<const CharLitExpr*>(&expr)) {
            out = static_cast<long long>(c->value);
            return true;
        }

        if (auto* b = dynamic_cast<const BoolLitExpr*>(&expr)) {
            out = b->value ? 1 : 0;
            return true;
        }

        if (auto* u = dynamic_cast<const UnaryExpr*>(&expr)) {
            long long inner = 0;

            if (u->op == "-" && constIntValue(*u->operand, inner)) {
                out = -inner;
                return true;
            }
        }

        return false;
    }

    bool Sema::alwaysReturns(const Stmt& stmt) {
        if (dynamic_cast<const ReturnStmt*>(&stmt)) {
            return true;
        }

        if (auto* ifs = dynamic_cast<const IfStmt*>(&stmt))
            return ifs->elseBranch && blockAlwaysReturns(*ifs->thenBranch) && blockAlwaysReturns(*ifs->elseBranch);

        if (auto* ds = dynamic_cast<const DoStmt*>(&stmt))
            return blockAlwaysReturns(*ds->body) && !blockCanBreakOut(*ds->body);

        if (auto* sw = dynamic_cast<const SwitchStmt*>(&stmt)) {
            if (!sw->defaultArm)
                return false;

            for (const auto& arm : sw->cases)
                if (!blockAlwaysReturns(*arm.body) || blockCanBreakOut(*arm.body))
                    return false;

            return blockAlwaysReturns(*sw->defaultArm) && !blockCanBreakOut(*sw->defaultArm);
        }

        return false;
    }

    bool Sema::blockAlwaysReturns(const BlockStmt& block) {
        for (const auto& stmt : block.stmts) {
            if (alwaysReturns(*stmt))
                return true;
        }

        return false;
    }

    bool Sema::canBreakOut(const Stmt& stmt) {
        if (dynamic_cast<const BreakStmt*>(&stmt))
            return true;

        if (auto* ifs = dynamic_cast<const IfStmt*>(&stmt))
            return blockCanBreakOut(*ifs->thenBranch)
                || (ifs->elseBranch && blockCanBreakOut(*ifs->elseBranch));

        return false;
    }

    bool Sema::blockCanBreakOut(const BlockStmt& block) {
        for (const auto& s : block.stmts)
            if (canBreakOut(*s))
                return true;

        return false;
    }

    // Checkers
    TypeRef Sema::resolveExpr(Expr& expr) {
        if (auto* lit = dynamic_cast<IntLitExpr*>(&expr))
            return expr.resolvedType = TypeRef::Int;

        if (auto* flit = dynamic_cast<FloatLitExpr*>(&expr))
            return expr.resolvedType = TypeRef::Float;

        if (auto* dlit = dynamic_cast<DoubleLitExpr*>(&expr))
            return expr.resolvedType = TypeRef::Double;

        if (auto* blit = dynamic_cast<BoolLitExpr*>(&expr))
            return expr.resolvedType = TypeRef::Bool;

        if (auto* clit = dynamic_cast<CharLitExpr*>(&expr))
            return expr.resolvedType = TypeRef::Character;

        if (auto* slit = dynamic_cast<StringLitExpr*>(&expr))
            return expr.resolvedType = TypeRef::String;

        if (auto* nlit = dynamic_cast<NullLitExpr*>(&expr))
            return expr.resolvedType = TypeRef::Null;

        if (auto* id = dynamic_cast<IdentExpr*>(&expr))
            return expr.resolvedType = lookup(id->name, id->line, id->column);

        if (auto *un = dynamic_cast<UnaryExpr*>(&expr)) {
            TypeRef opType = resolveExpr(*un->operand);

            if (un->op == "!")
                return expr.resolvedType = TypeRef::Bool;

            if (un->op == "-") {
                if (!isNumeric(opType))
                    throw std::runtime_error("Invalid operand for unary minus");

                return expr.resolvedType = opType;
            }

            throw std::runtime_error("Invalid unary operator");
        }

        if (auto* bin = dynamic_cast<BinaryExpr*>(&expr)) {
            TypeRef leftType = resolveExpr(*bin->lhs);
            TypeRef rightType = resolveExpr(*bin->rhs);

            const bool isEquality = bin->op == "==" || bin->op == "!=";
            const bool isRelational = bin->op == "<" || bin->op == "<=" || bin->op == ">" || bin->op == ">=";

            // `p == null` / `p != null` is pointer identity, not a value
            if (isEquality && (leftType == TypeRef::Null || rightType == TypeRef::Null)) {
                const TypeRef other = leftType == TypeRef::Null ? rightType : leftType;

                if (!isReferenceType(other))
                    throw std::runtime_error(
                        "cannot compare '" + typeName(other) + "' to null at line "
                        + std::to_string(bin->line) + " — only reference types can be null"
                    );

                return expr.resolvedType = TypeRef::Bool;
            }

            if ((isEquality || isRelational) && leftType == TypeRef::String && rightType == TypeRef::String)
                return expr.resolvedType = TypeRef::Bool;

            if (isEquality || isRelational) {
                if (leftType == TypeRef::String || rightType == TypeRef::String)
                    throw std::runtime_error(
                        "cannot compare '" + typeName(leftType) + "' with '" + typeName(rightType)
                        + "' at line " + std::to_string(bin->line)
                    );

                if (leftType == TypeRef::Dynamic || rightType == TypeRef::Dynamic)
                    throw std::runtime_error(
                        "cannot compare a 'dynamic' value directly at line " + std::to_string(bin->line)
                        + " — extract it first with static_cast<T>(...)"
                    );

                if (!isNumeric(leftType) || !isNumeric(rightType))
                    throw std::runtime_error("comparison requires numeric types at line " + std::to_string(bin->line));

                return expr.resolvedType = TypeRef::Bool;
            }

            if (bin->op == "&&" || bin->op == "||") {
                if (!isNumeric(leftType) || !isNumeric(rightType))
                    throw std::runtime_error(
                        "logical operator '" + bin->op + "' requires numeric operands at line "
                        + std::to_string(bin->line)
                    );

                return expr.resolvedType = TypeRef::Bool;
            }

            if (bin->op == "+" && leftType == TypeRef::String && rightType == TypeRef::String)
                return expr.resolvedType = TypeRef::String;

            if (leftType == TypeRef::String || rightType == TypeRef::String)
                throw std::runtime_error(
                    "operator '" + bin->op + "' is not defined for string operands at line "
                    + std::to_string(bin->line)
                );

            if (leftType == TypeRef::Dynamic || rightType == TypeRef::Dynamic)
                throw std::runtime_error(
                    "arithmetic on a 'dynamic' value at line " + std::to_string(bin->line)
                    + " — extract it first with static_cast<T>(...)"
                );

            if (!isNumeric(leftType) || !isNumeric(rightType))
                throw std::runtime_error("arithmetic operator '" + bin->op + "' requires numeric types at line " + std::to_string(bin->line));

            return expr.resolvedType = promoteArith(leftType, rightType);
        }

        if (auto* tern = dynamic_cast<TernaryExpr*>(&expr)) {
            TypeRef condType = resolveExpr(*tern->cond);

            if (!isNumeric(condType))
                throw std::runtime_error(
                    "ternary condition must be numeric at line " + std::to_string(tern->line)
                );

            TypeRef thenType = resolveExpr(*tern->thenExpr);
            TypeRef elseType = resolveExpr(*tern->elseExpr);

            // `cond ? s : null` resolves to the reference branch's type.
            if (thenType == TypeRef::Null && isReferenceType(elseType))
                return expr.resolvedType = elseType;

            if (elseType == TypeRef::Null && isReferenceType(thenType))
                return expr.resolvedType = thenType;

            if (thenType == elseType)
                return expr.resolvedType = thenType;

            if (isNumeric(thenType) && isNumeric(elseType))
                return expr.resolvedType = promoteArith(thenType, elseType);

            throw std::runtime_error(
                "ternary branches have incompatible types '" + typeName(thenType)
                + "' and '" + typeName(elseType) + "' at line " + std::to_string(tern->line)
            );
        }

        if (auto* call = dynamic_cast<CallExpr*>(&expr)) {
            if (call->callee == "print"){
                if (call->args.empty())
                    throw std::runtime_error("print requires at least one argument at line " + std::to_string(call->line));

                const TypeRef argType = resolveExpr(*call->args[0]);

                if (argType == TypeRef::Null)
                    throw std::runtime_error(
                        "cannot print the null literal directly at line " + std::to_string(call->line)
                    );

                return expr.resolvedType = TypeRef::Int;
            }

            const FuncSig& sig = *resolveCall(*call);

            if (call->args.size() != sig.paramTypes.size())
                throw std::runtime_error("argument count mismatch in call to '" + call->callee + "' at line " + std::to_string(call->line));

            for (std::size_t i = 0; i < call->args.size(); i++){
                TypeRef argType = resolveExpr(*call->args[i]);

                if(!canWiden(argType, sig.paramTypes[i]))
                    throw std::runtime_error(
                        "argument type mismatch for parameter " + std::to_string(i) +
                        " in call to '" + call->callee + "' at line " + std::to_string(call->line)
                    );
            }

            return expr.resolvedType = sig.returnType;
        }

        if (auto* cast = dynamic_cast<CastExpr*>(&expr)) {
            TypeRef src = resolveExpr(*cast->operand);
            checkCastable(src, cast->targetType, "static_cast", cast->line);
            return expr.resolvedType = cast->targetType;
        }

        if (auto* cast = dynamic_cast<DynCastExpr*>(&expr)) {
            TypeRef src = resolveExpr(*cast->operand);

            if (src != TypeRef::Dynamic)
                throw std::runtime_error(
                    "dynamic_cast requires a 'dynamic' operand, got '" + typeName(src)
                    + "' at line " + std::to_string(cast->line)
                );

            checkCastable(src, cast->targetType, "dynamic_cast", cast->line);
            return expr.resolvedType = cast->targetType;
        }

        throw std::runtime_error("Unhandled expression type");
    }

    void Sema::checkCastable(TypeRef src, TypeRef dst, const char* what, int line) {
        const std::string where = std::string(what) + " at line " + std::to_string(line);

        if (dst == TypeRef::Null)
            throw std::runtime_error("cannot cast to 'null' in " + where);

        if (src == TypeRef::Null) {
            if (!isReferenceType(dst))
                throw std::runtime_error("cannot cast null to '" + typeName(dst) + "' in " + where);

            return;
        }

        // Unboxing: any concrete type may be extracted from a dynamic. The tag
        // is verified at runtime.
        if (src == TypeRef::Dynamic)
            return;

        // Boxing via an explicit cast is allowed and mirrors implicit boxing.
        if (dst == TypeRef::Dynamic)
            return;

        // string converts to nothing else, and nothing else converts to string.
        // Parsing and formatting are `string` library functions in M6.
        if (src == TypeRef::String || dst == TypeRef::String)
            throw std::runtime_error(
                "cannot convert between '" + typeName(src) + "' and '" + typeName(dst) + "' in " + where
            );

        if (!isNumeric(src) || !isNumeric(dst))
            throw std::runtime_error(
                "cannot convert '" + typeName(src) + "' to '" + typeName(dst) + "' in " + where
            );
    }

    void Sema::checkStmt(Stmt& stmt) {
        if (auto* let = dynamic_cast<LetStmt*>(&stmt)) {
            TypeRef initType = resolveExpr(*let->init);

            if (dynamic_cast<IntLitExpr*>(let->init.get()) && isInteger(let->type)) {
                let->init->resolvedType = let->type;
                initType = let->type;
            } else if (dynamic_cast<FloatLitExpr*>(let->init.get()) && isFloat(let->type)) {
                let->init->resolvedType = let->type;
                initType = let->type;
            }

            if (!canWiden(initType, let->type))
                throw std::runtime_error(
                    "cannot initialize variable '" + let->name + "' of type '" + typeName(let->type)
                    + "' with expression of type '" + typeName(initType)
                    + "' at line " + std::to_string(let->line)
                );

            declare(let->name, let->type, let->line, let->column);
            return;
        }

        if (auto* asgn = dynamic_cast<AssignStmt*>(&stmt)) {
            TypeRef varType = lookup(asgn->name, asgn->line, asgn->column);
            TypeRef valType = resolveExpr(*asgn->value);

            if (!canWiden(valType, varType))
                throw std::runtime_error(
                    "cannot assign '" + typeName(valType)
                    + "' to '" + typeName(varType)
                    + "' (use static_cast) at line " + std::to_string(asgn->line)
                );

            return;
        }

        if (auto* ret = dynamic_cast<ReturnStmt*>(&stmt)) {
            TypeRef retType = resolveExpr(*ret->value);

            if (!canWiden(retType, currentReturnType_))
                throw std::runtime_error(
                    "cannot return '" + typeName(retType)
                    + "' from function returning '" + typeName(currentReturnType_)
                    + "' at line " + std::to_string(ret->line)
                );

            return;
        }

        if (auto* es = dynamic_cast<ExprStmt*>(&stmt)) {
            resolveExpr(*es->expr);
            return;
        }

        if (auto* ifs = dynamic_cast<IfStmt*>(&stmt)) {
            TypeRef condType = resolveExpr(*ifs->cond);

            if (!isNumeric(condType))
                throw std::runtime_error("if condition must be numeric at line " + std::to_string(ifs->line));
            
            pushScope();

            for (const auto& s : ifs->thenBranch->stmts) 
                checkStmt(*s);

            popScope();

            if (ifs->elseBranch) {
                pushScope();
                for (const auto& s : ifs->elseBranch->stmts) 
                    checkStmt(*s);
        
                popScope();
            }

            return;
        }

        if (auto* whl = dynamic_cast<WhileStmt*>(&stmt)) {
            TypeRef condType = resolveExpr(*whl->cond);

            if (!isNumeric(condType))
                throw std::runtime_error("while condition must be numeric at line " + std::to_string(whl->line));
            
            pushScope();
            contextStack_.push_back(Ctx::Loop);

            for (const auto& s : whl->body->stmts) 
                checkStmt(*s);
            
            contextStack_.pop_back();
            popScope();
            return;
        }

        if (auto* ds = dynamic_cast<DoStmt*>(&stmt)) {
            pushScope();
            contextStack_.push_back(Ctx::Loop);

            for (const auto& s : ds->body->stmts) 
                checkStmt(*s);

            popScope();
            contextStack_.pop_back();

            TypeRef condType = resolveExpr(*ds->cond);
            if (!isNumeric(condType))
                throw std::runtime_error("do...along condition must be numeric at line " + std::to_string(ds->line));
            
            return;
        }

        if (auto* fs = dynamic_cast<ForStmt*>(&stmt)) {
            pushScope();
            checkStmt(*fs->init);
            TypeRef condType = resolveExpr(*fs->cond);

            if (!isNumeric(condType))
                throw std::runtime_error("for condition must be numeric at line " + std::to_string(fs->line));
            
            checkStmt(*fs->step);
            contextStack_.push_back(Ctx::Loop);

            for (const auto& s : fs->body->stmts) 
                checkStmt(*s);
            
            contextStack_.pop_back();
            popScope();
            return;
        }

        if (auto* sw = dynamic_cast<SwitchStmt*>(&stmt)) {
            TypeRef scrType = resolveExpr(*sw->scrutinee);
            if (!isInteger(scrType))
                throw std::runtime_error("switch scrutinee must be integer or character at line " + std::to_string(sw->line));

            const int width = intBitWidth(scrType);
            std::map<long long, int> seen;

            for (auto& arm : sw->cases) {
                resolveExpr(*arm.value);

                long long value = 0;
                if (!constIntValue(*arm.value, value))
                    throw std::runtime_error(
                        "case value must be an integer, character, or boolean literal at line "
                        + std::to_string(arm.value->line)
                    );

                if (width < 64) {
                    const long long limit = 1LL << (width - 1);
                    const long long minValue = (width == 1) ? 0 : -limit;
                    const long long maxValue = (width == 1) ? 1 : limit - 1;

                    if (value < minValue || value > maxValue)
                        throw std::runtime_error(
                            "case value " + std::to_string(value) + " does not fit in scrutinee type '"
                            + typeName(scrType) + "' at line " + std::to_string(arm.value->line)
                        );
                }

                auto inserted = seen.emplace(value, arm.value->line);
                if (!inserted.second)
                    throw std::runtime_error(
                        "duplicate case value " + std::to_string(value) + " at line "
                        + std::to_string(arm.value->line) + " (previous case at line "
                        + std::to_string(inserted.first->second) + ")"
                    );

                arm.value->resolvedType = scrType;
                arm.constValue = value;

                pushScope();
                contextStack_.push_back(Ctx::Switch);

                for (const auto& s : arm.body->stmts)
                    checkStmt(*s);

                contextStack_.pop_back();
                popScope();
            }

            if (sw->defaultArm) {
                pushScope();

                for (const auto& s : sw->defaultArm->stmts) 
                    checkStmt(*s);

                popScope();
            }

            return;
        }

        if (auto *brk = dynamic_cast<BreakStmt*>(&stmt)) {
            if (contextStack_.empty())
                throw std::runtime_error("break statement not within a loop or switch at line " + std::to_string(brk->line));
            
            return;
        }

        if (auto *cont = dynamic_cast<ContinueStmt*>(&stmt)) {
            if (std::find(contextStack_.rbegin(), contextStack_.rend(), Ctx::Loop) == contextStack_.rend())
                throw std::runtime_error(
                    "'continue' outside of loop at line " + std::to_string(cont->line)
                );

            return;
        }

        throw std::runtime_error("sema: unknown statement type");
    }

    void Sema::checkFnDecl(FnDecl& fn) {
        // `extern fn` is a declaration only — its definition is in the C runtime,
        // so there is nothing here to type-check.
        if (fn.isExtern)
            return;

        currentReturnType_ = fn.returnType;
        currentNamespace_ = fn.owner;
        pushScope();

        for (const auto& p : fn.params)
            declare(p.name, p.type, fn.line, fn.column);

        for (const auto& s : fn.body->stmts)
            checkStmt(*s);
            
        popScope();

        if (!blockAlwaysReturns(*fn.body))
            throw std::runtime_error(
                "function '" + fn.name + "' may not return a value on all paths at line "
                + std::to_string(fn.line)
            );
    }
};