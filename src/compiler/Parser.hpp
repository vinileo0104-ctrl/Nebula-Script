#ifndef PARSER_HPP
#define PARSER_HPP

#include <vector>
#include <string>
#include <memory>
#include <unordered_set>
#include <fstream>
#include <utility>
#include <filesystem>
#include <algorithm>

#include "Token.hpp"
#include "Error.hpp"
#include "Ast.hpp"
#include "Lexer.hpp"

class Parser {
private:
    std::shared_ptr<std::unordered_set<std::string>> loadedFiles;
    std::filesystem::path currentDir;
    std::vector<Token> tokens;
    size_t current = 0;
    ErrorReporter& errorReporter;

    Token peek() const {
        return tokens[current];
    }

    bool isAtEnd() const {
        return peek().tokenType == NsTokenType::END_OF_FILE;
    }

    Token advance() {
        if (!isAtEnd()) current++;
        return tokens[current - 1];
    }

    bool check(NsTokenType type) const {
        if (isAtEnd()) return false;
        return peek().tokenType == type;
    }

    bool match(NsTokenType type) {
        if (check(type)) {
            advance();
            return true;
        }
        return false;
    }

    Token consume(NsTokenType type, ErrorCode errorCode, const std::string& message) {
        if (check(type)) return advance();
        
        Token currentToken = peek();
        errorReporter.report(errorCode, message, currentToken.line);
        return currentToken;
    }

    void synchronize() {
        advance();
        while (!isAtEnd()) {
            if (tokens[current - 1].tokenType == NsTokenType::RBRACE) return;
            switch (peek().tokenType) {
                case NsTokenType::KEYWORD_NUMBER:
                case NsTokenType::KEYWORD_FLOAT:
                case NsTokenType::KEYWORD_DOUBLE:
                case NsTokenType::KEYWORD_STRING:
                case NsTokenType::KEYWORD_BOOL:
                case NsTokenType::KEYWORD_HANDLE:
                case NsTokenType::KEYWORD_FN:
                case NsTokenType::KEYWORD_STRUCT:
                case NsTokenType::KEYWORD_FOR:
                case NsTokenType::KEYWORD_WHILE:
                case NsTokenType::KEYWORD_RETURN:
                case NsTokenType::KEYWORD_IF:
                    return;
                default:
                    advance();
            }
        }
    }

    bool isTypeToken() const {
        return check(NsTokenType::KEYWORD_NUMBER) || 
               check(NsTokenType::KEYWORD_FLOAT) || 
               check(NsTokenType::KEYWORD_DOUBLE) ||
               check(NsTokenType::KEYWORD_STRING) || 
               check(NsTokenType::KEYWORD_BOOL) ||
               check(NsTokenType::KEYWORD_HANDLE);
    }

    bool isCustomTypeDeclaration() const {
        if (!check(NsTokenType::IDENTIFIER)) return false;
        size_t next = current + 1;
        if (next < tokens.size() &&
            tokens[next].tokenType == NsTokenType::IDENTIFIER) {
            return true;
        }
        bool hasArraySuffix = false;
        while (next + 1 < tokens.size() &&
               tokens[next].tokenType == NsTokenType::LBRACKET &&
               tokens[next + 1].tokenType == NsTokenType::RBRACKET) {
            hasArraySuffix = true;
            next += 2;
        }
        return hasArraySuffix && next < tokens.size() &&
               tokens[next].tokenType == NsTokenType::IDENTIFIER;
    }

    std::string parseTypeName() {
        if (!isTypeToken() && !check(NsTokenType::IDENTIFIER)) {
            errorReporter.report(ErrorCode::NS_03_SyntaxError,
                                 "Esperado um tipo válido.", peek().line);
            return "";
        }

        std::string type = advance().lexeme;
        while (match(NsTokenType::LBRACKET)) {
            consume(NsTokenType::RBRACKET, ErrorCode::NS_03_SyntaxError,
                    "Esperado ']' após '[' no tipo do array.");
            type += "[]";
        }
        return type;
    }

    void parseStatementsInto(std::vector<std::unique_ptr<Stmt>>& statements) {
        if (match(NsTokenType::KEYWORD_USE)) {
            auto importedStmts = parseUseStatements();
            for (auto& stmt : importedStmts) {
                if (stmt) {
                    statements.push_back(std::move(stmt));
                }
            }
        } else {
            auto stmt = parseDeclaration();
            if (stmt) {
                statements.push_back(std::move(stmt));
            }
        }
    }

    // Auxiliar para verificar se o ficheiro importado é binário/DLL nativa
    bool isNativeLibrary(const std::string& filename) const {
        std::string ext = std::filesystem::path(filename).extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
        return (ext == ".dll" || ext == ".so" || ext == ".dylib");
    }

    std::vector<std::unique_ptr<Stmt>> parseUseStatements() {
        Token stringToken = consume(NsTokenType::STRING_LITERAL, ErrorCode::NS_03_SyntaxError, "Esperado o caminho do ficheiro entre aspas após 'use'.");
        std::string filename = stringToken.lexeme;

        // Se for uma DLL nativa, emite o nó DllImportStmt para o compilador gerar o bytecode FFI
        if (isNativeLibrary(filename)) {
            std::vector<std::unique_ptr<Stmt>> stmts;
            stmts.push_back(std::make_unique<DllImportStmt>(filename));
            return stmts;
        }

        std::filesystem::path targetPath;
        if (currentDir.empty()) {
            targetPath = filename;
        } else {
            targetPath = currentDir / filename;
        }

        std::string canonicalPath;
        try {
            canonicalPath = std::filesystem::absolute(targetPath).lexically_normal().string();
        } catch (...) {
            canonicalPath = targetPath.string();
        }

        if (loadedFiles->count(canonicalPath)) {
            return {};
        }
        loadedFiles->insert(canonicalPath);

        std::ifstream file(targetPath);
        if (!file.is_open()) {
            file.open(filename);
            if (!file.is_open()) {
                errorReporter.report(ErrorCode::NS_00_FileError, "Não foi possível abrir o módulo importado: " + targetPath.string(), stringToken.line);
                return {};
            }
            targetPath = filename;
        }

        std::vector<std::string> lines;
        std::string line;
        while (std::getline(file, line)) {
            lines.push_back(line);
        }
        file.close();

        Lexer subLexer(lines, errorReporter);
        std::vector<Token> subTokens = subLexer.scanTokens();
        
        Parser subParser(subTokens, errorReporter, targetPath.parent_path(), loadedFiles);
        return subParser.parse();
    }

public:
    Parser(std::vector<Token> tokens, ErrorReporter& reporter, 
           std::filesystem::path currentDir = std::filesystem::current_path(),
           std::shared_ptr<std::unordered_set<std::string>> loadedFiles = nullptr)
        : loadedFiles(loadedFiles ? loadedFiles : std::make_shared<std::unordered_set<std::string>>()),
          currentDir(currentDir),
          tokens(std::move(tokens)), 
          errorReporter(reporter) {}

    std::vector<std::unique_ptr<Stmt>> parse() {
        std::vector<std::unique_ptr<Stmt>> statements;
        while (!isAtEnd()) {
            parseStatementsInto(statements);
        }
        return statements;
    }

private:
    std::vector<std::unique_ptr<Stmt>> parseBlock() {
        std::vector<std::unique_ptr<Stmt>> statements;
        while (!check(NsTokenType::RBRACE) && !isAtEnd()) {
            parseStatementsInto(statements);
        }
        consume(NsTokenType::RBRACE, ErrorCode::NS_03_SyntaxError, "Esperado '}' para fechar o bloco.");
        return statements;
    }

    std::unique_ptr<Stmt> parseDeclaration() {
        if (isTypeToken() || isCustomTypeDeclaration()) {
            std::string typeStr = parseTypeName();
            return parseVarDeclaration(typeStr);
        } 
        if (check(NsTokenType::IDENTIFIER) && peek().lexeme == "extern") {
            advance();
            if (!match(NsTokenType::KEYWORD_FN)) {
                errorReporter.report(ErrorCode::NS_03_SyntaxError,
                                     "Esperado 'fn' após 'extern'.", peek().line);
                return nullptr;
            }
            return parseExternFunctionDeclaration();
        }
        if (match(NsTokenType::KEYWORD_FN))     return parseFunctionDeclaration();
        if (match(NsTokenType::KEYWORD_STRUCT)) return parseStructDeclaration();
        if (match(NsTokenType::KEYWORD_FOR))    return parseForStatement();
        if (match(NsTokenType::KEYWORD_WHILE))   return parseWhileStatement();
        if (match(NsTokenType::KEYWORD_RETURN))  return parseReturnStatement();
        if (match(NsTokenType::KEYWORD_IF))      return parseIfStatement();

        auto stmt = parseStatement();
        if (stmt) return stmt;

        Token t = peek();
        errorReporter.report(ErrorCode::NS_05_UnexpectedToken, "Token inesperado ou instrução inválida no escopo global.", t.line);
        synchronize();
        return nullptr;
    }

    std::unique_ptr<Stmt> parseStructDeclaration() {
        Token name = consume(NsTokenType::IDENTIFIER, ErrorCode::NS_03_SyntaxError,
                             "Esperado o nome da struct.");
        consume(NsTokenType::LBRACE, ErrorCode::NS_03_SyntaxError,
                "Esperado '{' após o nome da struct.");

        std::vector<std::pair<std::string, Token>> fields;
        while (!check(NsTokenType::RBRACE) && !isAtEnd()) {
            std::string fieldType = parseTypeName();
            Token fieldName = consume(NsTokenType::IDENTIFIER, ErrorCode::NS_03_SyntaxError,
                                      "Esperado o nome do campo da struct.");
            fields.push_back({fieldType, fieldName});
            match(NsTokenType::SEMICOLON);
        }
        consume(NsTokenType::RBRACE, ErrorCode::NS_03_SyntaxError,
                "Esperado '}' para fechar a struct.");
        return std::make_unique<StructDeclStmt>(name, std::move(fields));
    }

    std::unique_ptr<Stmt> parseVarDeclaration(const std::string& dataType) {
        Token name = consume(NsTokenType::IDENTIFIER, ErrorCode::NS_03_SyntaxError, "Esperado o nome da variável.");
        consume(NsTokenType::ASSIGN, ErrorCode::NS_03_SyntaxError, "Esperado '=' na atribuição da variável.");
        std::unique_ptr<Expr> initializer = parseExpression();
        return std::make_unique<VarDeclStmt>(dataType, name, std::move(initializer));
    }

    std::unique_ptr<Stmt> parseFunctionDeclaration() {
        Token name = consume(NsTokenType::IDENTIFIER, ErrorCode::NS_03_SyntaxError, "Esperado o nome da função após 'fn'.");
        consume(NsTokenType::LPAREN, ErrorCode::NS_03_SyntaxError, "Esperado '(' após o nome da função.");
        
        std::vector<std::pair<std::string, Token>> parameters;
        if (!check(NsTokenType::RPAREN)) {
            do {
                std::string paramType = "";
                if (isTypeToken() ||
                    (check(NsTokenType::IDENTIFIER) && current + 1 < tokens.size() &&
                     tokens[current + 1].tokenType == NsTokenType::COLON)) {
                    paramType = parseTypeName();
                    consume(NsTokenType::COLON, ErrorCode::NS_03_SyntaxError, "Esperado ':' após o tipo do parâmetro.");
                }
                Token paramName = consume(NsTokenType::IDENTIFIER, ErrorCode::NS_03_SyntaxError, "Esperado o nome do parâmetro.");
                parameters.push_back({paramType, paramName});
            } while (match(NsTokenType::COMMA));
        }

        consume(NsTokenType::RPAREN, ErrorCode::NS_03_SyntaxError, "Esperado ')' após os parâmetros da função.");

        std::string returnType = "void";
        if (match(NsTokenType::ARROW)) {
            if (isTypeToken() || check(NsTokenType::IDENTIFIER)) {
                returnType = parseTypeName();
            } else {
                errorReporter.report(ErrorCode::NS_05_UnexpectedToken, "Esperado tipo de retorno válido após '->'.", peek().line);
            }
        }

        consume(NsTokenType::LBRACE, ErrorCode::NS_03_SyntaxError, "Esperado '{' para iniciar o corpo da função.");
        std::vector<std::unique_ptr<Stmt>> body = parseBlock();
        return std::make_unique<FunctionDeclStmt>(name, parameters, returnType, std::move(body));
    }

    std::unique_ptr<Stmt> parseExternFunctionDeclaration() {
        Token name = consume(NsTokenType::IDENTIFIER,
                             ErrorCode::NS_03_SyntaxError,
                             "Esperado o nome da função externa após 'extern fn'.");
        consume(NsTokenType::LPAREN, ErrorCode::NS_03_SyntaxError,
                "Esperado '(' após o nome da função externa.");

        std::vector<std::pair<std::string, Token>> parameters;
        if (!check(NsTokenType::RPAREN)) {
            do {
                std::string paramType;
                if (isTypeToken()) {
                    paramType = parseTypeName();
                    consume(NsTokenType::COLON, ErrorCode::NS_03_SyntaxError,
                            "Esperado ':' após o tipo do parâmetro externo.");
                } else {
                    errorReporter.report(ErrorCode::NS_03_SyntaxError,
                                         "Parâmetros externos precisam de um tipo explícito.",
                                         peek().line);
                }
                Token paramName = consume(NsTokenType::IDENTIFIER,
                                          ErrorCode::NS_03_SyntaxError,
                                          "Esperado o nome do parâmetro externo.");
                parameters.push_back({paramType, paramName});
            } while (match(NsTokenType::COMMA));
        }

        consume(NsTokenType::RPAREN, ErrorCode::NS_03_SyntaxError,
                "Esperado ')' após os parâmetros da função externa.");

        std::string returnType = "void";
        if (match(NsTokenType::ARROW)) {
            if (isTypeToken()) {
                returnType = parseTypeName();
            } else if (check(NsTokenType::IDENTIFIER) && peek().lexeme == "void") {
                advance();
            } else {
                errorReporter.report(ErrorCode::NS_05_UnexpectedToken,
                                     "Esperado tipo de retorno válido após '->'.", peek().line);
            }
        }

        return std::make_unique<ExternFunctionDeclStmt>(
            name, std::move(parameters), std::move(returnType));
    }

    std::unique_ptr<Stmt> parseReturnStatement() {
        Token keyword = tokens[current - 1];
        std::unique_ptr<Expr> value = nullptr;
        if (!check(NsTokenType::RBRACE) && !isAtEnd()) {
            value = parseExpression();
        }
        return std::make_unique<ReturnStmt>(keyword, std::move(value));
    }

    std::unique_ptr<Stmt> parseIfStatement() {
        consume(NsTokenType::LPAREN, ErrorCode::NS_03_SyntaxError, "Esperado '(' após a palavra-chave 'if'.");
        std::unique_ptr<Expr> condition = parseExpression();
        consume(NsTokenType::RPAREN, ErrorCode::NS_03_SyntaxError, "Esperado ')' após a condição do 'if'.");
        
        consume(NsTokenType::LBRACE, ErrorCode::NS_03_SyntaxError, "Esperado '{' para abrir o bloco do 'if'.");
        std::vector<std::unique_ptr<Stmt>> thenBranch = parseBlock();

        std::vector<std::unique_ptr<Stmt>> elseBranch;
        if (match(NsTokenType::KEYWORD_ELSE)) {
            consume(NsTokenType::LBRACE, ErrorCode::NS_03_SyntaxError, "Esperado '{' para abrir o bloco do 'else'.");
            elseBranch = parseBlock();
        }

        return std::make_unique<IfStmt>(std::move(condition), std::move(thenBranch), std::move(elseBranch));
    }

    std::unique_ptr<Stmt> parseWhileStatement() {
        consume(NsTokenType::LPAREN, ErrorCode::NS_03_SyntaxError, "Esperado '(' após a palavra-chave 'while'.");
        std::unique_ptr<Expr> condition = parseExpression();
        consume(NsTokenType::RPAREN, ErrorCode::NS_03_SyntaxError, "Esperado ')' após a condição do 'while'.");
        consume(NsTokenType::LBRACE, ErrorCode::NS_03_SyntaxError, "Esperado '{' para abrir o bloco do 'while'.");
        std::vector<std::unique_ptr<Stmt>> body = parseBlock();
        return std::make_unique<WhileStmt>(std::move(condition), std::move(body));
    }

    std::unique_ptr<Stmt> parseForStatement() {
        consume(NsTokenType::LPAREN, ErrorCode::NS_03_SyntaxError,
                "Esperado '(' após 'for'.");

        std::unique_ptr<Stmt> initializer;
        if (isTypeToken() || isCustomTypeDeclaration()) {
            initializer = parseVarDeclaration(parseTypeName());
        } else {
            errorReporter.report(ErrorCode::NS_03_SyntaxError,
                                 "A inicialização do 'for' deve declarar uma variável.",
                                 peek().line);
            initializer = parseStatement();
        }
        consume(NsTokenType::SEMICOLON, ErrorCode::NS_03_SyntaxError,
                "Esperado ';' após a inicialização do 'for'.");

        std::unique_ptr<Expr> condition = parseExpression();
        consume(NsTokenType::SEMICOLON, ErrorCode::NS_03_SyntaxError,
                "Esperado ';' após a condição do 'for'.");

        std::unique_ptr<Stmt> increment;
        if (check(NsTokenType::IDENTIFIER) && current + 1 < tokens.size() &&
            tokens[current + 1].tokenType == NsTokenType::ASSIGN) {
            Token name = advance();
            advance();
            increment = std::make_unique<VarDeclStmt>(
                "", name, parseExpression());
        } else {
            errorReporter.report(ErrorCode::NS_03_SyntaxError,
                                 "O incremento do 'for' deve ser uma atribuição, como 'i = i + 1'.",
                                 peek().line);
        }
        consume(NsTokenType::RPAREN, ErrorCode::NS_03_SyntaxError,
                "Esperado ')' após o incremento do 'for'.");

        consume(NsTokenType::LBRACE, ErrorCode::NS_03_SyntaxError,
                "Esperado '{' para abrir o bloco do 'for'.");
        std::vector<std::unique_ptr<Stmt>> body = parseBlock();
        return std::make_unique<ForStmt>(
            std::move(initializer), std::move(condition), std::move(increment),
            std::move(body));
    }

    std::unique_ptr<Stmt> parseStatement() {
        if (check(NsTokenType::IDENTIFIER) && peek().lexeme == "print") {
            return parsePrintStatement();
        }
        
        // Adicionar suporte para a função sleep() isolada
        if (check(NsTokenType::IDENTIFIER) && peek().lexeme == "sleep") {
            return parseSleepStatement();
        }

        if (check(NsTokenType::IDENTIFIER) && current + 1 < tokens.size() &&
            tokens[current + 1].tokenType == NsTokenType::LBRACKET) {
            Token name = advance();
            advance();
            std::unique_ptr<Expr> index = parseExpression();
            consume(NsTokenType::RBRACKET, ErrorCode::NS_03_SyntaxError,
                    "Esperado ']' após o índice do array.");
            consume(NsTokenType::ASSIGN, ErrorCode::NS_03_SyntaxError,
                    "Esperado '=' após o índice do array.");
            std::unique_ptr<Expr> value = parseExpression();
            return std::make_unique<ArraySetStmt>(
                name, std::move(index), std::move(value));
        }

        if (check(NsTokenType::IDENTIFIER) && current + 3 < tokens.size() &&
            tokens[current + 1].tokenType == NsTokenType::DOT &&
            tokens[current + 2].tokenType == NsTokenType::IDENTIFIER &&
            tokens[current + 3].tokenType == NsTokenType::ASSIGN) {
            Token name = advance();
            advance();
            Token field = advance();
            advance();
            return std::make_unique<FieldSetStmt>(name, field, parseExpression());
        }

        if (check(NsTokenType::IDENTIFIER) && current + 1 < tokens.size() &&
            tokens[current + 1].tokenType == NsTokenType::LPAREN) {
            std::unique_ptr<Expr> expression = parseExpression();
            if (dynamic_cast<CallExpr*>(expression.get())) {
                return std::make_unique<ExpressionStmt>(std::move(expression));
            }
            errorReporter.report(ErrorCode::NS_03_SyntaxError,
                                 "Esperada uma chamada de função como instrução.", peek().line);
            return nullptr;
        }
        
        if (check(NsTokenType::IDENTIFIER) && current + 1 < tokens.size() && tokens[current + 1].tokenType == NsTokenType::ASSIGN) {
            Token name = advance(); 
            advance();
            std::unique_ptr<Expr> value = parseExpression();
            return std::make_unique<VarDeclStmt>("", name, std::move(value));
        }
        return nullptr;
    }

    std::unique_ptr<Stmt> parseSleepStatement() {
        Token keyword = advance();
        consume(NsTokenType::LPAREN, ErrorCode::NS_03_SyntaxError, "Esperado '(' após 'sleep'.");
        std::unique_ptr<Expr> duration = parseExpression();
        consume(NsTokenType::RPAREN, ErrorCode::NS_03_SyntaxError, "Esperado ')' após a duração do 'sleep'.");
        return std::make_unique<SleepStmt>(std::move(keyword), std::move(duration));
    }

    std::unique_ptr<Stmt> parsePrintStatement() {
        advance(); // Consome 'print'
        consume(NsTokenType::LPAREN, ErrorCode::NS_03_SyntaxError, "Esperado '(' após 'print'.");
        
        std::string message = "";
        std::vector<std::unique_ptr<Expr>> interpolations;

        // Suporte a sintaxe flexível: print("texto"), print(variavel), print("texto", variavel) ou print("texto" [variavel])
        if (!check(NsTokenType::RPAREN)) {
            if (check(NsTokenType::STRING_LITERAL)) {
                message = advance().lexeme;
            } else {
                // Se a expressão inicial for uma variável ou número em vez de string literal
                auto expr = parseExpression();
                if (expr) interpolations.push_back(std::move(expr));
            }

            // Suporte à interpolação por parênteses retos [expr]
            if (match(NsTokenType::LBRACKET)) {
                interpolations.push_back(parseExpression());
                consume(NsTokenType::RBRACKET, ErrorCode::NS_03_SyntaxError, "Esperado ']' após a interpolação.");
            }
            // Suporte à separação por vírgula print(a, b)
            else if (match(NsTokenType::COMMA)) {
                do {
                    auto expr = parseExpression();
                    if (expr) interpolations.push_back(std::move(expr));
                } while (match(NsTokenType::COMMA));
            }
        }

        consume(NsTokenType::RPAREN, ErrorCode::NS_03_SyntaxError, "Esperado ')' após os argumentos do print.");
        return std::make_unique<PrintStmt>(message, std::move(interpolations));
    }

    std::unique_ptr<Expr> parseExpression() {
        return parseOr();
    }

    std::unique_ptr<Expr> parseOr() {
        auto expr = parseAnd();
        while (match(NsTokenType::PIPE_PIPE)) {
            Token op = tokens[current - 1];
            expr = std::make_unique<BinaryExpr>(std::move(expr), op, parseAnd());
        }
        return expr;
    }

    std::unique_ptr<Expr> parseAnd() {
        auto expr = parseEquality();
        while (match(NsTokenType::AMPERSAND_AMPERSAND)) {
            Token op = tokens[current - 1];
            expr = std::make_unique<BinaryExpr>(std::move(expr), op, parseEquality());
        }
        return expr;
    }

    std::unique_ptr<Expr> parseEquality() {
        auto expr = parseComparison();
        while (match(NsTokenType::EQUAL_EQUAL) || match(NsTokenType::BANG_EQUAL)) {
            Token op = tokens[current - 1];
            expr = std::make_unique<BinaryExpr>(std::move(expr), op, parseComparison());
        }
        return expr;
    }

    std::unique_ptr<Expr> parseComparison() {
        auto expr = parseTerm();
        while (match(NsTokenType::GREATER) || match(NsTokenType::GREATER_EQUAL) ||
               match(NsTokenType::LESS) || match(NsTokenType::LESS_EQUAL)) {
            Token op = tokens[current - 1];
            expr = std::make_unique<BinaryExpr>(std::move(expr), op, parseTerm());
        }
        return expr;
    }

    std::unique_ptr<Expr> parseTerm() {
        auto expr = parseFactor();
        while (match(NsTokenType::PLUS) || match(NsTokenType::MINUS)) {
            Token op = tokens[current - 1];
            expr = std::make_unique<BinaryExpr>(std::move(expr), op, parseFactor());
        }
        return expr;
    }

    std::unique_ptr<Expr> parseFactor() {
        auto expr = parseUnary();
        while (match(NsTokenType::STAR) || match(NsTokenType::SLASH) ||
               match(NsTokenType::PERCENT)) {
            Token op = tokens[current - 1];
            expr = std::make_unique<BinaryExpr>(std::move(expr), op, parseUnary());
        }
        return expr;
    }

    std::unique_ptr<Expr> parseUnary() {
        if (match(NsTokenType::BANG) || match(NsTokenType::MINUS)) {
            Token op = tokens[current - 1];
            return std::make_unique<UnaryExpr>(op, parseUnary());
        }
        return parsePostfix();
    }

    std::unique_ptr<Expr> parsePostfix() {
        std::unique_ptr<Expr> expr;
        if (check(NsTokenType::INT_LITERAL) || check(NsTokenType::FLOAT_LITERAL) ||
            check(NsTokenType::STRING_LITERAL) || check(NsTokenType::BOOL_LITERAL)) {
            Token literal = advance();
            expr = std::make_unique<LiteralExpr>(literal.tokenType, literal.lexeme);
        } else if (match(NsTokenType::LBRACKET)) {
            std::vector<std::unique_ptr<Expr>> elements;
            if (!check(NsTokenType::RBRACKET)) {
                do {
                    elements.push_back(parseExpression());
                } while (match(NsTokenType::COMMA));
            }
            consume(NsTokenType::RBRACKET, ErrorCode::NS_03_SyntaxError,
                    "Esperado ']' para fechar o array.");
            expr = std::make_unique<ArrayLiteralExpr>(std::move(elements));
        } else if (check(NsTokenType::IDENTIFIER)) {
            Token name = advance();
            if (match(NsTokenType::LPAREN)) {
                std::vector<std::unique_ptr<Expr>> args;
                if (!check(NsTokenType::RPAREN)) {
                    do {
                        args.push_back(parseExpression());
                    } while (match(NsTokenType::COMMA));
                }
                consume(NsTokenType::RPAREN, ErrorCode::NS_03_SyntaxError,
                        "Esperado ')' após os argumentos da chamada de função.");
                expr = std::make_unique<CallExpr>(name, std::move(args));
            } else {
                expr = std::make_unique<VariableExpr>(name);
            }
        } else if (match(NsTokenType::LPAREN)) {
            expr = parseExpression();
            consume(NsTokenType::RPAREN, ErrorCode::NS_03_SyntaxError,
                    "Esperado ')' após a expressão agrupada.");
        } else {
            return nullptr;
        }

        while (true) {
            if (match(NsTokenType::LBRACKET)) {
                std::unique_ptr<Expr> index = parseExpression();
                consume(NsTokenType::RBRACKET, ErrorCode::NS_03_SyntaxError,
                        "Esperado ']' após o índice do array.");
                expr = std::make_unique<IndexExpr>(std::move(expr), std::move(index));
            } else if (match(NsTokenType::DOT)) {
                Token field = consume(NsTokenType::IDENTIFIER,
                                      ErrorCode::NS_03_SyntaxError,
                                      "Esperado nome do campo após '.'.");
                expr = std::make_unique<FieldExpr>(std::move(expr), field);
            } else {
                break;
            }
        }
        return expr;
    }
};

#endif // PARSER_HPP