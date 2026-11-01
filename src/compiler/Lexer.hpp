#ifndef LEXER_HPP
#define LEXER_HPP

#include <string>
#include <vector>
#include <cctype>
#include <cstdio>
#include <unordered_map>
#include "Token.hpp"
#include "Error.hpp"

class Lexer {
private:
    std::vector<std::string> lines;
    ErrorReporter& errorReporter;
    std::vector<Token> tokens;

    size_t lineIdx = 0;
    size_t charIdx = 0;
    int currentLine = 1;

    // Tabela de palavras-chave da linguagem
    const std::unordered_map<std::string, NsTokenType> keywords = {
        {"number", NsTokenType::KEYWORD_NUMBER},
        {"float", NsTokenType::KEYWORD_FLOAT},
        {"double", NsTokenType::KEYWORD_DOUBLE},
        {"string", NsTokenType::KEYWORD_STRING},
        {"bool", NsTokenType::KEYWORD_BOOL},
        {"handle", NsTokenType::KEYWORD_HANDLE},
        {"fn", NsTokenType::KEYWORD_FN},
        {"struct", NsTokenType::KEYWORD_STRUCT},
        {"while", NsTokenType::KEYWORD_WHILE},
        {"for", NsTokenType::KEYWORD_FOR},
        {"if", NsTokenType::KEYWORD_IF},
        {"else", NsTokenType::KEYWORD_ELSE},
        {"return", NsTokenType::KEYWORD_RETURN},
        {"use", NsTokenType::KEYWORD_USE},
        {"true", NsTokenType::BOOL_LITERAL},
        {"false", NsTokenType::BOOL_LITERAL}
    };

    // ------------------------------------------------------------
    // <cctype> exige unsigned char: um char negativo (UTF-8) seria comportamento indefinido
    // ------------------------------------------------------------
    static bool isDigitCh(char c) { return std::isdigit(static_cast<unsigned char>(c)) != 0; }
    static bool isAlphaCh(char c) { return std::isalpha(static_cast<unsigned char>(c)) != 0; }
    static bool isAlnumCh(char c) { return std::isalnum(static_cast<unsigned char>(c)) != 0; }

    // ------------------------------------------------------------
    // UTF-8
    // ------------------------------------------------------------
    static size_t utf8Length(unsigned char lead) {
        if (lead < 0x80) return 1;
        if ((lead & 0xE0) == 0xC0) return 2;
        if ((lead & 0xF0) == 0xE0) return 3;
        if ((lead & 0xF8) == 0xF0) return 4;
        return 1; // byte solto, inválido
    }

    // Descodifica o caractere em s[i]. 'len' = bytes ocupados; false se a sequência for inválida
    static bool decodeUtf8(const std::string& s, size_t i, size_t& len, unsigned& cp) {
        unsigned char lead = static_cast<unsigned char>(s[i]);
        len = utf8Length(lead);
        if (len == 1) { cp = lead; return lead < 0x80; }
        if (i + len > s.size()) { len = 1; cp = lead; return false; }

        cp = (len == 2) ? (lead & 0x1Fu) : (len == 3) ? (lead & 0x0Fu) : (lead & 0x07u);
        for (size_t k = 1; k < len; ++k) {
            unsigned char cont = static_cast<unsigned char>(s[i + k]);
            if ((cont & 0xC0) != 0x80) { len = 1; cp = lead; return false; }
            cp = (cp << 6) | (cont & 0x3Fu);
        }
        return true;
    }

    // Espaços e caracteres de largura zero que editores e copiar/colar introduzem sem se verem:
    // NBSP, espaços Unicode, ZWSP/ZWNJ/ZWJ, LRM/RLM, separadores de linha, BOM (U+FEFF)
    static bool isInvisibleCodePoint(unsigned cp) {
        return cp == 0x00A0 || cp == 0x1680 || (cp >= 0x2000 && cp <= 0x200F) ||
               cp == 0x2028 || cp == 0x2029 || cp == 0x202F || cp == 0x205F ||
               cp == 0x2060 || cp == 0x3000 || cp == 0xFEFF;
    }

    // ------------------------------------------------------------
    // Navegação
    // ------------------------------------------------------------
    bool isAtEnd() const {
        return lineIdx >= lines.size();
    }

    char peek() const {
        if (isAtEnd()) return '\0';
        if (charIdx >= lines[lineIdx].size()) return '\n'; // quebra de linha simulada
        return lines[lineIdx][charIdx];
    }

    // Consome um byte. No fim da linha consome a quebra de linha simulada e SÓ ENTÃO muda de
    // linha, para um token nunca atravessar o fim da linha (antes "a" + "print" virava "aprint").
    char advance() {
        if (isAtEnd()) return '\0';

        if (charIdx >= lines[lineIdx].size()) {
            lineIdx++;
            charIdx = 0;
            currentLine++;
            return '\n';
        }

        return lines[lineIdx][charIdx++];
    }

    bool match(char expected) {
        if (isAtEnd()) return false;
        if (peek() != expected) return false;
        advance();
        return true;
    }

    // Consome um caractere invisível na posição atual; true se consumiu
    bool skipInvisible() {
        const std::string& line = lines[lineIdx];
        size_t len = 1;
        unsigned cp = 0;
        if (decodeUtf8(line, charIdx, len, cp) && isInvisibleCodePoint(cp)) {
            charIdx += len;
            return true;
        }
        return false;
    }

    void skipWhitespaceAndComments() {
        while (!isAtEnd()) {
            char c = peek();
            if (c == ' ' || c == '\r' || c == '\t' || c == '\n') {
                advance();
            } else if (c == '/' && charIdx + 1 < lines[lineIdx].size() && lines[lineIdx][charIdx + 1] == '/') {
                // Comentário: ignora o resto da linha
                lineIdx++;
                charIdx = 0;
                currentLine++;
            } else if (static_cast<unsigned char>(c) >= 0x80 && skipInvisible()) {
                // caractere invisível ignorado
            } else {
                break;
            }
        }
    }

    // Em vez de imprimir o byte cru (que pode ser invisível), mostra o código do caractere
    void reportInvalidCharacter(int tokenLine) {
        const std::string& line = lines[lineIdx];
        size_t len = 1;
        unsigned cp = 0;
        bool valid = decodeUtf8(line, charIdx, len, cp);

        char code[48];
        std::string description;
        if (!valid) {
            std::snprintf(code, sizeof(code), "byte 0x%02X", static_cast<unsigned>(static_cast<unsigned char>(line[charIdx])));
            description = std::string("byte inválido (") + code + ")";
        } else if (cp < 0x20 || cp == 0x7F) {
            std::snprintf(code, sizeof(code), "U+%04X", cp);
            description = std::string("caractere de controle ") + code;
        } else {
            std::snprintf(code, sizeof(code), "U+%04X", cp);
            description = "'" + line.substr(charIdx, len) + "' (" + code + ")";
        }

        errorReporter.report(ErrorCode::NS_08_InvalidCharacter,
                             "Caractere inválido detetado: " + description +
                             " na coluna " + std::to_string(charIdx + 1), tokenLine);

        for (size_t k = 0; k < len; ++k) advance();
    }

    // Recusa ficheiros que não são texto UTF-8, em vez de gerar um erro por byte
    bool rejectNonText() {
        if (!lines.empty()) {
            const std::string& first = lines[0];
            if (first.size() >= 2) {
                unsigned char b0 = static_cast<unsigned char>(first[0]);
                unsigned char b1 = static_cast<unsigned char>(first[1]);
                if ((b0 == 0xFF && b1 == 0xFE) || (b0 == 0xFE && b1 == 0xFF)) {
                    errorReporter.report(ErrorCode::NS_00_FileError,
                        "O ficheiro está codificado em UTF-16. Grave-o como UTF-8 "
                        "(no VS Code: canto inferior direito > 'Salvar com codificação' > UTF-8).", 0);
                    return true;
                }
            }
        }
        for (const auto& l : lines) {
            if (l.find('\0') != std::string::npos) {
                errorReporter.report(ErrorCode::NS_00_FileError,
                    "O ficheiro parece binário (contém bytes nulos), não código-fonte Nebula. "
                    "Se for texto, grave-o como UTF-8.", 0);
                return true;
            }
        }
        return false;
    }

    void stringLiteral() {
        int startLine = currentLine;
        std::string value = "";

        advance(); // aspa inicial

        while (peek() != '"' && !isAtEnd() && peek() != '\n') {
            value += advance();
        }

        if (isAtEnd() || peek() == '\n') {
            errorReporter.report(ErrorCode::NS_07_UnterminatedString, "String não terminada antes do fim da linha.", startLine);
            return;
        }

        advance(); // aspa final
        tokens.push_back(Token(NsTokenType::STRING_LITERAL, value, startLine));
    }

    void numberLiteral() {
        int startLine = currentLine;
        std::string value = "";
        bool isFloat = false;

        while (isDigitCh(peek())) {
            value += advance();
        }

        if (peek() == '.' && charIdx + 1 < lines[lineIdx].size() && isDigitCh(lines[lineIdx][charIdx + 1])) {
            isFloat = true;
            value += advance();
            while (isDigitCh(peek())) {
                value += advance();
            }
        }

        NsTokenType type = isFloat ? NsTokenType::FLOAT_LITERAL : NsTokenType::INT_LITERAL;
        tokens.push_back(Token(type, value, startLine));
    }

    void identifierOrKeyword() {
        int startLine = currentLine;
        std::string value = "";

        while (isAlnumCh(peek()) || peek() == '_') {
            value += advance();
        }

        auto it = keywords.find(value);
        NsTokenType type = (it != keywords.end()) ? it->second : NsTokenType::IDENTIFIER;

        tokens.push_back(Token(type, value, startLine));
    }

public:
    Lexer(std::vector<std::string> sourceLines, ErrorReporter& reporter)
        : lines(std::move(sourceLines)), errorReporter(reporter) {}

    std::vector<Token> scanTokens() {
        tokens.clear();
        lineIdx = 0;
        charIdx = 0;
        currentLine = 1;

        if (rejectNonText()) {
            tokens.push_back(Token(NsTokenType::END_OF_FILE, "", 1));
            return tokens;
        }

        while (!isAtEnd()) {
            skipWhitespaceAndComments();
            if (isAtEnd()) break;

            char c = peek();
            int tokenLine = currentLine;

            if (c == ':') { advance(); tokens.push_back(Token(NsTokenType::COLON, ":", tokenLine)); }
            else if (c == '=') {
                advance();
                if (match('=')) tokens.push_back(Token(NsTokenType::EQUAL_EQUAL, "==", tokenLine));
                else tokens.push_back(Token(NsTokenType::ASSIGN, "=", tokenLine));
            }
            else if (c == '!') {
                advance();
                if (match('=')) tokens.push_back(Token(NsTokenType::BANG_EQUAL, "!=", tokenLine));
                else tokens.push_back(Token(NsTokenType::BANG, "!", tokenLine));
            }
            else if (c == '-') {
                advance();
                if (match('>')) tokens.push_back(Token(NsTokenType::ARROW, "->", tokenLine));
                else tokens.push_back(Token(NsTokenType::MINUS, "-", tokenLine));
            }
            else if (c == ',') { advance(); tokens.push_back(Token(NsTokenType::COMMA, ",", tokenLine)); }
            else if (c == ';') { advance(); tokens.push_back(Token(NsTokenType::SEMICOLON, ";", tokenLine)); }
            else if (c == '.') { advance(); tokens.push_back(Token(NsTokenType::DOT, ".", tokenLine)); }
            else if (c == '(') { advance(); tokens.push_back(Token(NsTokenType::LPAREN, "(", tokenLine)); }
            else if (c == ')') { advance(); tokens.push_back(Token(NsTokenType::RPAREN, ")", tokenLine)); }
            else if (c == '{') { advance(); tokens.push_back(Token(NsTokenType::LBRACE, "{", tokenLine)); }
            else if (c == '}') { advance(); tokens.push_back(Token(NsTokenType::RBRACE, "}", tokenLine)); }
            else if (c == '[') { advance(); tokens.push_back(Token(NsTokenType::LBRACKET, "[", tokenLine)); }
            else if (c == ']') { advance(); tokens.push_back(Token(NsTokenType::RBRACKET, "]", tokenLine)); }
            else if (c == '>') {
                advance();
                if (match('=')) tokens.push_back(Token(NsTokenType::GREATER_EQUAL, ">=", tokenLine));
                else tokens.push_back(Token(NsTokenType::GREATER, ">", tokenLine));
            }
            else if (c == '<') {
                advance();
                if (match('=')) tokens.push_back(Token(NsTokenType::LESS_EQUAL, "<=", tokenLine));
                else tokens.push_back(Token(NsTokenType::LESS, "<", tokenLine));
            }
            else if (c == '&') {
                advance();
                if (match('&')) tokens.push_back(Token(NsTokenType::AMPERSAND_AMPERSAND, "&&", tokenLine));
                else errorReporter.report(ErrorCode::NS_08_InvalidCharacter,
                                          "Operador '&' incompleto; use '&&'.", tokenLine);
            }
            else if (c == '|') {
                advance();
                if (match('|')) tokens.push_back(Token(NsTokenType::PIPE_PIPE, "||", tokenLine));
                else errorReporter.report(ErrorCode::NS_08_InvalidCharacter,
                                          "Operador '|' incompleto; use '||'.", tokenLine);
            }
            else if (c == '+') { advance(); tokens.push_back(Token(NsTokenType::PLUS, "+", tokenLine)); }
            else if (c == '*') { advance(); tokens.push_back(Token(NsTokenType::STAR, "*", tokenLine)); }
            else if (c == '/') { advance(); tokens.push_back(Token(NsTokenType::SLASH, "/", tokenLine)); }
            else if (c == '%') { advance(); tokens.push_back(Token(NsTokenType::PERCENT, "%", tokenLine)); }
            else if (c == '"') { stringLiteral(); }
            else if (isDigitCh(c)) { numberLiteral(); }
            else if (isAlphaCh(c) || c == '_') { identifierOrKeyword(); }
            else { reportInvalidCharacter(tokenLine); }
        }

        tokens.push_back(Token(NsTokenType::END_OF_FILE, "", currentLine));
        return tokens;
    }
};

#endif // LEXER_HPP