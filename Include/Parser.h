#pragma once

#include "AST.h"
#include "Token.h"

#include <vector>

namespace ZCompiler {
    class Parser {
        public:
            explicit Parser(std::vector<Token> tokens);
            Program parse();

        private:
            std::size_t pos_ = 0;
            std::vector<Token> tokens_;

            // Primitives
            void skipNewlines();
            const Token& advance();
            const Token& peek() const;
            bool check(TokenType type) const;  
            const Token& expect(TokenType type, const char* msg);

            // Grammar Rules
            TypeRef parseTypeRef();
            std::unique_ptr<BlockStmt> parseBlock();

            DeclPtr parseFnDecl();

            StmtPtr parseStmt();
            StmtPtr parseLetStmt();
            StmtPtr parseExprStmt();
            StmtPtr parseReturnStmt();
            StmtPtr parseAssignStmt();

            StmtPtr parseIfStmt();
            StmtPtr parseSwitchStmt();

            StmtPtr parseDoStmt();
            StmtPtr parseForStmt();
            StmtPtr parseWhileStmt();
            StmtPtr parseBreakStmt();
            StmtPtr parseContinueStmt();
           
            ExprPtr parseExpr();
            ExprPtr parseUnaryExpr();
            ExprPtr parsePrimaryExpr();
            ExprPtr parseCallorIdent();
            ExprPtr parseCompareExpr();

            ExprPtr parseIntLit();
            ExprPtr parseCharLit();
            ExprPtr parseBoolLit();
            ExprPtr parseFloatLit();
            ExprPtr parseDoubleLit();
            ExprPtr parseStringLit();

            ExprPtr parseOrExpr();
            ExprPtr parseAndExpr();
            ExprPtr parseAddExpr();
            ExprPtr parseMulExpr();
    };
}
