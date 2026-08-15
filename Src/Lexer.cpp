#include "Lexer.h"

#include <cctype>
#include <string>
#include <cstdint>
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
        else if(lexeme == "int32")
            type = TokenType::Int32;
        else if(lexeme == "int64")
            type = TokenType::Int64;
        else if(lexeme == "int128")
            type = TokenType::Int128;
        else if(lexeme == "float")
            type = TokenType::Float;
        else if(lexeme == "float16")
            type = TokenType::Float16;
        else if(lexeme == "float32")
            type = TokenType::Float32;
        else if(lexeme == "float64")
            type = TokenType::Float64;
        else if(lexeme == "double")
            type = TokenType::Double;
        else if(lexeme == "bool")
            type = TokenType::Bool;
        else if(lexeme == "character")
            type = TokenType::Character;
        else if(lexeme == "string")
            type = TokenType::String;
        else if(lexeme == "true")
            type = TokenType::True;
        else if(lexeme == "false")
            type = TokenType::False;
        else if(lexeme == "static_cast")
            type = TokenType::StaticCast;
        else if(lexeme == "dynamic_cast")
            type = TokenType::DynamicCast;
        else if(lexeme == "dynamic")
            type = TokenType::Dynamic;
        else if(lexeme == "null")
            type = TokenType::Null;
        else if(lexeme == "let")
            type = TokenType::Let;
        else if(lexeme == "if")
            type = TokenType::If;
        else if(lexeme == "switch")
            type = TokenType::Switch;
        else if(lexeme == "case")
            type = TokenType::Case;
        else if(lexeme == "default")
            type = TokenType::Default;
        else if(lexeme == "for")
            type = TokenType::For;
        else if(lexeme == "else")
            type = TokenType::Else;
        else if(lexeme == "while")
            type = TokenType::While;
        else if (lexeme == "do")
            type = TokenType::Do;
        else if (lexeme == "along")
            type = TokenType::Along;
        else if(lexeme == "break")
            type = TokenType::Break;
        else if(lexeme == "continue")
            type = TokenType::Continue;
        else if(lexeme == "using")
            type = TokenType::Using;
        else if(lexeme == "namespace")
            type = TokenType::Namespace;
        else if(lexeme == "return")
            type = TokenType::Return;

        return makeToken(type, std::move(lexeme), startLine, StartColumn);
    }

    Token Lexer::scanNumber(int startLine, int startColumn) {
        const std::size_t start = pos_ - 1;

        while (!isAtEnd() && std::isdigit(static_cast<unsigned char>(peek()))) {
            advance();
        }

        if(!isAtEnd() && peek() == '.' && std::isdigit(static_cast<unsigned char>(peek(1)))) {
            advance();

            while (!isAtEnd() && std::isdigit(static_cast<unsigned char>(peek())))
                advance();

            if (!isAtEnd() && (peek() == 'f' || peek() == 'F')) {
                advance();
                return makeToken(TokenType::FloatLit, std::string(source_.substr(start, pos_ - start)), startLine, startColumn);
            }

            return makeToken(TokenType::DoubleLit, std::string(source_.substr(start, pos_ - start)), startLine, startColumn);
        }

        return makeToken(TokenType::IntLit, std::string(source_.substr(start, pos_ - start)), startLine, startColumn);
    }

    Token Lexer::scanCharLit(int startLine, int startColumn){
        if (isAtEnd()) {
            throw std::runtime_error(
                "lex error: unterminated character literal at line " +
                std::to_string(startLine) + ", column " + std::to_string(startColumn)
            );
        }

        uint32_t codepoint;

        if(peek() == '\\'){
            advance();

            if (isAtEnd()) {
                throw std::runtime_error(
                    "lex error: unterminated character literal at line " +
                    std::to_string(startLine) + ", column " + std::to_string(startColumn)
                );
            }

            char esc = advance();

            switch (esc) {
                case 'n': 
                    codepoint = '\n'; 
                    break;
                case 't': 
                    codepoint = '\t';
                    break;
                case 'r': 
                    codepoint = '\r'; 
                    break;
                case '\\': 
                    codepoint = '\\'; 
                    break;
                case '\'': 
                    codepoint = '\'';
                    break;
                case '"': 
                    codepoint = '"'; 
                    break;
                default:
                    throw std::runtime_error(
                        std::string("unknown escape '\\") + esc
                        + "' at line " + std::to_string(startLine)
                    );
            }
        } else {
            codepoint = static_cast<unsigned char>(advance());
        }

        if (!match('\'')) {
            throw std::runtime_error(
                "lex error: unterminated character literal at line " +
                std::to_string(startLine) + ", column " + std::to_string(startColumn)
            );
        }

        return makeToken(TokenType::CharLit, std::to_string(codepoint), startLine, startColumn);
    }
    
    Token Lexer::scanStringLit(int startLine, int startColumn) {
        std::string value;

        while (!isAtEnd() && peek() != '"') {
            if(peek() == '\n') {
                throw std::runtime_error(
                    "lex error: unterminated string literal at line " +
                    std::to_string(startLine) + ", column " + std::to_string(startColumn)
                );
            }

            char c = advance();
            if (c == '\\') {
                if (isAtEnd()) {
                    throw std::runtime_error(
                        "lex error: unterminated string literal at line " +
                        std::to_string(startLine) + ", column " + std::to_string(startColumn)
                    );
                }

                char esc = advance();

                switch (esc) {
                    case 'n': 
                        value += '\n'; 
                        break;
                    case 't': 
                        value += '\t';
                        break;
                    case 'r': 
                        value += '\r'; 
                        break;
                    case '\\': 
                        value += '\\'; 
                        break;
                    case '\'': 
                        value += '\'';
                        break;
                    case '"': 
                        value += '"'; 
                        break;
                    default:
                        throw std::runtime_error(
                            std::string("unknown escape '\\") + esc
                            + "' at line " + std::to_string(startLine)
                        );
                }
            } else {
                value += c;
            }
        }

        if (isAtEnd()) {
            throw std::runtime_error(
                "lex error: unterminated string literal at line " +
                std::to_string(startLine) + ", column " + std::to_string(startColumn)
            );
        }

        advance();
        return makeToken(TokenType::StringLit, std::move(value), startLine, startColumn);
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
            case ';':
                return makeToken(TokenType::Semicolon, ";", startLine, startColumn);
            case ':':
                return makeToken(TokenType::Colon, ":", startLine, startColumn);
            case '?':
                return makeToken(TokenType::Question, "?", startLine, startColumn);
            case '.':
                return makeToken(TokenType::Dot, ".", startLine, startColumn);
            case '=':
                if (match('='))
                    return makeToken(TokenType::EqEq, "==", startLine, startColumn);

                return makeToken(TokenType::Eq, "=", startLine, startColumn);
            case '+':
                return makeToken(TokenType::Plus, "+", startLine, startColumn);
            case '-':
                if (match('>'))
                    return makeToken(TokenType::Arrow, "->", startLine, startColumn);
                    
                return makeToken(TokenType::Minus, "-", startLine, startColumn);
            case '*':
                return makeToken(TokenType::Star, "*", startLine, startColumn);
            case '/':
                return makeToken(TokenType::Slash, "/", startLine, startColumn);
            case '%':
                return makeToken(TokenType::Percent, "%", startLine, startColumn);
            case '<':
                if (match('='))
                    return makeToken(TokenType::LessEq, "<=", startLine, startColumn);
                    
                return makeToken(TokenType::Less, "<", startLine, startColumn);
            case '>':
                if (match('='))
                    return makeToken(TokenType::GreaterEq, ">=", startLine, startColumn);

                return makeToken(TokenType::Greater, ">", startLine, startColumn);
            case '!':
                if (match('='))
                    return makeToken(TokenType::NotEq, "!=", startLine, startColumn);

                return makeToken(TokenType::Not, "!", startLine, startColumn);
            case '&':
                if (match('&'))
                    return makeToken(TokenType::And, "&&", startLine, startColumn);

                throw std::runtime_error(
                    std::string("lex error: unexpected '&' (did you mean '&&'?) at line ") +
                    std::to_string(startLine) + ", column " + std::to_string(startColumn)
                );
            case '|':
                if (match('|'))
                    return makeToken(TokenType::Or, "||", startLine, startColumn);

                throw std::runtime_error(
                    std::string("lex error: unexpected '|' (did you mean '||'?) at line ") +
                    std::to_string(startLine) + ", column " + std::to_string(startColumn)
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

            if (c == '#') {
                while (!isAtEnd() && peek() != '\n')
                    advance();
                continue;
            }

            if (c == '\'') {
                tokens.push_back(scanCharLit(startLine, startColumn));
                continue;
            }

            if (c == '"') {
                tokens.push_back(scanStringLit(startLine, startColumn));
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
            case TokenType::FloatLit:    
                return "FloatLit";
            case TokenType::DoubleLit:
                return "DoubleLit";
            case TokenType::CharLit:
                return "CharLit";
            case TokenType::StringLit:
                return "StringLit";
            case TokenType::True:
                return "True";
            case TokenType::False:
                return "False";
            case TokenType::StaticCast:
                return "StaticCast";
            case TokenType::Identifier:  
                return "Identifier";
            case TokenType::Fn:          
                return "Fn";
            case TokenType::Int:         
                return "Int";
            case TokenType::Int32:      
                return "Int32";
            case TokenType::Int64:      
                return "Int64";
            case TokenType::Int128:
                return "Int128";
            case TokenType::Float:
                return "Float";
            case TokenType::Float16:
                return "Float16";
            case TokenType::Float32:
                return "Float32";
            case TokenType::Float64:
                return "Float64";
            case TokenType::Double:
                return "Double";
            case TokenType::Bool:
                return "Bool";
            case TokenType::Character:
                return "Character";
            case TokenType::String:
                return "String";
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
            case TokenType::Colon:
                return "Colon";
            case TokenType::NewLine:     
                return "NewLine";
            case TokenType::Eof:         
                return "Eof";
            case TokenType::Let:
                return "Let";
            case TokenType::Eq:
                return "Eq";
            case TokenType::Plus:
                return "Plus";
            case TokenType::Minus:
                return "Minus";
            case TokenType::Star:
                return "Star";
            case TokenType::Slash:
                return "Slash";
            case TokenType::Percent:
                return "Percent";
            case TokenType::EqEq:
                return "EqEq";
            case TokenType::NotEq:
                return "NotEq";
            case TokenType::Less:
                return "Less";
            case TokenType::LessEq:
                return "LessEq";
            case TokenType::Greater:
                return "Greater";
            case TokenType::GreaterEq:
                return "GreaterEq";
            case TokenType::And:
                return "And";
            case TokenType::Or:
                return "Or";
            case TokenType::Not:
                return "Not";
            case TokenType::If:
                return "If";
            case TokenType::Else:
                return "Else";
            case TokenType::While:
                return "While";
            case TokenType::For:
                return "For";
            case TokenType::Switch:
                return "Switch";
            case TokenType::Case:
                return "Case";
            case TokenType::Default:
                return "Default";
            case TokenType::Semicolon:
                return "Semicolon";
            case TokenType::Do:
                return "Do";
            case TokenType::Along:
                return "Along";
            case TokenType::Break:
                return "Break";
            case TokenType::Continue:
                return "Continue";
            case TokenType::Dynamic:
                return "Dynamic";
            case TokenType::DynamicCast:
                return "DynamicCast";
            case TokenType::Null:
                return "Null";
            case TokenType::Question:
                return "Question";
            case TokenType::Dot:
                return "Dot";
            case TokenType::Using:
                return "Using";
            case TokenType::Namespace:
                return "Namespace";
        };

        return "<unknown>";
    }
}