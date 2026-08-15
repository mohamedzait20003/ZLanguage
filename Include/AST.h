#pragma once

#include "Token.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace ZCompiler {
    // Forward Declarations
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

    // Types
    enum class TypeRef {
        Int,
        Int32,
        Int64,
        Int128,
        Float,
        Float16,
        Float32,
        Float64,
        Double,
        Bool,
        Character,
        String,
        Dynamic,
        Null,
    };

    // Reference types are pointer-shaped at the LLVM level, which is what makes
    // them assignable from `null` and comparable to it by identity.
    inline bool isReferenceType(TypeRef type) {
        return type == TypeRef::String || type == TypeRef::Dynamic || type == TypeRef::Null;
    }

    // AST Nodes
    struct Expr : Node {
        TypeRef resolvedType = TypeRef::Int;
    };

    struct Stmt : Node {};
    struct Decl : Node {};

    // Expressions
    struct IntLitExpr : Expr {
        long long value = 0;
    };

    struct FloatLitExpr : Expr {
        float value = 0.0;
    };

    struct DoubleLitExpr : Expr {
        double value = 0.0;
    };

    struct BoolLitExpr : Expr {
        bool value = false;
    };

    struct CharLitExpr : Expr {
        uint32_t value = 0;
    };

    struct StringLitExpr : Expr {
        std::string value;
    };

    struct CallExpr : Expr {
        std::string qualifier;
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

    struct UnaryExpr : Expr {
        std::string op;
        ExprPtr operand;
    };

    struct NullLitExpr : Expr {};

    struct CastExpr : Expr {
        TypeRef targetType;
        ExprPtr operand;
    };

    struct DynCastExpr : Expr {
        TypeRef targetType;
        ExprPtr operand;
    };

    struct TernaryExpr : Expr {
        ExprPtr cond;
        ExprPtr thenExpr;
        ExprPtr elseExpr;
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

    struct IfStmt : Stmt {
        ExprPtr cond;
        std::unique_ptr<BlockStmt> thenBranch;
        std::unique_ptr<BlockStmt> elseBranch;
    };

    struct CaseArm : Node {
        ExprPtr value;
        long long constValue = 0;
        std::unique_ptr<BlockStmt> body;
    };

    struct SwitchStmt : Stmt {
        ExprPtr scrutinee;
        std::vector<CaseArm> cases;
        std::unique_ptr<BlockStmt> defaultArm;
    };

    struct WhileStmt : Stmt {
        ExprPtr cond;
        std::unique_ptr<BlockStmt> body;
    };

    struct ForStmt : Stmt {
        StmtPtr init;
        ExprPtr cond;
        StmtPtr step;
        std::unique_ptr<BlockStmt> body;
    };

    struct DoStmt : Stmt {
        ExprPtr cond;
        std::unique_ptr<BlockStmt> body;
    };

    struct BreakStmt : Stmt {};

    struct ContinueStmt : Stmt {};

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

        std::string owner;
        bool isExtern = false;
    };

    // `namespace NAME { ... }`. Several blocks with the same name merge, so this
    // node is a contribution to a namespace rather than its definition.
    struct NamespaceDecl : Decl {
        std::string name;
        std::vector<DeclPtr> decls;
    };

    // `using NAME` — a file-level import, not a declaration, so it lives beside
    // the decl list rather than in it.
    struct UsingDecl {
        std::string name;
        int line = 0;
        int column = 0;
    };

    struct Program {
        std::vector<UsingDecl> usings;
        std::vector<DeclPtr> decls;
    };
}