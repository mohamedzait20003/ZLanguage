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
    };

    // Semantic Analyzer
    class Sema {
        public:
            void check(Program& program);

        private:
        std::vector<std::unordered_map<std::string, TypeRef>> scopes_;
        enum class Ctx { Loop, Switch };

        std::unordered_map<std::string, FuncSig> functions_;
        std::vector<Ctx> contextStack_;

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

        // Constant folding for `switch` case arms. Accepts integer, character and
        // boolean literals, optionally negated. Returns false for anything else.
        static bool constIntValue(const Expr& expr, long long& out);

        // Conservative "does control always leave via return?" analysis.
        static bool alwaysReturns(const Stmt& stmt);
        static bool blockAlwaysReturns(const BlockStmt& block);
        static bool canBreakOut(const Stmt& stmt);
        static bool blockCanBreakOut(const BlockStmt& block);


        // Checkers
        TypeRef resolveExpr(Expr& expr);
        void checkStmt(Stmt& stmt);
        void checkFnDecl(FnDecl& fn);
    };
}