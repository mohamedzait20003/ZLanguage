#pragma once

#include <string>
#include <string_view>

namespace ZCompiler {
    enum class TokenType {
        Fn,
        Let,
        Return,
        Identifier,
        StaticCast,

        True,
        False,
        Null,
        Nullptr,

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
        Char,
        String,

        IntLit,
        FloatLit,
        DoubleLit,
        CharLit,
        StringLit,

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
        Break,
        Continue,
        
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