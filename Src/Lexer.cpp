#include "Lexer.h"

#include <cctype>
#include <string>
#include <stdexcept>

namespace ZCompiler {
    Lexer::Lexer(std::string_view source) : source_(source) {}

    // Primitives
    bool Lexer::isAtEnd() const {
        return pos_ >= source_.size();
    }

    char Lexer::peek(std::size_t offset) const {
        const std::size_t target = pos_ + offset;

        if(target >= source_.size()) {
            return '\0';
        }

        return source_[target];
    }

    char Lexer::advance() {
        const char c = source_[pos_++];

        if (c == '\n') {
            ++line_;
            column_ = 1;
        } else {
            ++column_;
        }

        return c;
    }

    bool Lexer::match(char expected) {
        if(isAtEnd() || peek() != expected) {
            return false;
        }

        advance();
        return true;
    }

    // Emitter
    Token Lexer::makeToken(TokenType type, std::string lexeme, int line, int column) {
        return Token{type, std::move(lexeme), line, column};
    }

    // Scanners
    Token Lexer::scanIdentifierOrKeyword(int startLine, int StartColumn) {
        const std::size_t start = pos_ - 1;

        while (!isAtEnd() && (std::isalnum(static_cast<unsigned char>(peek())) || peek() == '_')) {
            advance();
        }

        std::string lexeme(source_.substr(start, pos_ - start));

        TokenType type = TokenType::Identifier;
        if(lexeme == "fn")
            type = TokenType::Fn;
        else if(lexeme == "int")
            type = TokenType::Int;
        else if(lexeme == "return")
            type = TokenType::Return;

        return makeToken(type, std::move(lexeme), startLine, StartColumn);
    }

    Token Lexer::scanNumber(int startLine, int startColumn) {
        const std::size_t start = pos_ - 1;

        while (!isAtEnd() && std::isdigit(static_cast<unsigned char>(peek()))) {
            advance();
        }

        std::string lexeme(source_.substr(start, pos_ - start));
        return makeToken(TokenType::IntLit, std::move(lexeme), startLine, startColumn);
    }

    Token Lexer::scanPunctuation(int startLine, int startColumn) {
        const char c = source_[pos_ - 1];

        switch (c) {
            case '(': 
                return makeToken(TokenType::LParen, "(", startLine, startColumn);
            case ')': 
                return makeToken(TokenType::RParen, ")", startLine, startColumn);
            case '{': 
                return makeToken(TokenType::LBrace, "{", startLine, startColumn);
            case '}':
                return makeToken(TokenType::RBrace, "}", startLine, startColumn);
            case ',':
                return makeToken(TokenType::Comma, ",", startLine, startColumn);
            case '-':
                if (match('>')) {
                    return makeToken(TokenType::Arrow, "->", startLine, startColumn);
                }

                throw std::runtime_error(
                    "lex error: unexpected '-' at line " + std::to_string(startLine) +
                    ", column " + std::to_string(startColumn)
                );
            default:
                throw std::runtime_error(
                    std::string("lex error: unknown character '") + c +
                    "' at line " + std::to_string(startLine) +
                    ", column " + std::to_string(startColumn)
                );
        }
    }

    std::vector<Token> Lexer::tokenize() {
        std::vector<Token> tokens;

        while(!isAtEnd()) {
            const int startLine = line_;
            const int startColumn = column_;
            const char c = advance();

            if (c == ' ' || c == '\t' || c == '\r') {
                continue;
            }

            if(c == '\n') {
                tokens.push_back(makeToken(TokenType::NewLine, "\\n", startLine, startColumn));
                continue;
            }

            if(std::isalpha(static_cast<unsigned char>(c)) || c == '_') {
                tokens.push_back(scanIdentifierOrKeyword(startLine, startColumn));
                continue;
            }

            if(std::isdigit(static_cast<unsigned char>(c))) {
                tokens.push_back(scanNumber(startLine, startColumn));
                continue;
            }

            tokens.push_back(scanPunctuation(startLine, startColumn));
        }

        tokens.push_back(makeToken(TokenType::Eof, "", line_, column_));
        return tokens;
    }

    std::string_view to_string(TokenType k) {
        switch (k) {
            case TokenType::IntLit:      
                return "IntLit";
            case TokenType::Identifier:  
                return "Identifier";
            case TokenType::Fn:          
                return "Fn";
            case TokenType::Int:         
                return "Int";
            case TokenType::Return:      
                return "Return";
            case TokenType::LParen:      
                return "LParen";
            case TokenType::RParen:      
                return "RParen";
            case TokenType::LBrace:      
                return "LBrace";
            case TokenType::RBrace:      
                return "RBrace";
            case TokenType::Arrow:
                return "Arrow";
            case TokenType::Comma:
                return "Comma";
            case TokenType::NewLine:     
                return "NewLine";
            case TokenType::Eof:         
                return "Eof";
        }
        return "<unknown>";
    }
}