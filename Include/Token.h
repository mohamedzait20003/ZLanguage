#pragma once

#include <string>
#include <string_view>

namespace ZCompiler {
    enum class TokenType {
        IntLit,
        Identifier,

        Fn,
        Int,
        Let,
        Return,

        LParen,
        RParen,
        LBrace,
        RBrace,
        Arrow,
        Comma,

        Eq,
        Plus,
        Minus,
        Star,
        Slash,
        Percent,

        EqEq,
        NotEq,
        Less,
        LessEq,
        Greater,
        GreaterEq,

        And,
        Or,
        Not,

        If,
        Else,
        Switch,
        Case,
        Default,

        For,
        While,
        Do,
        Along,
        
        NewLine,
        Semicolon,
        Colon,
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