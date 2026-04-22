#pragma once

#include "Token.h"

#include <memory>
#include <string>
#include <vector>

namespace ZCompiler {
    struct Expr;
    struct Stmt;
    struct Decl;

    using ExprPtr = std::unique_ptr<Expr>;
    using StmtPtr = std::unique_ptr<Stmt>;
    using DeclPtr = std::unique_ptr<Decl>;

    struct Node {
        int line = 0;
        int column = 0;
        virtual ~Node() = default;
    };

    struct Expr : Node {};
    struct Stmt : Node {};
    struct Decl : Node {};

    enum class TypeRef {
        Int,
    };

    // Expressions
    struct IntLitExpr : Expr {
        long long value = 0;
    };

    struct CallExpr : Expr {
        std::string callee;
        std::vector<ExprPtr> args;
    };

    struct IdentExpr : Expr {
        std::string name;
    };

    struct BinaryExpr : Expr {
        std::string op;
        ExprPtr lhs;
        ExprPtr rhs;
    };

    // Statements
    struct ExprStmt : Stmt {
        ExprPtr expr;
    };

    struct ReturnStmt : Stmt {
        ExprPtr value;
    };

    struct BlockStmt : Stmt {
        std::vector<StmtPtr> stmts;
    };

    struct LetStmt : Stmt {
        std::string name;
        TypeRef type = TypeRef::Int;
        ExprPtr init;
    };

    struct AssignStmt : Stmt {
        std::string name;
        ExprPtr value;
    };

    // Declarations
    struct Param {
        std::string name;
        TypeRef type = TypeRef::Int;
    };

    struct FnDecl : Decl {
        std::string name;
        std::vector<Param> params;
        TypeRef returnType = TypeRef::Int;
        std::unique_ptr<BlockStmt> body;
    };

    struct Program {
        std::vector<DeclPtr> decls;
    };
}