#pragma once

#include "AST.h"

#include <string>
#include <vector>
#include <unordered_map>

namespace ZCompiler {
    // Semantic Analysis Signature
    struct FuncSig {
        TypeRef returnType;
        std::vector<TypeRef> paramTypes;
        std::string owner;

        // Implemented in C: never mangled, never given a body.
        bool isExtern = false;
    };

    // Semantic Analyzer
    class Sema {
        public:
            void check(Program& program);

        private:
        std::vector<std::unordered_map<std::string, TypeRef>> scopes_;
        enum class Ctx { Loop, Switch };

        // File-scope functions, keyed by plain name.
        std::unordered_map<std::string, FuncSig> functions_;

        // Namespace constants: namespace -> name -> declared type. Constants and
        // functions live in separate tables because they are looked up from
        // different syntactic positions and can share a name harmlessly.
        std::unordered_map<std::string, TypeRef> fileConsts_;
        std::unordered_map<std::string, std::unordered_map<std::string, FuncSig>> namespaces_;
        std::unordered_map<std::string, std::unordered_map<std::string, TypeRef>> namespaceConsts_;
        
        std::vector<std::string> imports_;
        std::vector<Ctx> contextStack_;

        std::string currentNamespace_;

        TypeRef currentReturnType_ = TypeRef::Int;

        // Scope helpers
        void pushScope();
        void popScope();
        void declare(const std::string& name, TypeRef type, int line, int column);
        TypeRef lookup(const std::string& name, int line, int column) const;

        // Type helpers
        static TypeRef normalize(TypeRef type);
        static bool isNumeric(TypeRef type);
        static bool isInteger(TypeRef type);
        static bool isFloat(TypeRef type);

        static bool canWiden(TypeRef from, TypeRef to);
        static void checkCastable(TypeRef src, TypeRef dst, const char* what, int line);
        static TypeRef promoteArith(TypeRef a, TypeRef b);
        static std::string typeName(TypeRef type);
        static int intBitWidth(TypeRef type);

        static bool constIntValue(const Expr& expr, long long& out);
        static bool alwaysReturns(const Stmt& stmt);
        static bool blockAlwaysReturns(const BlockStmt& block);
        static bool canBreakOut(const Stmt& stmt);
        static bool blockCanBreakOut(const BlockStmt& block);


        const FuncSig* resolveCall(CallExpr& call);

        // Resolves a bare or qualified name to a constant, applying the same
        // innermost-first order as calls. Returns false when it is not a
        // constant at all, so locals keep priority.
        bool resolveConst(IdentExpr& ident, TypeRef& out) const;

        void collectNamespaces(const Program& program);
        void collectNamespace(const NamespaceDecl& ns);
        void applyUsings(const Program& program);
        void checkNamespaceBodies(const NamespaceDecl& ns);
        void checkConstDecls(const std::vector<DeclPtr>& decls);
        static bool isConstantExpr(const Expr& expr);

        // Checkers
        TypeRef resolveExpr(Expr& expr);
        void checkStmt(Stmt& stmt);
        void checkFnDecl(FnDecl& fn);
    };
}