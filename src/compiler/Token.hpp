#ifndef TOKEN_HPP
#define TOKEN_HPP

#include <string>
#include <string_view>

// Proteção contra macros do Windows
#ifdef _WIN32
#undef type
#endif

// Categorias de Tokens da linguagem Nebula Script
enum class NsTokenType {
    END_OF_FILE,
    INVALID,
    IDENTIFIER,
    INT_LITERAL,
    FLOAT_LITERAL,
    STRING_LITERAL,
    BOOL_LITERAL,
    NULL_LITERAL,
    KEYWORD_NUMBER,
    KEYWORD_FLOAT,
    KEYWORD_DOUBLE,
    KEYWORD_STRING,
    KEYWORD_BOOL,
    KEYWORD_HANDLE,
    KEYWORD_FN,
    KEYWORD_STRUCT,
    KEYWORD_WHILE,
    KEYWORD_FOR,
    KEYWORD_IF,
    KEYWORD_ELSE,
    KEYWORD_RETURN,
    KEYWORD_ELSEIF,
    KEYWORD_USE,
    COLON,
    ASSIGN,
    ARROW,
    COMMA,
    AMPERSAND_AMPERSAND,
    PIPE_PIPE,
    PERCENT,
    LPAREN,
    RPAREN,
    LBRACE,
    RBRACE,
    LBRACKET,
    RBRACKET,
    DOT,
    SEMICOLON,
    GREATER,
    GREATER_EQUAL,
    LESS,
    LESS_EQUAL,
    EQUAL_EQUAL,
    BANG,
    BANG_EQUAL,
    PLUS,
    MINUS,
    STAR,
    SLASH
};

// Estrutura leve para representar cada Token (usando tokenType para evitar conflito com o Windows)
struct Token {
    NsTokenType tokenType;
    std::string lexeme; 
    int line;           

    Token(NsTokenType tType, std::string lexeme, int line)
        : tokenType(tType), lexeme(std::move(lexeme)), line(line) {}
};

// Utilitário em string_view para depurar tokens
inline std::string_view tokenTypeToString(NsTokenType tType) {
    switch (tType) {
        case NsTokenType::END_OF_FILE:     return "END_OF_FILE";
        case NsTokenType::INVALID:         return "INVALID";
        case NsTokenType::IDENTIFIER:      return "IDENTIFIER";
        case NsTokenType::INT_LITERAL:     return "INT_LITERAL";
        case NsTokenType::FLOAT_LITERAL:   return "FLOAT_LITERAL";
        case NsTokenType::STRING_LITERAL:  return "STRING_LITERAL";
        case NsTokenType::BOOL_LITERAL:    return "BOOL_LITERAL";
        case NsTokenType::NULL_LITERAL:    return "NULL_LITERAL";
        case NsTokenType::KEYWORD_NUMBER:  return "KEYWORD_NUMBER";
        case NsTokenType::KEYWORD_FLOAT:   return "KEYWORD_FLOAT";
        case NsTokenType::KEYWORD_DOUBLE:   return "KEYWORD_DOUBLE";
        case NsTokenType::KEYWORD_STRING:  return "KEYWORD_STRING";
        case NsTokenType::KEYWORD_BOOL:    return "KEYWORD_BOOL";
        case NsTokenType::KEYWORD_HANDLE:   return "KEYWORD_HANDLE";
        case NsTokenType::KEYWORD_FN:      return "KEYWORD_FN";
        case NsTokenType::KEYWORD_STRUCT:  return "KEYWORD_STRUCT";
        case NsTokenType::KEYWORD_WHILE:   return "KEYWORD_WHILE";
        case NsTokenType::KEYWORD_FOR:     return "KEYWORD_FOR";
        case NsTokenType::KEYWORD_IF:      return "KEYWORD_IF";
        case NsTokenType::KEYWORD_ELSE:    return "KEYWORD_ELSE";
        case NsTokenType::KEYWORD_RETURN:  return "KEYWORD_RETURN";
        case NsTokenType::KEYWORD_ELSEIF:  return "KEYWORD_ELSEIF";
        case NsTokenType::KEYWORD_USE:     return "KEYWORD_USE";
        case NsTokenType::COLON:           return "COLON";
        case NsTokenType::ASSIGN:          return "ASSIGN";
        case NsTokenType::ARROW:           return "ARROW";
        case NsTokenType::COMMA:           return "COMMA";
        case NsTokenType::LPAREN:          return "LPAREN";
        case NsTokenType::RPAREN:          return "RPAREN";
        case NsTokenType::LBRACE:          return "LBRACE";
        case NsTokenType::RBRACE:          return "RBRACE";
        case NsTokenType::LBRACKET:        return "LBRACKET";
        case NsTokenType::RBRACKET:        return "RBRACKET";
        case NsTokenType::DOT:             return "DOT";
        case NsTokenType::SEMICOLON:       return "SEMICOLON";
        case NsTokenType::GREATER:         return "GREATER";
        case NsTokenType::GREATER_EQUAL:   return "GREATER_EQUAL";
        case NsTokenType::LESS:            return "LESS";
        case NsTokenType::LESS_EQUAL:      return "LESS_EQUAL";
        case NsTokenType::EQUAL_EQUAL:     return "EQUAL_EQUAL";
        case NsTokenType::BANG:            return "BANG";
        case NsTokenType::BANG_EQUAL:      return "BANG_EQUAL";
        case NsTokenType::PLUS:            return "PLUS";
        case NsTokenType::MINUS:           return "MINUS";
        case NsTokenType::STAR:            return "STAR";
        case NsTokenType::SLASH:           return "SLASH";
        case NsTokenType::AMPERSAND_AMPERSAND: return "AMPERSAND_AMPERSAND";
        case NsTokenType::PIPE_PIPE:       return "PIPE_PIPE";
        case NsTokenType::PERCENT:         return "PERCENT";
        default:                         return "UNKNOWN";
    }
}

#endif // TOKEN_HPP