#pragma once

#include "Token.h"

#include <string>
#include <vector>
#include <string_view>

namespace ZCompiler {
    class Lexer {
        public:
            explicit Lexer(std::string source);
            std::vector<Token> tokenize();

        private:
            int line_ = 1;
            int column_ = 1;
            std::size_t pos_ = 0;
            std::string source_;
            
            // Primitives
            char advance();
            bool isAtEnd() const;
            bool match(char expected);
            char peek(std::size_t offset = 0) const;

            // Token Emitters
            Token makeToken(TokenType type, std::string lexeme, int line, int column);
            
            // Scanners
            Token scanNumber(int startLine, int startColumn);
            Token scanCharLit(int startLine, int startColumn);
            Token scanStringLit(int startLine, int startColumn);
            Token scanPunctuation(int startLine, int startColumn);
            Token scanIdentifierOrKeyword(int startLine, int startColumn);
    };
}
