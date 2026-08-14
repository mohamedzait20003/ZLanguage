#include "Parser.h"

#include <cctype>
#include <string>
#include <cstdint>
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
        const Token& t = peek();
        advance();

        switch(t.type) {
            case TokenType::Int:
                return TypeRef::Int;
            case TokenType::Int32:
                return TypeRef::Int32;
            case TokenType::Int64:
                return TypeRef::Int64;
            case TokenType::Int128:
                return TypeRef::Int128;
            case TokenType::Float16:
                return TypeRef::Float16;
            case TokenType::Float:
                return TypeRef::Float;
            case TokenType::Float32:
                return TypeRef::Float32;
            case TokenType::Double:
                return TypeRef::Double;
            case TokenType::Float64:
                return TypeRef::Float64;
            case TokenType::Bool:
                return TypeRef::Bool;
            case TokenType::Character:
                return TypeRef::Character;
            case TokenType::String:
                return TypeRef::String;
            case TokenType::Dynamic:
                return TypeRef::Dynamic;
            default:
                throw std::runtime_error("expected type at line " + std::to_string(t.line) + ", column " + std::to_string(t.column));
        }
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
        while (!check(TokenType::RParen) && !check(TokenType::Eof)) {
            Param p;
            p.name = expect(TokenType::Identifier, "expected parameter name").lexeme;
            expect(TokenType::Colon, "expected ':' after parameter name");
            p.type = parseTypeRef();
            decl->params.push_back(std::move(p));
            if (!check(TokenType::RParen))
                expect(TokenType::Comma, "expected ',' between parameters");
        }
        expect(TokenType::RParen, "expected ')' after parameter list");
        expect(TokenType::Arrow, "expected '->' after parameter list");

        decl->returnType = parseTypeRef();

        skipNewlines();
        decl->body = parseBlock();

        return decl;
    }

    StmtPtr Parser::parseStmt() {
        skipNewlines();

        if (check(TokenType::Return))
            return parseReturnStmt();

        if (check(TokenType::Let))
            return parseLetStmt();

        if (check(TokenType::If))
            return parseIfStmt();

        if (check(TokenType::While))
            return parseWhileStmt();

        if (check(TokenType::Do))
            return parseDoStmt();

        if (check(TokenType::Switch))
            return parseSwitchStmt();
        
        if (check(TokenType::For))
            return parseForStmt();
        
        if (check(TokenType::Break))
            return parseBreakStmt();
        
        if (check(TokenType::Continue))
            return parseContinueStmt();

        if (check(TokenType::Identifier) && tokens_[pos_ + 1].type == TokenType::Eq)
            return parseAssignStmt();

        return parseExprStmt();
    }

    StmtPtr Parser::parseLetStmt() {
        const Token& letTok = expect(TokenType::Let, "expected 'let' keyword");

        std::string name = expect(TokenType::Identifier, "expected variable name").lexeme;
        expect(TokenType::Colon, "expected ':' after variable name");

        TypeRef type = parseTypeRef();
        expect(TokenType::Eq, "expected '=' after variable type");

        ExprPtr init = parseExpr();

        if (check(TokenType::NewLine))
            advance();

        auto stmt    = std::make_unique<LetStmt>();
        stmt->line   = letTok.line;
        stmt->column = letTok.column;
        stmt->name   = std::move(name);
        stmt->type   = type;
        stmt->init   = std::move(init);
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

    StmtPtr Parser::parseIfStmt() {
        const Token& t = expect(TokenType::If, "expected 'if'");
        expect(TokenType::LParen, "expected '(' after 'if'");
    

        ExprPtr cond = parseExpr();
        expect(TokenType::RParen, "expected ')' after condition");
        skipNewlines();

        auto thenBlock = parseBlock();
        auto stmt = std::make_unique<IfStmt>();

        stmt->line       = t.line;
        stmt->column     = t.column;
        stmt->cond       = std::move(cond);
        stmt->thenBranch = std::move(thenBlock);
        skipNewlines();
    

        if (check(TokenType::Else)) {
            advance();
            skipNewlines();
            stmt->elseBranch = parseBlock();
        }
        
        return stmt;
    }

    StmtPtr Parser::parseSwitchStmt() {
        const Token& t = expect(TokenType::Switch, "expected 'switch'");
        expect(TokenType::LParen, "expected '(' after 'switch'");

        ExprPtr scrutinee = parseExpr();
        expect(TokenType::RParen, "expected ')' after switch scrutinee");
        expect(TokenType::LBrace, "expected '{' to start switch body");
        skipNewlines();

        auto stmt = std::make_unique<SwitchStmt>();
        stmt->line     = t.line;
        stmt->column   = t.column;
        stmt->scrutinee = std::move(scrutinee);

        while (!check(TokenType::RBrace) && !check(TokenType::Eof)) {
            if (check(TokenType::Case)) {
                advance();

                ExprPtr val = parseExpr();
                expect(TokenType::Colon, "expected ':' after case value");
                skipNewlines();

                auto body = parseBlock();

                CaseArm arm;
                arm.value = std::move(val);
                arm.body  = std::move(body);
                stmt->cases.push_back(std::move(arm));

            } else if (check(TokenType::Default)) {
                const Token& def = advance();
                const int defLine = def.line;
                const int defColumn = def.column;

                expect(TokenType::Colon, "expected ':' after 'default'");
                skipNewlines();

                if (stmt->defaultArm)
                    throw std::runtime_error(
                        "switch has more than one 'default' arm at line " +
                        std::to_string(defLine) + ", column " + std::to_string(defColumn)
                    );

                stmt->defaultArm = parseBlock();

            } else {
                const Token& bad = peek();

                throw std::runtime_error(
                    "expected 'case' or 'default' in switch at line " +
                    std::to_string(bad.line) + ", column " + std::to_string(bad.column)
                );
            }

            skipNewlines();
        }

        expect(TokenType::RBrace, "expected '}' to close switch body");
        return stmt;
    }

    StmtPtr Parser::parseDoStmt() {
        const Token& t = expect(TokenType::Do, "expected 'do' keyword");
        skipNewlines();

        auto body = parseBlock();
        skipNewlines(); 

        expect(TokenType::Along, "expected 'along' after do-block");
        expect(TokenType::LParen, "expected '(' after 'along'");

        ExprPtr cond = parseExpr();
        expect(TokenType::RParen, "expected ')' after along condition");

        if (check(TokenType::NewLine))
            advance();

        auto stmt = std::make_unique<DoStmt>();

        stmt->line   = t.line;
        stmt->column = t.column;
        stmt->cond   = std::move(cond);
        stmt->body   = std::move(body);

        return stmt;
    }

    StmtPtr Parser::parseForStmt() {
        const Token& t = expect(TokenType::For, "expected 'for' keyword");
        expect(TokenType::LParen, "expected '(' after 'for'");

        StmtPtr init;

        if (check(TokenType::Let))
            init = parseLetStmt();
        else
            init = parseAssignStmt();

        expect(TokenType::Semicolon, "expected ';' after loop initializer");

        ExprPtr cond = parseExpr();
        expect(TokenType::Semicolon, "expected ';' after loop condition");

        StmtPtr step = parseAssignStmt();
        expect(TokenType::RParen, "expected ')' after for loop clauses");

        skipNewlines();
        auto body = parseBlock();

        auto stmt = std::make_unique<ForStmt>();
        
        stmt->line = t.line;
        stmt->column = t.column;
        stmt->init = std::move(init);
        stmt->cond = std::move(cond);
        stmt->step = std::move(step);
        stmt->body = std::move(body);

        return stmt;
    }

    StmtPtr Parser::parseWhileStmt() {
        const Token& t = expect(TokenType::While, "expected 'while'");
        expect(TokenType::LParen, "expected '(' after 'while'");
    

        ExprPtr cond = parseExpr();
        expect(TokenType::RParen, "expected ')' after condition");
        skipNewlines();

        auto body = parseBlock();
        auto stmt = std::make_unique<WhileStmt>();

        stmt->line   = t.line;
        stmt->column = t.column;
        stmt->cond   = std::move(cond);
        stmt->body   = std::move(body);

        return stmt;
    }

    StmtPtr Parser::parseBreakStmt() {
        const Token& t = expect(TokenType::Break, "expected 'break' keyword");

        auto stmt = std::make_unique<BreakStmt>();
        stmt->line = t.line;
        stmt->column = t.column;

        if (check(TokenType::NewLine))
            advance();

        return stmt;
    }

    StmtPtr Parser::parseContinueStmt() {
        const Token& t = expect(TokenType::Continue, "expected 'continue' keyword");

        auto stmt = std::make_unique<ContinueStmt>();
        stmt->line = t.line;
        stmt->column = t.column;

        if (check(TokenType::NewLine))
            advance();

        return stmt;
    }
    
    // Expressions
    ExprPtr Parser::parseExpr() {
        return parseTernaryExpr();
    }

    ExprPtr Parser::parseTernaryExpr() {
        ExprPtr cond = parseOrExpr();

        if (!check(TokenType::Question))
            return cond;

        const Token& q = advance();

        ExprPtr thenExpr = parseExpr();
        expect(TokenType::Colon, "expected ':' in ternary expression");

        ExprPtr elseExpr = parseTernaryExpr();
        auto expr = std::make_unique<TernaryExpr>();

        expr->line = q.line;
        expr->column = q.column;
        expr->cond = std::move(cond);
        expr->thenExpr = std::move(thenExpr);
        expr->elseExpr = std::move(elseExpr);
        return expr;
    }

    ExprPtr Parser::parseUnaryExpr() {
        if (check(TokenType::Not) || check(TokenType::Minus)) {
            const Token& t = advance();
            auto expr     = std::make_unique<UnaryExpr>();
            expr->op      = t.lexeme;
            expr->line    = t.line;
            expr->column  = t.column;
            expr->operand = parseUnaryExpr();
            return expr;
        }
        return parseCastExpr();
    }

    ExprPtr Parser::parseCastExpr() {
        const bool isStatic  = check(TokenType::StaticCast);
        const bool isDynamic = check(TokenType::DynamicCast);

        if (!isStatic && !isDynamic)
            return parsePrimaryExpr();

        const Token& kw = advance();
        const int line = kw.line, column = kw.column;
        const char* name = isStatic ? "static_cast" : "dynamic_cast";

        expect(TokenType::Less, std::string(std::string("expected '<' after '") + name + "'").c_str());
        TypeRef target = parseTypeRef();
        expect(TokenType::Greater, std::string(std::string("expected '>' after cast target type in '") + name + "'").c_str());
        expect(TokenType::LParen, std::string(std::string("expected '(' after '") + name + "<T>'").c_str());

        ExprPtr operand = parseExpr();
        expect(TokenType::RParen, "expected ')' after cast operand");

        if (isStatic) {
            auto expr        = std::make_unique<CastExpr>();
            expr->line       = line;
            expr->column     = column;
            expr->targetType = target;
            expr->operand    = std::move(operand);
            return expr;
        }

        auto expr        = std::make_unique<DynCastExpr>();
        expr->line       = line;
        expr->column     = column;
        expr->targetType = target;
        expr->operand    = std::move(operand);
        return expr;
    }

    ExprPtr Parser::parsePrimaryExpr() {
        if (check(TokenType::IntLit))
            return parseIntLit();
        
        if (check(TokenType::FloatLit))
            return parseFloatLit();
        
        if (check(TokenType::DoubleLit))
            return parseDoubleLit();
        
        if (check(TokenType::True) || check(TokenType::False))
            return parseBoolLit();
        
        if (check(TokenType::CharLit))
            return parseCharLit();
        
        if (check(TokenType::StringLit))
            return parseStringLit();

        if (check(TokenType::Null)) {
            const Token& t = advance();
            auto expr    = std::make_unique<NullLitExpr>();
            expr->line   = t.line;
            expr->column = t.column;
            return expr;
        }

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
            auto expr      = std::make_unique<IdentExpr>();
            expr->name     = t.lexeme;
            expr->line     = t.line;
            expr->column   = t.column;
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
            if (!check(TokenType::RParen))
                expect(TokenType::Comma, "expected ',' between arguments");
        }

        expect(TokenType::RParen, "expected ')' after arguments");
        return call;
    }

    ExprPtr Parser::parseCompareExpr() {
        ExprPtr left = parseAddExpr();

        while (check(TokenType::Less) || check(TokenType::LessEq) || check(TokenType::Greater) || check(TokenType::GreaterEq) || check(TokenType::EqEq) || check(TokenType::NotEq)) {
            const Token& opTok = peek();
            std::string op = opTok.lexeme;
            advance();
            ExprPtr right = parseAddExpr();
            auto bin = std::make_unique<BinaryExpr>();
            bin->line = opTok.line;
            bin->column = opTok.column;
            bin->op = op;
            bin->lhs = std::move(left);
            bin->rhs = std::move(right);
            left = std::move(bin);
        }

        return left;
    }

    ExprPtr Parser::parseIntLit() {
        const Token& t = expect(TokenType::IntLit, "expected integer literal");
        auto expr    = std::make_unique<IntLitExpr>();
        expr->line   = t.line;
        expr->column = t.column;
        expr->value  = std::stoll(t.lexeme);
        return expr;
    }

    ExprPtr Parser::parseCharLit() {
        const Token& t = expect(TokenType::CharLit, "expected character literal");
        auto expr = std::make_unique<CharLitExpr>();
        expr->line = t.line;
        expr->column = t.column;
        expr->value = static_cast<uint32_t>(std::stoul(t.lexeme));
        return expr;
    }

    ExprPtr Parser::parseBoolLit() {
        const Token& t = advance();
        auto expr    = std::make_unique<BoolLitExpr>();
        expr->line   = t.line;
        expr->column = t.column;
        expr->value  = (t.type == TokenType::True);
        return expr;
    }

    ExprPtr Parser::parseFloatLit() {
        const Token& t = expect(TokenType::FloatLit, "expected float literal");
        std::string raw = t.lexeme;

        if (!raw.empty() && (raw.back() == 'f' || raw.back() == 'F'))
            raw.pop_back();

        auto expr = std::make_unique<FloatLitExpr>();
        expr->line = t.line;
        expr->column = t.column;
        expr->value = std::stof(raw);
        return expr;
    }

    ExprPtr Parser::parseDoubleLit() {
        const Token& t = expect(TokenType::DoubleLit, "expected double literal");
        auto expr = std::make_unique<DoubleLitExpr>();
        expr->line = t.line;
        expr->column = t.column;
        expr->value = std::stod(t.lexeme);
        return expr;
    }

    ExprPtr Parser::parseStringLit() {
        const Token& t = expect(TokenType::StringLit, "expected string literal");
        auto expr = std::make_unique<StringLitExpr>();
        expr->line = t.line;
        expr->column = t.column;
        expr->value = t.lexeme;
        return expr;
    }

    ExprPtr Parser::parseOrExpr() {
        ExprPtr left = parseAndExpr();

        while (check(TokenType::Or)) {
            const Token& opTok = peek();
            std::string op = opTok.lexeme;
            advance();

            ExprPtr right = parseAndExpr();
            auto bin = std::make_unique<BinaryExpr>();
            bin->line = opTok.line;
            bin->column = opTok.column;
            bin->op = op;
            bin->lhs = std::move(left);
            bin->rhs = std::move(right);
            left = std::move(bin);
        }

        return left;
    }

    ExprPtr Parser::parseAndExpr() {
        ExprPtr left = parseCompareExpr();

        while (check(TokenType::And)) {
            const Token& opTok = peek();
            std::string op = opTok.lexeme;

            advance();
            ExprPtr right = parseCompareExpr();
            auto bin = std::make_unique<BinaryExpr>();
            bin->line = opTok.line;
            bin->column = opTok.column;
            bin->op = op;
            bin->lhs = std::move(left);
            bin->rhs = std::move(right);
            left = std::move(bin);
        }

        return left;
    }

    ExprPtr Parser::parseAddExpr() {
        ExprPtr left = parseMulExpr();

        while (check(TokenType::Plus) || check(TokenType::Minus)) {
            const Token& opTok = peek();
            std::string op = opTok.lexeme;

            advance();
            ExprPtr right = parseMulExpr();
            auto bin = std::make_unique<BinaryExpr>();
            bin->line = opTok.line;
            bin->column = opTok.column;
            bin->op = op;
            bin->lhs = std::move(left);
            bin->rhs = std::move(right);
            left = std::move(bin);
        }

        return left;
    }

    ExprPtr Parser::parseMulExpr() {
        ExprPtr left = parseUnaryExpr();

        while (check(TokenType::Star) || check(TokenType::Slash) || check(TokenType::Percent)) {
            const Token& opTok = peek();
            std::string op = opTok.lexeme;

            advance();
            ExprPtr right = parseUnaryExpr();
            auto bin = std::make_unique<BinaryExpr>();
            bin->line = opTok.line;
            bin->column = opTok.column;
            bin->op = op;
            bin->lhs = std::move(left);
            bin->rhs = std::move(right);
            left = std::move(bin);
        }

        return left;
    }
}