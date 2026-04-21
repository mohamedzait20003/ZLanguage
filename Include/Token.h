#pragma once

#include <string>
#include <string_view>

namespace ZCompiler {
    enum class TokenType {
        IntLit,
        Identifier,

        Fn,
        Int,
        Return,

        LParen,
        RParen,
        LBrace,
        RBrace,
        Arrow,
        Comma,
        
        NewLine,
        Eof,
    };

    struct Token {
        TokenType type;
        std::string lexeme;
        int line;
        int column;
    };

    std::string_view to_string(TokenType K);
}