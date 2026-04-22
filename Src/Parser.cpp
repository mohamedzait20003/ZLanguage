#include "Parser.h"

#include <cctype>
#include <string>
#include <stdexcept>

namespace ZCompiler {
    Parser::Parser(std::vector<Token> tokens) : tokens_(std::move(tokens)) {}

    Program Parser::parse() {
        Program program;
        skipNewlines();

        while (!check(TokenType::Eof)) {
            if (check(TokenType::Fn)) {
                program.decls.push_back(parseFnDecl());
            } else {
                const Token& t = peek();
                throw std::runtime_error(
                    "expected 'fn' at top level at line " + std::to_string(t.line) +
                    ", column " + std::to_string(t.column)
                );
            }
            skipNewlines();
        }

        return program;
    }

    // Primitives
    void Parser::skipNewlines() {
        while (check(TokenType::NewLine)) {
            advance();
        }
    }

    const Token& Parser::advance() {
        const Token& t = tokens_[pos_];

        if (t.type != TokenType::Eof) {
            pos_++;
        }

        return t;
    }

    const Token& Parser::peek() const {
        return tokens_[pos_];
    }

    bool Parser::check(TokenType type) const {
        return peek().type == type;
    }
    
    const Token& Parser::expect(TokenType type, const char* msg) {
        if(!check(type)){
            const Token& t = peek();

            throw std::runtime_error(
                std::string(msg) + " at line " + std::to_string(t.line) +
                ", column " + std::to_string(t.column) +
                " (got '" + t.lexeme + "')"
            );
        }

        return advance();
    }

    // Grammar Rules
    TypeRef Parser::parseTypeRef() {
        expect(TokenType::Int, "expected type");
        return TypeRef::Int;
    }

    std::unique_ptr<BlockStmt> Parser::parseBlock() {
        const Token& brace = expect(TokenType::LBrace, "expected '{' to start block");

        auto block = std::make_unique<BlockStmt>();

        block->line = brace.line;
        block->column = brace.column;

        skipNewlines();

        while (!check(TokenType::RBrace) && !check(TokenType::Eof)) {
            block->stmts.push_back(parseStmt());
            skipNewlines();
        }

        expect(TokenType::RBrace, "expected '}' to end block");
        return block;
    }

    DeclPtr Parser::parseFnDecl() {
        const Token& fnToken = expect(TokenType::Fn, "expected 'fn' keyword");

        auto decl = std::make_unique<FnDecl>();
        
        decl->line = fnToken.line;
        decl->column = fnToken.column;
        decl->name = expect(TokenType::Identifier, "expected function name").lexeme;

        expect(TokenType::LParen, "expected '(' after function name");
        expect(TokenType::RParen, "expected ')' after parameter list");
        expect(TokenType::Arrow, "expected '->' after parameter list");

        decl->returnType = parseTypeRef();

        skipNewlines();
        decl->body = parseBlock();

        return decl;
    }

    StmtPtr Parser::parseStmt() {
        skipNewlines();

        if (check(TokenType::Return)) {
            return parseReturnStmt();
        }

        if (check(TokenType::Let)) {
            return parseLetStmt();
        }

        if (check(TokenType::Identifier) && tokens_[pos_ + 1].type == TokenType::Eq) {
            return parseAssignStmt();
        }

        return parseExprStmt();
    }

    StmtPtr Parser::parseLetStmt() {
        expect(TokenType::Let, "expected 'let' keyword");

        std::string name = expect(TokenType::Identifier, "expected variable name").lexeme;
        expect(TokenType::Colon, "expected ':' after variable name");

        TypeRef type = parseTypeRef();
        expect(TokenType::Eq, "expected '=' after variable type");

        ExprPtr init = parseExpr();

        if (check(TokenType::NewLine)) 
            advance();

        auto stmt = std::make_unique<LetStmt>();

        stmt->name = std::move(name);
        stmt->type = type;
        stmt->init = std::move(init);
        return stmt;
    }

    StmtPtr Parser::parseExprStmt() {
        auto expr = parseExpr();

        auto stmt = std::make_unique<ExprStmt>();

        stmt->line = expr->line;
        stmt->column = expr->column;
        stmt->expr = std::move(expr);

        if (check(TokenType::NewLine)) {
            advance();
        }

        return stmt;
    }

    StmtPtr Parser::parseReturnStmt() {
        const Token& ret = expect(TokenType::Return, "expected 'return' keyword");

        auto stmt = std::make_unique<ReturnStmt>();

        stmt->line = ret.line;
        stmt->column = ret.column;
        stmt->value = parseExpr();

        if (check(TokenType::NewLine)) {
            advance();
        }

        return stmt;
    }

    StmtPtr Parser::parseAssignStmt() {
        std::string name = expect(TokenType::Identifier, "expected variable name").lexeme;
        expect(TokenType::Eq, "expected '=' after variable name");

        ExprPtr value = parseExpr();
        if (check(TokenType::NewLine))
            advance();

        auto stmt = std::make_unique<AssignStmt>();
        stmt->name = std::move(name);
        stmt->value = std::move(value);
        return stmt;
    }

    ExprPtr Parser::parseExpr() {
        return parseAddExpr();
    }

    ExprPtr Parser::parseIntLit() {
        const Token& t = expect(TokenType::IntLit, "expected integer literal");

        auto expr = std::make_unique<IntLitExpr>();

        expr->line = t.line;
        expr->column = t.column;
        expr->value = std::stoll(t.lexeme);

        return expr;
    }

    ExprPtr Parser::parseAddExpr() {
        ExprPtr left = parseMulExpr();

        while (check(TokenType::Plus) || check(TokenType::Minus)) {
            std::string op = peek().lexeme;
            advance();

            ExprPtr right = parseMulExpr();

            
            auto bin = std::make_unique<BinaryExpr>();
            bin->op    = op;
            bin->lhs   = std::move(left);
            bin->rhs   = std::move(right);

            left = std::move(bin);
        }
        
        return left;
    }

    ExprPtr Parser::parseMulExpr() {
        ExprPtr left = parsePrimaryExpr();

        while (check(TokenType::Star) || check(TokenType::Slash) || check(TokenType::Percent)) {
            std::string op = peek().lexeme;
            advance();

            ExprPtr right = parsePrimaryExpr();

            auto bin = std::make_unique<BinaryExpr>();
            bin->op    = op;
            bin->lhs   = std::move(left);
            bin->rhs   = std::move(right);

            left = std::move(bin);
        }

        return left;
    }

    ExprPtr Parser::parsePrimaryExpr() {
        if (check(TokenType::IntLit))
            return parseIntLit();

        if (check(TokenType::LParen)) {
            advance();
            ExprPtr e = parseExpr();
            expect(TokenType::RParen, "expected ')'");
            return e;
        }

        if (check(TokenType::Identifier)) {
            if (tokens_[pos_ + 1].type == TokenType::LParen)
                return parseCallorIdent();

            const Token& t = advance();

            auto expr = std::make_unique<IdentExpr>();
            expr->name   = t.lexeme;
            expr->line   = t.line;
            expr->column = t.column;

            return expr;
        }

        const Token& t = peek();
        throw std::runtime_error(
            "expected expression at line " + std::to_string(t.line) +
            ", column " + std::to_string(t.column)
        );
    }

    ExprPtr Parser::parseCallorIdent() {
        const Token& name = expect(TokenType::Identifier, "expected identifier");

        if (!check(TokenType::LParen)) {
            throw std::runtime_error(
                "bare identifier '" + name.lexeme + "' not supported yet"
            );
        }

        advance();

        auto call = std::make_unique<CallExpr>();

        call->line = name.line;
        call->column = name.column;
        call->callee = name.lexeme;

        while (!check(TokenType::RParen) && !check(TokenType::Eof)) {
            call->args.push_back(parseExpr());

            if (!check(TokenType::RParen)) {
                expect(TokenType::Comma, "expected ',' between arguments");
            }
        }

        expect(TokenType::RParen, "expected ')' after arguments");

        return call;
    }
}